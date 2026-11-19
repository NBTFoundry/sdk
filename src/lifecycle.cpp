#include "nbt/app/lifecycle.hpp"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nbt/app/bridge.hpp"
#include "nbt/app/log.hpp"
#include "nbt/app/parse.hpp"

namespace nbt {
namespace app {

namespace {

volatile sig_atomic_t g_stopping = 0;
volatile sig_atomic_t g_show_requested = 0;

void handle_signal(int signum) {
  if (signum == SIGUSR1) {
    g_show_requested = 1;
  } else {
    g_stopping = 1;
  }
}

void close_fd(int* fd) {
  if (*fd >= 0) {
    close(*fd);
    *fd = -1;
  }
}

}  // namespace

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

int Lifecycle::init(int argc, char** argv) {
  g_stopping = 0;
  g_show_requested = 0;
  ready_fd_ = -1;
  lease_fd_ = -1;
  status_fd_ = -1;
  controller_fd_ = -1;
  gamepad_fd_ = -1;
  surface_id_ = 0;
  caps_[0] = '\0';
  settings_file_[0] = '\0';
  last_settings_error_ = NULL;
  env_settings_.clear();
  settings_.clear();

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--ready-fd") == 0 && i + 1 < argc) {
      if (!parse_int(argv[++i], 3, &ready_fd_)) return 2;
    } else if (strcmp(argv[i], "--lease-fd") == 0 && i + 1 < argc) {
      if (!parse_int(argv[++i], 3, &lease_fd_)) return 2;
    }
  }
  if ((ready_fd_ >= 0) != (lease_fd_ >= 0) || (ready_fd_ >= 0 && ready_fd_ == lease_fd_)) {
    return 2;
  }

  if (lease_fd_ >= 0) {
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_RDLCK;
    fl.l_whence = SEEK_SET;
    if (fcntl(lease_fd_, F_SETLK, &fl) < 0) return 1;
  }

  const char* launch = getenv("NBT_LAUNCH");
  check_mode_ = (launch && strcmp(launch, "check") == 0);
  user_mode_ = (!check_mode_ && (ready_fd_ < 0 || (launch && strcmp(launch, "user") == 0)));

  const char* surf = getenv("NBT_SURFACE");
  if (user_mode_ && surf && surf[0]) {
    if (!parse_int(surf, 1, &surface_id_)) return 1;
  }

  const char* caps = getenv("NBT_BRIDGE_CAPS");
  if (caps && strlen(caps) < sizeof(caps_)) snprintf(caps_, sizeof(caps_), "%s", caps);
  can_show_ = user_mode_ && has_cap("show");
  has_live_settings_ = has_cap("live-settings");

  if (user_mode_ && has_cap("status")) {
    const char* sfd = getenv("NBT_STATUS_FD");
    int fd = -1;
    if (sfd && parse_int(sfd, 3, &fd) && fd != ready_fd_ && fd != lease_fd_) status_fd_ = fd;
  }
  if (user_mode_ && has_cap("controller")) {
    const int excluded[] = {ready_fd_, lease_fd_, status_fd_};
    controller_fd_ = datagram_input_fd(getenv("NBT_CONTROLLER_FD"), excluded, 3);
  }
  if (user_mode_ && has_cap("gamepad")) {
    const int excluded[] = {ready_fd_, lease_fd_, status_fd_, controller_fd_};
    gamepad_fd_ = datagram_input_fd(getenv("NBT_GAMEPAD_FD"), excluded, 4);
  }

  settings_load_env(&env_settings_);
  settings_ = env_settings_;
  if (has_live_settings_) {
    const char* sf = getenv("NBT_SETTINGS_FILE");
    if (sf && sf[0] && strlen(sf) < sizeof(settings_file_)) {
      snprintf(settings_file_, sizeof(settings_file_), "%s", sf);
      reload_settings(NULL);
    }
  }
  settings_generation_ = 0; /* the starting values are not a change */

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = handle_signal;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGUSR1, &sa, NULL);
  sa.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &sa, NULL);
  return 0;
}

bool Lifecycle::has_cap(const char* cap) const {
  return bridge_cap_has(caps_, cap);
}

const char* Lifecycle::data_dir() const {
  const char* dir = getenv("NBT_DATA_DIR");
  return dir ? dir : "";
}

void Lifecycle::close_fds() {
  close_fd(&gamepad_fd_);
  close_fd(&controller_fd_);
  close_fd(&status_fd_);
  close_fd(&ready_fd_);
  close_fd(&lease_fd_);
}

bool Lifecycle::send_ready() {
  if (ready_fd_ < 0) return true;
  const ssize_t n = write(ready_fd_, "R", 1);
  if (n == 1) return true;
  if (n < 0 && (errno == EAGAIN || errno == EINTR)) return true;
  return false;
}

void Lifecycle::send_status(uint64_t now_ms, const char* text) {
  if (status_fd_ < 0) return;
  char clean[kBridgeStatusMax + 1];
  if (bridge_status_sanitize(text, clean) == 0) return;
  char line[64];
  const int n = bridge_status_format(now_ms, clean, line, sizeof(line));
  if (n <= 0) return;
  if (lseek(status_fd_, 0, SEEK_SET) == 0 && write(status_fd_, line, static_cast<size_t>(n)) == n) {
    if (ftruncate(status_fd_, static_cast<off_t>(n)) < 0) {
      /* A longer previous line stays visible past this one; nothing to do. */
    }
  }
}

void Lifecycle::reload_settings(bool* changed) {
  if (changed) *changed = false;
  if (!settings_file_[0]) return;
  Settings next = env_settings_;
  const char* why = NULL;
  if (!settings_read_file(settings_file_, &next, &why)) {
    if (why != last_settings_error_) {
      log_info("live settings not applied: %s", why ? why : "unknown error");
    }
    last_settings_error_ = why;
    return;
  }
  last_settings_error_ = NULL;
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

  if (!send_ready()) {
    g_stopping = 1;
    return;
  }
  reload_settings(settings_changed);
  if (status_text) send_status(now_ms, status_text);
}

bool Lifecycle::stopping() const {
  return g_stopping != 0;
}

bool Lifecycle::consume_show_requested() {
  if (!g_show_requested) return false;
  g_show_requested = 0;
  return true;
}

void Lifecycle::request_stop() {
  g_stopping = 1;
}

}  // namespace app
}  // namespace nbt
