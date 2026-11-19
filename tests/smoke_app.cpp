/* A minimal application on the SDK, driven by tests/test_smoke.py. It prints
 * one event per line on stdout:
 *
 *   start mode=<check|user> surface=<id> caps=<list>
 *   settings gen=<n> <key>=<value>,...
 *   controller keys=<hex> pressed=<hex> released=<hex> rotation=<n> link=<0|1>
 *   back <tap|double|hold|long>
 *   gamepad seq=<n> buttons=<hex> link=<0|1>
 *   video <w>x<h>          (with --video)
 *   key <down|up> <key>    (host keyboard, with --video)
 *   audio active=<0|1> rate=<hz>   (with --audio)
 *   show
 *   stop
 *
 * Options: --crash (fault after installing the crash handler), --video,
 * --audio, --gesture <tap>,<double>,<hold>,<long> (milliseconds), and
 * --status-file <path> (its first line is the status text, read each tick). */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <GLES2/gl2.h>

#include "nbt/app/audio_mixer.hpp"
#include "nbt/app/back_gesture.hpp"
#include "nbt/app/clock.hpp"
#include "nbt/app/lifecycle.hpp"
#include "nbt/app/log.hpp"
#include "nbt/app/parse.hpp"
#include "nbt/app/video.hpp"
#include "nbt/controller/input.hpp"
#include "nbt/gamepad/input.hpp"
#include "nbt/gles2/gles2_compat.h"

namespace {

using nbt::app::BackGesture;

struct Options {
  bool crash;
  bool video;
  bool audio;
  const char* status_file;
  nbt::app::BackGestureConfig gesture;
};

void say(const char* fmt, ...) NBT_APP_PRINTF(1, 2);
void say(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stdout, fmt, ap);
  va_end(ap);
  fputc('\n', stdout);
  fflush(stdout);
}

void print_settings(const nbt::app::Lifecycle& lc) {
  char line[512];
  size_t used = 0;
  line[0] = '\0';
  const nbt::app::Settings& s = lc.settings();
  for (size_t i = 0; i < s.count() && used < sizeof(line); ++i) {
    const int n = snprintf(line + used, sizeof(line) - used, "%s%s=%s", i ? "," : "", s.at(i).key,
                           s.at(i).value);
    if (n < 0) break;
    used += static_cast<size_t>(n);
  }
  say("settings gen=%u %s", static_cast<unsigned>(lc.settings_generation()), line);
}

void read_status(const char* path, char* out, size_t size) {
  snprintf(out, size, "running");
  if (!path) return;
  FILE* fp = fopen(path, "r");
  if (!fp) return;
  if (fgets(out, static_cast<int>(size), fp)) out[strcspn(out, "\n")] = '\0';
  fclose(fp);
}

bool parse_gesture(const char* text, nbt::app::BackGestureConfig* out) {
  unsigned v[4];
  if (!text || sscanf(text, "%u,%u,%u,%u", &v[0], &v[1], &v[2], &v[3]) != 4) return false;
  out->tap_max_ms = v[0];
  out->double_tap_ms = v[1];
  out->hold_ms = v[2];
  out->long_hold_ms = v[3];
  return true;
}

const char* gesture_name(BackGesture::Event e) {
  switch (e) {
    case BackGesture::kTap:
      return "tap";
    case BackGesture::kDoubleTap:
      return "double";
    case BackGesture::kHold:
      return "hold";
    case BackGesture::kLongHold:
      return "long";
    default:
      return NULL;
  }
}

struct ControllerContext {
  BackGesture* back;
  uint64_t now;
};

void on_controller(void* context, const nbt_controller_wire_state& state,
                   const nbt::controller::Event& event) {
  ControllerContext* c = static_cast<ControllerContext*>(context);
  say("controller keys=%08x pressed=%08x released=%08x rotation=%d link=%u",
      static_cast<unsigned>(state.keys), static_cast<unsigned>(event.pressed),
      static_cast<unsigned>(event.released), static_cast<int>(event.rotation),
      static_cast<unsigned>(state.connected));
  const char* name = gesture_name(c->back->update((state.keys & NBT_CONTROLLER_BACK) != 0, c->now));
  if (name) say("back %s", name);
}

void on_gamepad(void* context, const nbt_gamepad_wire_state& state) {
  (void)context;
  say("gamepad seq=%u buttons=%08x link=%u", static_cast<unsigned>(state.sequence),
      static_cast<unsigned>(state.buttons), static_cast<unsigned>(state.connected));
}

