// SPDX-License-Identifier: MPL-2.0
/* A game: loaded from the files folder, its battery save kept in
 * <data>/saves/<game>.srm (written when it changes, at most every 10 s, and
 * on leaving), its states in <data>/states/<game>.state<slot>. */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "host_state.hpp"
#include "nbt/app/clock.hpp"
#include "nbt/app/log.hpp"
#include "nbt/frontend/save_file.hpp"

namespace nbt {
namespace retro {

namespace {

const uint64_t kSramCheckMs = 10000;

uint64_t fnv1a(const void* data, size_t size) {
  const unsigned char* p = static_cast<const unsigned char*>(data);
  uint64_t hash = UINT64_C(14695981039346656037);
  for (size_t i = 0; i < size; ++i) hash = (hash ^ p[i]) * UINT64_C(1099511628211);
  return hash;
}

}  // namespace

void notice(Host& h, uint64_t now_ms, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(h.notice, sizeof(h.notice), fmt, ap);
  va_end(ap);
  h.notice_until_ms = now_ms + 3000;
  app::log_info("%s", h.notice);
}

bool data_path(const Host& h, const char* name, const char* file, char* out, size_t size) {
  const char* data = h.lifecycle.data_dir();
  if (!data[0]) data = ".";
  char folder[512];
  if (snprintf(folder, sizeof(folder), "%s/%s", data, name) >= static_cast<int>(sizeof(folder))) {
    return false;
  }
  if (mkdir(folder, 0700) && errno != EEXIST) return false;
  const int n = snprintf(out, size, "%s/%s", folder, file);
  return n > 0 && static_cast<size_t>(n) < size;
}

void save_sram(Host& h, bool force) {
  const size_t size = h.loaded ? retro_get_memory_size(RETRO_MEMORY_SAVE_RAM) : 0;
  void* data = size ? retro_get_memory_data(RETRO_MEMORY_SAVE_RAM) : NULL;
  if (!data) return;
  const uint64_t now = app::monotonic_ms();
  if (!force && now - h.sram_checked_ms < kSramCheckMs) return;
  h.sram_checked_ms = now;
  const uint64_t hash = fnv1a(data, size);
  if (hash == h.sram_hash) return;
  char file[128], path[768];
  snprintf(file, sizeof(file), "%s.srm", h.game);
  if (data_path(h, "saves", file, path, sizeof(path)) &&
      frontend::save_file_write(path, data, size)) {
    h.sram_hash = hash;
    app::log_debug("battery save written (%zu bytes)", size);
  } else {
    app::log_info("battery save not written: %s", path);
  }
}

bool load_game(Host& h, size_t index) {
  if (index >= h.files.count()) return false;
  unload_game(h);
  const app::FileEntry& entry = h.files.at(index);
  char path[768];
  if (!h.files.path(index, path, sizeof(path))) return false;
  struct retro_game_info game;
  memset(&game, 0, sizeof(game));
  game.path = path;
  if (!h.info.need_fullpath) {
    size_t size = 0;
    h.content = h.files.read(index, h.config.max_file_bytes, &size);
    if (!h.content) {
      notice(h, app::monotonic_ms(), "Cannot read %s", entry.name);
      return false;
    }
    game.data = h.content;
    game.size = size;
  }
  app::log_stage("load game");
  if (!retro_load_game(&game)) {
    free(h.content);
    h.content = NULL;
    notice(h, app::monotonic_ms(), "Cannot start %s", entry.name);
    return false;
  }
  h.loaded = true;
  snprintf(h.game, sizeof(h.game), "%s", entry.name);
  retro_get_system_av_info(&h.av);
  h.frame_ms = h.av.timing.fps > 1.0 ? 1000.0 / h.av.timing.fps : 1000.0 / 60.0;
  h.next_frame_ms = static_cast<double>(app::monotonic_ms());
  h.audio.configure(h.av.timing.sample_rate, h.pcm.sample_rate());
  /* Its battery save, when there is one of the right size. */
  const size_t sram = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
  void* data = sram ? retro_get_memory_data(RETRO_MEMORY_SAVE_RAM) : NULL;
  char file[128], save[768];
  snprintf(file, sizeof(file), "%s.srm", h.game);
  if (data && data_path(h, "saves", file, save, sizeof(save)) &&
      frontend::save_file_read_into(save, data, sram)) {
    app::log_info("battery save loaded");
  }
  h.sram_hash = data ? fnv1a(data, sram) : 0;
  h.sram_checked_ms = app::monotonic_ms();
  /* The next start offers it first. */
  if (data_path(h, ".", "last-game", save, sizeof(save))) {
    frontend::save_file_write(save, h.game, strlen(h.game));
  }
  app::log_info("game %s: %ux%u, %.3f fps, %.0f Hz", h.game, h.av.geometry.base_width,
                h.av.geometry.base_height, h.av.timing.fps, h.av.timing.sample_rate);
  app::log_stage("run");
  return true;
}

void unload_game(Host& h) {
  if (!h.loaded) return;
  save_sram(h, true);
  retro_unload_game();
  free(h.content);
  h.content = NULL;
  h.loaded = false;
  h.game[0] = '\0';
  h.audio.clear();
}

bool save_state(Host& h) {
  const size_t size = h.loaded ? retro_serialize_size() : 0;
  void* data = size ? malloc(size) : NULL;
  char file[128], path[768];
  snprintf(file, sizeof(file), "%s.state%u", h.game, h.slot);
  const bool ok = data && retro_serialize(data, size) &&
                  data_path(h, "states", file, path, sizeof(path)) &&
                  frontend::save_file_write(path, data, size);
  free(data);
  return ok;
}

bool load_state(Host& h) {
  const size_t size = h.loaded ? retro_serialize_size() : 0;
  char file[128], path[768];
  snprintf(file, sizeof(file), "%s.state%u", h.game, h.slot);
  size_t read = 0;
  void* data = size && data_path(h, "states", file, path, sizeof(path))
                   ? frontend::save_file_read(path, size * 2, &read)
                   : NULL;
  const bool ok = data && retro_unserialize(data, read);
  free(data);
  return ok;
}

}  // namespace retro
}  // namespace nbt
