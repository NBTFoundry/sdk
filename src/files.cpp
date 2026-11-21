// SPDX-License-Identifier: MPL-2.0
#include "nbt/app/files.hpp"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace app {

namespace {

const size_t kNameMax = 96;

bool forbidden(unsigned char c) {
  return c < 32 || c > 126 || strchr("/\\:*?\"<>|", c) != NULL;
}

/* Case-insensitive first, so "b.sfc" sorts between "A.sfc" and "C.sfc";
 * byte order breaks ties. */
int compare(const char* a, const char* b) {
  const int folded = strcasecmp(a, b);
  return folded ? folded : strcmp(a, b);
}

}  // namespace

bool file_name_valid(const char* name) {
  if (!name) return false;
  size_t n = 0;
  while (n <= kNameMax && name[n]) ++n;
  if (!n || n > kNameMax || name[0] == '.' || name[0] == ' ' || name[n - 1] == '.' ||
      name[n - 1] == ' ') {
    return false;
  }
  for (size_t i = 0; i < n; ++i) {
    if (forbidden(static_cast<unsigned char>(name[i]))) return false;
  }
  const char* dot = strrchr(name, '.');
  return dot && dot != name && dot[1];
}

bool file_extension_in(const char* name, const char* const* extensions) {
  if (!extensions) return true;
  const char* dot = name ? strrchr(name, '.') : NULL;
  if (!dot) return false;
  for (size_t i = 0; extensions[i]; ++i) {
    if (!strcasecmp(dot + 1, extensions[i])) return true;
  }
  return false;
}

FileList::FileList() : count_(0) {
  dir_[0] = '\0';
}

namespace {

/* A game folder's name: a stored name's rules, at most 95 bytes, no
 * extension needed (NBT Manager's nbt_files_folder_valid). */
bool folder_name_valid(const char* name) {
  size_t n = 0;
  while (n <= 95 && name[n]) ++n;
  if (!n || n > 95 || name[0] == '.' || name[0] == ' ' || name[n - 1] == '.' ||
      name[n - 1] == ' ') {
    return false;
  }
  for (size_t i = 0; i < n; ++i) {
    if (forbidden(static_cast<unsigned char>(name[i]))) return false;
  }
  return true;
}

/* The position of `name`'s extension in `mains`, or -1. */
int main_rank(const char* name, const char* const* mains) {
  const char* dot = strrchr(name, '.');
  for (int i = 0; dot && mains[i]; ++i) {
    if (!strcasecmp(dot + 1, mains[i])) return i;
  }
  return -1;
}

/* `folder`'s main file into `entry` (and the size of all its files); false
 * for none. */
bool game_folder(const char* path, const char* const* mains, FileEntry* entry) {
  DIR* folder = opendir(path);
  if (!folder) return false;
  int best = -1;
  uint64_t size = 0;
  char file[768];
  for (struct dirent* inner; (inner = readdir(folder)) != NULL;) {
    const char* name = inner->d_name;
    struct stat st;
    if (!file_name_valid(name)) continue;
    snprintf(file, sizeof(file), "%s/%s", path, name);
    if (lstat(file, &st) || !S_ISREG(st.st_mode)) continue;
    size += static_cast<uint64_t>(st.st_size);
    const int rank = main_rank(name, mains);
    if (rank >= 0 &&
        (best < 0 || rank < best || (rank == best && compare(name, entry->file) < 0))) {
      best = rank;
      memcpy(entry->file, name, strlen(name) + 1);
    }
  }
  closedir(folder);
  entry->size = size;
  return best >= 0;
}

}  // namespace

bool FileList::load(const char* dir, const char* const* extensions,
                    const char* const* folder_mains) {
  count_ = 0;
  dir_[0] = '\0';
  if (!dir || !dir[0] || strlen(dir) >= sizeof(dir_)) return false;
  snprintf(dir_, sizeof(dir_), "%s", dir);
  DIR* folder = opendir(dir_);
  if (!folder) return false;
  char path[sizeof(dir_) + 258]; /* any directory entry's name fits */
  for (struct dirent* entry; (entry = readdir(folder)) != NULL;) {
    const char* name = entry->d_name;
    struct stat st;
    FileEntry found;
    if (count_ == kMaxFiles) break;
    snprintf(path, sizeof(path), "%s/%s", dir_, name);
    if (lstat(path, &st)) continue;
    found.file[0] = '\0';
    if (S_ISDIR(st.st_mode)) {
      if (!folder_mains || !folder_name_valid(name) || !game_folder(path, folder_mains, &found)) {
        continue;
      }
    } else if (!S_ISREG(st.st_mode) || !file_name_valid(name) ||
               !file_extension_in(name, extensions)) {
      continue;
    } else {
      found.size = static_cast<uint64_t>(st.st_size);
    }
    memcpy(found.name, name, strlen(name) + 1); /* valid: at most 96 bytes */
    size_t i = count_++;
    for (; i > 0 && compare(entries_[i - 1].name, name) > 0; --i) {
      entries_[i] = entries_[i - 1];
    }
    entries_[i] = found;
  }
  closedir(folder);
  return true;
}

int FileList::find(const char* name) const {
  for (size_t i = 0; name && i < count_; ++i) {
    if (!strcmp(entries_[i].name, name)) return static_cast<int>(i);
  }
  return -1;
}

bool FileList::path(size_t i, char* out, size_t size) const {
  if (i >= count_ || !out || !size) return false;
  const FileEntry& e = entries_[i];
  const int n = e.file[0] ? snprintf(out, size, "%s/%s/%s", dir_, e.name, e.file)
                          : snprintf(out, size, "%s/%s", dir_, e.name);
  return n > 0 && static_cast<size_t>(n) < size;
}

void* FileList::read(size_t i, size_t max, size_t* size) const {
  char file[sizeof(dir_) + 2 * kNameMax + 3];
  struct stat st;
  if (size) *size = 0;
  if (!path(i, file, sizeof(file))) return NULL;
  int flags = O_RDONLY;
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
  const int fd = open(file, flags);
  if (fd < 0) return NULL;
  if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
      static_cast<uint64_t>(st.st_size) > max) {
    close(fd);
    return NULL;
  }
  const size_t total = static_cast<size_t>(st.st_size);
  unsigned char* data = static_cast<unsigned char*>(malloc(total));
  size_t used = 0;
  while (data && used < total) {
    const ssize_t n = ::read(fd, data + used, total - used);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    used += static_cast<size_t>(n);
  }
  close(fd);
  if (!data || used != total) {
    free(data);
    return NULL;
  }
  if (size) *size = total;
  return data;
}

}  // namespace app
}  // namespace nbt
