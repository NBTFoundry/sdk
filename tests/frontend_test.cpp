// SPDX-License-Identifier: MPL-2.0
/* Unit tests of the front end (libnbt-frontend), the files list and the
 * libretro host's core options. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libretro.h"
#include "nbt/app/files.hpp"
#include "nbt/app/video.hpp"
#include "nbt/frontend/audio_queue.hpp"
#include "nbt/frontend/frame_video.hpp"
#include "nbt/frontend/hints.hpp"
#include "nbt/frontend/menu.hpp"
#include "nbt/frontend/pad_profiles.hpp"
#include "nbt/frontend/save_file.hpp"
#include "nbt/retro/core_options.hpp"

using namespace nbt;
using namespace nbt::frontend;

namespace {

int g_suites = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      exit(1);                                                        \
    }                                                                 \
  } while (0)

void temp_dir(char* path, size_t size) {
  snprintf(path, size, "/tmp/nbt-frontend-XXXXXX");
  CHECK(mkdtemp(path) != NULL);
}

void write_file(const char* dir, const char* name, const char* text) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s", dir, name);
  FILE* f = fopen(path, "wb");
  CHECK(f != NULL);
  fputs(text, f);
  fclose(f);
}

void test_audio_queue() {
  AudioQueue* q = new AudioQueue;
  q->configure(32000.0, 48000, 64);
  CHECK(q->target() == 2048);
  int16_t in[2 * 1000], out[2 * 600];
  for (int i = 0; i < 1000; ++i) in[2 * i] = in[2 * i + 1] = static_cast<int16_t>(i * 10);
  CHECK(q->push(in, 1000) == 1000 && q->fill() == 1000);
  q->render(out, 600); /* 600 device frames at 2/3 of a source frame each */
  CHECK(q->fill() >= 595 && q->fill() <= 605);
  /* A ramp stays a ramp after resampling. */
  for (int i = 1; i < 600; ++i) CHECK(out[2 * i] >= out[2 * (i - 1)]);
  CHECK(out[2 * 3] > out[0]);
  /* Empty: silence, counted. */
  q->clear();
  q->render(out, 10);
  CHECK(out[0] == 0 && out[19] == 0 && q->starved() == 10);
  /* Full: the rest is dropped and counted. */
  int16_t* big = new int16_t[2 * AudioQueue::kCapacity];
  memset(big, 0, sizeof(int16_t) * 2 * AudioQueue::kCapacity);
  CHECK(q->push(big, AudioQueue::kCapacity) == AudioQueue::kCapacity);
  CHECK(q->push(in, 10) == 0 && q->dropped() == 10);
  delete[] big;
  delete q;
  ++g_suites;
}

/* A SNES-like layout, the actions RETRO_DEVICE_ID_JOYPAD_*. */
const Binding kXboxButtons[] = {{NBT_GAMEPAD_SOUTH, RETRO_DEVICE_ID_JOYPAD_B},
                                {NBT_GAMEPAD_EAST, RETRO_DEVICE_ID_JOYPAD_A},
                                {NBT_GAMEPAD_UP, RETRO_DEVICE_ID_JOYPAD_UP},
                                {NBT_GAMEPAD_VIEW, RETRO_DEVICE_ID_JOYPAD_SELECT},
                                {NBT_GAMEPAD_MENU, RETRO_DEVICE_ID_JOYPAD_START}};
const PadProfile kXbox = {kXboxButtons,
                          sizeof(kXboxButtons) / sizeof(kXboxButtons[0]),
                          RETRO_DEVICE_ID_JOYPAD_UP,
                          RETRO_DEVICE_ID_JOYPAD_DOWN,
                          RETRO_DEVICE_ID_JOYPAD_LEFT,
                          RETRO_DEVICE_ID_JOYPAD_RIGHT,
                          RETRO_DEVICE_ID_JOYPAD_L,
                          RETRO_DEVICE_ID_JOYPAD_R,
                          NBT_GAMEPAD_VIEW | NBT_GAMEPAD_MENU,
                          NBT_GAMEPAD_GUIDE};
