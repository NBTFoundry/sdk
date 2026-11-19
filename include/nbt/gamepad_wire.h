// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GAMEPAD_WIRE_H
#define NBT_GAMEPAD_WIRE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* AF_UNIX SOCK_DGRAM; exactly one 24-byte little-endian state per datagram. */
#define NBT_GAMEPAD_WIRE_SIZE 24u
#define NBT_GAMEPAD_SOUTH (1u << 0)
#define NBT_GAMEPAD_EAST (1u << 1)
#define NBT_GAMEPAD_WEST (1u << 2)
#define NBT_GAMEPAD_NORTH (1u << 3)
#define NBT_GAMEPAD_LB (1u << 4)
#define NBT_GAMEPAD_RB (1u << 5)
#define NBT_GAMEPAD_VIEW (1u << 6)
#define NBT_GAMEPAD_MENU (1u << 7)
#define NBT_GAMEPAD_LS (1u << 8)
#define NBT_GAMEPAD_RS (1u << 9)
#define NBT_GAMEPAD_GUIDE (1u << 10)
#define NBT_GAMEPAD_UP (1u << 11)
#define NBT_GAMEPAD_DOWN (1u << 12)
#define NBT_GAMEPAD_LEFT (1u << 13)
#define NBT_GAMEPAD_RIGHT (1u << 14)
#define NBT_GAMEPAD_BUTTONS 0x7fffu
typedef struct {
    uint32_t sequence, buttons;
    int16_t lx, ly, rx, ry; /* positive right/up, -32768..32767 */
    uint16_t lt, rt;        /* 0..65535 */
    uint8_t connected;
} nbt_gamepad_wire_state;
int nbt_gamepad_wire_encode(const nbt_gamepad_wire_state *state, uint8_t *out, size_t size);
int nbt_gamepad_wire_decode(const uint8_t *data, size_t size, nbt_gamepad_wire_state *out);
/* Rescaled signed deadzone; uses widened arithmetic at -32768. */
int nbt_gamepad_axis(int value, unsigned deadzone);
#ifdef __cplusplus
}
#endif
#endif
