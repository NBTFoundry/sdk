/* SPDX-License-Identifier: MPL-2.0 */
/* Running an application payload with a host table (include/nbt/host_abi.h),
 * shared by nbt-runner and the feasibility probe. Both return the
 * application's status, or one of these. */
#ifndef NBT_RUNNER_RUN_H
#define NBT_RUNNER_RUN_H

#include "nbt/host_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  RUNNER_HUNG = 124,         /* a guest killed after its time limit */
  RUNNER_CANNOT_START = 125, /* the payload did not load or spawn */
  RUNNER_NOT_RUNNABLE = 126, /* no entry point, or a guest that never answered the arena */
  RUNNER_SIGNALED = 128      /* + the signal that killed a guest */
};

/* Mode A: dlopen (RTLD_NOW | RTLD_LOCAL), the module's entry, dlclose. */
int runner_run_module(const char* path, int argc, char** argv, const nbt_host_v1* host);

/* Mode B (QNX only): spawns the guest at `path` with `argv` and serves its
 * requests with `host` until it ends; a guest still running after timeout_s
 * (0: no limit) is killed as hung. */
int runner_run_guest(const char* path, char* const argv[], const nbt_host_v1* host, int timeout_s);

#ifdef __cplusplus
}
#endif

#endif