const Binding kSonyButtons[] = {{NBT_GAMEPAD_SOUTH, RETRO_DEVICE_ID_JOYPAD_Y}};
const PadProfile kSony = {kSonyButtons, 1,         kNoAction, kNoAction, kNoAction,
                          kNoAction,    kNoAction, kNoAction, 0,         NBT_GAMEPAD_GUIDE};
const Binding kKeys[] = {{NBT_CONTROLLER_PUSH, RETRO_DEVICE_ID_JOYPAD_A},
                         {NBT_CONTROLLER_TILT_UP, RETRO_DEVICE_ID_JOYPAD_UP}};
const ControllerProfile kController = {kKeys,
                                       2,
                                       RETRO_DEVICE_ID_JOYPAD_L,
                                       RETRO_DEVICE_ID_JOYPAD_R,
                                       150,
                                       RETRO_DEVICE_ID_JOYPAD_B,
                                       NBT_CONTROLLER_MENU};
const Binding kHostKeys[] = {{'x', RETRO_DEVICE_ID_JOYPAD_A}};
const KeyboardProfile kKeyboard = {kHostKeys, 1, app::kHostKeyEscape};

struct Edges : public app::InputSink {
  int downs, ups;
  Edges() : downs(0), ups(0) {}
  void on_action(const app::InputAction& a) {
    if (a.type == app::InputAction::kKeyDown) ++downs;
    if (a.type == app::InputAction::kKeyUp) ++ups;
  }
};

nbt_gamepad_wire_state pad(uint32_t buttons, uint8_t family) {
  nbt_gamepad_wire_state s;
  memset(&s, 0, sizeof(s));
  s.connected = 1;
  s.family = family;
  s.buttons = buttons;
  return s;
}

