// SPDX-License-Identifier: MPL-2.0
/* A libretro core for the host's tests (tests/test_retro.py): a 256x224
 * RGB565 test pattern, a 440 Hz tone at 32 kHz, 8 KiB of battery save that
 * counts frames, a serializable state. It prints one event per line on
 * stdout:
 *
 *   init | deinit | reset
 *   load <path> size=<n>
 *   system <directory>    (GET_SYSTEM_DIRECTORY, after load)
 *   rotation <on|refused> (SET_ROTATION, a quarter turn: a game starting "TURN")
 *   unload
 *   disc eject=<0|1> | disc index=<n>   (a game starting "DISCS" has two)
 *   input mask=<hex>      (when port 0's joypad changes)
 *   option core_region=<value>
 *   serialize | unserialize frame=<n>
 *   drawn <n> of 120      (every 120 frames: those the host had it draw) */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "libretro.h"

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_cb;
static retro_input_poll_t poll_cb;
static retro_input_state_t input_cb;

static uint16_t frame_buffer[256 * 224];
static unsigned char sram[8192];
static unsigned frame, counted, drawn;
static int16_t last_mask = -1;
static char region[32];
static unsigned disc_count = 1, disc_index;
static bool disc_ejected;

static void say(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  putchar('\n');
  fflush(stdout);
}

static bool RETRO_CALLCONV set_eject(bool ejected) {
  disc_ejected = ejected;
  say("disc eject=%d", ejected ? 1 : 0);
  return true;
}
static bool RETRO_CALLCONV get_eject(void) {
  return disc_ejected;
}
static unsigned RETRO_CALLCONV get_index(void) {
  return disc_index;
}
static bool RETRO_CALLCONV set_index(unsigned index) {
  if (!disc_ejected || index >= disc_count) return false;
  disc_index = index;
  say("disc index=%u", index);
  return true;
}
static unsigned RETRO_CALLCONV get_count(void) {
  return disc_count;
}
static bool RETRO_CALLCONV get_label(unsigned index, char* label, size_t len) {
  snprintf(label, len, "Side %c", 'A' + (int)index);
  return true;
}

void retro_set_environment(retro_environment_t cb) {
  static const struct retro_variable variables[] = {{"core_region", "Region; auto|NTSC|PAL"},
                                                    {NULL, NULL}};
  static const struct retro_disk_control_ext_callback disk = {
      set_eject, get_eject, get_index, set_index, get_count, NULL, NULL, NULL, NULL, get_label};
  unsigned version = 0;
  environ_cb = cb;
  cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)variables);
  if (cb(RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION, &version) && version >= 1) {
    cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE, (void*)&disk);
  }
}
void retro_set_video_refresh(retro_video_refresh_t cb) {
  video_cb = cb;
}
void retro_set_audio_sample(retro_audio_sample_t cb) {
  (void)cb;
}
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
  audio_cb = cb;
}
void retro_set_input_poll(retro_input_poll_t cb) {
  poll_cb = cb;
}
void retro_set_input_state(retro_input_state_t cb) {
  input_cb = cb;
}

void retro_init(void) {
  enum retro_pixel_format format = RETRO_PIXEL_FORMAT_RGB565;
  environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format);
  say("init");
}
void retro_deinit(void) {
  say("deinit");
}
unsigned retro_api_version(void) {
  return RETRO_API_VERSION;
}

void retro_get_system_info(struct retro_system_info* info) {
  memset(info, 0, sizeof(*info));
  info->library_name = "fake";
  info->library_version = "1";
  info->valid_extensions = "fake|FK";
  info->need_fullpath = false;
}

void retro_get_system_av_info(struct retro_system_av_info* info) {
  memset(info, 0, sizeof(*info));
  info->geometry.base_width = info->geometry.max_width = 256;
  info->geometry.base_height = info->geometry.max_height = 224;
  info->geometry.aspect_ratio = 4.0f / 3.0f;
  info->timing.fps = 60.0;
  info->timing.sample_rate = 32000.0;
}

void retro_set_controller_port_device(unsigned port, unsigned device) {
  (void)port;
  (void)device;
}

void retro_reset(void) {
  frame = 0;
  say("reset");
}

void retro_run(void) {
  struct retro_variable v = {"core_region", NULL};
  bool updated = false;
  int16_t samples[2 * 534];
  int16_t mask;
  int av = 3;
  unsigned i;
  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated &&
      environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value && strcmp(v.value, region)) {
    snprintf(region, sizeof(region), "%s", v.value);
    say("option core_region=%s", region);
  }
  poll_cb();
  mask = input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
  if (mask != last_mask) {
    last_mask = mask;
    say("input mask=%04x", (unsigned)(uint16_t)mask);
  }
  /* Frame skip: a frame the host does not draw is not rendered. */
  if (!environ_cb(RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &av) || (av & 1)) {
    for (i = 0; i < 256 * 224; ++i) frame_buffer[i] = (uint16_t)((i + frame) * 0x0841u);
    video_cb(frame_buffer, 256, 224, 256 * 2);
    ++drawn;
  }
  if (++counted == 120) {
    say("drawn %u of 120", drawn);
    counted = drawn = 0;
  }
  for (i = 0; i < 534; ++i) samples[2 * i] = samples[2 * i + 1] = (int16_t)((i % 73) * 200);
  audio_cb(samples, 534);
  ++frame;
  memcpy(sram, &frame, sizeof(frame));
}

size_t retro_serialize_size(void) {
  return sizeof(frame) + 60;
}

bool retro_serialize(void* data, size_t size) {
  if (size < sizeof(frame)) return false;
  memset(data, 0, size);
  memcpy(data, &frame, sizeof(frame));
  say("serialize");
  return true;
}

bool retro_unserialize(const void* data, size_t size) {
  if (size < sizeof(frame)) return false;
  memcpy(&frame, data, sizeof(frame));
  say("unserialize frame=%u", frame);
  return true;
}

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char* code) {
  (void)index;
  (void)enabled;
  (void)code;
}

bool retro_load_game(const struct retro_game_info* game) {
  const char* system = NULL;
  if (!game || !game->data) return false;
  say("load %s size=%u", game->path, (unsigned)game->size);
  if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system) && system) say("system %s", system);
  if (game->size >= 4 && memcmp(game->data, "TURN", 4) == 0) {
    unsigned turns = 1;
    say("rotation %s", environ_cb(RETRO_ENVIRONMENT_SET_ROTATION, &turns) ? "on" : "refused");
  }
  disc_count = game->size >= 5 && memcmp(game->data, "DISCS", 5) == 0 ? 2 : 1;
  disc_index = 0;
  disc_ejected = false;
  return game->size > 0 && memcmp(game->data, "BAD", 3) != 0;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info* info, size_t num) {
  (void)type;
  (void)info;
  (void)num;
  return false;
}

void retro_unload_game(void) {
  say("unload");
}
unsigned retro_get_region(void) {
  return RETRO_REGION_NTSC;
}

void* retro_get_memory_data(unsigned id) {
  return id == RETRO_MEMORY_SAVE_RAM ? sram : NULL;
}
size_t retro_get_memory_size(unsigned id) {
  return id == RETRO_MEMORY_SAVE_RAM ? sizeof(sram) : 0;
}
