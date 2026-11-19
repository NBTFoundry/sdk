#include "nbt/controller/input.hpp"
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
namespace nbt {
namespace controller {
namespace {
int32_t difference(int32_t to, int32_t from) {
    const uint32_t d = static_cast<uint32_t>(to) - static_cast<uint32_t>(from);
    return d < 0x80000000u ? static_cast<int32_t>(d) : -static_cast<int32_t>(~d) - 1;
}
} // namespace
Input::Input()
    : state_(), sequence_(0), sequenced_(false), armed_(false), positioned_(false), failed_(false) {
}
Event Input::reset() {
    const Event e = {0, state_.keys, 0};
    memset(&state_, 0, sizeof(state_));
    sequence_ = 0;
    sequenced_ = armed_ = positioned_ = false;
    return e;
}
bool Input::feed(const uint8_t *data, size_t size, Event *event) {
    nbt_controller_wire_state next;
    if (!event || nbt_controller_wire_decode(data, size, &next)) {
        return false;
    }
    const uint32_t delta = next.sequence - sequence_;
    if (sequenced_ && (!delta || delta >= 0x80000000u)) {
        return false;
    }
    sequenced_ = true;
    sequence_ = next.sequence;
    if (!next.connected) {
        armed_ = false;
    } else if (!next.keys) {
        armed_ = true;
    }
    const uint32_t keys = armed_ ? next.keys : 0;
    event->pressed = keys & ~state_.keys;
    event->released = state_.keys & ~keys;
    event->rotation =
        next.connected && positioned_ ? difference(next.rotation, state_.rotation) : 0;
    positioned_ = next.connected != 0;
    state_ = next;
    state_.keys = keys;
    return true;
}
void Input::poll(int fd, Delivery deliver, void *context) {
    if (fd < 0 || failed_) {
        return;
    }
    uint8_t bytes[NBT_CONTROLLER_WIRE_SIZE + 1];
    for (unsigned i = 0; i < 64; ++i) {
        const ssize_t n = recv(fd, bytes, sizeof(bytes), MSG_DONTWAIT);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        }
        if (n <= 0) {
            failed_ = true;
            const Event e = reset();
            if (deliver && e.released) {
                deliver(context, state_, e);
            }
            return;
        }
        Event e;
        if (feed(bytes, static_cast<size_t>(n), &e) && deliver &&
            (e.pressed || e.released || e.rotation)) {
            deliver(context, state_, e);
        }
    }
}
} // namespace controller
} // namespace nbt
