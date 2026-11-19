/* SPDX-License-Identifier: MPL-2.0 */
/* The system calls picolibc needs, forwarded to the host table. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <time.h>
#include <unistd.h>

#include "nbt_module.h"

int nbt_errno_from_host(int32_t error) {
  switch (error) {
    case NBT_EPERM: return EPERM;
    case NBT_ENOENT: return ENOENT;
    case NBT_EINTR: return EINTR;
    case NBT_EBADF: return EBADF;
    case NBT_EAGAIN: return EAGAIN;
    case NBT_ENOMEM: return ENOMEM;
    case NBT_EACCES: return EACCES;
    case NBT_EEXIST: return EEXIST;
    case NBT_ENOTDIR: return ENOTDIR;
    case NBT_EISDIR: return EISDIR;
    case NBT_EINVAL: return EINVAL;
    case NBT_EMFILE: return EMFILE;
    case NBT_ENOSPC: return ENOSPC;
    case NBT_EROFS: return EROFS;
    case NBT_ENAMETOOLONG: return ENAMETOOLONG;
    case NBT_ENOSYS: return ENOSYS;
    case NBT_ENOTEMPTY: return ENOTEMPTY;
    case NBT_ETIMEDOUT: return ETIMEDOUT;
    default: return EIO;
  }
}

int nbt_fail(int32_t rc) {
  errno = nbt_errno_from_host(-rc);
  return -1;
}

/* fds 0-2 are the console; files the host opens are offset past them. */
#define NBT_FD_BASE 3

ssize_t write(int fd, const void* buf, size_t len) {
  int32_t rc;
  if (fd == 1 || fd == 2) {
    rc = nbt_host->write_output(fd, buf, (uint32_t)len);
  } else if (fd >= NBT_FD_BASE) {
    rc = nbt_host->file_write(fd - NBT_FD_BASE, buf, (uint32_t)len);
  } else {
    rc = -NBT_EBADF;
  }
  return rc < 0 ? nbt_fail(rc) : rc;
}

ssize_t read(int fd, void* buf, size_t len) {
  if (fd == 0) return 0; /* no console input */
  if (fd < NBT_FD_BASE) return nbt_fail(-NBT_EBADF);
  int32_t rc = nbt_host->file_read(fd - NBT_FD_BASE, buf, (uint32_t)len);
  return rc < 0 ? nbt_fail(rc) : rc;
}

int open(const char* path, int flags, ...) {
  int mode = 0;
  if (flags & O_CREAT) {
    va_list ap;
    va_start(ap, flags);
    mode = va_arg(ap, int);
    va_end(ap);
  }
  int32_t f = 0;
  switch (flags & O_ACCMODE) {
    case O_WRONLY: f = NBT_O_WRONLY; break;
    case O_RDWR: f = NBT_O_RDWR; break;
    default: f = NBT_O_RDONLY; break;
  }
  if (flags & O_CREAT) f |= NBT_O_CREAT;
  if (flags & O_TRUNC) f |= NBT_O_TRUNC;
  if (flags & O_APPEND) f |= NBT_O_APPEND;
  if (flags & O_EXCL) f |= NBT_O_EXCL;
  int32_t rc = nbt_host->file_open(path, f, mode);
  return rc < 0 ? nbt_fail(rc) : rc + NBT_FD_BASE;
}

int close(int fd) {
  if (fd < NBT_FD_BASE) return 0;
  int32_t rc = nbt_host->file_close(fd - NBT_FD_BASE);
  return rc < 0 ? nbt_fail(rc) : 0;
}

off_t lseek(int fd, off_t offset, int whence) {
  if (fd < NBT_FD_BASE) return nbt_fail(-NBT_EBADF);
  int32_t w = whence == SEEK_CUR ? NBT_SEEK_CUR : whence == SEEK_END ? NBT_SEEK_END : NBT_SEEK_SET;
  int64_t rc = nbt_host->file_seek(fd - NBT_FD_BASE, (int64_t)offset, w);
  return rc < 0 ? (off_t)nbt_fail((int32_t)rc) : (off_t)rc;
}

static void to_stat(const nbt_stat* in, struct stat* out) {
  memset(out, 0, sizeof(*out));
  out->st_size = (off_t)in->size;
  out->st_mode = (mode_t)in->mode;
  out->st_nlink = 1;
  out->st_blksize = 4096;
  out->st_mtime = (time_t)(in->mtime_ns / 1000000000);
}

int fstat(int fd, struct stat* st) {
  if (fd < NBT_FD_BASE) {
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFCHR | 0600;
    return 0;
  }
  nbt_stat s;
  int32_t rc = nbt_host->file_fstat(fd - NBT_FD_BASE, &s);
  if (rc < 0) return nbt_fail(rc);
  to_stat(&s, st);
  return 0;
}

int stat(const char* path, struct stat* st) {
  nbt_stat s;
  int32_t rc = nbt_host->file_stat(path, &s);
  if (rc < 0) return nbt_fail(rc);
  to_stat(&s, st);
  return 0;
}

