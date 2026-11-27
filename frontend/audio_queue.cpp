// SPDX-License-Identifier: MPL-2.0
#include "nbt/frontend/audio_queue.hpp"

#include <string.h>

namespace nbt {
namespace frontend {

namespace {

class Lock {
 public:
  explicit Lock(pthread_mutex_t* mutex) : mutex_(mutex) { pthread_mutex_lock(mutex_); }
  ~Lock() { pthread_mutex_unlock(mutex_); }

 private:
  pthread_mutex_t* mutex_;
};

/* At most 0.5% faster or slower: inaudible, and enough for any two clocks. */
const double kMaxAdjust = 0.005;

}  // namespace

AudioQueue::AudioQueue()
    : read_(0),
      count_(0),
      target_(0),
      target_step_(0),
      target_max_(0),
      flowing_(false),
      dry_(false),
      dry_frames_(0),
      pause_frames_(0),
      late_(0),
      step_(1.0),
      phase_(0.0),
      dropped_(0),
      starved_(0) {
  pthread_mutex_init(&mutex_, NULL);
  memset(buffer_, 0, sizeof(buffer_));
}

AudioQueue::~AudioQueue() {
  pthread_mutex_destroy(&mutex_);
}

void AudioQueue::configure(double source_rate, int device_rate, unsigned latency_ms) {
  Lock lock(&mutex_);
  if (source_rate < 1000.0 || source_rate > 192000.0 || device_rate < 1000) {
    source_rate = device_rate > 0 ? device_rate : 48000;
    device_rate = static_cast<int>(source_rate);
  }
  step_ = source_rate / device_rate;
  target_ = static_cast<size_t>(source_rate * latency_ms / 1000.0);
  if (target_ < 64) target_ = 64;
  if (target_ > kCapacity / 2) target_ = kCapacity / 2;
  target_step_ = target_ / 4;
  target_max_ = static_cast<size_t>(source_rate * kLatencyMaxMs / 1000.0);
  if (target_max_ > kCapacity / 2) target_max_ = kCapacity / 2;
  if (target_max_ < target_) target_max_ = target_;
  pause_frames_ = static_cast<size_t>(device_rate / 10); /* 100 ms */
  flowing_ = dry_ = false;
  dry_frames_ = 0;
  late_ = 0;
  read_ = count_ = 0;
  phase_ = 0.0;
  dropped_ = starved_ = 0;
}

size_t AudioQueue::push(const int16_t* lr, size_t frames) {
  if (!lr) return 0;
  Lock lock(&mutex_);
  const size_t room = kCapacity - count_;
  const size_t kept = frames < room ? frames : room;
  dropped_ += frames - kept;
  for (size_t i = 0; i < kept; ++i) {
    const size_t at = (read_ + count_ + i) % kCapacity;
    buffer_[2 * at] = lr[2 * i];
    buffer_[2 * at + 1] = lr[2 * i + 1];
  }
  count_ += kept;
  return kept;
}

void AudioQueue::render(int16_t* out, int frames) {
  Lock lock(&mutex_);
  if (count_ >= 2) {
    // Frames again after a short empty spell: the loop was late.
    if (dry_ && dry_frames_ < pause_frames_ && ++late_ >= kStarvesToGrow) {
      late_ = 0;
      target_ = target_ + target_step_ < target_max_ ? target_ + target_step_ : target_max_;
    }
    dry_ = false;
    flowing_ = true;
  }
  // Fuller than the target: read a little faster; emptier: a little slower.
  double adjust = target_ ? (static_cast<double>(count_) - target_) / target_ * kMaxAdjust : 0.0;
  if (adjust > kMaxAdjust) adjust = kMaxAdjust;
  if (adjust < -kMaxAdjust) adjust = -kMaxAdjust;
  const double step = step_ * (1.0 + adjust);
  for (int i = 0; i < frames; ++i) {
    if (count_ < 2) {
      out[2 * i] = out[2 * i + 1] = 0;
      ++starved_;
      if (flowing_ && !dry_) {
        dry_ = true;
        dry_frames_ = 0;
      }
      ++dry_frames_;
      continue;
    }
    const size_t a = read_, b = (read_ + 1) % kCapacity;
    for (int c = 0; c < 2; ++c) {
      const double first = buffer_[2 * a + c], second = buffer_[2 * b + c];
      out[2 * i + c] = static_cast<int16_t>(first + (second - first) * phase_);
    }
    phase_ += step;
    while (phase_ >= 1.0 && count_ >= 2) {
      phase_ -= 1.0;
      read_ = (read_ + 1) % kCapacity;
      --count_;
    }
  }
}

void AudioQueue::render_callback(void* queue, int16_t* out_lr, int frames) {
  static_cast<AudioQueue*>(queue)->render(out_lr, frames);
}

size_t AudioQueue::fill() const {
  Lock lock(&mutex_);
  return count_;
}

uint64_t AudioQueue::dropped() const {
  Lock lock(&mutex_);
  return dropped_;
}

uint64_t AudioQueue::starved() const {
  Lock lock(&mutex_);
  return starved_;
}

void AudioQueue::clear() {
  Lock lock(&mutex_);
  read_ = count_ = 0;
  phase_ = 0.0;
  flowing_ = dry_ = false; /* a pause, not late frames */
  dry_frames_ = 0;
}

}  // namespace frontend
}  // namespace nbt
