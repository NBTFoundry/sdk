/* SPDX-License-Identifier: MPL-2.0 */
/* Messages between the runner and a Mode B guest process.
 *
 * A guest is a static executable with no QNX code in it (guest/). It runs as
 * a child of the runner and implements the host ABI (host_abi.h) itself,
 * forwarding what needs the runner's services (output, files, randomness)
 * as these messages over one connection to the runner's channel.
 *
 * The runner passes, in the guest's environment:
 *   NBT_GUEST_CHANNEL=<runner pid>/<chid>
 *   NBT_GUEST_ARENA=<fd>/<address, hex>/<size>   a shared-memory object the
 *                    guest maps at the same address as the runner
 *
 * Every request is one MsgSendv: a nbt_guest_req header, then len1 bytes and
 * len2 bytes of payload. The reply is a nbt_guest_rep, then the op's output.
 * Results follow host_abi.h: >= 0, or a negative NBT_E* value.
 *
 * Compatibility: the guest's half is linked into the application, the
 * runner's ships with NBT Manager. Ops, arena words and GL commands are only
 * ever added (an unknown op answers -NBT_ENOSYS). A change an older guest
 * could not follow raises NBT_GUEST_PROTO_VERSION; a runner serves every
 * version up to its own, and a guest refuses a runner older than itself. */
#ifndef NBT_GUEST_PROTO_H
#define NBT_GUEST_PROTO_H

#include <stdint.h>

#define NBT_GUEST_PROTO_VERSION 1u
#define NBT_GUEST_MSG_TYPE 0x4e42u /* above the QNX system message range */
#define NBT_GUEST_MAX_PAYLOAD 65536u /* bytes of len1 */
#define NBT_GUEST_MAX_TEXT 4096u     /* bytes of a path or text, its \0 included; of len2 */
#define NBT_GUEST_ARENA_MAGIC 0x4e425441u /* "NBTA" */

/* The arena, the same in both processes:
 *   [0, 64 KiB)     control words (nbt_arena_word)
 *   result area     written by the runner: results of synchronous GL calls
 *   command area    written by the guest: GL commands (gl_stream.h)
 *   audio ring      written by the guest, read by the runner's audio thread */
#define NBT_ARENA_SIZE (16u << 20)
#define NBTGL_RESULT_OFF (64u << 10)
#define NBTGL_RESULT_SIZE ((6u << 20) - NBTGL_RESULT_OFF)
#define NBTGL_CMD_OFF (6u << 20)
#define NBTGL_CMD_SIZE (9u << 20)
#define NBT_AUDIO_OFF (15u << 20)
#define NBT_AUDIO_FRAMES ((1u << 20) / 4) /* stereo S16 */

/* Control words: uint32 indices into the arena. */
enum nbt_arena_word {
  NBT_ARENA_MAGIC_WORD = 0,          /* runner: NBT_GUEST_ARENA_MAGIC */
  NBT_ARENA_ANSWER_WORD = 1,         /* guest, once mapped: ~NBT_GUEST_ARENA_MAGIC */
  NBT_ARENA_RUNNER_VERSION_WORD = 2, /* runner: its NBT_GUEST_PROTO_VERSION */
  NBT_ARENA_GUEST_VERSION_WORD = 3,  /* guest: its NBT_GUEST_PROTO_VERSION */
  NBT_AUDIO_WRITE_WORD = 4,          /* guest: frames written, wraps */
  NBT_AUDIO_READ_WORD = 5,           /* runner: frames read, wraps */
  NBT_ARENA_GUEST_CAPS_WORD = 6,     /* guest: NBT_GUEST_CAP_* it follows (0: an older guest) */
  NBT_AUDIO_TARGET_WORD = 7          /* runner: frames to keep in the ring (0: an older runner) */
};

/* NBT_ARENA_GUEST_CAPS_WORD.
 * AUDIO_TARGET: the guest keeps NBT_AUDIO_TARGET_WORD frames in the audio
 * ring and follows the word as it changes: the runner starts with two of its
 * fragments and raises it when the ring ran empty. A guest without it keeps
 * twice the fragment NBT_GUEST_AUDIO_OPEN answered, for as long as it plays. */
#define NBT_GUEST_CAP_AUDIO_TARGET 1u
#define NBT_AUDIO_TARGET_MAX 4096u

enum nbt_guest_op {
  NBT_GUEST_OUTPUT = 1, /* a = fd (1 or 2); payload: bytes */
  NBT_GUEST_OPEN,       /* a = flags, b = mode; payload: path\0 */
  NBT_GUEST_READ,       /* a = file, b = length; reply: bytes */
  NBT_GUEST_WRITE,      /* a = file; payload: bytes */
  NBT_GUEST_SEEK,       /* a = file, b = whence, d = offset */
  NBT_GUEST_CLOSE,      /* a = file */
  NBT_GUEST_STAT,       /* payload: path\0; reply: nbt_stat */
  NBT_GUEST_FSTAT,      /* a = file; reply: nbt_stat */
  NBT_GUEST_UNLINK,     /* payload: path\0 */
  NBT_GUEST_MKDIR,      /* b = mode; payload: path\0 */
  NBT_GUEST_RENAME,     /* payload: from\0 (len1), to\0 (len2) */
  NBT_GUEST_SYNC,       /* a = file */
  NBT_GUEST_RANDOM,     /* b = length (<= 256); reply: bytes */
  /* host ABI v1.1: application services */
  NBT_GUEST_APP_INFO,     /* reply: nbt_app_info */
  NBT_GUEST_APP_SETTINGS, /* b = text room; reply: uint32 generation, text; result = text length */
  NBT_GUEST_APP_READY,
  NBT_GUEST_APP_TICK,     /* payload: status\0 or nothing; result = NBT_APP_EV_* */
  NBT_GUEST_APP_POLL,     /* result = NBT_APP_EV_* */
  NBT_GUEST_APP_STATUS,   /* payload: text\0 */
  NBT_GUEST_APP_STOP,
  NBT_GUEST_BRIDGE_RECV,  /* a = source, b = room; reply: datagram */
  NBT_GUEST_DIR_OPEN,     /* payload: path\0 */
  NBT_GUEST_DIR_READ,     /* a = dir; reply: uint32 mode, name\0; result 1 or 0 at the end */
  NBT_GUEST_DIR_CLOSE,    /* a = dir */
  /* video, GL and audio (the arena: include/nbt/gl_stream.h) */
  NBT_GUEST_VIDEO_OPEN,   /* a = surface; reply: int32 width, height */
  NBT_GUEST_VIDEO_SWAP,   /* a = GL command bytes to run first */
  NBT_GUEST_VIDEO_CLOSE,
  NBT_GUEST_GL_RUN,       /* a = GL command bytes; results in the arena */
  NBT_GUEST_AUDIO_OPEN,   /* a = route wanted, b = preferred rate; reply: int32 rate, fragment */
  NBT_GUEST_AUDIO_CLOSE,
  NBT_GUEST_AUDIO_ROUTE   /* a = wanted */
};

typedef struct nbt_guest_req {
  uint16_t type; /* NBT_GUEST_MSG_TYPE */
  uint16_t op;
  int32_t a;
  int32_t b;
  int32_t c;
  int64_t d;
  uint32_t len1;
  uint32_t len2;
} nbt_guest_req;

typedef struct nbt_guest_rep {
  int64_t result;
} nbt_guest_rep;

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(nbt_guest_req) == 32, "nbt_guest_req layout");
_Static_assert(sizeof(nbt_guest_rep) == 8, "nbt_guest_rep layout");
#endif

#endif