int lstat(const char* path, struct stat* st) { return stat(path, st); }

int isatty(int fd) { return fd >= 0 && fd < NBT_FD_BASE; }

int unlink(const char* path) {
  int32_t rc = nbt_host->file_unlink(path);
  return rc < 0 ? nbt_fail(rc) : 0;
}

int mkdir(const char* path, mode_t mode) {
  int32_t rc = nbt_host->file_mkdir(path, (int32_t)mode);
  return rc < 0 ? nbt_fail(rc) : 0;
}

int rename(const char* from, const char* to) {
  int32_t rc = nbt_host->file_rename(from, to);
  return rc < 0 ? nbt_fail(rc) : 0;
}

int fsync(int fd) {
  if (fd < NBT_FD_BASE) return 0;
  int32_t rc = nbt_host->file_sync(fd - NBT_FD_BASE);
  return rc < 0 ? nbt_fail(rc) : 0;
}

/* Heap: one lazily committed arena from the host, handed out by sbrk. */
#ifndef NBT_HEAP_ARENA
#define NBT_HEAP_ARENA (96u << 20)
#endif
static char* heap_base;
static uint32_t heap_used;

void* sbrk(ptrdiff_t incr) {
  if (!heap_base) {
    heap_base = (char*)nbt_host->mem_map(NBT_HEAP_ARENA, NBT_PROT_READ | NBT_PROT_WRITE, NBT_MAP_LAZY);
    if (!heap_base) {
      errno = ENOMEM;
      return (void*)-1;
    }
  }
  if (incr < 0 ? (uint32_t)-incr > heap_used : (uint32_t)incr > NBT_HEAP_ARENA - heap_used) {
    errno = ENOMEM;
    return (void*)-1;
  }
  char* old = heap_base + heap_used;
  heap_used += (uint32_t)incr;
  return old;
}

void _exit(int status) {
  nbt_host->exit_process(status);
  for (;;) {
  }
}

int gettimeofday(struct timeval* tv, void* tz) {
  (void)tz;
  if (tv) {
    int64_t ns = nbt_host->realtime_ns();
    tv->tv_sec = (time_t)(ns / 1000000000);
    tv->tv_usec = (suseconds_t)((ns % 1000000000) / 1000);
  }
  return 0;
}

int clock_gettime(clockid_t clock, struct timespec* ts) {
  int64_t ns = clock == CLOCK_REALTIME ? nbt_host->realtime_ns() : nbt_host->monotonic_ns();
  ts->tv_sec = (time_t)(ns / 1000000000);
  ts->tv_nsec = (long)(ns % 1000000000);
  return 0;
}

clock_t times(struct tms* t) {
  int64_t ticks = nbt_host->monotonic_ns() / (1000000000 / CLOCKS_PER_SEC);
  if (t) memset(t, 0, sizeof(*t));
  return (clock_t)ticks;
}

int nanosleep(const struct timespec* req, struct timespec* rem) {
  /* req and rem may be the same struct (libc++ passes both): read first. */
  int64_t ns = (int64_t)req->tv_sec * 1000000000 + req->tv_nsec;
  int32_t rc = nbt_host->sleep_ns(ns);
  if (rem) rem->tv_sec = rem->tv_nsec = 0;
  return rc < 0 ? nbt_fail(rc) : 0;
}

int usleep(useconds_t usec) { return nbt_host->sleep_ns((int64_t)usec * 1000) < 0 ? -1 : 0; }

unsigned sleep(unsigned seconds) {
  nbt_host->sleep_ns((int64_t)seconds * 1000000000);
  return 0;
}

int getentropy(void* buf, size_t len) {
  if (len > 256) {
    errno = EIO;
    return -1;
  }
  int32_t rc = nbt_host->random_bytes(buf, (uint32_t)len);
  return rc == (int32_t)len ? 0 : nbt_fail(rc < 0 ? rc : -NBT_EIO);
}

char* getenv(const char* name) { return (char*)nbt_host->get_env(name); }

pid_t getpid(void) { return 1; }

int sigprocmask(int how, const sigset_t* set, sigset_t* old) {
  (void)how;
  (void)set;
  if (old) memset(old, 0, sizeof(*old));
  return 0;
}

/* Signals belong to the runner (stop and show requests reach the module as
 * host events; crashes are reported by the runner). */
int sigaction(int sig, const struct sigaction* act, struct sigaction* old) {
  (void)sig;
  (void)act;
  if (old) memset(old, 0, sizeof(*old));
  return 0;
}

uid_t geteuid(void) { return 0; } /* stat reports files as the module's own */
uid_t getuid(void) { return 0; }

/* Optional hooks that picolibc and C++ code reference weakly. Defined here so
 * a module imports nothing (an unresolved weak symbol would be looked up in
 * the runner's scope). */
char* _user_strerror(int errnum, int internal, int* errptr) {
  (void)errnum;
  (void)internal;
  (void)errptr;
  return NULL;
}
/* C++ thread_local initialiser for errno (constant-initialised: nothing to do). */
void _ZTH5errno(void) {}
