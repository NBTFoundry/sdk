// SPDX-License-Identifier: MPL-2.0
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "nbt/app/audio_a2i.hpp"
#include "nbt/app/audio_mix.hpp"
#include "nbt/app/audio_pcm.hpp"
#include "nbt/app/audio_mixer.hpp"
#include "nbt/app/audio_route.hpp"
#include "nbt/app/back_gesture.hpp"
#include "nbt/app/bridge.hpp"
#include "nbt/app/child.hpp"
#include "nbt/app/input_mixer.hpp"
#include "nbt/app/car_data.hpp"
#include "nbt/app/companion.hpp"
#include "nbt/app/diag.hpp"
#include "nbt/diag_wire.h"
#include "nbt/app/lifecycle.hpp"
#include "nbt/app/log.hpp"
#include "nbt/app/parse.hpp"
#include "nbt/app/payload.h"
#include "nbt/app/settings.hpp"
#include "nbt/app/version.hpp"
#include "nbt/app/video.hpp"
#include "nbt/controller/input.hpp"
#include "nbt/gamepad/input.hpp"
#include "nbt/gles2/gles2_compat.h"
#include "nbt/gles2/gles2_matrix.h"

using namespace nbt::app;

namespace {

int g_tests_run = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      exit(1);                                                        \
    }                                                                 \
  } while (0)

bool nearf(float a, float b, float eps = 1e-4f) {
  return fabsf(a - b) <= eps;
}

struct Recorded : public InputSink {
  InputAction items[64];
  int count;
  Recorded() : count(0) {}
  void clear() { count = 0; }
  void on_action(const InputAction& action) {
    if (count < 64) items[count++] = action;
  }
};

void make_temp_dir(char* path, size_t size, const char* name) {
  snprintf(path, size, "/tmp/nbt-sdk-%s-XXXXXX", name);
  CHECK(mkdtemp(path) != NULL);
}

void write_file(const char* path, const char* text, mode_t mode) {
  const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
  CHECK(fd >= 0);
  const size_t len = strlen(text);
  CHECK(write(fd, text, len) == static_cast<ssize_t>(len));
  CHECK(fchmod(fd, mode) == 0);
  CHECK(close(fd) == 0);
}

void test_gles2_matrix_and_batch() {
  gles2_mat_reset();
  const float* m = gles2_mat_top(NBT_GL_MODELVIEW);
  CHECK(nearf(m[0], 1.0f) && nearf(m[5], 1.0f) && nearf(m[10], 1.0f) && nearf(m[15], 1.0f));

  gles2_mat_mode(NBT_GL_MODELVIEW);
  gles2_mat_push();
  gles2_mat_translate(10.0f, -5.0f, 2.0f);
  gles2_mat_scale(2.0f, 3.0f, 4.0f);
  m = gles2_mat_top(NBT_GL_MODELVIEW);
  CHECK(nearf(m[0], 2.0f) && nearf(m[5], 3.0f) && nearf(m[10], 4.0f));
  CHECK(nearf(m[12], 10.0f) && nearf(m[13], -5.0f) && nearf(m[14], 2.0f));
  gles2_mat_pop();
  m = gles2_mat_top(NBT_GL_MODELVIEW);
  CHECK(nearf(m[12], 0.0f) && nearf(m[0], 1.0f));

  /* Underflow and overflow are clamped safely. */
  gles2_mat_pop();
  gles2_mat_mode(NBT_GL_PROJECTION);
  for (int i = 0; i < 64; i++) gles2_mat_push();
  gles2_mat_reset();

  gles2_mat_mode(NBT_GL_PROJECTION);
  gles2_mat_load_identity();
  gles2_mat_ortho(0.0f, 320.0f, 200.0f, 0.0f, -1.0f, 1.0f);
  m = gles2_mat_top(NBT_GL_PROJECTION);
  CHECK(nearf(m[0], 2.0f / 320.0f));
  CHECK(nearf(m[5], -2.0f / 200.0f));
  CHECK(nearf(m[12], -1.0f));
  CHECK(nearf(m[13], 1.0f));

  float mvp[16];
  gles2_mat_mvp(mvp);
  CHECK(nearf(mvp[0], 2.0f / 320.0f) && nearf(mvp[12], -1.0f));

  /* Quad index fan expansion */
  unsigned short idx[16];
  CHECK(gles2_expand_quad_indices(0, idx, 16) == 0);
  CHECK(gles2_expand_quad_indices(3, idx, 16) == 0);
  CHECK(gles2_expand_quad_indices(4, idx, 16) == 6);
  CHECK(idx[0] == 0 && idx[1] == 1 && idx[2] == 2 && idx[3] == 0 && idx[4] == 2 && idx[5] == 3);
  CHECK(gles2_expand_quad_indices(8, idx, 16) == 12);
  CHECK(idx[6] == 4 && idx[11] == 7);
  CHECK(gles2_expand_quad_indices(8, idx, 8) == 0);
  g_tests_run++;
}

void test_readers() {
  /* Gamepad wire (24 bytes) and axis scaling. */
  uint8_t bytes[NBT_GAMEPAD_WIRE_SIZE + 1];
  nbt_gamepad_wire_state st;
  memset(&st, 0, sizeof(st));
  st.connected = 1;
  st.lx = -32768;
  CHECK(nbt_gamepad_wire_encode(&st, bytes, sizeof(bytes)) == 24);
  nbt_gamepad_wire_state back;
  CHECK(!nbt_gamepad_wire_decode(bytes, 24, &back) && back.lx == -32768);
  CHECK(nbt_gamepad_wire_decode(bytes, 23, &back) < 0);
  CHECK(nbt_gamepad_wire_decode(bytes, 25, &back) < 0);
  CHECK(nbt_gamepad_axis(6553, 6553) == 0);
  CHECK(nbt_gamepad_axis(-32768, 6553) == -32767);
  CHECK(nbt_gamepad_axis(32767, 6553) == 32767);

  /* Controller wire (16 bytes) and the reader: keys count once all were seen
   * up; a lost link releases them. */
  nbt_controller_wire_state cs;
  memset(&cs, 0, sizeof(cs));
  cs.sequence = 1;
  cs.connected = 1;
  uint8_t cbytes[NBT_CONTROLLER_WIRE_SIZE];
  CHECK(nbt_controller_wire_encode(&cs, cbytes, sizeof(cbytes)) == NBT_CONTROLLER_WIRE_SIZE);
  nbt::controller::Input input;
  nbt::controller::Event ev;
  CHECK(input.feed(cbytes, sizeof(cbytes), &ev) && !ev.pressed && !ev.released);
  cs.sequence = 2;
  cs.keys = NBT_CONTROLLER_BACK;
  cs.rotation = -2;
  CHECK(nbt_controller_wire_encode(&cs, cbytes, sizeof(cbytes)) == NBT_CONTROLLER_WIRE_SIZE);
  CHECK(input.feed(cbytes, sizeof(cbytes), &ev) && ev.pressed == NBT_CONTROLLER_BACK);
  CHECK(ev.rotation == -2);
  CHECK(!input.feed(cbytes, sizeof(cbytes) - 1, &ev));
  cs.sequence = 3;
  cs.keys = 0;
  cs.connected = 0;
  CHECK(nbt_controller_wire_encode(&cs, cbytes, sizeof(cbytes)) == NBT_CONTROLLER_WIRE_SIZE);
  CHECK(input.feed(cbytes, sizeof(cbytes), &ev) && ev.released == NBT_CONTROLLER_BACK);
  g_tests_run++;
}

void test_input_mixer() {
  Recorded rec;
  InputMixer mixer;
  mixer.set_sink(&rec);
  const InputAction down = {InputAction::kKeyDown, 42};
  const InputAction up = {InputAction::kKeyUp, 42};
  mixer.action(0, down);
  mixer.action(1, down);
  CHECK(rec.count == 1);
  mixer.action(1, up); /* source 0 still holds it */
  CHECK(rec.count == 1);
  mixer.release(0);
  CHECK(rec.count == 2 && rec.items[1].type == InputAction::kKeyUp && rec.items[1].value == 42);
  const InputAction app = {InputAction::kAppAction + 3, 7};
  mixer.action(2, app);
  CHECK(rec.count == 3 && rec.items[2].type == InputAction::kAppAction + 3);
  /* Out of range sources and keys are ignored. */
  mixer.action(InputMixer::kMaxSources, down);
  const InputAction bad = {InputAction::kKeyDown, InputMixer::kMaxKeys};
  mixer.action(0, bad);
  CHECK(rec.count == 3);
  mixer.action(3, down);
  mixer.release_all();
  CHECK(rec.count == 5 && rec.items[4].type == InputAction::kKeyUp);
  g_tests_run++;
}

