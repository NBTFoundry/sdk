// SPDX-License-Identifier: MPL-2.0
// The guest GL command stream on a desktop GPU driver (Mesa, surfaceless EGL):
// the front end (FrameVideo, Overlay) draws the same scene twice, once with
// direct GL calls and once through the guest's encoders and the runner's
// decoders (guest/gl_stream.c, runner/gl_runner.c and the generated code), and
// both must give the same pixels with no GL error. The front end's gl* calls go
// through nbtgl_app (gl_redirect.h, generated), pointed at either side.
#include "gl_redirect.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nbt/frontend/frame_video.hpp"
#include "nbt/frontend/overlay.hpp"

struct nbtgl_functions nbtgl_app;

extern "C" {
/* The runner's side (gl_runner.h). */
extern struct nbtgl_functions nbtgl_fn;
int nbtgl_load(void* (*lookup)(const char* name));
int nbtgl_run(volatile uint8_t* arena, uint32_t bytes);
/* The guest's side (gl_stream_guest.h). */
void nbtgl_attach(volatile uint8_t* arena, int (*run)(uint32_t bytes));
uint32_t nbtgl_take(void);
void* nbtgl_proc(const char* name);
}

namespace {

/* Wider than the 4:3 frame: bars on both sides. */
const int kWidth = 800, kHeight = 480;
const uint32_t kArenaBytes = 16u << 20; /* as the runner's shared arena */

int failures = 0;
#define CHECK(cond)                                                \
  do {                                                             \
    if (!(cond)) {                                                 \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      ++failures;                                                  \
    }                                                              \
  } while (0)

void* gles;
void* driver_proc(const char* name) {
  void* p = dlsym(gles, name);
  return p ? p : reinterpret_cast<void*>(eglGetProcAddress(name));
}

void point_front_end(void* (*lookup)(const char* name)) {
#define X(n) nbtgl_app.n = reinterpret_cast<decltype(nbtgl_app.n)>(lookup("gl" #n));
  NBTGL_COMMANDS(X)
#undef X
}

uint8_t* arena;
int runs, failed_runs;
int run(uint32_t bytes) {
  ++runs;
  const int rc = nbtgl_run(arena, bytes);
  if (rc < 0) ++failed_runs;
  return rc;
}

struct Pixel {
  unsigned r, g, b;
  bool operator==(const Pixel& o) const { return r == o.r && g == o.g && b == o.b; }
};

/* Read straight from the driver: the scene is what the stream drew. */
Pixel read_pixel(int x, int y_from_top) {
  unsigned char p[4] = {0, 0, 0, 0};
  nbtgl_fn.ReadPixels(x, kHeight - 1 - y_from_top, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
  const Pixel out = {p[0], p[1], p[2]};
  return out;
}

struct Scene {
  Pixel frame, box, text, background;
  unsigned error;
};

/* A red 256x224 RGB565 frame scaled to the display, a green box and white
 * text from the overlay, three frames. */
Scene draw(bool stream) {
  using namespace nbt::frontend;
  static uint16_t image[224][256];
  for (int y = 0; y < 224; ++y)
    for (int x = 0; x < 256; ++x) image[y][x] = 0xf800;
  nbtgl_fn.ClearColor(0, 0, 1, 1);
  nbtgl_fn.Clear(GL_COLOR_BUFFER_BIT);
  FrameVideo video;
  Overlay overlay;
  CHECK(video.init());
  CHECK(overlay.init());
  const FrameOptions options = {kAspectDisplay, kFilterSharp, 4.0f / 3.0f};
  for (int frame = 0; frame < 3; ++frame) {
    video.upload(image, 256, 224, 512, kRgb565);
    video.draw(kWidth, kHeight, options);
    overlay.begin(kWidth, kHeight);
    overlay.box(20, 20, 100, 40, 0x00ff00ffu);
    overlay.text(20, 100, 4, "H", 0xffffffffu);
    overlay.end();
    if (stream) run(nbtgl_take());
  }
  nbtgl_fn.Finish();
  Scene s;
  s.frame = read_pixel(kWidth / 2, kHeight / 2);
  s.box = read_pixel(60, 40);
  s.text = read_pixel(22, 102);
  s.background = read_pixel(2, kHeight / 2);
  s.error = nbtgl_fn.GetError();
  video.shutdown();
  overlay.shutdown();
  if (stream) run(nbtgl_take());
  return s;
}

/* A frame red above, blue below, stretched over the display at each
 * rotation: row 0 (red) at the top, then on the left, at the bottom, on the
 * right. True when every turn puts it there. */
bool turned() {
  using namespace nbt::frontend;
  static uint16_t image[224][256];
  for (int y = 0; y < 224; ++y)
    for (int x = 0; x < 256; ++x) image[y][x] = y < 112 ? 0xf800 : 0x001f;
  FrameVideo video;
  CHECK(video.init());
  const Pixel red = {255, 0, 0}, blue = {0, 0, 255};
  /* Where row 0 shows, then the opposite side, for 0 to 3 quarter turns. */
  const int at[4][4] = {{kWidth / 2, kHeight / 4, kWidth / 2, kHeight * 3 / 4},
                        {kWidth / 4, kHeight / 2, kWidth * 3 / 4, kHeight / 2},
                        {kWidth / 2, kHeight * 3 / 4, kWidth / 2, kHeight / 4},
                        {kWidth * 3 / 4, kHeight / 2, kWidth / 4, kHeight / 2}};
  bool ok = true;
  for (unsigned turns = 0; turns < 4; ++turns) {
    const FrameOptions options = {kAspectStretch, kFilterNearest, 0.0f, turns};
    video.upload(image, 256, 224, 512, kRgb565);
    video.draw(kWidth, kHeight, options);
    nbtgl_fn.Finish();
    const Pixel first = read_pixel(at[turns][0], at[turns][1]);
    const Pixel last = read_pixel(at[turns][2], at[turns][3]);
    printf("turned %u: row 0 %u,%u,%u, last row %u,%u,%u\n", turns, first.r, first.g, first.b,
           last.r, last.g, last.b);
    ok = ok && first == red && last == blue;
  }
  video.shutdown();
  return ok;
}

void print(const char* name, const Scene& s) {
  printf("%s: frame %u,%u,%u box %u,%u,%u text %u,%u,%u edge %u,%u,%u GL error 0x%x\n", name,
         s.frame.r, s.frame.g, s.frame.b, s.box.r, s.box.g, s.box.b, s.text.r, s.text.g, s.text.b,
         s.background.r, s.background.g, s.background.b, s.error);
}

}  // namespace

int main() {
  setvbuf(stdout, NULL, _IONBF, 0);
  const char* extensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
  if (!extensions || !strstr(extensions, "EGL_MESA_platform_surfaceless")) {
    printf("SKIPPED: no surfaceless EGL (install Mesa's EGL)\n");
    return 0;
  }
  EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
  if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL)) {
    printf("FAIL: eglInitialize\n");
    return 1;
  }
  const EGLint config_attrs[] = {EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
                                 EGL_OPENGL_ES2_BIT,  EGL_RED_SIZE,    8,
                                 EGL_GREEN_SIZE,      8,               EGL_BLUE_SIZE,
                                 8,                   EGL_NONE};
  EGLConfig config;
  EGLint count = 0;
  if (!eglChooseConfig(dpy, config_attrs, &config, 1, &count) || count < 1) {
    printf("FAIL: no EGL configuration for a GLES 2 pbuffer\n");
    return 1;
  }
  const EGLint surface_attrs[] = {EGL_WIDTH, kWidth, EGL_HEIGHT, kHeight, EGL_NONE};
  EGLSurface surface = eglCreatePbufferSurface(dpy, config, surface_attrs);
  eglBindAPI(EGL_OPENGL_ES_API);
  const EGLint context_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  EGLContext context = eglCreateContext(dpy, config, EGL_NO_CONTEXT, context_attrs);
  if (!eglMakeCurrent(dpy, surface, surface, context)) {
    printf("FAIL: eglMakeCurrent\n");
    return 1;
  }
  gles = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL);
  CHECK(gles != NULL);
  if (!gles) return 1;
  const int missing = nbtgl_load(driver_proc);
  printf("driver: %s, %d GLES 2.0 entry points missing\n",
         reinterpret_cast<const char*>(nbtgl_fn.GetString(GL_RENDERER)), missing);
  CHECK(missing == 0);

  point_front_end(driver_proc);
  const Scene direct = draw(false);
  print("direct", direct);
  CHECK(direct.frame == (Pixel{255, 0, 0}));
  CHECK(direct.box == (Pixel{0, 255, 0}));
  CHECK(direct.text == (Pixel{255, 255, 255}));
  CHECK(direct.background == (Pixel{0, 0, 0}) || direct.background == (Pixel{0, 0, 255}));
  CHECK(direct.error == GL_NO_ERROR);
  CHECK(turned());

  arena = static_cast<uint8_t*>(calloc(1, kArenaBytes));
  CHECK(arena != NULL);
  if (!arena) return 1;
  nbtgl_attach(arena, run);
  point_front_end(nbtgl_proc);
  const Scene streamed = draw(true);
  print("stream", streamed);
  printf("stream: %d batches, %d refused\n", runs, failed_runs);
  CHECK(runs > 0);
  CHECK(failed_runs == 0);
  CHECK(streamed.frame == direct.frame);
  CHECK(streamed.box == direct.box);
  CHECK(streamed.text == direct.text);
  CHECK(streamed.background == direct.background);
  CHECK(streamed.error == GL_NO_ERROR);

  free(arena);
  eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(dpy, context);
  eglDestroySurface(dpy, surface);
  eglTerminate(dpy);
  if (failures) {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("OK: the guest GL stream draws as direct calls\n");
  return 0;
}
