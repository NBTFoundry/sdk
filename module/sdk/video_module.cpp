// SPDX-License-Identifier: MPL-2.0
// VideoEgl for app modules: the runner opens the window and the OpenGL ES 2
// context (on the calling thread) and swaps; gl* calls go straight to its
// driver through the module's GL table. Same interface as src/video_qnx.cpp.
#include <string.h>

#include "nbt/app/log.hpp"
#include "nbt/app/video.hpp"
#include "nbt_module.h"

extern "C" int nbt_gl_load(void);

namespace nbt {
namespace app {

int host_key_from_x11_keysym(unsigned long keysym) {
  (void)keysym; /* X11 keys reach a module already translated (poll_event) */
  return kHostKeyNone;
}

VideoEgl::VideoEgl()
    : ready_(false),
      win_w_(0),
      win_h_(0),
      dpy_(EGL_NO_DISPLAY),
      surf_(EGL_NO_SURFACE),
      ctx_(EGL_NO_CONTEXT) {
  memset(&native_, 0, sizeof(native_));
}

VideoEgl::~VideoEgl() {
  shutdown();
}

bool VideoEgl::init(int surface_id, const VideoConfig* config) {
  (void)config; /* window options are the runner's */
  if (ready_) return true;
  if (!NBT_HAS(video_open)) {
    log_info("video: the runner has no video service");
    return false;
  }
  int32_t w = 0, h = 0;
  const int32_t rc = nbt_host->video_open(surface_id, &w, &h);
  if (rc != 0) {
    log_info("video: the runner could not open the window (%d)", static_cast<int>(rc));
    return false;
  }
  const int missing = nbt_gl_load();
  if (missing) log_info("video: %d OpenGL ES entry points missing", missing);
  win_w_ = w;
  win_h_ = h;
  ready_ = true;
  return true;
}

void VideoEgl::swap() {
  if (ready_) nbt_host->video_swap();
}

void VideoEgl::shutdown() {
  if (!ready_) return;
  nbt_host->video_close();
  ready_ = false;
}

bool VideoEgl::poll_event(HostEvent* out) {
  if (!ready_ || !out || !NBT_HAS(video_poll_event)) return false;
  nbt_video_event ev;
  memset(&ev, 0, sizeof(ev));
  if (nbt_host->video_poll_event(&ev) <= 0 || ev.type <= 0 || ev.type > HostEvent::kClose) return false;
  out->type = static_cast<HostEvent::Type>(ev.type);
  out->key = ev.key;
  out->width = ev.width;
  out->height = ev.height;
  if (out->type == HostEvent::kResize) {
    win_w_ = ev.width;
    win_h_ = ev.height;
  }
  return true;
}

bool VideoEgl::open_host_window(const VideoConfig&) {
  return false;
}

bool VideoEgl::init_host_egl() {
  return false;
}

}  // namespace app
}  // namespace nbt
