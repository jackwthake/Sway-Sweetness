#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define BG 0xFF180602
#define TRANSPARENT_COLOR 0xFFFF00FF

// Type definitions
typedef int8_t   i8;
typedef uint8_t  u8;
typedef int16_t  i16;
typedef uint16_t u16;
typedef int32_t  i32;
typedef uint32_t u32;
typedef int64_t  i64;
typedef uint64_t u64;
typedef size_t   usize;

typedef float    f32;
typedef double   f64;

struct out_render {
  u32        *framebuffer;
  int         fb_w, fb_h;
};

typedef struct {
  size_t start;
  size_t pos;
  char *file_buffer;
  size_t file_buffer_size;
  float posf; /* fractional position for smooth, time-based advancement */
  float last_time; /* last update time used to compute delta */
} text_scroll_state_t;

typedef struct {
  float             time;
  u32              *lain;
  int               lain_w, lain_h;

  u32              *navi;
  int               navi_w, navi_h;

  struct out_render       *renderer_horizontal;
  struct out_render       *renderer_vertical;

  text_scroll_state_t      horizontal_scroll;
  text_scroll_state_t      vertical_scroll;
} scene_t;

scene_t *scene_create(struct out_render **renderers, int num_outs);
void     scene_destroy(scene_t *scene);
void     scene_draw(scene_t *scene, struct out_render **renderers, int num_outs, float time);
