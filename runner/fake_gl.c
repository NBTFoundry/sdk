/* SPDX-License-Identifier: MPL-2.0 */
/* NBT_RUNNER_FAKE_GL=1: video without a window and an OpenGL ES 2.0 that
 * records what it is given, for testing the guest GL stream where there is no
 * GPU (the QNX 6.5 VM). Calls it does not record return 0. */
#include "fake_gl.h"

#include <GLES2/gl2.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static struct {
  unsigned frames, draws, vertices, shaders, shader_bytes, buffers, buffer_bytes;
  uint32_t buffer_sum;
  float last_uniform;
  GLuint next_name;
} rec;

static void report(const char* when) {
  printf("fake-gl: %s: %u frames, %u draws (%u vertices), %u shaders (%u bytes), %u buffers (%u bytes, sum %08x),"
         " last uniform %.3f\n",
         when, rec.frames, rec.draws, rec.vertices, rec.shaders, rec.shader_bytes, rec.buffers, rec.buffer_bytes,
         (unsigned)rec.buffer_sum, rec.last_uniform);
  fflush(stdout);
}

static int32_t video_open(int32_t surface, int32_t* w, int32_t* h) {
  (void)surface;
  *w = 800;
  *h = 480;
  return 0;
}
static void video_swap(void) {
  if (++rec.frames == 1 || rec.frames % 100 == 0) report("swap");
}
static void video_close(void) { report("closed"); }

static GLuint GL_APIENTRY f_create(void) { return ++rec.next_name; }
static GLuint GL_APIENTRY f_create_shader(GLenum t) { (void)t; return ++rec.next_name; }
static void GL_APIENTRY f_gen(GLsizei n, GLuint* out) {
  for (GLsizei i = 0; i < n; ++i) out[i] = ++rec.next_name;
}
static void GL_APIENTRY f_shader_source(GLuint s, GLsizei count, const GLchar* const* str, const GLint* len) {
  (void)s;
  ++rec.shaders;
  for (GLsizei i = 0; i < count; ++i) rec.shader_bytes += len && len[i] >= 0 ? (unsigned)len[i] : strlen(str[i]);
}
static void GL_APIENTRY f_buffer_data(GLenum t, GLsizeiptr size, const void* data, GLenum usage) {
  (void)t, (void)usage;
  ++rec.buffers;
  rec.buffer_bytes += (unsigned)size;
  for (GLsizeiptr i = 0; data && i < size; ++i) rec.buffer_sum = rec.buffer_sum * 31 + ((const uint8_t*)data)[i];
}
static void GL_APIENTRY f_uniform1f(GLint loc, GLfloat v) { (void)loc; rec.last_uniform = v; }
static void GL_APIENTRY f_draw_arrays(GLenum m, GLint first, GLsizei count) {
  (void)m, (void)first;
  ++rec.draws;
  rec.vertices += (unsigned)count;
}
static void GL_APIENTRY f_get_iv(GLenum pname, GLint* out) {
  if (pname == GL_COMPILE_STATUS || pname == GL_LINK_STATUS) *out = GL_TRUE;
  else *out = 0;
}
static void GL_APIENTRY f_get_shader_iv(GLuint s, GLenum pname, GLint* out) { (void)s; f_get_iv(pname, out); }
static const GLubyte* GL_APIENTRY f_get_string(GLenum name) {
  return (const GLubyte*)(name == GL_VERSION ? "OpenGL ES 2.0 (fake)" : "fake");
}
static GLint GL_APIENTRY f_location(GLuint p, const GLchar* name) { (void)p; return (GLint)strlen(name); }
static GLenum GL_APIENTRY f_get_error(void) { return GL_NO_ERROR; }
static int f_any(void) { return 0; }

static void* gl_proc(const char* name) {
  static const struct {
    const char* name;
    void* fn;
  } table[] = {
      {"glCreateProgram", (void*)f_create},     {"glCreateShader", (void*)f_create_shader},
      {"glGenBuffers", (void*)f_gen},           {"glGenTextures", (void*)f_gen},
      {"glShaderSource", (void*)f_shader_source}, {"glBufferData", (void*)f_buffer_data},
      {"glUniform1f", (void*)f_uniform1f},      {"glDrawArrays", (void*)f_draw_arrays},
      {"glGetShaderiv", (void*)f_get_shader_iv}, {"glGetProgramiv", (void*)f_get_shader_iv},
      {"glGetString", (void*)f_get_string},     {"glGetUniformLocation", (void*)f_location},
      {"glGetAttribLocation", (void*)f_location}, {"glGetError", (void*)f_get_error},
  };
  for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i)
    if (strcmp(table[i].name, name) == 0) return table[i].fn;
  return strncmp(name, "gl", 2) == 0 ? (void*)f_any : NULL;
}

void runner_fake_gl(nbt_host_v1* host) {
  host->video_open = video_open;
  host->video_swap = video_swap;
  host->video_close = video_close;
  host->gl_proc = gl_proc;
}

/* --- audio: a thread that pulls fragments at the device's pace --- */

static struct {
  nbt_audio_render_fn render;
  void* context;
  volatile int running;
  pthread_t thread;
  unsigned long long frames, nonzero;
} snd;

static void* audio_thread(void* arg) {
  (void)arg;
  int16_t buf[2 * 512];
  const struct timespec period = {0, 10666666}; /* 512 frames at 48 kHz */
  while (snd.running) {
    snd.render(snd.context, buf, 512);
    snd.frames += 512;
    for (int i = 0; i < 1024; ++i)
      if (buf[i]) ++snd.nonzero;
    nanosleep(&period, NULL);
  }
  return NULL;
}

static int32_t audio_open(int32_t rate, nbt_audio_render_fn render, void* context, int32_t route, int32_t* out_rate,
                          int32_t* frag) {
  (void)rate, (void)route;
  if (snd.running) return -NBT_EAGAIN;
  snd.render = render;
  snd.context = context;
  snd.frames = snd.nonzero = 0;
  snd.running = 1;
  if (pthread_create(&snd.thread, NULL, audio_thread, NULL) != 0) {
    snd.running = 0;
    return -NBT_EAGAIN;
  }
  *out_rate = 48000;
  *frag = 512;
  return 0;
}

static void audio_close(void) {
  if (!snd.running) return;
  snd.running = 0;
  pthread_join(snd.thread, NULL);
  printf("fake-audio: %llu frames pulled, %llu non-silent samples\n", snd.frames, snd.nonzero);
  fflush(stdout);
}

static void audio_set_route(int32_t wanted) { (void)wanted; }

void runner_fake_audio(nbt_host_v1* host) {
  host->audio_open = audio_open;
  host->audio_close = audio_close;
  host->audio_set_route = audio_set_route;
}
