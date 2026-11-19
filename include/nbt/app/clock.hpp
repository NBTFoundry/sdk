// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_APP_CLOCK_HPP
#define NBT_APP_CLOCK_HPP

#include <stdint.h>

namespace nbt {
namespace app {

/* Milliseconds of CLOCK_MONOTONIC; 0 if the clock cannot be read. */
uint64_t monotonic_ms();

}  // namespace app
}  // namespace nbt

#endif /* NBT_APP_CLOCK_HPP */
