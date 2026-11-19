/* SPDX-License-Identifier: MPL-2.0 */
/* The parts of the Mode B guest runtime (guest/), which implements the host
 * ABI (include/nbt/host_abi.h) inside a static process:
 *   host.c   start-up, environment, time, memory, the host table
 *   sync.c   threads and address-keyed waiting
 *   rpc.c    requests to the runner (include/nbt/guest_proto.h): output,
 *            files, randomness, application services
 *   media.c  video, GL (gl_stream.c) and audio through the shared arena
 * Each part fills its members of the host table. */
#ifndef NBT_GUEST_GUEST_H
#define NBT_GUEST_GUEST_H

#include <stdint.h>

#include "nbt/guest_proto.h"
#include "nbt/host_abi.h"

#define LOAD(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define STORE(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#define XCHG(p, v) __atomic_exchange_n((p), (v), __ATOMIC_ACQ_REL)

/* host.c */
extern volatile uint32_t* guest_arena; /* NULL when the runner shares none */
int32_t guest_error(int qnx_errno);    /* a QNX errno as -NBT_E* */
int64_t guest_monotonic_ns(void);
int32_t guest_sleep_ns(int64_t ns);

/* sync.c */
void guest_sync_init(int pid, const uint8_t* const* cpupage);
void guest_sync_fill(nbt_host_v1* host);
int32_t guest_thread_create(uintptr_t* handle, void* (*fn)(void*), void* arg, uint32_t stack_size);
int32_t guest_thread_join(uintptr_t handle, void** result);

/* rpc.c. A request: integer arguments, one payload in, the output back. */
void guest_rpc_init(int runner_coid);
void guest_rpc_fill(nbt_host_v1* host);
int64_t guest_call(uint16_t op, int32_t a, int32_t b, const void* in, uint32_t in_len, void* out, uint32_t out_len);
int32_t guest_write_output(int32_t fd, const void* buf, uint32_t len);

/* media.c */
void guest_media_fill(nbt_host_v1* host);

#endif
