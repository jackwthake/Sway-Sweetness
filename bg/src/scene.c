#include "scene.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <sys/sysinfo.h>

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


static u8 clamp_u8(float v) {
  return (u8)(v > 255.0f ? 255.0f : (v < 0.0f ? 0.0f : v));
}


// Map a value from one range to another
float map_range(float value, float old_min, float old_max, float new_min, float new_max) {
  return new_min + (value - old_min) * (new_max - new_min) / (old_max - old_min);
}


#define TERRAIN_SEED  69
#define TERRAIN_SCALE 0.12f
#define TERRAIN_AMP   3.0f

// Mountain ramp: exaggerates height near the far (-z) edge so it reads as a
// distant range instead of a flat horizon.
#define MOUNTAIN_FAR_POWER     3.0f
#define MOUNTAIN_HEIGHT        45.0f
// Fraction of the plane's z-span (0 = near edge, 1 = far edge) before which no
// boost is applied at all - terrain stays exactly flat/base up to this point,
// instead of smoothly creeping up from the near edge. Fog hides most of what's
// beyond this anyway, so there's no point blending in earlier.
#define MOUNTAIN_START_T       0.25f
// Peak placement: a low-frequency mask along x decides where summits sit vs.
// gaps between them; raising it to a power pushes mid-range values toward 0
// so peaks stay distinct instead of one continuous ridge.
#define MOUNTAIN_PEAK_SCALE    0.04f
#define MOUNTAIN_PEAK_CONTRAST 3.0f
// Jagged per-peak detail: ridged fractal noise (self-similar ridges at
// doubling frequency/halving amplitude), sharpened by a power curve.
#define MOUNTAIN_DETAIL_SCALE    0.08f
#define MOUNTAIN_DETAIL_OCTAVES  4
#define MOUNTAIN_DETAIL_CONTRAST 1.5f

static float terrain_height(float x, float z) {
  float h_fbm   = fbm(x * TERRAIN_SCALE, z * TERRAIN_SCALE, 4, TERRAIN_SEED);
  float h_ridge = ridgeNoise(x * TERRAIN_SCALE, z * TERRAIN_SCALE, TERRAIN_SEED);
  float height = (h_fbm * 0.6f + h_ridge * 0.4f) * TERRAIN_AMP;
  return height;
}

// Fractal ridged noise: layers of ridgeNoise at doubling frequency and halving
// amplitude, like fbm() but built from ridges so peaks stay sharp instead of
// smoothing out. Returns ~[0, 1].
static float ridged_fbm(float x, float z, int octaves, int seed) {
  float sum = 0.0f, amp = 1.0f, freq = 1.0f, norm = 0.0f;
  for (int o = 0; o < octaves; o++) {
    sum  += ridgeNoise(x * freq, z * freq, seed + o * 13) * amp;
    norm += amp;
    amp  *= 0.5f;
    freq *= 2.0f;
  }
  return norm > 0.0f ? sum / norm : 0.0f;
}


