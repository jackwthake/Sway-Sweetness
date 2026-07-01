#include "scene.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>

#include <shader-works/maths.h>
#include <shader-works/primitives.h>

#include <noise.h>
#include <stdio.h>


u32 rgb_to_u32(u8 r, u8 g, u8 b) {
  return (u32)r | ((u32)g << 8) | ((u32)b << 16) | (0xFFu << 24);
}


void u32_to_rgb(u32 color, u8 *r, u8 *g, u8 *b) {
  *r = (u8)(color & 0xFF);
  *g = (u8)((color >> 8) & 0xFF);
  *b = (u8)((color >> 16) & 0xFF);
}


// Map a value from one range to another
float map_range(float value, float old_min, float old_max, float new_min, float new_max) {
  return new_min + (value - old_min) * (new_max - new_min) / (old_max - old_min);
}


#define TERRAIN_SEED  69
#define TERRAIN_SCALE 0.12f
#define TERRAIN_AMP   3.0f

static float terrain_height(float x, float z) {
  float h_fbm   = fbm(x * TERRAIN_SCALE, z * TERRAIN_SCALE, 4, TERRAIN_SEED);
  float h_ridge = ridgeNoise(x * TERRAIN_SCALE, z * TERRAIN_SCALE, TERRAIN_SEED);
  float height = (h_fbm * 0.6f + h_ridge * 0.4f) * TERRAIN_AMP; 
  return height;
}


static void displace_terrain(model_t *m) {
  for (usize i = 0; i < m->num_vertices; i++) {
    float x = m->vertex_data[i].position.x;
    float z = m->vertex_data[i].position.z;
    m->vertex_data[i].position.y = terrain_height(x, z);
  }

  for (usize f = 0; f < m->num_faces; f++) {
    usize base = f * 3;
    float3 a = m->vertex_data[base + 0].position;
    float3 b = m->vertex_data[base + 1].position;
    float3 c = m->vertex_data[base + 2].position;
    float3 n = float3_normalize(float3_cross(float3_sub(c, a), float3_sub(b, a)));
    m->face_normals[f]              = n;
    m->vertex_data[base + 0].normal = n;
    m->vertex_data[base + 1].normal = n;
    m->vertex_data[base + 2].normal = n;
  }
}


static u32 ground_frag_fn(u32 input, fragment_context_t *ctx, void *args, usize argc) {
  (void)input; (void)argc; (void)args;

  u32 out_col = 0x00;
  float check_size = 0.25f;
  float x = floorf(ctx->world_pos.x / check_size);
  float z = floorf(ctx->world_pos.z / check_size);

  float intensity = map_range(noise2D(x, z, TERRAIN_SEED), -1.0f, 1.0f, 0.85f, 1.0f);
  if (ctx->world_pos.y >= 0.45f) {
    u8 r = (u8)(20.f * intensity);
    u8 g = (u8)(40.f * intensity);
    u8 b = (u8)(20.f * intensity);
    out_col = rgb_to_u32(r, g, b);
  } else if (ctx->world_pos.y < 0.45) {
     // Gravel base color and texture
    float gravel_base = noise2D(x, z, TERRAIN_SEED + 500);
    float gravel_ridge = ridgeNoise(x * 0.7f, z * 0.7f, TERRAIN_SEED + 600);
    float gravel_intensity = map_range(gravel_base, -1.0f, 1.0f, 0.5f, 1.3f) + gravel_ridge * 0.4f;

    // White stone chance (15%)
    float stone_chance = map_range(hash2((int)x, (int)z, TERRAIN_SEED + 700), -1.0f, 1.0f, 0.0f, 1.0f);

    if (stone_chance > 0.95f) {
      // dark stones
      float stone_intensity = map_range(stone_chance, 0.85f, 1.0f, 10.0f, 30.0f);
      out_col = rgb_to_u32((u8)stone_intensity, (u8)stone_intensity, (u8)(stone_intensity + 5));
    } else {
      // Gray gravel
      float gray = 40.0f * gravel_intensity;
      out_col = rgb_to_u32(
        (u8)(gray > 255.0f ? 255 : (gray < 0 ? 0 : gray)),
        (u8)(gray > 255.0f ? 255 : (gray < 0 ? 0 : gray)),
        (u8)((gray + 10.0f) > 255.0f ? 255 : ((gray + 10.0f) < 0 ? 0 : (gray + 10.0f)))
      );
    }
  }

  return default_lighting_frag_shader.func(out_col, ctx, args, argc);
}


