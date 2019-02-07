/*
 * Xrd GLib
 * Copyright 2018 Collabora Ltd.
 * Author: Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#include "xrd-overlay-client.h"

#include <gdk/gdk.h>
#include <openvr-io.h>
#include <glib/gprintf.h>
#include "xrd-settings.h"
#include <openvr-math.h>

#define SCALE_FACTOR 0.75
#define ANALOG_TRESHOLD 0.000001
#define POLL_RATE_MS 20

G_DEFINE_TYPE (XrdOverlayClient, xrd_overlay_client, G_TYPE_OBJECT)

enum {
  KEYBOARD_PRESS_EVENT,
  CLICK_EVENT,
  MOVE_CURSOR_EVENT,
  LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

static void
xrd_overlay_client_finalize (GObject *gobject);

static void
xrd_overlay_client_class_init (XrdOverlayClientClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = xrd_overlay_client_finalize;

  signals[KEYBOARD_PRESS_EVENT] =
    g_signal_new ("keyboard-press-event",
                   G_TYPE_FROM_CLASS (klass),
                   G_SIGNAL_RUN_LAST,
                   0, NULL, NULL, NULL, G_TYPE_NONE,
                   1, GDK_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);

  signals[CLICK_EVENT] =
    g_signal_new ("click-event",
                   G_TYPE_FROM_CLASS (klass),
                   G_SIGNAL_RUN_LAST,
                   0, NULL, NULL, NULL, G_TYPE_NONE,
                   1, GDK_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);

  signals[MOVE_CURSOR_EVENT] =
    g_signal_new ("move-cursor-event",
                   G_TYPE_FROM_CLASS (klass),
                   G_SIGNAL_RUN_LAST,
                   0, NULL, NULL, NULL, G_TYPE_NONE,
                   1, GDK_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
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

  g_source_remove (self->poll_event_source_id);

  for (uint32_t i = 0; i < OPENVR_CONTROLLER_COUNT; i++)
    {
      g_object_unref (self->pointer_ray[i]);
      g_object_unref (self->pointer_tip[i]);
    }

  g_object_unref (self->wm_actions);

  g_hash_table_unref (self->overlays_to_windows);

  g_object_unref (self->manager);


  g_object_unref (self->context);
  self->context = NULL;

  /* Uploader needs to be freed after context! */
  g_object_unref (self->uploader);

  xrd_settings_destroy_instance ();
}

static void
_action_hand_pose_cb (OpenVRAction            *action,
                      OpenVRPoseEvent         *event,
                      XrdClientController     *controller)
{
  (void) action;
  XrdOverlayClient *self = controller->self;
  xrd_overlay_manager_update_pose (self->manager, &event->pose,
                                   controller->index);

  XrdOverlayPointer *pointer = self->pointer_ray[controller->index];
  xrd_overlay_pointer_move (pointer, &event->pose);
  g_free (event);
}

static void
_action_push_pull_scale_cb (OpenVRAction        *action,
                            OpenVRAnalogEvent   *event,
                            XrdClientController *controller)
{
  (void) action;
  XrdOverlayClient *self = controller->self;

  GrabState *grab_state =
      &self->manager->grab_state[controller->index];

  float x_state = graphene_vec3_get_x (&event->state);
  if (grab_state->overlay && fabs (x_state) > ANALOG_TRESHOLD)
    {
      float factor = x_state * SCALE_FACTOR;
      xrd_overlay_manager_scale (self->manager, grab_state, factor,
                                 POLL_RATE_MS);
    }

  float y_state = graphene_vec3_get_y (&event->state);
  if (grab_state->overlay && fabs (y_state) > ANALOG_TRESHOLD)
    {
      HoverState *hover_state =
        &self->manager->hover_state[controller->index];
      hover_state->distance +=
        self->scroll_to_push_ratio *
        hover_state->distance *
        graphene_vec3_get_y (&event->state) *
        (POLL_RATE_MS / 1000.);

      XrdOverlayPointer *pointer_ray = self->pointer_ray[controller->index];
      xrd_overlay_pointer_set_length (pointer_ray, hover_state->distance);
    }

  g_free (event);
}

