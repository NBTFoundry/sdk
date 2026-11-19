// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_FRONTEND_MENU_HPP
#define NBT_FRONTEND_MENU_HPP

#include <stddef.h>
#include <stdint.h>

#include "nbt/controller/input.hpp"
#include "nbt/gamepad/input.hpp"
#include "nbt/frontend/overlay.hpp"

namespace nbt {
namespace frontend {

enum MenuKey { kMenuNone = 0, kMenuUp, kMenuDown, kMenuSelect, kMenuBack };

/* A list the application fills (a pause menu, a file picker), drawn over its
 * frame with an Overlay: a title, up to kMaxItems items with one focused,
 * and up to two lines of note. Scrolls when longer than the window. */
class Menu {
 public:
  enum { kMaxItems = 64, kTextMax = 100 };
  enum Result { kNone = 0, kSelected, kClosed };

  Menu();
  void open(const char* title);
  void close() { open_ = false; }
  bool is_open() const { return open_; }
  void add(const char* text);
  void set_item(size_t i, const char* text);
  void set_note(const char* line1, const char* line2 = NULL);
  void set_focus(size_t i);
  size_t focus() const { return focus_; }
  size_t count() const { return count_; }
  const char* item(size_t i) const { return i < count_ ? items_[i] : ""; }

  /* Up and down move (wrapping), select chooses the focused item, back closes. */
  Result key(MenuKey key);
  void draw(Overlay& overlay, int window_w, int window_h) const;

 private:
  bool open_;
  char title_[kTextMax + 1];
  char items_[kMaxItems][kTextMax + 1];
  char notes_[2][kTextMax + 1];
  size_t count_, focus_;
};

/* Menu keys from every input: a pad's D-pad and left stick (repeating after
 * 400 ms), south confirms and east cancels; the iDrive controller's knob,
 * tilts, push and BACK; the desktop's arrows, Return, Escape and Backspace. */
class MenuInput {
 public:
  MenuInput();
  void gamepad(const nbt_gamepad_wire_state& state, uint64_t now_ms);
  void controller(const nbt_controller_wire_state& state, const controller::Event& event);
  void host_key(int key, bool down);
  /* Repeats; then the next key, or kMenuNone. */
  MenuKey next(uint64_t now_ms);
  void reset();

 private:
  void push(MenuKey key);

  gamepad::Navigation navigation_;
  nbt_gamepad_wire_state pad_;
  bool pad_seen_, back_pressed_;
  MenuKey queue_[16];
  size_t head_, count_;
};

}  // namespace frontend
}  // namespace nbt

#endif /* NBT_FRONTEND_MENU_HPP */
