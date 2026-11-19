// SPDX-License-Identifier: MPL-2.0
#include "nbt/frontend/menu.hpp"

#include <stdio.h>
#include <string.h>

#include "nbt/app/video.hpp"

namespace nbt {
namespace frontend {

namespace {

const uint32_t kDim = 0x000000d8u, kPanel = 0x1b2230ffu, kFocus = 0x2e6f8effu;
const uint32_t kTitle = 0xffffffffu, kText = 0xe6e9eeffu, kNote = 0x9aa3b0ffu;

void copy(char* out, size_t size, const char* text) {
  snprintf(out, size, "%s", text ? text : "");
}

}  // namespace

Menu::Menu() : open_(false), count_(0), focus_(0) {
  title_[0] = notes_[0][0] = notes_[1][0] = '\0';
}

void Menu::open(const char* title) {
  open_ = true;
  copy(title_, sizeof(title_), title);
  count_ = focus_ = 0;
  notes_[0][0] = notes_[1][0] = '\0';
}

void Menu::add(const char* text) {
  if (count_ < kMaxItems) copy(items_[count_++], sizeof(items_[0]), text);
}

void Menu::set_item(size_t i, const char* text) {
  if (i < count_) copy(items_[i], sizeof(items_[0]), text);
}

void Menu::set_note(const char* line1, const char* line2) {
  copy(notes_[0], sizeof(notes_[0]), line1);
  copy(notes_[1], sizeof(notes_[1]), line2);
}

void Menu::set_focus(size_t i) {
  focus_ = i < count_ ? i : 0;
}

Menu::Result Menu::key(MenuKey key) {
  if (!open_) return kNone;
  switch (key) {
    case kMenuUp:
      if (count_) focus_ = focus_ ? focus_ - 1 : count_ - 1;
      return kNone;
    case kMenuDown:
      if (count_) focus_ = focus_ + 1 < count_ ? focus_ + 1 : 0;
      return kNone;
    case kMenuPageUp:
      focus_ = focus_ > kPage ? focus_ - kPage : 0;
      return kNone;
    case kMenuPageDown:
      if (count_) focus_ = focus_ + kPage < count_ ? focus_ + kPage : count_ - 1;
      return kNone;
    case kMenuSelect:
      return count_ ? kSelected : kNone;
    case kMenuBack:
      open_ = false;
      return kClosed;
    default:
      return kNone;
  }
}

void Menu::draw(Overlay& overlay, int window_w, int window_h) const {
  if (!open_) return;
  const int scale = Overlay::scale_for(window_h);
  const int line = Overlay::kGlyph * scale, row = line + line / 2, margin = line;
  /* A panel at most 70 characters wide, centred, over the dimmed frame. */
  int panel_w = 70 * line + 2 * margin;
  if (panel_w > window_w - 2 * margin) panel_w = window_w - 2 * margin;
  const int x = (window_w - panel_w) / 2;
  const int notes = (notes_[0][0] ? 1 : 0) + (notes_[1][0] ? 1 : 0);
  const int top = margin, bottom = window_h - margin;
  const int list_top = top + margin + 2 * line;
  const int list_bottom = bottom - margin - notes * row;
  size_t shown = list_bottom > list_top ? static_cast<size_t>((list_bottom - list_top) / row) : 1;
  if (shown < 1) shown = 1;
  size_t first = focus_ >= shown ? focus_ - shown + 1 : 0;
  overlay.box(0, 0, window_w, window_h, kDim);
  overlay.box(x, top, panel_w, bottom - top, kPanel);
  const int columns = (panel_w - 2 * margin) / line;
  char text[kTextMax + 1];
  snprintf(text, sizeof(text), "%.*s", columns, title_);
  overlay.text(x + margin, top + margin, scale, text, kTitle);
  for (size_t i = first; i < count_ && i < first + shown; ++i) {
    const int y = list_top + static_cast<int>(i - first) * row;
    if (i == focus_) overlay.box(x + margin / 2, y - line / 4, panel_w - margin, row, kFocus);
    snprintf(text, sizeof(text), "%.*s", columns, items_[i]);
    overlay.text(x + margin, y, scale, text, kText);
  }
  for (int n = 0, y = list_bottom + margin / 2; n < 2; ++n) {
    if (!notes_[n][0]) continue;
    snprintf(text, sizeof(text), "%.*s", columns, notes_[n]);
    overlay.text(x + margin, y, scale, text, kNote);
    y += row;
  }
}

MenuInput::MenuInput() : pad_seen_(false), back_pressed_(false), head_(0), count_(0) {
  memset(&pad_, 0, sizeof(pad_));
}

void MenuInput::reset() {
  navigation_.reset();
  head_ = count_ = 0;
  pad_seen_ = back_pressed_ = false;
}

void MenuInput::push(MenuKey key) {
  if (key != kMenuNone && count_ < sizeof(queue_) / sizeof(queue_[0])) {
    queue_[(head_ + count_++) % (sizeof(queue_) / sizeof(queue_[0]))] = key;
  }
}

void MenuInput::gamepad(const nbt_gamepad_wire_state& state, uint64_t now_ms) {
  /* Buttons held when the menu opened count only once released. */
  const uint32_t before = pad_seen_ ? pad_.buttons : state.buttons;
  pad_ = state;
  pad_seen_ = true;
  const gamepad::NavigationEvent e = navigation_.update(state, now_ms);
  const uint32_t moves = e.pressed | e.repeated;
  if (moves & NBT_GAMEPAD_UP) push(kMenuUp);
  if (moves & NBT_GAMEPAD_DOWN) push(kMenuDown);
  const uint32_t pressed = state.buttons & ~before;
  if (pressed & NBT_GAMEPAD_SOUTH) push(kMenuSelect);
  if (pressed & NBT_GAMEPAD_EAST) push(kMenuBack);
  if (pressed & NBT_GAMEPAD_LB) push(kMenuPageUp);
  if (pressed & NBT_GAMEPAD_RB) push(kMenuPageDown);
}

void MenuInput::controller(const nbt_controller_wire_state& state, const controller::Event& event) {
  (void)state;
  for (int32_t i = 0; i < event.rotation && i < 8; ++i) push(kMenuDown);
  for (int32_t i = 0; i > event.rotation && i > -8; --i) push(kMenuUp);
  if (event.pressed & NBT_CONTROLLER_TILT_UP) push(kMenuUp);
  if (event.pressed & NBT_CONTROLLER_TILT_DOWN) push(kMenuDown);
  if (event.pressed & NBT_CONTROLLER_PUSH) push(kMenuSelect);
  /* BACK closes on release, a release of a press seen here: the hold that
   * opened the menu does not close it. */
  if (event.pressed & NBT_CONTROLLER_BACK) back_pressed_ = true;
  if ((event.released & NBT_CONTROLLER_BACK) && back_pressed_) {
    back_pressed_ = false;
    push(kMenuBack);
  }
}

void MenuInput::host_key(int key, bool down) {
  if (!down) return;
  if (key == app::kHostKeyUp) push(kMenuUp);
  if (key == app::kHostKeyDown) push(kMenuDown);
  if (key == app::kHostKeyReturn) push(kMenuSelect);
  if (key == app::kHostKeyEscape || key == app::kHostKeyBackspace) push(kMenuBack);
}

MenuKey MenuInput::next(uint64_t now_ms) {
  if (pad_seen_ && pad_.connected) {
    const gamepad::NavigationEvent e = navigation_.update(pad_, now_ms);
    if (e.repeated & NBT_GAMEPAD_UP) push(kMenuUp);
    if (e.repeated & NBT_GAMEPAD_DOWN) push(kMenuDown);
  }
  if (!count_) return kMenuNone;
  const MenuKey key = queue_[head_];
  head_ = (head_ + 1) % (sizeof(queue_) / sizeof(queue_[0]));
  --count_;
  return key;
}

}  // namespace frontend
}  // namespace nbt