static void
_action_grab_cb (OpenVRAction        *action,
                 OpenVRDigitalEvent  *event,
                 XrdClientController *controller)
{
  (void) action;
  XrdOverlayClient *self = controller->self;
  if (event->changed)
    {
      if (event->state == 1)
        xrd_overlay_manager_check_grab (self->manager, controller->index);
      else
        xrd_overlay_manager_check_release (self->manager, controller->index);
    }

  g_free (event);
}

void
_overlay_grab_start_cb (OpenVROverlay              *overlay,
                        OpenVRControllerIndexEvent *event,
                        gpointer                    _self)
{
  (void) overlay;
  XrdOverlayClient *self = _self;

  /* don't grab if this overlay is already grabbed */
  if (xrd_overlay_manager_is_grabbed (self->manager, overlay))
    {
      g_free (event);
      return;
    }

  xrd_overlay_manager_drag_start (self->manager, event->index);

  g_free (event);
}

void
_overlay_grab_cb (OpenVROverlay   *overlay,
                  OpenVRGrabEvent *event,
                  gpointer        _self)
{
  (void) overlay;
  XrdOverlayClient *self = (XrdOverlayClient*) _self;

  XrdOverlayPointerTip *pointer_tip =
    self->pointer_tip[event->controller_index];
  openvr_overlay_set_transform_absolute (OPENVR_OVERLAY (pointer_tip),
                                         &event->pose);

  xrd_overlay_pointer_tip_set_constant_width (pointer_tip);
  g_free (event);
}

void
_overlay_unmark (OpenVROverlay *overlay)
{
  graphene_vec3_t unmarked_color;
  graphene_vec3_init (&unmarked_color, 1.f, 1.f, 1.f);
  openvr_overlay_set_color (overlay, &unmarked_color);
}

void
_overlay_mark_orange (OpenVROverlay *overlay)
{
  graphene_vec3_t marked_color;
  graphene_vec3_init (&marked_color, .8f, .4f, .2f);
  openvr_overlay_set_color (overlay, &marked_color);
}

void
_button_hover_cb (OpenVROverlay    *overlay,
                  OpenVRHoverEvent *event,
                  gpointer         _self)
{
  XrdOverlayClient *self = _self;

  _overlay_mark_orange (overlay);

  XrdOverlayPointer *pointer =
      self->pointer_ray[event->controller_index];
  XrdOverlayPointerTip *pointer_tip =
      self->pointer_tip[event->controller_index];

  /* update pointer length and intersection overlay */
  graphene_matrix_t overlay_pose;
  openvr_overlay_get_transform_absolute (overlay, &overlay_pose);

  xrd_overlay_pointer_tip_update (pointer_tip, &overlay_pose, &event->point);
  xrd_overlay_pointer_set_length (pointer, event->distance);
  g_free (event);
}

void
_hover_end_cb (OpenVROverlay              *overlay,
               OpenVRControllerIndexEvent *event,
               gpointer                   _self)
{
  (void) event;
  XrdOverlayClient *self = (XrdOverlayClient*) _self;

  XrdOverlayPointer *pointer_ray = self->pointer_ray[event->index];
  xrd_overlay_pointer_reset_length (pointer_ray);

  /* unmark if no controller is hovering over this overlay */
  if (!xrd_overlay_manager_is_hovered (self->manager, overlay))
    _overlay_unmark (overlay);

  /* When leaving this overlay and immediately entering another, the tip should
   * still be active because it is now hovering another overlay. */
  gboolean active = self->manager->hover_state[event->index].overlay != NULL;

  XrdOverlayPointerTip *pointer_tip = self->pointer_tip[event->index];
  xrd_overlay_pointer_tip_set_active (pointer_tip, active);

  xrd_input_synth_reset_press_state (self->input_synth);

  g_free (event);
}

