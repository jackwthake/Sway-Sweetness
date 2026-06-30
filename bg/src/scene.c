#include "scene.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>

#include <shader-works/maths.h>
#include <shader-works/primitives.h>


u32 rgb_to_u32(u8 r, u8 g, u8 b) {
  return (u32)r | ((u32)g << 8) | ((u32)b << 16) | (0xFFu << 24);
}


void u32_to_rgb(u32 color, u8 *r, u8 *g, u8 *b) {
  *r = (u8)(color & 0xFF);
  *g = (u8)((color >> 8) & 0xFF);
  *b = (u8)((color >> 16) & 0xFF);
}


static u32 frag_func(u32 input, fragment_context_t *ctx, void *args, usize argc) {
  (void)input; (void)args; (void)argc;
  return default_lighting_frag_shader.func(rgb_to_u32(110, 90, 90), ctx, args, argc);
}


scene_t *scene_create(struct out_render **renderers, int num_outs) {
  scene_t *s = calloc(1, sizeof(*s));

  s->frag = (fragment_shader_t){
    .func  = frag_func,
    .argv  = NULL,
    .argc  = 0,
    .valid = true,
  };

  generate_cube(&s->cube, make_float3(0.0f, 2.0f, -6.0f),
                make_float3(1.0f, 1.0f, 1.0f));
  s->cube.frag_shader   = &s->frag;
  s->cube.vertex_shader = &default_vertex_shader;
  s->cube.use_textures  = false;

  s->sun = (light_t){
    .is_directional = true,
    .direction      = make_float3(-1.0f, -1.0f, -1.0f),
    .color          = rgb_to_u32(255, 255, 255),
  };

  renderers[0]->camera.position = make_float3(0.0f, 2.0f, 0.0f);
  if (num_outs > 1)
    renderers[1]->camera.position = make_float3(0.0f, 2.0f, 0.0f);

  return s;
}


void scene_destroy(scene_t *scene) {
  if (!scene) return;
  delete_model(&scene->cube);
  free(scene);
}


void scene_draw(scene_t *s, struct out_render **renderers, int num_outs, float time) {
  s->cube.transform.yaw   += 1.0f * 0.025f;
  s->cube.transform.pitch += 0.5f * 0.025f;
  s->cube.transform.position.z = sinf(time / 1000.0f) * 4.0f - 7.0f;

  for (int i = 0; i < num_outs; i++) {
    struct out_render *r = renderers[i];
    for (int j = 0; j < r->fb_w * r->fb_h; j++) {
      r->framebuffer[j] = rgb_to_u32(6, 7, 20);
      r->depthbuffer[j] = FLT_MAX;
    }
    update_camera(&r->renderer, &r->camera);
    render_model(&r->renderer, &r->camera, &s->cube, &s->sun, 1);
  }
}
