// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GAMEPAD_INPUT_HPP
#define NBT_GAMEPAD_INPUT_HPP
#include "nbt/gamepad_wire.h"
namespace nbt {
namespace gamepad {
/* Portable application-side reader, also used by the manager GUI. */
class Input {
public:
    Input();
    bool feed(const uint8_t *data, size_t size, uint64_t now);
    typedef void (*Delivery)(void *, const nbt_gamepad_wire_state &);
    void poll(int fd, uint64_t now, Delivery deliver = NULL, void *context = NULL);
    void reset();
    const nbt_gamepad_wire_state &state() const {
        return state_;
    }

private:
    nbt_gamepad_wire_state state_;
    uint64_t received_;
    uint32_t sequence_;
    bool armed_, sequenced_;
};
struct NavigationEvent {
    uint32_t pressed, released, repeated;
};
class Navigation {
public:
    Navigation();
    NavigationEvent update(const nbt_gamepad_wire_state &state, uint64_t now);
    void reset();

private:
    uint32_t buttons_, direction_;
    uint64_t repeatAt_;
};
} // namespace gamepad
} // namespace nbt
#endif