static u32 water_frag_fn(u32 input, fragment_context_t *ctx, void *args, usize argc) {
  (void)input; (void)argc;

  float time = *(float *)args;
  float t    = time * 0.00012f;
  float wave = ridgeNoise(ctx->world_pos.x * 0.35f,
                          ctx->world_pos.z * 0.35f - t,
                          TERRAIN_SEED + 400);

  float base_r = 10 + wave * 12;
  float base_g = 30 + wave * 18;
  float base_b = 60 + wave * 35;

  float cap = wave > 0.82f ? (wave - 0.82f) / 0.18f : 0.0f;
  u32 out_col = rgb_to_u32(
    (u8)(base_r + cap * (80 - base_r)),
    (u8)(base_g + cap * (80 - base_g)),
    (u8)(base_b + cap * (80 - base_b))
  );

  return default_lighting_frag_shader.func(out_col, ctx, args, argc);
}


// Vertex shader for plane ripple effect
float3 plane_ripple_vertex_shader(vertex_context_t *context, void *argv, usize argc) {
  (void)argc;
  float t       = (*(float *)argv) * 0.00012f;
  float3 vertex = context->original_vertex;

  float wave = ridgeNoise(vertex.x * 0.35f, vertex.z * 0.35f - t, TERRAIN_SEED + 400);
  vertex.y = wave * 0.35f;

  return vertex;
}


scene_t *scene_create(struct out_render **renderers, int num_outs) {
  scene_t *s = calloc(1, sizeof(*s));

  s->ground_frag = (fragment_shader_t){
    .func  = ground_frag_fn,
    .argv  = NULL,
    .argc  = 0,
    .valid = true,
  };

  s->water_frag = (fragment_shader_t){
    .func  = water_frag_fn,
    .argv  = &s->time,
    .argc  = 1,
    .valid = true,
  };

  s->water_vertex = (vertex_shader_t){
    .func = plane_ripple_vertex_shader, 
    .argv = &s->time, 
    .argc = 1,
    .valid = true
  };

  generate_plane(&s->ground_plane, make_float2(64, 48), make_float2(1, 1), make_float3(0, 0, -16));
  displace_terrain(&s->ground_plane);
  s->ground_plane.frag_shader   = &s->ground_frag;
  s->ground_plane.vertex_shader = &default_vertex_shader;
  s->ground_plane.use_textures  = false;

  generate_plane(&s->water_plane, make_float2(64, 48), make_float2(1, 1), make_float3(0, 0, -16));
  s->water_plane.frag_shader   = &s->water_frag;
  s->water_plane.vertex_shader = &s->water_vertex;
  s->water_plane.use_textures  = false;

  s->sun = (light_t){
    .is_directional = true,
    .direction      = make_float3(-1.0f, -1.0f, -1.0f),
    .color          = rgb_to_u32(255, 255, 255),
  };

  renderers[0]->camera.position = make_float3(0.0f, 2.0f, 0.0f);
  if (num_outs > 1) {
    renderers[1]->camera.position = make_float3(0.0f, 2.0f, 0.0f);

    // Seamlessly continue renderer[1]'s view to the right.
    // right edge of r1 + left half-FOV of r0 = yaw that places r0's left edge
    // exactly where r1's right edge ends.
    float r1_right_edge = atanf((float)renderers[1]->fb_w * 0.5f / renderers[1]->renderer.projection_scale);
    float r0_half_hfov  = atanf((float)renderers[0]->fb_w * 0.5f / renderers[0]->renderer.projection_scale);
    renderers[0]->camera.yaw = r1_right_edge + r0_half_hfov + 0.05;
    printf("bg: r0 yaw = %.5f rad (r1 right edge %.5f + r0 half-hfov %.5f)\n",
           renderers[0]->camera.yaw, r1_right_edge, r0_half_hfov);

    float yaw = renderers[0]->camera.yaw;
    renderers[0]->camera.position = make_float3(
      r1_right_edge * sinf(yaw),
      4.15f,
      2.5f * cosf(yaw)
    );
  }

  return s;
}


void scene_destroy(scene_t *scene) {
  if (!scene) return;
  delete_model(&scene->ground_plane);
  delete_model(&scene->water_plane);
  free(scene);
}


void scene_draw(scene_t *s, struct out_render **renderers, int num_outs, float time) {
  s->time = time;
  for (int i = 0; i < num_outs; i++) {
    struct out_render *r = renderers[i];
    for (int j = 0; j < r->fb_w * r->fb_h; j++) {
      r->framebuffer[j] = rgb_to_u32(6, 7, 20);
      r->depthbuffer[j] = FLT_MAX;
    }
    update_camera(&r->renderer, &r->camera);
    render_model(&r->renderer, &r->camera, &s->ground_plane, &s->sun, 1);
    render_model(&r->renderer, &r->camera, &s->water_plane, &s->sun, 1);
    apply_fog_to_screen(&r->renderer, 10, 20, 10, 10, 30);
  }
}