/* 3DUI buttons */
gboolean
_init_button (XrdOverlayClient   *self,
              XrdOverlayButton  **button,
              gchar              *id,
              gchar              *label,
              graphene_point3d_t *position,
              GCallback           callback)
{
  graphene_matrix_t transform;
  graphene_matrix_init_translate (&transform, position);

  *button = xrd_overlay_button_new (id, label);
  if (*button == NULL)
    return FALSE;

  OpenVROverlay *overlay = OPENVR_OVERLAY (*button);

  openvr_overlay_set_transform_absolute (overlay, &transform);

  xrd_overlay_manager_add_overlay (self->manager, overlay,
                                   OPENVR_OVERLAY_HOVER);

  if (!openvr_overlay_set_width_meters (overlay, 0.5f))
    return FALSE;

  g_signal_connect (overlay, "grab-start-event", (GCallback) callback, self);
  g_signal_connect (overlay, "hover-event", (GCallback) _button_hover_cb, self);
  g_signal_connect (overlay, "hover-end-event",
                    (GCallback) _hover_end_cb, self);

  return TRUE;
}

void
_button_sphere_press_cb (OpenVROverlay             *overlay,
                         OpenVRControllerIndexEvent *event,
                         gpointer                  _self)
{
  (void) event;
  (void) overlay;
  XrdOverlayClient *self = _self;
  xrd_overlay_manager_arrange_sphere (self->manager);
  g_free (event);
}

void
_button_reset_press_cb (OpenVROverlay              *overlay,
                        OpenVRControllerIndexEvent *event,
                        gpointer                   _self)
{
  (void) event;
  (void) overlay;
  XrdOverlayClient *self = _self;
  xrd_overlay_manager_arrange_reset (self->manager);
  g_free (event);
}

gboolean
_init_buttons (XrdOverlayClient *self)
{
  graphene_point3d_t position_reset = {
    .x =  0.0f,
    .y =  0.0f,
    .z = -1.0f
  };
  if (!_init_button (self, &self->button_reset,
                     "control.reset", "Reset", &position_reset,
                     (GCallback) _button_reset_press_cb))
    return FALSE;

  graphene_point3d_t position_sphere = {
    .x =  0.5f,
    .y =  0.0f,
    .z = -1.0f
  };
  if (!_init_button (self, &self->button_sphere,
                     "control.sphere", "Sphere", &position_sphere,
                     (GCallback) _button_sphere_press_cb))
    return FALSE;

  return TRUE;
}

static void
_keyboard_press_cb (OpenVROverlay    *overlay,
                    GdkEventKey      *event,
                    XrdOverlayClient *self)
{
  (void) overlay;
  g_signal_emit (self, signals[KEYBOARD_PRESS_EVENT], 0, event);
}

static void
_keyboard_close_cb (OpenVROverlay    *overlay,
                    XrdOverlayClient *self)
{
  (void) overlay;
  (void) self;
  g_print ("Keyboard closed\n");
}

static void
_action_show_keyboard_cb (OpenVRAction       *action,
                          OpenVRDigitalEvent *event,
                          XrdOverlayClient   *self)
{
  (void) action;
  if (!event->state && event->changed)
    {
      OpenVRContext *context = openvr_context_get_instance ();
      openvr_context_show_system_keyboard (context);

      g_signal_connect (context, "keyboard-press-event",
                        (GCallback) _keyboard_press_cb, self);
      g_signal_connect (context, "keyboard-close-event",
                        (GCallback) _keyboard_close_cb, self);
    }
}

void
_overlay_hover_cb (OpenVROverlay    *overlay,
                   OpenVRHoverEvent *event,
                   XrdOverlayClient *self)
{
  XrdOverlayWindow *win = g_hash_table_lookup (self->overlays_to_windows,
                                               overlay);

  if (!win)
    {
      g_printerr ("Error: Could not get XrdWindow for overlay %p\n", overlay);
      return;
    }

  /* update pointer length and intersection overlay */
  XrdOverlayPointerTip *pointer_tip =
    self->pointer_tip[event->controller_index];

  graphene_matrix_t overlay_pose;
  openvr_overlay_get_transform_absolute (overlay, &overlay_pose);
  xrd_overlay_pointer_tip_update (pointer_tip, &overlay_pose, &event->point);

  XrdOverlayPointer *pointer = self->pointer_ray[event->controller_index];
  xrd_overlay_pointer_set_length (pointer, event->distance);

  PixelSize size_pixels;
  openvr_overlay_get_size_pixels (overlay, &size_pixels);
  graphene_point_t position_2d;
  if (!openvr_overlay_get_2d_intersection (overlay,
                                          &event->point,
                                          &size_pixels,
                                          &position_2d))
    return;

  xrd_input_synth_move_cursor (self->input_synth, win, &position_2d);

  if (self->hover_window != win)
    xrd_input_synth_reset_scroll (self->input_synth);

  self->hover_window = win;
}