void test_back_gesture() {
  BackGesture g;
  /* Default timings: tap, then a second tap within 350 ms is a double tap. */
  CHECK(g.update(true, 950) == BackGesture::kNone && g.held());
  CHECK(g.update(false, 990) == BackGesture::kTap);
  CHECK(g.update(true, 1050) == BackGesture::kNone);
  CHECK(g.update(false, 1090) == BackGesture::kDoubleTap);
  /* A third tap starts over. */
  CHECK(g.update(true, 1200) == BackGesture::kNone);
  CHECK(g.update(false, 1250) == BackGesture::kTap);
  /* Hold at 600 ms, long hold at 1400 ms, each once; no tap on release. */
  CHECK(g.update(true, 2000) == BackGesture::kNone);
  CHECK(g.poll(2599) == BackGesture::kNone);
  CHECK(g.poll(2650) == BackGesture::kHold);
  CHECK(g.poll(3300) == BackGesture::kNone);
  CHECK(g.poll(3450) == BackGesture::kLongHold);
  CHECK(g.poll(3500) == BackGesture::kNone);
  CHECK(g.update(false, 3500) == BackGesture::kNone);
  /* A release after 400 ms without a hold is nothing. */
  CHECK(g.update(true, 4000) == BackGesture::kNone);
  CHECK(g.update(false, 4450) == BackGesture::kNone);
  /* Without double taps (e.g. in a menu) every short release is a tap. */
  CHECK(g.update(true, 5000) == BackGesture::kNone);
  CHECK(g.update(false, 5050, false) == BackGesture::kTap);
  CHECK(g.update(true, 5100) == BackGesture::kNone);
  CHECK(g.update(false, 5150, false) == BackGesture::kTap);

  /* Custom timings, long hold off. */
  BackGestureConfig cfg;
  back_gesture_defaults(&cfg);
  CHECK(cfg.tap_max_ms == 400 && cfg.double_tap_ms == 350 && cfg.hold_ms == 600 &&
        cfg.long_hold_ms == 1400);
  cfg.tap_max_ms = 100;
  cfg.hold_ms = 200;
  cfg.long_hold_ms = 0;
  BackGesture c;
  c.configure(cfg);
  CHECK(c.update(true, 0) == BackGesture::kNone);
  CHECK(c.update(false, 150) == BackGesture::kNone);
  CHECK(c.update(true, 1000) == BackGesture::kNone);
  CHECK(c.poll(1200) == BackGesture::kHold);
  CHECK(c.poll(9000) == BackGesture::kNone);
  c.reset();
  CHECK(!c.held());
  g_tests_run++;
}

void test_parse_and_bridge() {
  int v = 0;
  CHECK(parse_int("42", 3, &v) && v == 42);
  CHECK(!parse_int("2", 3, &v) && !parse_int("4x", 0, &v) && !parse_int("", 0, &v));
  CHECK(!parse_int(NULL, 0, &v) && !parse_int("99999999999", 0, &v));
  CHECK(parse_int_range("4", 0, 4, &v) && v == 4 && !parse_int_range("5", 0, 4, &v));
  bool b = false;
  CHECK(parse_bool("on", &b) && b && parse_bool("off", &b) && !b);
  CHECK(parse_bool("yes", &b) && b && parse_bool("0", &b) && !b);
  CHECK(!parse_bool("maybe", &b) && !parse_bool(NULL, &b));

  CHECK(bridge_cap_has("show,status,gamepad", "gamepad"));
  CHECK(bridge_cap_has("show", "show") && !bridge_cap_has("showx", "show"));
  CHECK(!bridge_cap_has("show,status", "stat") && !bridge_cap_has(NULL, "show"));
  CHECK(!bridge_cap_has("show", NULL) && !bridge_cap_has("", "show"));

  char clean[kBridgeStatusMax + 1];
  CHECK(bridge_status_sanitize("E1M1\tH:100%", clean) == 11 && !strcmp(clean, "E1M1 H:100%"));
  CHECK(bridge_status_sanitize("0123456789012345678901234567", clean) == 24);
  CHECK(bridge_status_sanitize(NULL, clean) == 0 && clean[0] == '\0');
  char line[64];
  CHECK(bridge_status_format(12, "Menu", line, sizeof(line)) == 8 && !strcmp(line, "12 Menu\n"));
  CHECK(bridge_status_format(1, "", line, sizeof(line)) < 0);
  CHECK(bridge_status_format(1, "bad\n", line, sizeof(line)) < 0);
  CHECK(bridge_status_format(1, "0123456789012345678901234", line, sizeof(line)) < 0);
  CHECK(bridge_status_format(123456789, "x", line, 4) < 0);

  int sockets[2];
  CHECK(socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets) == 0);
  char fd[24];
  snprintf(fd, sizeof(fd), "%d", sockets[1]);
  CHECK(datagram_input_fd(fd, sockets, 1) == sockets[1]);
  CHECK(fcntl(sockets[1], F_GETFL) & O_NONBLOCK);
  CHECK(datagram_input_fd(fd, sockets, 2) == -1);
  CHECK(datagram_input_fd("invalid", sockets, 2) == -1);
  int pipes[2];
  CHECK(pipe(pipes) == 0);
  snprintf(fd, sizeof(fd), "%d", pipes[0]);
  CHECK(datagram_input_fd(fd, NULL, 0) == -1); /* not a datagram socket */
  close(pipes[0]);
  close(pipes[1]);
  close(sockets[0]);
  close(sockets[1]);
  g_tests_run++;
}

