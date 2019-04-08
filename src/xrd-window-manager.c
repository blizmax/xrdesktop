/*
 * OpenVR GLib
 * Copyright 2018 Collabora Ltd.
 * Author: Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#include <gdk/gdk.h>
#include <math.h>

#include "xrd-window-manager.h"
#include "openvr-math.h"
#include "xrd-math.h"
#include "graphene-ext.h"

#include "xrd-follow-head-container.h"

struct _XrdWindowManager
{
  GObject parent;

  GSList *draggable_windows;
  GSList *managed_windows;
  GSList *hoverable_windows;
  GSList *destroy_windows;
  GSList *following;

  HoverState hover_state[OPENVR_CONTROLLER_COUNT];
  GrabState grab_state[OPENVR_CONTROLLER_COUNT];

  GHashTable *reset_transforms;
  GHashTable *reset_scalings;
};

G_DEFINE_TYPE (XrdWindowManager, xrd_window_manager, G_TYPE_OBJECT)

#define MINIMAL_SCALE_FACTOR 0.01

enum {
  NO_HOVER_EVENT,
  LAST_SIGNAL
};
static guint manager_signals[LAST_SIGNAL] = { 0 };

static void
xrd_window_manager_finalize (GObject *gobject);

static void
xrd_window_manager_class_init (XrdWindowManagerClass *klass)
{
  manager_signals[NO_HOVER_EVENT] =
    g_signal_new ("no-hover-event",
                   G_TYPE_FROM_CLASS (klass),
                   G_SIGNAL_RUN_FIRST,
                   0, NULL, NULL, NULL, G_TYPE_NONE,
                   1, GDK_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);

  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = xrd_window_manager_finalize;
}

void
_free_matrix_cb (gpointer m)
{
  graphene_matrix_free ((graphene_matrix_t*) m);
}

static void
xrd_window_manager_init (XrdWindowManager *self)
{
  self->draggable_windows = NULL;
  self->managed_windows = NULL;
  self->destroy_windows = NULL;
  self->hoverable_windows = NULL;

  self->reset_transforms = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                                  NULL, _free_matrix_cb);
  self->reset_scalings = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                                NULL, g_free);

  for (int i = 0; i < OPENVR_CONTROLLER_COUNT; i++)
    {
      self->hover_state[i].distance = 1.0f;
      self->hover_state[i].window = NULL;
      self->grab_state[i].window = NULL;
    }
}

XrdWindowManager *
xrd_window_manager_new (void)
{
  return (XrdWindowManager*) g_object_new (XRD_TYPE_WINDOW_MANAGER, 0);
}

static void
xrd_window_manager_finalize (GObject *gobject)
{
  XrdWindowManager *self = XRD_WINDOW_MANAGER (gobject);

  g_hash_table_unref (self->reset_transforms);
  g_hash_table_unref (self->reset_scalings);

  g_slist_free_full (self->destroy_windows, g_object_unref);
}

gboolean
_interpolate_cb (gpointer _transition)
{
  TransformTransition *transition = (TransformTransition *) _transition;

  XrdWindow *window = transition->window;

  graphene_matrix_t interpolated;
  openvr_math_matrix_interpolate (&transition->from,
                                  &transition->to,
                                   transition->interpolate,
                                  &interpolated);
  xrd_window_set_transformation_matrix (window, &interpolated);

  float interpolated_scaling =
    transition->from_scaling * (1.0f - transition->interpolate) +
    transition->to_scaling * transition->interpolate;

  /* TODO interpolate scaling instead of width */
  g_object_set (G_OBJECT(window), "scaling-factor", interpolated_scaling, NULL);

  transition->interpolate += 0.03f;

  if (transition->interpolate > 1)
    {
      xrd_window_set_transformation_matrix (window, &transition->to);

      g_object_set (G_OBJECT(window), "scaling-factor",
                    transition->to_scaling, NULL);

      g_object_unref (transition->window);
      g_free (transition);
      return FALSE;
    }

  return TRUE;
}

