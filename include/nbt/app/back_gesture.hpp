#ifndef NBT_APP_BACK_GESTURE_HPP
#define NBT_APP_BACK_GESTURE_HPP

#include <stdint.h>

namespace nbt {
namespace app {

/* Timings of the BACK key's gestures, in milliseconds. */
struct BackGestureConfig {
  uint32_t tap_max_ms;    /* a release before this is a tap (400) */
  uint32_t double_tap_ms; /* a second tap within this of the first is a double tap (350) */
  uint32_t hold_ms;       /* held this long: a hold (600) */
  uint32_t long_hold_ms;  /* held this long: a long hold (1400; 0 turns it off) */
};

void back_gesture_defaults(BackGestureConfig* config);

/* Turns one key's level (down or up) into tap, double tap, hold and long hold.
 * The defaults leave a long hold room before NBT Manager's own 2000 ms BACK
 * escape, so an application can exit cleanly first. */
class BackGesture {
 public:
  enum Event { kNone = 0, kTap, kDoubleTap, kHold, kLongHold };

  BackGesture();
  void configure(const BackGestureConfig& config);
  void reset();

  /* The key's level at `now_ms`. Taps are reported on release, and only when
   * no hold fired. With `allow_double_tap` false a release is always a tap and
   * does not start a double tap. */
  Event update(bool down, uint64_t now_ms, bool allow_double_tap = true);

  /* Call regularly while the key may be down: a long hold, else a hold, each
   * reported once per press. */
  Event poll(uint64_t now_ms);

  bool held() const { return down_; }

 private:
  BackGestureConfig config_;
  bool down_;
  bool hold_fired_;
  bool long_hold_fired_;
  uint64_t down_ms_;
  uint64_t last_tap_ms_;
};

}  // namespace app
}  // namespace nbt

#endif /* NBT_APP_BACK_GESTURE_HPP */
