// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_APP_PARSE_HPP
#define NBT_APP_PARSE_HPP

namespace nbt {
namespace app {

/* A decimal integer in [minimum, INT_MAX]; the whole text must be the number. */
bool parse_int(const char* text, long minimum, int* out);

/* A decimal integer in [minimum, maximum]. */
bool parse_int_range(const char* text, long minimum, long maximum, int* out);

/* "on"/"off", "true"/"false", "yes"/"no", "1"/"0". */
bool parse_bool(const char* text, bool* out);

}  // namespace app
}  // namespace nbt

#endif /* NBT_APP_PARSE_HPP */