void
_overlay_hover_start_cb (OpenVROverlay              *overlay,
                         OpenVRControllerIndexEvent *event,
                         XrdOverlayClient           *self)
{
  (void) overlay;
  (void) event;

  XrdOverlayPointerTip *pointer_tip = self->pointer_tip[event->index];
  xrd_overlay_pointer_tip_set_active (pointer_tip, TRUE);

  g_free (event);
}

void
_manager_no_hover_cb (XrdOverlayManager  *manager,
                      OpenVRNoHoverEvent *event,
                      XrdOverlayClient   *self)
{
  (void) manager;

  XrdOverlayPointerTip *pointer_tip =
    self->pointer_tip[event->controller_index];

  XrdOverlayPointer *pointer_ray = self->pointer_ray[event->controller_index];

  graphene_point3d_t distance_translation_point;
  graphene_point3d_init (&distance_translation_point,
                         0.f, 0.f, -pointer_ray->default_length);

  graphene_matrix_t tip_pose;

  graphene_quaternion_t controller_rotation;
  graphene_quaternion_init_from_matrix (&controller_rotation, &event->pose);

  graphene_vec3_t controller_translation;
  openvr_math_matrix_get_translation (&event->pose, &controller_translation);
  graphene_point3d_t controller_translation_point;
  graphene_point3d_init_from_vec3 (&controller_translation_point,
                                   &controller_translation);

  graphene_matrix_init_identity (&tip_pose);
  graphene_matrix_translate (&tip_pose, &distance_translation_point);
  graphene_matrix_rotate_quaternion (&tip_pose, &controller_rotation);
  graphene_matrix_translate (&tip_pose, &controller_translation_point);

  openvr_overlay_set_transform_absolute (OPENVR_OVERLAY (pointer_tip),
                                         &tip_pose);

  xrd_overlay_pointer_tip_set_constant_width (pointer_tip);

  xrd_overlay_pointer_tip_set_active (pointer_tip, FALSE);

  g_free (event);

  xrd_input_synth_reset_scroll (self->input_synth);

  self->hover_window = NULL;
}

XrdOverlayWindow *
xrd_overlay_client_add_window (XrdOverlayClient *self,
                               const char       *title,
                               gpointer          native,
                               uint32_t          width,
                               uint32_t          height)
{
  gchar *window_title = g_strdup (title);
  if (!window_title)
    window_title = g_strdup ("Unnamed Window");

  gchar overlay_id_str [25];
  g_sprintf (overlay_id_str, "xrd-window-%d", self->new_overlay_index);
  self->new_overlay_index++;

  OpenVROverlay *overlay = openvr_overlay_new ();
  openvr_overlay_create (overlay, overlay_id_str, window_title);
  g_free (window_title);

  if (!openvr_overlay_is_valid (overlay))
  {
    g_printerr ("Overlay unavailable.\n");
    return FALSE;
  }

   /* Mouse scale is required for the intersection test */
  openvr_overlay_set_mouse_scale (overlay, width, height);

  XrdOverlayWindow *window = xrd_overlay_window_new ();
  window->native = native;
  window->overlay = overlay;

  /* This has to be 0 for first upload, to trigger resolution change. */
  window->width = width;
  window->height = width;

  openvr_overlay_show (overlay);

  g_hash_table_insert (self->overlays_to_windows, overlay, window);

  xrd_overlay_manager_add_overlay (self->manager, overlay,
                                   OPENVR_OVERLAY_HOVER |
                                   OPENVR_OVERLAY_GRAB |
                                   OPENVR_OVERLAY_DESTROY_WITH_PARENT);
  g_signal_connect (overlay, "grab-start-event",
                    (GCallback) _overlay_grab_start_cb, self);
  g_signal_connect (overlay, "grab-event",
                    (GCallback) _overlay_grab_cb, self);
  // g_signal_connect (overlay, "release-event",
  //                   (GCallback) _overlay_release_cb, self);
  g_signal_connect (overlay, "hover-start-event",
                    (GCallback) _overlay_hover_start_cb, self);
  g_signal_connect (overlay, "hover-event",
                    (GCallback) _overlay_hover_cb, self);
  g_signal_connect (overlay, "hover-end-event",
                    (GCallback) _hover_end_cb, self);

  return window;
}

