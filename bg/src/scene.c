#include "scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#include "util.h"

static unsigned get_initial_reveal_length() {
  unsigned num_lines = 5 + (rand() % 10); // Randomly choose between 5 and 15 lines

  return num_lines * 160; // Assuming an average of 80 characters per line
}

static void reset_text_scroll_state(text_scroll_state_t *state) {
  if (!state) {
    return;
  }

  if (state->file_buffer_size == 0) {
    state->start = 0;
    state->pos = 0;
    state->last_time = 0.0f;
    return;
  }

  state->start = (size_t)(rand() % ((state->file_buffer_size / 2) + 1)); // Random start position within the buffer
  size_t initial_reveal = get_initial_reveal_length();
  state->pos = (state->start + initial_reveal) < state->file_buffer_size ? (state->start + initial_reveal) : state->file_buffer_size;
  state->posf = (float)state->pos;
    
  state->last_time = 0.0f;
}


static void free_text_scroll_state(text_scroll_state_t *state) {
  if (!state) {
    return;
  }

  free(state->file_buffer);
  state->file_buffer = NULL;
  state->file_buffer_size = 0;
  state->start = 0;
  state->pos = 0;
  state->posf = 0.0f;
  state->last_time = 0.0f;
}


static void load_next_file_for_state(text_scroll_state_t *state) {
  static char filebuffer[512];

  if (!state) {
    return;
  }

  /* prefer $HOME/Code, fall back to hardcoded path */
  const char *home = getenv("HOME");
  static char search_path[512];
  if (home) {
    snprintf(search_path, sizeof(search_path), "%s/Code", home);
  } else {
    strncpy(search_path, "/home/jwt/Code", sizeof(search_path));
    search_path[sizeof(search_path)-1] = '\0';
  }

  get_random_file_from_subdir(search_path, 5, filebuffer, sizeof(filebuffer));
  FILE *file = fopen(filebuffer, "r");
  if (!file) {
    /* fail silently to avoid spamming IO in render loop */
    return;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return;
  }

  long file_size = ftell(file);
  if (file_size < 0) {
    fclose(file);
    return;
  }

  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return;
  }

  free(state->file_buffer);
  state->file_buffer = malloc(file_size + 1);
  if (!state->file_buffer) {
    fclose(file);
    return;
  }

  size_t bytes_read = fread(state->file_buffer, 1, file_size, file);
  fclose(file);

  state->file_buffer_size = bytes_read;
  state->file_buffer[bytes_read] = '\0';
  reset_text_scroll_state(state);
}


