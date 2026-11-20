// SPDX-License-Identifier: MPL-2.0
#include "nbt/app/child.hpp"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#ifdef __QNXNTO__
#include <spawn.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "nbt/app/payload.h"

namespace nbt {
namespace app {

namespace {

bool valid_request(const char* path, char* const argv[], char* const envp[], const int* fds,
                   unsigned count, pid_t* pid) {
  return path && argv && argv[0] && envp && fds && count >= 1 && count <= kChildFdsMax && pid;
}

void sleep_ms(unsigned ms) {
  struct timespec ts;
  ts.tv_sec = static_cast<time_t>(ms / 1000u);
  ts.tv_nsec = static_cast<long>((ms % 1000u) * 1000000ul);
  while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
  }
}

/* Polls for the child's exit for up to `ms`. */
bool wait_exit(pid_t pid, unsigned ms, int* status) {
  const unsigned step = 20;
  for (unsigned waited = 0;; waited += step) {
    if (child_exited(pid, status)) return true;
    if (waited >= ms) return false;
    sleep_ms(step);
  }
}

}  // namespace

#ifdef __QNXNTO__

int spawn_child(const char* path, char* const argv[], char* const envp[], const int* fds,
                unsigned count, bool own_group, pid_t* pid) {
  if (!valid_request(path, argv, envp, fds, count, pid)) return EINVAL;
  int fdmap[kChildFdsMax];
  for (unsigned i = 0; i < count; ++i) fdmap[i] = fds[i] >= 0 ? fds[i] : SPAWN_FDCLOSED;
  struct inheritance inherit;
  memset(&inherit, 0, sizeof(inherit));
  inherit.flags = SPAWN_SETSIGDEF | SPAWN_SETSIGMASK;
  sigfillset(&inherit.sigdefault);
  sigemptyset(&inherit.sigmask);
  if (own_group) {
    inherit.flags |= SPAWN_SETGROUP;
    inherit.pgroup = SPAWN_NEWPGROUP;
  }
  const pid_t child = ::spawn(path, static_cast<int>(count), fdmap, &inherit, argv, envp);
  if (child < 0) return errno ? errno : EIO;
  *pid = child;
  return 0;
}

#else

/* fork() and execve(): between them only async-signal-safe calls. An exec
 * failure comes back through a close-on-exec pipe, as spawn() reports it. */
int spawn_child(const char* path, char* const argv[], char* const envp[], const int* fds,
                unsigned count, bool own_group, pid_t* pid) {
  if (!valid_request(path, argv, envp, fds, count, pid)) return EINVAL;
  int max_fd = 1024;
  struct rlimit rl;
  if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur < 65536) {
    max_fd = static_cast<int>(rl.rlim_cur);
  }
  int pipefd[2];
  if (pipe(pipefd) != 0) return errno;
  /* The report end lives above the map, so the map cannot replace it. */
  const int report = fcntl(pipefd[1], F_DUPFD_CLOEXEC, static_cast<int>(kChildFdsMax));
  close(pipefd[1]);
  if (report < 0 || fcntl(pipefd[0], F_SETFD, FD_CLOEXEC) != 0) {
    const int e = errno;
    close(pipefd[0]);
    if (report >= 0) close(report);
    return e;
  }
  const pid_t child = fork();
  if (child < 0) {
    const int e = errno;
    close(pipefd[0]);
    close(report);
    return e;
  }
  if (child == 0) {
    int err = 0;
    int high[kChildFdsMax];
    for (unsigned i = 0; i < count; ++i) {
      high[i] = fds[i] >= 0 ? fcntl(fds[i], F_DUPFD, static_cast<int>(kChildFdsMax)) : -1;
      if (fds[i] >= 0 && high[i] < 0 && !err) err = errno;
    }
    for (unsigned i = 0; i < count; ++i) {
      if (high[i] < 0) {
        close(static_cast<int>(i));
      } else if (dup2(high[i], static_cast<int>(i)) < 0 && !err) {
        err = errno;
      }
    }
    for (int fd = static_cast<int>(count); fd < max_fd; ++fd) {
      if (fd != report) close(fd);
    }
    if (!err && own_group && setpgid(0, 0) != 0) err = errno;
    struct sigaction dfl;
    memset(&dfl, 0, sizeof(dfl));
    dfl.sa_handler = SIG_DFL;
    for (int sig = 1; sig < NSIG; ++sig) sigaction(sig, &dfl, NULL);
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, NULL);
    if (!err) {
      execve(path, argv, envp);
      err = errno;
    }
    ssize_t w = write(report, &err, sizeof(err));
    (void)w;
    _exit(127);
  }
  close(report);
  int err = 0;
  ssize_t n;
  do {
    n = read(pipefd[0], &err, sizeof(err));
  } while (n < 0 && errno == EINTR);
  close(pipefd[0]);
  if (n == static_cast<ssize_t>(sizeof(err))) {
    while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {
    }
    return err ? err : EIO;
  }
  *pid = child;
  return 0;
}

#endif

bool child_exited(pid_t pid, int* status) {
  int st = 0;
  pid_t r;
  do {
    r = waitpid(pid, &st, WNOHANG);
  } while (r < 0 && errno == EINTR);
  if (r == pid || (r < 0 && errno == ECHILD)) {
    if (status) *status = r == pid ? st : -1;
    return true;
  }
  return false;
}

int stop_child(pid_t pid, bool group, unsigned wait_ms, unsigned term_ms) {
  if (pid <= 0) return -1;
  int status = -1;
  if (wait_exit(pid, wait_ms, &status)) return status;
  kill(group ? -pid : pid, SIGTERM);
  if (wait_exit(pid, term_ms, &status)) return status;
  kill(group ? -pid : pid, SIGKILL);
  pid_t r;
  do {
    r = waitpid(pid, &status, 0);
  } while (r < 0 && errno == EINTR);
  return r == pid ? status : -1;
}

bool own_executable(char* out, size_t size) {
  if (!out || size == 0) return false;
  out[0] = '\0';
#ifdef __QNXNTO__
  char path[PATH_MAX];
  if (_cmdname(path) && path[0] == '/' && strlen(path) < size) {
    strcpy(out, path);
    return true;
  }
  FILE* f = fopen("/proc/self/exefile", "r");
  if (!f) return false;
  const bool ok = fgets(out, static_cast<int>(size), f) != NULL;
  fclose(f);
  if (ok) out[strcspn(out, "\n")] = '\0';
  return ok && out[0] == '/';
#else
  const ssize_t n = readlink("/proc/self/exe", out, size - 1);
  if (n <= 0) return false;
  out[n] = '\0';
  return true;
#endif
}

int extract_own_payload(const char* dir, const char* name, char* out, size_t size,
                        const char** why) {
  char exe[PATH_MAX];
  if (!own_executable(exe, sizeof(exe))) {
    if (why) *why = "cannot name this executable";
    return -1;
  }
  if (!dir || !dir[0] || !name || !name[0] || strchr(name, '/') ||
      snprintf(out, size, "%s/%s", dir, name) >= static_cast<int>(size)) {
    if (why) *why = "bad payload path";
    return -1;
  }
  return nbt_payload_extract(exe, out, why);
}

}  // namespace app
}  // namespace nbt
