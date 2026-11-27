// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_APP_AUDIO_MIX_HPP
#define NBT_APP_AUDIO_MIX_HPP

/* NBT Manager's audio mixer: one process (nbt-runner --mixer) owns the head
 * unit's audio output and plays every application's and service's sound at
 * once (NBT Manager's docs/audio-mixer.md). AudioPcm uses it by itself when
 * the host names it in NBT_AUDIO_MIXER ("127.0.0.1:<port>").
 *
 * Wire, over TCP on the loopback: the client sends "NBTMIX2\n" (or
 * "NBTMIX1\n"), the mixer answers "NBTMIX2 <rate>\n" (or "NBTMIX1 <rate>\n").
 * Then records of a 4-byte header (type, 0, length u16 LE) and `length`
 * bytes. From the client:
 *   'P'  stereo S16LE frames at <rate>, at most kMixRecordMax bytes
 *   'R'  one byte: 1 while the client wants the audio connection, 0 not
 * Version 2 (pull): the mixer asks for frames as the output plays them, so a
 * client renders them just in time:
 *   'C'  from the mixer, a u32 LE: send that many more frames
 * The mixer keeps two of its fragments queued per version 2 client (11.6 ms),
 * and one more each time that client left it empty kMixStarvesToGrow times
 * (four at most).
 * Version 1 (push, an SDK before version 2): the client sends as fast as TCP
 * takes them; the mixer reads as the output plays. */

#include <stdint.h>

namespace nbt {
namespace app {

enum {
  kMixPort = 42863,
  kMixRate = 44100,
  kMixRecordMax = 4096,
  kMixClientsMax = 16,
  kMixFragFrames = 256, /* a version 2 client's render, and the output's fragment: 5.8 ms */
  kMixLevels = 4,
  kMixStarvesToGrow = 3,
};

/* What the mixer's output queues, least first. The mixer starts at the level
 * its state file holds (0 without one) and moves to the next when the output
 * has played kMixStallsToStep gaps within kMixStallWindowMs; it writes the
 * level to the file, so it is kept until the head unit starts again.
 * `frags`: the device's fragments of kMixFragFrames (the head unit's
 * libasound queues about 2 x frags - 1 of them: 17, 29, 64 and 110 ms);
 * `a2i_buffers`: A2I's buffers of as many frames (17, 23, 46 and 93 ms). */
struct MixLevel {
  int frags;
  int a2i_buffers;
};
enum { kMixStallsToStep = 3, kMixStallWindowMs = 30000 };
/* Level `level`'s numbers (the last level's beyond it). */
MixLevel mix_level(int level);
/* The state file: NBT_MIX_STATE, else /dev/shmem/nbt-mixer-level on the head
 * unit (gone when it restarts), else none (NULL). */
const char* mix_state_path();
/* The level in the state file, 0 without one; and writing it. */
int mix_state_read();
void mix_state_write(int level);

/* Connects to the mixer at `address` ("a.b.c.d:port") within a second: the
 * socket, and the mixer's rate in *rate, or -1. `*pull` asks for version 2;
 * it is false on return when the mixer only speaks version 1. */
int mix_connect(const char* address, int* rate, bool* pull);
/* Sends one record; false when the mixer is gone. */
bool mix_send(int fd, char type, const void* data, uint16_t size);
/* The mixer itself: serves on `address` until no client has come for
 * `idle_ms`. 0 when it ended idle, 3 when another mixer has the address, 1 on
 * an error. NBT_MIX_CAPTURE=<file> writes the mix there instead of playing it,
 * paced as the output would be, and the CLOCK_MONOTONIC microsecond of its
 * first frame in <file>.t0 (tests). */
int mix_serve(const char* address, unsigned idle_ms);

}  // namespace app
}  // namespace nbt

#endif
