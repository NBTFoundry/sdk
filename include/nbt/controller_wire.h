// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_CONTROLLER_WIRE_H
#define NBT_CONTROLLER_WIRE_H
/* Bridge capability `controller`: the iDrive controller and the favourite keys
 * as the manager decodes them, for the application on screen. AF_UNIX
 * SOCK_DGRAM on NBT_CONTROLLER_FD (8); exactly one 16-byte little-endian state
 * per datagram, sent when it changes. See docs/app-bridge.md. */
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NBT_CONTROLLER_WIRE_SIZE 16u
/* Keys held. Tilts are held too; a diagonal sets two of them. */
#define NBT_CONTROLLER_PUSH (1u << 0)
#define NBT_CONTROLLER_MENU (1u << 1)
#define NBT_CONTROLLER_BACK (1u << 2)
#define NBT_CONTROLLER_OPTION (1u << 3)
#define NBT_CONTROLLER_COM (1u << 4)
#define NBT_CONTROLLER_MEDIA (1u << 5)
#define NBT_CONTROLLER_NAV (1u << 6)
#define NBT_CONTROLLER_MAP (1u << 7)
#define NBT_CONTROLLER_TILT_UP (1u << 8)
#define NBT_CONTROLLER_TILT_DOWN (1u << 9)
#define NBT_CONTROLLER_TILT_LEFT (1u << 10)
#define NBT_CONTROLLER_TILT_RIGHT (1u << 11)
/* The centre console's favourite keys 1-8 (n 0-7). */
#define NBT_CONTROLLER_FAVOURITE(n) (1u << (16u + (unsigned)(n)))
#define NBT_CONTROLLER_KEYS 0x00ff0fffu
typedef struct {
    uint32_t sequence; /* +1 per state sent */
    uint32_t keys;     /* NBT_CONTROLLER_* held */
    int32_t rotation;  /* detents turned clockwise since the feed started; wraps */
    uint8_t connected; /* the manager's MOST link is up; otherwise keys are 0 */
} nbt_controller_wire_state;
int nbt_controller_wire_encode(const nbt_controller_wire_state *state, uint8_t *out, size_t size);
int nbt_controller_wire_decode(const uint8_t *data, size_t size, nbt_controller_wire_state *out);
#ifdef __cplusplus
}
#endif
#endif
