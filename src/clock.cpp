// SPDX-License-Identifier: MPL-2.0
#include "nbt/app/clock.hpp"

#include <time.h>

namespace nbt {
namespace app {

uint64_t monotonic_ms() {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
  return static_cast<uint64_t>(ts.tv_sec) * 1000u + static_cast<uint64_t>(ts.tv_nsec) / 1000000u;
}

}  // namespace app
}  // namespace nbt
