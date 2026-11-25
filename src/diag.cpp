// SPDX-License-Identifier: MPL-2.0
// The requests; the channel (init, request) is src/diag_channel.cpp, or
// module/sdk/diag_module.cpp in modules and guests.
#include "nbt/app/diag.hpp"

#include <string.h>

#include "nbt/diag_wire.h"

namespace nbt {
namespace app {

Diag::Diag() : fd_(-1), next_id_(1) {}

namespace {

/* The answer after its positive service id and `skip` echoed bytes, or a
 * negative answer's code. */
Diag::Status unwrap(Diag::Status status, const uint8_t* answer, size_t got, uint8_t service,
                    size_t skip, uint8_t* out, size_t capacity, size_t* size, uint8_t* nrc) {
  if (size) *size = 0;
  if (nrc) *nrc = 0;
  if (status != Diag::kOk) return status;
  if (got >= 3 && answer[0] == 0x7F) {
    if (nrc) *nrc = answer[2];
    return Diag::kOk;
  }
  if (got < 1 + skip || answer[0] != (service | 0x40)) return Diag::kMalformed;
  const size_t n = got - 1 - skip < capacity ? got - 1 - skip : capacity;
  if (out && n) memcpy(out, answer + 1 + skip, n);
  if (size) *size = n;
  return Diag::kOk;
}

}  // namespace

Diag::Status Diag::read(uint8_t ecu, uint16_t did, uint8_t* out, size_t capacity, size_t* size,
                        uint8_t* nrc) {
  const uint8_t uds[3] = {0x22, static_cast<uint8_t>(did >> 8), static_cast<uint8_t>(did)};
  uint8_t answer[NBT_DIAG_WIRE_REPLY_MAX];
  size_t got = 0;
  const Status status = request(ecu, uds, sizeof(uds), answer, sizeof(answer), &got);
  return unwrap(status, answer, got, 0x22, 2, out, capacity, size, nrc);
}

Diag::Status Diag::codes(uint8_t ecu, uint8_t mask, uint8_t* out, size_t capacity, size_t* size,
                         uint8_t* nrc) {
  const uint8_t uds[3] = {0x19, 0x02, mask};
  uint8_t answer[NBT_DIAG_WIRE_REPLY_MAX];
  size_t got = 0;
  const Status status = request(ecu, uds, sizeof(uds), answer, sizeof(answer), &got, 5000);
  // 59 02 <availability mask>, then the codes.
  return unwrap(status, answer, got, 0x19, 2, out, capacity, size, nrc);
}

Diag::Status Diag::clear(uint8_t ecu, uint8_t* nrc) {
  const uint8_t uds[4] = {0x14, 0xFF, 0xFF, 0xFF};
  uint8_t answer[16];
  size_t got = 0;
  const Status status = request(ecu, uds, sizeof(uds), answer, sizeof(answer), &got, 5000);
  return unwrap(status, answer, got, 0x14, 0, NULL, 0, NULL, nrc);
}

}  // namespace app
}  // namespace nbt
