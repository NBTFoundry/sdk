// SPDX-License-Identifier: MPL-2.0
#include "nbt/app/audio_pcm.hpp"

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __QNXNTO__
#include <sys/asoundlib.h>
#endif

#include "nbt/app/log.hpp"

namespace nbt {
namespace app {

namespace {

#ifdef __QNXNTO__

typedef int (*QnxOpenPreferredFn)(snd_pcm_t** handle, int* card, int* device, int mode);
typedef int (*QnxPluginInfoFn)(snd_pcm_t* handle, snd_pcm_channel_info_t* info);
typedef int (*QnxPluginParamsFn)(snd_pcm_t* handle, snd_pcm_channel_params_t* params);
typedef int (*QnxPluginPrepareFn)(snd_pcm_t* handle, int channel);
typedef int (*QnxPluginStatusFn)(snd_pcm_t* handle, snd_pcm_channel_status_t* status);
typedef ssize_t (*QnxPluginWriteFn)(snd_pcm_t* handle, const void* buffer, size_t size);
typedef int (*QnxPluginFlushFn)(snd_pcm_t* handle, int channel);
typedef int (*QnxCloseFn)(snd_pcm_t* handle);

struct AudioApi {
  QnxOpenPreferredFn open_preferred;
  QnxPluginInfoFn plugin_info;
  QnxPluginParamsFn plugin_params;
  QnxPluginPrepareFn plugin_prepare;
  QnxPluginStatusFn plugin_status;
  QnxPluginWriteFn plugin_write;
  QnxPluginFlushFn plugin_flush;
  QnxCloseFn close;
};

#else

enum { kAlsaPcmStreamPlayback = 0, kAlsaPcmFormatS16Le = 2, kAlsaPcmAccessRwInterleaved = 3 };

typedef int (*AlsaOpenFn)(void** pcm, const char* name, int stream, int mode);
typedef int (*AlsaSetParamsFn)(void* pcm, int format, int access, unsigned channels, unsigned rate,
                               int soft_resample, unsigned latency_us);
typedef long (*AlsaWriteiFn)(void* pcm, const void* buffer, unsigned long frames);
typedef int (*AlsaRecoverFn)(void* pcm, int err, int silent);
typedef int (*AlsaDropFn)(void* pcm);
typedef int (*AlsaCloseFn)(void* pcm);

struct AudioApi {
  AlsaOpenFn open;
  AlsaSetParamsFn set_params;
  AlsaWriteiFn writei;
  AlsaRecoverFn recover;
  AlsaDropFn drop;
  AlsaCloseFn close;
};

#endif

/* The libasound entry points of the one open AudioPcm (plain data: no
 * constructor runs before main). */
AudioApi g_audio_api;
bool g_pcm_open = false;

}  // namespace

bool AudioPcm::open(int preferred_rate, AudioRenderFn render, void* context, bool route_wanted) {
  if (active() || !render) return active();
  const char* no_audio = getenv("NBT_NO_AUDIO");
  if (no_audio && no_audio[0] == '1') return false;
  if (g_pcm_open) {
    log_info("audio output already open in this process");
    return false;
  }
  sample_rate_ =
      (preferred_rate >= 8000 && preferred_rate <= 48000) ? preferred_rate : kAudioDefaultRate;
  frag_frames_ = kAudioFragFrames;
  render_ = render;
  render_context_ = context;
  stop_requested_ = false;
  g_pcm_open = true;
  owner_ = true;
  if (!open_device()) {
    close_device();
    return false;
  }
  if (pthread_create(&thread_, NULL, &AudioPcm::thread_entry, this) != 0) {
    close_device();
    return false;
  }
  thread_running_ = true;
  if (route_.configure()) {
    route_.start();
    route_.set_wanted(route_wanted);
  }
  return true;
}

void AudioPcm::close() {
  stop_requested_ = true;
  if (thread_running_) {
    pthread_join(thread_, NULL);
    thread_running_ = false;
  }
  route_.stop();
  close_device();
}

void AudioPcm::set_route_wanted(bool wanted) {
  if (active()) route_.set_wanted(wanted);
}

bool AudioPcm::open_device() {
#ifdef __QNXNTO__
  static const char* const kMmAlsa = "/opt/mm/lib/libasound/libasound.so";
  if (!dl_handle_) {
    dl_handle_ = dlopen(kMmAlsa, RTLD_LAZY);
    if (!dl_handle_) {
      if (access(kMmAlsa, F_OK) == 0) {
        log_info("qnx dlopen %s failed: %s", kMmAlsa, dlerror());
        return false;
      }
      dl_handle_ = dlopen("libasound.so.2", RTLD_LAZY);
    }
  }
  if (!dl_handle_) return false;

  g_audio_api.open_preferred =
      reinterpret_cast<QnxOpenPreferredFn>(dlsym(dl_handle_, "snd_pcm_open_preferred"));
  g_audio_api.plugin_info =
      reinterpret_cast<QnxPluginInfoFn>(dlsym(dl_handle_, "snd_pcm_plugin_info"));
  g_audio_api.plugin_params =
      reinterpret_cast<QnxPluginParamsFn>(dlsym(dl_handle_, "snd_pcm_plugin_params"));
  g_audio_api.plugin_prepare =
      reinterpret_cast<QnxPluginPrepareFn>(dlsym(dl_handle_, "snd_pcm_plugin_prepare"));
  g_audio_api.plugin_status =
      reinterpret_cast<QnxPluginStatusFn>(dlsym(dl_handle_, "snd_pcm_plugin_status"));
  g_audio_api.plugin_write =
      reinterpret_cast<QnxPluginWriteFn>(dlsym(dl_handle_, "snd_pcm_plugin_write"));
  g_audio_api.plugin_flush =
      reinterpret_cast<QnxPluginFlushFn>(dlsym(dl_handle_, "snd_pcm_plugin_flush"));
  g_audio_api.close = reinterpret_cast<QnxCloseFn>(dlsym(dl_handle_, "snd_pcm_close"));

  if (!g_audio_api.open_preferred || !g_audio_api.plugin_params || !g_audio_api.plugin_prepare ||
      !g_audio_api.plugin_write || !g_audio_api.close) {
    close_device();
    return false;
  }

  int card = 0;
  int dev = 0;
  snd_pcm_t* pcm = NULL;
  if (g_audio_api.open_preferred(&pcm, &card, &dev, SND_PCM_OPEN_PLAYBACK) < 0 || !pcm) {
    log_info("qnx snd_pcm_open_preferred failed");
    close_device();
    return false;
  }
  pcm_handle_ = pcm;

  int rate = sample_rate_;
  int frag_bytes = kAudioFragFrames * 4;
  if (g_audio_api.plugin_info) {
    snd_pcm_channel_info_t info;
    memset(&info, 0, sizeof(info));
    info.channel = SND_PCM_CHANNEL_PLAYBACK;
    if (g_audio_api.plugin_info(pcm, &info) == 0) {
      rate = clamp_qnx_rate(rate, info.min_rate, info.max_rate);
      frag_bytes = choose_qnx_frag_size(frag_bytes, info.min_fragment_size, info.max_fragment_size);
      log_info("qnx audio info rate=[%u..%u] frag=[%u..%u] -> rate=%d frag=%d", info.min_rate,
               info.max_rate, info.min_fragment_size, info.max_fragment_size, rate, frag_bytes);
    }
  } else {
    frag_bytes = choose_qnx_frag_size(frag_bytes, 0, 0);
  }

  static const int kFragsTries[3][2] = {{0, 5}, {0, 10}, {1, 8}};
  bool params_ok = false;
  unsigned int last_why = 0;
  for (int t = 0; t < 3; t++) {
    int try_frag = (kFragsTries[t][0] && frag_bytes <= 4096) ? (frag_bytes * 2) : frag_bytes;
    snd_pcm_channel_params_t params;
    memset(&params, 0, sizeof(params));
    params.channel = SND_PCM_CHANNEL_PLAYBACK;
    params.mode = SND_PCM_MODE_BLOCK;
    params.start_mode = SND_PCM_START_FULL;
    params.stop_mode = SND_PCM_STOP_STOP;
    params.buf.block.frag_size = try_frag;
    params.buf.block.frags_min = 1;
    params.buf.block.frags_max = kFragsTries[t][1];
    params.format.interleave = 1;
    params.format.format = SND_PCM_SFMT_S16_LE;
    params.format.rate = rate;
    params.format.voices = 2;
    if (g_audio_api.plugin_params(pcm, &params) >= 0) {
      if (params.buf.block.frag_size >= 256 && params.buf.block.frag_size <= 8192) {
        frag_bytes = params.buf.block.frag_size;
      }
      log_debug("qnx snd_pcm_plugin_params ok try=%d frag=%d frags=[%d..%d]", t,
                params.buf.block.frag_size, params.buf.block.frags_min, params.buf.block.frags_max);
      params_ok = true;
      break;
    }
    last_why = params.why_failed;
  }
  if (!params_ok || g_audio_api.plugin_prepare(pcm, SND_PCM_CHANNEL_PLAYBACK) < 0) {
    log_info("qnx snd_pcm_plugin_params/prepare failed why=%u", last_why);
    close_device();
    return false;
  }
  sample_rate_ = rate;
  frag_frames_ = frag_bytes / 4;
  if (frag_frames_ < 64) frag_frames_ = 64;
  if (frag_frames_ > 2048) frag_frames_ = 2048;
  log_info("qnx audio ok card=%d dev=%d rate=%d frag_frames=%d", card, dev, sample_rate_,
           frag_frames_);
  return true;
#else
  dl_handle_ = dlopen("libasound.so.2", RTLD_LAZY);
  if (!dl_handle_) return false;
  g_audio_api.open = reinterpret_cast<AlsaOpenFn>(dlsym(dl_handle_, "snd_pcm_open"));
  g_audio_api.set_params =
      reinterpret_cast<AlsaSetParamsFn>(dlsym(dl_handle_, "snd_pcm_set_params"));
  g_audio_api.writei = reinterpret_cast<AlsaWriteiFn>(dlsym(dl_handle_, "snd_pcm_writei"));
  g_audio_api.recover = reinterpret_cast<AlsaRecoverFn>(dlsym(dl_handle_, "snd_pcm_recover"));
  g_audio_api.drop = reinterpret_cast<AlsaDropFn>(dlsym(dl_handle_, "snd_pcm_drop"));
  g_audio_api.close = reinterpret_cast<AlsaCloseFn>(dlsym(dl_handle_, "snd_pcm_close"));

  if (!g_audio_api.open || !g_audio_api.set_params || !g_audio_api.writei || !g_audio_api.close) {
    close_device();
    return false;
  }

  if (g_audio_api.open(&pcm_handle_, "default", kAlsaPcmStreamPlayback, 0) < 0 || !pcm_handle_) {
    close_device();
    return false;
  }

  if (g_audio_api.set_params(pcm_handle_, kAlsaPcmFormatS16Le, kAlsaPcmAccessRwInterleaved, 2,
                             static_cast<unsigned>(sample_rate_), 1, 50000) < 0) {
    close_device();
    return false;
  }
  return true;
#endif
}

void AudioPcm::close_device() {
  if (pcm_handle_) {
#ifdef __QNXNTO__
    snd_pcm_t* pcm = static_cast<snd_pcm_t*>(pcm_handle_);
    if (g_audio_api.plugin_flush) g_audio_api.plugin_flush(pcm, SND_PCM_CHANNEL_PLAYBACK);
    if (g_audio_api.close) g_audio_api.close(pcm);
#else
    if (g_audio_api.drop) g_audio_api.drop(pcm_handle_);
    if (g_audio_api.close) g_audio_api.close(pcm_handle_);
#endif
    pcm_handle_ = NULL;
  }
  if (owner_) {
    memset(&g_audio_api, 0, sizeof(g_audio_api));
    g_pcm_open = false;
    owner_ = false;
  }
#ifndef __QNXNTO__
  if (dl_handle_) {
    dlclose(dl_handle_);
    dl_handle_ = NULL;
  }
#endif
}

void* AudioPcm::thread_entry(void* arg) {
  static_cast<AudioPcm*>(arg)->playback_loop();
  return NULL;
}

void AudioPcm::playback_loop() {
  const int frames = (frag_frames_ >= 64 && frag_frames_ <= 2048) ? frag_frames_ : kAudioFragFrames;
  int16_t* buf = static_cast<int16_t*>(calloc(static_cast<size_t>(frames * 2), sizeof(int16_t)));
  if (!buf) return;
#ifdef __QNXNTO__
  int consecutive_errors = 0;
  uint64_t writes_ok = 0;
  uint64_t writes_err = 0;
#endif
  while (!stop_requested_) {
    render_(render_context_, buf, frames);
#ifdef __QNXNTO__
    snd_pcm_t* pcm = static_cast<snd_pcm_t*>(pcm_handle_);
    const size_t bytes = static_cast<size_t>(frames) * 4u;
    ssize_t written = g_audio_api.plugin_write(pcm, buf, bytes);
    if (written == static_cast<ssize_t>(bytes)) {
      consecutive_errors = 0;
      if (++writes_ok == 1) {
        log_debug("qnx audio first write ok bytes=%u", static_cast<unsigned>(bytes));
      }
      continue;
    }
    if (stop_requested_) break;
    if (written <= 0) {
      ++writes_err;
      if (++consecutive_errors > 50) {
        log_info("qnx audio stopped after repeated write errors (total=%llu)",
                 static_cast<unsigned long long>(writes_err));
        break;
      }
      bool need_prepare = true;
      int st_val = -1;
      if (g_audio_api.plugin_status) {
        snd_pcm_channel_status_t st;
        memset(&st, 0, sizeof(st));
        st.channel = SND_PCM_CHANNEL_PLAYBACK;
        if (g_audio_api.plugin_status(pcm, &st) == 0) {
          st_val = st.status;
          need_prepare =
              (st.status == SND_PCM_STATUS_UNDERRUN || st.status == SND_PCM_STATUS_READY ||
               st.status == SND_PCM_STATUS_ERROR || st.status == SND_PCM_STATUS_CHANGE);
        }
      }
      if (writes_err <= 5 || (writes_err % 50) == 0) {
        log_debug("qnx audio write rc=%ld status=%d prepare=%d err=%llu",
                  static_cast<long>(written), st_val, need_prepare ? 1 : 0,
                  static_cast<unsigned long long>(writes_err));
      }
      if (need_prepare) {
        g_audio_api.plugin_prepare(pcm, SND_PCM_CHANNEL_PLAYBACK);
      }
      usleep(10000);
    } else {
      consecutive_errors = 0;
    }
#else
    long rc = g_audio_api.writei(pcm_handle_, buf, static_cast<unsigned long>(frames));
    if (rc < 0 && !stop_requested_) {
      if (g_audio_api.recover) {
        g_audio_api.recover(pcm_handle_, static_cast<int>(rc), 1);
      } else {
        usleep(5000);
      }
    }
#endif
  }
  free(buf);
}

}  // namespace app
}  // namespace nbt
