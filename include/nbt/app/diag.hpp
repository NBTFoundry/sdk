// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_APP_DIAG_HPP
#define NBT_APP_DIAG_HPP

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace app {

/* Capability `diag` (bridge 1.8): UDS requests to the car's control units,
 * sent by NBT Manager as the car's one tester (its docs/diag-bridge.md). The
 * signed package lists the control units and what may be asked of each
 * (`bridge-diag`, e.g. `12:read+codes+clear` for the engine); anything else is
 * refused. Calls block until the answer: make them off the drawing thread. */
class Diag {
 public:
  enum Status {
    kOk = 0,         // the control unit's answer, positive or 7F negative
    kTimeout = 1,    // no answer in time
    kRefused = 2,    // not allowed by the signed policy
    kNoRoute = 3,    // no gateway reached
    kNotParked = 4,  // a clear while the car is not known to stand still
    kBusy = 5,       // another tester held the gateway
    kMalformed = 6,  // the request was not valid
    kNoChannel = 7,  // not granted, or the manager went away
  };
  Diag();
  /* From NBT_DIAG_FD; false when the capability was not granted. */
  bool init();
  bool available() const { return fd_ >= 0; }
  /* Sends `uds` to control unit `ecu` and waits up to `timeout_ms` (1-10000,
   * 0: 2000) plus a margin for its answer into `answer` (`*size` bytes). */
  Status request(uint8_t ecu, const uint8_t* uds, size_t uds_size, uint8_t* answer, size_t capacity,
                 size_t* size, unsigned timeout_ms = 0);
  /* 22 <did>: the data after the identifier in `out` (`*size` bytes); a
   * negative answer gives kOk with *size 0 and `*nrc` set (may be NULL). */
  Status read(uint8_t ecu, uint16_t did, uint8_t* out, size_t capacity, size_t* size,
              uint8_t* nrc = NULL);
  /* 19 02 <mask>: the stored fault codes, 4 bytes each (3-byte code, status). */
  Status codes(uint8_t ecu, uint8_t mask, uint8_t* out, size_t capacity, size_t* size,
               uint8_t* nrc = NULL);
  /* 14 FF FF FF: clears every fault code; only parked. *nrc set when refused. */
  Status clear(uint8_t ecu, uint8_t* nrc = NULL);

 private:
  int fd_;
  uint16_t next_id_;
};

}  // namespace app
}  // namespace nbt

#endif
