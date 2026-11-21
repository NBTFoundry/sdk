// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_APP_AUDIO_A2I_HPP
#define NBT_APP_AUDIO_A2I_HPP

#include <stdint.h>

namespace nbt {
namespace app {

/* The parameters A2I_Client_Open takes (liba2i_client, NBTevo firmware), as
 * the head unit's media player fills them for io-amp's input_stereo_0. The
 * layout is the 32-bit head unit's: a host build only talks to a test
 * double built from this same header. */
struct A2iParams {
  uint32_t magic;        /* 0x02000000 */
  uint32_t direction;    /* 0: playback */
  uint32_t one;          /* 1 */
  uint32_t buffers;      /* number of buffers (the media player: 8) */
  uint32_t buffer_bytes; /* a power of two, at least 32 */
  uint32_t channels;     /* 2 */
  uint32_t sample_bytes; /* 2: 16-bit */
  uint32_t rate;         /* Hz (io-amp runs at 44100) */
  uint32_t zero_20;
  const char* endpoint; /* io-amp input, e.g. "input_stereo_0" */
  uint32_t stereo;      /* 1 for two channels */
  uint32_t zero_2c;
  uint32_t format;     /* 0: 16-bit */
  uint32_t cookie;     /* 0x05555555 */
  uint32_t timeout_ms; /* twice the whole buffer's duration */
  uint32_t reserved[5];
};

enum {
  kA2iMagic = 0x02000000u,
  kA2iCookie = 0x05555555u,
  kA2iNoBuffer = 0x14, /* A2I_Client_GetBuffer: every buffer queued */
  kA2iRate = 44100,
  kA2iBufferBytes = 2048,
  kA2iBuffers = 8
};

/* Fills `params` for 16-bit stereo playback to `endpoint`. */
void a2i_fill_params(A2iParams* params, const char* endpoint, int rate, int buffer_bytes,
                     int buffers);
/* An io-amp input name: 1..31 characters of [a-z0-9_]. */
bool a2i_endpoint_valid(const char* endpoint);

/* Playback straight into one of io-amp's inputs through the head unit's
 * liba2i_client (loaded at run time: NBT_A2I_LIB, else
 * /opt/mm/lib/liba2i_client.so), the way its media player and dev-libasound
 * write them. Blocking writes of 16-bit stereo; one thread. */
class A2iOutput {
 public:
  A2iOutput();
  ~A2iOutput();

  /* Opens `endpoint`, fills every buffer with silence and starts playback. */
  bool open(const char* endpoint, int rate, int buffer_bytes, int buffers);
  /* Queues `frames` stereo frames, waiting while every buffer is queued. */
  bool write(const int16_t* lr, int frames);
  void close();
  bool is_open() const { return handle_ != 0; }

 private:
  A2iOutput(const A2iOutput&);
  A2iOutput& operator=(const A2iOutput&);

  bool load();
  /* The next free buffer into cur_ (waiting when `wait`); false when none. */
  bool next_buffer(bool wait);
  bool flush_buffer();

  void* lib_;
  void* handle_;
  void* fn_[7];
  uint8_t* cur_;
  uint32_t cur_size_;
  uint32_t cur_fill_;
  uint32_t errors_;
};

}  // namespace app
}  // namespace nbt

#endif /* NBT_APP_AUDIO_A2I_HPP */
