#include "nbt/app/parse.hpp"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

namespace nbt {
namespace app {

bool parse_int_range(const char* text, long minimum, long maximum, int* out) {
  if (!text || !text[0] || !out) return false;
  if (maximum > INT_MAX) maximum = INT_MAX;
  char* end = NULL;
  errno = 0;
  const long v = strtol(text, &end, 10);
  if (errno || !end || end == text || *end != '\0' || v < minimum || v > maximum) return false;
  *out = static_cast<int>(v);
  return true;
}

bool parse_int(const char* text, long minimum, int* out) {
  return parse_int_range(text, minimum, INT_MAX, out);
}

bool parse_bool(const char* text, bool* out) {
  if (!text || !out) return false;
  if (!strcmp(text, "on") || !strcmp(text, "true") || !strcmp(text, "yes") || !strcmp(text, "1")) {
    *out = true;
    return true;
  }
  if (!strcmp(text, "off") || !strcmp(text, "false") || !strcmp(text, "no") || !strcmp(text, "0")) {
    *out = false;
    return true;
  }
  return false;
}

}  // namespace app
}  // namespace nbt
