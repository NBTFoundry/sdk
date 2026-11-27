// SPDX-License-Identifier: MPL-2.0
// NBT Manager's audio mixer and its client (nbt/app/audio_mix.hpp): the
// mixer runs as `nbt-runner --mixer`; modules and guests play through the
// runner, so this file is not in their SDK (module/module.mk).
#include "nbt/app/audio_mix.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <new>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "nbt/app/audio_pcm.hpp"
#include "nbt/app/clock.hpp"
#include "nbt/app/log.hpp"

namespace nbt {
namespace app {
namespace {

const char kHello1[] = "NBTMIX1\n";
const char kHello2[] = "NBTMIX2\n";

bool parse_address(const char* text, struct sockaddr_in* out) {
  char host[32];
  unsigned port = 0;
  const char* colon = text ? strrchr(text, ':') : NULL;
  if (!colon || colon == text || static_cast<size_t>(colon - text) >= sizeof(host)) return false;
  memcpy(host, text, static_cast<size_t>(colon - text));
  host[colon - text] = 0;
  char* end = NULL;
  port = static_cast<unsigned>(strtoul(colon + 1, &end, 10));
  memset(out, 0, sizeof(*out));
  out->sin_family = AF_INET;
  out->sin_port = htons(static_cast<uint16_t>(port));
  return end && !*end && port > 0 && port < 65536 && inet_aton(host, &out->sin_addr);
}

bool send_all(int fd, const void* data, size_t size) {
  const char* p = static_cast<const char*>(data);
  while (size) {
    const ssize_t n = send(fd, p, size, MSG_NOSIGNAL);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return false;
    p += n;
    size -= static_cast<size_t>(n);
  }
  return true;
}

uint64_t monotonic_us() {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
  return static_cast<uint64_t>(now.tv_sec) * 1000000u + static_cast<uint64_t>(now.tv_nsec) / 1000u;
}

// What waits for the output, per client:
//   version 2 (pull)  its target queued or asked for: kPullFrags fragments
//                     (11.6 ms), one more each time it starved, kPullFragsMax
//                     at most; the mixer asks for what is missing, kCreditMin
//                     frames at least
//   version 1 (push)  its ring topped up to kPushTarget (46 ms) each time the
//                     output takes from it, plus what waits in its socket
// The output itself queues what its level says (mix_level).
enum {
  kRingFrames = 8192,
  kPullFrags = 2,
  kPullFragsMax = 4,
  kCreditMin = kMixFragFrames / 2,
  kPushTarget = 2048,
  kSocketBuffer = 4096,
  kIdleCloseMs = 3000,
};

const MixLevel kLevels[kMixLevels] = {{2, 3}, {3, 4}, {6, 8}, {10, 16}};

// One client: the record being read, then its frames waiting to be played.
// `parked`: a version 1 client the serving loop does not read (ring full).
struct Client {
  int fd;
  bool greeted, route, pull, parked;
  uint8_t record[4 + kMixRecordMax];
  size_t have;
  int16_t ring[kRingFrames * 2];
  size_t head, used;  // frames
  size_t asked;       // version 2: asked for, not yet received
  size_t target;      // version 2: frames to keep queued or asked for
  bool played;        // the output has taken frames of it
  unsigned starved;   // version 2: times the output found it empty since
};

class Mixer {
 public:
  Mixer()
      : device_open_(false),
        route_(false),
        level_(0),
        stalls_seen_(0),
        stall_count_(0),
        capture_path_(NULL),
        capture_(NULL),
        capture_stop_(false) {
    pthread_mutex_init(&mutex_, NULL);
    memset(clients_, 0, sizeof(clients_));
    wake_[0] = wake_[1] = -1;
  }
  ~Mixer() {
    for (Client*& c : clients_) drop(c);
    close_output();
    if (capture_) fclose(capture_);
    for (int fd : wake_) {
      if (fd >= 0) close(fd);
    }
    pthread_mutex_destroy(&mutex_);
  }

