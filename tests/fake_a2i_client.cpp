// SPDX-License-Identifier: MPL-2.0
/* A test double of the head unit's liba2i_client (A2iOutput's calls): a
 * fixed set of buffers, GetNotification "plays" one, the samples written are
 * kept for the test (fake_a2i_state).
 * NBT_FAKE_A2I_PACE=1: the buffers are played at the stream's rate instead,
 * from Start on, as the head unit does; with NBT_FAKE_A2I_GAP_MS=<n> the
 * output also stands still for 12 ms every n ms, and whenever it has nothing
 * queued (an underrun). */
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "nbt/app/audio_a2i.hpp"

using nbt::app::A2iParams;

namespace {

struct FakeA2i {
  A2iParams params;
  char endpoint[32];
  int opens, starts, stops, closes;
  int writes_before_start;
  unsigned queued;
  unsigned taken;
  unsigned char* slots;
  unsigned char captured[65536];
  unsigned captured_bytes;
  bool paced, started_this_open;
  uint64_t gap_period_us, start_us, lost_us, written_bytes;
};

FakeA2i g_fake;

uint64_t now_us() {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return static_cast<uint64_t>(t.tv_sec) * 1000000u + static_cast<uint64_t>(t.tv_nsec) / 1000u;
}

/* Paced: what is still queued, from the clock. */
void play() {
  if (!g_fake.paced || !g_fake.started_this_open) return;
  const uint64_t per_second = static_cast<uint64_t>(g_fake.params.rate) * 4u;
  uint64_t t = now_us() - g_fake.start_us - g_fake.lost_us;
  if (g_fake.gap_period_us) {
    const uint64_t still = t / g_fake.gap_period_us * 12000u;
    t -= still < t ? still : t;
  }
  uint64_t played = t * per_second / 1000000u;
  if (played > g_fake.written_bytes) { /* nothing queued: it stood still */
    g_fake.lost_us += (played - g_fake.written_bytes) * 1000000u / per_second;
    played = g_fake.written_bytes;
  }
  const uint64_t left = g_fake.written_bytes - played;
  g_fake.queued =
      static_cast<unsigned>((left + g_fake.params.buffer_bytes - 1) / g_fake.params.buffer_bytes);
}

}  // namespace

extern "C" {

/* opens, starts, stops, closes, writes before the start. */
__attribute__((visibility("default"))) void fake_a2i_counts(int out[5]) {
  out[0] = g_fake.opens;
  out[1] = g_fake.starts;
  out[2] = g_fake.stops;
  out[3] = g_fake.closes;
  out[4] = g_fake.writes_before_start;
}

__attribute__((visibility("default"))) void fake_a2i_params(A2iParams* out) {
  *out = g_fake.params;
}

__attribute__((visibility("default"))) unsigned fake_a2i_captured(const unsigned char** data) {
  *data = g_fake.captured;
  return __atomic_load_n(&g_fake.captured_bytes, __ATOMIC_ACQUIRE);
}

__attribute__((visibility("default"))) int A2I_Client_Open(void** handle, const A2iParams* p) {
  g_fake.params = *p;
  strncpy(g_fake.endpoint, p->endpoint ? p->endpoint : "", sizeof(g_fake.endpoint) - 1);
  g_fake.params.endpoint = g_fake.endpoint;
  g_fake.opens++;
  delete[] g_fake.slots;
  g_fake.slots = new unsigned char[p->buffers * p->buffer_bytes];
  g_fake.queued = g_fake.taken = 0;
  const char* pace = getenv("NBT_FAKE_A2I_PACE");
  const char* gap = getenv("NBT_FAKE_A2I_GAP_MS");
  g_fake.paced = pace && pace[0] == '1';
  g_fake.gap_period_us = gap ? strtoul(gap, NULL, 10) * 1000u : 0;
  g_fake.lost_us = g_fake.written_bytes = 0;
  g_fake.started_this_open = false;
  *handle = &g_fake;
  return 0;
}

__attribute__((visibility("default"))) int A2I_Client_Start(void*) {
  g_fake.starts++;
  g_fake.start_us = now_us();
  g_fake.started_this_open = true;
  return 0;
}

__attribute__((visibility("default"))) int A2I_Client_Stop(void*) {
  g_fake.stops++;
  return 0;
}

__attribute__((visibility("default"))) int A2I_Client_Close(void** handle) {
  g_fake.closes++;
  delete[] g_fake.slots;
  g_fake.slots = NULL;
  *handle = NULL;
  return 0;
}

__attribute__((visibility("default"))) int A2I_Client_GetNotification(void*) {
  if (g_fake.paced) {
    usleep(300);
    return 0;
  }
  usleep(1000);
  if (g_fake.queued > 0) g_fake.queued--;
  return 0;
}

__attribute__((visibility("default"))) int A2I_Client_GetBuffer(void*, void** buffer,
                                                                uint32_t* bytes) {
  if (g_fake.started_this_open) play();
  if (g_fake.queued >= g_fake.params.buffers) return nbt::app::kA2iNoBuffer;
  const unsigned slot = g_fake.taken++ % g_fake.params.buffers;
  *buffer = g_fake.slots + slot * g_fake.params.buffer_bytes;
  *bytes = g_fake.params.buffer_bytes;
  return 0;
}

__attribute__((visibility("default"))) int A2I_Client_Out_Write(void*, const void* buffer,
                                                                uint32_t bytes, uint32_t) {
  if (!g_fake.starts) g_fake.writes_before_start++;
  g_fake.queued++;
  g_fake.written_bytes += bytes;
  unsigned n = bytes;
  if (n > sizeof(g_fake.captured) - g_fake.captured_bytes) {
    n = sizeof(g_fake.captured) - g_fake.captured_bytes;
  }
  memcpy(g_fake.captured + g_fake.captured_bytes, buffer, n);
  __atomic_store_n(&g_fake.captured_bytes, g_fake.captured_bytes + n, __ATOMIC_RELEASE);
  return 0;
}

}  // extern "C"
