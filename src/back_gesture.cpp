// SPDX-License-Identifier: MPL-2.0
#include "nbt/app/back_gesture.hpp"

namespace nbt {
namespace app {

void back_gesture_defaults(BackGestureConfig* config) {
  if (!config) return;
  config->tap_max_ms = 400u;
  config->double_tap_ms = 350u;
  config->hold_ms = 600u;
  config->long_hold_ms = 1400u;
}

BackGesture::BackGesture()
    : down_(false), hold_fired_(false), long_hold_fired_(false), down_ms_(0), last_tap_ms_(0) {
  back_gesture_defaults(&config_);
}

void BackGesture::configure(const BackGestureConfig& config) {
  config_ = config;
}

void BackGesture::reset() {
  down_ = false;
  hold_fired_ = false;
  long_hold_fired_ = false;
  down_ms_ = 0;
  last_tap_ms_ = 0;
}

BackGesture::Event BackGesture::update(bool down, uint64_t now_ms, bool allow_double_tap) {
  if (down && !down_) {
    down_ = true;
    down_ms_ = now_ms;
    hold_fired_ = false;
    long_hold_fired_ = false;
    return kNone;
  }
  if (down || !down_) return kNone;

  const uint64_t held = (now_ms >= down_ms_) ? (now_ms - down_ms_) : 0;
  down_ = false;
  if (hold_fired_ || held >= config_.tap_max_ms) return kNone;
  if (!allow_double_tap) return kTap;
  if (last_tap_ms_ > 0 && now_ms >= last_tap_ms_ &&
      (now_ms - last_tap_ms_) < config_.double_tap_ms) {
    last_tap_ms_ = 0;
    return kDoubleTap;
  }
  last_tap_ms_ = now_ms;
  return kTap;
}

BackGesture::Event BackGesture::poll(uint64_t now_ms) {
  if (!down_ || now_ms < down_ms_) return kNone;
  const uint64_t held = now_ms - down_ms_;
  if (config_.long_hold_ms && held >= config_.long_hold_ms && !long_hold_fired_) {
    long_hold_fired_ = true;
    return kLongHold;
  }
  if (held >= config_.hold_ms && !hold_fired_) {
    hold_fired_ = true;
    return kHold;
  }
  return kNone;
}

}  // namespace app
}  // namespace nbt
