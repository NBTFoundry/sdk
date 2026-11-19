/* SPDX-License-Identifier: MPL-2.0 */
/* Car-session probe (feasibility E1 items 9-13): what the QEMU VM cannot
 * answer. A module run by nbt-runner as an ordinary managed application:
 *  - GL: a shader quad for 10 s on the manager's surface; frame-time p50/p99;
 *  - audio: a 440 Hz tone for the same 10 s; frames rendered against the rate;
 *  - memcpy/memset/malloc throughput (compare with NBT_RUNNER_BENCH=1's
 *    numbers from the runner's libc.so.3);
 *  - NEON: d16-d31 kept across context switches (decides opt-in NEON).
 * Results go to stdout (the application's log) and the status label shows
 * "probe N/M ok". In a health check it only reports ready. */
#include <GLES2/gl2.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "membench.h"

int nbt_gl_load(void) __attribute__((visibility("hidden")));

static int64_t now_ns(void) { return nbt_host->monotonic_ns(); }

static int stopping(void) { return (nbt_host->app_poll() & NBT_APP_EV_STOP) != 0; }

/* --- audio: a tone, counted --- */

static volatile int64_t audio_frames;
static int audio_rate;

static void tone(void* ctx, int16_t* out, int32_t frames) {
  (void)ctx;
  static uint32_t phase;
  for (int32_t i = 0; i < frames; ++i) {
    const int16_t v = (int16_t)(6000.0f * sinf((float)phase * 6.2831853f * 440.0f / (float)audio_rate));
    out[2 * i] = out[2 * i + 1] = v;
    ++phase;
  }
  audio_frames += frames;
}

/* --- GL: a moving quad --- */

static GLuint compile(GLenum kind, const char* text) {
  GLuint s = glCreateShader(kind);
  glShaderSource(s, 1, &text, NULL);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  return ok ? s : 0;
}

static int cmp_i64(const void* a, const void* b) {
  int64_t x = *(const int64_t*)a, y = *(const int64_t*)b;
  return x < y ? -1 : x > y;
}

