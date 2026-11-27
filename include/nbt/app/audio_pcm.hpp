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

/* The calling audio thread, on the head unit: priority kAudioPriority (above
 * iDrive's applications, as NBT-AAD's audio) for at most kAudioBudgetMs of
 * each kAudioPeriodMs, its own priority beyond (QNX sporadic scheduling), so
 * a render that runs away cannot starve iDrive. Nothing elsewhere.
 * kAudioBlocksMax: how often the thread may wait within a period and keep the
 * priority; a 5.8 ms fragment makes it wait several times per period. */
enum { kAudioPriority = 15, kAudioBudgetMs = 4, kAudioPeriodMs = 20, kAudioBlocksMax = 64 };
void audio_thread_priority();

/* The sample rate to ask the head unit's audio driver for: `preferred` within
 * 8000..48000 (else the default), clamped to the driver's range. */
int clamp_qnx_rate(int preferred, int min_rate, int max_rate);
/* A power-of-two fragment size in bytes within the driver's range. */
int choose_qnx_frag_size(int preferred_bytes, int min_frag, int max_frag);

/* Fills `frames` stereo S16 frames (2 * frames values) on the playback thread. */
typedef void (*AudioRenderFn)(void* context, int16_t* out_lr, int frames);

/* Tells when an output that takes frames as it plays them has played a gap.
 * After each write the writer is some frames ahead of the clock; the most it
 * is ahead over a second stays the same while the output plays on, and falls
 * by the gap when the output stood still (it ran empty, there or here). */
class AudioPace {
 public:
  AudioPace() { reset(44100, 512); }
  void reset(int rate, int frag_frames);
  /* A write of `frames` returned at `now_us` (CLOCK_MONOTONIC). True once a
   * second at most, when a gap of more than half a fragment was played. */
  bool wrote(int frames, uint64_t now_us);

 private:
  int rate_, frag_;
  bool started_, have_reference_;
  uint64_t start_us_, window_end_us_, written_;
  int64_t best_, reference_;
  unsigned windows_;
};

/* Stereo S16LE output: libasound loaded at run time (io-audio's libasound on
 * the head unit, ALSA on a Linux host), a playback thread pulling from a
 * render callback, and the head unit's audio connection (AudioRoute).
 * NBT_AUDIO_MIXER=<address> plays through NBT Manager's mixer instead, at its
 * rate, with every other application at once (nbt/app/audio_mix.hpp): the
 * render is called as the mixer asks, kMixFragFrames at most; the device
 * when the mixer does not answer. The playback thread runs at
 * audio_thread_priority().
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
    return (pcm_handle_ != NULL || (a2i_ && a2i_->is_open()) || mix_fd_ >= 0) && thread_running_;
  }
  /* The device's rate once open (else the requested rate). */
  int sample_rate() const { return sample_rate_; }
  int frag_frames() const { return frag_frames_; }

  /* Builds the audio connection when wanted (sound on), releases it otherwise. */
  void set_route_wanted(bool wanted);
  /* Before open: the device's fragments (latency), else the driver's choice.
   * The head unit's libasound queues them twice: about 2 x frags - 1
   * fragments wait between a render and the audio input. */
  void set_buffer_frags(int frags) { frags_ = frags; }
  /* Before open: a fragment's frames, a power of two (the device and A2I),
   * else kAudioFragFrames (A2I: kA2iBufferBytes). */
  void set_fragment_frames(int frames) { frag_wanted_ = frames; }
  /* Before open: A2I's buffers, all queued while it plays; else kA2iBuffers. */
  void set_a2i_buffers(int buffers) { a2i_buffers_ = buffers; }
  /* Gaps the device or A2I has played since open (AudioPace), for a caller
   * that would rather queue more. */
  unsigned stalls() const { return stalls_; }

 private:
  AudioPcm(const AudioPcm&);
  AudioPcm& operator=(const AudioPcm&);

  static void* thread_entry(void* arg);
  void playback_loop();
  void a2i_loop(int16_t* buf, int frames);
  void mix_loop(int16_t* buf, int frames);
  void mix_pull_loop(int16_t* buf, int frames);
  bool open_mixer();
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
  int mix_fd_;     /* NBT_AUDIO_MIXER: the mixer's socket */
  bool mix_pull_;  /* the mixer asks for frames (version 2) */
  volatile bool mix_route_;
  int frags_;       /* set_buffer_frags, 0 for the default */
  int frag_wanted_; /* set_fragment_frames, 0 for the default */
  int a2i_buffers_; /* set_a2i_buffers, 0 for the default */
  AudioPace pace_;  /* the playback thread's */
  volatile unsigned stalls_;
  void paced(int frames); /* a write to the device or A2I returned */
};

}  // namespace app
}  // namespace nbt

#endif /* NBT_APP_AUDIO_PCM_HPP */