static void displace_terrain(model_t *m) {
  // Pass 1: base procedural terrain height.
  for (usize i = 0; i < m->num_vertices; i++) {
    float x = m->vertex_data[i].position.x;
    float z = m->vertex_data[i].position.z;
    m->vertex_data[i].position.y = terrain_height(x, z);
  }

  // Pass 2: ramp the far -z edge up into a distant mountain range. far_t goes
  // from 0 at the near edge (max z) to 1 at the far edge (min z). Below
  // MOUNTAIN_START_T the boost is exactly zero (no smoothing toward camera);
  // beyond it, remapped to 0..1 and raised to a power so the rise is
  // concentrated toward the far edge instead of a linear ramp.
  float z_min = FLT_MAX, z_max = -FLT_MAX;
  for (usize i = 0; i < m->num_vertices; i++) {
    float z = m->vertex_data[i].position.z;
    if (z < z_min) z_min = z;
    if (z > z_max) z_max = z;
  }
  float z_range = z_max - z_min;

  for (usize i = 0; i < m->num_vertices; i++) {
    float x = m->vertex_data[i].position.x;
    float z = m->vertex_data[i].position.z;
    float far_t = z_range > 0.0f ? (z_max - z) / z_range : 0.0f;
    if (far_t <= MOUNTAIN_START_T) continue;

    float t = (far_t - MOUNTAIN_START_T) / (1.0f - MOUNTAIN_START_T);

    float peak_raw  = map_range(noise2D(x * MOUNTAIN_PEAK_SCALE, 0.0f, TERRAIN_SEED + 800), -1.0f, 1.0f, 0.0f, 1.0f);
    float peak_mask = powf(peak_raw, MOUNTAIN_PEAK_CONTRAST);
    float detail    = powf(ridged_fbm(x * MOUNTAIN_DETAIL_SCALE, z * MOUNTAIN_DETAIL_SCALE,
                                      MOUNTAIN_DETAIL_OCTAVES, TERRAIN_SEED + 900), MOUNTAIN_DETAIL_CONTRAST);

    m->vertex_data[i].position.y += powf(t, MOUNTAIN_FAR_POWER) * MOUNTAIN_HEIGHT * peak_mask * detail;
  }

  // Pass 3: recompute face + vertex normals to match the displaced heights.
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
    u8 r = clamp_u8(60.f * intensity);
    u8 g = clamp_u8(170.f * intensity);
    u8 b = clamp_u8(70.f * intensity);
    out_col = rgb_to_u32(r, g, b);
  } else if (ctx->world_pos.y < 0.45) {
     // Gravel base color and texture
    float gravel_base = noise2D(x, z, TERRAIN_SEED + 500);
    float gravel_ridge = ridgeNoise(x * 0.7f, z * 0.7f, TERRAIN_SEED + 600);
    float gravel_intensity = map_range(gravel_base, -1.0f, 1.0f, 0.5f, 1.3f) + gravel_ridge * 0.4f;

    // White stone chance (15%)
    float stone_chance = map_range(hash2((int)x, (int)z, TERRAIN_SEED + 700), -1.0f, 1.0f, 0.0f, 1.0f);

    if (stone_chance > 0.95f) {
      // Bright white stones
      float stone_intensity = map_range(stone_chance, 0.85f, 1.0f, 190.0f, 255.0f);
      out_col = rgb_to_u32(clamp_u8(stone_intensity), clamp_u8(stone_intensity), clamp_u8(stone_intensity + 10));
    } else {
      // Light gray gravel
      float gray = 150.0f * gravel_intensity;
      out_col = rgb_to_u32(clamp_u8(gray), clamp_u8(gray), clamp_u8(gray + 15.0f));
    }
  }

  return default_lighting_frag_shader.func(out_col, ctx, args, argc);
}


#define WATER_WAVE_SCALE   0.35f
#define WATER_HEIGHT_SCALE 0.35f
#define WATER_TIME_SCALE   0.00012f
#define WATER_NORMAL_EPS   0.05f

// Raw ripple value (unscaled) shared by the color and height/normal calculations
// below, so they can never drift out of sync with each other.
static float water_wave(float x, float z, float t) {
  return ridgeNoise(x * WATER_WAVE_SCALE, z * WATER_WAVE_SCALE - t, TERRAIN_SEED + 400);
}

static float water_height(float x, float z, float t) {
  return water_wave(x, z, t) * WATER_HEIGHT_SCALE;
}

// Analytic surface normal from the height field's gradient (central differences),
// so shading reacts to the ripples instead of using the plane's flat base normal.
static float3 water_normal(float x, float z, float t) {
  float dHdx = (water_height(x + WATER_NORMAL_EPS, z, t) - water_height(x - WATER_NORMAL_EPS, z, t))
             / (2.0f * WATER_NORMAL_EPS);
  float dHdz = (water_height(x, z + WATER_NORMAL_EPS, t) - water_height(x, z - WATER_NORMAL_EPS, t))
             / (2.0f * WATER_NORMAL_EPS);
  return float3_normalize(make_float3(-dHdx, -1.0f, dHdz));
}


