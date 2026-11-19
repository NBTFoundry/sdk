#include "video_qnx.hpp"

#ifdef __QNXNTO__

#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nbt/app/log.hpp"

extern "C" {
typedef struct _screen_context* screen_context_t;
typedef struct _screen_window* screen_window_t;
typedef struct _screen_display* screen_display_t;
enum {
  SCREEN_APPLICATION_CONTEXT = 0,
  SCREEN_APPLICATION_WINDOW = 0,
  SCREEN_PROPERTY_BUFFER_SIZE = 5,
  SCREEN_PROPERTY_DISPLAY = 11,
  SCREEN_PROPERTY_FORMAT = 14,
  SCREEN_PROPERTY_ID_STRING = 20,
  SCREEN_PROPERTY_POSITION = 35,
  SCREEN_PROPERTY_SIZE = 40,
  SCREEN_PROPERTY_SOURCE_SIZE = 42,
  SCREEN_PROPERTY_USAGE = 48,
  SCREEN_PROPERTY_DISPLAY_COUNT = 59,
  SCREEN_PROPERTY_DISPLAYS = 60,
  SCREEN_FORMAT_RGBA8888 = 8,
  SCREEN_FORMAT_RGBX8888 = 9,
  SCREEN_USAGE_NATIVE = (1 << 0),
  SCREEN_USAGE_WRITE = (1 << 2),
  SCREEN_USAGE_OPENGL_ES2 = (1 << 5),
  KD_WINDOWPROPERTY_SIZE = 66,
  KD_PROP_ID_STRING = 0x3ea
};
int screen_create_context(screen_context_t* context, int flags);
int screen_destroy_context(screen_context_t context);
int screen_get_context_property_iv(screen_context_t context, int property, int* values);
int screen_get_context_property_pv(screen_context_t context, int property, void** values);
int screen_get_display_property_iv(screen_display_t display, int property, int* values);
int screen_create_window_type(screen_window_t* window, screen_context_t context, int type);
int screen_destroy_window(screen_window_t window);
int screen_set_window_property_iv(screen_window_t window, int property, const int* value);
int screen_set_window_property_pv(screen_window_t window, int property, void** value);
int screen_set_window_property_cv(screen_window_t window, int property, int length,
                                  const char* value);
int screen_create_window_buffers(screen_window_t window, int count);

/* libKD.so.1 references kdMain if opened at runtime via dlopen(). */
int kdMain(int argc, const char* const* argv) {
  (void)argc;
  (void)argv;
  return 1;
}
}