void test_settings() {
  CHECK(setting_key_valid("turn_sens") && !setting_key_valid("Turn") && !setting_key_valid("1a"));
  CHECK(!setting_key_valid("a23456789012345678") && !setting_key_valid(""));
  CHECK(setting_value_valid("medium") && setting_value_valid("x-1") && !setting_value_valid("on!"));
  CHECK(!setting_value_valid("1234567890123") && !setting_value_valid(""));

  Settings s;
  CHECK(s.count() == 0 && s.get("sound") == NULL && !strcmp(s.get("sound", "on"), "on"));
  CHECK(s.set("sound", "on") && s.set("sound", "off") && s.count() == 1);
  CHECK(!strcmp(s.get("sound"), "off") && !s.set("BAD", "x") && !s.set("ok", "BAD"));

  /* The manager's format: header, terminated lines, all or nothing, last wins. */
  const char good[] = "NBTVALUES1\nturn_sens=high\nsound=on\nturn_sens=low\n";
  CHECK(s.parse_values(good, sizeof(good) - 1));
  CHECK(!strcmp(s.get("turn_sens"), "low") && !strcmp(s.get("sound"), "on") && s.count() == 2);
  const char* bad[] = {
      "NBTVALUES2\nsound=on\n",   "NBTVALUES1\nsound=on",   "NBTVALUES1\nsound\n",
      "NBTVALUES1\nSound=on\n",   "NBTVALUES1\nsound=ON\n", "NBTVALUES1\nsound=\n",
      "NBTVALUES1\nx=1\nbad=!\n", "NBTVALUES1\n=on\n",
  };
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
    Settings copy = s;
    CHECK(!copy.parse_values(bad[i], strlen(bad[i])));
    CHECK(copy.equals(s));
  }
  CHECK(s.parse_values("NBTVALUES1\n", 11) && s.count() == 2);
  const char with_nul[] = "NBTVALUES1\nsound=on\0\n";
  CHECK(!s.parse_values(with_nul, sizeof(with_nul) - 1));

  /* Capacity: kSettingsMax distinct keys. */
  Settings full;
  char key[16];
  for (unsigned i = 0; i < kSettingsMax; ++i) {
    snprintf(key, sizeof(key), "k%u", i);
    CHECK(full.set(key, "v"));
  }
  CHECK(!full.set("extra", "v") && full.set("k0", "w"));

  /* equals ignores order; merge overlays. */
  Settings a, b;
  CHECK(a.set("x", "1") && a.set("y", "2") && b.set("y", "2") && b.set("x", "1"));
  CHECK(a.equals(b));
  CHECK(b.set("x", "3") && !a.equals(b));
  a.merge(b);
  CHECK(!strcmp(a.get("x"), "3") && a.count() == 2);

  /* Environment: NBT_SETTING_<KEY>, lower-cased; invalid entries skipped. */
  setenv("NBT_SETTING_TURN_SENS", "high", 1);
  setenv("NBT_SETTING_BAD", "NOT VALID", 1);
  setenv("NBT_SETTINGS_FILE", "/nonexistent", 1);
  Settings env;
  CHECK(settings_load_env(&env) >= 1);
  CHECK(!strcmp(env.get("turn_sens"), "high") && env.get("bad") == NULL);
  CHECK(env.get("s_file") == NULL);
  unsetenv("NBT_SETTING_TURN_SENS");
  unsetenv("NBT_SETTING_BAD");
  unsetenv("NBT_SETTINGS_FILE");

  /* Files: private regular files only. */
  char dir[64];
  make_temp_dir(dir, sizeof(dir), "settings");
  char path[128], link_path[128], hard_path[128], fifo_path[128];
  snprintf(path, sizeof(path), "%s/settings", dir);
  snprintf(link_path, sizeof(link_path), "%s/link", dir);
  snprintf(hard_path, sizeof(hard_path), "%s/hard", dir);
  snprintf(fifo_path, sizeof(fifo_path), "%s/fifo", dir);
  const char* why = NULL;
  Settings f;
  CHECK(!settings_read_file(path, &f, &why) && why != NULL); /* missing */
  write_file(path, "NBTVALUES1\nsound=off\nbrightness=3\n", 0600);
  CHECK(settings_read_file(path, &f, &why) && why == NULL);
  CHECK(!strcmp(f.get("sound"), "off") && !strcmp(f.get("brightness"), "3"));
  CHECK(symlink(path, link_path) == 0);
  Settings g;
  CHECK(!settings_read_file(link_path, &g, &why) && g.count() == 0);
  CHECK(chmod(path, 0644) == 0);
  CHECK(!settings_read_file(path, &g, &why) && !strcmp(why, "unsafe settings file"));
  CHECK(chmod(path, 0600) == 0);
  CHECK(link(path, hard_path) == 0);
  CHECK(!settings_read_file(path, &g, &why) && !strcmp(why, "unsafe settings file"));
  CHECK(unlink(hard_path) == 0);
  CHECK(mkfifo(fifo_path, 0600) == 0);
  CHECK(!settings_read_file(fifo_path, &g, &why));
  write_file(path, "NBTVALUES1\nsound\n", 0600);
  CHECK(!settings_read_file(path, &g, &why) && !strcmp(why, "malformed settings file"));
  /* The manager's limit: the file must be shorter than 372 bytes. */
  char big[kSettingsFileMax + 1];
  memset(big, 0, sizeof(big));
  strcpy(big, "NBTVALUES1\n");
  size_t used = strlen(big);
  while (used + 16 < kSettingsFileMax) {
    used += static_cast<size_t>(snprintf(big + used, sizeof(big) - used, "k%02u=abcdefghij\n",
                                         static_cast<unsigned>(used % 97)));
  }
  while (used < kSettingsFileMax - 1) big[used++] = '\n';
  big[used] = '\0';
  write_file(path, big, 0600); /* 371 bytes: read, but blank lines are malformed */
  CHECK(!settings_read_file(path, &g, &why) && !strcmp(why, "malformed settings file"));
  big[used++] = '\n';
  big[used] = '\0';
  write_file(path, big, 0600); /* 372 bytes: too long */
  CHECK(!settings_read_file(path, &g, &why) &&
        !strcmp(why, "settings file unreadable or oversized"));
  CHECK(!settings_read_file(NULL, &g, &why));
  unlink(path);
  unlink(link_path);
  unlink(fifo_path);
  rmdir(dir);
  g_tests_run++;
}

