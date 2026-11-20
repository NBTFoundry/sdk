// SPDX-License-Identifier: MPL-2.0
#include "nbt/app/car_data.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nbt/bridge_hold.h"

namespace nbt {
namespace app {

namespace {

bool name_valid(const char* name) {
  const size_t n = name ? strlen(name) : 0;
  if (n < 3 || n > 64 || name[0] == '.' || name[n - 1] == '.') return false;
  for (size_t i = 0; i < n; ++i) {
    const char c = name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
          (c == '.' && name[i - 1] != '.'))) {
      return false;
    }
  }
  return strchr(name, '.') != NULL;
}

}  // namespace

CarData::CarData() {
  dir_[0] = '\0';
}

bool CarData::init() {
  const char* dir = getenv("NBT_CDS_DIR");
  if (!dir || dir[0] != '/' || strlen(dir) >= sizeof(dir_)) return false;
  snprintf(dir_, sizeof(dir_), "%s", dir);
  return true;
}

bool CarData::read(const char* name, char* value, size_t size, uint64_t* sequence) const {
  char path[384], line[NBT_CDS_LINE_MAX + 1], text[NBT_CDS_VALUE_MAX + 1];
  if (!dir_[0] || !value || !size || !name_valid(name) ||
      snprintf(path, sizeof(path), "%s/nbt-bridge1-cds-%s", dir_, name) >=
          static_cast<int>(sizeof(path))) {
    return false;
  }
  const int fd = open(path, O_RDONLY | O_NONBLOCK);
  if (fd < 0) return false;
  ssize_t n;
  do {
    n = ::read(fd, line, sizeof(line));
  } while (n < 0 && errno == EINTR);
  close(fd);
  uint64_t seq = 0;
  if (n <= 0 || nbt_cds_value_parse(line, static_cast<size_t>(n), &seq, text) ||
      strlen(text) >= size) {
    return false;
  }
  memcpy(value, text, strlen(text) + 1);
  if (sequence) *sequence = seq;
  return true;
}

}  // namespace app
}  // namespace nbt