static gboolean
_is_in_list (GSList *list,
             XrdWindow *window)
{
  GSList *l;
  for (l = list; l != NULL; l = l->next)
    {
      if (l->data == window)
        return TRUE;
    }
  return FALSE;
}

void
xrd_window_manager_arrange_reset (XrdWindowManager *self)
{
  GSList *l;
  for (l = self->managed_windows; l != NULL; l = l->next)
    {
      XrdWindow *window = (XrdWindow *) l->data;

      TransformTransition *transition = g_malloc (sizeof *transition);

      graphene_matrix_t *transform =
        g_hash_table_lookup (self->reset_transforms, window);

      xrd_window_get_transformation_matrix (window, &transition->from);

      float *scaling = g_hash_table_lookup (self->reset_scalings, window);
      transition->to_scaling = *scaling;

      g_object_get (G_OBJECT(window), "scaling-factor", &transition->from_scaling, NULL);

      if (!graphene_matrix_equals (&transition->from, transform))
        {
          transition->interpolate = 0;
          transition->window = window;
          g_object_ref (window);

          graphene_matrix_init_from_matrix (&transition->to, transform);

          g_timeout_add (20, _interpolate_cb, transition);
        }
      else
        {
          g_free (transition);
        }
    }
}

gboolean
xrd_window_manager_arrange_sphere (XrdWindowManager *self)
{
  guint num_overlays = g_slist_length (self->managed_windows);
  uint32_t grid_height = (uint32_t) sqrt((float) num_overlays);
  uint32_t grid_width = (uint32_t) ((float) num_overlays / (float) grid_height);

  while (grid_width * grid_height < num_overlays)
    grid_width++;

  float theta_start = M_PI / 2.0f;
  float theta_end = M_PI - M_PI / 8.0f;
  float theta_range = theta_end - theta_start;
  float theta_step = theta_range / grid_width;

  float phi_start = 0;
  float phi_end = M_PI;
  float phi_range = phi_end - phi_start;
  float phi_step = phi_range / grid_height;

  guint i = 0;

  for (float theta = theta_start; theta < theta_end; theta += theta_step)
    {
      /* TODO: don't need hack 0.01 to check phi range */
      for (float phi = phi_start; phi < phi_end - 0.01; phi += phi_step)
        {
          TransformTransition *transition = g_malloc (sizeof *transition);

          float radius = 3.0f;

          float const x = sin (theta) * cos (phi);
          float const y = cos (theta);
          float const z = sin (phi) * sin (theta);

          graphene_matrix_t transform;

          graphene_vec3_t position;
          graphene_vec3_init (&position,
                              x * radius,
                              y * radius,
                              z * radius);

          graphene_matrix_init_look_at (&transform,
                                        &position,
                                        graphene_vec3_zero (),
                                        graphene_vec3_y_axis ());

          XrdWindow *window =
              (XrdWindow *) g_slist_nth_data (self->managed_windows, i);

          if (window == NULL)
            {
              g_printerr ("Window %d does not exist!\n", i);
              return FALSE;
            }

          xrd_window_get_transformation_matrix (window, &transition->from);

          g_object_get (G_OBJECT(window), "scaling-factor", &transition->from_scaling, NULL);

          if (!graphene_matrix_equals (&transition->from, &transform))
            {
              transition->interpolate = 0;
              transition->window = window;
              g_object_ref (window);

              graphene_matrix_init_from_matrix (&transition->to, &transform);

              float *scaling = g_hash_table_lookup (self->reset_scalings, window);
              transition->to_scaling = *scaling;

              g_timeout_add (20, _interpolate_cb, transition);
            }
          else
            {
              g_free (transition);
            }

          i++;
          if (i > num_overlays)
            return TRUE;
        }
    }

  return TRUE;
}

