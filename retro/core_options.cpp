// SPDX-License-Identifier: MPL-2.0
#include "nbt/retro/core_options.hpp"

#include <stdio.h>
#include <string.h>

#include "libretro.h" /* third_party/libretro-common */

namespace nbt {
namespace retro {

namespace {

void copy(char* out, size_t size, const char* text, size_t n) {
  if (n >= size) n = size - 1;
  memcpy(out, text, n);
  out[n] = '\0';
}

}  // namespace

CoreOptions::CoreOptions() : count_(0), maps_(NULL), map_count_(0), updated_(false) {}

void CoreOptions::set_maps(const VariableMap* maps, size_t count) {
  maps_ = maps;
  map_count_ = maps ? count : 0;
}

bool CoreOptions::listed(const Entry& e, const char* value) const {
  const size_t n = strlen(value);
  for (const char* p = e.options; *p;) {
    const char* bar = strchr(p, '|');
    const size_t len = bar ? static_cast<size_t>(bar - p) : strlen(p);
    if (len == n && !memcmp(p, value, n)) return true;
    if (!bar) break;
    p = bar + 1;
  }
  return false;
}

/* The mapped value when the setting has one the core lists, else the default. */
void CoreOptions::choose(Entry& e) const {
  const char* bar = strchr(e.options, '|');
  copy(e.value, sizeof(e.value), e.options,
       bar ? static_cast<size_t>(bar - e.options) : strlen(e.options));
  for (size_t m = 0; m < map_count_; ++m) {
    if (!maps_[m].variable || strcmp(maps_[m].variable, e.key) || !maps_[m].values) continue;
    const char* setting = settings_.get(maps_[m].setting);
    for (size_t i = 0; setting && maps_[m].values[i] && maps_[m].values[i + 1]; i += 2) {
      if (!strcmp(maps_[m].values[i], setting) && listed(e, maps_[m].values[i + 1])) {
        copy(e.value, sizeof(e.value), maps_[m].values[i + 1], strlen(maps_[m].values[i + 1]));
      }
    }
  }
}

void CoreOptions::declare(const ::retro_variable* variables) {
  count_ = 0;
  for (size_t i = 0; variables && variables[i].key && count_ < kMaxVariables; ++i) {
    const char* key = variables[i].key;
    const char* value = variables[i].value;
    const char* semicolon = value ? strchr(value, ';') : NULL;
    if (strlen(key) > kKeyMax || !semicolon) continue;
    Entry& e = entries_[count_];
    copy(e.key, sizeof(e.key), key, strlen(key));
    const char* options = semicolon + 1;
    while (*options == ' ') ++options;
    copy(e.options, sizeof(e.options), options, strlen(options));
    if (!e.options[0]) continue;
    choose(e);
    ++count_;
  }
  updated_ = true;
}

void CoreOptions::apply(const app::Settings& settings) {
  settings_ = settings;
  for (size_t i = 0; i < count_; ++i) {
    char before[kValueMax + 1];
    memcpy(before, entries_[i].value, sizeof(before));
    choose(entries_[i]);
    if (strcmp(before, entries_[i].value)) updated_ = true;
  }
}

const char* CoreOptions::get(const char* key) const {
  for (size_t i = 0; key && i < count_; ++i) {
    if (!strcmp(entries_[i].key, key)) return entries_[i].value;
  }
  return NULL;
}

bool CoreOptions::take_update() {
  const bool updated = updated_;
  updated_ = false;
  return updated;
}

}  // namespace retro
}  // namespace nbt
