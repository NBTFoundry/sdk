// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_APP_AUDIO_PCM_HPP
#define NBT_APP_AUDIO_PCM_HPP

#include <pthread.h>
#include <stdint.h>

#include "nbt/app/audio_a2i.hpp"
#include "nbt/app/audio_route.hpp"

namespace nbt {
namespace app {

enum { kAudioDefaultRate = 22050, kAudioFragFrames = 512 };

/* The sample rate to ask the head unit's audio driver for: `preferred` within
 * 8000..48000 (else the default), clamped to the driver's range. */
int clamp_qnx_rate(int preferred, int min_rate, int max_rate);
/* A power-of-two fragment size in bytes within the driver's range. */
int choose_qnx_frag_size(int preferred_bytes, int min_frag, int max_frag);

/* Fills `frames` stereo S16 frames (2 * frames values) on the playback thread. */
typedef void (*AudioRenderFn)(void* context, int16_t* out_lr, int frames);

/* Stereo S16LE output: libasound loaded at run time (io-audio's libasound on
 * the head unit, ALSA on a Linux host), a playback thread pulling from a
 * render callback, and the head unit's audio connection (AudioRoute).
 * NBT_AUDIO_A2I=<io-amp input> plays straight into that input through A2I
 * instead (A2iOutput, 44100 Hz), for a connection whose source is not the
 * browser's libasound input. NBT_NO_AUDIO=1 skips the device. One open
 * instance per process. */
class AudioPcm {
 public:
  AudioPcm();
  ~AudioPcm();

  /* Opens the device and starts playback. `route_wanted` is the initial wish
   * for the audio connection. False when no device opened. */
  bool open(int preferred_rate, AudioRenderFn render, void* context, bool route_wanted);
  void close();

  bool active() const {
    return (pcm_handle_ != NULL || (a2i_ && a2i_->is_open())) && thread_running_;
  }
  /* The device's rate once open (else the requested rate). */
  int sample_rate() const { return sample_rate_; }
  int frag_frames() const { return frag_frames_; }

  /* Builds the audio connection when wanted (sound on), releases it otherwise. */
  void set_route_wanted(bool wanted);

 private:
  AudioPcm(const AudioPcm&);
  AudioPcm& operator=(const AudioPcm&);

  static void* thread_entry(void* arg);
  void playback_loop();
  void a2i_loop(int16_t* buf, int frames);
  bool open_device();
  void close_device();

  AudioRenderFn render_;
  void* render_context_;
  int sample_rate_;
  int frag_frames_;
  void* dl_handle_;
  void* pcm_handle_;
  bool owner_;
  bool thread_running_;
  volatile bool stop_requested_;
  pthread_t thread_;
  AudioRoute route_;
  A2iOutput* a2i_; /* NBT_AUDIO_A2I only; never in a module */
};

}  // namespace app
}  // namespace nbt

#endif /* NBT_APP_AUDIO_PCM_HPP */
