// SPDX-License-Identifier: MPL-2.0
// nbt/app/diag.hpp's channel for app modules and guests: the runner has no
// diagnostics channel, so there is none. Same interface as src/diag_channel.cpp.
#include "nbt/app/diag.hpp"

namespace nbt {
namespace app {

bool Diag::init() {
  fd_ = -1;
  return false;
}

Diag::Status Diag::request(uint8_t, const uint8_t*, size_t, uint8_t*, size_t, size_t* size,
                           unsigned) {
  if (size) *size = 0;
  return kNoChannel;
}

}  // namespace app
}  // namespace nbt
