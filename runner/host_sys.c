/* SPDX-License-Identifier: MPL-2.0 */
/* The runner's system services (runner/host_sys.h). QNX (built with the SDP);
 * also builds on Linux for unit tests of the confinement rules. */
#include "host_sys.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef __QNX__
#include <sys/neutrino.h>
#endif

/* --- errors --- */

int32_t runner_host_error(int e) {
  switch (e) {
    case EPERM: return -NBT_EPERM;
    case ENOENT: return -NBT_ENOENT;
    case EINTR: return -NBT_EINTR;
    case EBADF: return -NBT_EBADF;
    case EAGAIN: return -NBT_EAGAIN;
    case ENOMEM: return -NBT_ENOMEM;
    case EACCES: return -NBT_EACCES;
    case EEXIST: return -NBT_EEXIST;
    case ENOTDIR: return -NBT_ENOTDIR;
    case EISDIR: return -NBT_EISDIR;
    case EINVAL: return -NBT_EINVAL;
    case EMFILE: return -NBT_EMFILE;
    case ENOSPC: return -NBT_ENOSPC;
    case EROFS: return -NBT_EROFS;
    case ENAMETOOLONG: return -NBT_ENAMETOOLONG;
    case ENOSYS: return -NBT_ENOSYS;
    case ENOTEMPTY: return -NBT_ENOTEMPTY;
    case ETIMEDOUT: return -NBT_ETIMEDOUT;
    default: return -NBT_EIO;
  }
}

/* --- output and time --- */

static int32_t h_write_output(int32_t fd, const void* buf, uint32_t len) {
  if (fd != 1 && fd != 2) return -NBT_EBADF;
  ssize_t n = write(fd, buf, len);
  return n < 0 ? runner_host_error(errno) : (int32_t)n;
}

