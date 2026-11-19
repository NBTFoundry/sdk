/* SPDX-License-Identifier: MPL-2.0 */
/* The runner's side of a guest's GL command stream (include/nbt/gl_stream.h):
 * decode and execute on the runner's context. The commands come from the
 * guest: every size and offset is checked against the batch. */
#ifndef NBT_RUNNER_GL_RUNNER_H
#define NBT_RUNNER_GL_RUNNER_H

#include <stddef.h>
#include <stdint.h>

#include "gl_commands.h"  /* generated */
#include "gl_functions.h" /* generated */
#include "nbt/gl_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

extern struct nbtgl_functions nbtgl_fn;
/* Fills nbtgl_fn; returns how many entry points are missing. */
int nbtgl_load(void* (*lookup)(const char* name));
/* Runs `bytes` of commands from the arena's command area; results of a
 * synchronous call go to its result area. 0, or -1 on a malformed batch. */
int nbtgl_run(volatile uint8_t* arena, uint32_t bytes);

/* One command's arguments at w[*pos] (the header read): executes it and
 * moves *pos past it. 1 when it wrote results, 0, or -1 when malformed. */
typedef int (*nbtgl_decoder)(const uint32_t* w, size_t n, size_t* pos);
extern const nbtgl_decoder nbtgl_decoders[NBTGL_ID_END]; /* generated */

/* For the decoders. An input pointer at w[*at]: its bytes (count in *bytes),
 * or NULL when they overrun the batch. */
const void* nbtgl_get(const uint32_t* w, size_t n, size_t* at, uint32_t* bytes);
/* The result area when `bytes` fit, else NULL. */
uint8_t* nbtgl_result(uint64_t bytes);

#ifdef __cplusplus
}
#endif

#endif