void test_pad_mapper() {
  Profiles profiles;
  memset(&profiles, 0, sizeof(profiles));
  profiles.pad[0] = &kXbox;
  profiles.pad[NBT_GAMEPAD_FAMILY_DUALSHOCK4] = &kSony;
  profiles.controller = &kController;
  profiles.keyboard = &kKeyboard;
  PadMapper* m = new PadMapper(profiles);
  Edges edges;
  m->set_sink(&edges);
  CHECK(m->pad_family() == -1);
  /* An Xbox pad: by position, south is B, east is A. */
  m->gamepad(pad(NBT_GAMEPAD_SOUTH, NBT_GAMEPAD_FAMILY_XBOX), 0);
  CHECK(m->take_pad_change() && !m->take_pad_change());
  CHECK(m->pad_family() == NBT_GAMEPAD_FAMILY_XBOX);
  CHECK(m->held(RETRO_DEVICE_ID_JOYPAD_B) && !m->held(RETRO_DEVICE_ID_JOYPAD_A));
  /* The stick and the triggers, with hysteresis. */
  nbt_gamepad_wire_state s = pad(0, NBT_GAMEPAD_FAMILY_XBOX);
  s.ly = 20000;
  s.rt = 40000;
  m->gamepad(s, 1);
  CHECK(m->held(RETRO_DEVICE_ID_JOYPAD_UP) && m->held(RETRO_DEVICE_ID_JOYPAD_R));
  CHECK(!m->held(RETRO_DEVICE_ID_JOYPAD_B));
  s.ly = 14000;
  s.rt = 15000;
  m->gamepad(s, 2);
  CHECK(m->held(RETRO_DEVICE_ID_JOYPAD_UP) && m->held(RETRO_DEVICE_ID_JOYPAD_R));
  s.ly = 0;
  s.rt = 0;
  m->gamepad(s, 3);
  CHECK(!m->held(RETRO_DEVICE_ID_JOYPAD_UP) && !m->held(RETRO_DEVICE_ID_JOYPAD_R));
  /* Two sources hold one action: it stays held until both let go. */
  m->gamepad(pad(NBT_GAMEPAD_UP, NBT_GAMEPAD_FAMILY_XBOX), 4);
  nbt_controller_wire_state c;
  memset(&c, 0, sizeof(c));
  c.connected = 1;
  c.keys = NBT_CONTROLLER_TILT_UP;
  controller::Event e = {NBT_CONTROLLER_TILT_UP, 0, 0};
  m->controller(c, e, 5);
  const int downs = edges.downs;
  m->gamepad(pad(0, NBT_GAMEPAD_FAMILY_XBOX), 6);
  CHECK(m->held(RETRO_DEVICE_ID_JOYPAD_UP) && edges.downs == downs);
  c.keys = 0;
  e.pressed = 0;
  e.released = NBT_CONTROLLER_TILT_UP;
  m->controller(c, e, 7);
  CHECK(!m->held(RETRO_DEVICE_ID_JOYPAD_UP));
  /* The menu chord, once; Guide alone too. */
  m->gamepad(pad(NBT_GAMEPAD_VIEW, NBT_GAMEPAD_FAMILY_XBOX), 8);
  CHECK(m->poll(8) == kCommandNone);
  m->gamepad(pad(NBT_GAMEPAD_VIEW | NBT_GAMEPAD_MENU, NBT_GAMEPAD_FAMILY_XBOX), 9);
  CHECK(m->poll(9) == kCommandMenu && m->poll(10) == kCommandNone);
  m->gamepad(pad(NBT_GAMEPAD_GUIDE, NBT_GAMEPAD_FAMILY_XBOX), 11);
  CHECK(m->poll(11) == kCommandMenu);
  /* A DualShock 4: its own profile at once; nothing of the Xbox one held. */
  m->gamepad(pad(NBT_GAMEPAD_SOUTH, NBT_GAMEPAD_FAMILY_DUALSHOCK4), 12);
  CHECK(m->take_pad_change() && m->pad_family() == NBT_GAMEPAD_FAMILY_DUALSHOCK4);
  CHECK(m->held(RETRO_DEVICE_ID_JOYPAD_Y) && !m->held(RETRO_DEVICE_ID_JOYPAD_B));
  /* Unplugged: everything released. */
  nbt_gamepad_wire_state gone;
  memset(&gone, 0, sizeof(gone));
  m->gamepad(gone, 13);
  CHECK(m->take_pad_change() && m->pad_family() == -1 && !m->held(RETRO_DEVICE_ID_JOYPAD_Y));
  /* The knob pulses its action per detent. */
  c.keys = 0;
  controller::Event turn = {0, 0, 2};
  m->controller(c, turn, 100);
  CHECK(m->held(RETRO_DEVICE_ID_JOYPAD_R));
  m->poll(399);
  CHECK(m->held(RETRO_DEVICE_ID_JOYPAD_R));
  m->poll(400);
  CHECK(!m->held(RETRO_DEVICE_ID_JOYPAD_R));
  /* BACK: a tap holds B briefly; a hold is the menu, a long hold quits. */
  controller::Event none = {0, 0, 0};
  c.keys = NBT_CONTROLLER_BACK;
  m->controller(c, none, 1000);
  c.keys = 0;
  m->controller(c, none, 1100);
  CHECK(m->held(RETRO_DEVICE_ID_JOYPAD_B));
  m->poll(1300);
  CHECK(!m->held(RETRO_DEVICE_ID_JOYPAD_B));
  c.keys = NBT_CONTROLLER_BACK;
  m->controller(c, none, 2000);
  CHECK(m->poll(2700) == kCommandMenu);
  CHECK(m->poll(3500) == kCommandQuit);
  c.keys = 0;
  m->controller(c, none, 3600);
  CHECK(!m->held(RETRO_DEVICE_ID_JOYPAD_B)); /* a hold is no tap */
  /* MENU opens the menu; the keyboard plays and opens it too. */
  controller::Event menu = {NBT_CONTROLLER_MENU, 0, 0};
  c.keys = NBT_CONTROLLER_MENU;
  m->controller(c, menu, 4000);
  CHECK(m->poll(4000) == kCommandMenu);
  m->host_key('x', true);
  CHECK(m->held(RETRO_DEVICE_ID_JOYPAD_A));
  m->host_key('x', false);
  m->host_key(app::kHostKeyEscape, true);
  CHECK(!m->held(RETRO_DEVICE_ID_JOYPAD_A) && m->poll(4001) == kCommandMenu);
  m->release_all();
  CHECK(edges.downs == edges.ups);
  delete m;
  ++g_suites;
}

