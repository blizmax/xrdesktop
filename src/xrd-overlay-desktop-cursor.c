/*
 * Xrd GLib
 * Copyright 2019 Collabora Ltd.
 * Author: Christoph Haag <christoph.haag@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#include "xrd-overlay-desktop-cursor.h"

#include "openvr-math.h"
#include "xrd-settings.h"
#include "xrd-math.h"
#include "graphene-ext.h"
#include "xrd-desktop-cursor.h"

struct _XrdOverlayDesktopCursor
{
  OpenVROverlay parent;

  gboolean use_constant_apparent_width;
  /* setting, either absolute size or the apparent size in 3 meter distance */
  float cursor_width_meter;

  /* cached values set by apparent size and used in hotspot calculation */
  float current_cursor_width_meter;

  int hotspot_x;
  int hotspot_y;

  int texture_width;
  int texture_height;
};

static void
xrd_overlay_desktop_cursor_interface_init (XrdDesktopCursorInterface *iface);

G_DEFINE_TYPE_WITH_CODE (XrdOverlayDesktopCursor, xrd_overlay_desktop_cursor, OPENVR_TYPE_OVERLAY,
                         G_IMPLEMENT_INTERFACE (XRD_TYPE_DESKTOP_CURSOR,
                                                xrd_overlay_desktop_cursor_interface_init))

void
xrd_overlay_desktop_cursor_submit_texture (XrdOverlayDesktopCursor *self,
                                           GulkanClient *uploader,
                                           GulkanTexture *texture,
                                           int hotspot_x,
                                           int hotspot_y);

void
xrd_overlay_desktop_cursor_update (XrdOverlayDesktopCursor *self,
                                   XrdWindow               *window,
                                   graphene_point3d_t      *intersection);

void
xrd_overlay_desktop_cursor_show (XrdOverlayDesktopCursor *self);

void
xrd_overlay_desktop_cursor_hide (XrdOverlayDesktopCursor *self);

void
xrd_overlay_desktop_cursor_set_constant_width (XrdOverlayDesktopCursor *self,
                                               graphene_point3d_t *cursor_point);

static void
xrd_overlay_desktop_cursor_finalize (GObject *gobject);

static void
xrd_overlay_desktop_cursor_class_init (XrdOverlayDesktopCursorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = xrd_overlay_desktop_cursor_finalize;
}

void
_set_width (XrdOverlayDesktopCursor *self, float width)
{
  openvr_overlay_set_width_meters (OPENVR_OVERLAY (self), width);
  self->current_cursor_width_meter = width;
}

static void
_update_cursor_width (GSettings *settings, gchar *key, gpointer user_data)
{
  XrdOverlayDesktopCursor *self = user_data;
  self->cursor_width_meter = g_settings_get_double (settings, key);
  _set_width (self, self->cursor_width_meter);
}

static void
_update_use_constant_apparent_width (GSettings *settings, gchar *key,
                                     gpointer user_data)
{
  XrdOverlayDesktopCursor *self = user_data;
  self->use_constant_apparent_width = g_settings_get_boolean (settings, key);
  if (self->use_constant_apparent_width)
    {
      graphene_matrix_t cursor_pose;
      openvr_overlay_get_transform_absolute (OPENVR_OVERLAY(self), &cursor_pose);

      graphene_vec3_t cursor_point_vec;
      graphene_matrix_get_translation_vec3 (&cursor_pose, &cursor_point_vec);
      graphene_point3d_t cursor_point;
      xrd_overlay_desktop_cursor_set_constant_width (self, &cursor_point);
    }
  else
    _set_width (self, self->cursor_width_meter);
}

static void
xrd_overlay_desktop_cursor_init (XrdOverlayDesktopCursor *self)
{
  self->texture_width = 0;
  self->texture_height = 0;

  openvr_overlay_create (OPENVR_OVERLAY (self),
                         "org.xrdesktop.cursor", "XR Desktop Cursor");
  if (!openvr_overlay_is_valid (OPENVR_OVERLAY (self)))
    {
      g_printerr ("Cursor overlay unavailable.\n");
      return;
    }

  xrd_settings_connect_and_apply (G_CALLBACK (_update_cursor_width),
                                  "cursor-width", self);

  xrd_settings_connect_and_apply (G_CALLBACK
                                  (_update_use_constant_apparent_width),
                                  "pointer-tip-apparent-width-is-constant",
                                  self);

  /* pointer ray is MAX, pointer tip is MAX - 1, so cursor is MAX - 2 */
  openvr_overlay_set_sort_order (OPENVR_OVERLAY (self), UINT32_MAX - 2);

  openvr_overlay_show (OPENVR_OVERLAY (self));
}

XrdOverlayDesktopCursor *
xrd_overlay_desktop_cursor_new ()
{
  XrdOverlayDesktopCursor *self =
      (XrdOverlayDesktopCursor*) g_object_new (XRD_TYPE_OVERLAY_DESKTOP_CURSOR, 0);
  return self;
}

void
xrd_overlay_desktop_cursor_submit_texture (XrdOverlayDesktopCursor *self,
                                           GulkanClient *uploader,
                                           GulkanTexture *texture,
                                           int hotspot_x,
                                           int hotspot_y)
{
  openvr_overlay_uploader_submit_frame (OPENVR_OVERLAY_UPLOADER (uploader),
                                        OPENVR_OVERLAY (self), texture);

  self->hotspot_x = hotspot_x;
  self->hotspot_y = hotspot_y;

  self->texture_width = texture->width;
  self->texture_height = texture->height;
}

