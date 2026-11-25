// SPDX-License-Identifier: MPL-2.0
#include "nbt/app/companion.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace app {

namespace {

const size_t kBodyMax = NBT_COMPANION_PAGE_MAX - 21;  // room for the sequence line

bool text_valid(const char* text) {
  const size_t n = text ? strlen(text) : 0;
  if (!n || n > NBT_COMPANION_TEXT_MAX) return false;
  for (size_t i = 0; i < n; ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c < 0x20 || c == 0x7f) return false;
  }
  return true;
}

bool id_valid(const char* id) {
  return id && nbt_companion_id_valid(id, strlen(id));
}

}  // namespace

Companion::Companion()
    : fd_(-1),
      used_(0),
      widgets_(0),
      shownLength_(0),
      published_(false),
      sequence_(0),
      lastEvent_(0) {
  events_[0] = '\0';
}

bool Companion::init() {
  const char* fd = getenv("NBT_COMPANION_FD");
  const char* events = getenv("NBT_COMPANION_EVENTS");
  if (!fd || strcmp(fd, "11") || fcntl(11, F_GETFD) < 0 || !events || events[0] != '/' ||
      strlen(events) >= sizeof(events_)) {
    return false;
  }
  snprintf(events_, sizeof(events_), "%s", events);
  fd_ = 11;
  return true;
}

void Companion::begin() {
  used_ = 0;
  widgets_ = 0;
}

bool Companion::add(const char* line, size_t length) {
  if (widgets_ >= NBT_COMPANION_WIDGETS_MAX || used_ + length > kBodyMax) return false;
  memcpy(page_ + used_, line, length);
  used_ += length;
  ++widgets_;
  return true;
}

bool Companion::title(const char* text) {
  char line[160];
  return text_valid(text) &&
         add(line, static_cast<size_t>(snprintf(line, sizeof(line), "title\t%s\n", text)));
}

bool Companion::text(const char* text) {
  char line[160];
  return text_valid(text) &&
         add(line, static_cast<size_t>(snprintf(line, sizeof(line), "text\t%s\n", text)));
}

bool Companion::button(const char* id, const char* label) {
  char line[192];
  return id_valid(id) && text_valid(label) &&
         add(line,
             static_cast<size_t>(snprintf(line, sizeof(line), "button\t%s\t%s\n", id, label)));
}

bool Companion::toggle(const char* id, const char* label, bool on) {
  char line[192];
  return id_valid(id) && text_valid(label) &&
         add(line, static_cast<size_t>(snprintf(line, sizeof(line), "toggle\t%s\t%s\t%c\n", id,
                                                label, on ? '1' : '0')));
}

bool Companion::choice(const char* id, const char* label, const char* value,
                       const char* const* choices, size_t count) {
  char line[512];
  if (!id_valid(id) || !text_valid(label) || !value || !value[0] ||
      !nbt_companion_value_valid(value, strlen(value)) || !choices || !count ||
      count > NBT_COMPANION_CHOICES_MAX) {
    return false;
  }
  int n = snprintf(line, sizeof(line), "choice\t%s\t%s\t%s\t", id, label, value);
  bool listed = false;
  for (size_t i = 0; i < count; ++i) {
    const size_t length = choices[i] ? strlen(choices[i]) : 0;
    if (!length || !nbt_companion_value_valid(choices[i], length)) return false;
    listed = listed || !strcmp(choices[i], value);
    n +=
        snprintf(line + n, sizeof(line) - static_cast<size_t>(n), "%s%s", i ? "," : "", choices[i]);
  }
  if (!listed || static_cast<size_t>(n) + 1 >= sizeof(line)) return false;
  line[n++] = '\n';
  return add(line, static_cast<size_t>(n));
}

bool Companion::progress(const char* label, unsigned percent) {
  char line[160];
  return text_valid(label) && percent <= 100 &&
         add(line, static_cast<size_t>(
                       snprintf(line, sizeof(line), "progress\t%s\t%u\n", label, percent)));
}

bool Companion::publish() {
  if (fd_ < 0) return false;
  if (published_ && used_ == shownLength_ && !memcmp(page_, shown_, used_)) return true;
  char out[NBT_COMPANION_PAGE_MAX];
  const int head =
      snprintf(out, sizeof(out), "%llu\n", static_cast<unsigned long long>(sequence_ + 1));
  memcpy(out + head, page_, used_);
  const size_t length = static_cast<size_t>(head) + used_;
  // lseek and write: modules have no pwrite.
  ssize_t n = -1;
  if (lseek(fd_, 0, SEEK_SET) == 0) {
    do {
      n = write(fd_, out, length);
    } while (n < 0 && errno == EINTR);
  }
  if (n != static_cast<ssize_t>(length) || ftruncate(fd_, static_cast<off_t>(length))) return false;
  ++sequence_;
  memcpy(shown_, page_, used_);
  shownLength_ = used_;
  published_ = true;
  return true;
}

bool Companion::next(Event* out) {
  char text[NBT_COMPANION_EVENTS_MAX * (NBT_COMPANION_EVENT_LINE_MAX + 1)];
  if (!out || !events_[0]) return false;
  const int fd = open(events_, O_RDONLY | O_NONBLOCK);
  if (fd < 0) return false;
  ssize_t n;
  do {
    n = read(fd, text, sizeof(text));
  } while (n < 0 && errno == EINTR);
  close(fd);
  // The oldest event after the last one read.
  for (size_t at = 0; n > 0 && at < static_cast<size_t>(n);) {
    const size_t rest = static_cast<size_t>(n) - at;
    const char* end = static_cast<const char*>(memchr(text + at, '\n', rest));
    const size_t length = end ? static_cast<size_t>(end - (text + at)) + 1 : rest;
    uint64_t sequence = 0;
    if (!nbt_companion_event_parse(text + at, length, &sequence, out->id, out->value) &&
        sequence > lastEvent_) {
      lastEvent_ = sequence;
      return true;
    }
    at += length;
  }
  return false;
}

}  // namespace app
}  // namespace nbt
