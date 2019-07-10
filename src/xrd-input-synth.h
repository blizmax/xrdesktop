/*
 * xrdesktop
 * Copyright 2018 Collabora Ltd.
 * Author: Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XRD_INPUT_SYNTH_H_
#define XRD_INPUT_SYNTH_H_

#if !defined (XRD_INSIDE) && !defined (XRD_COMPILATION)
#error "Only <xrd.h> can be included directly."
#endif

#include <glib-object.h>

#include <openvr-glib.h>

#include "xrd-window.h"
#include "xrd-overlay-window.h"

G_BEGIN_DECLS

#define XRD_TYPE_INPUT_SYNTH xrd_input_synth_get_type()
G_DECLARE_FINAL_TYPE (XrdInputSynth, xrd_input_synth, XRD, INPUT_SYNTH, GObject)

/**
 * XrdClickEvent:
 * @window: The #XrdWindow that was clicked.
 * @position: A #graphene_point_t 2D screen position for the click.
 * @button: The #int identifier of the mouse button.
 * @state: A #gboolean that is %TRUE when pressed and %FALSE when released.
 * @controller_handle: A #guint64 with the OpenVR handle to the controller.
 *
 * A 2D mouse click event.
 **/
typedef struct XrdClickEvent {
  XrdWindow        *window;
  graphene_point_t *position;
  int               button;
  gboolean          state;
  guint64           controller_handle;
} XrdClickEvent;

/**
 * XrdMoveCursorEvent:
 * @window: The #XrdWindow on which the cursor was moved.
 * @position: A #graphene_point_t with the current 2D screen position.
 * @ignore: A #gboolean wheather the synthesis should be ignored.
 *
 * A 2D mouse move event.
 *
 * Ignoring this events means only updating the cursor position in VR so it
 * does not appear frozen, but don't actually synthesize mouse move events.
 *
 **/
typedef struct XrdMoveCursorEvent {
  XrdWindow *window;
  graphene_point_t *position;
  gboolean ignore;
} XrdMoveCursorEvent;

XrdInputSynth *
xrd_input_synth_new (void);

gboolean
xrd_input_synth_poll_events (XrdInputSynth *self);

void
xrd_input_synth_reset_scroll (XrdInputSynth *self);

void
xrd_input_synth_reset_press_state (XrdInputSynth *self);

void
xrd_input_synth_move_cursor (XrdInputSynth    *self,
                             XrdWindow *window,
                             graphene_matrix_t *controller_pose,
                             graphene_point3d_t *intersection);

guint64
xrd_input_synth_synthing_controller (XrdInputSynth *self);

void
xrd_input_synth_hand_off_to_controller (XrdInputSynth *self,
                                        guint64 controller_handle);


G_END_DECLS

#endif /* XRD_INPUT_SYNTH_H_ */