void
xrd_window_manager_save_reset_transform (XrdWindowManager *self,
                                         XrdWindow *window)
{
  graphene_matrix_t *transform =
    g_hash_table_lookup (self->reset_transforms, window);
  xrd_window_get_transformation_matrix (window, transform);

  float *scaling = g_hash_table_lookup (self->reset_scalings, window);
  g_object_get (G_OBJECT(window), "scaling-factor", scaling, NULL);
}

void
xrd_window_manager_add_window (XrdWindowManager *self,
                               XrdWindow *window,
                               XrdWindowFlags flags)
{
  /* Freed with manager */
  if (flags & XRD_WINDOW_DESTROY_WITH_PARENT)
    self->destroy_windows = g_slist_append (self->destroy_windows, window);

  /* Movable overlays (user can move them) */
  if (flags & XRD_WINDOW_DRAGGABLE)
    self->draggable_windows = g_slist_append (self->draggable_windows, window);

  /* Managed overlays (window manager can move them) */
  if (flags & XRD_WINDOW_MANAGED)
    self->managed_windows = g_slist_append (self->managed_windows, window);

  /* All windows that can be hovered, includes button windows */
  if (flags & XRD_WINDOW_HOVERABLE)
    self->hoverable_windows = g_slist_append (self->hoverable_windows, window);

  if (flags & XRD_WINDOW_FOLLOW_HEAD)
    {
      XrdFollowHeadContainer *fhc = xrd_follow_head_container_new ();
      float distance = xrd_math_hmd_window_distance (window);
      xrd_follow_head_container_set_window (fhc, window, distance);

      self->following = g_slist_append (self->following, fhc);
    }

  /* Register reset position */
  graphene_matrix_t *transform = graphene_matrix_alloc ();
  xrd_window_get_transformation_matrix (window, transform);
  g_hash_table_insert (self->reset_transforms, window, transform);

  float *scaling = (float*) g_malloc (sizeof (float));
  g_object_get (G_OBJECT(window), "scaling-factor", scaling, NULL);
  g_hash_table_insert (self->reset_scalings, window, scaling);

  g_object_ref (window);
}

void
xrd_window_manager_poll_window_events (XrdWindowManager *self)
{
  for (GSList *l = self->hoverable_windows; l != NULL; l = l->next)
    {
      XrdWindow *window = (XrdWindow *) l->data;
      xrd_window_poll_event (window);
    }

  for (GSList *l = self->following; l != NULL; l = l->next)
    {
      XrdFollowHeadContainer *fhc = (XrdFollowHeadContainer *) l->data;
      xrd_follow_head_container_step (fhc);
    }
}

void
xrd_window_manager_remove_window (XrdWindowManager *self,
                                  XrdWindow *window)
{
  self->destroy_windows = g_slist_remove (self->destroy_windows, window);
  self->draggable_windows = g_slist_remove (self->draggable_windows, window);
  self->managed_windows = g_slist_remove (self->managed_windows, window);
  self->hoverable_windows = g_slist_remove (self->hoverable_windows, window);

  for (GSList *l = self->following; l != NULL; l = l->next) {
    XrdFollowHeadContainer *fhc = (XrdFollowHeadContainer *) l->data;
    if (xrd_follow_head_container_get_window (fhc) == window)
      {
        self->following = g_slist_remove (self->following, fhc);
        g_object_unref (fhc);
      }
  }
  g_hash_table_remove (self->reset_transforms, window);

  for (int i = 0; i < OPENVR_CONTROLLER_COUNT; i++)
    {
      if (self->hover_state[i].window == XRD_WINDOW (window))
        {
          XrdControllerIndexEvent *hover_end_event =
              g_malloc (sizeof (XrdControllerIndexEvent));
          hover_end_event->index = i;
          xrd_window_emit_hover_end (window, hover_end_event);

          self->hover_state[i].window = NULL;
        }
      if (self->grab_state[i].window == XRD_WINDOW (window))
        self->grab_state[i].window = NULL;
    }

  g_object_unref (window);
}

