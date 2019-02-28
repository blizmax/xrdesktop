/*
 * OpenVR GLib
 * Copyright 2018 Collabora Ltd.
 * Author: Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#include <glib.h>
#include <glib-unix.h>
#include <gmodule.h>

#include "xrd-overlay-client.h"
#include <gdk/gdk.h>

#define GRID_WIDTH 6
#define GRID_HEIGHT 5

typedef struct Example
{
  GMainLoop *loop;
  XrdOverlayClient *client;
  GSList *windows;
} Example;

gboolean
_sigint_cb (gpointer _self)
{
  Example *self = (Example*) _self;
  g_main_loop_quit (self->loop);
  return TRUE;
}

GdkPixbuf *
load_gdk_pixbuf (const gchar* name)
{
  GError * error = NULL;
  GdkPixbuf *pixbuf_rgb = gdk_pixbuf_new_from_resource (name, &error);

  if (error != NULL)
    {
      g_printerr ("Unable to read file: %s\n", error->message);
      g_error_free (error);
      return NULL;
    }

  GdkPixbuf *pixbuf = gdk_pixbuf_add_alpha (pixbuf_rgb, false, 0, 0, 0);
  g_object_unref (pixbuf_rgb);
  return pixbuf;
}

static GulkanTexture *
_make_texture (GulkanClient *gc, const gchar *resource, float scale,
               float *texture_width, float *texture_height)
{
  GdkPixbuf *pixbuf = load_gdk_pixbuf (resource);
  if (pixbuf == NULL)
    {
      g_printerr ("Could not load image.\n");
      return FALSE;
    }

  GdkPixbuf *unref = pixbuf;
  pixbuf =
      gdk_pixbuf_scale_simple (pixbuf,
                               (float)gdk_pixbuf_get_width (pixbuf) * scale,
                               (float)gdk_pixbuf_get_height (pixbuf) * scale,
                               GDK_INTERP_NEAREST);

  g_object_unref (unref);

  GulkanTexture *texture =
    gulkan_texture_new_from_pixbuf (gc->device, pixbuf,
                                    VK_FORMAT_R8G8B8A8_UNORM);

  gulkan_client_upload_pixbuf (gc, texture, pixbuf);

  *texture_width = gdk_pixbuf_get_width (pixbuf);
  *texture_height = gdk_pixbuf_get_height (pixbuf);
  g_object_unref (pixbuf);

  return texture;
}

gboolean
_init_windows (Example *self)
{
  GulkanClient *gc = GULKAN_CLIENT (self->client->uploader);
  float texture_width, texture_height;
  GulkanTexture *hawk_big = _make_texture (gc, "/res/hawk.jpg", 0.1,
                                           &texture_width, &texture_height);

  /* TODO: ppm setting */
  double ppm = 300.0;

  float window_x = 0;
  float window_y = 0;
  for (int col = 0; col < GRID_WIDTH; col++)
    {
      float max_window_height = 0;
      for (int row = 0; row < GRID_HEIGHT; row++)
        {
          XrdOverlayWindow *window =
            xrd_overlay_client_add_window (self->client, "A window.", NULL,
                                           FALSE, FALSE);
          self->windows = g_slist_append (self->windows, window);

          xrd_overlay_window_submit_texture (window, self->client->uploader,
                                             hawk_big);

          float window_width;
          xrd_window_get_xr_width (XRD_WINDOW (window), &window_width);
          window_x += window_width;

          float window_height;
          xrd_window_get_xr_height (XRD_WINDOW (window), &window_height);
          if (window_height > max_window_height)
            max_window_height = window_height;

          graphene_point3d_t point = {
            .x = window_x,
            .y = window_y,
            .z = -3
          };
          graphene_matrix_t transform;
          graphene_matrix_init_translate (&transform, &point);
          xrd_overlay_window_set_transformation_matrix (window, &transform);

          xrd_window_manager_save_reset_transform (self->client->manager,
                                                   XRD_WINDOW (window));

          if (col == 0 && row == 0)
            {
              float texture_width, texture_height;
              GulkanTexture *cat_small = _make_texture (gc, "/res/cat.jpg", 0.03,
                                                         &texture_width,
                                                         &texture_height);
              XrdOverlayWindow *child =
                xrd_overlay_client_add_window (self->client, "A child.", NULL,
                                               TRUE, FALSE);
              self->windows = g_slist_append (self->windows, child);

              xrd_overlay_window_submit_texture (child, self->client->uploader,
                                                 cat_small);

              graphene_point_t offset = { .x = 25, .y = 25 };
              xrd_overlay_window_add_child (window, child, &offset);

              /*
              graphene_point3d_t point = {
                .x = x,
                .y = y,
                .z = -2.9
              };
              graphene_matrix_t transform;
              graphene_matrix_init_translate (&transform, &point);
              xrd_overlay_window_set_transformation_matrix (child, &transform);
              */
            }
        }
      window_x = 0;
      window_y += max_window_height;
    }

  {
    XrdOverlayWindow *tracked_window =
        xrd_overlay_client_add_window (self->client, "Head Tracked window.",
                                       NULL, FALSE, TRUE);
    self->windows = g_slist_append (self->windows, tracked_window);

    xrd_overlay_window_submit_texture (tracked_window, self->client->uploader,
                                         hawk_big);
    graphene_point3d_t point = { .x = 0, .y = 1, .z = -1.2 };
    graphene_matrix_t transform;
    graphene_matrix_init_translate (&transform, &point);
    xrd_overlay_window_set_transformation_matrix (tracked_window, &transform);
  }


  GdkPixbuf *cursor_pixbuf = load_gdk_pixbuf ("/res/cursor.png");
  if (cursor_pixbuf == NULL)
    {
      g_printerr ("Could not load image.\n");
      return FALSE;
    }
  xrd_overlay_desktop_cursor_upload_pixbuf (self->client->cursor,
                                            cursor_pixbuf, 3, 3);
  return TRUE;
}

