// SPDX-License-Identifier: MPL-2.0
#include "nbt/app/audio_route.hpp"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#ifdef __QNXNTO__
#include <spawn.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "nbt/app/clock.hpp"
#include "nbt/app/log.hpp"

extern "C" char** environ;

namespace nbt {
namespace app {

namespace {

const uint32_t kDefaultConnId = 0xb8u;
const int kChildTimeoutMs = 5000;
const int kMaxRetries = 5;
const unsigned kRetryDelaysMs[kMaxRetries] = {2000u, 3000u, 5000u, 5000u, 5000u};
const size_t kChildEnvMax = 128;

void trim_output(char* s) {
  if (!s) return;
  for (size_t i = 0; s[i]; ++i) {
    if (s[i] == '\r' || s[i] == '\n') s[i] = ' ';
  }
  size_t len = strlen(s);
  while (len > 0 && s[len - 1] == ' ') s[--len] = '\0';
}

}  // namespace

uint32_t AudioRoute::parse_conn_env(const char* val, uint32_t fallback) {
  if (!val || !val[0]) return fallback;
  char* end = NULL;
  errno = 0;
  unsigned long parsed = strtoul(val, &end, 0);
  if (errno != 0 || end == val || *end != '\0' || parsed > 0xfffffffful) {
    return fallback;
  }
  return static_cast<uint32_t>(parsed);
}

uint32_t AudioRoute::conn_file(const char* data_dir, uint32_t fallback) {
  char path[512];
  struct stat st;
  if (!data_dir || !data_dir[0] ||
      snprintf(path, sizeof(path), "%s/audio-conn", data_dir) >= static_cast<int>(sizeof(path)) ||
      lstat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > 32) {
    return fallback;
  }
  FILE* fp = fopen(path, "r");
  if (!fp) return fallback;
  char line[40] = "";
  const bool read = fgets(line, sizeof(line), fp) != NULL;
  fclose(fp);
  if (!read) return fallback;
  line[strcspn(line, "\r\n")] = '\0';
  return parse_conn_env(line, fallback);
}

int AudioRoute::build_child_env(char** const src_env, char* out[], size_t max_out) {
  if (!out || max_out == 0) return 0;
  size_t count = 0;
  if (src_env) {
    for (char** p = src_env; *p && count + 1 < max_out; ++p) {
      if (strncmp(*p, "LD_PRELOAD=", 11) == 0) continue;
      out[count++] = *p;
    }
  }
  out[count] = NULL;
  return static_cast<int>(count);
}

AudioRoute::AudioRoute()
    : thread_(),
      conn_id_(kDefaultConnId),
      configured_(false),
      thread_running_(false),
      stop_requested_(false),
      wanted_(false),
      built_(false),
      fail_streak_(0) {
  path_[0] = '\0';
  pthread_mutex_init(&mu_, NULL);
  pthread_cond_init(&cond_, NULL);
}

AudioRoute::~AudioRoute() {
  stop();
  pthread_cond_destroy(&cond_);
  pthread_mutex_destroy(&mu_);
}

bool AudioRoute::configured() const {
  pthread_mutex_lock(&mu_);
  bool c = configured_;
  pthread_mutex_unlock(&mu_);
  return c;
}

bool AudioRoute::built() const {
  pthread_mutex_lock(&mu_);
  bool b = built_;
  pthread_mutex_unlock(&mu_);
  return b;
}

bool AudioRoute::configure() {
  pthread_mutex_lock(&mu_);
  path_[0] = '\0';
  configured_ = false;
  const uint32_t from_file = conn_file(getenv("NBT_DATA_DIR"), kDefaultConnId);
  conn_id_ = parse_conn_env(getenv("NBT_AUDIO_CONN"), from_file);
  if (conn_id_ == 0) {
    pthread_mutex_unlock(&mu_);
    log_debug("audio route disabled (NBT_AUDIO_CONN=0)");
    return false;
  }

  const char* override_path = getenv("NBT_AUDIOCONN");
  if (override_path && override_path[0]) {
    if (access(override_path, X_OK) == 0 && strlen(override_path) < sizeof(path_)) {
      snprintf(path_, sizeof(path_), "%s", override_path);
    }
  } else {
    static const char* const kCandidates[] = {
        "/fs/sda0/opt/mm/bin/audioconn",
        "/opt/mm/bin/audioconn",
    };
    for (size_t i = 0; i < sizeof(kCandidates) / sizeof(kCandidates[0]); ++i) {
      if (access(kCandidates[i], X_OK) == 0) {
        snprintf(path_, sizeof(path_), "%s", kCandidates[i]);
        break;
      }
    }
  }

  configured_ = (path_[0] != '\0');
  const uint32_t conn = conn_id_;
  pthread_mutex_unlock(&mu_);
  if (from_file != kDefaultConnId) {
    log_info("audio connection 0x%x from audio-conn in the data folder (default 0x%x)",
             static_cast<unsigned>(conn), static_cast<unsigned>(kDefaultConnId));
  }
  if (configured_) {
    log_debug("audio route using %s conn=0x%x", path_, static_cast<unsigned>(conn_id_));
  } else {
#ifdef __QNXNTO__
    log_info("audioconn not found; connection 0x%x will not be built",
             static_cast<unsigned>(conn_id_));
#else
    log_debug("audioconn not found; audio routing disabled");
#endif
  }
  return configured_;
}

void AudioRoute::start() {
  pthread_mutex_lock(&mu_);
  if (thread_running_ || !configured_) {
    pthread_mutex_unlock(&mu_);
    return;
  }
  stop_requested_ = false;
  fail_streak_ = 0;
  if (pthread_create(&thread_, NULL, &AudioRoute::worker_entry, this) == 0) {
    thread_running_ = true;
  } else {
    log_info("audio route worker failed to start");
  }
  pthread_mutex_unlock(&mu_);
}

void AudioRoute::set_wanted(bool wanted) {
  pthread_mutex_lock(&mu_);
  wanted_ = wanted;
  if (wanted) fail_streak_ = 0;
  pthread_cond_signal(&cond_);
  pthread_mutex_unlock(&mu_);
}

void AudioRoute::stop() {
  pthread_mutex_lock(&mu_);
  wanted_ = false;
  stop_requested_ = true;
  pthread_cond_signal(&cond_);
  bool join = thread_running_;
  pthread_mutex_unlock(&mu_);

  if (join) {
    pthread_join(thread_, NULL);
    pthread_mutex_lock(&mu_);
    thread_running_ = false;
    pthread_mutex_unlock(&mu_);
  }
}

void* AudioRoute::worker_entry(void* arg) {
  static_cast<AudioRoute*>(arg)->worker_loop();
  return NULL;
}

bool AudioRoute::sleep_ms_interruptible_locked(unsigned ms) {
  const bool want_snapshot = wanted_;
  struct timespec deadline;
  if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) return !stop_requested_;
  deadline.tv_sec += static_cast<time_t>(ms / 1000u);
  deadline.tv_nsec += static_cast<long>((ms % 1000u) * 1000000ul);
  if (deadline.tv_nsec >= 1000000000L) {
    deadline.tv_sec += 1;
    deadline.tv_nsec -= 1000000000L;
  }
  while (!stop_requested_ && wanted_ == want_snapshot) {
    int rc = pthread_cond_timedwait(&cond_, &mu_, &deadline);
    if (rc == ETIMEDOUT) break;
  }
  return !stop_requested_ && wanted_ == want_snapshot;
}

int AudioRoute::run_audioconn(const char* path, char op, uint32_t conn_id, char* out_buf,
                              size_t out_size) {
  if (out_buf && out_size > 0) out_buf[0] = '\0';
  int pipefd[2] = {-1, -1};
  if (pipe(pipefd) != 0) return -1;
  (void)fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
  (void)fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);

