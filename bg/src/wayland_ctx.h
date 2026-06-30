#pragma once

#include <stdbool.h>
#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct bg_output {
    struct wl_output             *wl_output;
    struct wl_surface            *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    int    width, height;
    bool   configured;
    bool   closed;
};

struct wayland_ctx {
    struct wl_display          *display;
    struct wl_registry         *registry;
    struct wl_compositor       *compositor;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct bg_output           *outputs;
    int                         n_outputs;
};

struct wayland_ctx *wayland_ctx_create(void);
void wayland_ctx_destroy(struct wayland_ctx *wl);
