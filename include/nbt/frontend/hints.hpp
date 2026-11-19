// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_FRONTEND_HINTS_HPP
#define NBT_FRONTEND_HINTS_HPP

#include <stdint.h>

namespace nbt {
namespace frontend {

/* What the user holds, named as printed on it, for menus and notices
 * (English). `family` is a pad's NBT_GAMEPAD_FAMILY_*, or -1 for the iDrive
 * controller. */
const char* input_name(int family);
/* A pad's button (one NBT_GAMEPAD_* bit): "A", "Cross", "Create"... */
const char* pad_button_name(int family, uint32_t button);
/* An iDrive controller key (one NBT_CONTROLLER_* bit): "Push", "BACK"... */
const char* controller_key_name(uint32_t key);
/* The confirm and cancel buttons of the menus for `family` (the controller's
 * when -1): "A"/"B", "Cross"/"Circle", "Push"/"BACK". */
const char* confirm_name(int family);
const char* cancel_name(int family);

}  // namespace frontend
}  // namespace nbt

#endif /* NBT_FRONTEND_HINTS_HPP */
