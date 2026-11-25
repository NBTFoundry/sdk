// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_DIAG_WIRE_H
#define NBT_DIAG_WIRE_H
/* Bridge capability `diag` (1.8): an application's diagnostic requests to the
 * car's control units and their answers, AF_UNIX SOCK_DGRAM on NBT_DIAG_FD
 * (10). One datagram is one request or one reply; the host answers each
 * request once, in order (docs/diag-bridge.md).
 *
 *   request: 0x01, ecu, id (u16 LE), timeout ms (u16 LE, 0: 2000), UDS bytes (1-1024)
 *   reply:   0x81, ecu, id (u16 LE), status, transport, UDS bytes (status OK only)
 */
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NBT_DIAG_WIRE_HEADER 6u
#define NBT_DIAG_WIRE_UDS_MAX 1024u
#define NBT_DIAG_WIRE_REPLY_MAX 4090u
#define NBT_DIAG_WIRE_MAX (NBT_DIAG_WIRE_HEADER + NBT_DIAG_WIRE_REPLY_MAX)
#define NBT_DIAG_WIRE_TIMEOUT_DEFAULT 2000u
#define NBT_DIAG_WIRE_TIMEOUT_MAX 10000u
/* Reply status. */
#define NBT_DIAG_OK 0u         /* the control unit's answer, positive or 7F negative */
#define NBT_DIAG_TIMEOUT 1u    /* the control unit did not answer in time */
#define NBT_DIAG_REFUSED 2u    /* not allowed by the application's signed policy */
#define NBT_DIAG_NO_ROUTE 3u   /* no gateway (or tunnel) reached */
#define NBT_DIAG_NOT_PARKED 4u /* a clear while the car is not known to stand still */
#define NBT_DIAG_BUSY 5u       /* another tester holds the gateway */
#define NBT_DIAG_MALFORMED 6u  /* the request was not a valid datagram */
/* Reply transport. */
#define NBT_DIAG_VIA_NONE 0u
#define NBT_DIAG_VIA_HSFZ 1u
#define NBT_DIAG_VIA_MOST 2u
typedef struct {
    uint8_t ecu;
    uint16_t id;
    uint16_t timeout_ms;
    const uint8_t *uds; /* points into the datagram */
    size_t size;
} nbt_diag_request;
typedef struct {
    uint8_t ecu;
    uint16_t id;
    uint8_t status;
    uint8_t transport;
    const uint8_t *uds; /* points into the datagram */
    size_t size;
} nbt_diag_reply;
/* The datagram's length, or -1 (bad sizes or buffer too small). */
int nbt_diag_request_encode(uint8_t ecu, uint16_t id, uint16_t timeout_ms, const uint8_t *uds,
                            size_t size, uint8_t *out, size_t capacity);
int nbt_diag_request_decode(const uint8_t *data, size_t size, nbt_diag_request *out);
int nbt_diag_reply_encode(uint8_t ecu, uint16_t id, uint8_t status, uint8_t transport,
                          const uint8_t *uds, size_t size, uint8_t *out, size_t capacity);
int nbt_diag_reply_decode(const uint8_t *data, size_t size, nbt_diag_reply *out);
#ifdef __cplusplus
}
#endif
#endif
