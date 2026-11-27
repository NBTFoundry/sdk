// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_FRONTEND_AUDIO_QUEUE_HPP
#define NBT_FRONTEND_AUDIO_QUEUE_HPP

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace frontend {

/* Stereo S16 frames from an application's loop (an emulator's frame at a
 * time) to AudioPcm's playback thread, at another rate: linear resampling
 * from the source rate to the device's, with the ratio nudged (at most
 * 0.5%) to keep the queue near its target, so the two clocks never drift
 * apart. An empty queue plays silence; a full one drops what does not fit.
 * The target grows by a quarter each time the queue ran empty
 * kStarvesToGrow times while frames kept coming (the loop's frames came
 * late), up to kLatencyMaxMs: a loop that keeps its pace keeps the short
 * delay, one that does not gets the longer queue it needs. */
class AudioQueue {
 public:
  enum { kCapacity = 16384 }; /* source frames */
  enum { kLatencyMs = 32, kLatencyMaxMs = 64, kStarvesToGrow = 3 };

  AudioQueue();
  ~AudioQueue();

  /* The source's rate (32040.5 for a SNES, say) and the device's; empties
   * the queue. The target starts at `latency_ms` of source frames. */
  void configure(double source_rate, int device_rate, unsigned latency_ms = kLatencyMs);
  /* The loop: frames produced; returns how many were kept. */
  size_t push(const int16_t* lr, size_t frames);
  /* The playback thread: `frames` device frames. */
  void render(int16_t* out_lr, int frames);
  /* AudioRenderFn for AudioPcm::open, with the queue as context. */
  static void render_callback(void* queue, int16_t* out_lr, int frames);

  size_t fill() const;
  size_t target() const { return target_; }
  /* Source frames dropped (full) and device frames played empty, since configure. */
  uint64_t dropped() const;
  uint64_t starved() const;
  void clear();

 private:
  AudioQueue(const AudioQueue&);
  AudioQueue& operator=(const AudioQueue&);

  mutable pthread_mutex_t mutex_;
  int16_t buffer_[kCapacity * 2];
  size_t read_, count_;
  size_t target_, target_step_, target_max_;
  /* An empty spell: device frames played empty since frames ran out; it
   * counts as late frames when more come within kPauseFrames (a pause
   * otherwise). */
  bool flowing_, dry_;
  size_t dry_frames_, pause_frames_;
  unsigned late_;
  double step_;  /* source frames per device frame */
  double phase_; /* position between the first two queued frames */
  uint64_t dropped_, starved_;
};

}  // namespace frontend
}  // namespace nbt

#endif /* NBT_FRONTEND_AUDIO_QUEUE_HPP */
