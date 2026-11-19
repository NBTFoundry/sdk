// SPDX-License-Identifier: MPL-2.0
/* The libretro callbacks: the environment the core asks about, its frames,
 * sound and controls. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "host_state.hpp"
#include "nbt/app/clock.hpp"
#include "nbt/app/log.hpp"

namespace nbt {
namespace retro {

namespace {

Host* g_host = NULL;

void RETRO_CALLCONV core_log(enum retro_log_level level, const char* fmt, ...) {
  char line[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  line[strcspn(line, "\n")] = '\0';
  if (level >= RETRO_LOG_INFO) {
    app::log_info("core: %s", line);
  } else {
    app::log_debug("core: %s", line);
  }
}

bool set_format(Host& h, enum retro_pixel_format format) {
  switch (format) {
    case RETRO_PIXEL_FORMAT_RGB565:
      h.format = frontend::kRgb565;
      return true;
    case RETRO_PIXEL_FORMAT_XRGB8888:
      h.format = frontend::kXrgb8888;
      return true;
    case RETRO_PIXEL_FORMAT_0RGB1555:
      h.format = frontend::k0rgb1555;
      return true;
    default:
      return false;
  }
}

bool RETRO_CALLCONV environment(unsigned cmd, void* data) {
  Host* h = g_host;
  if (!h) return false;
  switch (cmd) {
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
      *static_cast<bool*>(data) = true;
      return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE: {
      const struct retro_message* m = static_cast<const struct retro_message*>(data);
      if (m && m->msg) notice(*h, app::monotonic_ms(), "%s", m->msg);
      return true;
    }
    case RETRO_ENVIRONMENT_SHUTDOWN:
      h->quit = true;
      return true;
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
      return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
      *static_cast<const char**>(data) = h->lifecycle.data_dir()[0] ? h->lifecycle.data_dir() : ".";
      return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      return set_format(*h, *static_cast<const enum retro_pixel_format*>(data));
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
      struct retro_variable* v = static_cast<struct retro_variable*>(data);
      v->value = h->options.get(v->key);
      return v->value != NULL;
    }
    case RETRO_ENVIRONMENT_SET_VARIABLES:
      h->options.declare(static_cast<const struct retro_variable*>(data));
      h->options.apply(h->lifecycle.settings());
      return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      *static_cast<bool*>(data) = h->options.take_update();
      return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      static_cast<struct retro_log_callback*>(data)->log = core_log;
      return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
      h->av = *static_cast<const struct retro_system_av_info*>(data);
      if (h->av.timing.fps > 1.0) h->frame_ms = 1000.0 / h->av.timing.fps;
      h->audio.configure(h->av.timing.sample_rate, h->pcm.sample_rate());
      return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
      h->av.geometry = *static_cast<const struct retro_game_geometry*>(data);
      return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
      *static_cast<unsigned*>(data) = RETRO_LANGUAGE_ENGLISH;
      return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
      return true;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
      /* Bit 0 video (off for a frame skipped), bit 1 audio. */
      *static_cast<int*>(data) = (h->video_on ? 1 : 0) | (h->sound ? 2 : 0);
      return true;
    case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
      *static_cast<bool*>(data) = false;
      return true;
    case RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS:
      *static_cast<unsigned*>(data) = 1;
      return true;
    default:
      /* Core options v1/v2 (GET_CORE_OPTIONS_VERSION unanswered: cores then
       * declare plain variables), hardware rendering, the rest: not offered. */
      return false;
  }
}

void RETRO_CALLCONV video_refresh(const void* data, unsigned width, unsigned height, size_t pitch) {
  Host* h = g_host;
  if (h && data && data != RETRO_HW_FRAME_BUFFER_VALID) {
    h->frame.upload(data, static_cast<int>(width), static_cast<int>(height), pitch, h->format);
  }
}

void RETRO_CALLCONV audio_sample(int16_t left, int16_t right) {
  if (g_host && g_host->sound && g_host->pcm.active()) {
    const int16_t lr[2] = {left, right};
    g_host->audio.push(lr, 1);
  }
}

size_t RETRO_CALLCONV audio_sample_batch(const int16_t* data, size_t frames) {
  if (g_host && g_host->sound && g_host->pcm.active()) g_host->audio.push(data, frames);
  return frames;
}

void RETRO_CALLCONV input_poll() {}

int16_t RETRO_CALLCONV input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
  (void)index;
  Host* h = g_host;
  if (!h || port != 0 || (device & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD ||
      h->menu_kind != kMenuClosed) {
    return 0;
  }
  if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
    int16_t mask = 0;
    for (int b = 0; b < 16; ++b) {
      if (h->mapper.held(b)) mask = static_cast<int16_t>(mask | (1 << b));
    }
    return mask;
  }
  return id < 16 && h->mapper.held(static_cast<int>(id)) ? 1 : 0;
}

}  // namespace

Host* host() {
  return g_host;
}

void set_host(Host* h) {
  g_host = h;
}

void install_callbacks() {
  retro_set_environment(environment);
  retro_set_video_refresh(video_refresh);
  retro_set_audio_sample(audio_sample);
  retro_set_audio_sample_batch(audio_sample_batch);
  retro_set_input_poll(input_poll);
  retro_set_input_state(input_state);
}

}  // namespace retro
}  // namespace nbt
