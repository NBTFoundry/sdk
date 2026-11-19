// SPDX-License-Identifier: MPL-2.0
#include "nbt/controller_wire.h"
#include <string.h>
namespace {
const uint8_t kMagic = 0x43; // 'C'
const uint8_t kVersion = 1;
uint32_t get(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
           static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}
void put(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) {
        p[i] = static_cast<uint8_t>(v >> (8 * i));
    }
}
bool valid(const nbt_controller_wire_state &s) {
    return s.connected <= 1 && !(s.keys & ~NBT_CONTROLLER_KEYS) && (s.connected || !s.keys);
}
} // namespace
int nbt_controller_wire_encode(const nbt_controller_wire_state *s, uint8_t *p, size_t n) {
    if (!s || !p || n < NBT_CONTROLLER_WIRE_SIZE || !valid(*s)) {
        return -1;
    }
    memset(p, 0, NBT_CONTROLLER_WIRE_SIZE);
    p[0] = kMagic;
    p[1] = kVersion;
    p[2] = s->connected;
    put(p + 4, s->sequence);
    put(p + 8, s->keys);
    put(p + 12, static_cast<uint32_t>(s->rotation));
    return NBT_CONTROLLER_WIRE_SIZE;
}
int nbt_controller_wire_decode(const uint8_t *p, size_t n, nbt_controller_wire_state *out) {
    if (!p || !out || n != NBT_CONTROLLER_WIRE_SIZE || p[0] != kMagic || p[1] != kVersion || p[3]) {
        return -1;
    }
    nbt_controller_wire_state s;
    memset(&s, 0, sizeof(s));
    s.connected = p[2];
    s.sequence = get(p + 4);
    s.keys = get(p + 8);
    const uint32_t r = get(p + 12);
    // Two's complement without implementation-defined conversion.
    s.rotation = r < 0x80000000u ? static_cast<int32_t>(r) : -static_cast<int32_t>(~r) - 1;
    if (!valid(s)) {
        return -1;
    }
    *out = s;
    return 0;
}
