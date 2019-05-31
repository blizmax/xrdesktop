/*
 * xrdesktop
 * Copyright 2019 Collabora Ltd.
 * Author: Christoph Haag <christoph.haag@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XRD_FOLLOW_HEAD_WINDOW_H_
#define XRD_FOLLOW_HEAD_WINDOW_H_

#if !defined (XRD_INSIDE) && !defined (XRD_COMPILATION)
#error "Only <xrd.h> can be included directly."
#endif

#include <glib-object.h>
#include "xrd-window.h"

G_BEGIN_DECLS

#define XRD_TYPE_FOLLOW_HEAD_CONTAINER xrd_follow_head_container_get_type()
G_DECLARE_FINAL_TYPE (XrdFollowHeadContainer, xrd_follow_head_container, XRD, FOLLOW_HEAD_CONTAINER, GObject)

struct _XrdFollowHeadContainer;

XrdFollowHeadContainer*
xrd_follow_head_container_new (void);

void
xrd_follow_head_container_set_window (XrdFollowHeadContainer *self,
                                      XrdWindow *window,
                                      float distance);

XrdWindow*
xrd_follow_head_container_get_window (XrdFollowHeadContainer *self);

float
xrd_follow_head_container_get_distance (XrdFollowHeadContainer *self);

float
xrd_follow_head_container_get_speed (XrdFollowHeadContainer *self);

void
xrd_follow_head_container_set_speed (XrdFollowHeadContainer *self,
                                     float speed);

gboolean
xrd_follow_head_container_step (XrdFollowHeadContainer *fhc);

G_END_DECLS

#endif /* XRD_FOLLOW_HEAD_WINDOW_H_ */
