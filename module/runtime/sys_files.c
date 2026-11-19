/* SPDX-License-Identifier: MPL-2.0 */
/* Descriptors and paths beyond plain files: bridge input datagrams,
 * directories, and the rest of what std::filesystem uses. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "nbt_module.h"


/* --- bridge input datagrams (pseudo descriptors, see nbt_module.h) --- */

ssize_t recv(int fd, void* buf, size_t len, int flags) {
  (void)flags; /* always non-blocking */
  if (!NBT_FD_IS_BRIDGE(fd) || !NBT_HAS(bridge_recv)) return nbt_fail(-NBT_EBADF);
  int32_t rc = nbt_host->bridge_recv(fd - NBT_FD_BRIDGE, buf, (uint32_t)len);
  return rc < 0 ? nbt_fail(rc) : rc;
}

int getsockopt(int fd, int level, int name, void* value, socklen_t* len) {
  if (!NBT_FD_IS_BRIDGE(fd)) return nbt_fail(-NBT_EBADF);
  if (level != SOL_SOCKET || name != SO_TYPE || !value || !len || *len < sizeof(int)) return nbt_fail(-NBT_EINVAL);
  *(int*)value = SOCK_DGRAM;
  *len = sizeof(int);
  return 0;
}

/* Descriptor flags have no meaning here: files are the host's, bridge input
 * is always non-blocking, and leases belong to the runner. */
int fcntl(int fd, int cmd, ...) {
  if (fd < 0) return nbt_fail(-NBT_EBADF);
  switch (cmd) {
    case F_GETFL: return NBT_FD_IS_BRIDGE(fd) ? O_RDWR | O_NONBLOCK : O_RDWR;
    case F_GETFD: return FD_CLOEXEC;
    case F_SETFL:
    case F_SETFD:
    case F_SETLK:
    case F_SETLKW: return 0;
    default: return nbt_fail(-NBT_EINVAL);
  }
}

/* --- directories --- */

struct nbt_dir {
  int32_t handle;
  struct dirent entry;
};

DIR* opendir(const char* path) {
  if (!NBT_HAS(dir_open)) {
    errno = ENOSYS;
    return NULL;
  }
  int32_t h = nbt_host->dir_open(path);
  if (h < 0) {
    nbt_fail(h);
    return NULL;
  }
  DIR* d = (DIR*)calloc(1, sizeof(*d));
  if (!d) {
    nbt_host->dir_close(h);
    errno = ENOMEM;
    return NULL;
  }
  d->handle = h;
  return d;
}

struct dirent* readdir(DIR* d) {
  uint32_t mode = 0;
  int32_t rc = nbt_host->dir_read(d->handle, d->entry.d_name, sizeof(d->entry.d_name), &mode);
  if (rc <= 0) {
    if (rc < 0) nbt_fail(rc);
    return NULL;
  }
  d->entry.d_ino = 1;
  d->entry.d_type = (mode & NBT_S_IFMT) == NBT_S_IFDIR ? DT_DIR : (mode & NBT_S_IFMT) == NBT_S_IFREG ? DT_REG : DT_UNKNOWN;
  return &d->entry;
}

int closedir(DIR* d) {
  int32_t rc = nbt_host->dir_close(d->handle);
  free(d);
  return rc < 0 ? nbt_fail(rc) : 0;
}

/* --- the rest of what std::filesystem uses --- */

/* Relative paths resolve against the data folder: it is the working one. */
char* getcwd(char* buf, size_t size) {
  const char* dir = nbt_host->get_env("NBT_DATA_DIR");
  if (!dir || !*dir) dir = "/";
  if (!buf || strlen(dir) + 1 > size) {
    errno = ERANGE;
    return NULL;
  }
  strcpy(buf, dir);
  return buf;
}

/* No symbolic links reach a module: an absolute path, or the data folder's. */
char* realpath(const char* path, char* resolved) {
  char cwd[256];
  char* out = resolved ? resolved : (char*)malloc(PATH_MAX);
  if (!out) return NULL;
  int n;
  if (path[0] == '/') {
    n = snprintf(out, PATH_MAX, "%s", path);
  } else {
    if (!getcwd(cwd, sizeof(cwd))) cwd[0] = '\0';
    n = snprintf(out, PATH_MAX, "%s/%s", cwd, path);
  }
  if (n < 0 || n >= PATH_MAX) {
    if (!resolved) free(out);
    errno = ENAMETOOLONG;
    return NULL;
  }
  struct stat st;
  if (stat(out, &st) != 0) {
    if (!resolved) free(out);
    return NULL;
  }
  return out;
}

long sysconf(int name) {
  switch (name) {
    case _SC_NPROCESSORS_CONF:
    case _SC_NPROCESSORS_ONLN: return 2; /* the head unit's dual Cortex-A15 */
    case _SC_PAGESIZE: return 4096;
    default: errno = EINVAL; return -1;
  }
}

int openat(int dirfd, const char* path, int flags, ...) {
  if (dirfd != AT_FDCWD && path[0] != '/') return nbt_fail(-NBT_ENOSYS);
  int mode = 0;
  if (flags & O_CREAT) {
    va_list ap;
    va_start(ap, flags);
    mode = va_arg(ap, int);
    va_end(ap);
  }
  return open(path, flags, mode);
}

int unlinkat(int dirfd, const char* path, int flags) {
  if ((dirfd != AT_FDCWD && path[0] != '/') || (flags & AT_REMOVEDIR)) return nbt_fail(-NBT_ENOSYS);
  return unlink(path);
}

int access(const char* path, int mode) {
  struct stat st;
  if (stat(path, &st) != 0) return -1;
  if ((mode & W_OK) && !(st.st_mode & 0200)) return nbt_fail(-NBT_EACCES);
  return 0;
}
