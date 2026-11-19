// SPDX-License-Identifier: MPL-2.0
#include "nbt/frontend/save_file.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace frontend {

namespace {

int open_existing(const char* path) {
  int flags = O_RDONLY;
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
  return open(path, flags);
}

bool read_all(int fd, unsigned char* out, size_t size) {
  size_t used = 0;
  while (used < size) {
    const ssize_t n = read(fd, out + used, size - used);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return false;
    used += static_cast<size_t>(n);
  }
  return true;
}

}  // namespace

bool save_file_write(const char* path, const void* data, size_t size) {
  char next[1024];
  if (!path || (!data && size) ||
      snprintf(next, sizeof(next), "%s.next", path) >= static_cast<int>(sizeof(next))) {
    return false;
  }
  unlink(next);
  const int fd = open(next, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0) return false;
  const unsigned char* bytes = static_cast<const unsigned char*>(data);
  size_t used = 0;
  while (used < size) {
    const ssize_t n = write(fd, bytes + used, size - used);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    used += static_cast<size_t>(n);
  }
  const bool written = used == size && fsync(fd) == 0;
  if (close(fd) || !written || rename(next, path)) {
    unlink(next);
    return false;
  }
  return true;
}

void* save_file_read(const char* path, size_t max, size_t* size) {
  struct stat st;
  if (size) *size = 0;
  const int fd = path ? open_existing(path) : -1;
  if (fd < 0) return NULL;
  void* data = NULL;
  if (!fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_size > 0 &&
      static_cast<uint64_t>(st.st_size) <= max) {
    const size_t total = static_cast<size_t>(st.st_size);
    data = malloc(total);
    if (data && read_all(fd, static_cast<unsigned char*>(data), total)) {
      if (size) *size = total;
    } else {
      free(data);
      data = NULL;
    }
  }
  close(fd);
  return data;
}

bool save_file_read_into(const char* path, void* data, size_t size) {
  struct stat st;
  const int fd = path && data && size ? open_existing(path) : -1;
  if (fd < 0) return false;
  bool ok = false;
  if (!fstat(fd, &st) && S_ISREG(st.st_mode) && static_cast<uint64_t>(st.st_size) == size) {
    unsigned char* copy = static_cast<unsigned char*>(malloc(size));
    if (copy && read_all(fd, copy, size)) {
      memcpy(data, copy, size);
      ok = true;
    }
    free(copy);
  }
  close(fd);
  return ok;
}

}  // namespace frontend
}  // namespace nbt
