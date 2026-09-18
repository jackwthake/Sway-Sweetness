#define _POSIX_C_SOURCE 200809L

#include "wayland_ctx.h"
#include "egl_ctx.h"
#include "scene.h"

#include <EGL/egl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#define RENDER_SCALE  2
#define TARGET_FPS    10
#define FRAME_MS      (1000.0f / TARGET_FPS)
/* GPU presenter frequency in Hz (runs independently of CPU update rate) */
#define GPU_FPS       30

static float get_time_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (float)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static struct out_render *out_render_create(struct bg_output *out) {
  struct out_render *r = calloc(1, sizeof(*r));
  r->fb_w = out->width  / RENDER_SCALE;
  r->fb_h = out->height / RENDER_SCALE;
  r->framebuffer[0] = malloc(r->fb_w * r->fb_h * sizeof(u32));
  r->framebuffer[1] = malloc(r->fb_w * r->fb_h * sizeof(u32));
  r->front = 0;
  r->has_frame = 0;
  /* initialize mutex/cond */
  pthread_mutex_t *m = malloc(sizeof(*m));
  pthread_cond_t  *c = malloc(sizeof(*c));
  pthread_mutex_init(m, NULL);
  pthread_cond_init(c, NULL);
  r->lock = m;
  r->cond = c;

  printf("bg: output %dx%d -> framebuffer %dx%d\n",
         out->width, out->height, r->fb_w, r->fb_h);
  return r;
}

static void out_render_destroy(struct out_render *r) {
  if (!r) return;
  if (r->framebuffer[0]) free(r->framebuffer[0]);
  if (r->framebuffer[1]) free(r->framebuffer[1]);
  if (r->lock) {
    pthread_mutex_destroy((pthread_mutex_t *)r->lock);
    free(r->lock);
  }
  if (r->cond) {
    pthread_cond_destroy((pthread_cond_t *)r->cond);
    free(r->cond);
  }
  free(r);
}

struct presenter_args {
  struct egl_ctx *egl;
  struct out_render *ren;
  struct wayland_ctx *wl;
  struct wl_surface *surface;
  int width;
  int height;
  int index;
  volatile int *running;
};

