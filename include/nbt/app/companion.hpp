// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_APP_COMPANION_HPP
#define NBT_APP_COMPANION_HPP

#include <stddef.h>
#include <stdint.h>

#include "nbt/companion_wire.h"

namespace nbt {
namespace app {

/* Capability `companion` (bridge 1.9): a page of a few widgets in the phone
 * app, and the driver's taps and choices back (NBT Manager's
 * docs/companion-bridge.md). The service has it when the application declares
 * one. Build the page from the main loop, publish it (written only when it
 * changed), and read the events: both are cheap. */
class Companion {
 public:
  struct Event {
    char id[NBT_COMPANION_ID_MAX + 1];
    char value[NBT_COMPANION_VALUE_MAX + 1]; /* empty for a button, 0/1 for a toggle */
  };

  Companion();
  /* From NBT_COMPANION_FD and NBT_COMPANION_EVENTS; false when not granted. */
  bool init();
  bool available() const { return fd_ >= 0; }

  /* A new page: widgets in order, then publish(). A widget that is invalid
   * (texts 1-120 bytes without tabs or control characters, ids 1-24 of
   * a-z 0-9 _ -) or does not fit (32 widgets, 4 KiB) is left out: false. */
  void begin();
  bool title(const char* text);
  bool text(const char* text);
  bool button(const char* id, const char* label);
  bool toggle(const char* id, const char* label, bool on);
  /* `value` among `choices` (1-12, each 1-24 bytes without commas). */
  bool choice(const char* id, const char* label, const char* value, const char* const* choices,
              size_t count);
  bool progress(const char* label, unsigned percent);
  /* Writes the page when it differs from the last one; false on an error. */
  bool publish();

  /* The phone's next event, oldest first; false when there is none. */
  bool next(Event* out);

 private:
  bool add(const char* line, size_t length);

  int fd_;
  char events_[256];
  char page_[NBT_COMPANION_PAGE_MAX];
  size_t used_;
  unsigned widgets_;
  char shown_[NBT_COMPANION_PAGE_MAX];
  size_t shownLength_;
  bool published_;
  uint64_t sequence_;
  uint64_t lastEvent_;
};

}  // namespace app
}  // namespace nbt

#endif
