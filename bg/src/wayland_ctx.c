#include "wayland_ctx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void lsurf_configure(void *data,
  struct zwlr_layer_surface_v1 *surf,
  uint32_t serial, uint32_t w, uint32_t h) {
  struct bg_output *out = data;
  out->width  = (int)w;
  out->height = (int)h;
  zwlr_layer_surface_v1_ack_configure(surf, serial);
  out->configured = true;
}

static void lsurf_closed(void *data, struct zwlr_layer_surface_v1 *surf) {
  (void)surf;
  ((struct bg_output *)data)->closed = true;
}

static const struct zwlr_layer_surface_v1_listener lsurf_listener = {
  .configure = lsurf_configure,
  .closed    = lsurf_closed,
};

static void reg_global(void *data, struct wl_registry *reg,
  uint32_t name, const char *iface, uint32_t version) {
  struct wayland_ctx *wl = data;
  (void)version;
  if (!strcmp(iface, wl_compositor_interface.name)) {
    wl->compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
  } else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name)) {
    wl->layer_shell = wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, 1);
  } else if (!strcmp(iface, wl_output_interface.name)) {
    wl->n_outputs++;
    wl->outputs = realloc(wl->outputs, wl->n_outputs * sizeof(*wl->outputs));
    struct bg_output *out = &wl->outputs[wl->n_outputs - 1];
    memset(out, 0, sizeof(*out));
    out->wl_output = wl_registry_bind(reg, name, &wl_output_interface, 1);
  }
}

static void reg_global_remove(void *data, struct wl_registry *reg, uint32_t name) {
  (void)data; (void)reg; (void)name;
}

static const struct wl_registry_listener reg_listener = {
  .global        = reg_global,
  .global_remove = reg_global_remove,
};

static void setup_output_surface(struct wayland_ctx *wl, struct bg_output *out) {
  out->surface = wl_compositor_create_surface(wl->compositor);
  out->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
    wl->layer_shell, out->surface, out->wl_output,
    ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "bg");
  zwlr_layer_surface_v1_add_listener(out->layer_surface, &lsurf_listener, out);
  // Anchor all edges + exclusive_zone -1: fill the output, stay behind everything.
  zwlr_layer_surface_v1_set_anchor(out->layer_surface,
    ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP    | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
    ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT   | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
  zwlr_layer_surface_v1_set_exclusive_zone(out->layer_surface, -1);
  zwlr_layer_surface_v1_set_size(out->layer_surface, 0, 0);
  wl_surface_commit(out->surface);
}

struct wayland_ctx *wayland_ctx_create(void) {
  struct wayland_ctx *wl = calloc(1, sizeof(*wl));
  if (!wl) return NULL;

  wl->display = wl_display_connect(NULL);
  if (!wl->display) {
    fprintf(stderr, "bg: cannot connect to Wayland display\n");
    free(wl);
    return NULL;
  }

  wl->registry = wl_display_get_registry(wl->display);
  wl_registry_add_listener(wl->registry, &reg_listener, wl);
  // First roundtrip: collect compositor, layer_shell, and all wl_outputs.
  wl_display_roundtrip(wl->display);

  if (!wl->compositor || !wl->layer_shell) {
    fprintf(stderr, "bg: compositor missing wl_compositor or zwlr_layer_shell_v1\n");
    wl_display_disconnect(wl->display);
    free(wl);
    return NULL;
  }
  if (wl->n_outputs == 0) {
    fprintf(stderr, "bg: no outputs found\n");
    wl_display_disconnect(wl->display);
    free(wl);
    return NULL;
  }

  // Create a layer surface for every output, then roundtrip to get configure events.
  for (int i = 0; i < wl->n_outputs; i++) {
    setup_output_surface(wl, &wl->outputs[i]);
  }
  wl_display_roundtrip(wl->display);

  for (int i = 0; i < wl->n_outputs; i++) {
    struct bg_output *out = &wl->outputs[i];
    if (!out->configured || out->width <= 0 || out->height <= 0) {
      fprintf(stderr, "bg: output %d configure failed (w=%d h=%d)\n",
              i, out->width, out->height);
      wl_display_disconnect(wl->display);
      free(wl->outputs);
      free(wl);
      return NULL;
    }
    printf("bg: output %d: %dx%d\n", i, out->width, out->height);
  }

  return wl;
}

void wayland_ctx_destroy(struct wayland_ctx *wl) {
  if (!wl) return;
  for (int i = 0; i < wl->n_outputs; i++) {
    struct bg_output *out = &wl->outputs[i];
    if (out->layer_surface) zwlr_layer_surface_v1_destroy(out->layer_surface);
    if (out->surface)       wl_surface_destroy(out->surface);
    if (out->wl_output)     wl_output_destroy(out->wl_output);
  }
  free(wl->outputs);
  if (wl->layer_shell) zwlr_layer_shell_v1_destroy(wl->layer_shell);
  if (wl->compositor)  wl_compositor_destroy(wl->compositor);
  if (wl->registry)    wl_registry_destroy(wl->registry);
  if (wl->display)     wl_display_disconnect(wl->display);
  free(wl);
}