  char flag_arg[4] = {'-', op, '\0', '\0'};
  char conn_arg[16];
  snprintf(conn_arg, sizeof(conn_arg), "0x%x", static_cast<unsigned>(conn_id));
  char* argv[4] = {const_cast<char*>(path), flag_arg, conn_arg, NULL};

  char* envp[kChildEnvMax];
  build_child_env(environ, envp, kChildEnvMax);

#ifdef __QNXNTO__
  /* QNX 6.5 fork() returns -1 (ENOSYS) in a multithreaded process (libasound/SDL threads). */
  int nullfd = open("/dev/null", O_RDONLY);
  int fdmap[3] = {nullfd >= 0 ? nullfd : SPAWN_FDCLOSED, pipefd[1], pipefd[1]};
  struct inheritance inherit;
  memset(&inherit, 0, sizeof(inherit));
  inherit.flags = SPAWN_SETSIGDEF | SPAWN_SETSIGMASK;
  sigfillset(&inherit.sigdefault);
  sigemptyset(&inherit.sigmask);
  pid_t pid = ::spawn(path, 3, fdmap, &inherit, argv, envp);
  int spawn_err = errno;
  if (nullfd >= 0) close(nullfd);
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    if (out_buf && out_size > 1) {
      snprintf(out_buf, out_size, "spawn errno %d (%s)", spawn_err, strerror(spawn_err));
    }
    return -1;
  }
#else
  pid_t pid = fork();
  if (pid < 0) {
    int fork_err = errno;
    close(pipefd[0]);
    close(pipefd[1]);
    if (out_buf && out_size > 1) {
      snprintf(out_buf, out_size, "fork errno %d (%s)", fork_err, strerror(fork_err));
    }
    return -1;
  }
  if (pid == 0) {
    int nullfd = open("/dev/null", O_RDONLY);
    if (nullfd >= 0) {
      if (nullfd != STDIN_FILENO) (void)dup2(nullfd, STDIN_FILENO);
      if (nullfd > STDERR_FILENO) (void)close(nullfd);
    }
    if (pipefd[1] != STDOUT_FILENO) (void)dup2(pipefd[1], STDOUT_FILENO);
    if (pipefd[1] != STDERR_FILENO) (void)dup2(pipefd[1], STDERR_FILENO);
    if (pipefd[0] > STDERR_FILENO) (void)close(pipefd[0]);
    if (pipefd[1] > STDERR_FILENO) (void)close(pipefd[1]);
    execve(path, argv, envp);
    _exit(127);
  }
