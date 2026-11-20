// SPDX-License-Identifier: MPL-2.0
// child_environment() (nbt/app/child.hpp): no system calls, so modules have it too.
#include <string.h>

#include "nbt/app/child.hpp"

namespace nbt {
namespace app {

namespace {

/* The variable's name, up to and including '='. */
size_t name_length(const char* entry) {
  const char* eq = strchr(entry, '=');
  return eq ? static_cast<size_t>(eq - entry) + 1 : strlen(entry);
}

bool set_by(char* const* first, const char* entry) {
  const size_t n = name_length(entry);
  for (char* const* p = first; p && *p; ++p) {
    if (name_length(*p) == n && strncmp(*p, entry, n) == 0) return true;
  }
  return false;
}

}  // namespace

int child_environment(char* const* src, char* const* first, char* out[], size_t max_out,
                      bool keep_preload) {
  if (!out || max_out == 0) return -1;
  size_t n = 0;
  for (char* const* p = first; p && *p; ++p) {
    if (n + 1 >= max_out) return -1;
    out[n++] = *p;
  }
  for (char* const* p = src; p && *p; ++p) {
    if ((!keep_preload && strncmp(*p, "LD_PRELOAD=", 11) == 0) || set_by(first, *p)) continue;
    if (n + 1 >= max_out) return -1;
    out[n++] = *p;
  }
  out[n] = NULL;
  return static_cast<int>(n);
}

}  // namespace app
}  // namespace nbt
