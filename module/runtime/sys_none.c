/* SPDX-License-Identifier: MPL-2.0 */
/* What a module or guest cannot do: no other processes, no shell, and none of
 * the file operations the host does not offer (permissions, links, sizes,
 * times, space). Declared by the C library, so portable code links; each
 * fails with ENOSYS. */
#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include "nbt_module.h"

#pragma GCC diagnostic ignored "-Wunused-parameter"

#define UNSUPPORTED(type, name, params) \
  type name params {                    \
    errno = ENOSYS;                     \
    return (type)-1;                    \
  }

UNSUPPORTED(pid_t, fork, (void))
UNSUPPORTED(int, execl, (const char* path, const char* arg, ...))
UNSUPPORTED(int, execv, (const char* path, char* const argv[]))
UNSUPPORTED(int, execvp, (const char* file, char* const argv[]))
UNSUPPORTED(int, execve, (const char* path, char* const argv[], char* const envp[]))
UNSUPPORTED(pid_t, waitpid, (pid_t pid, int* status, int options))
UNSUPPORTED(int, pipe, (int fds[2]))
UNSUPPORTED(int, dup, (int fd))
UNSUPPORTED(int, dup2, (int from, int to))
UNSUPPORTED(int, poll, (struct pollfd * fds, nfds_t nfds, int timeout))
UNSUPPORTED(int, chdir, (const char* path))
UNSUPPORTED(int, fchmod, (int fd, mode_t mode))
UNSUPPORTED(int, fchmodat, (int dirfd, const char* path, mode_t mode, int flags))
UNSUPPORTED(int, link, (const char* from, const char* to))
UNSUPPORTED(int, symlink, (const char* from, const char* to))
UNSUPPORTED(int, truncate, (const char* path, off_t len))
UNSUPPORTED(int, ftruncate, (int fd, off_t len))
UNSUPPORTED(int, utimes, (const char* path, const struct timeval times[2]))
UNSUPPORTED(int, statvfs, (const char* path, struct statvfs* out))

DIR* fdopendir(int fd) {
  errno = ENOSYS;
  return NULL;
}

/* Nothing is a symbolic link. */
ssize_t readlink(const char* path, char* buf, size_t len) {
  errno = EINVAL;
  return -1;
}

int system(const char* command) { return command ? -1 : 0; /* no shell */ }

/* The only process a module can signal is its own (abort, raise). */
int kill(pid_t pid, int sig) {
  if (pid != 0 && pid != getpid()) {
    errno = ESRCH;
    return -1;
  }
  if (sig != 0) nbt_host->exit_process(128 + sig);
  return 0;
}
