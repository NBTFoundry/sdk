// SPDX-License-Identifier: MPL-2.0
#include "nbt/app/diag.hpp"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "nbt/app/clock.hpp"
#include "nbt/diag_wire.h"

namespace nbt {
namespace app {

Diag::Diag() : fd_(-1), next_id_(1) {}

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
