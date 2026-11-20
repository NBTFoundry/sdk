// SPDX-License-Identifier: MPL-2.0
// nbt-runner: runs an NBT application built without the QNX SDP.
//
//   nbt-runner [--ready-fd N --lease-fd N] [--nbt-payload PATH] [arguments...]
//
// The runner is an ordinary NBT Manager application (nbt/app/lifecycle.hpp):
// it takes the bridge, then loads the payload and gives it the host ABI
// (include/nbt/host_abi.h), backed by the SDK's QNX classes:
//  - a module (ET_DYN, note mode 1) is loaded in this process with dlopen and
//    called directly;
//  - a guest (ET_EXEC, mode 2) runs as a child process with no QNX code in it
//    and asks for the same services by message (runner/guest_server.c).
// The payload is --nbt-payload, else one appended to this executable
// (test packages, nbt/app/payload.h). The runner is the only application-side
// code linked with QNX libraries.
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <EGL/egl.h>

#include "fake_gl.h"
#include "host_sys.h"
#include "membench.h"
#include "nbt/app/audio_pcm.hpp"
#include "nbt/app/child.hpp"
#include "nbt/app/lifecycle.hpp"
#include "nbt/app/log.hpp"
#include "nbt/app/payload.h"
#include "nbt/app/video.hpp"
#include "nbt/host_abi.h"
#include "run.h"

using nbt::app::AudioPcm;
using nbt::app::Lifecycle;
using nbt::app::VideoEgl;

