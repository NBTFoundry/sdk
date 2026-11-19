// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_APP_AUDIO_MIXER_HPP
#define NBT_APP_AUDIO_MIXER_HPP

#include <pthread.h>
#include <stdint.h>

#include "nbt/app/audio_pcm.hpp"

namespace nbt {
namespace app {

/* Renders `nsamp` stereo S16 frames of music into `dest` (called with the
 * mixer locked, on the playback thread). */
typedef void (*MusicRenderFn)(void* dest_s16_stereo, unsigned nsamp);

/* A small game-audio mixer on AudioPcm (library libnbt-audio-mixer): 16
 * channels of 8-bit unsigned or 16-bit signed mono samples, each with its own
 * rate (16.16 fixed-point step) and left/right volume 0..127, mixed over an
 * optional music renderer into stereo S16. */
class AudioMixer {
 public:
  enum { kChannels = 16 };

  AudioMixer();
  ~AudioMixer();

  /* `open_device` false mixes without output (mix_frames only). */
  bool init(int sample_rate, bool open_device);
  void shutdown();

  /* Muted: silence, and the head unit's audio connection released. */
  void set_muted(bool muted);
  bool is_muted() const;
  bool audio_active() const;
  int sample_rate() const { return sample_rate_; }

  void set_music_renderer(MusicRenderFn fn);
  MusicRenderFn music_renderer() const;

  void lock();
  void unlock();

  /* Starts a sound on `preferred_slot`, or on a free (else the oldest)
   * channel when out of range. `step_fp` 0 derives the step from the rates.
   * Returns the channel, or -1. */
  int start_channel(int preferred_slot, const void* pcm_data, uint32_t num_samples, int src_rate,
                    int bits, int left_vol, int right_vol, uint32_t step_fp);
  void update_channel(int slot, int left_vol, int right_vol, uint32_t step_fp);
  void stop_channel(int slot);
  void stop_all_channels();
  bool is_channel_playing(int slot) const;
  bool any_channel_playing() const;

  /* Mixes `num_frames` stereo frames into `out_s16_lr`. Returns the number of
   * channels that were playing. */
  int mix_frames(int16_t* out_s16_lr, int num_frames);

 private:
  AudioMixer(const AudioMixer&);
  AudioMixer& operator=(const AudioMixer&);

  struct Channel {
    bool active;
    const uint8_t* samples8;
    const int16_t* samples16;
    uint32_t length;
    int sample_rate;
    uint64_t pos_fp;
    uint32_t step_fp;
    int left_vol;
    int right_vol;
    uint64_t start_seq;
  };

  static void render(void* context, int16_t* out_lr, int frames);

  mutable pthread_mutex_t mu_;
  bool initialized_;
  bool muted_;
  int sample_rate_;
  uint64_t seq_;
  MusicRenderFn music_fn_;
  Channel channels_[kChannels];
  AudioPcm pcm_;
};

}  // namespace app
}  // namespace nbt

#endif /* NBT_APP_AUDIO_MIXER_HPP */
