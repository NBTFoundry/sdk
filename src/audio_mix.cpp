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
#include <unistd.h>

#include "nbt/app/audio_pcm.hpp"
#include "nbt/app/clock.hpp"
#include "nbt/app/log.hpp"

namespace nbt {
namespace app {
namespace {

const char kHello[] = "NBTMIX1\n";

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

// A client's ring is topped up to kTargetFrames (46 ms) each time the device
// takes from it; kSocketBuffer bounds what waits in the socket besides.
enum { kRingFrames = 8192, kTargetFrames = 2048, kIdleCloseMs = 3000, kSocketBuffer = 4096 };

// One client: the record being read, then its frames waiting to be played.
// `parked` while the serving loop does not read it (its ring is full).
struct Client {
  int fd;
  bool greeted, route, parked;
  uint8_t record[4 + kMixRecordMax];
  size_t have;
  int16_t ring[kRingFrames * 2];
  size_t head, used;  // frames
};

class Mixer {
 public:
  Mixer() : device_open_(false), route_(false), capture_(NULL), capture_stop_(false) {
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
    const char* capture = getenv("NBT_MIX_CAPTURE");
    if (capture && capture[0] && !(capture_ = fopen(capture, "wb"))) {
      close(listener);
      return 1;
    }
    log_info("mixer: serving %s", address);
    uint64_t alone_since = monotonic_ms();
    while (true) {
      struct pollfd fds[2 + kMixClientsMax];
      Client* owner[2 + kMixClientsMax];
      nfds_t count = 0;
      fds[count] = {listener, POLLIN, 0};
      owner[count++] = NULL;
      fds[count] = {wake_[0], POLLIN, 0};  // a parked client has room again
      owner[count++] = NULL;
      for (Client* c : clients_) {
        if (c && wants_input(c)) {
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
    const bool room = c->used < kTargetFrames;
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
    if (!c->greeted) {  // "NBTMIX1\n"
      const ssize_t n = recv(c->fd, c->record + c->have, sizeof(kHello) - 1 - c->have, 0);
      if (n <= 0) return drop(slot(c));
      c->have += static_cast<size_t>(n);
      if (c->have < sizeof(kHello) - 1) return;
      char reply[32];
      const int length = snprintf(reply, sizeof(reply), "NBTMIX1 %d\n", kMixRate);
      if (memcmp(c->record, kHello, sizeof(kHello) - 1) ||
          !send_all(c->fd, reply, static_cast<size_t>(length))) {
        return drop(slot(c));
      }
      c->greeted = c->route = true;
      c->have = 0;
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
    for (size_t i = 0; i < count && c->used < kRingFrames; ++i, ++c->used) {
      const size_t at = (c->head + c->used) % kRingFrames;
      memcpy(&c->ring[2 * at], frames + 4 * i, 4);
    }
    pthread_mutex_unlock(&mutex_);
  }

  // The device's thread. A parked client whose ring falls below the target
  // wakes the serving loop, so each client is read as steadily as the device
  // plays and its own render is paced the same way.
  void render(int16_t* out, int frames) {
    bool wake = false;
    pthread_mutex_lock(&mutex_);
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
      if (c && c->parked && c->used < kTargetFrames) {
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
    if (anyone && !device_open_) {
      device_open_ =
          capture_ ? start_capture() : out_.open(kMixRate, &Mixer::render_entry, this, route);
      route_ = route;
      if (!device_open_) log_info("mixer: no audio output");
    } else if (device_open_ && !anyone && alone_ms >= kIdleCloseMs) {
      close_output();
    } else if (device_open_ && !capture_ && route != route_) {
      out_.set_route_wanted(route);
      route_ = route;
    }
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
  // the device would, at the mixer's rate, and writes it to the file.
  bool start_capture() {
    capture_stop_ = false;
    return pthread_create(&capture_thread_, NULL, &Mixer::capture_entry, this) == 0;
  }

  static void* capture_entry(void* self) {
    static_cast<Mixer*>(self)->capture_loop();
    return NULL;
  }

  void capture_loop() {
    int16_t buf[2 * kAudioFragFrames];
    const uint64_t started = monotonic_ms();
    uint64_t produced = 0;
    while (!capture_stop_) {
      if ((produced + kAudioFragFrames) * 1000 > (monotonic_ms() - started) * kMixRate) {
        usleep(1000);
        continue;
      }
      render(buf, kAudioFragFrames);
      fwrite(buf, 4, kAudioFragFrames, capture_);
      produced += kAudioFragFrames;
    }
    fflush(capture_);
  }

  pthread_mutex_t mutex_;
  int wake_[2];
  Client* clients_[kMixClientsMax];
  AudioPcm out_;
  bool device_open_, route_;
  FILE* capture_;
  pthread_t capture_thread_;
  volatile bool capture_stop_;
};

}  // namespace

int mix_connect(const char* address, int* rate) {
  struct sockaddr_in where;
  if (!parse_address(address, &where) || !rate) return -1;
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  const int buffer = kSocketBuffer, on = 1;  // little latency between the application and the mix
  if (fd < 0) return -1;
  (void)setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffer, sizeof(buffer));
  (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
  char reply[32];
  size_t have = 0;
  if (connect(fd, reinterpret_cast<struct sockaddr*>(&where), sizeof(where)) ||
      !send_all(fd, kHello, sizeof(kHello) - 1)) {
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
  int value = 0;
  if (sscanf(reply, "NBTMIX1 %d\n", &value) != 1 || value < 8000 || value > 48000) {
    close(fd);
    return -1;
  }
  *rate = value;
  return fd;
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

int mix_serve(const char* address, unsigned idle_ms) {
  Mixer* mixer = new (std::nothrow) Mixer;  // its client rings are large: not on the stack
  if (!mixer) return 1;
  const int result = mixer->serve(address, idle_ms);
  delete mixer;
  return result;
}

}  // namespace app
}  // namespace nbt
