/*
 * xrdesktop
 * Copyright 2018 Collabora Ltd.
 * Author: Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#include "xrd-overlay-client.h"

#include <openvr-glib.h>

#include <gdk/gdk.h>
#include <glib/gprintf.h>
#include "xrd-math.h"
#include "xrd-client.h"
#include "graphene-ext.h"
#include "xrd-pointer.h"
#include "xrd-pointer-tip.h"
#include "xrd-overlay-desktop-cursor.h"

struct _XrdOverlayClient
{
  XrdClient parent;

  gboolean pinned_only;
  XrdOverlayWindow *pinned_button;

  OpenVROverlayUploader *uploader;
};

G_DEFINE_TYPE (XrdOverlayClient, xrd_overlay_client, XRD_TYPE_CLIENT)

static void
xrd_overlay_client_finalize (GObject *gobject);

gboolean
xrd_overlay_client_add_button (XrdOverlayClient   *self,
                               XrdWindow         **button,
                               int                 label_count,
                               gchar             **label,
                               graphene_point3d_t *position,
                               GCallback           press_callback,
                               gpointer            press_callback_data);

GulkanClient *
xrd_overlay_client_get_uploader (XrdOverlayClient *self);

static void
xrd_overlay_client_class_init (XrdOverlayClientClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = xrd_overlay_client_finalize;

  XrdClientClass *xrd_client_class = XRD_CLIENT_CLASS (klass);
  xrd_client_class->add_button =
      (void*) xrd_overlay_client_add_button;
  xrd_client_class->get_uploader =
      (void*) xrd_overlay_client_get_uploader;
  xrd_client_class->init_controller =
      (void*) xrd_overlay_client_init_controller;
}

XrdOverlayClient *
xrd_overlay_client_new (void)
{
  return (XrdOverlayClient*) g_object_new (XRD_TYPE_OVERLAY_CLIENT, 0);
}

static void
xrd_overlay_client_finalize (GObject *gobject)
{
  XrdOverlayClient *self = XRD_OVERLAY_CLIENT (gobject);

  G_OBJECT_CLASS (xrd_overlay_client_parent_class)->finalize (gobject);

  /* Uploader needs to be freed after context! */
  g_object_unref (self->uploader);
}

GulkanClient *
xrd_overlay_client_get_uploader (XrdOverlayClient *self)
{
  return GULKAN_CLIENT (self->uploader);
}

gboolean
xrd_overlay_client_add_button (XrdOverlayClient   *self,
                               XrdWindow         **button,
                               int                 label_count,
                               gchar             **label,
                               graphene_point3d_t *position,
                               GCallback           press_callback,
                               gpointer            press_callback_data)
{
  graphene_matrix_t transform;
  graphene_matrix_init_translate (&transform, position);

  int width = 220;
  int height = 220;
  int ppm = 450;

  GulkanClient *client = GULKAN_CLIENT (self->uploader);

  GString *full_label = g_string_new ("");
  for (int i = 0; i < label_count; i++)
    {
      g_string_append (full_label, label[i]);
      if (i < label_count - 1)
        g_string_append (full_label, " ");
    }

  XrdWindow *window =
    XRD_WINDOW (xrd_overlay_window_new_from_ppm (full_label->str,
                                                 width, height, ppm));

  g_string_free (full_label, FALSE);

  if (window == NULL)
    return FALSE;

  VkImageLayout layout = xrd_client_get_upload_layout (XRD_CLIENT (self));

  xrd_button_set_text (window, client, layout, label_count, label);

  *button = window;

  xrd_window_set_transformation (window, &transform);

  XrdWindowManager *manager = xrd_client_get_manager (XRD_CLIENT (self));
  xrd_window_manager_add_window (manager,
                                 *button,
                                 XRD_WINDOW_HOVERABLE |
                                 XRD_WINDOW_DESTROY_WITH_PARENT |
                                 XRD_WINDOW_MANAGER_BUTTON);

  g_signal_connect (window, "grab-start-event",
                    (GCallback) press_callback, press_callback_data);

  xrd_client_add_button_callbacks (XRD_CLIENT (self), window);

  return TRUE;
}

static void
xrd_overlay_client_init (XrdOverlayClient *self)
{
  xrd_client_set_upload_layout (XRD_CLIENT (self),
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

  self->pinned_only = FALSE;
  OpenVRContext *openvr_context =
    xrd_client_get_openvr_context (XRD_CLIENT (self));

  if (!openvr_context_init_overlay (openvr_context))
    {
      g_printerr ("Error: Could not init OpenVR application.\n");
      return;
    }
  if (!openvr_context_is_valid (openvr_context))
    {
      g_printerr ("Error: OpenVR context is invalid.\n");
      return;
    }

  self->uploader = openvr_overlay_uploader_new ();
  if (!openvr_overlay_uploader_init_vulkan (self->uploader, false))
    g_printerr ("Unable to initialize Vulkan!\n");

  xrd_client_post_openvr_init (XRD_CLIENT (self));

  XrdDesktopCursor *cursor =
    XRD_DESKTOP_CURSOR (xrd_overlay_desktop_cursor_new (self->uploader));

  xrd_client_set_desktop_cursor (XRD_CLIENT (self), cursor);
}

void
xrd_overlay_client_init_controller (XrdOverlayClient *self,
                                    XrdController *controller)
{
  guint64 controller_handle = controller->controller_handle;
  controller->pointer_ray =
    XRD_POINTER (xrd_overlay_pointer_new (controller_handle));
  if (controller->pointer_ray == NULL)
    {
      g_printerr ("Error: Could not init pointer %lu\n", controller_handle);
      return;
    }

  controller->pointer_tip =
    XRD_POINTER_TIP (xrd_overlay_pointer_tip_new (controller_handle,
                                                  self->uploader));
  if (controller->pointer_tip == NULL)
    {
      g_printerr ("Error: Could not init pointer tip %lu\n", controller_handle);
      return;
    }

  xrd_pointer_tip_set_active (controller->pointer_tip, FALSE);
  xrd_pointer_tip_show (controller->pointer_tip);
}
