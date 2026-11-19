// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_APP_VIDEO_HPP
#define NBT_APP_VIDEO_HPP

#include <EGL/egl.h>
#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace app {

/* Host (desktop) window options; ignored on the head unit, where the window
 * covers the panel. */
struct VideoConfig {
  const char* host_title; /* default "NBT App (Host GLES2)" */
  int host_width;         /* default 1280 */
  int host_height;        /* default 480 */
};

/* Keys of the host keyboard: printable ASCII (letters lower-case), or one of
 * the named keys below. */
enum HostKey {
  kHostKeyNone = 0,
  kHostKeyEscape = 0x100,
  kHostKeyReturn,
  kHostKeyBackspace,
  kHostKeyTab,
  kHostKeyUp,
  kHostKeyDown,
  kHostKeyLeft,
  kHostKeyRight,
  kHostKeyLeftCtrl,
  kHostKeyRightCtrl,
  kHostKeyLeftShift,
  kHostKeyRightShift
};

struct HostEvent {
  enum Type { kNone = 0, kKeyDown, kKeyUp, kResize, kClose };
  Type type;
  int key;    /* HostKey, for kKeyDown and kKeyUp */
  int width;  /* for kResize */
  int height; /* for kResize */
};

/* The HostKey for an X11 KeySym (kHostKeyNone when unmapped). */
int host_key_from_x11_keysym(unsigned long keysym);

/* Platform handles; internal. */
struct VideoNative {
  void* screen_ctx;
  void* screen_win;
  void* kd_lib;
  void* kd_win;
  void* x_display;
  uintptr_t x_window;
};

/* An OpenGL ES 2 window and context. On the head unit it opens a full-panel
 * window bound to the manager's surface (NBT_SURFACE): libKD first, QNX Screen
 * otherwise. On a Linux host it opens an X11 window. */
class VideoEgl {
 public:
  VideoEgl();
  ~VideoEgl();

  /* Opens the window, makes the context current and shows a black frame (so
   * the manager can bind the surface at once). */
  bool init(int surface_id, const VideoConfig* config = NULL);
  void swap();
  void shutdown();

  /* The next host event; false when there is none (always on the head unit). */
  bool poll_event(HostEvent* out);

  bool ready() const { return ready_; }
  int window_width() const { return win_w_; }
  int window_height() const { return win_h_; }
  EGLDisplay display() const { return dpy_; }
  EGLSurface surface() const { return surf_; }
  EGLContext context() const { return ctx_; }

 private:
  VideoEgl(const VideoEgl&);
  VideoEgl& operator=(const VideoEgl&);

  bool open_host_window(const VideoConfig& config);
  bool init_host_egl();

  bool ready_;
  int win_w_;
  int win_h_;
  EGLDisplay dpy_;
  EGLSurface surf_;
  EGLContext ctx_;
  VideoNative native_;
};

}  // namespace app
}  // namespace nbt

#endif /* NBT_APP_VIDEO_HPP */