void
_test_hover (XrdWindowManager  *self,
             graphene_matrix_t *pose,
             int                controller_index)
{
  XrdHoverEvent *hover_event = g_malloc (sizeof (XrdHoverEvent));
  hover_event->distance = FLT_MAX;

  XrdWindow *closest = NULL;

  for (GSList *l = self->hoverable_windows; l != NULL; l = l->next)
    {
      XrdWindow *window = (XrdWindow *) l->data;

      graphene_point3d_t intersection_point;
      if (xrd_window_intersects (window, pose, &intersection_point))
        {
          float distance =
            xrd_math_point_matrix_distance (&intersection_point, pose);
          if (distance < hover_event->distance)
            {
              closest = window;
              hover_event->distance = distance;
              graphene_matrix_init_from_matrix (&hover_event->pose, pose);
              graphene_point3d_init_from_point (&hover_event->point,
                                                &intersection_point);
            }
        }
    }

  HoverState *hover_state = &self->hover_state[controller_index];

  if (closest != NULL)
    {
      /* The recipient of the hover_end event should already see that this
       * overlay is not hovered anymore, so we need to set the hover state
       * before sending the event */
      XrdWindow *last_hovered_window = hover_state->window;
      hover_state->distance = hover_event->distance;
      hover_state->window = closest;
      graphene_matrix_init_from_matrix (&hover_state->pose, pose);

      /* We now hover over an overlay */
      if (closest != last_hovered_window)
        {
          XrdControllerIndexEvent *hover_start_event =
              g_malloc (sizeof (XrdControllerIndexEvent));
          hover_start_event->index = controller_index;
          xrd_window_emit_hover_start (closest, hover_start_event);
        }

      if (closest != last_hovered_window
          && last_hovered_window != NULL)
        {
          XrdControllerIndexEvent *hover_end_event =
              g_malloc (sizeof (XrdControllerIndexEvent));
          hover_end_event->index = controller_index;
          xrd_window_emit_hover_end (last_hovered_window, hover_end_event);
        }

      xrd_window_intersection_to_2d_offset_meter (
        closest, &hover_event->point, &hover_state->intersection_offset);

      hover_event->controller_index = controller_index;
      xrd_window_emit_hover (closest, hover_event);
    }
  else
    {
      /* No intersection was found, nothing is hovered */
      g_free (hover_event);

      /* Emit hover end event only if we had hovered something earlier */
      if (hover_state->window != NULL)
        {
          XrdWindow *last_hovered_window = hover_state->window;
          hover_state->window = NULL;
          XrdControllerIndexEvent *hover_end_event =
              g_malloc (sizeof (XrdControllerIndexEvent));
          hover_end_event->index = controller_index;
          xrd_window_emit_hover_end (last_hovered_window, hover_end_event);
        }

      /* Emit no hover event every time when hovering nothing */
      XrdNoHoverEvent *no_hover_event = g_malloc (sizeof (XrdNoHoverEvent));
      no_hover_event->controller_index = controller_index;
      graphene_matrix_init_from_matrix (&no_hover_event->pose, pose);
      g_signal_emit (self, manager_signals[NO_HOVER_EVENT], 0, no_hover_event);
    }
}