void test_lifecycle() {
  char name[] = "app";
  char ready_flag[] = "--ready-fd";
  char lease_flag[] = "--lease-fd";

  /* Usage errors: an unpaired, repeated or malformed descriptor. */
  {
    char three[] = "3";
    char* argv[] = {name, ready_flag, three, NULL};
    Lifecycle lc;
    CHECK(lc.init(3, argv) == 2);
  }
  {
    char seven[] = "7";
    char* argv[] = {name, ready_flag, seven, lease_flag, seven, NULL};
    Lifecycle lc;
    CHECK(lc.init(5, argv) == 2);
  }
  {
    char bad[] = "x";
    char seven[] = "7";
    char* argv[] = {name, ready_flag, bad, lease_flag, seven, NULL};
    Lifecycle lc;
    CHECK(lc.init(5, argv) == 2);
  }
  CHECK(Lifecycle::is_bridge_arg("--ready-fd") && Lifecycle::is_bridge_arg("--lease-fd"));
  CHECK(!Lifecycle::is_bridge_arg("-iwad") && !Lifecycle::is_bridge_arg(NULL));

  /* A user launch without bridge descriptors; a bad NBT_SURFACE is an error. */
  char* argv[] = {name, NULL};
  setenv("NBT_SURFACE", "0", 1);
  {
    Lifecycle lc;
    CHECK(lc.init(1, argv) == 1);
  }
  setenv("NBT_SURFACE", "12", 1);

  /* User mode: status, controller and gamepad descriptors; check mode: none. */
  char dir[64];
  make_temp_dir(dir, sizeof(dir), "lifecycle");
  char status_path[128];
  snprintf(status_path, sizeof(status_path), "%s/status", dir);
  int ready[2], controller[2], gamepad[2];
  CHECK(pipe(ready) == 0);
  CHECK(socketpair(AF_UNIX, SOCK_DGRAM, 0, controller) == 0);
  CHECK(socketpair(AF_UNIX, SOCK_DGRAM, 0, gamepad) == 0);
  char lease_path[128];
  snprintf(lease_path, sizeof(lease_path), "%s/lease", dir);
  const int lease = open(lease_path, O_RDWR | O_CREAT, 0600);
  const int status = open(status_path, O_RDWR | O_CREAT, 0600);
  CHECK(lease >= 0 && status >= 0);
  char ready_text[16], lease_text[16], status_text[16], controller_text[16], gamepad_text[16];
  snprintf(ready_text, sizeof(ready_text), "%d", ready[1]);
  snprintf(lease_text, sizeof(lease_text), "%d", lease);
  snprintf(status_text, sizeof(status_text), "%d", status);
  snprintf(controller_text, sizeof(controller_text), "%d", controller[1]);
  snprintf(gamepad_text, sizeof(gamepad_text), "%d", gamepad[1]);
  char* full_argv[] = {name, ready_flag, ready_text, lease_flag, lease_text, NULL};
  setenv("NBT_BRIDGE_CAPS", "show,status,controller,gamepad,live-settings", 1);
  setenv("NBT_STATUS_FD", status_text, 1);
  setenv("NBT_CONTROLLER_FD", controller_text, 1);
  setenv("NBT_GAMEPAD_FD", gamepad_text, 1);
  setenv("NBT_DATA_DIR", dir, 1);

  setenv("NBT_LAUNCH", "check", 1);
  {
    Lifecycle lc;
    CHECK(lc.init(5, full_argv) == 0 && lc.is_check_mode() && !lc.is_user_mode());
    CHECK(lc.status_fd() == -1 && lc.controller_fd() == -1 && lc.gamepad_fd() == -1);
    CHECK(!lc.can_show() && lc.surface_id() == 0);
    lc.close_fds();
  }
  /* The check launch closed ready and lease: new ones for the user launch. */
  CHECK(pipe(ready) == 0);
  const int lease2 = open(lease_path, O_RDWR);
  CHECK(lease2 >= 0);
  snprintf(ready_text, sizeof(ready_text), "%d", ready[1]);
  snprintf(lease_text, sizeof(lease_text), "%d", lease2);

  /* A controller descriptor equal to another bridge descriptor is refused. */
  setenv("NBT_LAUNCH", "user", 1);
  setenv("NBT_CONTROLLER_FD", ready_text, 1);
  char settings_path[128];
  snprintf(settings_path, sizeof(settings_path), "%s/settings", dir);
  write_file(settings_path, "NBTVALUES1\nsound=off\n", 0600);
  setenv("NBT_SETTINGS_FILE", settings_path, 1);
  setenv("NBT_SETTING_SOUND", "on", 1);
  setenv("NBT_SETTING_BRIGHTNESS", "2", 1);
  {
    Lifecycle lc;
    CHECK(lc.init(5, full_argv) == 0 && lc.is_user_mode() && lc.can_show());
    CHECK(lc.surface_id() == 12 && lc.status_fd() == status && lc.controller_fd() == -1);
    CHECK(lc.gamepad_fd() == gamepad[1] && lc.has_cap("live-settings") && !lc.has_cap("most"));
    CHECK(!strcmp(lc.data_dir(), dir));
    /* The file overlays the environment. */
    CHECK(!strcmp(lc.settings().get("sound"), "off") &&
          !strcmp(lc.settings().get("brightness"), "2"));
    CHECK(lc.settings_generation() == 0);

    /* tick: heartbeat and status at most once a second; settings reload. */
    bool changed = true;
    lc.tick(5000, "Main\tMenu", &changed);
    CHECK(!changed);
    char buf[8];
    CHECK(read(ready[0], buf, sizeof(buf)) == 1 && buf[0] == 'R');
    char got[64];
    ssize_t n = pread(status, got, sizeof(got) - 1, 0);
    CHECK(n > 0);
    got[n] = '\0';
    CHECK(!strcmp(got, "5000 Main Menu\n"));
    write_file(settings_path, "NBTVALUES1\nsound=on\n", 0600);
    lc.tick(5500, "Ignored", &changed);
    CHECK(!changed); /* within the second */
    lc.tick(6000, "E1M1", &changed);
    CHECK(changed && lc.settings_generation() == 1 && !strcmp(lc.settings().get("sound"), "on"));
    n = pread(status, got, sizeof(got) - 1, 0);
    got[n] = '\0';
    CHECK(!strcmp(got, "6000 E1M1\n")); /* the longer old line was truncated */
    /* An unsafe file leaves the last values. */
    write_file(settings_path, "NBTVALUES1\nsound=off\n", 0644);
    lc.tick(7000, NULL, &changed);
    CHECK(!changed && !strcmp(lc.settings().get("sound"), "on"));

    /* SIGUSR1 asks to show (SIGTERM is covered by tests/test_smoke.py). */
    CHECK(!lc.consume_show_requested());
    raise(SIGUSR1);
    CHECK(lc.consume_show_requested() && !lc.consume_show_requested());

    /* A failed heartbeat (the manager closed its end) stops the application. */
    CHECK(!lc.stopping());
    close(ready[0]);
    lc.tick(9000, NULL, &changed);
    CHECK(lc.stopping());

    lc.close_fds();
    CHECK(lc.status_fd() == -1 && lc.gamepad_fd() == -1);
  }
  /* close_fds closed every bridge descriptor. */
  CHECK(fcntl(status, F_GETFD) < 0 && fcntl(gamepad[1], F_GETFD) < 0);
  CHECK(fcntl(ready[1], F_GETFD) < 0 && fcntl(lease2, F_GETFD) < 0);
  close(controller[0]);
  close(controller[1]);
  close(gamepad[0]);
  const char* vars[] = {
      "NBT_SURFACE",       "NBT_BRIDGE_CAPS",       "NBT_STATUS_FD", "NBT_CONTROLLER_FD",
      "NBT_GAMEPAD_FD",    "NBT_DATA_DIR",          "NBT_LAUNCH",    "NBT_SETTINGS_FILE",
      "NBT_SETTING_SOUND", "NBT_SETTING_BRIGHTNESS"};
  for (size_t i = 0; i < sizeof(vars) / sizeof(vars[0]); ++i) unsetenv(vars[i]);
  unlink(settings_path);
  unlink(status_path);
  unlink(lease_path);
  rmdir(dir);
  signal(SIGTERM, SIG_DFL);
  signal(SIGUSR1, SIG_DFL);
  g_tests_run++;
}

void fake_music(void* dest, unsigned nsamp) {
  int16_t* p = static_cast<int16_t*>(dest);
  for (unsigned i = 0; i < nsamp; i++) {
    p[i * 2 + 0] = 500;
    p[i * 2 + 1] = -500;
  }
}

void test_audio_mixer() {
  AudioMixer mixer;
  CHECK(mixer.init(22050, false));
  CHECK(!mixer.audio_active() && mixer.sample_rate() == 22050);
  mixer.set_music_renderer(&fake_music);
  CHECK(mixer.music_renderer() == &fake_music);

  uint8_t sfx8[256];
  memset(sfx8, 228, sizeof(sfx8)); /* +100 above the 128 midpoint */
  int slot = mixer.start_channel(0, sfx8, 256, 11025, 8, 120, 40, (11025u << 16) / 22050u);
  CHECK(slot == 0);
  CHECK(mixer.is_channel_playing(0));
  CHECK(mixer.any_channel_playing());

  int16_t out[64 * 2];
  int active = mixer.mix_frames(out, 64);
  CHECK(active == 1);
  CHECK(out[0] > 500 && out[1] < out[0]); /* left louder than right */

  int16_t sfx16[8] = {1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000};
  CHECK(mixer.start_channel(-1, sfx16, 8, 22050, 16, 127, 127, 0) == 1);
  CHECK(mixer.start_channel(-1, sfx16, 8, 22050, 12, 127, 127, 0) == -1);
  mixer.update_channel(1, 0, 0, 0);
  mixer.stop_channel(1);
  CHECK(!mixer.is_channel_playing(1) && !mixer.is_channel_playing(99));

  mixer.set_muted(true);
  CHECK(mixer.is_muted());
  mixer.mix_frames(out, 64);
  for (int i = 0; i < 64 * 2; i++) CHECK(out[i] == 0);
  mixer.set_muted(false);

  mixer.stop_all_channels();
  CHECK(!mixer.any_channel_playing());
  mixer.shutdown();

  CHECK(clamp_qnx_rate(22050, 22050, 44100) == 22050);
  CHECK(clamp_qnx_rate(22050, 44100, 48000) == 44100);
  CHECK(clamp_qnx_rate(48000, 8000, 44100) == 44100);
  CHECK(clamp_qnx_rate(22050, 0, 0) == 22050);
  CHECK(choose_qnx_frag_size(2048, 512, 8192) == 2048);
  CHECK(choose_qnx_frag_size(4000, 1024, 3000) == 2048);
  CHECK(choose_qnx_frag_size(2048, 4096, 8192) == 4096);
  CHECK(choose_qnx_frag_size(1024, 1500, 3000) == 2048);

  /* NBT_NO_AUDIO=1: no device, the mixer still mixes. */
  setenv("NBT_NO_AUDIO", "1", 1);
  AudioPcm pcm;
  CHECK(!pcm.open(22050, NULL, NULL, false) && !pcm.active());
  unsetenv("NBT_NO_AUDIO");
  g_tests_run++;
}

/* Renders a ramp: frame i holds (i, -i) as 16-bit values. */
void ramp_render(void* context, int16_t* out_lr, int frames) {
  int* next = static_cast<int*>(context);
  for (int i = 0; i < frames; ++i, ++*next) {
    out_lr[2 * i] = static_cast<int16_t>(*next);
    out_lr[2 * i + 1] = static_cast<int16_t>(-*next);
  }
}