static int64_t clock_ns(clockid_t c) {
  struct timespec ts;
  clock_gettime(c, &ts);
  return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
static int64_t h_monotonic_ns(void) { return clock_ns(CLOCK_MONOTONIC); }
static int64_t h_realtime_ns(void) { return clock_ns(CLOCK_REALTIME); }

static int32_t h_sleep_ns(int64_t ns) {
  if (ns <= 0) return 0;
  struct timespec ts = {(time_t)(ns / 1000000000), (long)(ns % 1000000000)};
  while (nanosleep(&ts, &ts) != 0) {
    if (errno != EINTR) return runner_host_error(errno);
  }
  return 0;
}

/* --- memory --- */

static int prot_of(int32_t p) {
  return (p & NBT_PROT_READ ? PROT_READ : 0) | (p & NBT_PROT_WRITE ? PROT_WRITE : 0) |
         (p & NBT_PROT_EXEC ? PROT_EXEC : 0);
}

static void* h_mem_map(uint32_t len, int32_t prot, int32_t flags) {
  int f = MAP_PRIVATE | MAP_ANON;
#ifdef MAP_LAZY
  if (flags & NBT_MAP_LAZY) f |= MAP_LAZY;
#else
  (void)flags;
#endif
  void* p = mmap(NULL, len, prot_of(prot), f, -1, 0);
  return p == MAP_FAILED ? NULL : p;
}
static int32_t h_mem_unmap(void* addr, uint32_t len) {
  return munmap(addr, len) == 0 ? 0 : runner_host_error(errno);
}
static int32_t h_mem_protect(void* addr, uint32_t len, int32_t prot) {
  return mprotect(addr, len, prot_of(prot)) == 0 ? 0 : runner_host_error(errno);
}
static int32_t h_icache_flush(void* addr, uint32_t len) {
#ifdef MS_INVALIDATE_ICACHE
  return msync(addr, len, MS_INVALIDATE_ICACHE) == 0 ? 0 : runner_host_error(errno);
#else
  __builtin___clear_cache((char*)addr, (char*)addr + len);
  return 0;
#endif
}

/* --- threads --- */

static pthread_key_t thread_word_key;

static int32_t h_thread_create(uintptr_t* handle, void* (*fn)(void*), void* arg, uint32_t stack_size) {
  pthread_attr_t attr;
  pthread_t t;
  pthread_attr_init(&attr);
  if (stack_size < 16384) stack_size = 16384;
  pthread_attr_setstacksize(&attr, stack_size);
  int rc = pthread_create(&t, &attr, fn, arg);
  pthread_attr_destroy(&attr);
  if (rc != 0) return runner_host_error(rc);
  *handle = (uintptr_t)t;
  return 0;
}
static int32_t h_thread_join(uintptr_t handle, void** result) {
  int rc = pthread_join((pthread_t)handle, result);
  return rc == 0 ? 0 : runner_host_error(rc);
}
static int32_t h_thread_detach(uintptr_t handle) {
  int rc = pthread_detach((pthread_t)handle);
  return rc == 0 ? 0 : runner_host_error(rc);
}
static void h_thread_yield(void) { sched_yield(); }
static void* h_thread_word_get(void) { return pthread_getspecific(thread_word_key); }
static void h_thread_word_set(void* value) { pthread_setspecific(thread_word_key, value); }

/* Address-keyed waiting: a waiter checks the word under its bucket's mutex,
 * so a waker that changed the word and then calls wake cannot be missed. */
#define WAIT_BUCKETS 64
static struct {
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} buckets[WAIT_BUCKETS];

static unsigned bucket_of(volatile uint32_t* addr) {
  uintptr_t a = (uintptr_t)addr >> 2;
  return (unsigned)((a ^ (a >> 6) ^ (a >> 12)) % WAIT_BUCKETS);
}

static int32_t h_wait(volatile uint32_t* addr, uint32_t expected, int64_t timeout_ns) {
  unsigned b = bucket_of(addr);
  int32_t result = 0;
  pthread_mutex_lock(&buckets[b].mutex);
  if (__atomic_load_n(addr, __ATOMIC_ACQUIRE) != expected) {
    result = -NBT_EAGAIN;
  } else if (timeout_ns < 0) {
    pthread_cond_wait(&buckets[b].cond, &buckets[b].mutex);
  } else {
    struct timespec until;
    clock_gettime(CLOCK_MONOTONIC, &until);
    int64_t ns = (int64_t)until.tv_nsec + timeout_ns % 1000000000;
    until.tv_sec += (time_t)(timeout_ns / 1000000000 + ns / 1000000000);
    until.tv_nsec = (long)(ns % 1000000000);
    if (pthread_cond_timedwait(&buckets[b].cond, &buckets[b].mutex, &until) == ETIMEDOUT) {
      result = -NBT_ETIMEDOUT;
    }
  }
  pthread_mutex_unlock(&buckets[b].mutex);
  return result;
}

static int32_t h_wake(volatile uint32_t* addr, int32_t count) {
  unsigned b = bucket_of(addr);
  (void)count; /* several words share a bucket: wake them all */
  pthread_mutex_lock(&buckets[b].mutex);
  pthread_cond_broadcast(&buckets[b].cond);
  pthread_mutex_unlock(&buckets[b].mutex);
  return 0;
}

/* --- files, confined to the data folder --- */

static char data_dir[PATH_MAX];
static char files_dir[PATH_MAX]; /* read only; "" when none */
static char cds_dir[PATH_MAX];   /* NBT_CDS_DIR: its car-data values, read only; "" when none */
#define CDS_VALUE_PREFIX "nbt-bridge1-cds-"
#define MAX_FILES 64
static int file_fd[MAX_FILES];

static int under(const char* path, const char* dir) {
  size_t n = strlen(dir);
  return n > 0 && strncmp(path, dir, n) == 0 && (path[n] == '/' || path[n] == '\0');
}

/* A car-data value the manager publishes for the application (capability
 * `car-data`): a file of that name directly in NBT_CDS_DIR, nothing else there. */
static int cds_value(const char* path) {
  size_t n = strlen(cds_dir);
  const char* name = path + n + 1;
  return n > 0 && under(path, cds_dir) && path[n] == '/' && !strchr(name, '/') &&
         strncmp(name, CDS_VALUE_PREFIX, sizeof(CDS_VALUE_PREFIX) - 1) == 0;
}

/* Absolute paths must lie in the data folder, or (reading only) in the files
 * folder or be a car-data value; relative paths resolve against the data
 * folder. */
static int32_t resolve(const char* path, char* out, size_t size, int write) {
  if (!path || !*path) return -NBT_ENOENT;
  /* No ".." component, anywhere. */
  for (const char* p = path; *p;) {
    const char* end = strchr(p, '/');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (len == 2 && p[0] == '.' && p[1] == '.') return -NBT_EACCES;
    if (!end) break;
    p = end + 1;
  }
  if (path[0] == '/') {
    if (!under(path, data_dir) && (write || (!under(path, files_dir) && !cds_value(path)))) {
      return -NBT_EACCES;
    }
    if (strlen(path) >= size) return -NBT_ENAMETOOLONG;
    strcpy(out, path);
    return 0;
  }
  if ((size_t)snprintf(out, size, "%s/%s", data_dir, path) >= size) return -NBT_ENAMETOOLONG;
  return 0;
}

static int32_t h_file_open(const char* path, int32_t flags, int32_t mode) {
  char full[PATH_MAX];
  int writing = (flags & NBT_O_ACCMODE) != NBT_O_RDONLY || (flags & (NBT_O_CREAT | NBT_O_TRUNC | NBT_O_APPEND));
  int32_t rc = resolve(path, full, sizeof(full), writing);
  if (rc) return rc;
  int slot = 0;
  while (slot < MAX_FILES && file_fd[slot] >= 0) ++slot;
  if (slot == MAX_FILES) return -NBT_EMFILE;
  int f = (flags & NBT_O_ACCMODE) == NBT_O_WRONLY ? O_WRONLY
          : (flags & NBT_O_ACCMODE) == NBT_O_RDWR ? O_RDWR
                                                  : O_RDONLY;
  if (flags & NBT_O_CREAT) f |= O_CREAT;
  if (flags & NBT_O_TRUNC) f |= O_TRUNC;
  if (flags & NBT_O_APPEND) f |= O_APPEND;
  if (flags & NBT_O_EXCL) f |= O_EXCL;
  int fd = open(full, f | O_CLOEXEC, (mode_t)(mode & 0777));
  if (fd < 0) return runner_host_error(errno);
  file_fd[slot] = fd;
  return slot;
}

static int fd_of(int32_t slot) { return slot >= 0 && slot < MAX_FILES ? file_fd[slot] : -1; }

static int32_t h_file_read(int32_t slot, void* buf, uint32_t len) {
  int fd = fd_of(slot);
  if (fd < 0) return -NBT_EBADF;
  ssize_t n = read(fd, buf, len);
  return n < 0 ? runner_host_error(errno) : (int32_t)n;
}
static int32_t h_file_write(int32_t slot, const void* buf, uint32_t len) {
  int fd = fd_of(slot);
  if (fd < 0) return -NBT_EBADF;
  ssize_t n = write(fd, buf, len);
  return n < 0 ? runner_host_error(errno) : (int32_t)n;
}
static int64_t h_file_seek(int32_t slot, int64_t offset, int32_t whence) {
  int fd = fd_of(slot);
  if (fd < 0) return -NBT_EBADF;
  int w = whence == NBT_SEEK_CUR ? SEEK_CUR : whence == NBT_SEEK_END ? SEEK_END : SEEK_SET;
  off64_t r = lseek64(fd, (off64_t)offset, w);
  return r < 0 ? runner_host_error(errno) : (int64_t)r;
}
static int32_t h_file_close(int32_t slot) {
  int fd = fd_of(slot);
  if (fd < 0) return -NBT_EBADF;
  file_fd[slot] = -1;
  return close(fd) == 0 ? 0 : runner_host_error(errno);
}
static void to_nbt_stat(const struct stat* s, nbt_stat* out) {
  memset(out, 0, sizeof(*out));
  out->size = (uint64_t)s->st_size;
  out->mtime_ns = (int64_t)s->st_mtime * 1000000000;
  out->mode = (S_ISDIR(s->st_mode) ? NBT_S_IFDIR : S_ISREG(s->st_mode) ? NBT_S_IFREG : 0) |
              (uint32_t)(s->st_mode & 0777);
}
static int32_t h_file_stat(const char* path, nbt_stat* out) {
  char full[PATH_MAX];
  struct stat s;
  int32_t rc = resolve(path, full, sizeof(full), 0);
  if (rc) return rc;
  if (stat(full, &s) != 0) return runner_host_error(errno);
  to_nbt_stat(&s, out);
  return 0;
}
static int32_t h_file_fstat(int32_t slot, nbt_stat* out) {
  struct stat s;
  int fd = fd_of(slot);
  if (fd < 0) return -NBT_EBADF;
  if (fstat(fd, &s) != 0) return runner_host_error(errno);
  to_nbt_stat(&s, out);
  return 0;
}
static int32_t h_file_unlink(const char* path) {
  char full[PATH_MAX];
  int32_t rc = resolve(path, full, sizeof(full), 1);
  if (rc) return rc;
  return unlink(full) == 0 ? 0 : runner_host_error(errno);
}
static int32_t h_file_mkdir(const char* path, int32_t mode) {
  char full[PATH_MAX];
  int32_t rc = resolve(path, full, sizeof(full), 1);
  if (rc) return rc;
  return mkdir(full, (mode_t)(mode & 0777)) == 0 ? 0 : runner_host_error(errno);
}
static int32_t h_file_rename(const char* from, const char* to) {
  char a[PATH_MAX], b[PATH_MAX];
  int32_t rc = resolve(from, a, sizeof(a), 1);
  if (!rc) rc = resolve(to, b, sizeof(b), 1);
  if (rc) return rc;
  return rename(a, b) == 0 ? 0 : runner_host_error(errno);
}
static int32_t h_file_sync(int32_t slot) {
  int fd = fd_of(slot);
  if (fd < 0) return -NBT_EBADF;
  return fsync(fd) == 0 ? 0 : runner_host_error(errno);
}

/* --- environment, randomness, exit --- */

static const char* h_get_env(const char* name) {
  if (!name || strncmp(name, "NBT_", 4) != 0) return NULL;
  return getenv(name);
}

static int32_t h_random_bytes(void* buf, uint32_t len) {
  int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  if (fd >= 0) {
    ssize_t n = read(fd, buf, len);
    close(fd);
    if (n == (ssize_t)len) return (int32_t)len;
  }
  /* No random service (a bare test image): cycle counter and clocks. */
  uint64_t x = (uint64_t)h_monotonic_ns() ^ ((uint64_t)h_realtime_ns() << 17);
#ifdef __QNX__
  x ^= ClockCycles();
#endif
  for (uint32_t i = 0; i < len; ++i) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    ((unsigned char*)buf)[i] = (unsigned char)x;
  }
  return (int32_t)len;
}

