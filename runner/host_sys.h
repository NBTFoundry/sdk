/* SPDX-License-Identifier: MPL-2.0 */
/* The runner's system services (include/nbt/host_abi.h): output, time,
 * memory, threads, waiting, files confined to the application's folders,
 * environment, randomness. Shared by nbt-runner and the feasibility probe. */
#ifndef NBT_RUNNER_HOST_SYS_H
#define NBT_RUNNER_HOST_SYS_H

#include <stdint.h>

#include "nbt/host_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* data_dir: read-write (relative paths resolve here); files_dir: read only,
 * may be NULL or "". Returns 0, or -1 when data_dir is unusable. */
int runner_sys_init(const char* data_dir, const char* files_dir);
/* Fills the system members of `host` (the v1.0 part and the directory calls). */
void runner_sys_fill(nbt_host_v1* host);
/* -NBT_E* for an errno value. */
int32_t runner_host_error(int e);
/* Milliseconds of CLOCK_MONOTONIC. */
uint64_t runner_now_ms(void);

#ifdef __cplusplus
}
#endif

#endif
