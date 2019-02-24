/*
 * Xrd GLib
 * Copyright 2018 Collabora Ltd.
 * Author: Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XRD_WINDOW_H_
#define XRD_WINDOW_H_

#include <glib-object.h>

#include <openvr-overlay-uploader.h>
#include <gulkan-texture.h>

G_BEGIN_DECLS

#define XRD_TYPE_WINDOW xrd_window_get_type()

#define XRD_WINDOW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), XRD_TYPE_WINDOW, XrdWindow))
#define XRD_WINDOW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), XRD_TYPE_WINDOW, XrdWindowClass))
#define XRD_IS_WINDOW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), XRD_TYPE_WINDOW))
#define XRD_IS_WINDOW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), XRD_TYPE_WINDOW))
#define XRD_WINDOW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), XRD_TYPE_WINDOW, XrdWindowClass))

typedef struct _XrdWindow      XrdWindow;
typedef struct _XrdWindowClass XrdWindowClass;

G_DEFINE_AUTOPTR_CLEANUP_FUNC(XrdWindow, g_object_unref)

struct _XrdWindowClass
{
  GObjectClass parent;

  gboolean
  (*xrd_window_set_transformation_matrix) (XrdWindow *self,
                                           graphene_matrix_t *mat);

  gboolean
  (*xrd_window_get_transformation_matrix) (XrdWindow *self,
                                          graphene_matrix_t *mat);

  void
  (*xrd_window_submit_texture) (XrdWindow *self,
                                OpenVROverlayUploader *uploader,
                                GulkanTexture *texture);

  float
  (*xrd_window_pixel_to_xr_scale) (XrdWindow *self, int pixel);

  gboolean
  (*xrd_window_get_xr_width) (XrdWindow *self, float *meters);

  gboolean
  (*xrd_window_get_xr_height) (XrdWindow *self, float *meters);

  gboolean
  (*xrd_window_get_scaling_factor) (XrdWindow *self, float *factor);

  gboolean
  (*xrd_window_set_scaling_factor) (XrdWindow *self, float factor);

  void
  (*xrd_window_poll_event) (XrdWindow *self);

  gboolean
  (*xrd_window_intersects) (XrdWindow   *self,
                            graphene_matrix_t  *pointer_transformation_matrix,
                            graphene_point3d_t *intersection_point);

  gboolean
  (*xrd_window_intersection_to_window_coords) (XrdWindow   *self,
                                               graphene_point3d_t *intersection_point,
                                               PixelSize          *size_pixels,
                                               graphene_point_t   *window_coords);

  gboolean
  (*xrd_window_intersection_to_offset_center) (XrdWindow *self,
                                               graphene_point3d_t *intersection_point,
                                               graphene_point_t   *offset_center);

  /* TODO: get rid of [OpenVR]ControllerIndexEvent*/
  void
  (*xrd_window_emit_grab_start) (XrdWindow *self,
                                 OpenVRControllerIndexEvent *event);

  void
  (*xrd_window_emit_grab) (XrdWindow *self,
                           OpenVRGrabEvent *event);

  void
  (*xrd_window_emit_release) (XrdWindow *self,
                              OpenVRControllerIndexEvent *event);

  void
  (*xrd_window_emit_hover_end) (XrdWindow *self,
                                OpenVRControllerIndexEvent *event);

  void
  (*xrd_window_emit_hover) (XrdWindow    *self,
                            OpenVRHoverEvent *event);

  void
  (*xrd_window_emit_hover_start) (XrdWindow *self,
                                  OpenVRControllerIndexEvent *event);

  void
  (*xrd_window_add_child) (XrdWindow *self,
                           XrdWindow *child,
                           graphene_point_t *offset_center);

  void
  (*xrd_window_internal_init) (XrdWindow *self);
};

GType xrd_window_get_type (void) G_GNUC_CONST;

struct _XrdWindow
{
  GObject parent;
};

/*
XrdWindow *
xrd_window_new (gchar *window_title, int width, int height, gpointer native);
*/

gboolean
xrd_window_set_transformation_matrix (XrdWindow *self, graphene_matrix_t *mat);

gboolean
xrd_window_get_transformation_matrix (XrdWindow *self, graphene_matrix_t *mat);


/* TODO: More generic class than OpenVROverlayUploader */
void
xrd_window_submit_texture (XrdWindow *self,
                           OpenVROverlayUploader *uploader,
                           GulkanTexture *texture);

float
xrd_window_pixel_to_xr_scale (XrdWindow *self, int pixel);

gboolean
xrd_window_get_xr_width (XrdWindow *self, float *meters);

gboolean
xrd_window_get_xr_height (XrdWindow *self, float *meters);

gboolean
xrd_window_get_scaling_factor (XrdWindow *self, float *factor);

gboolean
xrd_window_set_scaling_factor (XrdWindow *self, float factor);

void
xrd_window_poll_event (XrdWindow *self);

gboolean
xrd_window_intersects (XrdWindow   *self,
                       graphene_matrix_t  *pointer_transformation_matrix,
                       graphene_point3d_t *intersection_point);

gboolean
xrd_window_intersection_to_window_coords (XrdWindow   *self,
                                          graphene_point3d_t *intersection_point,
                                          PixelSize          *size_pixels,
                                          graphene_point_t   *window_coords);

gboolean
xrd_window_intersection_to_offset_center (XrdWindow *self,
                                          graphene_point3d_t *intersection_point,
                                          graphene_point_t   *offset_center);


void
xrd_window_emit_grab_start (XrdWindow *self,
                            OpenVRControllerIndexEvent *event);

void
xrd_window_emit_grab (XrdWindow *self,
                      OpenVRGrabEvent *event);

void
xrd_window_emit_release (XrdWindow *self,
                         OpenVRControllerIndexEvent *event);

void
xrd_window_emit_hover_end (XrdWindow *self,
                           OpenVRControllerIndexEvent *event);

void
xrd_window_emit_hover (XrdWindow    *self,
                       OpenVRHoverEvent *event);

void
xrd_window_emit_hover_start (XrdWindow *self,
                             OpenVRControllerIndexEvent *event);

void
xrd_window_add_child (XrdWindow *self,
                      XrdWindow *child,
                      graphene_point_t *offset_center);


/* TODO: this is a stopgap solution for so children can init a window.
 * Pretty sure there's a more glib like solution. */
void
xrd_window_internal_init (XrdWindow *self);

G_END_DECLS

#endif /* XRD_WINDOW_H_ */
