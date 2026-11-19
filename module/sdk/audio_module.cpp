// SPDX-License-Identifier: MPL-2.0
// AudioPcm for app modules: the runner opens the device, builds the head
// unit's audio connection and runs the playback thread, which calls the
// module's render callback directly. Same interface as src/audio_pcm.cpp.
#include <stdlib.h>
#include <string.h>

#include "nbt/app/audio_pcm.hpp"
#include "nbt/app/log.hpp"
#include "nbt_module.h"

namespace nbt {
namespace app {

bool AudioPcm::open(int preferred_rate, AudioRenderFn render, void* context, bool route_wanted) {
  if (pcm_handle_) return true;
  const char* off = getenv("NBT_NO_AUDIO");
  if (off && strcmp(off, "1") == 0) return false;
  if (!render || !NBT_HAS(audio_open)) return false;
  render_ = render;
  render_context_ = context;
  int32_t rate = 0, frag = 0;
  const int32_t rc = nbt_host->audio_open(preferred_rate, reinterpret_cast<nbt_audio_render_fn>(render), context,
                                          route_wanted ? 1 : 0, &rate, &frag);
  if (rc != 0) {
    log_info("audio: no device (%d)", static_cast<int>(rc));
    return false;
  }
  sample_rate_ = rate;
  frag_frames_ = frag;
  pcm_handle_ = this; /* open */
  thread_running_ = true;
  owner_ = true;
  return true;
}

void AudioPcm::close() {
  if (!pcm_handle_) return;
  nbt_host->audio_close();
  pcm_handle_ = NULL;
  thread_running_ = false;
  owner_ = false;
}

void AudioPcm::set_route_wanted(bool wanted) {
  if (pcm_handle_ && NBT_HAS(audio_set_route)) nbt_host->audio_set_route(wanted ? 1 : 0);
}

}  // namespace app
}  // namespace nbt
