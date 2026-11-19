#include "nbt/app/audio_mixer.hpp"

#include <string.h>

namespace nbt {
namespace app {

namespace {

int clamp_s16(int32_t v) {
  if (v < -32768) return -32768;
  if (v > 32767) return 32767;
  return v;
}

int clamp_volume(int v) {
  return (v < 0) ? 0 : (v > 127 ? 127 : v);
}

}  // namespace

AudioMixer::AudioMixer()
    : initialized_(false),
      muted_(false),
      sample_rate_(kAudioDefaultRate),
      seq_(0),
      music_fn_(NULL) {
  pthread_mutex_init(&mu_, NULL);
  memset(channels_, 0, sizeof(channels_));
}

AudioMixer::~AudioMixer() {
  shutdown();
  pthread_mutex_destroy(&mu_);
}

void AudioMixer::render(void* context, int16_t* out_lr, int frames) {
  static_cast<AudioMixer*>(context)->mix_frames(out_lr, frames);
}

bool AudioMixer::init(int sample_rate, bool open_device) {
  if (initialized_) return true;
  sample_rate_ = (sample_rate >= 8000 && sample_rate <= 48000) ? sample_rate : kAudioDefaultRate;
  stop_all_channels();
  initialized_ = true;
  if (open_device && pcm_.open(sample_rate_, &AudioMixer::render, this, !is_muted())) {
    sample_rate_ = pcm_.sample_rate();
  }
  return true;
}

void AudioMixer::shutdown() {
  if (!initialized_) return;
  pcm_.close();
  stop_all_channels();
  initialized_ = false;
}

void AudioMixer::set_muted(bool muted) {
  pthread_mutex_lock(&mu_);
  muted_ = muted;
  pthread_mutex_unlock(&mu_);
  pcm_.set_route_wanted(!muted);
}

bool AudioMixer::is_muted() const {
  pthread_mutex_lock(&mu_);
  const bool m = muted_;
  pthread_mutex_unlock(&mu_);
  return m;
}

bool AudioMixer::audio_active() const {
  return initialized_ && pcm_.active();
}

void AudioMixer::set_music_renderer(MusicRenderFn fn) {
  pthread_mutex_lock(&mu_);
  music_fn_ = fn;
  pthread_mutex_unlock(&mu_);
}

MusicRenderFn AudioMixer::music_renderer() const {
  pthread_mutex_lock(&mu_);
  MusicRenderFn fn = music_fn_;
  pthread_mutex_unlock(&mu_);
  return fn;
}

void AudioMixer::lock() {
  pthread_mutex_lock(&mu_);
}

void AudioMixer::unlock() {
  pthread_mutex_unlock(&mu_);
}

int AudioMixer::start_channel(int preferred_slot, const void* pcm_data, uint32_t num_samples,
                              int src_rate, int bits, int left_vol, int right_vol,
                              uint32_t step_fp) {
  if (!pcm_data || num_samples == 0 || src_rate <= 0) return -1;
  pthread_mutex_lock(&mu_);
  int slot = preferred_slot;
  if (slot < 0 || slot >= kChannels) {
    slot = 0;
    uint64_t oldest = ~0ULL;
    for (int i = 0; i < kChannels; i++) {
      if (!channels_[i].active) {
        slot = i;
        break;
      }
      if (channels_[i].start_seq < oldest) {
        oldest = channels_[i].start_seq;
        slot = i;
      }
    }
  }

  Channel& s = channels_[slot];
  memset(&s, 0, sizeof(s));
  s.active = true;
  s.samples8 = (bits == 8) ? static_cast<const uint8_t*>(pcm_data) : NULL;
  s.samples16 = (bits == 16) ? static_cast<const int16_t*>(pcm_data) : NULL;
  if (!s.samples8 && !s.samples16) {
    s.active = false;
    pthread_mutex_unlock(&mu_);
    return -1;
  }
  s.length = num_samples;
  s.sample_rate = src_rate;
  s.pos_fp = 0;
  s.step_fp = step_fp ? step_fp
                      : static_cast<uint32_t>((static_cast<uint64_t>(src_rate) << 16) /
                                              static_cast<uint64_t>(sample_rate_));
  s.left_vol = clamp_volume(left_vol);
  s.right_vol = clamp_volume(right_vol);
  s.start_seq = ++seq_;
  pthread_mutex_unlock(&mu_);
  return slot;
}

void AudioMixer::update_channel(int slot, int left_vol, int right_vol, uint32_t step_fp) {
  if (slot < 0 || slot >= kChannels) return;
  pthread_mutex_lock(&mu_);
  if (channels_[slot].active) {
    channels_[slot].left_vol = clamp_volume(left_vol);
    channels_[slot].right_vol = clamp_volume(right_vol);
    if (step_fp != 0) channels_[slot].step_fp = step_fp;
  }
  pthread_mutex_unlock(&mu_);
}

void AudioMixer::stop_channel(int slot) {
  if (slot < 0 || slot >= kChannels) return;
  pthread_mutex_lock(&mu_);
  channels_[slot].active = false;
  pthread_mutex_unlock(&mu_);
}

void AudioMixer::stop_all_channels() {
  pthread_mutex_lock(&mu_);
  for (int i = 0; i < kChannels; i++) channels_[i].active = false;
  pthread_mutex_unlock(&mu_);
}

bool AudioMixer::is_channel_playing(int slot) const {
  if (slot < 0 || slot >= kChannels) return false;
  pthread_mutex_lock(&mu_);
  const bool active = channels_[slot].active;
  pthread_mutex_unlock(&mu_);
  return active;
}

bool AudioMixer::any_channel_playing() const {
  pthread_mutex_lock(&mu_);
  bool any = false;
  for (int i = 0; i < kChannels; i++) {
    if (channels_[i].active) {
      any = true;
      break;
    }
  }
  pthread_mutex_unlock(&mu_);
  return any;
}

int AudioMixer::mix_frames(int16_t* out_s16_lr, int num_frames) {
  if (!out_s16_lr || num_frames <= 0) return 0;

  pthread_mutex_lock(&mu_);
  if (muted_) {
    memset(out_s16_lr, 0, static_cast<size_t>(num_frames) * 2 * sizeof(int16_t));
    pthread_mutex_unlock(&mu_);
    return 0;
  }

  if (music_fn_) {
    music_fn_(out_s16_lr, static_cast<unsigned>(num_frames));
  } else {
    memset(out_s16_lr, 0, static_cast<size_t>(num_frames) * 2 * sizeof(int16_t));
  }

  int active_count = 0;
  for (int c = 0; c < kChannels; c++) {
    if (channels_[c].active) active_count++;
  }

  for (int f = 0; f < num_frames; f++) {
    int32_t acc_l = out_s16_lr[f * 2 + 0];
    int32_t acc_r = out_s16_lr[f * 2 + 1];

    for (int c = 0; c < kChannels; c++) {
      Channel& s = channels_[c];
      if (!s.active) continue;
      const uint32_t idx = static_cast<uint32_t>(s.pos_fp >> 16);
      if (idx >= s.length) {
        s.active = false;
        continue;
      }
      int32_t sample = 0;
      if (s.samples8) {
        sample = (static_cast<int32_t>(s.samples8[idx]) - 128) * 256;
      } else if (s.samples16) {
        sample = static_cast<int32_t>(s.samples16[idx]);
      }
      acc_l += (sample * s.left_vol) >> 8;
      acc_r += (sample * s.right_vol) >> 8;
      s.pos_fp += s.step_fp;
      if ((s.pos_fp >> 16) >= s.length) {
        s.active = false;
      }
    }

    out_s16_lr[f * 2 + 0] = static_cast<int16_t>(clamp_s16(acc_l));
    out_s16_lr[f * 2 + 1] = static_cast<int16_t>(clamp_s16(acc_r));
  }

  pthread_mutex_unlock(&mu_);
  return active_count;
}

}  // namespace app
}  // namespace nbt
