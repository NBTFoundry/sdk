// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_APP_AUDIO_ROUTE_HPP
#define NBT_APP_AUDIO_ROUTE_HPP

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace app {

/* Builds and releases the head unit's audio connection for an application's
 * sound (ConnectionMaster through `audioconn`; connection 0xb8 =
 * eSRC_BROWSER -> eSNK_SPEAKERS, the source dev-libasound's input_stereo_3
 * plays into). A worker thread runs at most one audioconn at a time, with
 * LD_PRELOAD removed so the child does not load the HMI's graphics stack.
 * NBT_AUDIO_CONN overrides the connection (0 disables routing) and
 * NBT_AUDIOCONN the audioconn binary. */
class AudioRoute {
 public:
  AudioRoute();
  ~AudioRoute();

  bool configure();
  void start();
  void set_wanted(bool wanted);
  void stop();

  bool configured() const;
  bool built() const;
  uint32_t conn_id() const { return conn_id_; }
  const char* binary_path() const { return path_; }

  /* Exposed for unit tests. */
  static uint32_t parse_conn_env(const char* val, uint32_t fallback);
  /* The connection named in <data_dir>/audio-conn (one number, e.g. 0x9a), or
   * `fallback`: selects another of the head unit's audio sources, whose
   * minimum volume may differ from the browser source's. Regular files only. */
  static uint32_t conn_file(const char* data_dir, uint32_t fallback);

 private:
  AudioRoute(const AudioRoute&);
  AudioRoute& operator=(const AudioRoute&);

  static void* worker_entry(void* arg);
  void worker_loop();
  bool sleep_ms_interruptible_locked(unsigned ms);
  int run_audioconn(const char* path, char op, uint32_t conn_id, char* out_buf, size_t out_size);

  mutable pthread_mutex_t mu_;
  pthread_cond_t cond_;
  pthread_t thread_;
  char path_[128];
  uint32_t conn_id_;
  bool configured_;
  bool thread_running_;
  bool stop_requested_;
  bool wanted_;
  bool built_;
  int fail_streak_;
};

}  // namespace app
}  // namespace nbt

#endif /* NBT_APP_AUDIO_ROUTE_HPP */
