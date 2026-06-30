#include "egl_ctx.h"
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>

// Blit a 2D texture to the full viewport.
// UV.y is flipped: OpenGL puts row 0 at the bottom of a texture, but the
// shader-works framebuffer is stored top-to-bottom.
static const char *vert_src =
  "attribute vec2 a_pos;\n"
  "varying vec2 v_uv;\n"
  "void main() {\n"
  "  v_uv = vec2(a_pos.x * 0.5 + 0.5, 0.5 - a_pos.y * 0.5);\n"
  "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
  "}\n";

static const char *frag_src =
  "precision mediump float;\n"
  "uniform sampler2D u_tex;\n"
  "varying vec2 v_uv;\n"
  "void main() {\n"
  "  gl_FragColor = texture2D(u_tex, v_uv);\n"
  "}\n";

static const float quad[] = {
  -1.0f, -1.0f,
   1.0f, -1.0f,
  -1.0f,  1.0f,
   1.0f,  1.0f,
};

static GLuint compile_shader(GLenum type, const char *src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  GLint ok = GL_FALSE;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char buf[512];
    glGetShaderInfoLog(s, sizeof(buf), NULL, buf);
    fprintf(stderr, "bg: shader error: %s\n", buf);
    glDeleteShader(s);
    return 0;
  }
  return s;
}

struct egl_ctx *egl_ctx_create(struct wl_display *wayland_dpy,
  struct wl_surface *wayland_surf, int width, int height) {
  struct egl_ctx *egl = calloc(1, sizeof(*egl));
  if (!egl) return NULL;

  egl->dpy = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_EXT, wayland_dpy, NULL);
  if (egl->dpy == EGL_NO_DISPLAY) {
    fprintf(stderr, "bg: eglGetPlatformDisplay failed\n");
    free(egl);
    return NULL;
  }

  EGLint major, minor;
  if (!eglInitialize(egl->dpy, &major, &minor)) {
    fprintf(stderr, "bg: eglInitialize failed\n");
    free(egl);
    return NULL;
  }

  static const EGLint cfg_attrs[] = {
    EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_RED_SIZE,        8,
    EGL_GREEN_SIZE,      8,
    EGL_BLUE_SIZE,       8,
    EGL_ALPHA_SIZE,      8,
    EGL_NONE
  };
  EGLConfig cfg;
  EGLint n = 0;
  eglChooseConfig(egl->dpy, cfg_attrs, &cfg, 1, &n);
  if (n == 0) {
    fprintf(stderr, "bg: no suitable EGL config\n");
    eglTerminate(egl->dpy);
    free(egl);
    return NULL;
  }

  eglBindAPI(EGL_OPENGL_ES_API);
  static const EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
  egl->ctx = eglCreateContext(egl->dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
  if (egl->ctx == EGL_NO_CONTEXT) {
    fprintf(stderr, "bg: eglCreateContext failed\n");
    eglTerminate(egl->dpy);
    free(egl);
    return NULL;
  }

  egl->egl_window = wl_egl_window_create(wayland_surf, width, height);
  egl->surf = eglCreateWindowSurface(egl->dpy, cfg,
    (EGLNativeWindowType)egl->egl_window, NULL);
  if (egl->surf == EGL_NO_SURFACE) {
    fprintf(stderr, "bg: eglCreateWindowSurface failed\n");
    wl_egl_window_destroy(egl->egl_window);
    eglDestroyContext(egl->dpy, egl->ctx);
    eglTerminate(egl->dpy);
    free(egl);
    return NULL;
  }

  eglMakeCurrent(egl->dpy, egl->surf, egl->surf, egl->ctx);

  GLuint vs = compile_shader(GL_VERTEX_SHADER,   vert_src);
  GLuint fs = compile_shader(GL_FRAGMENT_SHADER, frag_src);
  egl->prog = glCreateProgram();
  glAttachShader(egl->prog, vs);
  glAttachShader(egl->prog, fs);
  glLinkProgram(egl->prog);
  glDeleteShader(vs);
  glDeleteShader(fs);

  glUseProgram(egl->prog);
  egl->a_pos = glGetAttribLocation(egl->prog, "a_pos");
  glUniform1i(glGetUniformLocation(egl->prog, "u_tex"), 0);

  glGenBuffers(1, &egl->vbo);
  glBindBuffer(GL_ARRAY_BUFFER, egl->vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

  // GL_NEAREST: pixel-art nearest-neighbor upscale.
  glGenTextures(1, &egl->tex);
  glBindTexture(GL_TEXTURE_2D, egl->tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  return egl;
}

void egl_ctx_upload_frame(struct egl_ctx *egl, const uint32_t *pixels,
  int fb_width, int fb_height) {
  glBindTexture(GL_TEXTURE_2D, egl->tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fb_width, fb_height,
               0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

void egl_ctx_present(struct egl_ctx *egl) {
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(egl->prog);
  glBindBuffer(GL_ARRAY_BUFFER, egl->vbo);
  glEnableVertexAttribArray(egl->a_pos);
  glVertexAttribPointer(egl->a_pos, 2, GL_FLOAT, GL_FALSE, 0, NULL);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, egl->tex);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(egl->a_pos);
  eglSwapBuffers(egl->dpy, egl->surf);
}

void egl_ctx_destroy(struct egl_ctx *egl) {
  if (!egl) return;
  eglMakeCurrent(egl->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (egl->tex)        glDeleteTextures(1, &egl->tex);
  if (egl->vbo)        glDeleteBuffers(1, &egl->vbo);
  if (egl->prog)       glDeleteProgram(egl->prog);
  if (egl->surf)       eglDestroySurface(egl->dpy, egl->surf);
  if (egl->egl_window) wl_egl_window_destroy(egl->egl_window);
  if (egl->ctx)        eglDestroyContext(egl->dpy, egl->ctx);
  if (egl->dpy)        eglTerminate(egl->dpy);
  free(egl);
}