static void h_exit_process(int32_t status) {
  fflush(NULL);
  _exit(status);
}

/* --- ABI self-test --- */

static double h_abi_test_mix(int32_t a, double b, float c, int64_t d, float e) {
  return (double)a + b * 2.0 + (double)c * 3.0 + (double)d + (double)e * 5.0;
}
static double h_abi_test_callback(double (*fn)(float, double, int64_t, int32_t)) {
  return fn(1.5f, 2.25, (int64_t)3 << 33, -9);
}


/* --- directories (data folder; files folder read only) --- */

#define MAX_DIRS 16
static struct {
  DIR* dir;
  char path[PATH_MAX];
} dirs[MAX_DIRS];

static int32_t h_dir_open(const char* path) {
  char full[PATH_MAX];
  int32_t rc = resolve(path, full, sizeof(full), 0);
  if (rc) return rc;
  int slot = 0;
  while (slot < MAX_DIRS && dirs[slot].dir) ++slot;
  if (slot == MAX_DIRS) return -NBT_EMFILE;
  DIR* d = opendir(full);
  if (!d) return runner_host_error(errno);
  dirs[slot].dir = d;
  snprintf(dirs[slot].path, sizeof(dirs[slot].path), "%s", full);
  return slot;
}

static int32_t h_dir_read(int32_t slot, char* name, uint32_t name_len, uint32_t* mode) {
  if (slot < 0 || slot >= MAX_DIRS || !dirs[slot].dir || !name || name_len == 0) return -NBT_EBADF;
  for (;;) {
    struct dirent* e = readdir(dirs[slot].dir);
    if (!e) return 0;
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
    if (strlen(e->d_name) >= name_len) continue; /* cannot be named back: skip */
    snprintf(name, name_len, "%s", e->d_name);
    if (mode) {
      char full[PATH_MAX];
      struct stat s;
      *mode = 0;
      if ((size_t)snprintf(full, sizeof(full), "%s/%s", dirs[slot].path, e->d_name) < sizeof(full) &&
          lstat(full, &s) == 0)
        *mode = (S_ISDIR(s.st_mode) ? NBT_S_IFDIR : S_ISREG(s.st_mode) ? NBT_S_IFREG : 0) | (uint32_t)(s.st_mode & 0777);
    }
    return 1;
  }
}

