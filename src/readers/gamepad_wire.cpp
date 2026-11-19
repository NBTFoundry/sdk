// SPDX-License-Identifier: MPL-2.0
#include "nbt/gamepad_wire.h"
#include <string.h>
namespace {
uint32_t get(const uint8_t *p, unsigned n) {
    uint32_t v = 0;
    for (unsigned i = 0; i < n; ++i) {
        v |= static_cast<uint32_t>(p[i]) << (8 * i);
    }
    return v;
}
void put(uint8_t *p, uint32_t v, unsigned n) {
    for (unsigned i = 0; i < n; ++i) {
        p[i] = static_cast<uint8_t>(v >> (8 * i));
    }
}
int16_t axis(uint32_t v) {
    return static_cast<int16_t>(v < 32768 ? static_cast<int>(v) : static_cast<int>(v) - 65536);
}
bool valid(const nbt_gamepad_wire_state &s) {
    return s.connected <= 1 && !(s.buttons & ~NBT_GAMEPAD_BUTTONS) &&
           (s.connected || !(s.buttons || s.lx || s.ly || s.rx || s.ry || s.lt || s.rt));
}
} // namespace
int nbt_gamepad_wire_encode(const nbt_gamepad_wire_state *s, uint8_t *p, size_t n) {
    if (!s || !p || n < NBT_GAMEPAD_WIRE_SIZE || !valid(*s)) {
        return -1;
    }
    memset(p, 0, NBT_GAMEPAD_WIRE_SIZE);
    p[0] = p[1] = 1;
    p[2] = s->connected;
    put(p + 4, s->sequence, 4);
    put(p + 8, s->buttons, 4);
    put(p + 12, static_cast<uint16_t>(s->lx), 2);
    put(p + 14, static_cast<uint16_t>(s->ly), 2);
    put(p + 16, static_cast<uint16_t>(s->rx), 2);
    put(p + 18, static_cast<uint16_t>(s->ry), 2);
    put(p + 20, s->lt, 2);
    put(p + 22, s->rt, 2);
    return NBT_GAMEPAD_WIRE_SIZE;
}
int nbt_gamepad_wire_decode(const uint8_t *p, size_t n, nbt_gamepad_wire_state *out) {
    if (!p || !out || n != NBT_GAMEPAD_WIRE_SIZE || p[0] != 1 || p[1] != 1 || p[3]) {
        return -1;
    }
    nbt_gamepad_wire_state s;
    memset(&s, 0, sizeof(s));
    s.connected = p[2];
    s.sequence = get(p + 4, 4);
    s.buttons = get(p + 8, 4);
    s.lx = axis(get(p + 12, 2));
    s.ly = axis(get(p + 14, 2));
    s.rx = axis(get(p + 16, 2));
    s.ry = axis(get(p + 18, 2));
    s.lt = static_cast<uint16_t>(get(p + 20, 2));
    s.rt = static_cast<uint16_t>(get(p + 22, 2));
    if (!valid(s)) {
        return -1;
    }
    *out = s;
    return 0;
}
int nbt_gamepad_axis(int v, unsigned deadzone) {
    if (v < -32768 || v > 32767 || deadzone >= 32767) {
        return 0;
    }
    const int magnitude = v < 0 ? -v : v;
    if (magnitude <= static_cast<int>(deadzone)) {
        return 0;
    }
    const int limit = v < 0 ? 32768 : 32767;
    const int scaled =
        (magnitude - static_cast<int>(deadzone)) * 32767 / (limit - static_cast<int>(deadzone));
    return v < 0 ? -scaled : scaled;
}
