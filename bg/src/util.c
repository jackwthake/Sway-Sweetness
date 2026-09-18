#define _DEFAULT_SOURCE

#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <dirent.h>

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


void draw_string_to_framebuffer(u32 *framebuffer, int fb_w, int fb_h, const char *str, size_t str_len, int x_offset, int y_offset, int *next_char_x, int *next_char_y, u32 color) {
  int original_x_offset = x_offset; // Store the original x_offset for line breaks

  while (*str && str_len > 0) {
    if (*str == '\n') {
      y_offset += GLYPH_HEIGHT; // Move to the next line
      x_offset = original_x_offset; // Reset x position
      *next_char_x = x_offset;
      *next_char_y = y_offset;
    } else if (*str == '\t'){
      x_offset += GLYPH_WIDTH * 2; // Move to the next tab
      *next_char_x = x_offset;
      *next_char_y = y_offset;
    } else {
      draw_glyph_to_framebuffer(framebuffer, fb_w, fb_h, *str, x_offset, y_offset, color);
      x_offset += GLYPH_WIDTH; // Move to the next character position
      *next_char_x = x_offset;
      *next_char_y = y_offset;
    }
    
    str++;
    str_len--;
  }
}


static bool ends_with(const char *str, const char *suffix) {
  if (!str || !suffix) {
    return false;
  }
  
  size_t str_len = strlen(str);
  size_t suffix_len = strlen(suffix);
  
  // If the suffix is longer than the string, it cannot be a match
  if (suffix_len > str_len) {
    return false;
  }
  
  // Move the pointer of the main string to where the suffix should start
  // and compare the remaining substring with the suffix
  return strcmp(str + (str_len - suffix_len), suffix) == 0;
}


static void get_random_file_from_subdir_recurse(const char *base_dir, unsigned max_recurse_depth, unsigned current_depth, char *out_path, size_t out_path_size) {
  static const char *const allowed_extensions[] = {".c", ".cpp", ".hs", ".rs", NULL};
  static const char *const ignored_dirs[] = {".git", "node_modules", "include", "lib", "third-party", NULL};

  static size_t total_file_count = 0;
  static char **all_files = NULL;
  static char **all_dirs = NULL;
  
  if (current_depth > max_recurse_depth) {
    return;
  }

  DIR *dir = opendir(base_dir);
  if (!dir) {
    perror("opendir");
    return;
  }

  struct dirent *entry;
  size_t file_count = 0;
  char **file_list = NULL;
  size_t dir_count = 0;
  char **dir_list = NULL;

  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_type == DT_REG) { // Regular file
      // Check if the file has an allowed extension
      bool allowed = false;
      for (int i = 0; allowed_extensions[i] != NULL; i++) {
        if (ends_with(entry->d_name, allowed_extensions[i])) {
          allowed = true;
          break;
        }
      }
      if (!allowed) continue;

      file_list = realloc(file_list, sizeof(char *) * (file_count + 1));
      file_list[file_count] = strdup(entry->d_name);
      file_count++;
    } else if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
      // Check if the directory is ignored
      bool ignored = false;
      for (int i = 0; ignored_dirs[i] != NULL; i++) {
        if (strcmp(entry->d_name, ignored_dirs[i]) == 0) {
          ignored = true;
          break;
        }
      }
      if (ignored) continue;

      // Store subdirectory for processing
      dir_list = realloc(dir_list, sizeof(char *) * (dir_count + 1));
      dir_list[dir_count] = strdup(entry->d_name);
      dir_count++;
    }
  }

  closedir(dir);

  if (file_count > 0) {
    all_files = realloc(all_files, sizeof(char *) * (total_file_count + file_count));
    for (size_t i = 0; i < file_count; i++) {
      char full_path[512];
      snprintf(full_path, sizeof(full_path), "%s/%s", base_dir, file_list[i]);
      all_files[total_file_count + i] = strdup(full_path);
    }
    total_file_count += file_count;
  }

  for (size_t i = 0; i < dir_count; i++) {
    char subdir_path[512];
    snprintf(subdir_path, sizeof(subdir_path), "%s/%s", base_dir, dir_list[i]);
    get_random_file_from_subdir_recurse(subdir_path, max_recurse_depth, current_depth + 1, out_path, out_path_size);
  }

  if (current_depth == 0 && total_file_count > 0) {
    size_t random_index = rand() % total_file_count;
    snprintf(out_path, out_path_size, "%s", all_files[random_index]);
    
    for (size_t i = 0; i < total_file_count; i++) {
      free(all_files[i]);
    }
    free(all_files);
    all_files = NULL;
    total_file_count = 0;
  }

  for (size_t i = 0; i < file_count; i++) {
    free(file_list[i]);
  }
  free(file_list);
  
  for (size_t i = 0; i < dir_count; i++) {
    free(dir_list[i]);
  }
  free(dir_list);
}


void get_random_file_from_subdir(const char *base_dir, unsigned max_recurse_depth, char *out_path, size_t out_path_size) {
  get_random_file_from_subdir_recurse(base_dir, max_recurse_depth, 0, out_path, out_path_size);
}