namespace nbt {
namespace app {

namespace {

struct QnxVideoState {
  void* screen_ctx;
  void* screen_win;
  void* kd_lib;
  void* kd_win;
  EGLDisplay dpy;
  EGLSurface surf;
  EGLContext ctx;
  int win_w;
  int win_h;
};

void close_state(QnxVideoState* s);

void resolve_panel_size(screen_display_t disp0, int* out_w, int* out_h) {
  *out_w = 1440;
  *out_h = 540;
  if (disp0) {
    int sz[2] = {0, 0};
    if (screen_get_display_property_iv(disp0, SCREEN_PROPERTY_SIZE, sz) == 0 && sz[0] >= 320 &&
        sz[0] <= 4096 && sz[1] >= 200 && sz[1] <= 4096) {
      *out_w = sz[0];
      *out_h = sz[1];
      log_info("qnx display size=%dx%d (screen)", *out_w, *out_h);
      return;
    }
  }
  log_info("qnx display size=%dx%d (fallback)", *out_w, *out_h);
}

EGLDisplay acquire_egl_display(screen_display_t* displays, int ndisp) {
  EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  log_info("eglGetDisplay(DEFAULT) -> %p err=0x%x", static_cast<void*>(dpy), eglGetError());
  if (dpy != EGL_NO_DISPLAY) return dpy;

  for (int i = 0; i < ndisp; i++) {
    if (!displays[i]) continue;
    dpy = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(displays[i]));
    log_info("eglGetDisplay(disp[%d]=%p) -> %p err=0x%x", i, static_cast<void*>(displays[i]),
             static_cast<void*>(dpy), eglGetError());
    if (dpy != EGL_NO_DISPLAY) return dpy;
  }
  return EGL_NO_DISPLAY;
}

bool choose_es2_config(EGLDisplay dpy, EGLConfig* out_cfg, int* out_alpha) {
  static const EGLint c0[] = {EGL_SURFACE_TYPE,
                              EGL_WINDOW_BIT,
                              EGL_RENDERABLE_TYPE,
                              EGL_OPENGL_ES2_BIT,
                              EGL_RED_SIZE,
                              8,
                              EGL_GREEN_SIZE,
                              8,
                              EGL_BLUE_SIZE,
                              8,
                              EGL_ALPHA_SIZE,
                              8,
                              EGL_DEPTH_SIZE,
                              16,
                              EGL_STENCIL_SIZE,
                              8,
                              EGL_NONE};
  static const EGLint c1[] = {EGL_SURFACE_TYPE,
                              EGL_WINDOW_BIT,
                              EGL_RENDERABLE_TYPE,
                              EGL_OPENGL_ES2_BIT,
                              EGL_RED_SIZE,
                              8,
                              EGL_GREEN_SIZE,
                              8,
                              EGL_BLUE_SIZE,
                              8,
                              EGL_ALPHA_SIZE,
                              8,
                              EGL_DEPTH_SIZE,
                              16,
                              EGL_NONE};
  static const EGLint c2[] = {EGL_SURFACE_TYPE,
                              EGL_WINDOW_BIT,
                              EGL_RENDERABLE_TYPE,
                              EGL_OPENGL_ES2_BIT,
                              EGL_RED_SIZE,
                              8,
                              EGL_GREEN_SIZE,
                              8,
                              EGL_BLUE_SIZE,
                              8,
                              EGL_ALPHA_SIZE,
                              8,
                              EGL_NONE};
  static const EGLint c3[] = {EGL_SURFACE_TYPE,
                              EGL_WINDOW_BIT,
                              EGL_RENDERABLE_TYPE,
                              EGL_OPENGL_ES2_BIT,
                              EGL_RED_SIZE,
                              8,
                              EGL_GREEN_SIZE,
                              8,
                              EGL_BLUE_SIZE,
                              8,
                              EGL_DEPTH_SIZE,
                              16,
                              EGL_NONE};
  static const EGLint c4[] = {EGL_SURFACE_TYPE,
                              EGL_WINDOW_BIT,
                              EGL_RENDERABLE_TYPE,
                              EGL_OPENGL_ES2_BIT,
                              EGL_RED_SIZE,
                              8,
                              EGL_GREEN_SIZE,
                              8,
                              EGL_BLUE_SIZE,
                              8,
                              EGL_NONE};
  static const EGLint c5[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE,
                              EGL_OPENGL_ES2_BIT, EGL_NONE};
  const EGLint* const candidates[] = {c0, c1, c2, c3, c4, c5};

  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
    EGLConfig cfg = nullptr;
    EGLint num = 0;
    if (eglChooseConfig(dpy, candidates[i], &cfg, 1, &num) && num >= 1 && cfg) {
      EGLint alpha = 0;
      EGLint cfg_id = 0;
      eglGetConfigAttrib(dpy, cfg, EGL_ALPHA_SIZE, &alpha);
      eglGetConfigAttrib(dpy, cfg, EGL_CONFIG_ID, &cfg_id);
      *out_cfg = cfg;
      *out_alpha = alpha;
      log_info("eglChooseConfig tier=%u ok id=%d alpha=%d", static_cast<unsigned>(i),
               static_cast<int>(cfg_id), static_cast<int>(alpha));
      return true;
    }
  }
  log_info("eglChooseConfig failed err=0x%x", eglGetError());
  return false;
}

bool try_screen_window(screen_context_t sctx, screen_display_t disp0, EGLDisplay dpy, EGLConfig cfg,
                       int surface_id, int w, int h, int usage, int format, bool create_bufs,
                       screen_window_t* out_win, EGLSurface* out_surf) {
  screen_window_t win = nullptr;
  if (screen_create_window_type(&win, sctx, SCREEN_APPLICATION_WINDOW) != 0 || !win) {
    log_info("screen_create_window_type failed errno=%d", errno);
    return false;
  }
  if (disp0) {
    void* dptr = disp0;
    (void)screen_set_window_property_pv(win, SCREEN_PROPERTY_DISPLAY, &dptr);
  }
  if (surface_id > 0) {
    char id[16];
    snprintf(id, sizeof(id), "%d", surface_id);
    if (screen_set_window_property_cv(win, SCREEN_PROPERTY_ID_STRING, static_cast<int>(strlen(id)),
                                      id) != 0) {
      log_info("screen ID_STRING failed errno=%d", errno);
    }
  }
  int size[2] = {w, h};
  int pos[2] = {0, 0};
  if (screen_set_window_property_iv(win, SCREEN_PROPERTY_USAGE, &usage) != 0 ||
      screen_set_window_property_iv(win, SCREEN_PROPERTY_FORMAT, &format) != 0 ||
      screen_set_window_property_iv(win, SCREEN_PROPERTY_SIZE, size) != 0) {
    log_info("screen set props failed usage=0x%x fmt=%d errno=%d", usage, format, errno);
    screen_destroy_window(win);
    return false;
  }
  (void)screen_set_window_property_iv(win, SCREEN_PROPERTY_SOURCE_SIZE, size);
  (void)screen_set_window_property_iv(win, SCREEN_PROPERTY_POSITION, pos);
  (void)screen_set_window_property_iv(win, SCREEN_PROPERTY_BUFFER_SIZE, size);
  if (create_bufs && screen_create_window_buffers(win, 2) != 0) {
    log_info("screen_create_window_buffers failed errno=%d", errno);
    screen_destroy_window(win);
    return false;
  }
  EGLSurface surf =
      eglCreateWindowSurface(dpy, cfg, reinterpret_cast<EGLNativeWindowType>(win), nullptr);
  if (surf == EGL_NO_SURFACE) {
    log_info("eglCreateWindowSurface(screen usage=0x%x bufs=%d) failed err=0x%x", usage,
             create_bufs ? 1 : 0, eglGetError());
    screen_destroy_window(win);
    return false;
  }
  log_info("eglCreateWindowSurface(screen usage=0x%x fmt=%d bufs=%d) ok", usage, format,
           create_bufs ? 1 : 0);
  *out_win = win;
  *out_surf = surf;
  return true;
}

bool try_kd_window(EGLDisplay dpy, EGLConfig cfg, int surface_id, int w, int h, void** out_lib,
                   void** out_kd, EGLSurface* out_surf) {
  void* lib = dlopen("libKD.so.1", RTLD_LAZY);
  if (!lib) lib = dlopen("libKD.so", RTLD_LAZY);
  if (!lib) {
    log_info("dlopen libKD failed: %s", dlerror());
    return false;
  }
  typedef void* (*FnCreate)(EGLDisplay, EGLConfig, void*);
  typedef int (*FnSetIv)(void*, int, const int32_t*);
  typedef int (*FnSetCv)(void*, int, const char*);
  typedef int (*FnRealize)(void*, EGLNativeWindowType*);
  typedef int (*FnDestroy)(void*);
  typedef int (*FnErr)(void);

  FnCreate fn_create = reinterpret_cast<FnCreate>(dlsym(lib, "kdCreateWindow"));
  FnSetIv fn_set_iv = reinterpret_cast<FnSetIv>(dlsym(lib, "kdSetWindowPropertyiv"));
  FnSetCv fn_set_cv = reinterpret_cast<FnSetCv>(dlsym(lib, "kdSetWindowPropertycv"));
  FnRealize fn_realize = reinterpret_cast<FnRealize>(dlsym(lib, "kdRealizeWindow"));
  FnDestroy fn_destroy = reinterpret_cast<FnDestroy>(dlsym(lib, "kdDestroyWindow"));
  FnErr fn_err = reinterpret_cast<FnErr>(dlsym(lib, "kdGetError"));
  if (!fn_create || !fn_set_iv || !fn_set_cv || !fn_realize || !fn_destroy) {
    log_info("libKD missing required symbols");
    dlclose(lib);
    return false;
  }
  void* kd = fn_create(dpy, cfg, nullptr);
  if (!kd) {
    log_info("kdCreateWindow failed err=%d", fn_err ? fn_err() : -1);
    dlclose(lib);
    return false;
  }
  int32_t size[2] = {w, h};
  (void)fn_set_iv(kd, KD_WINDOWPROPERTY_SIZE, size);
  if (surface_id > 0) {
    char id[16];
    snprintf(id, sizeof(id), "%d", surface_id);
    (void)fn_set_cv(kd, KD_PROP_ID_STRING, id);
  }
  EGLNativeWindowType native = 0;
  if (fn_realize(kd, &native) != 0 || !native) {
    log_info("kdRealizeWindow failed err=%d", fn_err ? fn_err() : -1);
    fn_destroy(kd);
    dlclose(lib);
    return false;
  }
  EGLSurface surf = eglCreateWindowSurface(dpy, cfg, native, nullptr);
  if (surf == EGL_NO_SURFACE) {
    log_info("eglCreateWindowSurface(KD) failed err=0x%x", eglGetError());
    fn_destroy(kd);
    dlclose(lib);
    return false;
  }
  log_info("eglCreateWindowSurface(KD) ok");
  *out_lib = lib;
  *out_kd = kd;
  *out_surf = surf;
  return true;
}

bool open_state(int surface_id, QnxVideoState* out) {
  memset(out, 0, sizeof(*out));
  out->dpy = EGL_NO_DISPLAY;
  out->surf = EGL_NO_SURFACE;
  out->ctx = EGL_NO_CONTEXT;

  screen_context_t sctx = nullptr;
  if (screen_create_context(&sctx, SCREEN_APPLICATION_CONTEXT) != 0 || !sctx) {
    log_info("screen_create_context failed errno=%d", errno);
    return false;
  }
  out->screen_ctx = sctx;

  int ndisp = 0;
  screen_display_t sdisp[4] = {nullptr, nullptr, nullptr, nullptr};
  screen_get_context_property_iv(sctx, SCREEN_PROPERTY_DISPLAY_COUNT, &ndisp);
  if (ndisp > 4) ndisp = 4;
  if (ndisp > 0) {
    screen_get_context_property_pv(sctx, SCREEN_PROPERTY_DISPLAYS, reinterpret_cast<void**>(sdisp));
  }
  log_info("qnx screen displays=%d first=%p", ndisp, static_cast<void*>(sdisp[0]));
  resolve_panel_size(sdisp[0], &out->win_w, &out->win_h);

  out->dpy = acquire_egl_display(sdisp, ndisp);
  if (out->dpy == EGL_NO_DISPLAY) {
    close_state(out);
    return false;
  }
  EGLint maj = 0;
  EGLint min = 0;
  if (!eglInitialize(out->dpy, &maj, &min)) {
    log_info("eglInitialize failed err=0x%x", eglGetError());
    close_state(out);
    return false;
  }
  log_info("eglInitialize ok EGL %d.%d", static_cast<int>(maj), static_cast<int>(min));
  if (!eglBindAPI(EGL_OPENGL_ES_API)) {
    log_info("eglBindAPI failed err=0x%x", eglGetError());
    close_state(out);
    return false;
  }

  EGLConfig cfg = nullptr;
  int alpha_bits = 0;
  if (!choose_es2_config(out->dpy, &cfg, &alpha_bits)) {
    close_state(out);
    return false;
  }

  if (!try_kd_window(out->dpy, cfg, surface_id, out->win_w, out->win_h, &out->kd_lib, &out->kd_win,
                     &out->surf)) {
    const int fmt = (alpha_bits > 0) ? SCREEN_FORMAT_RGBA8888 : SCREEN_FORMAT_RGBX8888;
    const int usage_es2 = SCREEN_USAGE_OPENGL_ES2;
    const int usage_mix = SCREEN_USAGE_OPENGL_ES2 | SCREEN_USAGE_NATIVE | SCREEN_USAGE_WRITE;
    struct Attempt {
      int usage;
      bool bufs;
    };
    const Attempt attempts[] = {
        {usage_es2, false},
        {usage_es2, true},
        {usage_mix, false},
        {usage_mix, true},
    };
    screen_window_t swin = nullptr;
    for (size_t i = 0; i < sizeof(attempts) / sizeof(attempts[0]); i++) {
      if (try_screen_window(sctx, sdisp[0], out->dpy, cfg, surface_id, out->win_w, out->win_h,
                            attempts[i].usage, fmt, attempts[i].bufs, &swin, &out->surf)) {
        out->screen_win = swin;
        break;
      }
    }
  }
  if (out->surf == EGL_NO_SURFACE) {
    close_state(out);
    return false;
  }

  static const EGLint kCtxAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  out->ctx = eglCreateContext(out->dpy, cfg, EGL_NO_CONTEXT, kCtxAttrs);
  if (out->ctx == EGL_NO_CONTEXT) {
    log_info("eglCreateContext failed err=0x%x", eglGetError());
    close_state(out);
    return false;
  }
  if (!eglMakeCurrent(out->dpy, out->surf, out->surf, out->ctx)) {
    log_info("eglMakeCurrent failed err=0x%x", eglGetError());
    close_state(out);
    return false;
  }
  EGLBoolean swap_ok = eglSwapInterval(out->dpy, 1);
  log_debug("qnx eglMakeCurrent ok surface_id=%d win=%dx%d swapInterval=%d kd=%p screen_win=%p",
            surface_id, out->win_w, out->win_h, static_cast<int>(swap_ok), out->kd_win,
            out->screen_win);
  return true;
}

void close_state(QnxVideoState* s) {
  if (!s) return;
  if (s->dpy != EGL_NO_DISPLAY) {
    eglMakeCurrent(s->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (s->ctx != EGL_NO_CONTEXT) eglDestroyContext(s->dpy, s->ctx);
    if (s->surf != EGL_NO_SURFACE) eglDestroySurface(s->dpy, s->surf);
  }
  /* Destroy KD / Screen window before eglTerminate (matches OpenKODE / libKD_X lifetime). */
  if (s->kd_win && s->kd_lib) {
    typedef int (*FnDestroy)(void*);
    FnDestroy fn_destroy = reinterpret_cast<FnDestroy>(dlsym(s->kd_lib, "kdDestroyWindow"));
    if (fn_destroy) fn_destroy(s->kd_win);
  }
  if (s->screen_win) screen_destroy_window(static_cast<screen_window_t>(s->screen_win));
  if (s->dpy != EGL_NO_DISPLAY) eglTerminate(s->dpy);
  if (s->kd_lib) dlclose(s->kd_lib);
  if (s->screen_ctx) screen_destroy_context(static_cast<screen_context_t>(s->screen_ctx));
  memset(s, 0, sizeof(*s));
  s->dpy = EGL_NO_DISPLAY;
  s->surf = EGL_NO_SURFACE;
  s->ctx = EGL_NO_CONTEXT;
}

}  // namespace

bool qnx_video_open(int surface_id, VideoNative* native, EGLDisplay* dpy, EGLSurface* surf,
                    EGLContext* ctx, int* win_w, int* win_h) {
  QnxVideoState s;
  if (!open_state(surface_id, &s)) return false;
  native->screen_ctx = s.screen_ctx;
  native->screen_win = s.screen_win;
  native->kd_lib = s.kd_lib;
  native->kd_win = s.kd_win;
  *dpy = s.dpy;
  *surf = s.surf;
  *ctx = s.ctx;
  *win_w = s.win_w;
  *win_h = s.win_h;
  return true;
}

void qnx_video_close(VideoNative* native, EGLDisplay* dpy, EGLSurface* surf, EGLContext* ctx) {
  QnxVideoState s;
  memset(&s, 0, sizeof(s));
  s.screen_ctx = native->screen_ctx;
  s.screen_win = native->screen_win;
  s.kd_lib = native->kd_lib;
  s.kd_win = native->kd_win;
  s.dpy = *dpy;
  s.surf = *surf;
  s.ctx = *ctx;
  close_state(&s);
  memset(native, 0, sizeof(*native));
  *dpy = EGL_NO_DISPLAY;
  *surf = EGL_NO_SURFACE;
  *ctx = EGL_NO_CONTEXT;
}

}  // namespace app
}  // namespace nbt

#endif /* __QNXNTO__ */
