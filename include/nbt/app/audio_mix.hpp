// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_APP_AUDIO_MIX_HPP
#define NBT_APP_AUDIO_MIX_HPP

/* NBT Manager's audio mixer: one process (nbt-runner --mixer) owns the head
 * unit's audio output and plays every application's and service's sound at
 * once (NBT Manager's docs/audio-mixer.md). AudioPcm uses it by itself when
 * the host names it in NBT_AUDIO_MIXER ("127.0.0.1:<port>").
 *
 * Wire, over TCP on the loopback: the client sends "NBTMIX1\n", the mixer
 * answers "NBTMIX1 <rate>\n"; then the client sends records of a 4-byte
 * header (type, 0, length u16 LE) and `length` bytes:
 *   'P'  stereo S16LE frames at <rate>, at most kMixRecordMax bytes
 *   'R'  one byte: 1 while the client wants the audio connection, 0 not
 * TCP's flow control paces a client to what the mixer plays. */

#include <stdint.h>

namespace nbt {
namespace app {

enum {
  kMixPort = 42863,
  kMixRate = 44100,
  kMixRecordMax = 4096,
  kMixClientsMax = 16,
};

/* Connects to the mixer at `address` ("a.b.c.d:port") within a second: the
 * socket, and the mixer's rate in *rate, or -1. */
int mix_connect(const char* address, int* rate);
/* Sends one record; false when the mixer is gone. */
bool mix_send(int fd, char type, const void* data, uint16_t size);
/* The mixer itself: serves on `address` until no client has come for
 * `idle_ms`. 0 when it ended idle, 3 when another mixer has the address, 1 on
 * an error. NBT_MIX_CAPTURE=<file> writes the mix there instead of playing it
 * (tests). */
int mix_serve(const char* address, unsigned idle_ms);

}  // namespace app
}  // namespace nbt

#endif