void
xrd_overlay_desktop_cursor_update (XrdOverlayDesktopCursor *self,
                                   XrdWindow               *window,
                                   graphene_point3d_t      *intersection)
{
  if (self->texture_width == 0 || self->texture_height == 0)
    return;

  /* TODO: first we have to know the size of the cursor at the target  position
   * so we can calculate the hotspot.
   * Setting the size first flickers sometimes a bit.
   * */
  xrd_overlay_desktop_cursor_set_constant_width (self, intersection);

  /* Calculate the position of the cursor in the space of the window it is "on",
   * because the cursor is rotated in 3d to lie on the overlay's plane:
   * Move a point (center of the cursor) from the origin
   * 1) To the offset it has on the overlay it is on.
   *    This places the cursor's center at the target point on the overlay.
   * 2) half the width of the cursor right, half the height down.
   *    This places the upper left corner of the cursor at the target point.
   * 3) the offset of the hotspot up/left.
   *    This places exactly the hotspot at the target point. */

  graphene_point_t offset_2d;
  xrd_window_intersection_to_2d_offset_meter (window, intersection, &offset_2d);

  graphene_point3d_t offset_3d;
  graphene_point3d_init (&offset_3d, offset_2d.x, offset_2d.y, 0);

  graphene_matrix_t transform;
  graphene_matrix_init_translate (&transform, &offset_3d);

  /* TODO: following assumes width==height. Are there non quadratic cursors? */

  graphene_point3d_t cursor_radius;
  graphene_point3d_init (&cursor_radius,
                         self->current_cursor_width_meter / 2.,
                         - self->current_cursor_width_meter / 2., 0);
  graphene_matrix_translate (&transform, &cursor_radius);

  float cursor_hotspot_meter_x = - self->hotspot_x /
      ((float)self->texture_width) * self->current_cursor_width_meter;
  float cursor_hotspot_meter_y = + self->hotspot_y /
      ((float)self->texture_height) * self->current_cursor_width_meter;

  graphene_point3d_t cursor_hotspot;
  graphene_point3d_init (&cursor_hotspot, cursor_hotspot_meter_x,
                         cursor_hotspot_meter_y, 0);
  graphene_matrix_translate (&transform, &cursor_hotspot);

  graphene_matrix_t overlay_transform;
  xrd_window_get_transformation (window, &overlay_transform);
  graphene_matrix_multiply(&transform, &overlay_transform, &transform);

  openvr_overlay_set_transform_absolute (OPENVR_OVERLAY (self), &transform);
}

/* TODO: scene app needs device poses too. Put in openvr_system? */
static gboolean
_get_hmd_pose (graphene_matrix_t *pose)
{
  OpenVRContext *context = openvr_context_get_instance ();
  VRControllerState_t state;
  if (context->system->IsTrackedDeviceConnected(k_unTrackedDeviceIndex_Hmd) &&
      context->system->GetTrackedDeviceClass (k_unTrackedDeviceIndex_Hmd) ==
          ETrackedDeviceClass_TrackedDeviceClass_HMD &&
      context->system->GetControllerState (k_unTrackedDeviceIndex_Hmd,
                                           &state, sizeof(state)))
    {
      /* k_unTrackedDeviceIndex_Hmd should be 0 => posearray[0] */
      TrackedDevicePose_t openvr_pose;
      context->system->GetDeviceToAbsoluteTrackingPose (context->origin, 0,
                                                        &openvr_pose, 1);
      openvr_math_matrix34_to_graphene (&openvr_pose.mDeviceToAbsoluteTracking,
                                        pose);

      return openvr_pose.bDeviceIsConnected &&
             openvr_pose.bPoseIsValid &&
             openvr_pose.eTrackingResult ==
                 ETrackingResult_TrackingResult_Running_OK;
    }
  return FALSE;
}

void
xrd_overlay_desktop_cursor_set_constant_width (XrdOverlayDesktopCursor *self,
                                               graphene_point3d_t *cursor_point)
{
  if (!self->use_constant_apparent_width)
    return;

  graphene_matrix_t hmd_pose;
  gboolean has_pose = _get_hmd_pose (&hmd_pose);
  if (!has_pose)
    {
      _set_width (self, self->cursor_width_meter);
      return;
    }

  graphene_point3d_t hmd_point;
  graphene_matrix_get_translation_point3d (&hmd_pose, &hmd_point);

  float distance = graphene_point3d_distance (cursor_point, &hmd_point, NULL);

  /* divide distance by 3 so the width and the apparent width are the same at
   * a distance of 3 meters. This makes e.g. self->width = 0.3 look decent in
   * both cases at typical usage distances. */
  float new_width = self->cursor_width_meter / 3.0 * distance;

  _set_width (self, new_width);
}

void
xrd_overlay_desktop_cursor_show (XrdOverlayDesktopCursor *self)
{
  openvr_overlay_show (OPENVR_OVERLAY (self));
}

void
xrd_overlay_desktop_cursor_hide (XrdOverlayDesktopCursor *self)
{
  openvr_overlay_hide (OPENVR_OVERLAY (self));
}

static void
xrd_overlay_desktop_cursor_finalize (GObject *gobject)
{
  XrdOverlayDesktopCursor *self = XRD_OVERLAY_DESKTOP_CURSOR (gobject);
  openvr_overlay_destroy (OPENVR_OVERLAY (self));
}

static void
xrd_overlay_desktop_cursor_interface_init (XrdDesktopCursorInterface *iface)
{
  iface->submit_texture = (void*) xrd_overlay_desktop_cursor_submit_texture;
  iface->update = (void*) xrd_overlay_desktop_cursor_update;
  iface->show = (void*) xrd_overlay_desktop_cursor_show;
  iface->hide = (void*) xrd_overlay_desktop_cursor_hide;
  iface->set_constant_width = (void*) xrd_overlay_desktop_cursor_set_constant_width;
}
