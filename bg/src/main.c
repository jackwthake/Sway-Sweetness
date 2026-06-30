#define _POSIX_C_SOURCE 200809L 

#include "wayland_ctx.h"
#include "egl_ctx.h"

#include <shader-works/renderer.h>
#include <shader-works/maths.h>
#include <shader-works/primitives.h>

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Pixel format: GL_RGBA / GL_UNSIGNED_BYTE.
// On little-endian: byte 0=R, byte 1=G, byte 2=B, byte 3=A.
u32 rgb_to_u32(u8 r, u8 g, u8 b) {
  return (u32)r | ((u32)g << 8) | ((u32)b << 16) | (0xFFu << 24);
}

void u32_to_rgb(u32 color, u8 *r, u8 *g, u8 *b) {
  *r = (u8)(color & 0xFF);
  *g = (u8)((color >> 8) & 0xFF);
  *b = (u8)((color >> 16) & 0xFF);
}

static float get_time_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (float)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

#define RENDER_SCALE 5  // render at 1/5 of output resolution
#define MAX_DEPTH    10

// Per-output render state: own framebuffer sized to match the output's aspect ratio.
struct out_render {
  u32        *framebuffer;
  f32        *depthbuffer;
  int         fb_w, fb_h;
  renderer_t  renderer;
  transform_t camera;
};

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

u32 frag_func(u32 input, fragment_context_t *ctx, void *args, usize argc) {
  (void)input; (void)args; (void)argc;

  u8 r = (u8)(110);
  u8 g = (u8)(90);
  u8 b = (u8)(90);
  return default_lighting_frag_shader.func(rgb_to_u32(r, g, b), ctx, args, argc);
}

fragment_shader_t frag = { .func = frag_func, .argv = NULL, .argc = 0, .valid = true};

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

  model_t cube_model = {0};
  generate_cube(&cube_model, make_float3(0.0f, 2.0f, -6.0f),
                make_float3(1.0f, 1.0f, 1.0f));
  cube_model.frag_shader   = &frag;
  cube_model.vertex_shader = &default_vertex_shader;
  cube_model.use_textures  = false;

  // Camera positions — one per output. Adjust to taste.
  ren[0]->camera.position = make_float3(0.0f, 2.0f,  0.0f);  // front
  if (n > 1)
    ren[1]->camera.position = make_float3(4.0f, 3.0f, -2.0f); // side/above

  light_t sun = {
    .is_directional = true,
    .direction      = make_float3(-1.0f, -1.0f, -1.0f),
    .color          = rgb_to_u32(255, 255, 255),
  };

  bool running = true;
  while (running) {
    float t = get_time_ms();

    cube_model.transform.yaw   += 1.0f  * 0.025f;
    cube_model.transform.pitch += 0.5f  * 0.025f;
    cube_model.transform.position.z = (sinf(t / 1000.0f) * 4.0f) - 7.0f;

    for (int i = 0; i < n; i++) {
      if (wl->outputs[i].closed) { running = false; break; }

      struct out_render *r = ren[i];
      for (int j = 0; j < r->fb_w * r->fb_h; j++) {
        r->framebuffer[j] = rgb_to_u32(26, 27, 70);
        r->depthbuffer[j] = FLT_MAX;
      }
      update_camera(&r->renderer, &r->camera);
      render_model(&r->renderer, &r->camera, &cube_model, &sun, 1);

      eglMakeCurrent(egl[i]->dpy, egl[i]->surf, egl[i]->surf, egl[i]->ctx);
      egl_ctx_upload_frame(egl[i], r->framebuffer, r->fb_w, r->fb_h);
      egl_ctx_present(egl[i]);
    }

    wl_display_dispatch_pending(wl->display);
    wl_display_flush(wl->display);
  }

  delete_model(&cube_model);
  for (int i = 0; i < n; i++) {
    egl_ctx_destroy(egl[i]);
    out_render_destroy(ren[i]);
  }
  free(egl);
  free(ren);
  wayland_ctx_destroy(wl);
  return 0;
}