void
_drag_window (XrdWindowManager  *self,
              graphene_matrix_t *pose,
              int                controller_index)
{
  HoverState *hover_state = &self->hover_state[controller_index];
  GrabState *grab_state = &self->grab_state[controller_index];

  graphene_point3d_t controller_translation_point;
  graphene_matrix_get_translation_point3d (pose, &controller_translation_point);
  graphene_quaternion_t controller_rotation;
  graphene_quaternion_init_from_matrix (&controller_rotation, pose);

  graphene_point3d_t distance_translation_point;
  graphene_point3d_init (&distance_translation_point,
                         0.f, 0.f, -hover_state->distance);

  graphene_matrix_t transformation_matrix;
  graphene_matrix_init_identity (&transformation_matrix);

  /* first translate the overlay so that the grab point is the origin */
  graphene_matrix_translate (&transformation_matrix,
                             &grab_state->offset_translation_point);

  XrdGrabEvent *event = g_malloc (sizeof (XrdGrabEvent));
  event->controller_index = controller_index;
  graphene_matrix_init_identity (&event->pose);

  /* then apply the rotation that the overlay had when it was grabbed */
  graphene_matrix_rotate_quaternion (&event->pose,
                                     &grab_state->window_rotation);

  /* reverse the rotation induced by the controller pose when it was grabbed */
  graphene_matrix_rotate_quaternion (
      &event->pose, &grab_state->window_transformed_rotation_neg);

  /* then translate the overlay to the controller ray distance */
  graphene_matrix_translate (&event->pose, &distance_translation_point);

  /*
   * rotate the translated overlay. Because the original controller rotation has
   * been subtracted, this will only add the diff to the original rotation
   */
  graphene_matrix_rotate_quaternion (&event->pose,
                                     &controller_rotation);

  /* and finally move the whole thing so the controller is the origin */
  graphene_matrix_translate (&event->pose, &controller_translation_point);

  /* Apply pointer tip transform to overlay */
  graphene_matrix_multiply (&transformation_matrix,
                            &event->pose,
                            &transformation_matrix);


  xrd_window_set_transformation_matrix (grab_state->window,
                                        &transformation_matrix);

  xrd_window_emit_grab (grab_state->window, event);
}

void
xrd_window_manager_drag_start (XrdWindowManager *self,
                               int               controller_index)
{
  HoverState *hover_state = &self->hover_state[controller_index];
  GrabState *grab_state = &self->grab_state[controller_index];

  if (!_is_in_list (self->draggable_windows, hover_state->window))
    return;

  /* Copy hover to grab state */
  grab_state->window = hover_state->window;

  graphene_quaternion_t controller_rotation;
  graphene_quaternion_init_from_matrix (&controller_rotation,
                                        &hover_state->pose);

  graphene_matrix_t window_transform;
  xrd_window_get_transformation_matrix (grab_state->window, &window_transform);
  graphene_quaternion_init_from_matrix (
      &grab_state->window_rotation, &window_transform);

  graphene_point3d_t distance_translation_point;
  graphene_point3d_init (&distance_translation_point,
                         0.f, 0.f, -hover_state->distance);

  graphene_point3d_t negative_distance_translation_point;
  graphene_point3d_init (&negative_distance_translation_point,
                         0.f, 0.f, +hover_state->distance);

  graphene_point3d_init (
      &grab_state->offset_translation_point,
      -hover_state->intersection_offset.x,
      -hover_state->intersection_offset.y,
      0.f);

  /* Calculate the inverse of the overlay rotatation that is induced by the
   * controller dragging the overlay in an arc to its current location when it
   * is grabbed. Multiplying this inverse rotation to the rotation of the
   * overlay will subtract the initial rotation induced by the controller pose
   * when the overlay was grabbed.
   */
  graphene_matrix_t target_transformation_matrix;
  graphene_matrix_init_identity (&target_transformation_matrix);
  graphene_matrix_translate (&target_transformation_matrix,
                             &distance_translation_point);
  graphene_matrix_rotate_quaternion (&target_transformation_matrix,
                                     &controller_rotation);
  graphene_matrix_translate (&target_transformation_matrix,
                             &negative_distance_translation_point);
  graphene_quaternion_t transformed_rotation;
  graphene_quaternion_init_from_matrix (&transformed_rotation,
                                        &target_transformation_matrix);
  graphene_quaternion_invert (
      &transformed_rotation,
      &grab_state->window_transformed_rotation_neg);
}