scene_t *scene_create(struct out_render **renderers, int num_outs) {
  srand((unsigned int)time(NULL));

  scene_t *s = calloc(1, sizeof(*s));

  char asset_path[PATH_MAX];
  if (!resolve_asset_path("lain.bmp", asset_path, sizeof(asset_path))) {
    fprintf(stderr, "Warning: couldn't find installed asset 'lain.bmp', falling back to '%s'\n", asset_path);
  }
  s->lain = convert_bmp_to_framebuffer(asset_path, &s->lain_w, &s->lain_h);
  if (!s->lain) {
    free(s);
    return NULL;
  }

  if (!resolve_asset_path("navi.bmp", asset_path, sizeof(asset_path))) {
    fprintf(stderr, "Warning: couldn't find installed asset 'navi.bmp', falling back to '%s'\n", asset_path);
  }
  s->navi = convert_bmp_to_framebuffer(asset_path, &s->navi_w, &s->navi_h);
  if (!s->navi) {
    free(s->lain);
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

  load_next_file_for_state(&s->horizontal_scroll);
  load_next_file_for_state(&s->vertical_scroll);

  return s;
}


void scene_destroy(scene_t *scene) {
  if (!scene) return;
  free_text_scroll_state(&scene->horizontal_scroll);
  free_text_scroll_state(&scene->vertical_scroll);
  free(scene->lain);
  free(scene);
}


size_t find_next_line_start(const char *buffer, size_t buffer_size, size_t start_pos) {
  for (size_t i = start_pos; i < buffer_size; i++) {
    if (buffer[i] == '\n') {
      return i + 1; // Return the position after the newline
    }
  }
  return buffer_size; // If no newline is found, return the end of the buffer
}

void render_text_animation(scene_t *s, float time, struct out_render *renderer, int max_height, text_scroll_state_t *scroll_state) {
  if (!scroll_state) return;

  /* Advance one character per frame (caller already slowed framerate). */
  if (scroll_state->file_buffer && scroll_state->pos < scroll_state->file_buffer_size) {
    scroll_state->pos += 1;
    scroll_state->posf = (float)scroll_state->pos;
  }

  if (scroll_state->file_buffer && scroll_state->pos >= scroll_state->file_buffer_size) {
    load_next_file_for_state(scroll_state);
  }

  const char *to_show = scroll_state->file_buffer ? scroll_state->file_buffer + scroll_state->start : NULL;
  int carret_x = 0, carret_y = 0;

  if (to_show) {
    size_t reveal_len = 0;
    if (scroll_state->pos > scroll_state->start) reveal_len = scroll_state->pos - scroll_state->start;
    if (reveal_len > scroll_state->file_buffer_size - scroll_state->start) reveal_len = scroll_state->file_buffer_size - scroll_state->start;
    draw_string_to_framebuffer(renderer->framebuffer, renderer->fb_w, renderer->fb_h, (char *)to_show, reveal_len, 10, 10, &carret_x, &carret_y, 0xFFFAFAFA);
  } else {
    draw_string_to_framebuffer(renderer->framebuffer, renderer->fb_w, renderer->fb_h, "No file loaded", strlen("No file loaded"), 10, 10, &carret_x, &carret_y, 0xFFFAFAFA);
  }

  if (scroll_state->file_buffer && (carret_y + 16 > max_height)) {
    scroll_state->start = find_next_line_start(scroll_state->file_buffer, scroll_state->file_buffer_size, scroll_state->start);
    if (scroll_state->start >= scroll_state->file_buffer_size) {
      load_next_file_for_state(scroll_state);
    }
  }

  if (scroll_state->file_buffer && carret_x + 8 > renderer->fb_w) {
    scroll_state->pos = find_next_line_start(scroll_state->file_buffer, scroll_state->file_buffer_size, scroll_state->pos);
    scroll_state->posf = (float)scroll_state->pos;
    if (scroll_state->pos >= scroll_state->file_buffer_size) {
      load_next_file_for_state(scroll_state);
    }
  }

  /* draw 8x16 cursor at the end of the revealed text */
  if (scroll_state->file_buffer && scroll_state->pos < scroll_state->file_buffer_size) {
    for (int y = 0; y < 16; y++) {
      for (int x = 0; x < 8; x++) {
        int fb_x = carret_x + x;
        int fb_y = carret_y + y;
        if (fb_x >= 0 && fb_x < renderer->fb_w && fb_y >= 0 && fb_y < renderer->fb_h) {
          renderer->framebuffer[fb_y * renderer->fb_w + fb_x] = 0xFFF6F6F6; /* White cursor */
        }
      }
    }
  }
}


void render_horizontal(scene_t *s) {
  if (!s->renderer_horizontal) return;
  int lain_x = s->renderer_horizontal->fb_w - (s->lain_w * 0.80f);

  draw_bitmap_to_framebuffer(s->renderer_horizontal->framebuffer, s->renderer_horizontal->fb_w, s->renderer_horizontal->fb_h, s->navi, s->navi_w, s->navi_h, (s->renderer_horizontal->fb_w / 2) - (s->navi_w / 2), (s->renderer_horizontal->fb_h / 2) - (s->navi_h / 2));
  render_text_animation(s, s->time, s->renderer_horizontal, s->renderer_horizontal->fb_h, &s->horizontal_scroll);
  draw_bitmap_to_framebuffer(s->renderer_horizontal->framebuffer, s->renderer_horizontal->fb_w, s->renderer_horizontal->fb_h, s->lain, s->lain_w, s->lain_h, lain_x, 0);
}


void render_vertical(scene_t *s) {
  if (!s->renderer_vertical) return;
  int lain_y = s->renderer_vertical->fb_h - (s->lain_h * 0.60f);
  int lain_x = s->renderer_vertical->fb_w - (s->lain_w * 0.80f);

  draw_bitmap_to_framebuffer(s->renderer_vertical->framebuffer, s->renderer_vertical->fb_w, s->renderer_vertical->fb_h, s->navi, s->navi_w, s->navi_h, (s->renderer_vertical->fb_w / 2) - (s->navi_w / 2), (s->renderer_vertical->fb_h / 2) - (s->navi_h / 2));
  render_text_animation(s, s->time, s->renderer_vertical, s->renderer_vertical->fb_h, &s->vertical_scroll);
  draw_bitmap_to_framebuffer(s->renderer_vertical->framebuffer, s->renderer_vertical->fb_w, s->renderer_vertical->fb_h, s->lain, s->lain_w, s->lain_h, lain_x, lain_y);
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
}
