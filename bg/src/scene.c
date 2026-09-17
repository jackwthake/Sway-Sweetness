#include "scene.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "util.h"


scene_t *scene_create(struct out_render **renderers, int num_outs) {
  scene_t *s = calloc(1, sizeof(*s));

  s->lain = convert_bmp_to_framebuffer("assets/lain.bmp", &s->lain_w, &s->lain_h);
  if (!s->lain) {
    free(s);
    return NULL;
  }

  for (int i = 0; i < num_outs; i++) {
    if (is_renderer_horizontal(renderers[i])) {
      s->renderer_horizontal = renderers[i];
    } else {
      s->renderer_vertical = renderers[i];
    }
  }

  return s;
}


void scene_destroy(scene_t *scene) {
  if (!scene) return;
  free(scene->lain);
  free(scene);
}


void render_horizontal(scene_t *s) {
  if (!s->renderer_horizontal) return;
  int lain_x = s->renderer_horizontal->fb_w - (s->lain_w * 0.80f);

  draw_bitmap_to_framebuffer(s->renderer_horizontal->framebuffer, s->renderer_horizontal->fb_w, s->renderer_horizontal->fb_h, s->lain, s->lain_w, s->lain_h, lain_x, 0);

  draw_string_to_framebuffer(s->renderer_horizontal->framebuffer, s->renderer_horizontal->fb_w, s->renderer_horizontal->fb_h, "The quick brown fox jumps over the lazy dog\n1234567890\n\t{},.?/\\!-_=+*&~^\n[]#$%'\";:", 8, 32, 0xFFFAFAFA);
}


void render_vertical(scene_t *s) {
  if (!s->renderer_vertical) return;
  int lain_y = s->renderer_vertical->fb_h - (s->lain_h * 0.60f);
  int lain_x = s->renderer_vertical->fb_w - (s->lain_w * 0.80f);

  draw_bitmap_to_framebuffer(s->renderer_vertical->framebuffer, s->renderer_vertical->fb_w, s->renderer_vertical->fb_h, s->lain, s->lain_w, s->lain_h, lain_x, lain_y);
}


void crt_bloom_screen_shader(u32 *framebuffer, int fb_w, int fb_h, float time) {
  size_t pixel_count = (size_t)fb_w * fb_h;
  u32 *source = malloc(pixel_count * sizeof(*source));
  if (!source) {
    return;
  }
  memcpy(source, framebuffer, pixel_count * sizeof(*source));

  for (int y = 0; y < fb_h; y++) {
    for (int x = 0; x < fb_w; x++) {
      u32 color = source[y * fb_w + x];

      u8 r = (color >> 16) & 0xFF;
      u8 g = (color >> 8) & 0xFF;
      u8 b = color & 0xFF;

      int offset = (int)(1.15f * sinf(time * 0.001f + y * 0.07f));
      int new_x_r = x + offset;
      int new_x_b = x - offset;

      if (new_x_r >= 0 && new_x_r < fb_w) {
        u32 red_color = source[y * fb_w + new_x_r];
        r = (red_color >> 16) & 0xFF;
      } else {
        r = 0;
      }

      if (new_x_b >= 0 && new_x_b < fb_w) {
        u32 blue_color = source[y * fb_w + new_x_b];
        b = blue_color & 0xFF;
      } else {
        b = 0;
      }

      if (r > 200 || g > 200 || b > 200) {
        r = (u8)fmin(r * 1.15f, 255.0f);
        g = (u8)fmin(g * 1.15f, 255.0f);
        b = (u8)fmin(b * 1.15f, 255.0f);
      }

      framebuffer[y * fb_w + x] = ((u32)r << 16) | ((u32)g << 8) | (u32)b;
    }
  }

  free(source);
}


void vignette_screen_shader(u32 *framebuffer, int fb_w, int fb_h) {
  float center_x = fb_w / 2.0f;
  float center_y = fb_h / 2.0f;
  float max_distance = sqrt(center_x * center_x + center_y * center_y) * 2.0f;

  for (int y = 0; y < fb_h; y++) {
    for (int x = 0; x < fb_w; x++) {
      u32 color = framebuffer[y * fb_w + x];

      u8 r = (color >> 16) & 0xFF;
      u8 g = (color >> 8) & 0xFF;
      u8 b = color & 0xFF;

      float dx = x - center_x;
      float dy = y - center_y;
      float distance = sqrt(dx * dx + dy * dy);
      float vignette_factor = 1.0f - (distance / max_distance);
      vignette_factor = fmax(vignette_factor, 0.0f);

      r = (u8)(r * vignette_factor);
      g = (u8)(g * vignette_factor);
      b = (u8)(b * vignette_factor);

      framebuffer[y * fb_w + x] = ((u32)r << 16) | ((u32)g << 8) | (u32)b;
    }
  }
}


void scene_draw(scene_t *s, struct out_render **renderers, int num_outs, float time) {
  s->time = time;

  for (int i = 0; i < num_outs; i++) {
    struct out_render *r = renderers[i];
    for (int j = 0; j < r->fb_w * r->fb_h; j++) {
      r->framebuffer[j] = BG;
    }
  }

  render_horizontal(s);
  render_vertical(s);


  for (int i = 0; i < num_outs; i++) {
    crt_bloom_screen_shader(renderers[i]->framebuffer, renderers[i]->fb_w, renderers[i]->fb_h, time);
    vignette_screen_shader(renderers[i]->framebuffer, renderers[i]->fb_w, renderers[i]->fb_h);
  }
}