#endif

  close(pipefd[1]);
  size_t used = 0;
  bool timed_out = false;
  const uint64_t deadline = monotonic_ms() + static_cast<uint64_t>(kChildTimeoutMs);
  for (;;) {
    uint64_t now = monotonic_ms();
    int left_ms = (now < deadline) ? static_cast<int>(deadline - now) : 0;
    if (left_ms <= 0) {
      timed_out = true;
      break;
    }
    struct pollfd pfd;
    pfd.fd = pipefd[0];
    pfd.events = POLLIN;
    pfd.revents = 0;
    int ready = poll(&pfd, 1, left_ms);
    if (ready < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (ready == 0) {
      timed_out = true;
      break;
    }
    char chunk[128];
    ssize_t n = read(pipefd[0], chunk, sizeof(chunk));
    if (n < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (n == 0) break;
    if (out_buf && out_size > 1 && used + 1 < out_size) {
      size_t keep = static_cast<size_t>(n);
      size_t avail = out_size - 1 - used;
      if (keep > avail) keep = avail;
      memcpy(out_buf + used, chunk, keep);
      used += keep;
      out_buf[used] = '\0';
    }
  }
  close(pipefd[0]);
  if (timed_out) {
    (void)kill(pid, SIGKILL);
  }

  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) return -1;
  }
  if (timed_out) return -1;
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void AudioRoute::worker_loop() {
  pthread_mutex_lock(&mu_);
  for (;;) {
    while (!stop_requested_ &&
           ((wanted_ && (built_ || fail_streak_ > kMaxRetries)) || (!wanted_ && !built_))) {
      pthread_cond_wait(&cond_, &mu_);
    }

    if (stop_requested_) {
      if (built_) {
        char path_copy[sizeof(path_)];
        snprintf(path_copy, sizeof(path_copy), "%s", path_);
        uint32_t conn_copy = conn_id_;
        pthread_mutex_unlock(&mu_);

        char out[256];
        int rc = run_audioconn(path_copy, 'r', conn_copy, out, sizeof(out));
        trim_output(out);
        log_debug("audio route -r 0x%x rc=%d %s", static_cast<unsigned>(conn_copy), rc, out);

        pthread_mutex_lock(&mu_);
        built_ = false;
      }
      break;
    }

    char path_copy[sizeof(path_)];
    snprintf(path_copy, sizeof(path_copy), "%s", path_);
    uint32_t conn_copy = conn_id_;

    if (wanted_ && !built_) {
      pthread_mutex_unlock(&mu_);
      char out[256];
      int rc = run_audioconn(path_copy, 'b', conn_copy, out, sizeof(out));
      trim_output(out);
      pthread_mutex_lock(&mu_);
      if (rc == 0) {
        built_ = true;
        fail_streak_ = 0;
        log_info("audio route 0x%x granted%s%s", static_cast<unsigned>(conn_copy),
                 out[0] ? ": " : "", out);
        if (stop_requested_ || !wanted_) {
          /* Mute or stop raced with the build: loop immediately to release it. */
          continue;
        }
      } else {
        ++fail_streak_;
        if (fail_streak_ == 1 || fail_streak_ > kMaxRetries) {
          log_info("audio route 0x%x build failed rc=%d (%s)%s", static_cast<unsigned>(conn_copy),
                   rc, out[0] ? out : "no output", fail_streak_ > kMaxRetries ? "; giving up" : "");
        } else {
          log_debug("audio route 0x%x retry %d/%d rc=%d (%s)", static_cast<unsigned>(conn_copy),
                    fail_streak_ - 1, kMaxRetries, rc, out);
        }
        if (fail_streak_ <= kMaxRetries && !stop_requested_ && wanted_) {
          unsigned delay = kRetryDelaysMs[fail_streak_ - 1];
          (void)sleep_ms_interruptible_locked(delay);
        }
      }
    } else if (!wanted_ && built_) {
      pthread_mutex_unlock(&mu_);
      char out[256];
      int rc = run_audioconn(path_copy, 'r', conn_copy, out, sizeof(out));
      trim_output(out);
      pthread_mutex_lock(&mu_);
      built_ = false;
      fail_streak_ = 0;
      log_debug("audio route 0x%x released rc=%d %s", static_cast<unsigned>(conn_copy), rc, out);
    }
  }
  pthread_mutex_unlock(&mu_);
}

}  // namespace app
}  // namespace nbt