static void gl_and_audio(int32_t surface) {
  int32_t w = 0, h = 0;
  if (nbt_host->video_open(surface, &w, &h) != 0) {
    check_detail("video: window", 0, "the runner could not open it");
    return;
  }
  int missing = nbt_gl_load();
  char detail[96];
  snprintf(detail, sizeof(detail), "%dx%d, %d GL entry points missing", (int)w, (int)h, missing);
  check_detail("video: window and GL", missing == 0, detail);

  int32_t rate = 0, frag = 0;
  const int audio_ok = nbt_host->audio_open(48000, tone, NULL, 1, &rate, &frag) == 0;
  audio_rate = rate ? rate : 48000;
  snprintf(detail, sizeof(detail), "%d Hz, %d-frame fragments", (int)rate, (int)frag);
  check_detail("audio: device", audio_ok, detail);

  GLuint vs = compile(GL_VERTEX_SHADER,
                      "attribute vec2 p; uniform float t;\n"
                      "void main() { gl_Position = vec4(p.x * 0.5 + 0.4 * sin(t), p.y * 0.5, 0.0, 1.0); }");
  GLuint fs = compile(GL_FRAGMENT_SHADER,
                      "precision mediump float; uniform float t;\n"
                      "void main() { gl_FragColor = vec4(0.5 + 0.5 * sin(t), 0.3, 0.8, 1.0); }");
  GLuint prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glBindAttribLocation(prog, 0, "p");
  glLinkProgram(prog);
  GLint linked = 0;
  glGetProgramiv(prog, GL_LINK_STATUS, &linked);
  check("gl: shaders", vs && fs && linked);
  static const GLfloat quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
  GLuint vbo;
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(0);
  GLint t_loc = glGetUniformLocation(prog, "t");

  enum { kFrames = 600 };
  static int64_t frame_ns[kFrames];
  const int64_t start = now_ns();
  int64_t last = start;
  int frames = 0;
  while (frames < kFrames && !stopping()) {
    glViewport(0, 0, w, h);
    glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(prog);
    glUniform1f(t_loc, (float)frames / 30.0f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    nbt_host->video_swap();
    const int64_t t = now_ns();
    frame_ns[frames++] = t - last;
    last = t;
    nbt_host->app_tick(check_status());
  }
  const int64_t elapsed = now_ns() - start;
  qsort(frame_ns, frames, sizeof(frame_ns[0]), cmp_i64);
  const double p50 = frames ? frame_ns[frames / 2] / 1e6 : 0, p99 = frames ? frame_ns[frames * 99 / 100] / 1e6 : 0;
  snprintf(detail, sizeof(detail), "%d frames in %.2f s, p50 %.2f ms, p99 %.2f ms", frames, elapsed / 1e9, p50,
           p99);
  check_detail("gl: quad at panel rate", frames == kFrames && glGetError() == GL_NO_ERROR && p99 < 34.0, detail);
  if (audio_ok) {
    const double expect = (double)audio_rate * (double)elapsed / 1e9;
    snprintf(detail, sizeof(detail), "%lld frames rendered, %.0f expected", (long long)audio_frames, expect);
    check_detail("audio: tone kept up", audio_frames > expect * 0.9 && audio_frames < expect * 1.1 + 4096, detail);
    nbt_host->audio_close();
  }
  nbt_host->video_close();
}

/* --- throughput --- */

static void bench(void) {
  nbt_membench m;
  char detail[96];
  if (nbt_membench_run(&m, now_ns) != 0) {
    check("bench: buffers", 0);
    return;
  }
  snprintf(detail, sizeof(detail), "%.0f MB/s", m.memcpy_mbs);
  check_detail("bench: memcpy 4 MiB", m.memcpy_mbs > 0, detail);
  snprintf(detail, sizeof(detail), "%.0f MB/s", m.memset_mbs);
  check_detail("bench: memset 4 MiB", m.memset_mbs > 0, detail);
  snprintf(detail, sizeof(detail), "%.0f ns per malloc+free", m.malloc_ns);
  check_detail("bench: malloc/free", 1, detail);
}

/* --- NEON: d16-d31 across context switches --- */

static volatile int neon_bad;

static void* neon_thread(void* arg) {
  const uint64_t pattern = (uint64_t)(uintptr_t)arg * 0x0101010101010101ull;
  for (int round = 0; round < 2000 && !neon_bad; ++round) {
    uint64_t out[16];
    /* d16-d31 are invisible to the compiler here (vfpv3-d16): no clobbers. */
    __asm__ volatile(
        ".fpu neon\n"
        "vmov d16, %Q0, %R0\n vmov d17, d16\n vmov d18, d16\n vmov d19, d16\n"
        "vmov d20, d16\n vmov d21, d16\n vmov d22, d16\n vmov d23, d16\n"
        "vmov d24, d16\n vmov d25, d16\n vmov d26, d16\n vmov d27, d16\n"
        "vmov d28, d16\n vmov d29, d16\n vmov d30, d16\n vmov d31, d16\n"
        :
        : "r"(pattern));
    nbt_host->thread_yield();
    nbt_host->sleep_ns(100000); /* let the other thread run on this core */
    __asm__ volatile(
        ".fpu neon\n"
        "vstmia %0, {d16-d31}\n"
        :
        : "r"(out)
        : "memory");
    for (int i = 0; i < 16; ++i)
      if (out[i] != pattern) neon_bad = 1;
  }
  return NULL;
}

static void neon(void) {
  uintptr_t t1, t2;
  printf("neon: testing d16-d31 (a SIGILL here means no access to them)\n");
  fflush(stdout);
  nbt_host->thread_create(&t1, neon_thread, (void*)(uintptr_t)0x5a, 65536);
  nbt_host->thread_create(&t2, neon_thread, (void*)(uintptr_t)0xa5, 65536);
  nbt_host->thread_join(t1, NULL);
  nbt_host->thread_join(t2, NULL);
  check_detail("neon: d16-d31 preserved", !neon_bad, neon_bad ? "corrupted: keep vfpv3-d16" : "NEON can be enabled");
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  nbt_app_info info;
  memset(&info, 0, sizeof(info));
  if (nbt_host->app_info(&info) != 0) return 1;
  if (nbt_host->app_ready() != 0) return 1;
  if (info.flags & NBT_APP_CHECK) {
    while (!stopping()) nbt_host->sleep_ns(200000000);
    return 0;
  }
  bench();
  if (info.surface_id > 0) gl_and_audio(info.surface_id);
  neon();
  check_summary();
  while (!stopping()) {
    nbt_host->app_tick(check_status());
    nbt_host->sleep_ns(250000000);
  }
  return 0;
}