void
_cleanup (Example *self)
{
  g_main_loop_unref (self->loop);
  g_slist_free (self->windows);
  g_object_unref (self->client);
  g_print ("bye\n");
}

static void
_click_cb (XrdOverlayClient *client,
           XrdClickEvent    *event,
           Example          *self)
{
  (void) client;
  (void) self;
  g_print ("click: %f, %f\n",
           event->position->x, event->position->y);
}

/*
static void
_move_cursor_cb (XrdOverlayClient   *client,
                 XrdMoveCursorEvent *event,
                 Example            *self)
{
  (void) client;
  (void) self;
  g_print ("move: %f, %f\n",
           event->position->x, event->position->y);
}
*/

static void
_keyboard_press_cb (XrdOverlayClient *client,
                    GdkEventKey      *event,
                    Example          *self)
{
  (void) client;
  (void) self;
  g_print ("key: %d\n", event->keyval);
}

static void
_request_quit_cb (XrdOverlayClient *client,
                  Example          *self)
{
  (void) client;
  (void) self;
  g_print ("Got quit request from the runtime\n");
  g_main_loop_quit (self->loop);
}

int
main ()
{
  Example self = {
    .loop = g_main_loop_new (NULL, FALSE),
    .client = xrd_overlay_client_new (),
    .windows = NULL
  };

  if (!_init_windows (&self))
    return -1;

  g_unix_signal_add (SIGINT, _sigint_cb, &self);

  g_signal_connect (self.client, "click-event",
                    (GCallback) _click_cb, &self);
  //g_signal_connect (self.client, "move-cursor-event",
  //                  (GCallback) _move_cursor_cb, &self);
  g_signal_connect (self.client, "keyboard-press-event",
                    (GCallback) _keyboard_press_cb, &self);

  g_signal_connect (self.client, "request-quit-event",
                    (GCallback) _request_quit_cb, &self);
  
  /* start glib main loop */
  g_main_loop_run (self.loop);

  _cleanup (&self);

}
