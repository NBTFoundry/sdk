#include "nbt/app/bridge.hpp"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "nbt/app/parse.hpp"

namespace nbt {
namespace app {

bool bridge_cap_has(const char* caps, const char* cap) {
  if (!caps || !caps[0] || !cap || !cap[0]) return false;
  const size_t cap_len = strlen(cap);
  const char* p = caps;
  while (*p) {
    const char* comma = strchr(p, ',');
    const size_t tok_len = comma ? static_cast<size_t>(comma - p) : strlen(p);
    if (tok_len == cap_len && strncmp(p, cap, cap_len) == 0) return true;
    if (!comma) break;
    p = comma + 1;
  }
  return false;
}

size_t bridge_status_sanitize(const char* text, char out[kBridgeStatusMax + 1]) {
  size_t len = 0;
  if (text) {
    while (text[len] && len < kBridgeStatusMax) {
      const unsigned char ch = static_cast<unsigned char>(text[len]);
      out[len] = (ch >= 0x20 && ch <= 0x7e) ? static_cast<char>(ch) : ' ';
      ++len;
    }
  }
  out[len] = '\0';
  return len;
}

int bridge_status_format(uint64_t sequence, const char* text, char* out, size_t size) {
  if (!text || !out || !size) return -1;
  const size_t len = strlen(text);
  if (len == 0 || len > kBridgeStatusMax) return -1;
  for (size_t i = 0; i < len; ++i) {
    const unsigned char ch = static_cast<unsigned char>(text[i]);
    if (ch < 0x20 || ch > 0x7e) return -1;
  }
  const int n = snprintf(out, size, "%llu %s\n", static_cast<unsigned long long>(sequence), text);
  if (n <= 0 || static_cast<size_t>(n) >= size) return -1;
  return n;
}

int datagram_input_fd(const char* text, const int* excluded, size_t count) {
  int fd = -1, type = 0;
  if (!parse_int(text, 3, &fd)) return -1;
  for (size_t i = 0; i < count; ++i)
    if (excluded && fd == excluded[i]) return -1;
  socklen_t size = sizeof(type);
  if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &size) || type != SOCK_DGRAM) return -1;
  /* POSIX promises only "not -1" from a successful F_SETFL; the head unit's
   * io-pkt sockets return other values. */
  const int flags = fcntl(fd, F_GETFL, 0);
  return flags != -1 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1 ? fd : -1;
}

}  // namespace app
}  // namespace nbt
