#include "nbt/gamepad/input.hpp"
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
namespace nbt {
namespace gamepad {
Input::Input() : state_(), received_(0), sequence_(0), armed_(false), sequenced_(false) {
}
void Input::reset() {
    memset(&state_, 0, sizeof(state_));
    received_ = 0;
    armed_ = sequenced_ = false;
}
bool Input::feed(const uint8_t *data, size_t size, uint64_t now) {
    nbt_gamepad_wire_state next;
    if (nbt_gamepad_wire_decode(data, size, &next)) {
        reset();
        return false;
    }
    const uint32_t delta = next.sequence - sequence_;
    if (sequenced_ && (!delta || delta >= 0x80000000u)) {
        return false;
    }
    sequenced_ = true;
    sequence_ = next.sequence;
    received_ = now;
    if (!next.connected) {
        armed_ = false;
    }
    const bool neutral = !next.buttons && !nbt_gamepad_axis(next.lx, 6553) &&
                         !nbt_gamepad_axis(next.ly, 6553) && !nbt_gamepad_axis(next.rx, 6553) &&
                         !nbt_gamepad_axis(next.ry, 6553) && next.lt < 13107 && next.rt < 13107;
    if (next.connected && neutral) {
        armed_ = true;
    }
    state_ = next;
    if (!armed_) {
        const uint8_t connected = state_.connected;
        memset(&state_, 0, sizeof(state_));
        state_.connected = connected;
    }
    return true;
}
void Input::poll(int fd, uint64_t now, Delivery deliver, void *context) {
    if (fd < 0) {
        reset();
        return;
    }
    uint8_t bytes[NBT_GAMEPAD_WIRE_SIZE + 1];
    for (unsigned i = 0; i < 64; ++i) {
        const ssize_t n = recv(fd, bytes, sizeof(bytes), MSG_DONTWAIT);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                reset();
            }
            break;
        }
        const bool wasConnected = state_.connected != 0;
        const bool accepted = feed(bytes, static_cast<size_t>(n), now);
        if (deliver && (accepted || (wasConnected && !state_.connected))) {
            deliver(context, state_);
        }
    }
    if (now < received_ || now - received_ > 500) {
        const bool wasConnected = state_.connected != 0;
        reset();
        if (deliver && wasConnected) {
            deliver(context, state_);
        }
    }
}
Navigation::Navigation() : buttons_(0), direction_(0), repeatAt_(0) {
}
void Navigation::reset() {
    buttons_ = direction_ = 0;
    repeatAt_ = 0;
}
NavigationEvent Navigation::update(const nbt_gamepad_wire_state &s, uint64_t now) {
    uint32_t buttons = s.connected ? s.buttons : 0;
    if (s.connected) {
        if (s.lx < -6553) {
            buttons |= NBT_GAMEPAD_LEFT;
        }
        if (s.lx > 6553) {
            buttons |= NBT_GAMEPAD_RIGHT;
        }
        if (s.ly < -6553) {
            buttons |= NBT_GAMEPAD_DOWN;
        }
        if (s.ly > 6553) {
            buttons |= NBT_GAMEPAD_UP;
        }
    }
    NavigationEvent e = {buttons & ~buttons_, buttons_ & ~buttons, 0};
    const uint32_t dirs =
        buttons & (NBT_GAMEPAD_UP | NBT_GAMEPAD_DOWN | NBT_GAMEPAD_LEFT | NBT_GAMEPAD_RIGHT);
    if (dirs != direction_) {
        direction_ = dirs;
        repeatAt_ = now + 400;
    } else if (dirs && now >= repeatAt_) {
        e.repeated = dirs;
        repeatAt_ = now + 100;
    }
    buttons_ = buttons;
    return e;
}
} // namespace gamepad
} // namespace nbt