static int32_t h_dir_close(int32_t slot) {
  if (slot < 0 || slot >= MAX_DIRS || !dirs[slot].dir) return -NBT_EBADF;
  closedir(dirs[slot].dir);
  dirs[slot].dir = NULL;
  return 0;
}

/* --- setup --- */

int runner_sys_init(const char* data, const char* files) {
  if (!data || !*data || strlen(data) >= sizeof(data_dir)) return -1;
  strcpy(data_dir, data);
  files_dir[0] = '\0';
  if (files && *files && strlen(files) < sizeof(files_dir)) strcpy(files_dir, files);
  const char* cds = getenv("NBT_CDS_DIR");
  cds_dir[0] = '\0';
  if (cds && cds[0] == '/' && strlen(cds) < sizeof(cds_dir)) strcpy(cds_dir, cds);
  for (int i = 0; i < MAX_FILES; ++i) file_fd[i] = -1;
  for (int i = 0; i < WAIT_BUCKETS; ++i) {
    pthread_mutex_init(&buckets[i].mutex, NULL);
    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    pthread_cond_init(&buckets[i].cond, &ca);
    pthread_condattr_destroy(&ca);
  }
  pthread_key_create(&thread_word_key, NULL);
  return 0;
}

uint64_t runner_now_ms(void) {
  return (uint64_t)h_monotonic_ns() / 1000000u;
}

