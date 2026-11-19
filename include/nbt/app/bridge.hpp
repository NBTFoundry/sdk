// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_APP_BRIDGE_HPP
#define NBT_APP_BRIDGE_HPP

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace app {

enum {
  /* Longest status text on NBT_STATUS_FD. */
  kBridgeStatusMax = 24
};

/* Whether the comma-separated capability list (NBT_BRIDGE_CAPS) names `cap`.
 * Whole names only: "show" does not match "showx". */
bool bridge_cap_has(const char* caps, const char* cap);

/* Copies `text` with every character outside printable ASCII replaced by a
 * space, cut at kBridgeStatusMax. Returns the length (0 for no text). */
size_t bridge_status_sanitize(const char* text, char out[kBridgeStatusMax + 1]);

/* The status line "<sequence> <text>\n". `text` must be 1..kBridgeStatusMax
 * printable ASCII characters. Returns the line length, or -1. */
int bridge_status_format(uint64_t sequence, const char* text, char* out, size_t size);

/* The bridge's datagram input descriptor named by `text` (the value of
 * NBT_CONTROLLER_FD or NBT_GAMEPAD_FD), made non-blocking. -1 unless it is a
 * datagram socket other than the `excluded` descriptors. */
int datagram_input_fd(const char* text, const int* excluded, size_t count);

}  // namespace app
}  // namespace nbt

#endif /* NBT_APP_BRIDGE_HPP */
