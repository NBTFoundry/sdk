#ifndef NBT_APP_LIFECYCLE_HPP
#define NBT_APP_LIFECYCLE_HPP

#include <stdint.h>

#include "nbt/app/settings.hpp"

namespace nbt {
namespace app {

/* An application's side of the NBT Manager bridge: launch arguments and
 * environment, readiness, heartbeats, status, live settings and signals.
 * One instance per process (the signal flags are process-wide). */
class Lifecycle {
 public:
  Lifecycle();
  ~Lifecycle();

  /* Reads --ready-fd/--lease-fd (both or neither) and takes a shared lock on
   * the lease descriptor; reads NBT_LAUNCH, NBT_SURFACE, NBT_BRIDGE_CAPS and
   * the descriptors of the granted capabilities; loads settings; installs the
   * SIGTERM/SIGINT (stop), SIGUSR1 (show) and SIGPIPE (ignored) handlers.
   * Returns 0 when ready to run, 1 when the lease or surface is unusable, and
   * 2 on a usage error. */
  int init(int argc, char** argv);

  /* Whether argv[i] is a bridge argument (it takes a value), for removing
   * them before handing argv to other code. */
  static bool is_bridge_arg(const char* arg);

  /* Closes the ready, lease, status, controller and gamepad descriptors. */
  void close_fds();

  /* NBT_LAUNCH=check: a health check (no display, no input). */
  bool is_check_mode() const { return check_mode_; }
  /* A user launch (NBT_LAUNCH=user, or no bridge descriptors at all). */
  bool is_user_mode() const { return user_mode_; }
  /* User mode with capability `show`: SIGUSR1 asks to come to the front. */
  bool can_show() const { return can_show_; }
  bool has_cap(const char* cap) const;
  const char* bridge_caps() const { return caps_; }
  /* NBT_DATA_DIR, the application's private folder, or "". */
  const char* data_dir() const;

  int surface_id() const { return surface_id_; }
  int status_fd() const { return status_fd_; }
  int controller_fd() const { return controller_fd_; }
  int gamepad_fd() const { return gamepad_fd_; }

  /* NBT_SETTING_* overlaid with NBT_SETTINGS_FILE's values (live-settings). */
  const Settings& settings() const { return settings_; }
  /* Increases each time tick() finds the settings changed. */
  uint32_t settings_generation() const { return settings_generation_; }

  /* Writes the readiness byte; true without a ready descriptor. */
  bool send_ready();
  /* Writes "<now_ms> <text>\n" to the status descriptor (text made printable
   * and cut to 24 characters). */
  void send_status(uint64_t now_ms, const char* text);
  /* At most once a second: heartbeat (stops on a failed write), settings
   * reload, status. `settings_changed` is set when the settings changed. */
  void tick(uint64_t now_ms, const char* status_text, bool* settings_changed);

  bool stopping() const;
  bool consume_show_requested();
  void request_stop();

 private:
  Lifecycle(const Lifecycle&);
  Lifecycle& operator=(const Lifecycle&);

  void reload_settings(bool* changed);

  int ready_fd_;
  int lease_fd_;
  int status_fd_;
  int controller_fd_;
  int gamepad_fd_;
  int surface_id_;
  bool check_mode_;
  bool user_mode_;
  bool can_show_;
  bool has_live_settings_;
  char caps_[256];
  char settings_file_[512];
  const char* last_settings_error_;
  uint64_t last_heartbeat_ms_;
  uint32_t settings_generation_;
  Settings env_settings_;
  Settings settings_;
};

}  // namespace app
}  // namespace nbt

#endif /* NBT_APP_LIFECYCLE_HPP */
