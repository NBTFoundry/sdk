/* SPDX-License-Identifier: MPL-2.0 */
/* Throughput of the C library's memcpy, memset and malloc/free, with the same
 * loops wherever they run: in a module or guest (picolibc; the car probe) and
 * in the runner (the head unit's libc.so.3; NBT_RUNNER_BENCH=1), so the
 * numbers compare. */
#ifndef NBT_COMMON_MEMBENCH_H
#define NBT_COMMON_MEMBENCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nbt_membench {
  double memcpy_mbs; /* MB/s, 4 MiB buffers */
  double memset_mbs;
  double malloc_ns; /* per malloc + free of 16 to 271 bytes */
} nbt_membench;

/* now_ns: a monotonic clock. 0, or -1 when the buffers could not be had. */
int nbt_membench_run(nbt_membench* out, int64_t (*now_ns)(void));

#ifdef __cplusplus
}
#endif

#endif