void runner_sys_fill(nbt_host_v1* h) {
  h->abi_major = NBT_HOST_ABI_MAJOR;
  h->abi_minor = NBT_HOST_ABI_MINOR;
  h->size = sizeof(nbt_host_v1);
  h->write_output = h_write_output;
  h->monotonic_ns = h_monotonic_ns;
  h->realtime_ns = h_realtime_ns;
  h->sleep_ns = h_sleep_ns;
  h->mem_map = h_mem_map;
  h->mem_unmap = h_mem_unmap;
  h->mem_protect = h_mem_protect;
  h->icache_flush = h_icache_flush;
  h->thread_create = h_thread_create;
  h->thread_join = h_thread_join;
  h->thread_detach = h_thread_detach;
  h->thread_yield = h_thread_yield;
  h->thread_word_get = h_thread_word_get;
  h->thread_word_set = h_thread_word_set;
  h->wait = h_wait;
  h->wake = h_wake;
  h->file_open = h_file_open;
  h->file_read = h_file_read;
  h->file_write = h_file_write;
  h->file_seek = h_file_seek;
  h->file_close = h_file_close;
  h->file_stat = h_file_stat;
  h->file_fstat = h_file_fstat;
  h->file_unlink = h_file_unlink;
  h->file_mkdir = h_file_mkdir;
  h->file_rename = h_file_rename;
  h->file_sync = h_file_sync;
  h->get_env = h_get_env;
  h->random_bytes = h_random_bytes;
  h->exit_process = h_exit_process;
  h->abi_test_mix = h_abi_test_mix;
  h->abi_test_callback = h_abi_test_callback;
  h->dir_open = h_dir_open;
  h->dir_read = h_dir_read;
  h->dir_close = h_dir_close;
}
