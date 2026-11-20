// SPDX-License-Identifier: MPL-2.0
// nbt/app/child.hpp for app modules and guests: they cannot start processes
// (docs/native-apps.md), so every call reports that. Same interface as
// src/child.cpp; child_environment() is src/child_env.cpp's.
#include <errno.h>

#include "nbt/app/child.hpp"

namespace nbt {
namespace app {

int spawn_child(const char*, char* const[], char* const[], const int*, unsigned, bool, pid_t*) {
  return ENOSYS;
}

bool child_exited(pid_t, int* status) {
  if (status) *status = -1;
  return true;
}

int stop_child(pid_t, bool, unsigned, unsigned) {
  return -1;
}

bool own_executable(char* out, size_t size) {
  if (out && size) out[0] = '\0';
  return false;
}

int extract_own_payload(const char*, const char*, char* out, size_t size, const char** why) {
  if (out && size) out[0] = '\0';
  if (why) *why = "a module cannot start programs";
  return -1;
}

}  // namespace app
}  // namespace nbt
