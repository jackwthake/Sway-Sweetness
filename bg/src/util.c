#include "util.h"

#include <stdio.h>
#include <stdlib.h>

#include "glyphs.inc"

u32 rgb_to_u32(u8 r, u8 g, u8 b) {
  return (u32)r | ((u32)g << 8) | ((u32)b << 16) | (0xFFu << 24);
}


void u32_to_rgb(u32 color, u8 *r, u8 *g, u8 *b) {
  *r = (u8)(color & 0xFF);
  *g = (u8)((color >> 8) & 0xFF);
  *b = (u8)((color >> 16) & 0xFF);
}


u32 *convert_bmp_to_framebuffer(const char *bmp_path, int *width, int *height) {
  FILE *file = fopen(bmp_path, "rb");
  if (!file) {
    fprintf(stderr, "Error: Could not open BMP file %s\n", bmp_path);
    return NULL;
  }

  // Read BMP header
  fseek(file, 18, SEEK_SET);
  fread(width, sizeof(int), 1, file);
  fread(height, sizeof(int), 1, file);

  // Allocate framebuffer
  u32 *framebuffer = malloc((*width) * (*height) * sizeof(u32));
  if (!framebuffer) {
    fprintf(stderr, "Error: Could not allocate memory for framebuffer\n");
    fclose(file);
    return NULL;
  }

  // Read pixel data (BMP stores pixels bottom-to-top)
  fseek(file, 54, SEEK_SET); // Skip BMP header
  for (int y = *height - 1; y >= 0; y--) {
    for (int x = 0; x < *width; x++) {
      u8 bgr[3];
      fread(bgr, sizeof(u8), 3, file);
      framebuffer[y * (*width) + x] = rgb_to_u32(bgr[2], bgr[1], bgr[0]); // Convert BGR to RGB
    }
    // Skip padding bytes
    int padding = (4 - ((*width) * 3) % 4) % 4;
    fseek(file, padding, SEEK_CUR);
  }

  fclose(file);
  return framebuffer;
}


void draw_bitmap_to_framebuffer(u32 *framebuffer, int fb_w, int fb_h, u32 *bitmap, int bmp_w, int bmp_h, int x_offset, int y_offset) {
  for (int y = 0; y < bmp_h; y++) {
    for (int x = 0; x < bmp_w; x++) {
      u32 color = bitmap[y * bmp_w + x];
      if (color != TRANSPARENT_COLOR) { // Skip transparent pixels
        int fb_x = x + x_offset;
        int fb_y = y + y_offset;
        if (fb_x >= 0 && fb_x < fb_w && fb_y >= 0 && fb_y < fb_h) {
          framebuffer[fb_y * fb_w + fb_x] = color;
        }
      }
    }
  }
}


bool is_renderer_horizontal(struct out_render *r) {
  return r->fb_w > r->fb_h;
}



// draw from 8x16 font bitmap to framebuffer at specified position
#define GLYPH_WIDTH 8
#define GLYPH_HEIGHT 16

void draw_glyph_to_framebuffer(u32 *framebuffer, int fb_w, int fb_h, const char c, int x_offset, int y_offset, u32 color) {
  if (c < 0 || c > 255) return; // Only handle ASCII range

  for (int y = 0; y < GLYPH_HEIGHT; y++) {
    u8 row = glyph_bitmaps[(unsigned char)c][y];
    for (int x = 0; x < GLYPH_WIDTH; x++) {
      if (row & (1 << (7 - x))) { // Check if pixel is set
        int fb_x = x + x_offset;
        int fb_y = y + y_offset;
        if (fb_x >= 0 && fb_x < fb_w && fb_y >= 0 && fb_y < fb_h) {
          framebuffer[fb_y * fb_w + fb_x] = color;
        }
      }
    }
  }
}


void draw_string_to_framebuffer(u32 *framebuffer, int fb_w, int fb_h, const char *str, int x_offset, int y_offset, u32 color) {
  int original_x_offset = x_offset; // Store the original x_offset for line breaks

  while (*str) {
    if (*str == '\n') {
      y_offset += GLYPH_HEIGHT; // Move to the next line
      x_offset = original_x_offset; // Reset x position
    } else if (*str == '\t'){
      x_offset += GLYPH_WIDTH * 2; // Move to the next tab
    } else {
      draw_glyph_to_framebuffer(framebuffer, fb_w, fb_h, *str, x_offset, y_offset, color);
      x_offset += GLYPH_WIDTH; // Move to the next character position
    }
    
    str++;
  }
}
