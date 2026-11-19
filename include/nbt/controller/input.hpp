#ifndef NBT_CONTROLLER_INPUT_HPP
#define NBT_CONTROLLER_INPUT_HPP
#include "nbt/controller_wire.h"
namespace nbt {
namespace controller {
/* What changed since the previous state: keys (NBT_CONTROLLER_*) pressed and
 * released, and detents turned, clockwise positive. */
struct Event {
    uint32_t pressed, released;
    int32_t rotation;
};
/* Application-side reader for capability `controller` (portable C++11, no
 * allocation). Keys count only once all were seen up, so the push that
 * started the application is never a press; a lost link releases them. */
class Input {
public:
    Input();
    typedef void (*Delivery)(void *context, const nbt_controller_wire_state &state,
                             const Event &event);
    /* One datagram; false when malformed or out of sequence (ignored). */
    bool feed(const uint8_t *data, size_t size, Event *event);
    /* Reads what is pending on the non-blocking descriptor and delivers each
     * change. A failed socket releases everything once. */
    void poll(int fd, Delivery deliver, void *context);
    /* Forgets the state; the event releases the keys that were held. */
    Event reset();
    const nbt_controller_wire_state &state() const {
        return state_;
    }

private:
    nbt_controller_wire_state state_;
    uint32_t sequence_;
    bool sequenced_, armed_, positioned_, failed_;
};
} // namespace controller
} // namespace nbt
#endif