void test_audio_a2i() {
  A2iParams p;
  a2i_fill_params(&p, "input_stereo_0", 44100, 2048, 8);
  CHECK(p.magic == 0x02000000u && p.direction == 0 && p.one == 1 && p.buffers == 8);
  CHECK(p.buffer_bytes == 2048 && p.channels == 2 && p.sample_bytes == 2 && p.rate == 44100);
  CHECK(p.stereo == 1 && p.format == 0 && p.cookie == 0x05555555u && p.timeout_ms == 185);
  CHECK(!strcmp(p.endpoint, "input_stereo_0") && p.zero_20 == 0 && p.zero_2c == 0);
  CHECK(a2i_endpoint_valid("input_stereo_0") && a2i_endpoint_valid("input_stereo_AirPlayMain"));
  CHECK(!a2i_endpoint_valid("") && !a2i_endpoint_valid(NULL) && !a2i_endpoint_valid("/input"));
  CHECK(!a2i_endpoint_valid("../x") && !a2i_endpoint_valid("a_name_that_is_far_too_long_0123"));

  /* NBT_AUDIO_A2I: AudioPcm plays through the library at 44100 Hz. */
  const char* fake = getenv("NBT_TEST_FAKE_A2I");
  CHECK(fake && fake[0]);
  void* lib = dlopen(fake, RTLD_NOW | RTLD_LOCAL); /* keeps its state past AudioPcm's dlclose */
  CHECK(lib != NULL);
  typedef void (*CountsFn)(int*);
  typedef void (*ParamsFn)(A2iParams*);
  typedef unsigned (*CapturedFn)(const unsigned char**);
  CountsFn counts = reinterpret_cast<CountsFn>(dlsym(lib, "fake_a2i_counts"));
  ParamsFn params = reinterpret_cast<ParamsFn>(dlsym(lib, "fake_a2i_params"));
  CapturedFn captured = reinterpret_cast<CapturedFn>(dlsym(lib, "fake_a2i_captured"));
  CHECK(counts && params && captured);
  setenv("NBT_A2I_LIB", fake, 1);
  setenv("NBT_AUDIO_A2I", "input_stereo_0", 1);
  setenv("NBT_AUDIO_CONN", "0", 1);
  int next = 1;
  {
    AudioPcm pcm;
    CHECK(pcm.open(22050, ramp_render, &next, false));
    CHECK(pcm.active() && pcm.sample_rate() == 44100 && pcm.frag_frames() == 512);
    const unsigned char* data = NULL;
    for (int i = 0; i < 2000 && captured(&data) < 8 * 2048 + 4 * 2048; ++i) usleep(1000);
    pcm.close();
    CHECK(!pcm.active());
  }
  int c[5];
  counts(c);
  CHECK(c[0] == 1 && c[1] == 1 && c[2] == 1 && c[3] == 1 && c[4] == 8);
  params(&p);
  CHECK(!strcmp(p.endpoint, "input_stereo_0") && p.rate == 44100 && p.buffer_bytes == 2048);
  const unsigned char* data = NULL;
  const unsigned bytes = captured(&data);
  CHECK(bytes >= 12 * 2048);
  for (unsigned i = 0; i < 8 * 2048; ++i) CHECK(data[i] == 0); /* primed silence */
  const int16_t* lr = reinterpret_cast<const int16_t*>(data + 8 * 2048);
  for (int i = 0; i < 4 * 512; ++i) {
    CHECK(lr[2 * i] == static_cast<int16_t>(i + 1) &&
          lr[2 * i + 1] == static_cast<int16_t>(-(i + 1)));
  }
  /* A name that is no io-amp input: no device. */
  setenv("NBT_AUDIO_A2I", "../input", 1);
  {
    AudioPcm pcm;
    CHECK(!pcm.open(22050, ramp_render, &next, false) && !pcm.active());
  }
  unsetenv("NBT_AUDIO_A2I");
  unsetenv("NBT_A2I_LIB");
  unsetenv("NBT_AUDIO_CONN");
  dlclose(lib);
  g_tests_run++;
}