namespace {

Lifecycle* g_lc;
VideoEgl* g_video;
AudioPcm* g_audio;
nbt_host_v1 g_host;

/* --- application services --- */

int32_t app_info(nbt_app_info* out) {
  memset(out, 0, sizeof(*out));
  uint32_t f = 0;
  if (g_lc->is_check_mode()) f |= NBT_APP_CHECK;
  if (g_lc->is_user_mode()) f |= NBT_APP_USER;
  if (g_lc->can_show()) f |= NBT_APP_CAN_SHOW;
  if (g_lc->status_fd() >= 0) f |= NBT_APP_STATUS;
  if (g_lc->controller_fd() >= 0) f |= NBT_APP_CONTROLLER;
  if (g_lc->gamepad_fd() >= 0) f |= NBT_APP_GAMEPAD;
  out->flags = f;
  out->surface_id = g_lc->surface_id();
  snprintf(out->caps, sizeof(out->caps), "%s", g_lc->bridge_caps());
  return 0;
}

int32_t app_settings(char* buf, uint32_t len, uint32_t* generation) {
  const nbt::app::Settings& s = g_lc->settings();
  size_t pos = 0;
  int n = snprintf(buf, len, "NBTVALUES1\n");
  if (n < 0 || static_cast<uint32_t>(n) >= len) return -NBT_ENOSPC;
  pos = static_cast<size_t>(n);
  for (size_t i = 0; i < s.count(); ++i) {
    n = snprintf(buf + pos, len - pos, "%s=%s\n", s.at(i).key, s.at(i).value);
    if (n < 0 || pos + static_cast<size_t>(n) >= len) return -NBT_ENOSPC;
    pos += static_cast<size_t>(n);
  }
  if (generation) *generation = g_lc->settings_generation();
  return static_cast<int32_t>(pos);
}

int32_t app_ready() {
  return g_lc->send_ready() ? 0 : -NBT_EIO;
}

uint32_t events(bool settings_changed) {
  uint32_t ev = 0;
  if (g_lc->stopping()) ev |= NBT_APP_EV_STOP;
  if (g_lc->consume_show_requested()) ev |= NBT_APP_EV_SHOW;
  if (settings_changed) ev |= NBT_APP_EV_SETTINGS;
  return ev;
}

uint32_t app_tick(const char* status_text) {
  bool changed = false;
  g_lc->tick(runner_now_ms(), status_text, &changed);
  return events(changed);
}

uint32_t app_poll() {
  return events(false);
}

void app_status(const char* text) {
  g_lc->send_status(runner_now_ms(), text);
}

void app_request_stop() {
  g_lc->request_stop();
}

int32_t bridge_recv(int32_t source, void* buf, uint32_t len) {
  const int fd = source == NBT_BRIDGE_CONTROLLER ? g_lc->controller_fd()
                 : source == NBT_BRIDGE_GAMEPAD  ? g_lc->gamepad_fd()
                                                 : -1;
  if (fd < 0) return -NBT_EBADF;
  const ssize_t n = recv(fd, buf, len, MSG_DONTWAIT);
  if (n >= 0) return static_cast<int32_t>(n);
  return errno == EAGAIN || errno == EWOULDBLOCK ? -NBT_EAGAIN : runner_host_error(errno);
}

/* --- video and GL --- */

int32_t video_open(int32_t surface_id, int32_t* width, int32_t* height) {
  if (!g_video->init(surface_id)) return -NBT_EIO;
  *width = g_video->window_width();
  *height = g_video->window_height();
  return 0;
}

void video_swap() {
  g_video->swap();
}

void video_close() {
  g_video->shutdown();
}

int32_t video_poll_event(nbt_video_event* out) {
  nbt::app::HostEvent ev;
  if (!g_video->poll_event(&ev)) return 0;
  out->type = ev.type;
  out->key = ev.key;
  out->width = ev.width;
  out->height = ev.height;
  return 1;
}

void* gl_proc(const char* name) {
  static void* gles = NULL;
  if (!name || strncmp(name, "gl", 2) != 0) return NULL;
  if (!gles) gles = dlopen("libGLESv2.so.1", RTLD_NOW);
  void* p = gles ? dlsym(gles, name) : NULL;
  if (!p) p = reinterpret_cast<void*>(eglGetProcAddress(name));
  return p;
}

/* --- audio --- */

int32_t audio_open(int32_t preferred_rate, nbt_audio_render_fn render, void* context,
                   int32_t route_wanted, int32_t* rate, int32_t* frag_frames) {
  if (!g_audio->open(preferred_rate, reinterpret_cast<nbt::app::AudioRenderFn>(render), context,
                     route_wanted != 0))
    return -NBT_EIO;
  *rate = g_audio->sample_rate();
  *frag_frames = g_audio->frag_frames();
  return 0;
}

void audio_close() {
  g_audio->close();
}

void audio_set_route(int32_t wanted) {
  g_audio->set_route_wanted(wanted != 0);
}

/* --- payload --- */

/* The appended payload, copied where it may be loaded: the data folder, else
 * /dev/shmem (data folders may refuse execution). */
bool extract_payload(const char* data_dir, char* out, size_t size) {
  char name[32];
  snprintf(name, sizeof(name), ".nbt-payload-%d", static_cast<int>(getpid()));
  const char* dirs[] = {data_dir, "/dev/shmem"};
  for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
    if (!dirs[i] || !dirs[i][0]) continue;
    const char* why = NULL;
    const int rc = nbt::app::extract_own_payload(dirs[i], name, out, size, &why);
    if (rc == 1) return true;
    if (rc == 0) {
      nbt::app::log_info("payload: none appended to this executable");
      break;
    }
    nbt::app::log_info("payload: %s (%s)", why ? why : "?", dirs[i]);
  }
  out[0] = '\0';
  return false;
}

/* NBT_RUNNER_BENCH=1: the head unit's libc.so.3 on the car probe's loops
 * (common/membench.c), for comparing with the module's picolibc. */
void bench_native() {
  nbt_membench m;
  if (nbt_membench_run(&m, g_host.monotonic_ns) != 0) return;
  nbt::app::log_info("libc.so.3: memcpy %.0f MB/s, memset %.0f MB/s, %.0f ns per malloc+free",
                     m.memcpy_mbs, m.memset_mbs, m.malloc_ns);
}

}  // namespace

