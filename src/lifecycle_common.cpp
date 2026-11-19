// SPDX-License-Identifier: MPL-2.0
// Lifecycle's parts shared by every platform: an application process
// (lifecycle.cpp) and an application under a runner (module/sdk/).
#include <stdlib.h>
#include <string.h>

#include "nbt/app/bridge.hpp"
#include "nbt/app/lifecycle.hpp"

namespace nbt {
namespace app {

Lifecycle::Lifecycle()
    : ready_fd_(-1),
      lease_fd_(-1),
      status_fd_(-1),
      controller_fd_(-1),
      gamepad_fd_(-1),
      surface_id_(0),
      check_mode_(false),
      user_mode_(true),
      can_show_(false),
      has_live_settings_(false),
      last_settings_error_(NULL),
      last_heartbeat_ms_(0),
      settings_generation_(0) {
  caps_[0] = '\0';
  settings_file_[0] = '\0';
}

Lifecycle::~Lifecycle() {
  close_fds();
}

bool Lifecycle::is_bridge_arg(const char* arg) {
  return arg && (strcmp(arg, "--ready-fd") == 0 || strcmp(arg, "--lease-fd") == 0);
}

bool Lifecycle::has_cap(const char* cap) const {
  return bridge_cap_has(caps_, cap);
}

const char* Lifecycle::data_dir() const {
  const char* dir = getenv("NBT_DATA_DIR");
  return dir ? dir : "";
}

const char* Lifecycle::files_dir() const {
  const char* dir = getenv("NBT_FILES_DIR");
  return dir ? dir : "";
}

}  // namespace app
}  // namespace nbt