/**
 * xrd_window_manager_scale:
 * @self: The #XrdWindowManager
 * @grab_state: The #GrabState to scale
 * @factor: Scale factor
 * @update_rate_ms: The update rate in ms
 *
 * While dragging a window, scale the window *factor* times per second
 */

void
xrd_window_manager_scale (XrdWindowManager *self,
                          GrabState        *grab_state,
                          float             factor,
                          float             update_rate_ms)
{
  if (grab_state->window == NULL)
    return;
  (void) self;

  float current_factor;
  g_object_get (G_OBJECT(grab_state->window), "scaling-factor", &current_factor, NULL);

  float new_factor = current_factor + current_factor * factor * (update_rate_ms / 1000.);
  /* Don't make the overlay so small it can not be grabbed anymore */
  if (new_factor > MINIMAL_SCALE_FACTOR)
    {
      /* Grab point is relative to overlay center so we can just scale it */
      graphene_point3d_scale (&grab_state->offset_translation_point,
                              1 + factor * (update_rate_ms / 1000.),
                              &grab_state->offset_translation_point);

      g_object_set (G_OBJECT(grab_state->window), "scaling-factor", new_factor, NULL);
    }
}

void
xrd_window_manager_check_grab (XrdWindowManager *self,
                               int               controller_index)
{
  HoverState *hover_state = &self->hover_state[controller_index];

  if (hover_state->window == NULL)
    return;

   XrdControllerIndexEvent *grab_event =
      g_malloc (sizeof (XrdControllerIndexEvent));
  grab_event->index = controller_index;
  xrd_window_emit_grab_start (hover_state->window, grab_event);
}

void
xrd_window_manager_check_release (XrdWindowManager *self,
                                  int               controller_index)
{
  GrabState *grab_state = &self->grab_state[controller_index];

  if (grab_state->window == NULL)
    return;

  XrdControllerIndexEvent *release_event =
      g_malloc (sizeof (XrdControllerIndexEvent));
  release_event->index = controller_index;
  xrd_window_emit_release (grab_state->window, release_event);
  grab_state->window = NULL;
}

void
xrd_window_manager_update_pose (XrdWindowManager  *self,
                                graphene_matrix_t *pose,
                                int                controller_index)
{
  /* Drag test */
  if (self->grab_state[controller_index].window != NULL)
    _drag_window (self, pose, controller_index);
  else
    _test_hover (self, pose, controller_index);
}

gboolean
xrd_window_manager_is_hovering (XrdWindowManager *self)
{
  for (uint32_t i = 0; i < OPENVR_CONTROLLER_COUNT; i++)
    if (self->hover_state[i].window != NULL)
      return TRUE;
  return FALSE;
}

gboolean
xrd_window_manager_is_grabbing (XrdWindowManager *self)
{
  for (uint32_t i = 0; i < OPENVR_CONTROLLER_COUNT; i++)
    if (self->grab_state[i].window != NULL)
      return TRUE;
  return FALSE;
}

gboolean
xrd_window_manager_is_grabbed (XrdWindowManager *self,
                               XrdWindow        *window)
{
  for (uint32_t i = 0; i < OPENVR_CONTROLLER_COUNT; i++)
    if (self->grab_state[i].window == window)
      return TRUE;
  return FALSE;
}

gboolean
xrd_window_manager_is_hovered (XrdWindowManager *self,
                               XrdWindow        *window)
{
  for (uint32_t i = 0; i < OPENVR_CONTROLLER_COUNT; i++)
    if (self->hover_state[i].window == window)
      return TRUE;
  return FALSE;
}

GrabState *
xrd_window_manager_get_grab_state (XrdWindowManager *self,
                                   int controller_index)
{
  return &self->grab_state[controller_index];
}

HoverState *
xrd_window_manager_get_hover_state (XrdWindowManager *self,
                                    int controller_index)
{
  return &self->hover_state[controller_index];
}
