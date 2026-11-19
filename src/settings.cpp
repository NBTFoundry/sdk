// SPDX-License-Identifier: MPL-2.0
#include "nbt/app/settings.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" char** environ;

namespace nbt {
namespace app {

namespace {

const char kValuesHeader[] = "NBTVALUES1\n";
const size_t kValuesHeaderLen = sizeof(kValuesHeader) - 1;
const char kEnvPrefix[] = "NBT_SETTING_";
const size_t kEnvPrefixLen = sizeof(kEnvPrefix) - 1;

bool key_valid(const char* s, size_t n) {
  if (!n || n > kSettingKeyMax || s[0] < 'a' || s[0] > 'z') return false;
  for (size_t i = 0; i < n; ++i) {
    const char c = s[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  }
  return true;
}

bool value_valid(const char* s, size_t n) {
  if (!n || n > kSettingValueMax) return false;
  for (size_t i = 0; i < n; ++i) {
    const char c = s[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  }
  return true;
}

}  // namespace

bool setting_key_valid(const char* key) {
  return key && key_valid(key, strlen(key));
}

bool setting_value_valid(const char* value) {
  return value && value_valid(value, strlen(value));
}

Settings::Settings() : count_(0) {
  memset(items_, 0, sizeof(items_));
}

void Settings::clear() {
  memset(items_, 0, sizeof(items_));
  count_ = 0;
}

const char* Settings::get(const char* key) const {
  if (!key) return NULL;
  for (size_t i = 0; i < count_; ++i) {
    if (strcmp(items_[i].key, key) == 0) return items_[i].value;
  }
  return NULL;
}

const char* Settings::get(const char* key, const char* fallback) const {
  const char* value = get(key);
  return value ? value : fallback;
}

bool Settings::set(const char* key, const char* value) {
  if (!setting_key_valid(key) || !setting_value_valid(value)) return false;
  for (size_t i = 0; i < count_; ++i) {
    if (strcmp(items_[i].key, key) == 0) {
      strcpy(items_[i].value, value);
      return true;
    }
  }
  if (count_ >= kSettingsMax) return false;
  strcpy(items_[count_].key, key);
  strcpy(items_[count_].value, value);
  ++count_;
  return true;
}

void Settings::merge(const Settings& other) {
  for (size_t i = 0; i < other.count_; ++i) set(other.items_[i].key, other.items_[i].value);
}

bool Settings::equals(const Settings& other) const {
  if (count_ != other.count_) return false;
  for (size_t i = 0; i < count_; ++i) {
    const char* value = other.get(items_[i].key);
    if (!value || strcmp(value, items_[i].value) != 0) return false;
  }
  return true;
}

bool Settings::parse_values(const char* text, size_t size) {
  if (!text || size < kValuesHeaderLen || size >= kSettingsFileMax ||
      memcmp(text, kValuesHeader, kValuesHeaderLen) != 0 || memchr(text, '\0', size)) {
    return false;
  }
  Settings applied = *this;
  size_t pos = kValuesHeaderLen;
  while (pos < size) {
    const char* line = text + pos;
    const char* end = static_cast<const char*>(memchr(line, '\n', size - pos));
    if (!end) return false; /* unterminated line */
    const char* equals =
        static_cast<const char*>(memchr(line, '=', static_cast<size_t>(end - line)));
    if (!equals) return false;
    const size_t key_len = static_cast<size_t>(equals - line);
    const size_t value_len = static_cast<size_t>(end - equals - 1);
    if (!key_valid(line, key_len) || !value_valid(equals + 1, value_len)) return false;
    char key[kSettingKeyMax + 1];
    char value[kSettingValueMax + 1];
    memcpy(key, line, key_len);
    key[key_len] = '\0';
    memcpy(value, equals + 1, value_len);
    value[value_len] = '\0';
    if (!applied.set(key, value)) return false;
    pos = static_cast<size_t>(end - text) + 1;
  }
  *this = applied;
  return true;
}

size_t settings_load_env(Settings* out) {
  if (!out || !environ) return 0;
  size_t added = 0;
  for (char** p = environ; *p; ++p) {
    const char* entry = *p;
    if (strncmp(entry, kEnvPrefix, kEnvPrefixLen) != 0) continue;
    const char* name = entry + kEnvPrefixLen;
    const char* equals = strchr(name, '=');
    if (!equals) continue;
    const size_t key_len = static_cast<size_t>(equals - name);
    if (!key_len || key_len > kSettingKeyMax) continue;
    char key[kSettingKeyMax + 1];
    for (size_t i = 0; i < key_len; ++i) {
      const char c = name[i];
      key[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    key[key_len] = '\0';
    if (out->set(key, equals + 1)) ++added;
  }
  return added;
}

bool settings_read_file(const char* path, Settings* out, const char** reason) {
  const char* why = NULL;
  struct stat before, st;
  char text[kSettingsFileMax];
  size_t used = 0;
  bool eof = false;
  int fd = -1;
  if (!path || !path[0] || !out) {
    why = "no settings file";
  } else if (lstat(path, &before) != 0) {
    why = "settings file missing";
  } else if (!S_ISREG(before.st_mode)) {
    why = "settings file is not a regular file";
  } else {
    int flags = O_RDONLY | O_NONBLOCK;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    fd = open(path, flags);
    if (fd < 0) {
      why = "settings file cannot be opened";
    } else if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
               st.st_uid != geteuid() || st.st_nlink != 1 || (st.st_mode & 0077) != 0 ||
               st.st_dev != before.st_dev || st.st_ino != before.st_ino) {
      why = "unsafe settings file";
    } else {
      while (used < sizeof(text)) {
        const ssize_t n = read(fd, text + used, sizeof(text) - used);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) break;
        if (n == 0) {
          eof = true;
          break;
        }
        used += static_cast<size_t>(n);
      }
      if (!eof) why = "settings file unreadable or oversized";
    }
    if (fd >= 0 && close(fd) != 0 && !why) why = "settings file close failed";
  }
  if (!why) {
    Settings next = *out;
    if (next.parse_values(text, used)) {
      *out = next;
    } else {
      why = "malformed settings file";
    }
  }
  if (reason) *reason = why;
  return why == NULL;
}

}  // namespace app
}  // namespace nbt
