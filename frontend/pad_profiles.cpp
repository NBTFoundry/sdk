// SPDX-License-Identifier: MPL-2.0
#include "nbt/frontend/pad_profiles.hpp"

#include <string.h>

namespace nbt {
namespace frontend {

namespace {

/* Stick and trigger thresholds, with hysteresis so a control resting near
 * one does not chatter. */
const int kStickPress = 16384, kStickRelease = 12000;     /* of 32767 */
const int kTriggerPress = 16384, kTriggerRelease = 13107; /* of 65535: 25% / 20% */
const uint32_t kTapMs = 120;                              /* how long a BACK tap holds its action */

void set_bit(uint32_t (&bits)[16], int action) {
  if (action >= 0 && action <= kMaxAction) {
    bits[action >> 5] |= 1u << (action & 31);
  }
}

bool has_bit(const uint32_t (&bits)[16], int action) {
  return action >= 0 && action <= kMaxAction && (bits[action >> 5] >> (action & 31)) & 1u;
}

/* A stick direction past its threshold, or still held above the lower one. */
bool past(int value, bool held) {
  return held ? value > kStickRelease : value > kStickPress;
}

}  // namespace

PadMapper::PadMapper(const Profiles& profiles)
    : profiles_(profiles),
      family_(-1),
      pad_changed_(false),
      pad_buttons_(0),
      left_trigger_(false),
      right_trigger_(false),
      controller_keys_(0),
      tap_until_(0),
      now_ms_(0),
      pending_(kCommandNone) {
  memset(held_, 0, sizeof(held_));
  pulse_until_[0] = pulse_until_[1] = 0;
  app::BackGestureConfig config;
  app::back_gesture_defaults(&config);
  back_.configure(config);
}

const PadProfile* PadMapper::pad_profile() const {
  const PadProfile* p = family_ >= 0 && family_ <= static_cast<int>(NBT_GAMEPAD_FAMILY_MAX)
                            ? profiles_.pad[family_]
                            : NULL;
  return p ? p : profiles_.pad[0];
}

void PadMapper::set_source(Source source, const uint32_t (&next)[16]) {
  for (int word = 0; word < 16; ++word) {
    const uint32_t changed = held_[source][word] ^ next[word];
    for (int bit = 0; changed >> bit && bit < 32; ++bit) {
      if (!((changed >> bit) & 1u)) continue;
      app::InputAction a;
      a.type = (next[word] >> bit) & 1u ? app::InputAction::kKeyDown : app::InputAction::kKeyUp;
      a.value = word * 32 + bit;
      mixer_.action(source, a);
    }
    held_[source][word] = next[word];
  }
}

void PadMapper::hold(Source source, int action, bool on) {
  if (action < 0 || action > kMaxAction) return;
  uint32_t next[16];
  memcpy(next, held_[source], sizeof(next));
  if (on) {
    next[action >> 5] |= 1u << (action & 31);
  } else {
    next[action >> 5] &= ~(1u << (action & 31));
  }
  set_source(source, next);
}

void PadMapper::command(Command c) {
  if (c > pending_) pending_ = c; /* quitting outranks the menu */
}

void PadMapper::gamepad(const nbt_gamepad_wire_state& state, uint64_t now_ms) {
  now_ms_ = now_ms;
  const int family = state.connected ? state.family : -1;
  uint32_t next[16] = {0};
  if (family != family_) {
    family_ = family;
    pad_changed_ = true;
    set_source(kPad, next); /* nothing held under the previous profile stays */
    pad_buttons_ = 0;
    left_trigger_ = right_trigger_ = false;
  }
  const PadProfile* p = pad_profile();
  if (!state.connected || !p) {
    set_source(kPad, next);
    pad_buttons_ = 0;
    return;
  }
  for (size_t i = 0; i < p->button_count; ++i) {
    if (state.buttons & p->buttons[i].control) set_bit(next, p->buttons[i].action);
  }
  const uint32_t (&now)[16] = held_[kPad];
  if (past(state.ly, has_bit(now, p->stick_up))) set_bit(next, p->stick_up);
  if (past(-state.ly, has_bit(now, p->stick_down))) set_bit(next, p->stick_down);
  if (past(-state.lx, has_bit(now, p->stick_left))) set_bit(next, p->stick_left);
  if (past(state.lx, has_bit(now, p->stick_right))) set_bit(next, p->stick_right);
  left_trigger_ = left_trigger_ ? state.lt > kTriggerRelease : state.lt > kTriggerPress;
  right_trigger_ = right_trigger_ ? state.rt > kTriggerRelease : state.rt > kTriggerPress;
  if (left_trigger_) set_bit(next, p->left_trigger);
  if (right_trigger_) set_bit(next, p->right_trigger);
  const uint32_t pressed = state.buttons & ~pad_buttons_;
  if ((p->menu_button && (pressed & p->menu_button)) ||
      (p->menu_buttons && (state.buttons & p->menu_buttons) == p->menu_buttons &&
       (pressed & p->menu_buttons))) {
    command(kCommandMenu);
  }
  pad_buttons_ = state.buttons;
  set_source(kPad, next);
}

void PadMapper::controller(const nbt_controller_wire_state& state, const controller::Event& event,
                           uint64_t now_ms) {
  now_ms_ = now_ms;
  const ControllerProfile* p = profiles_.controller;
  if (!p || !state.connected) {
    uint32_t none[16] = {0};
    set_source(kController, none);
    controller_keys_ = 0;
    pulse_until_[0] = pulse_until_[1] = tap_until_ = 0;
    back_.reset();
    return;
  }
  if (event.rotation) {
    const int detents = event.rotation > 0 ? event.rotation : -event.rotation;
    uint64_t& until = pulse_until_[event.rotation > 0 ? 1 : 0];
    const uint64_t from = until > now_ms ? until : now_ms;
    until = from +
            static_cast<uint64_t>(p->pulse_ms) * static_cast<uint64_t>(detents > 8 ? 8 : detents);
  }
  if (event.pressed & p->menu_keys) command(kCommandMenu);
  if (back_.update((state.keys & NBT_CONTROLLER_BACK) != 0, now_ms, false) ==
      app::BackGesture::kTap) {
    tap_until_ = now_ms + kTapMs;
  }
  controller_keys_ = state.keys;
  refresh(now_ms);
}

void PadMapper::host_key(int key, bool down) {
  const KeyboardProfile* p = profiles_.keyboard;
  if (!p) return;
  if (down && key == p->menu_key) command(kCommandMenu);
  for (size_t i = 0; i < p->key_count; ++i) {
    if (static_cast<int>(p->keys[i].control) == key) hold(kKeyboard, p->keys[i].action, down);
  }
}

Command PadMapper::poll(uint64_t now_ms) {
  refresh(now_ms);
  const Command c = pending_;
  pending_ = kCommandNone;
  return c;
}

void PadMapper::refresh(uint64_t now_ms) {
  now_ms_ = now_ms;
  const ControllerProfile* p = profiles_.controller;
  if (p) {
    const app::BackGesture::Event e = back_.poll(now_ms);
    if (e == app::BackGesture::kHold) command(kCommandMenu);
    if (e == app::BackGesture::kLongHold) command(kCommandQuit);
    uint32_t next[16] = {0};
    for (size_t i = 0; i < p->key_count; ++i) {
      if (controller_keys_ & p->keys[i].control) set_bit(next, p->keys[i].action);
    }
    if (pulse_until_[0] > now_ms) set_bit(next, p->turn_left);
    if (pulse_until_[1] > now_ms) set_bit(next, p->turn_right);
    if (tap_until_ > now_ms) set_bit(next, p->back_tap);
    set_source(kController, next);
  }
}

bool PadMapper::held(int action) const {
  for (int s = 0; s < kSources; ++s) {
    if (has_bit(held_[s], action)) return true;
  }
  return false;
}

bool PadMapper::take_pad_change() {
  const bool changed = pad_changed_;
  pad_changed_ = false;
  return changed;
}

void PadMapper::release_all() {
  uint32_t none[16] = {0};
  for (int s = 0; s < kSources; ++s) set_source(static_cast<Source>(s), none);
  /* The pad's buttons stay known: a chord still held does not open the menu again. */
  pulse_until_[0] = pulse_until_[1] = tap_until_ = 0;
  left_trigger_ = right_trigger_ = false;
}

}  // namespace frontend
}  // namespace nbt
