// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_FRONTEND_PAD_PROFILES_HPP
#define NBT_FRONTEND_PAD_PROFILES_HPP

#include <stddef.h>
#include <stdint.h>

#include "nbt/app/back_gesture.hpp"
#include "nbt/app/input_mixer.hpp"
#include "nbt/controller/input.hpp"
#include "nbt/gamepad_wire.h"

namespace nbt {
namespace frontend {

/* An application's actions are its own numbers, 0 to kMaxAction (a game's
 * keys, a libretro joypad's buttons). */
enum { kNoAction = -1, kMaxAction = 511 };

/* A control (a gamepad's NBT_GAMEPAD_* bit, the iDrive controller's
 * NBT_CONTROLLER_* bit, a host key) and the action it holds. */
struct Binding {
  uint32_t control;
  int action;
};

/* How one family of pads plays. Each application lays out its own, so that
 * a button sits where the original controller has it. */
struct PadProfile {
  const Binding* buttons;
  size_t button_count;
  int stick_up, stick_down, stick_left, stick_right; /* the left stick as directions */
  int left_trigger, right_trigger;                   /* pressed past a quarter */
  uint32_t menu_buttons; /* held together, open the front end's menu (0: none) */
  uint32_t menu_button;  /* alone, opens it (0: none) */
};

/* How the iDrive controller plays: its keys, the knob's detents (each holds
 * its action for pulse_ms) and BACK: a tap is back_tap's action, a hold the
 * menu, a long hold quits (before NBT Manager's own escape). */
struct ControllerProfile {
  const Binding* keys;
  size_t key_count;
  int turn_left, turn_right;
  uint32_t pulse_ms;
  int back_tap;
  uint32_t menu_keys; /* any of them opens the menu (NBT_CONTROLLER_MENU, say) */
};

/* The desktop keyboard (nbt::app::HostKey values). */
struct KeyboardProfile {
  const Binding* keys;
  size_t key_count;
  int menu_key; /* opens the menu */
};

struct Profiles {
  /* By NBT_GAMEPAD_FAMILY_*; pad[0] serves a pad of unknown family and any
   * family left NULL. */
  const PadProfile* pad[NBT_GAMEPAD_FAMILY_MAX + 1];
  const ControllerProfile* controller;
  const KeyboardProfile* keyboard;
};

enum Command { kCommandNone = 0, kCommandMenu, kCommandQuit };

/* Turns the pad, the iDrive controller and the keyboard into the
 * application's actions by the profile of the pad in use, chosen by its
 * family as soon as it is plugged in. An action is held while any source
 * holds it; changes go to the sink as InputAction key down and up, with the
 * action as value. */
class PadMapper {
 public:
  enum Source { kPad = 0, kController, kKeyboard, kSources };

  explicit PadMapper(const Profiles& profiles);
  void set_sink(app::InputSink* sink) { mixer_.set_sink(sink); }

  void gamepad(const nbt_gamepad_wire_state& state, uint64_t now_ms);
  void controller(const nbt_controller_wire_state& state, const controller::Event& event,
                  uint64_t now_ms);
  void host_key(int key, bool down);
  /* Ends pulses and fires BACK holds; returns a command once. */
  Command poll(uint64_t now_ms);

  bool held(int action) const;
  /* The pad in use: its NBT_GAMEPAD_FAMILY_*, or -1 with none connected. */
  int pad_family() const { return family_; }
  /* True once after a pad was connected, changed family or was unplugged. */
  bool take_pad_change();
  /* Releases every action (the menu opened, the screen was lost). */
  void release_all();

 private:
  PadMapper(const PadMapper&);
  PadMapper& operator=(const PadMapper&);

  const PadProfile* pad_profile() const;
  /* The controller's actions now: its keys, the knob's pulses, a BACK tap. */
  void refresh(uint64_t now_ms);
  void hold(Source source, int action, bool on);
  void set_source(Source source, const uint32_t (&next)[16]);
  void command(Command c);

  Profiles profiles_;
  app::InputMixer mixer_;
  uint32_t held_[kSources][16]; /* one bit per action */
  int family_;
  bool pad_changed_;
  uint32_t pad_buttons_;
  bool left_trigger_, right_trigger_;
  uint32_t controller_keys_;
  uint64_t pulse_until_[2]; /* turn_left, turn_right */
  uint64_t tap_until_;
  uint64_t now_ms_;
  app::BackGesture back_;
  Command pending_;
};

}  // namespace frontend
}  // namespace nbt

#endif /* NBT_FRONTEND_PAD_PROFILES_HPP */