void test_hints() {
  CHECK(!strcmp(pad_button_name(NBT_GAMEPAD_FAMILY_XBOX, NBT_GAMEPAD_SOUTH), "A"));
  CHECK(!strcmp(pad_button_name(NBT_GAMEPAD_FAMILY_DUALSHOCK4, NBT_GAMEPAD_EAST), "Circle"));
  CHECK(!strcmp(pad_button_name(NBT_GAMEPAD_FAMILY_DUALSENSE, NBT_GAMEPAD_VIEW), "Create"));
  CHECK(!strcmp(pad_button_name(NBT_GAMEPAD_FAMILY_DUALSHOCK4, NBT_GAMEPAD_VIEW), "Share"));
  CHECK(!strcmp(pad_button_name(0, 3u), "?")); /* not one button */
  CHECK(!strcmp(controller_key_name(NBT_CONTROLLER_FAVOURITE(2)), "Favourite 3"));
  CHECK(!strcmp(confirm_name(-1), "Push") && !strcmp(cancel_name(NBT_GAMEPAD_FAMILY_XBOX), "B"));
  CHECK(!strcmp(input_name(NBT_GAMEPAD_FAMILY_DUALSENSE), "DualSense"));
  ++g_suites;
}

void test_save_file() {
  char dir[64], path[128];
  temp_dir(dir, sizeof(dir));
  snprintf(path, sizeof(path), "%s/save", dir);
  CHECK(save_file_write(path, "hello", 5));
  size_t size = 0;
  char* data = static_cast<char*>(save_file_read(path, 16, &size));
  CHECK(data && size == 5 && !memcmp(data, "hello", 5));
  free(data);
  CHECK(!save_file_read(path, 4, &size)); /* larger than allowed */
  char exact[5];
  CHECK(save_file_read_into(path, exact, 5) && !memcmp(exact, "hello", 5));
  char wrong[6] = "xxxxx";
  CHECK(!save_file_read_into(path, wrong, 6) && wrong[0] == 'x');
  char link[128];
  snprintf(link, sizeof(link), "%s/link", dir);
  CHECK(!symlink(path, link) && !save_file_read(link, 16, &size)); /* not followed */
  struct stat st;
  CHECK(!stat(path, &st) && (st.st_mode & 0777) == 0600);
  char next[160];
  snprintf(next, sizeof(next), "%s.next", path);
  CHECK(access(next, F_OK) != 0);
  unlink(link);
  unlink(path);
  rmdir(dir);
  ++g_suites;
}