static u32 water_frag_fn(u32 input, fragment_context_t *ctx, void *args, usize argc) {
  (void)input; (void)argc;

  float time = *(float *)args;
  float t    = time * WATER_TIME_SCALE;
  float wave = water_wave(ctx->world_pos.x, ctx->world_pos.z, t);

  float base_r = 40 + wave * 40;
  float base_g = 110 + wave * 60;
  float base_b = 200 + wave * 55;

  float cap = wave > 0.82f ? (wave - 0.82f) / 0.18f : 0.0f;
  u32 out_col = rgb_to_u32(
    clamp_u8(base_r + cap * (240 - base_r)),
    clamp_u8(base_g + cap * (240 - base_g)),
    clamp_u8(base_b + cap * (245 - base_b))
  );

  ctx->normal = water_normal(ctx->world_pos.x, ctx->world_pos.z, t);

  return default_lighting_frag_shader.func(out_col, ctx, args, argc);
}


// Vertex shader for plane ripple effect
float3 plane_ripple_vertex_shader(vertex_context_t *context, void *argv, usize argc) {
  (void)argc;
  float t       = (*(float *)argv) * WATER_TIME_SCALE;
  float3 vertex = context->original_vertex;

  vertex.y = water_height(vertex.x, vertex.z, t);

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
    .direction      = make_float3(-1.0f, -1.0f, 1.0f),
    .color          = rgb_to_u32(5, 5, 15),
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


#define STAR_SEED 1337
#define SKY_BG_R  6
#define SKY_BG_G  7
#define SKY_BG_B  20

// One star per running process, drawn straight into the framebuffer (no 3D
// geometry involved). Left undrawn in the depth buffer so fog/terrain still
// pass over or occlude them normally.
static void draw_stars(struct out_render *r, int star_start, int star_end) {
  int sky_h = r->fb_h / 2;
  if (sky_h <= 0 || r->fb_w <= 0) return;

  for (int i = star_start; i < star_end; i++) {
    float rx = map_range(hash2(i, 0, STAR_SEED), -1.0f, 1.0f, 0.0f, 1.0f);
    float ry = map_range(hash2(i, 1, STAR_SEED), -1.0f, 1.0f, 0.0f, 1.0f);
    // Opacity: lerp from the sky background (barely there) to bright white,
    // rather than just clamping brightness to a narrow band.
    float opacity = map_range(hash2(i, 2, STAR_SEED), -1.0f, 1.0f, 0.0f, 0.6f);

    int x = (int)(rx * (r->fb_w - 1));
    int y = (int)(ry * (sky_h - 1));

    u8 sr = clamp_u8(SKY_BG_R + opacity * (255.0f - SKY_BG_R));
    u8 sg = clamp_u8(SKY_BG_G + opacity * (255.0f - SKY_BG_G));
    u8 sb = clamp_u8(SKY_BG_B + opacity * (255.0f - SKY_BG_B));
    r->framebuffer[y * r->fb_w + x] = rgb_to_u32(sr, sg, sb);
  }
}


void scene_draw(scene_t *s, struct out_render **renderers, int num_outs, float time) {
  s->time = time;

  struct sysinfo sys_info;
  int num_stars = (sysinfo(&sys_info) == 0) ? (int)sys_info.procs : 0;

  // Split the star field across outputs (one star per process overall,
  // not per screen) rather than drawing the full field on each.
  int base_share = num_outs > 0 ? num_stars / num_outs : 0;
  int remainder  = num_outs > 0 ? num_stars % num_outs : 0;
  int star_start = 0;

  for (int i = 0; i < num_outs; i++) {
    struct out_render *r = renderers[i];
    for (int j = 0; j < r->fb_w * r->fb_h; j++) {
      r->framebuffer[j] = rgb_to_u32(SKY_BG_R, SKY_BG_G, SKY_BG_B);
      r->depthbuffer[j] = FLT_MAX;
    }

    int share = base_share + (i < remainder ? 1 : 0);
    draw_stars(r, star_start, star_start + share);
    star_start += share;

    update_camera(&r->renderer, &r->camera);
    render_model(&r->renderer, &r->camera, &s->ground_plane, &s->sun, 1);
    render_model(&r->renderer, &r->camera, &s->water_plane, &s->sun, 1);
    apply_fog_to_screen(&r->renderer, 2, 20, 10, 10, 30);
  }
}