void
xrd_overlay_client_remove_window (XrdOverlayClient *self,
                                  XrdOverlayWindow *window)
{
  if (!g_hash_table_remove (self->overlays_to_windows, window->overlay))
    {
      g_printerr ("Error: Could not remove overlay, key not found.\n");
      return;
    }

  xrd_overlay_manager_remove_overlay (self->manager, window->overlay);
}

gboolean
xrd_overlay_client_poll_events_cb (gpointer _self)
{
  XrdOverlayClient *self = _self;
  if (!self->context)
    return FALSE;

  openvr_context_poll_event (self->context);

  if (!openvr_action_set_poll (self->wm_actions))
    return FALSE;

  if (xrd_overlay_manager_is_hovering (self->manager) &&
      !xrd_overlay_manager_is_grabbing (self->manager))
    if (!xrd_input_synth_poll_events (self->input_synth))
      return FALSE;

  xrd_overlay_manager_poll_overlay_events (self->manager);

  return TRUE;
}

static void
_update_scroll_to_push (GSettings *settings, gchar *key, gpointer user_data)
{
  XrdOverlayClient *self = user_data;
  self->scroll_to_push_ratio = g_settings_get_double (settings, key);
}

static void
_update_poll_rate (GSettings *settings, gchar *key, gpointer user_data)
{
  XrdOverlayClient *self = user_data;
  if (self->poll_event_source_id != 0)
    g_source_remove (self->poll_event_source_id);
  int poll_rate = g_settings_get_int (settings, key);

  self->poll_event_source_id =
      g_timeout_add (poll_rate, xrd_overlay_client_poll_events_cb, self);
}


static void
_synth_click_cb (XrdInputSynth    *synth,
                 XrdClickEvent    *event,
                 XrdOverlayClient *self)
{
  (void) synth;
  if (self->hover_window)
    {
      event->window = self->hover_window;
      g_signal_emit (self, signals[CLICK_EVENT], 0, event);

      if (event->button == 1)
        {
          HoverState *hover_state =
              &self->manager->hover_state[event->controller_index];
          if (hover_state->overlay != NULL)
            {
              XrdOverlayPointerTip *pointer_tip =
                  self->pointer_tip[event->controller_index];
              xrd_overlay_pointer_tip_animate_pulse (pointer_tip);
            }
        }
    }
}

static void
_synth_move_cursor_cb (XrdInputSynth      *synth,
                       XrdMoveCursorEvent *event,
                       XrdOverlayClient   *self)
{
  (void) synth;
  g_signal_emit (self, signals[MOVE_CURSOR_EVENT], 0, event);
}

