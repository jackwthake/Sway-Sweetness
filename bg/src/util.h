#ifndef __UTIL_H__
#define __UTIL_H__

#include "scene.h"

u32 rgb_to_u32(u8 r, u8 g, u8 b);
void u32_to_rgb(u32 color, u8 *r, u8 *g, u8 *b);

u32 *convert_bmp_to_framebuffer(const char *bmp_path, int *width, int *height);
void draw_bitmap_to_framebuffer(u32 *framebuffer, int fb_w, int fb_h, u32 *bitmap, int bmp_w, int bmp_h, int x_offset, int y_offset);
bool is_renderer_horizontal(struct out_render *renderer);


void draw_glyph_to_framebuffer(u32 *framebuffer, int fb_w, int fb_h, const char c, int x_offset, int y_offset, u32 color);
void draw_string_to_framebuffer(u32 *framebuffer, int fb_w, int fb_h, const char *str, size_t str_len, int x_offset, int y_offset, int *next_char_x, int *next_char_y, u32 color);

void get_random_file_from_subdir(const char *base_dir, unsigned max_recurse_depth, char *out_path, size_t out_path_size);

// Resolve an asset filename to a full path. Tries several locations
// (BG_ASSET_DIR, executable-relative assets/, ~/.local/share/bg/assets,
// /usr/local/share/bg/assets, /usr/share/bg/assets, ./assets) and returns
// true if a readable file was found. If not found, `out_path` will contain
// a reasonable fallback and the function returns false.
bool resolve_asset_path(const char *asset_name, char *out_path, size_t out_path_size);

#endif // __UTIL_H__