static void *presenter_thread(void *vargs) {
  struct presenter_args *args = vargs;
  struct egl_ctx *egl = args->egl;
  struct out_render *r = args->ren;
  int idx = args->index;

  if (!egl_ctx_init_surface(egl, args->surface, args->width, args->height)) {
    fprintf(stderr, "bg: output %d could not initialize its GL surface\n", idx);
    free(args);
    return NULL;
  }

  struct timespec sleep_ts = {0, (long)(1e9 / GPU_FPS)};

  while (*args->running) {
    /* compute aberration and time */
    float aberation = 0.0015f;
    if ((rand() % 100) < 2) aberation = 0.005f;
    float t = get_time_ms();

    /* Wait a short time / or until new frame available. */
    pthread_mutex_t *m = (pthread_mutex_t *)r->lock;
    pthread_mutex_lock(m);
    /* we don't strictly need to wait on cond; just present latest available */
    int has = r->has_frame;
    u32 *fb = out_render_frontbuffer(r);
    pthread_mutex_unlock(m);

    if (has && fb) {
      egl_ctx_upload_frame(egl, fb, r->fb_w, r->fb_h);
      if (!egl_ctx_present(egl, aberation, t, r->fb_w, r->fb_h)) {
        fprintf(stderr, "bg: output %d lost its GL surface, presenter exiting\n", idx);
        break;
      }
    }

    nanosleep(&sleep_ts, NULL);
  }

  /* Unbind context before exiting thread */
  eglMakeCurrent(egl->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  free(args);
  return NULL;
}

int main(void) {
  // Line-buffer stdout so startup/status messages hit the log file immediately
  // instead of sitting in a full buffer that a crash would lose.
  setvbuf(stdout, NULL, _IOLBF, 0);
  printf("%s\n", getcwd(NULL, 0));

  struct wayland_ctx *wl = wayland_ctx_create();
  if (!wl) return 1;

  int n = wl->n_outputs;
  struct egl_ctx    **egl = calloc(n, sizeof(*egl));
  struct out_render **ren = calloc(n, sizeof(*ren));

  for (int i = 0; i < n; i++) {
    struct bg_output *out = &wl->outputs[i];
    egl[i] = egl_ctx_create(wl->display);
    ren[i] = out_render_create(out);
    if (!egl[i] || !ren[i]) {
      fprintf(stderr, "bg: setup failed for output %d\n", i);
      return 1;
    }
  }

  scene_t *scene = scene_create(ren, n);

  /* start presenter threads (one per output) */
  pthread_t *presenters = calloc(n, sizeof(*presenters));
  volatile int running_flag = 1;
  for (int i = 0; i < n; i++) {
    struct presenter_args *args = malloc(sizeof(*args));
    args->egl = egl[i];
    args->ren = ren[i];
    args->wl = wl;
    args->surface = wl->outputs[i].surface;
    args->width = wl->outputs[i].width;
    args->height = wl->outputs[i].height;
    args->index = i;
    args->running = &running_flag;
    pthread_create(&presenters[i], NULL, presenter_thread, args);
  }

  bool running = true;
  while (running) {
    float frame_start = get_time_ms();

    for (int i = 0; i < n; i++) {
      if (wl->outputs[i].closed) { running = false; break; }
    }
    if (!running) break;

    scene_draw(scene, ren, n, frame_start);

    /* Swap backbuffers into front for presenters to upload/present */
    for (int i = 0; i < n; i++) {
      struct out_render *r = ren[i];
      pthread_mutex_t *m = (pthread_mutex_t *)r->lock;
      pthread_mutex_lock(m);
      r->front ^= 1;
      r->has_frame = 1;
      pthread_cond_signal((pthread_cond_t *)r->cond);
      pthread_mutex_unlock(m);
    }

    // Actually read any pending data off the socket (dispatch_pending alone
    // only processes events already queued from a previous read - it won't
    // notice e.g. a fresh layer-surface configure sent after a DPMS cycle,
    // which the compositor expects an ack for before it'll composite further
    // commits. Without this, the client keeps swapping/committing "success-
    // fully" while the compositor silently withholds the surface.
    while (wl_display_prepare_read(wl->display) != 0) {
      wl_display_dispatch_pending(wl->display);
    }
    wl_display_flush(wl->display);
    struct pollfd pfd = { .fd = wl_display_get_fd(wl->display), .events = POLLIN };
    if (poll(&pfd, 1, 0) > 0) {
      wl_display_read_events(wl->display);
    } else {
      wl_display_cancel_read(wl->display);
    }

    if (wl_display_dispatch_pending(wl->display) < 0 ||
        wl_display_flush(wl->display) < 0 ||
        wl_display_get_error(wl->display) != 0) {
      fprintf(stderr, "bg: wayland connection error (%d), exiting\n",
              wl_display_get_error(wl->display));
      break;
    }

    float elapsed_ms = get_time_ms() - frame_start;
    float remaining_ms = FRAME_MS - elapsed_ms;
    if (remaining_ms > 0.0f) {
      struct timespec ts = {
        .tv_sec  = 0,
        .tv_nsec = (long)(remaining_ms * 1000000.0f),
      };
      nanosleep(&ts, NULL);
    }
  }

  scene_destroy(scene);
  /* signal presenter threads to stop and join them */
  running_flag = 0;
  for (int i = 0; i < n; i++) {
    pthread_join(presenters[i], NULL);
  }
  free(presenters);

  for (int i = 0; i < n; i++) {
    egl_ctx_destroy(egl[i]);
    out_render_destroy(ren[i]);
  }
  free(egl);
  free(ren);
  wayland_ctx_destroy(wl);
  return 0;
}
