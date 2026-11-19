// SPDX-License-Identifier: MPL-2.0
// Lifecycle for app modules: the runner owns the NBT Manager bridge (ready and
// lease descriptors, signals, status, the settings file); this forwards to it
// through the host ABI. Same interface as src/lifecycle.cpp.
#include "nbt/app/lifecycle.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nbt/app/bridge.hpp"
#include "nbt_module.h"

namespace nbt {
namespace app {

namespace {

bool g_show_pending = false;

uint32_t poll_events() {
  if (!NBT_HAS(app_poll)) return 0;
  const uint32_t ev = nbt_host->app_poll();
  if (ev & NBT_APP_EV_SHOW) g_show_pending = true;
  return ev;
}

}  // namespace

int Lifecycle::init(int argc, char** argv) {
  (void)argc;
  (void)argv; /* the runner took the bridge arguments */
  g_show_pending = false;
  status_fd_ = controller_fd_ = gamepad_fd_ = -1;
  if (!NBT_HAS(app_info)) return 1;
  nbt_app_info info;
  memset(&info, 0, sizeof(info));
  if (nbt_host->app_info(&info) != 0) return 1;
  info.caps[sizeof(info.caps) - 1] = '\0';
  snprintf(caps_, sizeof(caps_), "%s", info.caps);
  check_mode_ = (info.flags & NBT_APP_CHECK) != 0;
  user_mode_ = (info.flags & NBT_APP_USER) != 0;
  can_show_ = (info.flags & NBT_APP_CAN_SHOW) != 0;
  has_live_settings_ = has_cap("live-settings");
  surface_id_ = info.surface_id;
  if (info.flags & NBT_APP_STATUS) status_fd_ = 0; /* a token: status goes through the host */
  if (info.flags & NBT_APP_CONTROLLER) controller_fd_ = NBT_FD_BRIDGE + NBT_BRIDGE_CONTROLLER;
  if (info.flags & NBT_APP_GAMEPAD) gamepad_fd_ = NBT_FD_BRIDGE + NBT_BRIDGE_GAMEPAD;
  reload_settings(NULL);
  settings_generation_ = 0; /* the starting values are not a change */
  return 0;
}

void Lifecycle::close_fds() {
  status_fd_ = controller_fd_ = gamepad_fd_ = -1;
}

bool Lifecycle::send_ready() {
  return !NBT_HAS(app_ready) || nbt_host->app_ready() == 0;
}

void Lifecycle::send_status(uint64_t now_ms, const char* text) {
  (void)now_ms;
  if (status_fd_ >= 0 && text && NBT_HAS(app_status)) nbt_host->app_status(text);
}

void Lifecycle::reload_settings(bool* changed) {
  if (changed) *changed = false;
  if (!NBT_HAS(app_settings)) return;
  char text[kSettingsFileMax + 64];
  uint32_t generation = 0;
  const int32_t n = nbt_host->app_settings(text, sizeof(text), &generation);
  if (n < 0) return;
  Settings next;
  if (n > 0 && !next.parse_values(text, static_cast<size_t>(n))) return;
  if (!next.equals(settings_)) {
    settings_ = next;
    ++settings_generation_;
    if (changed) *changed = true;
  }
}

void Lifecycle::tick(uint64_t now_ms, const char* status_text, bool* settings_changed) {
  if (settings_changed) *settings_changed = false;
  if (last_heartbeat_ms_ != 0 && now_ms - last_heartbeat_ms_ < 1000u) return;
  last_heartbeat_ms_ = now_ms;
  if (!NBT_HAS(app_tick)) return;
  const uint32_t ev = nbt_host->app_tick(status_fd_ >= 0 ? status_text : NULL);
  if (ev & NBT_APP_EV_SHOW) g_show_pending = true;
  if (ev & NBT_APP_EV_SETTINGS) reload_settings(settings_changed);
}

bool Lifecycle::stopping() const {
  return (poll_events() & NBT_APP_EV_STOP) != 0;
}

bool Lifecycle::consume_show_requested() {
  poll_events();
  if (!g_show_pending) return false;
  g_show_pending = false;
  return true;
}

void Lifecycle::request_stop() {
  if (NBT_HAS(app_request_stop)) nbt_host->app_request_stop();
}

}  // namespace app
}  // namespace nbt
