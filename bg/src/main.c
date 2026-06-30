#define _POSIX_C_SOURCE 200809L

#include "wayland_ctx.h"
#include "egl_ctx.h"
#include "scene.h"

#include <EGL/egl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define RENDER_SCALE  6
#define MAX_DEPTH     10
#define TARGET_FPS    25
#define FRAME_MS      (1000.0f / TARGET_FPS)

static float get_time_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (float)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static struct out_render *out_render_create(struct bg_output *out) {
  struct out_render *r = calloc(1, sizeof(*r));
  r->fb_w = out->width  / RENDER_SCALE;
  r->fb_h = out->height / RENDER_SCALE;
  r->framebuffer = malloc(r->fb_w * r->fb_h * sizeof(u32));
  r->depthbuffer = malloc(r->fb_w * r->fb_h * sizeof(f32));
  init_renderer(&r->renderer, r->fb_w, r->fb_h, 0, 0,
                r->framebuffer, r->depthbuffer, NULL, MAX_DEPTH);
  printf("bg: output %dx%d -> framebuffer %dx%d\n",
         out->width, out->height, r->fb_w, r->fb_h);
  return r;
}

static void out_render_destroy(struct out_render *r) {
  if (!r) return;
  free(r->framebuffer);
  free(r->depthbuffer);
  free(r);
}

int main(void) {
  struct wayland_ctx *wl = wayland_ctx_create();
  if (!wl) return 1;

  int n = wl->n_outputs;
  struct egl_ctx    **egl = calloc(n, sizeof(*egl));
  struct out_render **ren = calloc(n, sizeof(*ren));

  for (int i = 0; i < n; i++) {
    struct bg_output *out = &wl->outputs[i];
    egl[i] = egl_ctx_create(wl->display, out->surface, out->width, out->height);
    ren[i] = out_render_create(out);
    if (!egl[i] || !ren[i]) {
      fprintf(stderr, "bg: setup failed for output %d\n", i);
      return 1;
    }
  }

  scene_t *scene = scene_create(ren, n);

  bool running = true;
  while (running) {
    float frame_start = get_time_ms();

    for (int i = 0; i < n; i++) {
      if (wl->outputs[i].closed) { running = false; break; }
    }
    if (!running) break;

    scene_draw(scene, ren, n, frame_start);

    for (int i = 0; i < n; i++) {
      eglMakeCurrent(egl[i]->dpy, egl[i]->surf, egl[i]->surf, egl[i]->ctx);
      egl_ctx_upload_frame(egl[i], ren[i]->framebuffer, ren[i]->fb_w, ren[i]->fb_h);
      egl_ctx_present(egl[i]);
    }

    wl_display_dispatch_pending(wl->display);
    wl_display_flush(wl->display);

    float elapsed_ms = get_time_ms() - frame_start;
    float remaining_ms = FRAME_MS - elapsed_ms;
    if (remaining_ms > 0.0f) {
      struct timespec ts = {
        .tv_sec  = 0,
        .tv_nsec = (long)(remaining_ms * 1000000.0f),
      };
      nanosleep(&ts, NULL);
    }
  }

  scene_destroy(scene);
  for (int i = 0; i < n; i++) {
    egl_ctx_destroy(egl[i]);
    out_render_destroy(ren[i]);
  }
  free(egl);
  free(ren);
  wayland_ctx_destroy(wl);
  return 0;
}
