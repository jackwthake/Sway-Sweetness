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
"\n"
"uniform sampler2D u_texture;\n"
"uniform vec2 u_resolution;\n"
"uniform float u_time;\n"
"uniform float color_shift;\n"
"varying vec2 v_uv;\n"
"\n"
"void main() {\n"
"    // 1. Screen curvature (barrel distortion)\n"
"    vec2 tc = v_uv - 0.5;\n"
"    float dist = dot(tc, tc);\n"
"    tc *= 1.0 + dist * 0.1;\n"
"    tc += 0.5;\n"
"\n"
"    // 2. Out-of-bounds check (black border)\n"
"    if (tc.x < 0.0 || tc.x > 1.0 || tc.y < 0.0 || tc.y > 1.0) {\n"
"        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);\n"
"    } else {\n"
"        // 3. Chromatic Aberration\n"
"        float r = texture2D(u_texture, vec2(tc.x - color_shift, tc.y)).r;\n"
"        float g = texture2D(u_texture, tc).g;\n"
"        float b = texture2D(u_texture, vec2(tc.x + color_shift, tc.y)).b;\n"
"        vec4 cta = vec4(r, g, b, 1.0);\n"
"\n"
"        // 4. Base horizontal scanlines\n"
"        float scanline = sin(tc.y * u_resolution.y * 3.14159) * 0.12;\n"
"        cta.rgb -= scanline;\n"
"\n"
"        // 5. Discrete Rolling Interference Bars\n"
"        // Move coordinates over time and scale the frequency (how many bars)\n"
"        float bar_coord = fract((tc.y - u_time * 0.00005) * 0.75);\n"
"\n"
"        // Create a discrete bar: if the coordinate is less than 0.1, darken it\n"
"        float bar_mask = step(bar_coord, 0.1); \n"
"        \n"
"        // Apply the sharp bar effect (darkens the pixels inside the bar mask)\n"
"        cta.rgb -= bar_mask * 0.05; \n"
"\n"
"        gl_FragColor = cta;\n"
"    }\n"
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
    printf("bg: shader error: %s\n");
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
  egl->u_resolution = glGetUniformLocation(egl->prog, "u_resolution");
  egl->u_time = glGetUniformLocation(egl->prog, "u_time");
  egl->u_color_shift = glGetUniformLocation(egl->prog, "color_shift");
  glUniform1i(glGetUniformLocation(egl->prog, "u_texture"), 0);

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

bool egl_ctx_present(struct egl_ctx *egl, float aberation, float time, int fb_width, int fb_height) {
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(egl->prog);
  glUniform2f(egl->u_resolution, (float)fb_width, (float)fb_height);
  glUniform1f(egl->u_time, time);
  glUniform1f(egl->u_color_shift, aberation);
  glBindBuffer(GL_ARRAY_BUFFER, egl->vbo);
  glEnableVertexAttribArray(egl->a_pos);
  glVertexAttribPointer(egl->a_pos, 2, GL_FLOAT, GL_FALSE, 0, NULL);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, egl->tex);
  glUniform2f(egl->u_resolution, fb_width, fb_height);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(egl->a_pos);
  if (!eglSwapBuffers(egl->dpy, egl->surf)) {
    fprintf(stderr, "bg: eglSwapBuffers failed (0x%x)\n", eglGetError());
    return false;
  }
  return true;
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
