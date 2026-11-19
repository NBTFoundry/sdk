/* SPDX-License-Identifier: MPL-2.0 */
/* The guest's side of the GL command stream (include/nbt/gl_stream.h). One
 * GL thread: like a context, the stream is not shared between threads. */
#ifndef NBT_GUEST_GL_STREAM_H
#define NBT_GUEST_GL_STREAM_H

#include <stddef.h>
#include <stdint.h>

#include "gl_commands.h" /* generated */
#include "nbt/gl_stream.h"

struct nbtgl_entry {
  const char* name;
  void* fn;
};
extern const struct nbtgl_entry nbtgl_entries[NBTGL_ID_END - 1]; /* generated */

/* The arena and the call that runs the commands (set by guest/host.c):
 * run(bytes) executes them in the runner and waits; < 0 when it failed. */
void nbtgl_attach(volatile uint8_t* arena, int (*run)(uint32_t bytes));
/* Commands written since the last run, for video_swap to carry. */
uint32_t nbtgl_take(void);
void* nbtgl_proc(const char* name);

/* For the encoders: reserve `bytes` (header included) for command `id`,
 * fill, commit. */
uint32_t* nbtgl_begin(uint32_t id, uint64_t bytes);
size_t nbtgl_put(uint32_t* w, size_t at, const void* data, size_t bytes);
void nbtgl_end(uint32_t* w, size_t at);
/* Runs everything written so far and waits: the result area, or NULL. */
const uint8_t* nbtgl_sync(void);

#endif
