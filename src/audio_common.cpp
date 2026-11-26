// SPDX-License-Identifier: MPL-2.0
// AudioPcm's parts shared by every platform (the QNX device, a runner's):
// construction and the parameter choices.
#include "nbt/app/audio_pcm.hpp"

#include <stddef.h>

namespace nbt {
namespace app {

AudioPcm::AudioPcm()
    : render_(NULL),
      render_context_(NULL),
      sample_rate_(kAudioDefaultRate),
      frag_frames_(kAudioFragFrames),
      dl_handle_(NULL),
      pcm_handle_(NULL),
      owner_(false),
      thread_running_(false),
      stop_requested_(false),
      thread_(),
      a2i_(NULL),
      mix_fd_(-1),
      mix_pull_(false),
      mix_route_(false),
      frags_(0) {}

AudioPcm::~AudioPcm() {
  close();
}

int clamp_qnx_rate(int preferred, int min_rate, int max_rate) {
  int rate = (preferred >= 8000 && preferred <= 48000) ? preferred : kAudioDefaultRate;
  if (min_rate > 0 && rate < min_rate) rate = min_rate;
  if (max_rate > 0 && rate > max_rate) rate = max_rate;
  return rate;
}

int choose_qnx_frag_size(int preferred_bytes, int min_frag, int max_frag) {
  int lo = (min_frag > 256) ? min_frag : 256;
  int hi = (max_frag > 0 && max_frag < 8192) ? max_frag : 8192;
  if (hi < lo) hi = lo;
  int target = (preferred_bytes > 0) ? preferred_bytes : (kAudioFragFrames * 4);
  if (target < lo) target = lo;
  if (target > hi) target = hi;

  int pow2 = 256;
  while (pow2 * 2 <= target && pow2 * 2 <= hi) {
    pow2 *= 2;
  }
  if (pow2 < lo) {
    int up = 256;
    while (up < lo && up < 8192) up *= 2;
    if (up <= hi) return up;
    return lo;
  }
  return pow2;
}

}  // namespace app
}  // namespace nbt