int main(int argc, char** argv) {
  nbt::app::log_init("nbt-runner");
  nbt::app::log_install_crash_handlers();
  static Lifecycle lc;
  static VideoEgl video;
  static AudioPcm audio;
  g_lc = &lc;
  g_video = &video;
  g_audio = &audio;
  const int init = lc.init(argc, argv);
  if (init != 0) return init;

  const char* data_dir = lc.data_dir();
  if (runner_sys_init(data_dir[0] ? data_dir : "/tmp", lc.files_dir()) != 0) return 1;
  memset(&g_host, 0, sizeof(g_host));
  runner_sys_fill(&g_host);
  g_host.app_info = app_info;
  g_host.app_settings = app_settings;
  g_host.app_ready = app_ready;
  g_host.app_tick = app_tick;
  g_host.app_poll = app_poll;
  g_host.app_status = app_status;
  g_host.app_request_stop = app_request_stop;
  g_host.bridge_recv = bridge_recv;
  g_host.video_open = video_open;
  g_host.video_swap = video_swap;
  g_host.video_close = video_close;
  g_host.video_poll_event = video_poll_event;
  g_host.gl_proc = gl_proc;
  g_host.audio_open = audio_open;
  g_host.audio_close = audio_close;
  g_host.audio_set_route = audio_set_route;
  const char* fake = getenv("NBT_RUNNER_FAKE_GL");
  if (fake && strcmp(fake, "1") == 0) runner_fake_gl(&g_host); /* tests without a GPU */
  const char* fake_audio = getenv("NBT_RUNNER_FAKE_AUDIO");
  if (fake_audio && strcmp(fake_audio, "1") == 0)
    runner_fake_audio(&g_host); /* without a sound card */

  /* The payload, and the application's own arguments (no bridge ones). */
  char payload[1024] = "";
  char* app_argv[64];
  int app_argc = 1;
  for (int i = 1; i < argc && app_argc < 63; ++i) {
    if (Lifecycle::is_bridge_arg(argv[i]) && i + 1 < argc) {
      ++i;
    } else if (strcmp(argv[i], "--nbt-payload") == 0 && i + 1 < argc) {
      snprintf(payload, sizeof(payload), "%s", argv[++i]);
    } else {
      app_argv[app_argc++] = argv[i];
    }
  }
  bool extracted = false;
  if (!payload[0]) extracted = extract_payload(data_dir, payload, sizeof(payload));
  if (!payload[0]) {
    nbt::app::log_info("no application payload");
    return 2;
  }
  nbt_payload_info info;
  const char* why = NULL;
  if (nbt_payload_read(payload, &info, &why) != 0) {
    nbt::app::log_info("%s: %s", payload, why ? why : "unreadable");
    return 1;
  }
  if (info.abi_major != NBT_HOST_ABI_MAJOR || info.abi_minor > NBT_HOST_ABI_MINOR) {
    nbt::app::log_info("needs runtime %u.%u; this runner has %d.%d", info.abi_major, info.abi_minor,
                       NBT_HOST_ABI_MAJOR, NBT_HOST_ABI_MINOR);
    return 1;
  }
  const char* name = getenv("NBT_APP_ID");
  app_argv[0] = const_cast<char*>(name && name[0] ? name : "app");
  app_argv[app_argc] = NULL;

  const char* bench = getenv("NBT_RUNNER_BENCH");
  if (bench && strcmp(bench, "1") == 0) bench_native();

  int status;
  if (info.mode == NBT_PAYLOAD_MODULE) {
    nbt::app::log_stage("app");
    status = runner_run_module(payload, app_argc, app_argv, &g_host);
  } else {
    status = runner_run_guest(payload, app_argv, &g_host, 0);
  }
  nbt::app::log_info("application ended, status %d", status);
  audio.close();
  video.shutdown();
  lc.close_fds();
  if (extracted) unlink(payload);
  return status;
}