void test_log() {
  char dir[64];
  make_temp_dir(dir, sizeof(dir), "log");
  char log_file[128];
  snprintf(log_file, sizeof(log_file), "%s/stderr", dir);
  fflush(stderr);
  const int saved_stderr = dup(STDERR_FILENO);
  const int capture = open(log_file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  CHECK(saved_stderr >= 0 && capture >= 0);
  dup2(capture, STDERR_FILENO);
  close(capture);
  CHECK(!strcmp(log_tag(), "app"));
  log_init("sdktest");
  CHECK(!strcmp(log_tag(), "sdktest"));
  log_info("plain %d", 7);
  log_set_debug(false);
  CHECK(!log_debug_enabled());
  log_debug("suppressed_debug_line");
  log_set_debug(true);
  CHECK(log_debug_enabled());
  log_debug("enabled_debug_line");
  log_stage("unit_test");
  CHECK(!strcmp(log_current_stage(), "unit_test"));
  log_init("a-very-long-tag-that-is-cut-short");
  CHECK(strlen(log_tag()) == 23);
  setenv("NBT_ROLE", "service", 1); /* an application's service says so */
  log_init("sdktest");
  CHECK(!strcmp(log_tag(), "sdktest:service"));
  unsetenv("NBT_ROLE");
  fflush(stderr);
  dup2(saved_stderr, STDERR_FILENO);
  close(saved_stderr);
  FILE* lfp = fopen(log_file, "r");
  CHECK(lfp != NULL);
  char lbuf[512];
  const size_t lread = fread(lbuf, 1, sizeof(lbuf) - 1, lfp);
  fclose(lfp);
  lbuf[lread] = '\0';
  CHECK(strstr(lbuf, "[sdktest] plain 7\n") != NULL);
  CHECK(strstr(lbuf, "suppressed_debug_line") == NULL);
  CHECK(strstr(lbuf, "[sdktest][dbg] enabled_debug_line") != NULL);
  CHECK(strstr(lbuf, "[sdktest] stage: unit_test") != NULL);
  log_set_debug(false);
  log_init("sdktest");
  unlink(log_file);
  rmdir(dir);
  g_tests_run++;
}

void test_audio_route() {
  CHECK(AudioRoute::parse_conn_env(NULL, 0xb8u) == 0xb8u);
  CHECK(AudioRoute::parse_conn_env("", 0xb8u) == 0xb8u);
  CHECK(AudioRoute::parse_conn_env("0", 0xb8u) == 0u);
  CHECK(AudioRoute::parse_conn_env("0xc7", 0xb8u) == 0xc7u);
  CHECK(AudioRoute::parse_conn_env("bogus", 0xb8u) == 0xb8u);

  /* audio-conn in the data folder names another connection. */
  char dir[64];
  make_temp_dir(dir, sizeof(dir), "conn");
  char file[128];
  snprintf(file, sizeof(file), "%s/audio-conn", dir);
  CHECK(AudioRoute::conn_file(dir, 0xb8u) == 0xb8u); /* absent */
  CHECK(AudioRoute::conn_file(NULL, 0xb8u) == 0xb8u);
  write_file(file, "0x9a\n", 0600);
  CHECK(AudioRoute::conn_file(dir, 0xb8u) == 0x9au);
  write_file(file, "browser\n", 0600); /* not a number */
  CHECK(AudioRoute::conn_file(dir, 0xb8u) == 0xb8u);
  CHECK(unlink(file) == 0);
  CHECK(mkdir(file, 0700) == 0); /* not a regular file */
  CHECK(AudioRoute::conn_file(dir, 0xb8u) == 0xb8u);
  CHECK(rmdir(file) == 0 && rmdir(dir) == 0);

  char tmp_dir[64];
  make_temp_dir(tmp_dir, sizeof(tmp_dir), "route");
  char script_path[128], log_path[128];
  snprintf(script_path, sizeof(script_path), "%s/audioconn", tmp_dir);
  snprintf(log_path, sizeof(log_path), "%s/calls.log", tmp_dir);
  FILE* sf = fopen(script_path, "w");
  CHECK(sf != NULL);
  fprintf(sf, "#!/bin/sh\necho \"$* ld=$LD_PRELOAD\" >> \"%s\"\nexit 0\n", log_path);
  fclose(sf);
  CHECK(chmod(script_path, 0700) == 0);

  setenv("NBT_AUDIOCONN", script_path, 1);
  setenv("LD_PRELOAD", "fake_preload.so", 1);
  AudioRoute route;
  CHECK(route.configure());
  CHECK(route.conn_id() == 0xb8u);
  route.start();
  route.set_wanted(true);
  for (int i = 0; i < 60 && !route.built(); ++i) usleep(25000);
  CHECK(route.built());

  route.set_wanted(false);
  for (int i = 0; i < 60 && route.built(); ++i) usleep(25000);
  CHECK(!route.built());

  route.set_wanted(true);
  for (int i = 0; i < 60 && !route.built(); ++i) usleep(25000);
  CHECK(route.built());

  route.stop();
  CHECK(!route.built());
  unsetenv("NBT_AUDIOCONN");
  unsetenv("LD_PRELOAD");

  FILE* lf = fopen(log_path, "r");
  CHECK(lf != NULL);
  char lbuf[512];
  const size_t lread = fread(lbuf, 1, sizeof(lbuf) - 1, lf);
  fclose(lf);
  lbuf[lread] = '\0';
  CHECK(strcmp(lbuf, "-b 0xb8 ld=\n-r 0xb8 ld=\n-b 0xb8 ld=\n-r 0xb8 ld=\n") == 0);

  /* A missing audioconn binary fails configure() cleanly. */
  setenv("NBT_AUDIOCONN", "/nonexistent/audioconn_missing", 1);
  AudioRoute missing_route;
  CHECK(!missing_route.configure());
  CHECK(!missing_route.built());

  /* A failing audioconn leaves the route unbuilt and stops cleanly. */
  char fail_script[128];
  snprintf(fail_script, sizeof(fail_script), "%s/audioconn_fail", tmp_dir);
  FILE* ff = fopen(fail_script, "w");
  CHECK(ff != NULL);
  fprintf(ff, "#!/bin/sh\necho failed >&2\nexit 1\n");
  fclose(ff);
  CHECK(chmod(fail_script, 0700) == 0);
  setenv("NBT_AUDIOCONN", fail_script, 1);
  AudioRoute fail_route;
  CHECK(fail_route.configure());
  fail_route.start();
  fail_route.set_wanted(true);
  usleep(120000);
  CHECK(!fail_route.built());
  fail_route.stop();
  CHECK(!fail_route.built());
  unsetenv("NBT_AUDIOCONN");
  unlink(fail_script);

  unlink(log_path);
  unlink(script_path);
  rmdir(tmp_dir);
  g_tests_run++;
}

/* The output a child wrote to `fd` until it closed it. */
void read_all(int fd, char* out, size_t size) {
  size_t used = 0;
  for (;;) {
    const ssize_t n = read(fd, out + used, size - 1 - used);
    if (n <= 0) break;
    used += static_cast<size_t>(n);
    if (used + 1 >= size) break;
  }
  out[used] = '\0';
}

void test_child() {
  char e1[] = "PATH=/bin";
  char e2[] = "LD_PRELOAD=/opt/sys/lib/libEGL_X.gpp.so";
  char e3[] = "FOO=bar";
  char e4[] = "MODE=old";
  char* src_env[5] = {e1, e2, e3, e4, NULL};
  char f1[] = "MODE=new";
  char* first[2] = {f1, NULL};
  char* out_env[5];
  CHECK(child_environment(src_env, NULL, out_env, 5) == 3);
  CHECK(strcmp(out_env[0], e1) == 0 && strcmp(out_env[1], e3) == 0 && out_env[3] == NULL);
  CHECK(child_environment(src_env, first, out_env, 5) == 3); /* MODE from `first` only */
  CHECK(out_env[0] == f1 && out_env[1] == e1 && out_env[2] == e3 && out_env[3] == NULL);
  CHECK(child_environment(src_env, first, out_env, 3) == -1); /* does not fit */
  CHECK(child_environment(src_env, NULL, out_env, 5, true) == 4);
  CHECK(out_env[1] == e2 && out_env[4] == NULL);             /* LD_PRELOAD kept */
  CHECK(child_environment(src_env, first, out_env, 5) == 3); /* the children below: no preload */

  /* The map: 0 = /dev/null, 1 and 2 = a pipe, 3 = another pipe; nothing else. */
  int out_pipe[2], extra_pipe[2];
  CHECK(pipe(out_pipe) == 0 && pipe(extra_pipe) == 0);
  const int null_fd = open("/dev/null", O_RDONLY);
  const int leak = open("/dev/null", O_RDONLY); /* not in the map */
  CHECK(null_fd >= 0 && leak >= 0);
  char script[160];
  snprintf(script, sizeof(script),
           "echo out$FOO; echo three >&3; if [ -e /proc/self/fd/%d ]; then echo leak; fi", leak);
  char sh[] = "/bin/sh", dash_c[] = "-c";
  char* argv[] = {sh, dash_c, script, NULL};
  const int fds[4] = {null_fd, out_pipe[1], out_pipe[1], extra_pipe[1]};
  pid_t pid = -1;
  CHECK(spawn_child("/bin/sh", argv, out_env, fds, 4, true, &pid) == 0 && pid > 0);
  close(out_pipe[1]);
  close(extra_pipe[1]);
  char text[64];
  read_all(out_pipe[0], text, sizeof(text));
  CHECK(strcmp(text, "outbar\n") == 0);
  read_all(extra_pipe[0], text, sizeof(text));
  CHECK(strcmp(text, "three\n") == 0);
  int status = -1;
  CHECK(stop_child(pid, true, 2000, 1000) >= 0);
  CHECK(child_exited(pid, &status)); /* already reaped: ECHILD counts as exited */
  close(out_pipe[0]);
  close(extra_pipe[0]);
  close(leak);

  /* A program that cannot be executed is an error, as spawn() reports it. */
  char missing[] = "/nonexistent/program";
  char* argv2[] = {missing, NULL};
  CHECK(spawn_child(missing, argv2, out_env, fds, 1, false, &pid) == ENOENT);
  CHECK(spawn_child(missing, argv2, out_env, fds, 0, false, &pid) == EINVAL);

  /* A child that ignores nothing ends with SIGTERM; one that ignores it, with SIGKILL. */
  char sleeper[] = "exec sleep 30";
  char trapper[] = "trap '' TERM; while :; do sleep 1; done";
  char* argv3[] = {sh, dash_c, sleeper, NULL};
  CHECK(spawn_child("/bin/sh", argv3, out_env, fds, 1, true, &pid) == 0);
  CHECK(!child_exited(pid, NULL));
  status = stop_child(pid, true, 50, 2000);
  CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);
  char* argv4[] = {sh, dash_c, trapper, NULL};
  CHECK(spawn_child("/bin/sh", argv4, out_env, fds, 1, true, &pid) == 0);
  usleep(200000); /* the trap is set */
  status = stop_child(pid, true, 50, 300);
  CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
  close(null_fd);

  /* This executable, and the payload it does not carry. */
  char exe[512];
  CHECK(own_executable(exe, sizeof(exe)) && exe[0] == '/');
  char dir[64], path[160];
  make_temp_dir(dir, sizeof(dir), "child");
  const char* why = NULL;
  CHECK(extract_own_payload(dir, "companion", path, sizeof(path), &why) == 0);
  CHECK(extract_own_payload(dir, "../escape", path, sizeof(path), &why) == -1);

  /* An appended payload: <executable><payload><"NBTPAYLD"><size>. */
  char carrier[160], copy[160];
  snprintf(carrier, sizeof(carrier), "%s/carrier", dir);
  snprintf(copy, sizeof(copy), "%s/copy", dir);
  const char body[] = "#!/bin/sh\necho hi\n";
  const uint64_t size = sizeof(body) - 1;
  FILE* f = fopen(carrier, "wb");
  CHECK(f != NULL);
  CHECK(fwrite("\177ELF-not-really", 1, 15, f) == 15 && fwrite(body, 1, size, f) == size);
  uint8_t trailer[NBT_PAYLOAD_TRAILER_SIZE];
  memcpy(trailer, NBT_PAYLOAD_TRAILER_MAGIC, 8);
  for (int i = 0; i < 8; ++i) trailer[8 + i] = static_cast<uint8_t>(size >> (8 * i));
  CHECK(fwrite(trailer, 1, sizeof(trailer), f) == sizeof(trailer) && fclose(f) == 0);
  CHECK(nbt_payload_extract(carrier, copy, &why) == 1);
  struct stat st;
  CHECK(stat(copy, &st) == 0 && st.st_size == static_cast<off_t>(size) &&
        (st.st_mode & 0777) == 0700);
  CHECK(unlink(copy) == 0);
  trailer[8] = 0xff; /* larger than the file */
  f = fopen(carrier, "r+b");
  CHECK(f != NULL && fseek(f, -8, SEEK_END) == 0 && fwrite(trailer + 8, 1, 8, f) == 8 &&
        fclose(f) == 0);
  CHECK(nbt_payload_extract(carrier, copy, &why) == -1);
  CHECK(unlink(copy) != 0); /* nothing left behind */
  CHECK(unlink(carrier) == 0 && rmdir(dir) == 0);
  g_tests_run++;
}