void test_file_list() {
  char dir[64], sub[128], link[128];
  temp_dir(dir, sizeof(dir));
  write_file(dir, "b game.sfc", "b");
  write_file(dir, "A game.SFC", "aa");
  write_file(dir, "c.smc", "ccc");
  write_file(dir, "notes.txt", "x");
  write_file(dir, ".hidden.sfc", "x");
  snprintf(sub, sizeof(sub), "%s/folder.sfc", dir);
  CHECK(!mkdir(sub, 0700));
  snprintf(link, sizeof(link), "%s/link.sfc", dir);
  CHECK(!symlink("c.smc", link));
  static const char* const kExtensions[] = {"sfc", "smc", NULL};
  app::FileList* list = new app::FileList;
  CHECK(list->load(dir, kExtensions) && list->count() == 3);
  CHECK(!strcmp(list->at(0).name, "A game.SFC") && !strcmp(list->at(1).name, "b game.sfc") &&
        !strcmp(list->at(2).name, "c.smc") && list->at(2).size == 3);
  CHECK(list->find("c.smc") == 2 && list->find("notes.txt") == -1);
  size_t size = 0;
  char* data = static_cast<char*>(list->read(0, 16, &size));
  CHECK(data && size == 2 && data[0] == 'a');
  free(data);
  CHECK(!list->read(2, 2, &size)); /* too large */
  CHECK(list->load(dir, NULL) && list->count() == 4);
  CHECK(!list->load("/nonexistent/folder", kExtensions) && list->count() == 0);
  CHECK(app::file_name_valid("x.sfc") && !app::file_name_valid("x") &&
        !app::file_name_valid("a/b.c"));
  /* Game folders (`|folders`): one entry each, opened at their main file. */
  char game[160], other[160], loose[160];
  snprintf(game, sizeof(game), "%s/Tekken 3", dir);
  snprintf(other, sizeof(other), "%s/No main", dir);
  CHECK(!mkdir(game, 0700) && !mkdir(other, 0700));
  write_file(game, "Tekken 3 (Track 2).bin", "22");
  write_file(game, "Tekken 3 (Track 1).bin", "1");
  write_file(game, "Tekken 3.cue", "cue");
  write_file(other, "readme.txt", "x");
  snprintf(loose, sizeof(loose), "%s/Loose.bin", dir);
  write_file(dir, "Loose.bin", "4444");
  static const char* const kDiscs[] = {"cue", "bin", NULL};
  static const char* const kMains[] = {"m3u", "cue", "bin", NULL};
  CHECK(list->load(dir, kDiscs) && list->count() == 1); /* no folders without mains */
  CHECK(list->load(dir, kDiscs, kMains) && list->count() == 2);
  CHECK(!strcmp(list->at(0).name, "Loose.bin") && !list->at(0).file[0]);
  CHECK(!strcmp(list->at(1).name, "Tekken 3") && !strcmp(list->at(1).file, "Tekken 3.cue") &&
        list->at(1).size == 6);
  char opened[256];
  CHECK(list->path(1, opened, sizeof(opened)) && strstr(opened, "/Tekken 3/Tekken 3.cue"));
  data = static_cast<char*>(list->read(1, 16, &size));
  CHECK(data && size == 3 && !memcmp(data, "cue", 3));
  free(data);
  static const char* const kBinsOnly[] = {"m3u", "bin", NULL};
  CHECK(list->load(dir, kDiscs, kBinsOnly) && !strcmp(list->at(1).file, "Tekken 3 (Track 1).bin"));
  const char* inner[] = {"Tekken 3/Tekken 3 (Track 2).bin", "Tekken 3/Tekken 3 (Track 1).bin",
                         "Tekken 3/Tekken 3.cue", "No main/readme.txt", "Loose.bin"};
  for (size_t i = 0; i < 5; ++i) {
    char path[200];
    snprintf(path, sizeof(path), "%s/%s", dir, inner[i]);
    unlink(path);
  }
  rmdir(game);
  rmdir(other);
  unlink(link);
  rmdir(sub);
  const char* names[] = {"b game.sfc", "A game.SFC", "c.smc", "notes.txt", ".hidden.sfc"};
  for (size_t i = 0; i < 5; ++i) {
    char path[160];
    snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
    unlink(path);
  }
  rmdir(dir);
  delete list;
  ++g_suites;
}

void test_core_options() {
  static const char* const kRegion[] = {"auto", "auto", "ntsc", "NTSC", "pal", "PAL", NULL};
  static const char* const kBogus[] = {"on", "not-listed", NULL};
  const retro::VariableMap maps[] = {{"region", "core_region", kRegion},
                                     {"turbo", "core_turbo", kBogus}};
  const struct retro_variable declared[] = {{"core_region", "Region; auto|NTSC|PAL"},
                                            {"core_turbo", "Turbo; disabled|enabled"},
                                            {"core_bad", "no semicolon"},
                                            {NULL, NULL}};
  retro::CoreOptions* o = new retro::CoreOptions;
  o->set_maps(maps, 2);
  o->declare(declared);
  CHECK(o->count() == 2 && o->take_update() && !o->take_update());
  CHECK(!strcmp(o->get("core_region"), "auto") && !o->get("core_bad") && !o->get("missing"));
  app::Settings s;
  CHECK(s.set("region", "pal") && s.set("turbo", "on"));
  o->apply(s);
  CHECK(!strcmp(o->get("core_region"), "PAL") && o->take_update());
  CHECK(!strcmp(o->get("core_turbo"), "disabled")); /* a value the core does not list: never */
  o->apply(s);
  CHECK(!o->take_update());
  delete o;
  ++g_suites;
}

