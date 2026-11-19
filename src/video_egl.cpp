#include <GLES2/gl2.h>
#include <stdio.h>
#include <string.h>

#include "nbt/app/log.hpp"
#include "nbt/app/video.hpp"
#include "video_qnx.hpp"

#ifndef __QNXNTO__
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#endif

namespace nbt {
namespace app {

namespace {

const char kDefaultTitle[] = "NBT App (Host GLES2)";
const int kDefaultHostWidth = 1280;
const int kDefaultHostHeight = 480;

}  // namespace

int host_key_from_x11_keysym(unsigned long keysym) {
#ifndef __QNXNTO__
  switch (keysym) {
    case XK_Escape:
      return kHostKeyEscape;
    case XK_Return:
    case XK_KP_Enter:
      return kHostKeyReturn;
    case XK_BackSpace:
      return kHostKeyBackspace;
    case XK_Tab:
      return kHostKeyTab;
    case XK_Up:
      return kHostKeyUp;
    case XK_Down:
      return kHostKeyDown;
    case XK_Left:
      return kHostKeyLeft;
    case XK_Right:
      return kHostKeyRight;
    case XK_Control_L:
      return kHostKeyLeftCtrl;
    case XK_Control_R:
      return kHostKeyRightCtrl;
    case XK_Shift_L:
      return kHostKeyLeftShift;
    case XK_Shift_R:
      return kHostKeyRightShift;
    default:
      break;
  }
  if (keysym >= XK_A && keysym <= XK_Z) return static_cast<int>(keysym - XK_A + 'a');
#endif
  if (keysym >= 0x20 && keysym <= 0x7e) return static_cast<int>(keysym);
  return kHostKeyNone;
}

VideoEgl::VideoEgl()
    : ready_(false),
      win_w_(kDefaultHostWidth),
      win_h_(kDefaultHostHeight),
      dpy_(EGL_NO_DISPLAY),
      surf_(EGL_NO_SURFACE),
      ctx_(EGL_NO_CONTEXT) {
  memset(&native_, 0, sizeof(native_));
}

VideoEgl::~VideoEgl() {
  shutdown();
}

#ifndef __QNXNTO__
bool VideoEgl::open_host_window(const VideoConfig& config) {
  win_w_ = config.host_width > 0 ? config.host_width : kDefaultHostWidth;
  win_h_ = config.host_height > 0 ? config.host_height : kDefaultHostHeight;
  Display* xdpy = XOpenDisplay(NULL);
  if (!xdpy) {
    log_info("XOpenDisplay failed (set DISPLAY)");
    return false;
  }
  int screen = DefaultScreen(xdpy);
  Window root = RootWindow(xdpy, screen);
  Window xwin = XCreateSimpleWindow(xdpy, root, 0, 0, static_cast<unsigned>(win_w_),
                                    static_cast<unsigned>(win_h_), 0, 0, 0);
  XStoreName(xdpy, xwin, config.host_title ? config.host_title : kDefaultTitle);
  XSelectInput(xdpy, xwin, KeyPressMask | KeyReleaseMask | StructureNotifyMask | ExposureMask);
  Atom wm_del = XInternAtom(xdpy, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(xdpy, xwin, &wm_del, 1);
  XMapWindow(xdpy, xwin);
  XFlush(xdpy);
  native_.x_display = xdpy;
  native_.x_window = static_cast<uintptr_t>(xwin);
  return true;
}

bool VideoEgl::init_host_egl() {
  Display* xdpy = static_cast<Display*>(native_.x_display);
  typedef EGLDisplay(EGLAPIENTRY * FnGetPlatformDisplay)(EGLenum, void*, const EGLint*);
  FnGetPlatformDisplay get_plat =
      reinterpret_cast<FnGetPlatformDisplay>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
  static const EGLenum kEglPlatformX11 = 0x31D5;
  dpy_ = get_plat ? get_plat(kEglPlatformX11, xdpy, NULL)
                  : eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(xdpy));
  if (dpy_ == EGL_NO_DISPLAY) dpy_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (dpy_ == EGL_NO_DISPLAY) {
    log_info("eglGetDisplay failed err=0x%x", eglGetError());
    return false;
  }
  if (!eglInitialize(dpy_, NULL, NULL)) {
    log_info("eglInitialize failed err=0x%x", eglGetError());
    return false;
  }
  if (!eglBindAPI(EGL_OPENGL_ES_API)) {
    log_info("eglBindAPI failed err=0x%x", eglGetError());
    return false;
  }

  static const EGLint kCfgAttrs[] = {EGL_SURFACE_TYPE,
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
                                     EGL_STENCIL_SIZE,
                                     8,
                                     EGL_NONE};
  EGLConfig cfg = NULL;
  EGLint num_cfg = 0;
  if (!eglChooseConfig(dpy_, kCfgAttrs, &cfg, 1, &num_cfg) || num_cfg < 1) {
    log_info("eglChooseConfig failed err=0x%x", eglGetError());
    return false;
  }

  surf_ =
      eglCreateWindowSurface(dpy_, cfg, static_cast<EGLNativeWindowType>(native_.x_window), NULL);
  if (surf_ == EGL_NO_SURFACE) {
    log_info("eglCreateWindowSurface failed err=0x%x", eglGetError());
    return false;
  }

  static const EGLint kCtxAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  ctx_ = eglCreateContext(dpy_, cfg, EGL_NO_CONTEXT, kCtxAttrs);
  if (ctx_ == EGL_NO_CONTEXT) {
    log_info("eglCreateContext failed err=0x%x", eglGetError());
    return false;
  }
  if (!eglMakeCurrent(dpy_, surf_, surf_, ctx_)) {
    log_info("eglMakeCurrent failed err=0x%x", eglGetError());
    return false;
  }
  eglSwapInterval(dpy_, 1);
  return true;
}
#endif

bool VideoEgl::init(int surface_id, const VideoConfig* config) {
  shutdown();
  VideoConfig defaults = {kDefaultTitle, kDefaultHostWidth, kDefaultHostHeight};
  const VideoConfig& cfg = config ? *config : defaults;
#ifdef __QNXNTO__
  (void)cfg;
  if (!qnx_video_open(surface_id, &native_, &dpy_, &surf_, &ctx_, &win_w_, &win_h_)) {
    shutdown();
    return false;
  }
#else
  (void)surface_id;
  if (!open_host_window(cfg) || !init_host_egl()) {
    shutdown();
    return false;
  }
#endif
  {
    const char* egl_vendor = eglQueryString(dpy_, EGL_VENDOR);
    const char* egl_ver = eglQueryString(dpy_, EGL_VERSION);
    const char* gl_vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* gl_renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* gl_ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    log_debug("EGL vendor=%s version=%s", egl_vendor ? egl_vendor : "?", egl_ver ? egl_ver : "?");
    log_debug("GL vendor=%s renderer=%s version=%s", gl_vendor ? gl_vendor : "?",
              gl_renderer ? gl_renderer : "?", gl_ver ? gl_ver : "?");
  }

  /* An initial black frame at once, so the manager's layer manager binds the surface. */
  glViewport(0, 0, win_w_, win_h_);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  eglSwapBuffers(dpy_, surf_);

  ready_ = true;
  return true;
}

void VideoEgl::swap() {
  if (ready_ && dpy_ != EGL_NO_DISPLAY && surf_ != EGL_NO_SURFACE) {
    eglSwapBuffers(dpy_, surf_);
  }
}

void VideoEgl::shutdown() {
  ready_ = false;
#ifdef __QNXNTO__
  if (dpy_ != EGL_NO_DISPLAY || native_.screen_ctx || native_.kd_lib) {
    qnx_video_close(&native_, &dpy_, &surf_, &ctx_);
  }
#else
  if (dpy_ != EGL_NO_DISPLAY) {
    eglMakeCurrent(dpy_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (ctx_ != EGL_NO_CONTEXT) eglDestroyContext(dpy_, ctx_);
    if (surf_ != EGL_NO_SURFACE) eglDestroySurface(dpy_, surf_);
    eglTerminate(dpy_);
  }
  Display* xdpy = static_cast<Display*>(native_.x_display);
  if (xdpy) {
    if (native_.x_window) XDestroyWindow(xdpy, static_cast<Window>(native_.x_window));
    XCloseDisplay(xdpy);
  }
  memset(&native_, 0, sizeof(native_));
#endif
  ctx_ = EGL_NO_CONTEXT;
  surf_ = EGL_NO_SURFACE;
  dpy_ = EGL_NO_DISPLAY;
}

bool VideoEgl::poll_event(HostEvent* out) {
  if (!out) return false;
  memset(out, 0, sizeof(*out));
#ifndef __QNXNTO__
  Display* xdpy = static_cast<Display*>(native_.x_display);
  if (!xdpy) return false;
  const Atom wm_del = XInternAtom(xdpy, "WM_DELETE_WINDOW", False);
  while (XPending(xdpy) > 0) {
    XEvent ev;
    XNextEvent(xdpy, &ev);
    if (ev.type == ClientMessage && static_cast<Atom>(ev.xclient.data.l[0]) == wm_del) {
      out->type = HostEvent::kClose;
      return true;
    }
    if (ev.type == ConfigureNotify) {
      if (ev.xconfigure.width > 0 && ev.xconfigure.height > 0 &&
          (ev.xconfigure.width != win_w_ || ev.xconfigure.height != win_h_)) {
        win_w_ = ev.xconfigure.width;
        win_h_ = ev.xconfigure.height;
        out->type = HostEvent::kResize;
        out->width = win_w_;
        out->height = win_h_;
        return true;
      }
    } else if (ev.type == KeyPress || ev.type == KeyRelease) {
      const int key = host_key_from_x11_keysym(XLookupKeysym(&ev.xkey, 0));
      if (key == kHostKeyNone) continue;
      out->type = (ev.type == KeyPress) ? HostEvent::kKeyDown : HostEvent::kKeyUp;
      out->key = key;
      return true;
    }
  }
#endif
  return false;
}

}  // namespace app
}  // namespace nbt
