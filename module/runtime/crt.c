/* SPDX-License-Identifier: MPL-2.0 */
/* Module entry: takes the host table, runs the module's constructors (which
 * the loader never sees: module/module.ld moves them out of .init_array), and
 * calls main. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "nbt_module.h"

const nbt_host_v1* nbt_host;

typedef void (*nbt_init_fn)(void);
extern nbt_init_fn __nbt_preinit_array_start[] NBT_HIDDEN;
extern nbt_init_fn __nbt_preinit_array_end[] NBT_HIDDEN;
extern nbt_init_fn __nbt_init_array_start[] NBT_HIDDEN;
extern nbt_init_fn __nbt_init_array_end[] NBT_HIDDEN;
extern nbt_init_fn __nbt_fini_array_start[] NBT_HIDDEN;
extern nbt_init_fn __nbt_fini_array_end[] NBT_HIDDEN;

int main(int argc, char** argv);

/* ABI identity read by the installer and the runner without running code.
 * Mode 1: a module the runner loads; mode 2: a guest process (guest/). */
#ifndef NBT_MODULE_MODE
#define NBT_MODULE_MODE 1
#endif
struct nbt_note {
  uint32_t namesz, descsz, type;
  char name[4];
  uint32_t abi_major, abi_minor, mode;
};
__attribute__((section(".note.nbt"), used, aligned(4))) static const struct nbt_note nbt_note = {
    4, 12, 1, {'N', 'B', 'T', '\0'}, NBT_HOST_ABI_MAJOR, NBT_HOST_ABI_MINOR, NBT_MODULE_MODE};

uintptr_t __stack_chk_guard = 0x5f3a9c1du;

/* Identifies this module to __cxa_atexit (C++ static destructors). */
void* __dso_handle NBT_HIDDEN = &__dso_handle;

static int32_t nbt_initialized;

static void run_array(nbt_init_fn* start, nbt_init_fn* end) {
  for (nbt_init_fn* f = start; f < end; ++f) (*f)();
}

/* Destructors, atexit handlers and C++ static destructors, newest first.
 * Also what picolibc's exit() calls before _exit. */
void __libc_fini_array(void) {
  for (nbt_init_fn* f = __nbt_fini_array_end; f > __nbt_fini_array_start;) (*--f)();
}

__attribute__((visibility("default"))) int32_t nbt_module_v1(const nbt_host_v1* host, int32_t argc,
                                                              char** argv) {
  if (!host || host->abi_major != NBT_HOST_ABI_MAJOR ||
      host->size < offsetof(nbt_host_v1, exit_process) + sizeof(host->exit_process)) {
    return 127;
  }
  nbt_host = host;
  if (!nbt_initialized) {
    uintptr_t guard;
    if (host->random_bytes && host->random_bytes(&guard, sizeof(guard)) == (int32_t)sizeof(guard)) {
      __stack_chk_guard = guard & ~(uintptr_t)0xff; /* a zero byte stops string overruns */
    }
    nbt_thread_init_main();
    run_array(__nbt_preinit_array_start, __nbt_preinit_array_end);
    run_array(__nbt_init_array_start, __nbt_init_array_end);
    nbt_initialized = 1;
  }
  int status = main(argc, argv);
  nbt_thread_run_destructors(nbt_thread_current()); /* the main thread's thread_locals */
  __libc_fini_array();
  fflush(stdout);
  fflush(stderr);
  return status;
}

void __stack_chk_fail(void) {
  static const char msg[] = "module: stack smashing detected\n";
  nbt_host->write_output(2, msg, sizeof(msg) - 1);
  nbt_host->exit_process(134);
  for (;;) {
  }
}
