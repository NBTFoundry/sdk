// SPDX-License-Identifier: MPL-2.0
/* An emulator application on the libretro host, with tests/fake_core.c for
 * its core (tests/test_retro.py). Its controls follow the SNES pad's shape,
 * as an emulator's would: each face button where the original has it. */
#include "libretro.h"
#include "nbt/app/video.hpp"
#include "nbt/retro/host.hpp"

namespace {

using nbt::frontend::Binding;
using nbt::frontend::ControllerProfile;
using nbt::frontend::KeyboardProfile;
using nbt::frontend::PadProfile;
using nbt::frontend::Profiles;

const Binding kPadButtons[] = {{NBT_GAMEPAD_SOUTH, RETRO_DEVICE_ID_JOYPAD_B},
                               {NBT_GAMEPAD_EAST, RETRO_DEVICE_ID_JOYPAD_A},
                               {NBT_GAMEPAD_WEST, RETRO_DEVICE_ID_JOYPAD_Y},
                               {NBT_GAMEPAD_NORTH, RETRO_DEVICE_ID_JOYPAD_X},
                               {NBT_GAMEPAD_LB, RETRO_DEVICE_ID_JOYPAD_L},
                               {NBT_GAMEPAD_RB, RETRO_DEVICE_ID_JOYPAD_R},
                               {NBT_GAMEPAD_VIEW, RETRO_DEVICE_ID_JOYPAD_SELECT},
                               {NBT_GAMEPAD_MENU, RETRO_DEVICE_ID_JOYPAD_START},
                               {NBT_GAMEPAD_UP, RETRO_DEVICE_ID_JOYPAD_UP},
                               {NBT_GAMEPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_DOWN},
                               {NBT_GAMEPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_LEFT},
                               {NBT_GAMEPAD_RIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT}};
const PadProfile kPad = {kPadButtons,
                         sizeof(kPadButtons) / sizeof(kPadButtons[0]),
                         RETRO_DEVICE_ID_JOYPAD_UP,
                         RETRO_DEVICE_ID_JOYPAD_DOWN,
                         RETRO_DEVICE_ID_JOYPAD_LEFT,
                         RETRO_DEVICE_ID_JOYPAD_RIGHT,
                         RETRO_DEVICE_ID_JOYPAD_L,
                         RETRO_DEVICE_ID_JOYPAD_R,
                         NBT_GAMEPAD_VIEW | NBT_GAMEPAD_MENU,
                         NBT_GAMEPAD_GUIDE};
const Binding kKeys[] = {{NBT_CONTROLLER_PUSH, RETRO_DEVICE_ID_JOYPAD_A},
                         {NBT_CONTROLLER_TILT_UP, RETRO_DEVICE_ID_JOYPAD_UP},
                         {NBT_CONTROLLER_TILT_DOWN, RETRO_DEVICE_ID_JOYPAD_DOWN},
                         {NBT_CONTROLLER_TILT_LEFT, RETRO_DEVICE_ID_JOYPAD_LEFT},
                         {NBT_CONTROLLER_TILT_RIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT},
                         {NBT_CONTROLLER_MEDIA, RETRO_DEVICE_ID_JOYPAD_SELECT},
                         {NBT_CONTROLLER_OPTION, RETRO_DEVICE_ID_JOYPAD_START}};
const ControllerProfile kController = {
    kKeys, sizeof(kKeys) / sizeof(kKeys[0]), RETRO_DEVICE_ID_JOYPAD_L, RETRO_DEVICE_ID_JOYPAD_R,
    150,   RETRO_DEVICE_ID_JOYPAD_B,         NBT_CONTROLLER_MENU};
const Binding kHostKeys[] = {{'x', RETRO_DEVICE_ID_JOYPAD_A},
                             {'z', RETRO_DEVICE_ID_JOYPAD_B},
                             {nbt::app::kHostKeyReturn, RETRO_DEVICE_ID_JOYPAD_START},
                             {nbt::app::kHostKeyUp, RETRO_DEVICE_ID_JOYPAD_UP},
                             {nbt::app::kHostKeyDown, RETRO_DEVICE_ID_JOYPAD_DOWN},
                             {nbt::app::kHostKeyLeft, RETRO_DEVICE_ID_JOYPAD_LEFT},
                             {nbt::app::kHostKeyRight, RETRO_DEVICE_ID_JOYPAD_RIGHT}};
const KeyboardProfile kKeyboard = {kHostKeys, sizeof(kHostKeys) / sizeof(kHostKeys[0]),
                                   nbt::app::kHostKeyEscape};

const char* const kRegion[] = {"auto", "auto", "ntsc", "NTSC", "pal", "PAL", NULL};
const nbt::retro::VariableMap kVariables[] = {{"region", "core_region", kRegion}};

}  // namespace

int main(int argc, char** argv) {
  Profiles profiles = {{&kPad, NULL, NULL, NULL}, &kController, &kKeyboard};
  nbt::retro::HostConfig config = {"fake", "NBT fake core", "Fake console",
                                   NULL,   &profiles,       kVariables,
                                   1,      4.0f / 3.0f,     1024 * 1024,
                                   true /* BIOS files in the files folder */};
  return nbt::retro::run(argc, argv, config);
}
