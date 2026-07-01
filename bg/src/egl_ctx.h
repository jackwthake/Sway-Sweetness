#pragma once

#include <EGL/egl.h>
#include <wayland-egl.h>
#include <stdbool.h>
#include <stdint.h>

struct wl_display;
struct wl_surface;

struct egl_ctx {
  EGLDisplay           dpy;
  EGLContext           ctx;
  EGLSurface           surf;
  struct wl_egl_window *egl_window;
  uint32_t             tex;
  uint32_t             prog;
  uint32_t             vbo;
  int                  a_pos;
};

struct egl_ctx *egl_ctx_create(struct wl_display *dpy, struct wl_surface *surf,
                                int width, int height);
// Upload a software framebuffer (GL_RGBA byte order: R,G,B,A) as the blit texture.
void egl_ctx_upload_frame(struct egl_ctx *egl, const uint32_t *pixels,
                           int fb_width, int fb_height);
// Draw the uploaded frame scaled to the full EGL surface and swap buffers.
// Returns false if eglSwapBuffers fails (e.g. the GPU/display connection was
// lost across a suspend/resume) so the caller can tear down and exit.
bool egl_ctx_present(struct egl_ctx *egl);
void egl_ctx_destroy(struct egl_ctx *egl);
