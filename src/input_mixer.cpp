#include "nbt/app/input_mixer.hpp"

#include <string.h>

namespace nbt {
namespace app {

InputMixer::InputMixer() : sink_(NULL) {
  memset(held_, 0, sizeof(held_));
}

void InputMixer::action(unsigned source, const InputAction& action) {
  if (source >= kMaxSources || !sink_) return;
  if (action.type != InputAction::kKeyDown && action.type != InputAction::kKeyUp) {
    sink_->on_action(action);
    return;
  }
  if (action.value < 0 || action.value >= kMaxKeys) return;
  bool before = false, after = false;
  for (unsigned s = 0; s < kMaxSources; ++s) before = before || held_[s][action.value];
  held_[source][action.value] = action.type == InputAction::kKeyDown;
  for (unsigned s = 0; s < kMaxSources; ++s) after = after || held_[s][action.value];
  if (before != after) sink_->on_action(action);
}

void InputMixer::release(unsigned source) {
  if (source >= kMaxSources) return;
  for (int key = 0; key < kMaxKeys; ++key) {
    if (held_[source][key]) {
      const InputAction up = {InputAction::kKeyUp, key};
      action(source, up);
    }
  }
}

void InputMixer::release_all() {
  for (unsigned s = 0; s < kMaxSources; ++s) release(s);
}

}  // namespace app
}  // namespace nbt
