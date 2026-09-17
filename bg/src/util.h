#ifndef __UTIL_H__
#define __UTIL_H__

#include "scene.h"

u32 rgb_to_u32(u8 r, u8 g, u8 b);
void u32_to_rgb(u32 color, u8 *r, u8 *g, u8 *b);

u32 *convert_bmp_to_framebuffer(const char *bmp_path, int *width, int *height);
void draw_bitmap_to_framebuffer(u32 *framebuffer, int fb_w, int fb_h, u32 *bitmap, int bmp_w, int bmp_h, int x_offset, int y_offset);
bool is_renderer_horizontal(struct out_render *renderer);


void draw_glyph_to_framebuffer(u32 *framebuffer, int fb_w, int fb_h, const char c, int x_offset, int y_offset, u32 color);
void draw_string_to_framebuffer(u32 *framebuffer, int fb_w, int fb_h, const char *str, int x_offset, int y_offset, u32 color);

#endif // __UTIL_H__