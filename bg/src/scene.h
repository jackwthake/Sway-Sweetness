#pragma once

#include <stdbool.h>
#include <shader-works/renderer.h>

struct out_render {
  u32        *framebuffer;
  f32        *depthbuffer;
  int         fb_w, fb_h;
  renderer_t  renderer;
  transform_t camera;
};

typedef struct {
  model_t           cube;
  light_t           sun;
  fragment_shader_t frag;
} scene_t;

scene_t *scene_create(struct out_render **renderers, int num_outs);
void     scene_destroy(scene_t *scene);
void     scene_draw(scene_t *scene, struct out_render **renderers, int num_outs, float time);