void test_host_keys() {
  CHECK(host_key_from_x11_keysym(0xff1b) == kHostKeyEscape);    /* XK_Escape */
  CHECK(host_key_from_x11_keysym(0xff0d) == kHostKeyReturn);    /* XK_Return */
  CHECK(host_key_from_x11_keysym(0xff52) == kHostKeyUp);        /* XK_Up */
  CHECK(host_key_from_x11_keysym(0xffe4) == kHostKeyRightCtrl); /* XK_Control_R */
  CHECK(host_key_from_x11_keysym('a') == 'a');
  CHECK(host_key_from_x11_keysym('A') == 'a');
  CHECK(host_key_from_x11_keysym(' ') == ' ');
  CHECK(host_key_from_x11_keysym(0xffbe) == kHostKeyNone); /* XK_F1: unmapped */
  CHECK(strlen(NBT_APP_SDK_VERSION) > 0);
  g_tests_run++;
}

}  // namespace

// Capability companion (bridge 1.9): the page on fd 11, written when it
// changes, and the phone's events read in order.
void test_companion() {
  using nbt::app::Companion;
  char dir[] = "/tmp/nbt-sdk-companion-XXXXXX", page_path[96], events_path[96], text[4097];
  CHECK(mkdtemp(dir) != NULL);
  snprintf(page_path, sizeof(page_path), "%s/page", dir);
  snprintf(events_path, sizeof(events_path), "%s/events", dir);
  unsetenv("NBT_COMPANION_FD");
  {
    Companion none;
    CHECK(!none.init() && !none.available() && !none.publish());
  }
  const int page = open(page_path, O_RDWR | O_CREAT | O_TRUNC, 0600);
  CHECK(page >= 0 && dup2(page, 11) == 11 && close(page) == 0);
  setenv("NBT_COMPANION_FD", "11", 1);
  setenv("NBT_COMPANION_EVENTS", events_path, 1);
  Companion c;
  CHECK(c.init() && c.available());
  const char* const modes[] = {"eco", "sport"};
  c.begin();
  CHECK(c.title("Boost") && c.text("1.2 bar") && c.button("go", "Go"));
  CHECK(c.toggle("log", "Log", true) && c.choice("mode", "Mode", "sport", modes, 2));
  CHECK(c.progress("Oil", 75));
  CHECK(!c.title("") && !c.text("a\tb") && !c.button("Go", "Go") && !c.progress("Oil", 101));
  CHECK(!c.choice("mode", "Mode", "race", modes, 2) && !c.choice("mode", "Mode", "eco", modes, 0));
  CHECK(c.publish());
  const char expected[] =
      "1\ntitle\tBoost\ntext\t1.2 bar\nbutton\tgo\tGo\ntoggle\tlog\tLog\t1\n"
      "choice\tmode\tMode\tsport\teco,sport\nprogress\tOil\t75\n";
  memset(text, 0, sizeof(text));
  CHECK(pread(11, text, sizeof(text) - 1, 0) == static_cast<ssize_t>(strlen(expected)) &&
        !strcmp(text, expected));
  uint64_t sequence = 0;
  CHECK(!nbt_companion_page_check(text, strlen(text), &sequence) && sequence == 1);
  c.begin();
  CHECK(c.title("Boost") && c.text("1.2 bar") && c.button("go", "Go"));
  CHECK(c.toggle("log", "Log", true) && c.choice("mode", "Mode", "sport", modes, 2));
  CHECK(c.progress("Oil", 75) && c.publish());
  CHECK(pread(11, text, 2, 0) == 2 && !memcmp(text, "1\n", 2)); /* unchanged, not rewritten */
  c.begin();
  CHECK(c.title("Done") && c.publish());
  memset(text, 0, sizeof(text));
  CHECK(pread(11, text, sizeof(text) - 1, 0) == 13 && !strcmp(text, "2\ntitle\tDone\n"));
  c.begin();
  for (unsigned i = 0; i < NBT_COMPANION_WIDGETS_MAX; ++i) CHECK(c.text("x"));
  CHECK(!c.text("x")); /* 33 */
  Companion::Event event;
  CHECK(!c.next(&event));
  const int events = open(events_path, O_RDWR | O_CREAT | O_TRUNC, 0600);
  const char lines[] = "1\tgo\t\n2\tmode\teco\nbad\n3\tlog\t0\n";
  CHECK(events >= 0 && write(events, lines, strlen(lines)) == static_cast<ssize_t>(strlen(lines)) &&
        close(events) == 0);
  CHECK(c.next(&event) && !strcmp(event.id, "go") && !event.value[0]);
  CHECK(c.next(&event) && !strcmp(event.id, "mode") && !strcmp(event.value, "eco"));
  CHECK(c.next(&event) && !strcmp(event.id, "log") && !strcmp(event.value, "0"));
  CHECK(!c.next(&event));
  close(11);
  unlink(page_path);
  unlink(events_path);
  rmdir(dir);
  unsetenv("NBT_COMPANION_FD");
  unsetenv("NBT_COMPANION_EVENTS");
  g_tests_run++;
}

/* Bridge 1.6: the hold request and answer, the network rules, car data. */
// Capability diag (bridge 1.8): requests out, answers matched by id, the
// helpers unwrap positive and negative answers.
void test_diag() {
  using nbt::app::Diag;
  int pair[2];
  CHECK(socketpair(AF_UNIX, SOCK_DGRAM, 0, pair) == 0);
  CHECK(dup2(pair[0], 10) == 10);
  setenv("NBT_DIAG_FD", "10", 1);
  Diag diag;
  CHECK(diag.init() && diag.available());
  // A stale answer (another id) is skipped; id 1 answers the first request.
  uint8_t wire[64];
  const uint8_t stale[] = {0x62, 0x00, 0x00};
  int n = nbt_diag_reply_encode(0x12, 99, NBT_DIAG_OK, NBT_DIAG_VIA_HSFZ, stale, 3, wire, 64);
  CHECK(send(pair[1], wire, static_cast<size_t>(n), 0) == n);
  const uint8_t value[] = {0x62, 0xf4, 0x0c, 0x1a, 0xf8};
  n = nbt_diag_reply_encode(0x12, 1, NBT_DIAG_OK, NBT_DIAG_VIA_HSFZ, value, 5, wire, 64);
  CHECK(send(pair[1], wire, static_cast<size_t>(n), 0) == n);
  uint8_t out[16], nrc = 0;
  size_t size = 0;
  CHECK(diag.read(0x12, 0xf40c, out, sizeof(out), &size, &nrc) == Diag::kOk);
  CHECK(size == 2 && out[0] == 0x1a && out[1] == 0xf8 && nrc == 0);
  ssize_t got = recv(pair[1], wire, sizeof(wire), MSG_DONTWAIT);
  nbt_diag_request request;
  CHECK(got == 9 && !nbt_diag_request_decode(wire, static_cast<size_t>(got), &request));
  CHECK(request.ecu == 0x12 && request.id == 1 && request.size == 3 && request.uds[0] == 0x22);
  // A negative answer: kOk with its code.
  const uint8_t negative[] = {0x7f, 0x14, 0x22};
  n = nbt_diag_reply_encode(0x12, 2, NBT_DIAG_OK, NBT_DIAG_VIA_HSFZ, negative, 3, wire, 64);
  CHECK(send(pair[1], wire, static_cast<size_t>(n), 0) == n);
  CHECK(diag.clear(0x12, &nrc) == Diag::kOk && nrc == 0x22);
  CHECK(recv(pair[1], wire, sizeof(wire), MSG_DONTWAIT) == 10);
  // A refusal comes back as such.
  n = nbt_diag_reply_encode(0x40, 3, NBT_DIAG_REFUSED, NBT_DIAG_VIA_NONE, NULL, 0, wire, 64);
  CHECK(send(pair[1], wire, static_cast<size_t>(n), 0) == n);
  CHECK(diag.codes(0x40, 0xff, out, sizeof(out), &size) == Diag::kRefused && size == 0);
  close(10);
  close(pair[0]);
  close(pair[1]);
  unsetenv("NBT_DIAG_FD");
  Diag none;
  CHECK(!none.init() && none.request(0x12, value, 3, out, 16, &size) == Diag::kNoChannel);
  ++g_tests_run;
}

