// SPDX-License-Identifier: MPL-2.0
/* The libretro host's state, shared by its parts: host.cpp (the loop and its
 * menus), session.cpp (a game: loading, saves, states) and environment.cpp
 * (the core's callbacks). One per process: libretro's callbacks take no
 * context, so they reach it through host(). */
#ifndef NBT_RETRO_HOST_STATE_HPP
#define NBT_RETRO_HOST_STATE_HPP

#include <stddef.h>
#include <stdint.h>

#include "nbt/app/audio_pcm.hpp"
#include "nbt/app/files.hpp"
#include "nbt/app/lifecycle.hpp"
#include "nbt/app/video.hpp"
#include "nbt/controller/input.hpp"
#include "nbt/frontend/audio_queue.hpp"
#include "nbt/frontend/frame_video.hpp"
#include "nbt/frontend/menu.hpp"
#include "nbt/frontend/overlay.hpp"
#include "nbt/frontend/pad_profiles.hpp"
#include "nbt/gamepad/input.hpp"
#include "nbt/retro/core_options.hpp"
#include "nbt/retro/host.hpp"
#include "libretro.h" /* third_party/libretro-common */

namespace nbt {
namespace retro {

enum MenuKind { kMenuClosed, kMenuPause, kMenuGames };

struct Host {
  explicit Host(const HostConfig& c);

  const HostConfig& config;
  app::Lifecycle lifecycle;
  app::VideoEgl video;
  app::AudioPcm pcm;
  frontend::FrameVideo frame;
  frontend::Overlay overlay;
  frontend::AudioQueue audio;
  frontend::PadMapper mapper;
  frontend::MenuInput menu_input;
  frontend::Menu menu;
  controller::Input controller;
  gamepad::Input gamepad;
  CoreOptions options;
  app::FileList files;

  /* The core. */
  struct retro_system_info info;
  struct retro_system_av_info av;
  frontend::PixelFormat format;
  char extensions[128]; /* the core's valid_extensions, '|' separated */
  const char* extension_list[17];
  char extension_store[16][9];

  /* The game. */
  bool loaded;
  char game[97]; /* its file name */
  void* content;
  uint64_t sram_hash;
  uint64_t sram_checked_ms;
  unsigned slot;

  /* The front end. */
  MenuKind menu_kind;
  frontend::FrameOptions frame_options;
  bool sound, show_fps;
  double frame_ms, next_frame_ms;
  unsigned frames_this_second, fps;
  uint64_t fps_since_ms;
  char notice[96];
  uint64_t notice_until_ms;
  bool quit;
};

Host* host();
void set_host(Host* h);

/* environment.cpp: the core's callbacks. */
void install_callbacks();
/* session.cpp: the game. */
bool load_game(Host& h, size_t index);
void unload_game(Host& h);
void save_sram(Host& h, bool force);
bool save_state(Host& h);
bool load_state(Host& h);
void notice(Host& h, uint64_t now_ms, const char* fmt, ...);
/* The data folder's sub-folder `name`, created when absent: "<data>/<name>". */
bool data_path(const Host& h, const char* name, const char* file, char* out, size_t size);

}  // namespace retro
}  // namespace nbt

#endif /* NBT_RETRO_HOST_STATE_HPP */