void test_menu_and_placement() {
  Menu* menu = new Menu;
  menu->open("Pause");
  menu->add("Resume");
  menu->add("Quit");
  CHECK(menu->key(kMenuUp) == Menu::kNone && menu->focus() == 1);
  CHECK(menu->key(kMenuDown) == Menu::kNone && menu->focus() == 0);
  CHECK(menu->key(kMenuSelect) == Menu::kSelected && menu->is_open());
  CHECK(menu->key(kMenuBack) == Menu::kClosed && !menu->is_open());
  /* A whole files folder, paged by ten and stopping at the ends. */
  menu->open("Games");
  for (int i = 0; i < 513; ++i) menu->add("game.sfc");
  CHECK(menu->count() == 513);
  CHECK(menu->key(kMenuPageUp) == Menu::kNone && menu->focus() == 0);
  menu->key(kMenuPageDown);
  menu->key(kMenuPageDown);
  CHECK(menu->focus() == 20);
  menu->set_focus(508);
  CHECK(menu->key(kMenuPageDown) == Menu::kNone && menu->focus() == 512);
  menu->key(kMenuPageUp);
  CHECK(menu->focus() == 502);
  delete menu;
  /* A held BACK that opened the menu does not close it on release. */
  MenuInput in;
  nbt_controller_wire_state c;
  memset(&c, 0, sizeof(c));
  controller::Event released = {0, NBT_CONTROLLER_BACK, 0};
  in.controller(c, released);
  CHECK(in.next(0) == kMenuNone);
  controller::Event pressed = {NBT_CONTROLLER_BACK, 0, 0};
  in.controller(c, pressed);
  in.controller(c, released);
  CHECK(in.next(0) == kMenuBack);
  controller::Event turn = {0, 0, -2};
  in.controller(c, turn);
  CHECK(in.next(0) == kMenuUp && in.next(0) == kMenuUp && in.next(0) == kMenuNone);
  /* A pad's shoulder buttons page, once seen released. */
  nbt_gamepad_wire_state p;
  memset(&p, 0, sizeof(p));
  p.connected = 1;
  in.gamepad(p, 0);
  p.buttons = NBT_GAMEPAD_RB;
  in.gamepad(p, 10);
  CHECK(in.next(10) == kMenuPageDown && in.next(10) == kMenuNone);
  /* The panel's 1440x540 and a SNES frame of 256x224. */
  FrameOptions o = {kAspectDisplay, kFilterSharp, 4.0f / 3.0f};
  int x, y, w, h;
  FrameVideo::place(1440, 540, 256, 224, o, &x, &y, &w, &h);
  CHECK(w == 720 && h == 540 && x == 360 && y == 0);
  o.aspect = kAspectInteger;
  FrameVideo::place(1440, 540, 256, 224, o, &x, &y, &w, &h);
  CHECK(w == 512 && h == 448 && y == 46);
  o.aspect = kAspectStretch;
  FrameVideo::place(1440, 540, 256, 224, o, &x, &y, &w, &h);
  CHECK(w == 1440 && h == 540);
  /* A vertical arcade game: the frame turned a quarter, shown at 3:4; at an
   * integer scale its height runs across. */
  o.aspect = kAspectDisplay;
  o.display_aspect = 3.0f / 4.0f;
  o.rotation = 1;
  FrameVideo::place(1440, 540, 256, 224, o, &x, &y, &w, &h);
  CHECK(w == 405 && h == 540 && x == 517 && y == 0);
  o.aspect = kAspectInteger;
  o.rotation = 3;
  FrameVideo::place(1440, 540, 256, 224, o, &x, &y, &w, &h);
  CHECK(w == 448 && h == 512 && y == 14);
  /* Its own shape, unturned and turned. */
  o.aspect = kAspectDisplay;
  o.display_aspect = 0.0f;
  o.rotation = 2;
  FrameVideo::place(1440, 540, 256, 224, o, &x, &y, &w, &h);
  CHECK(w == 617 && h == 540);
  o.rotation = 1;
  FrameVideo::place(1440, 540, 256, 224, o, &x, &y, &w, &h);
  CHECK(w == 473 && h == 540);
  ++g_suites;
}

}  // namespace

int main() {
  test_audio_queue();
  test_pad_mapper();
  test_hints();
  test_save_file();
  test_file_list();
  test_core_options();
  test_menu_and_placement();
  printf("ALL %d FRONT END TEST SUITES PASSED\n", g_suites);
  return 0;
}
