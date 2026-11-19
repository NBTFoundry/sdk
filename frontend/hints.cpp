// SPDX-License-Identifier: MPL-2.0
#include "nbt/frontend/hints.hpp"

#include "nbt/controller_wire.h"
#include "nbt/gamepad_wire.h"

namespace nbt {
namespace frontend {

namespace {

/* By button bit 0-14: south, east, west, north, LB, RB, View, Menu, LS, RS,
 * Guide, then the D-pad. */
const char* const kXbox[15] = {"A",  "B",  "X",    "Y",  "LB",   "RB",   "View", "Menu",
                               "LS", "RS", "Xbox", "Up", "Down", "Left", "Right"};
const char* const kDualShock4[15] = {"Cross", "Circle", "Square",  "Triangle", "L1",
                                     "R1",    "Share",  "Options", "L3",       "R3",
                                     "PS",    "Up",     "Down",    "Left",     "Right"};
const char* const kDualSense[15] = {"Cross", "Circle", "Square",  "Triangle", "L1",
                                    "R1",    "Create", "Options", "L3",       "R3",
                                    "PS",    "Up",     "Down",    "Left",     "Right"};
const char* const kGeneric[15] = {"South", "East",   "West",  "North", "L",
                                  "R",     "Select", "Start", "L3",    "R3",
                                  "Home",  "Up",     "Down",  "Left",  "Right"};

const char* const* names(int family) {
  switch (family) {
    case NBT_GAMEPAD_FAMILY_XBOX:
      return kXbox;
    case NBT_GAMEPAD_FAMILY_DUALSHOCK4:
      return kDualShock4;
    case NBT_GAMEPAD_FAMILY_DUALSENSE:
      return kDualSense;
    default:
      return kGeneric;
  }
}

int bit_of(uint32_t value) {
  for (int bit = 0; bit < 32; ++bit) {
    if (value == (1u << bit)) return bit;
  }
  return -1;
}

}  // namespace

const char* input_name(int family) {
  switch (family) {
    case -1:
      return "iDrive controller";
    case NBT_GAMEPAD_FAMILY_XBOX:
      return "Xbox controller";
    case NBT_GAMEPAD_FAMILY_DUALSHOCK4:
      return "DualShock 4";
    case NBT_GAMEPAD_FAMILY_DUALSENSE:
      return "DualSense";
    default:
      return "Gamepad";
  }
}

const char* pad_button_name(int family, uint32_t button) {
  const int bit = bit_of(button);
  return bit >= 0 && bit < 15 ? names(family)[bit] : "?";
}

const char* controller_key_name(uint32_t key) {
  static const char* const kKeys[12] = {"Push",    "MENU",      "BACK",      "OPTION",
                                        "COM",     "MEDIA",     "NAV",       "MAP",
                                        "Tilt up", "Tilt down", "Tilt left", "Tilt right"};
  static const char* const kFavourites[8] = {"Favourite 1", "Favourite 2", "Favourite 3",
                                             "Favourite 4", "Favourite 5", "Favourite 6",
                                             "Favourite 7", "Favourite 8"};
  const int bit = bit_of(key);
  if (bit >= 0 && bit < 12) return kKeys[bit];
  if (bit >= 16 && bit < 24) return kFavourites[bit - 16];
  return "?";
}

const char* confirm_name(int family) {
  return family < 0 ? "Push" : pad_button_name(family, NBT_GAMEPAD_SOUTH);
}

const char* cancel_name(int family) {
  return family < 0 ? "BACK" : pad_button_name(family, NBT_GAMEPAD_EAST);
}

}  // namespace frontend
}  // namespace nbt
