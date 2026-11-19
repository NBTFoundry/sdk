// SPDX-License-Identifier: MPL-2.0
/* The libretro host's loop: the bridge, the window, sound, controls, the game
 * list and the pause menu around one statically linked core. */
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "host_state.hpp"
#include "nbt/app/clock.hpp"
#include "nbt/app/log.hpp"
#include "nbt/frontend/hints.hpp"
#include "nbt/frontend/save_file.hpp"

namespace nbt {
namespace retro {

namespace {

enum PauseItem { kResume, kSave, kLoad, kSlot, kReset, kGames, kQuit };
const unsigned kSlots = 4;

struct Events {
  Host* h;
  uint64_t now;
};

void on_controller(void* context, const nbt_controller_wire_state& state,
                   const controller::Event& event) {
  Events* e = static_cast<Events*>(context);
  if (e->h->menu_kind != kMenuClosed) {
    e->h->menu_input.controller(state, event);
  } else {
    e->h->mapper.controller(state, event, e->now);
  }
}

void on_gamepad(void* context, const nbt_gamepad_wire_state& state) {
  Events* e = static_cast<Events*>(context);
  if (e->h->menu_kind != kMenuClosed) e->h->menu_input.gamepad(state, e->now);
  e->h->mapper.gamepad(state, e->now); /* the pad in use, menu or not */
}

/* The core's valid_extensions ("smc|sfc"), when the application lists none. */
void core_extensions(Host& h) {
  size_t n = 0;
  snprintf(h.extensions, sizeof(h.extensions), "%s",
           h.info.valid_extensions ? h.info.valid_extensions : "");
  for (char* p = h.extensions; *p && n < 16;) {
    char* bar = strchr(p, '|');
    const size_t len = bar ? static_cast<size_t>(bar - p) : strlen(p);
    if (len && len < sizeof(h.extension_store[0])) {
      for (size_t i = 0; i < len; ++i) {
        const char c = p[i];
        h.extension_store[n][i] = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
      }
      h.extension_store[n][len] = '\0';
      h.extension_list[n] = h.extension_store[n];
      ++n;
    }
    if (!bar) break;
    p = bar + 1;
  }
  h.extension_list[n] = NULL;
}

void apply_settings(Host& h) {
  const app::Settings& s = h.lifecycle.settings();
  const char* aspect = s.get("aspect", "4-3");
  h.frame_options.aspect = !strcmp(aspect, "pixel")  ? frontend::kAspectInteger
                           : !strcmp(aspect, "full") ? frontend::kAspectStretch
                                                     : frontend::kAspectDisplay;
  const char* filter = s.get("filter", "sharp");
  h.frame_options.filter = !strcmp(filter, "smooth")    ? frontend::kFilterSmooth
                           : !strcmp(filter, "nearest") ? frontend::kFilterNearest
                                                        : frontend::kFilterSharp;
  h.show_fps = !strcmp(s.get("show_fps", "off"), "on");
  const char* skip = s.get("frameskip", "off");
  h.frameskip = !strcmp(skip, "auto") ? -1 : !strcmp(skip, "1") ? 1 : !strcmp(skip, "2") ? 2 : 0;
  app::log_set_debug(!strcmp(s.get("debug_log", "off"), "on"));
  const bool sound = strcmp(s.get("sound", "on"), "off") != 0;
  if (sound != h.sound) {
    h.sound = sound;
    h.audio.clear();
    h.pcm.set_route_wanted(sound);
  }
  h.options.apply(s);
}

void open_pause(Host& h) {
  char text[64];
  h.mapper.release_all();
  h.menu_input.reset();
  h.audio.clear();
  h.menu_kind = kMenuPause;
  h.menu.open(h.game);
  h.menu.add("Resume");
  snprintf(text, sizeof(text), "Save state (slot %u)", h.slot);
  h.menu.add(text);
  snprintf(text, sizeof(text), "Load state (slot %u)", h.slot);
  h.menu.add(text);
  snprintf(text, sizeof(text), "Slot: %u", h.slot);
  h.menu.add(text);
  h.menu.add("Reset");
  h.menu.add("Choose another game");
  h.menu.add("Quit");
  const int family = h.mapper.pad_family();
  snprintf(text, sizeof(text), "%s: %s to choose, %s to go back", frontend::input_name(family),
           frontend::confirm_name(family), frontend::cancel_name(family));
  h.menu.set_note(text);
}

void open_games(Host& h) {
  char title[96], last[97] = "";
  h.mapper.release_all();
  h.menu_input.reset();
  h.audio.clear();
  h.files.load(h.lifecycle.files_dir(),
               h.config.extensions ? h.config.extensions : h.extension_list);
  h.menu_kind = kMenuGames;
  snprintf(title, sizeof(title), "%s: choose a game", h.config.system ? h.config.system : "Games");
  h.menu.open(title);
  for (size_t i = 0; i < h.files.count() && i < frontend::Menu::kMaxItems - 1; ++i) {
    h.menu.add(h.files.at(i).name);
  }
  if (!h.files.count()) {
    h.menu.add("Quit");
    h.menu.set_note("No games yet. Send them from the NBT Manager phone app,",
                    "or import them from a USB stick: hold this app > Files.");
    return;
  }
  char path[768];
  size_t size = 0;
  void* text = data_path(h, ".", "last-game", path, sizeof(path))
                   ? frontend::save_file_read(path, sizeof(last) - 1, &size)
                   : NULL;
  if (text) {
    memcpy(last, text, size);
    free(text);
    const int found = h.files.find(last);
    if (found >= 0) h.menu.set_focus(static_cast<size_t>(found));
  }
  char note[64];
  snprintf(note, sizeof(note), "%u games", static_cast<unsigned>(h.files.count()));
  h.menu.set_note(note);
}

void pause_choice(Host& h, uint64_t now) {
  switch (h.menu.focus()) {
    case kResume:
      h.menu.close();
      break;
    case kSave:
      notice(h, now, save_state(h) ? "State saved in slot %u" : "Cannot save slot %u", h.slot);
      h.menu.close();
      break;
    case kLoad:
      notice(h, now, load_state(h) ? "State of slot %u loaded" : "Slot %u is empty", h.slot);
      h.menu.close();
      break;
    case kSlot: {
      h.slot = h.slot % kSlots + 1;
      const size_t focus = h.menu.focus();
      open_pause(h);
      h.menu.set_focus(focus);
      return;
    }
    case kReset:
      retro_reset();
      h.menu.close();
      break;
    case kGames:
      unload_game(h);
      open_games(h);
      return;
    default:
      h.quit = true;
      break;
  }
  if (!h.menu.is_open()) h.menu_kind = kMenuClosed;
}

void menu_keys(Host& h, uint64_t now) {
  for (frontend::MenuKey key; (key = h.menu_input.next(now)) != frontend::kMenuNone;) {
    const frontend::Menu::Result r = h.menu.key(key);
    if (r == frontend::Menu::kClosed) { /* back: to the game, or the list stays */
      if (h.loaded) {
        h.menu_kind = kMenuClosed;
      } else {
        open_games(h); /* no game: the list stays */
      }
      return;
    }
    if (r != frontend::Menu::kSelected) continue;
    if (h.menu_kind == kMenuPause) {
      pause_choice(h, now);
    } else if (!h.files.count()) {
      h.quit = true;
    } else if (load_game(h, h.menu.focus())) {
      h.menu.close();
      h.menu_kind = kMenuClosed;
    }
    return;
  }
}

void input(Host& h, uint64_t now) {
  Events events = {&h, now};
  h.controller.poll(h.lifecycle.controller_fd(), &on_controller, &events);
  h.gamepad.poll(h.lifecycle.gamepad_fd(), now, &on_gamepad, &events);
  app::HostEvent ev;
  while (h.video.poll_event(&ev)) {
    if (ev.type == app::HostEvent::kClose) h.quit = true;
    if (ev.type != app::HostEvent::kKeyDown && ev.type != app::HostEvent::kKeyUp) continue;
    const bool down = ev.type == app::HostEvent::kKeyDown;
    if (h.menu_kind != kMenuClosed) {
      h.menu_input.host_key(ev.key, down);
    } else {
      h.mapper.host_key(ev.key, down);
    }
  }
  const frontend::Command command = h.mapper.poll(now);
  if (h.mapper.take_pad_change()) {
    const int family = h.mapper.pad_family();
    if (family >= 0) {
      notice(h, now, "%s connected", frontend::input_name(family));
    } else {
      notice(h, now, "Gamepad unplugged: the iDrive controller plays");
    }
  }
  if (command == frontend::kCommandQuit) h.quit = true;
  if (command == frontend::kCommandMenu && h.menu_kind == kMenuClosed && h.loaded) open_pause(h);
  if (h.menu_kind != kMenuClosed) menu_keys(h, now);
}

void draw(Host& h, uint64_t now) {
  const int w = h.video.window_width(), wh = h.video.window_height();
  h.frame.draw(w, wh, h.frame_options);
  h.overlay.begin(w, wh);
  const int scale = frontend::Overlay::scale_for(wh), line = frontend::Overlay::kGlyph * scale;
  if (h.show_fps && h.menu_kind == kMenuClosed) {
    char fps[16];
    snprintf(fps, sizeof(fps), "%u fps", h.fps);
    const int tw = frontend::Overlay::text_width(fps, scale);
    h.overlay.box(w - tw - line, line / 2, tw + line / 2, line + line / 2, 0x00000099u);
    h.overlay.text(w - tw - line * 3 / 4, line * 3 / 4, scale, fps, 0xffffffffu);
  }
  h.menu.draw(h.overlay, w, wh);
  if (h.notice[0] && now < h.notice_until_ms) {
    const int tw = frontend::Overlay::text_width(h.notice, scale);
    const int x = (w - tw) / 2, y = wh - 3 * line;
    h.overlay.box(x - line, y - line / 2, tw + 2 * line, 2 * line, 0x000000c0u);
    h.overlay.text(x, y, scale, h.notice, 0xffffffffu);
  }
  h.overlay.end();
  h.video.swap();
}

/* Whether the frame about to run is drawn: a fixed frame skip draws one in
 * frameskip + 1; auto skips while the loop runs over a frame late, at most 3
 * in a row. A core that ignores GET_AUDIO_VIDEO_ENABLE draws them all. */
bool draw_this_frame(Host& h, uint64_t now) {
  bool skip = false;
  if (h.frameskip > 0) {
    skip = h.skipped < static_cast<unsigned>(h.frameskip);
  } else if (h.frameskip < 0) {
    skip = h.skipped < 3 && static_cast<double>(now) > h.next_frame_ms + h.frame_ms;
  }
  h.skipped = skip ? h.skipped + 1 : 0;
  return !skip;
}

/* Runs the core's frames due, and returns how many it drew; a queue of sound
 * well ahead holds them back. */
int run_frames(Host& h, uint64_t now) {
  int drawn = 0;
  if (!h.loaded || h.menu_kind != kMenuClosed) {
    h.next_frame_ms = static_cast<double>(now);
    return 0;
  }
  if (now - h.fps_since_ms >= 1000) {
    h.fps = h.frames_this_second;
    h.frames_this_second = 0;
    h.fps_since_ms = now;
  }
  if (static_cast<double>(now) > h.next_frame_ms + 5 * h.frame_ms) {
    h.next_frame_ms = static_cast<double>(now); /* fell behind: start again from now */
  }
  for (int n = 0; n < 2 && static_cast<double>(now) >= h.next_frame_ms; ++n) {
    if (h.sound && h.pcm.active() && h.audio.fill() > 2 * h.audio.target()) break;
    h.video_on = draw_this_frame(h, now);
    retro_run();
    if (h.video_on) ++drawn;
    ++h.frames_this_second;
    h.next_frame_ms += h.frame_ms;
  }
  h.video_on = true;
  save_sram(h, false);
  return drawn;
}

int check_mode(Host& h) {
  app::log_stage("check");
  if (!h.lifecycle.send_ready()) return 1;
  while (!h.lifecycle.stopping()) {
    usleep(200000);
    h.lifecycle.tick(app::monotonic_ms(), NULL, NULL);
  }
  return 0;
}

int play(Host& h) {
  const app::VideoConfig window = {h.config.title ? h.config.title : "NBT", 1080, 405};
  app::log_stage("video");
  if (!h.video.init(h.lifecycle.surface_id(), &window) || !h.frame.init() || !h.overlay.init()) {
    app::log_info("video unavailable");
    return 1;
  }
  apply_settings(h);
  h.pcm.open(48000, &frontend::AudioQueue::render_callback, &h.audio, h.sound);
  app::log_stage("core");
  install_callbacks();
  retro_init();
  retro_get_system_info(&h.info);
  core_extensions(h);
  app::log_info("core %s %s, files in %s", h.info.library_name ? h.info.library_name : "?",
                h.info.library_version ? h.info.library_version : "?",
                h.lifecycle.files_dir()[0] ? h.lifecycle.files_dir() : "(none)");
  if (!h.lifecycle.send_ready()) return 1;
  open_games(h);
  while (!h.lifecycle.stopping() && !h.quit) {
    const uint64_t now = app::monotonic_ms();
    input(h, now);
    /* A new frame, or the menus, are drawn; a swap waits for the panel. */
    if (run_frames(h, now) || !h.loaded || h.menu_kind != kMenuClosed) {
      draw(h, now);
    } else {
      usleep(1000);
    }
    bool changed = false;
    char status[25];
    snprintf(status, sizeof(status), "%.24s", h.loaded ? h.game : "Choosing a game");
    char* dot = h.loaded ? strrchr(status, '.') : NULL;
    if (dot) *dot = '\0';
    h.lifecycle.tick(now, status, &changed);
    if (changed) apply_settings(h);
    (void)h.lifecycle.consume_show_requested();
    if (!h.loaded || h.menu_kind != kMenuClosed) usleep(8000);
  }
  unload_game(h);
  retro_deinit();
  return 0;
}

}  // namespace

Host::Host(const HostConfig& c)
    : config(c),
      mapper(*c.profiles),
      format(frontend::kRgb565),
      loaded(false),
      content(NULL),
      sram_hash(0),
      sram_checked_ms(0),
      slot(1),
      menu_kind(kMenuClosed),
      sound(true),
      show_fps(false),
      frameskip(0),
      skipped(0),
      video_on(true),
      frame_ms(1000.0 / 60.0),
      next_frame_ms(0),
      frames_this_second(0),
      fps(0),
      fps_since_ms(0),
      notice_until_ms(0),
      quit(false) {
  memset(&info, 0, sizeof(info));
  memset(&av, 0, sizeof(av));
  extensions[0] = game[0] = notice[0] = '\0';
  extension_list[0] = NULL;
  frame_options.aspect = frontend::kAspectDisplay;
  frame_options.filter = frontend::kFilterSharp;
  frame_options.display_aspect = c.display_aspect;
}

int run(int argc, char** argv, const HostConfig& config) {
  app::log_init(config.tag ? config.tag : "retro");
  if (!config.profiles) return 2;
  Host* h = new (std::nothrow) Host(config);
  if (!h) return 1;
  int rc = h->lifecycle.init(argc, argv);
  if (rc == 0) {
    app::log_install_crash_handlers();
    set_host(h);
    h->options.set_maps(config.variables, config.variable_count);
    rc = h->lifecycle.is_check_mode() ? check_mode(*h) : play(*h);
    set_host(NULL);
    h->pcm.close();
    h->overlay.shutdown();
    h->frame.shutdown();
    h->video.shutdown();
    h->lifecycle.close_fds();
  }
  delete h;
  return rc;
}

}  // namespace retro
}  // namespace nbt