const char* key_name(int key, char* buf, size_t size) {
  switch (key) {
    case nbt::app::kHostKeyEscape:
      return "escape";
    case nbt::app::kHostKeyReturn:
      return "return";
    case nbt::app::kHostKeyUp:
      return "up";
    case nbt::app::kHostKeyDown:
      return "down";
    default:
      break;
  }
  snprintf(buf, size, "%d", key);
  return buf;
}

}  // namespace

int main(int argc, char** argv) {
  Options opt;
  memset(&opt, 0, sizeof(opt));
  nbt::app::back_gesture_defaults(&opt.gesture);
  for (int i = 1; i < argc; ++i) {
    if (nbt::app::Lifecycle::is_bridge_arg(argv[i])) {
      ++i;
    } else if (!strcmp(argv[i], "--crash")) {
      opt.crash = true;
    } else if (!strcmp(argv[i], "--video")) {
      opt.video = true;
    } else if (!strcmp(argv[i], "--audio")) {
      opt.audio = true;
    } else if (!strcmp(argv[i], "--gesture") && i + 1 < argc) {
      if (!parse_gesture(argv[++i], &opt.gesture)) return 2;
    } else if (!strcmp(argv[i], "--status-file") && i + 1 < argc) {
      opt.status_file = argv[++i];
    }
  }

  nbt::app::log_init("smoke");
  nbt::app::Lifecycle lifecycle;
  const int rc = lifecycle.init(argc, argv);
  if (rc != 0) return rc;
  nbt::app::log_install_crash_handlers();
  if (opt.crash) {
    nbt::app::log_stage("crashing");
    raise(SIGSEGV);
    return 3;
  }

  say("start mode=%s surface=%d caps=%s", lifecycle.is_check_mode() ? "check" : "user",
      lifecycle.surface_id(), lifecycle.bridge_caps());
  print_settings(lifecycle);

  nbt::app::VideoEgl video;
  if (opt.video && lifecycle.is_user_mode()) {
    const nbt::app::VideoConfig cfg = {"NBT SDK smoke", 640, 240};
    if (!video.init(lifecycle.surface_id(), &cfg) || !gles2_batch_init()) {
      say("video failed");
      return 1;
    }
    say("video %dx%d", video.window_width(), video.window_height());
  }

  nbt::app::AudioMixer mixer;
  if (opt.audio && lifecycle.is_user_mode()) {
    mixer.init(nbt::app::kAudioDefaultRate, true);
    say("audio active=%d rate=%d", mixer.audio_active() ? 1 : 0, mixer.sample_rate());
  }

  if (!lifecycle.send_ready()) return 1;

  BackGesture back;
  back.configure(opt.gesture);
  nbt::controller::Input controller;
  nbt::gamepad::Input gamepad;
  char status[64];

  while (!lifecycle.stopping()) {
    const uint64_t now = nbt::app::monotonic_ms();
    if (lifecycle.controller_fd() >= 0) {
      ControllerContext ctx = {&back, now};
      controller.poll(lifecycle.controller_fd(), &on_controller, &ctx);
    }
    const char* name = gesture_name(back.poll(now));
    if (name) say("back %s", name);
    if (lifecycle.gamepad_fd() >= 0) gamepad.poll(lifecycle.gamepad_fd(), now, &on_gamepad, NULL);

    if (video.ready()) {
      nbt::app::HostEvent ev;
      while (video.poll_event(&ev)) {
        char buf[16];
        if (ev.type == nbt::app::HostEvent::kKeyDown || ev.type == nbt::app::HostEvent::kKeyUp) {
          say("key %s %s", ev.type == nbt::app::HostEvent::kKeyDown ? "down" : "up",
              key_name(ev.key, buf, sizeof(buf)));
        } else if (ev.type == nbt::app::HostEvent::kClose) {
          lifecycle.request_stop();
        }
      }
      glClearColor(0.1f, 0.2f, 0.4f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      video.swap();
    }

    read_status(opt.status_file, status, sizeof(status));
    bool changed = false;
    lifecycle.tick(now, status, &changed);
    if (changed) print_settings(lifecycle);
    if (lifecycle.consume_show_requested()) say("show");
    usleep(10000);
  }

  say("stop");
  if (video.ready()) gles2_batch_shutdown();
  video.shutdown();
  mixer.shutdown();
  lifecycle.close_fds();
  return 0;
}
