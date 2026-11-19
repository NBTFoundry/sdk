/* SPDX-License-Identifier: MPL-2.0 */
/* Internal to the module runtime: the host table and the thread record. */
#ifndef NBT_MODULE_INTERNAL_H
#define NBT_MODULE_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "nbt/host_abi.h"

#define NBT_HIDDEN __attribute__((visibility("hidden")))

extern const nbt_host_v1* nbt_host NBT_HIDDEN;

#define NBT_KEYS_MAX 64

struct nbt_thread {
  uintptr_t handle; /* host handle; 0 for the main thread */
  void* (*fn)(void*);
  void* arg;
  void* result;
  int32_t detached;
  void* specific[NBT_KEYS_MAX];
  struct nbt_tls_dtor* tls_dtors; /* C++ thread_local destructors, newest first */
};

/* The calling thread's record (created on first use for foreign threads). */
struct nbt_thread* nbt_thread_current(void) NBT_HIDDEN;
void nbt_thread_init_main(void) NBT_HIDDEN;
/* Runs the calling thread's C++ thread_local destructors, then its key
 * destructors. */
void nbt_thread_run_destructors(struct nbt_thread* self) NBT_HIDDEN;

/* errno value for an NBT_E* error (positive). */
int nbt_errno_from_host(int32_t error) NBT_HIDDEN;
/* Sets errno for a negative host result `rc` and returns -1. */
int nbt_fail(int32_t rc) NBT_HIDDEN;

/* Pseudo descriptors for the bridge input datagrams (NBT_BRIDGE_*): recv on
 * them reaches the host's bridge_recv. */
#define NBT_FD_BRIDGE 0x3f00
#define NBT_FD_IS_BRIDGE(fd) ((fd) >= NBT_FD_BRIDGE && (fd) <= NBT_FD_BRIDGE + 1)

/* Whether the runner's table has `member` (host_abi.h). */
#define NBT_HAS(member) NBT_HOST_HAS(nbt_host, member)

#endif