static void
xrd_overlay_client_init (XrdOverlayClient *self)
{
  xrd_settings_connect_and_apply (G_CALLBACK (_update_scroll_to_push),
                                  "scroll-to-push-ratio", self);

  self->hover_window = NULL;
  self->new_overlay_index = 0;
  self->poll_event_source_id = 0;

  self->overlays_to_windows = g_hash_table_new_full (g_direct_hash,
                                                     g_direct_equal,
                                                     NULL, g_object_unref);

  self->context = openvr_context_get_instance ();
  if (!openvr_context_init_overlay (self->context))
    {
      g_printerr ("Error: Could not init OpenVR application.\n");
      return;
    }
  if (!openvr_context_is_valid (self->context))
    {
      g_printerr ("Error: OpenVR context is invalid.\n");
      return;
    }

  self->uploader = openvr_overlay_uploader_new ();
  if (!openvr_overlay_uploader_init_vulkan (self->uploader, false))
    g_printerr ("Unable to initialize Vulkan!\n");

  self->manager = xrd_overlay_manager_new ();

  for (int i = 0; i < OPENVR_CONTROLLER_COUNT; i++)
    {
      self->pointer_ray[i] = xrd_overlay_pointer_new (i);
      if (self->pointer_ray[i] == NULL)
        return;

      self->pointer_tip[i] = xrd_overlay_pointer_tip_new (i, self->uploader);
      if (self->pointer_tip[i] == NULL)
        return;

      xrd_overlay_pointer_tip_init_vulkan (self->pointer_tip[i]);
      xrd_overlay_pointer_tip_set_active (self->pointer_tip[i], FALSE);
      openvr_overlay_show (OPENVR_OVERLAY (self->pointer_tip[i]));
    }

  _init_buttons (self);

  self->left.self = self;
  self->left.index = 0;

  self->right.self = self;
  self->right.index = 1;

  if (!openvr_io_load_cached_action_manifest (
        "xrdesktop",
        "/res/bindings",
        "actions.json",
        "bindings_vive_controller.json",
        "bindings_knuckles_controller.json",
        NULL))
    {
      g_print ("Failed to load action bindings!\n");
      return;
    }

  self->wm_actions = openvr_action_set_new_from_url ("/actions/wm");

  openvr_action_set_connect (self->wm_actions, OPENVR_ACTION_POSE,
                             "/actions/wm/in/hand_pose_left",
                             (GCallback) _action_hand_pose_cb, &self->left);
  openvr_action_set_connect (self->wm_actions, OPENVR_ACTION_POSE,
                             "/actions/wm/in/hand_pose_right",
                             (GCallback) _action_hand_pose_cb, &self->right);

  openvr_action_set_connect (self->wm_actions, OPENVR_ACTION_DIGITAL,
                             "/actions/wm/in/grab_window_left",
                             (GCallback) _action_grab_cb, &self->left);
  openvr_action_set_connect (self->wm_actions, OPENVR_ACTION_DIGITAL,
                             "/actions/wm/in/grab_window_right",
                             (GCallback) _action_grab_cb, &self->right);

  openvr_action_set_connect (self->wm_actions, OPENVR_ACTION_ANALOG,
                             "/actions/wm/in/push_pull_scale_left",
                             (GCallback) _action_push_pull_scale_cb,
                            &self->left);
  openvr_action_set_connect (self->wm_actions, OPENVR_ACTION_ANALOG,
                             "/actions/wm/in/push_pull_scale_right",
                             (GCallback) _action_push_pull_scale_cb,
                            &self->right);

  openvr_action_set_connect (self->wm_actions, OPENVR_ACTION_ANALOG,
                             "/actions/wm/in/push_pull_left",
                             (GCallback) _action_push_pull_scale_cb,
                            &self->left);
  openvr_action_set_connect (self->wm_actions, OPENVR_ACTION_ANALOG,
                             "/actions/wm/in/push_pull_right",
                             (GCallback) _action_push_pull_scale_cb,
                            &self->right);

  openvr_action_set_connect (self->wm_actions, OPENVR_ACTION_DIGITAL,
                             "/actions/wm/in/show_keyboard_left",
                             (GCallback) _action_show_keyboard_cb, self);
  openvr_action_set_connect (self->wm_actions, OPENVR_ACTION_DIGITAL,
                             "/actions/wm/in/show_keyboard_right",
                             (GCallback) _action_show_keyboard_cb, self);

  g_signal_connect (self->manager, "no-hover-event",
                    (GCallback) _manager_no_hover_cb, self);

  xrd_settings_connect_and_apply (G_CALLBACK (_update_poll_rate),
                                  "event-update-rate-ms", self);

  self->input_synth = xrd_input_synth_new ();
  g_signal_connect (self->input_synth, "click-event",
                    (GCallback) _synth_click_cb, self);
  g_signal_connect (self->input_synth, "move-cursor-event",
                    (GCallback) _synth_move_cursor_cb, self);
}