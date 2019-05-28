/*
 * OpenVR GLib
 * Copyright 2018 Collabora Ltd.
 * Author: Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XRD_OVERLAY_POINTER_H_
#define XRD_OVERLAY_POINTER_H_

#if !defined (XRD_INSIDE) && !defined (XRD_COMPILATION)
#error "Only <xrd.h> can be included directly."
#endif

#include <glib-object.h>
#include "xrd-overlay-model.h"

#include <openvr-glib.h>

G_BEGIN_DECLS

#define XRD_TYPE_OVERLAY_POINTER xrd_overlay_pointer_get_type()
G_DECLARE_FINAL_TYPE (XrdOverlayPointer, xrd_overlay_pointer, XRD, OVERLAY_POINTER,
                      XrdOverlayModel)

struct _XrdOverlayPointer;

XrdOverlayPointer *xrd_overlay_pointer_new (guint64 controller_index);

G_END_DECLS

#endif /* XRD_OVERLAY_POINTER_H_ */