  int serve(const char* address, unsigned idle_ms) {
    struct sockaddr_in where;
    if (!parse_address(address, &where)) return 1;
    unsetenv("NBT_AUDIO_MIXER");  // its own output is the device
    if (pipe(wake_) != 0) return 1;
    for (int fd : wake_) {
      (void)fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
      (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    const int on = 1, buffer = kSocketBuffer;  // accepted sockets inherit it
    if (listener < 0 || setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) ||
        bind(listener, reinterpret_cast<struct sockaddr*>(&where), sizeof(where)) ||
        listen(listener, 8)) {
      const int code = errno == EADDRINUSE ? 3 : 1;
      if (listener >= 0) close(listener);
      return code;
    }
    (void)setsockopt(listener, SOL_SOCKET, SO_RCVBUF, &buffer, sizeof(buffer));
    capture_path_ = getenv("NBT_MIX_CAPTURE");
    if (capture_path_ && capture_path_[0] && !(capture_ = fopen(capture_path_, "wb"))) {
      close(listener);
      return 1;
    }
    audio_thread_priority();  // asks and reads on the output's time
    level_ = mix_state_read();
    log_info("mixer: serving %s, output level %d", address, level_);
    uint64_t alone_since = monotonic_ms();
    while (true) {
      struct pollfd fds[2 + kMixClientsMax];
      Client* owner[2 + kMixClientsMax];
      nfds_t count = 0;
      fds[count] = {listener, POLLIN, 0};
      owner[count++] = NULL;
      fds[count] = {wake_[0], POLLIN, 0};  // the output took frames
      owner[count++] = NULL;
      for (Client* c : clients_) {
        if (c && (c->pull || wants_input(c))) {
          fds[count] = {c->fd, POLLIN, 0};
          owner[count++] = c;
        }
      }
      const int ready = poll(fds, count, 100);
      if (ready < 0 && errno != EINTR) break;
      for (nfds_t i = 0; ready > 0 && i < count; ++i) {
        if (!fds[i].revents) continue;
        if (owner[i]) {
          read_from(owner[i]);
        } else if (fds[i].fd == wake_[0]) {
          char drain[16];
          while (read(wake_[0], drain, sizeof(drain)) > 0) {
          }
        } else {
          accept_from(listener);
        }
      }
      const uint64_t now = monotonic_ms();
      const bool anyone = connected();
      if (anyone) alone_since = now;
      update_output(anyone, now - alone_since);
      ask();
      if (!anyone && now - alone_since >= idle_ms) break;
    }
    close(listener);
    log_info("mixer: no client for %u ms, ending", idle_ms);
    return 0;
  }

  static void render_entry(void* self, int16_t* out, int frames) {
    static_cast<Mixer*>(self)->render(out, frames);
  }

 private:
  bool wants_input(Client* c) {
    pthread_mutex_lock(&mutex_);
    const bool room = c->used < kPushTarget;
    c->parked = !room;
    pthread_mutex_unlock(&mutex_);
    return room;
  }

  bool connected() const {
    for (const Client* c : clients_) {
      if (c && c->greeted) return true;
    }
    return false;
  }

  void accept_from(int listener) {
    const int fd = accept(listener, NULL, NULL);
    if (fd < 0) return;
    for (Client*& c : clients_) {
      if (!c) {
        c = new (std::nothrow) Client;
        if (!c) break;
        memset(c, 0, sizeof(*c));
        c->fd = fd;
        return;
      }
    }
    close(fd);  // full
  }

  void drop(Client*& c) {
    if (!c) return;
    pthread_mutex_lock(&mutex_);
    Client* gone = c;
    c = NULL;
    pthread_mutex_unlock(&mutex_);
    close(gone->fd);
    delete gone;
  }

  Client*& slot(Client* c) {
    for (Client*& s : clients_) {
      if (s == c) return s;
    }
    return clients_[0];  // not reached
  }

  void read_from(Client* c) {
    if (!c->greeted) {  // "NBTMIX2\n" or "NBTMIX1\n"
      const size_t size = sizeof(kHello2) - 1;
      const ssize_t n = recv(c->fd, c->record + c->have, size - c->have, 0);
      if (n <= 0) return drop(slot(c));
      c->have += static_cast<size_t>(n);
      if (c->have < size) return;
      const bool pull = !memcmp(c->record, kHello2, size);
      if (!pull && memcmp(c->record, kHello1, size)) return drop(slot(c));
      char reply[32];
      const int length = snprintf(reply, sizeof(reply), "NBTMIX%d %d\n", pull ? 2 : 1, kMixRate);
      if (!send_all(c->fd, reply, static_cast<size_t>(length))) return drop(slot(c));
      pthread_mutex_lock(&mutex_);
      c->greeted = c->route = true;
      c->pull = pull;
      c->target = kPullFrags * kMixFragFrames;
      c->have = 0;
      pthread_mutex_unlock(&mutex_);
      return;
    }
    const size_t want =
        c->have < 4 ? 4 - c->have : 4 + (c->record[2] | c->record[3] << 8) - c->have;
    const ssize_t n = want ? recv(c->fd, c->record + c->have, want, 0) : 0;
    if (want && n <= 0) return drop(slot(c));
    c->have += static_cast<size_t>(n);
    const size_t length = c->have >= 4 ? (c->record[2] | c->record[3] << 8) : 0;
    if (c->have < 4 || length > kMixRecordMax || c->have < 4 + length) return;
    c->have = 0;
    if (c->record[0] == 'R' && length == 1) {
      c->route = c->record[4] != 0;
    } else if (c->record[0] == 'P' && length % 4 == 0) {
      push(c, c->record + 4, length / 4);
    } else {
      drop(slot(c));
    }
  }

  // `frames` may sit at any address in the record: copied as bytes (the car
  // faults on a misaligned 16-bit load).
  void push(Client* c, const uint8_t* frames, size_t count) {
    pthread_mutex_lock(&mutex_);
    c->asked -= count < c->asked ? count : c->asked;
    for (size_t i = 0; i < count && c->used < kRingFrames; ++i, ++c->used) {
      const size_t at = (c->head + c->used) % kRingFrames;
      memcpy(&c->ring[2 * at], frames + 4 * i, 4);
    }
    pthread_mutex_unlock(&mutex_);
  }

  // Frames a version 2 client should be asked for now (under the mutex).
  static size_t lacking(const Client* c) {
    const size_t queued = c->used + c->asked;
    const size_t lack = queued < c->target ? c->target - queued : 0;
    return lack >= kCreditMin || (lack && !queued) ? lack : 0;
  }

  // Asks each version 2 client for what it lacks ('C', u32 LE frames).
  void ask() {
    for (Client*& c : clients_) {
      if (!c || !c->greeted || !c->pull) continue;
      pthread_mutex_lock(&mutex_);
      const size_t lack = lacking(c);
      pthread_mutex_unlock(&mutex_);
      if (!lack) continue;
      const uint8_t credit[8] = {'C',
                                 0,
                                 4,
                                 0,
                                 static_cast<uint8_t>(lack),
                                 static_cast<uint8_t>(lack >> 8),
                                 static_cast<uint8_t>(lack >> 16),
                                 static_cast<uint8_t>(lack >> 24)};
      const ssize_t n = send(c->fd, credit, sizeof(credit), MSG_NOSIGNAL | MSG_DONTWAIT);
      if (n == static_cast<ssize_t>(sizeof(credit))) {
        pthread_mutex_lock(&mutex_);
        c->asked += lack;
        pthread_mutex_unlock(&mutex_);
      } else if (n >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        drop(c);  // a part sent breaks the stream
      }
    }
  }

  // The output's thread. It wakes the serving loop when a client should be
  // asked for frames (version 2) or read again (version 1), so each client is
  // served as steadily as the output plays.
  void render(int16_t* out, int frames) {
    bool wake = false;
    pthread_mutex_lock(&mutex_);
    // A version 2 client that has played and cannot fill this fragment left
    // the output empty: after kMixStarvesToGrow times it is kept fuller.
    for (Client* c : clients_) {
      if (!c || !c->greeted || !c->pull) continue;
      if (c->used >= static_cast<size_t>(frames)) {
        c->played = true;
      } else if (c->played && ++c->starved >= kMixStarvesToGrow) {
        c->starved = 0;
        if (c->target < static_cast<size_t>(kPullFragsMax * kMixFragFrames)) {
          c->target += kMixFragFrames;
        }
      }
    }
    for (int i = 0; i < frames; ++i) {
      int32_t left = 0, right = 0;
      for (Client* c : clients_) {
        if (!c || !c->used) continue;
        left += c->ring[2 * c->head];
        right += c->ring[2 * c->head + 1];
        c->head = (c->head + 1) % kRingFrames;
        --c->used;
      }
      out[2 * i] = static_cast<int16_t>(left > 32767 ? 32767 : left < -32768 ? -32768 : left);
      out[2 * i + 1] = static_cast<int16_t>(right > 32767    ? 32767
                                            : right < -32768 ? -32768
                                                             : right);
    }
    for (Client* c : clients_) {
      if (!c || !c->greeted) continue;
      if (c->pull) {
        wake = wake || lacking(c);
      } else if (c->parked && c->used < kPushTarget) {
        c->parked = false;
        wake = true;
      }
    }
    pthread_mutex_unlock(&mutex_);
    const char byte = 1;
    if (wake && write(wake_[1], &byte, 1) < 0) {
      // full: the loop is already due to wake
    }
  }

  // The device opens with the first client and closes kIdleCloseMs after the last.
  void update_output(bool anyone, uint64_t alone_ms) {
    bool route = false;
    for (const Client* c : clients_) route = route || (c && c->greeted && c->route);
    if (device_open_ && !capture_) watch_stalls(route);
    if (anyone && !device_open_) {
      device_open_ = capture_ ? start_capture() : open_output(route);
      route_ = route;
      if (!device_open_) log_info("mixer: no audio output");
    } else if (device_open_ && !anyone && alone_ms >= kIdleCloseMs) {
      close_output();
    } else if (device_open_ && !capture_ && route != route_) {
      out_.set_route_wanted(route);
      route_ = route;
    }
  }

  bool open_output(bool route) {
    const MixLevel level = mix_level(level_);
    out_.set_fragment_frames(kMixFragFrames);
    out_.set_buffer_frags(level.frags);
    out_.set_a2i_buffers(level.a2i_buffers);
    stalls_seen_ = 0;
    stall_count_ = 0;
    return out_.open(kMixRate, &Mixer::render_entry, this, route);
  }

  // The output played kMixStallsToStep gaps within kMixStallWindowMs: it
  // queues too little for this car now. The next level, kept in the state
  // file, at the price of a moment's silence while the output opens again.
  void watch_stalls(bool route) {
    const unsigned stalls = out_.stalls();
    if (stalls == stalls_seen_) return;
    const uint64_t now = monotonic_ms();
    for (; stalls_seen_ != stalls; ++stalls_seen_) {
      stall_ms_[stall_count_ % kMixStallsToStep] = now;
      ++stall_count_;
    }
    if (stall_count_ < kMixStallsToStep || level_ + 1 >= kMixLevels) return;
    if (now - stall_ms_[stall_count_ % kMixStallsToStep] > kMixStallWindowMs) return;
    ++level_;
    mix_state_write(level_);
    log_info("mixer: the output played gaps, level %d from now on", level_);
    out_.close();
    device_open_ = open_output(route);
    if (!device_open_) log_info("mixer: no audio output");
  }

  void close_output() {
    if (!device_open_) return;
    if (capture_) {
      capture_stop_ = true;
      pthread_join(capture_thread_, NULL);
    } else {
      out_.close();
    }
    device_open_ = false;
  }

  // Tests: the device's stand-in. Its own thread takes a fragment each time
  // the device would, at the mixer's rate, and writes it to the file; the
  // time of its first frame goes to <file>.t0.
  bool start_capture() {
    capture_stop_ = false;
    return pthread_create(&capture_thread_, NULL, &Mixer::capture_entry, this) == 0;
  }

  static void* capture_entry(void* self) {
    static_cast<Mixer*>(self)->capture_loop();
    return NULL;
  }

  void capture_loop() {
    int16_t buf[2 * kMixFragFrames];
    const uint64_t started = monotonic_us();
    char path[512];
    snprintf(path, sizeof(path), "%s.t0", capture_path_);
    FILE* t0 = fopen(path, "w");
    if (t0) {
      fprintf(t0, "%llu\n", static_cast<unsigned long long>(started));
      fclose(t0);
    }
    uint64_t produced = 0;
    while (!capture_stop_) {
      if ((produced + kMixFragFrames) * 1000000u > (monotonic_us() - started) * kMixRate) {
        usleep(500);
        continue;
      }
      render(buf, kMixFragFrames);
      fwrite(buf, 4, kMixFragFrames, capture_);
      produced += kMixFragFrames;
    }
    fflush(capture_);
  }

  pthread_mutex_t mutex_;
  int wake_[2];
  Client* clients_[kMixClientsMax];
  AudioPcm out_;
  bool device_open_, route_;
  int level_;
  unsigned stalls_seen_, stall_count_;
  uint64_t stall_ms_[kMixStallsToStep];  // the last gaps' times, a ring
  const char* capture_path_;
  FILE* capture_;
  pthread_t capture_thread_;
  volatile bool capture_stop_;
};

// One connection, greeted in `version`: the socket, or -1.
int connect_version(const struct sockaddr_in& where, int version, int* rate) {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  const int buffer = kSocketBuffer, on = 1;  // little latency between the application and the mix
  if (fd < 0) return -1;
  (void)setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffer, sizeof(buffer));
  (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
  char reply[32];
  size_t have = 0;
  const char* hello = version == 2 ? kHello2 : kHello1;
  if (connect(fd, reinterpret_cast<const struct sockaddr*>(&where), sizeof(where)) ||
      !send_all(fd, hello, sizeof(kHello1) - 1)) {
    close(fd);
    return -1;
  }
  const uint64_t deadline = monotonic_ms() + 1000;
  while (have < sizeof(reply) - 1 && (!have || reply[have - 1] != '\n')) {
    const uint64_t now = monotonic_ms();
    struct pollfd p = {fd, POLLIN, 0};
    if (now >= deadline || poll(&p, 1, static_cast<int>(deadline - now)) <= 0) break;
    const ssize_t n = recv(fd, reply + have, 1, 0);
    if (n <= 0) break;
    have += static_cast<size_t>(n);
  }
  reply[have] = 0;
  int agreed = 0, value = 0;
  if (sscanf(reply, "NBTMIX%d %d\n", &agreed, &value) != 2 || agreed != version || value < 8000 ||
      value > 48000) {
    close(fd);
    return -1;
  }
  *rate = value;
  return fd;
}

}  // namespace

// Version 2 first when asked; a mixer of an older SDK closes on its greeting,
// and version 1 follows.
int mix_connect(const char* address, int* rate, bool* pull) {
  struct sockaddr_in where;
  if (!parse_address(address, &where) || !rate || !pull) return -1;
  if (*pull) {
    const int fd = connect_version(where, 2, rate);
    if (fd >= 0) return fd;
    *pull = false;
  }
  return connect_version(where, 1, rate);
}

// One send per record: a header sent alone would wait for its ACK (Nagle).
bool mix_send(int fd, char type, const void* data, uint16_t size) {
  if (size > kMixRecordMax) return false;
  uint8_t record[4 + kMixRecordMax];
  record[0] = static_cast<uint8_t>(type);
  record[1] = 0;
  record[2] = static_cast<uint8_t>(size);
  record[3] = static_cast<uint8_t>(size >> 8);
  if (size) memcpy(record + 4, data, size);
  return send_all(fd, record, 4u + size);
}

MixLevel mix_level(int level) {
  return kLevels[level < 0 ? 0 : level >= kMixLevels ? kMixLevels - 1 : level];
}

const char* mix_state_path() {
  const char* path = getenv("NBT_MIX_STATE");
  if (path && path[0]) return path;
#ifdef __QNXNTO__
  return "/dev/shmem/nbt-mixer-level";
#else
  return NULL;
#endif
}

int mix_state_read() {
  const char* path = mix_state_path();
  FILE* in = path ? fopen(path, "r") : NULL;
  int level = 0;
  if (in) {
    if (fscanf(in, "%d", &level) != 1 || level < 0 || level >= kMixLevels) level = 0;
    fclose(in);
  }
  return level;
}

void mix_state_write(int level) {
  const char* path = mix_state_path();
  FILE* out = path ? fopen(path, "w") : NULL;
  if (!out) return;
  fprintf(out, "%d\n", level);
  fclose(out);
}

int mix_serve(const char* address, unsigned idle_ms) {
  Mixer* mixer = new (std::nothrow) Mixer;  // its client rings are large: not on the stack
  if (!mixer) return 1;
  const int result = mixer->serve(address, idle_ms);
  delete mixer;
  return result;
}

}  // namespace app
}  // namespace nbt
