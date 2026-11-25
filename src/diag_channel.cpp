// SPDX-License-Identifier: MPL-2.0
// nbt/app/diag.hpp's channel: datagrams on NBT_DIAG_FD (bridge 1.8 `diag`).
// Modules and guests use module/sdk/diag_module.cpp.
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "nbt/app/clock.hpp"
#include "nbt/app/diag.hpp"
#include "nbt/diag_wire.h"

namespace nbt {
namespace app {

bool Diag::init() {
  const char* fd = getenv("NBT_DIAG_FD");
  fd_ = fd && !strcmp(fd, "10") ? 10 : -1;
  return fd_ >= 0;
}

Diag::Status Diag::request(uint8_t ecu, const uint8_t* uds, size_t uds_size, uint8_t* answer,
                           size_t capacity, size_t* size, unsigned timeout_ms) {
  uint8_t wire[NBT_DIAG_WIRE_MAX];
  if (size) *size = 0;
  if (fd_ < 0) return kNoChannel;
  const uint16_t id = next_id_++;
  const int n = nbt_diag_request_encode(ecu, id, static_cast<uint16_t>(timeout_ms), uds, uds_size,
                                        wire, sizeof(wire));
  if (n < 0) return kMalformed;
  if (send(fd_, wire, static_cast<size_t>(n), 0) != n) return kNoChannel;
  // The manager waits for the control unit, and for "response pending" up to
  // five times as long: a reply always comes, unless the manager is gone.
  const uint64_t deadline =
      monotonic_ms() + 6u * (timeout_ms ? timeout_ms : NBT_DIAG_WIRE_TIMEOUT_DEFAULT) + 5000u;
  for (;;) {
    const uint64_t now = monotonic_ms();
    if (now >= deadline) return kNoChannel;
    struct pollfd p = {fd_, POLLIN, 0};
    const int ready = poll(&p, 1, static_cast<int>(deadline - now));
    if (ready < 0 && errno == EINTR) continue;
    if (ready <= 0) return kNoChannel;
    const ssize_t got = recv(fd_, wire, sizeof(wire), 0);
    if (got <= 0) return kNoChannel;
    nbt_diag_reply reply;
    if (nbt_diag_reply_decode(wire, static_cast<size_t>(got), &reply) || reply.id != id) {
      continue;  // an answer to a request given up on
    }
    if (reply.status == NBT_DIAG_OK) {
      const size_t copy = reply.size < capacity ? reply.size : capacity;
      if (answer && copy) memcpy(answer, reply.uds, copy);
      if (size) *size = copy;
    }
    return static_cast<Status>(reply.status);
  }
}

}  // namespace app
}  // namespace nbt