void test_bridge_16() {
  char dir[64], hold_path[128], state_path[128], value_path[160], fd_text[16];
  make_temp_dir(dir, sizeof(dir), "bridge16");
  snprintf(hold_path, sizeof(hold_path), "%s/hold", dir);
  snprintf(state_path, sizeof(state_path), "%s/state", dir);
  const int hold = open(hold_path, O_RDWR | O_CREAT, 0600);
  CHECK(hold >= 3);
  snprintf(fd_text, sizeof(fd_text), "%d", hold);
  setenv("NBT_LAUNCH", "user", 1);
  setenv("NBT_BRIDGE_CAPS", "hold,car-data,net", 1);
  setenv("NBT_HOLD_FD", fd_text, 1);
  setenv("NBT_HOLD_STATE", state_path, 1);
  setenv("NBT_NET", "tcp:5277@uap0+wfd0", 1);
  setenv("NBT_CDS_DIR", dir, 1);
  char name[] = "app";
  char* argv[] = {name, NULL};
  {
    Lifecycle lc;
    CHECK(lc.init(1, argv) == 0);
    CHECK(!lc.is_service());
    CHECK(!strcmp(lc.net_rules(), "tcp:5277@uap0+wfd0"));
    CHECK(lc.request_hold(Lifecycle::kHold));
    char text[32] = "";
    CHECK(pread(hold, text, sizeof(text) - 1, 0) == 7 && !strcmp(text, "1 hold\n"));
    CHECK(lc.request_hold(Lifecycle::kPeek));
    memset(text, 0, sizeof(text));
    CHECK(pread(hold, text, sizeof(text) - 1, 0) == 7 && !strcmp(text, "2 peek\n"));
    uint64_t seq = 0;
    CHECK(lc.hold_state(&seq) == 0);
    const int state = open(state_path, O_RDWR | O_CREAT | O_TRUNC, 0600);
    CHECK(state >= 0 && write(state, "5 aside\n", 8) == 8 && close(state) == 0);
    CHECK(lc.hold_state(&seq) == 2 && seq == 5);
    lc.close_fds(); /* closes the hold descriptor */
  }
  CarData cars;
  CHECK(cars.init());
  char value[201];
  uint64_t seq = 0;
  CHECK(!cars.read("driving.gear", value, sizeof(value), &seq));
  snprintf(value_path, sizeof(value_path), "%s/nbt-bridge1-cds-driving.gear", dir);
  const int fd = open(value_path, O_RDWR | O_CREAT | O_TRUNC, 0600);
  CHECK(fd >= 0 && write(fd, "3 {\"gear\":2}\n", 13) == 13 && close(fd) == 0);
  CHECK(cars.read("driving.gear", value, sizeof(value), &seq) && seq == 3 &&
        !strcmp(value, "{\"gear\":2}"));
  CHECK(!cars.read("../driving.gear", value, sizeof(value), &seq));
  CHECK(!cars.read("driving.gear", value, 4, &seq)); /* too small */
  /* An application's service (bridge 1.9): `service` without `show`. */
  setenv("NBT_BRIDGE_CAPS", "show,service", 1);
  {
    Lifecycle lc;
    CHECK(lc.init(1, argv) == 0 && !lc.is_service());
  }
  setenv("NBT_BRIDGE_CAPS", "car-data,service", 1);
  {
    Lifecycle lc;
    CHECK(lc.init(1, argv) == 0 && lc.is_service());
  }
  /* Without the grants: nothing. */
  setenv("NBT_BRIDGE_CAPS", "", 1);
  {
    Lifecycle lc;
    CHECK(lc.init(1, argv) == 0 && !lc.request_hold(Lifecycle::kHold) && !lc.net_rules()[0]);
  }
  unsetenv("NBT_HOLD_FD");
  unsetenv("NBT_HOLD_STATE");
  unsetenv("NBT_NET");
  unsetenv("NBT_CDS_DIR");
  unsetenv("NBT_BRIDGE_CAPS");
  unsetenv("NBT_LAUNCH");
}

/* NBT Manager's audio mixer: an AudioPcm (NBT_AUDIO_MIXER) and a raw client
 * play at once; the mix is their sum. */
static void constant_render(void* context, int16_t* out, int frames) {
  const int16_t value = *static_cast<const int16_t*>(context);
  for (int i = 0; i < 2 * frames; ++i) out[i] = value;
}

static void test_audio_mix() {
  char capture[] = "/tmp/nbt-mix-XXXXXX";
  const int capture_fd = mkstemp(capture);
  CHECK(capture_fd >= 0);
  close(capture_fd);
  char address[32];
  snprintf(address, sizeof(address), "127.0.0.1:%d", 40000 + getpid() % 5000);
  const pid_t mixer = fork();
  CHECK(mixer >= 0);
  if (mixer == 0) {
    setenv("NBT_MIX_CAPTURE", capture, 1);
    _exit(nbt::app::mix_serve(address, 800));
  }
  int rate = 0, fd = -1;
  for (int i = 0; i < 100 && fd < 0; ++i) {  // until it listens
    fd = nbt::app::mix_connect(address, &rate);
    if (fd < 0) usleep(20000);
  }
  CHECK(fd >= 0 && rate == nbt::app::kMixRate);
  CHECK(nbt::app::mix_connect("127.0.0.1", &rate) < 0); /* no port */
  setenv("NBT_AUDIO_MIXER", address, 1);
  int16_t level = 500;
  {
    nbt::app::AudioPcm pcm;
    CHECK(pcm.open(22050, constant_render, &level, true) && pcm.sample_rate() == rate);
    int16_t frames[2 * 441];
    for (int i = 0; i < 2 * 441; ++i) frames[i] = 1500;
    const uint8_t route = 1;
    CHECK(nbt::app::mix_send(fd, 'R', &route, 1));
    for (int i = 0; i < 20; ++i) CHECK(nbt::app::mix_send(fd, 'P', frames, sizeof(frames)));
    usleep(300000);
    pcm.close();
  }
  unsetenv("NBT_AUDIO_MIXER");
  close(fd);
  int status = -1;
  CHECK(waitpid(mixer, &status, 0) == mixer && WIFEXITED(status) && WEXITSTATUS(status) == 0);
  FILE* in = fopen(capture, "rb");
  CHECK(in != NULL);
  int16_t sample[2];
  bool both = false, too_loud = false;
  while (fread(sample, 4, 1, in) == 1) {
    both = both || (sample[0] == 2000 && sample[1] == 2000);
    too_loud = too_loud || sample[0] > 2000 || sample[1] > 2000;
  }
  fclose(in);
  unlink(capture);
  CHECK(both && !too_loud);
  g_tests_run++;
}

int main() {
  test_audio_mix();
  test_gles2_matrix_and_batch();
  test_readers();
  test_input_mixer();
  test_back_gesture();
  test_parse_and_bridge();
  test_settings();
  test_lifecycle();
  test_bridge_16();
  test_diag();
  test_companion();
  test_audio_mixer();
  test_audio_a2i();
  test_log();
  test_audio_route();
  test_child();
  test_host_keys();
  printf("ALL %d SDK UNIT TEST SUITES PASSED\n", g_tests_run);
  return 0;
}
