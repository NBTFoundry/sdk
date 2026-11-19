/* SPDX-License-Identifier: MPL-2.0 */
/* Mode A: a module (ET_DYN, no imports, one export) loaded into the runner. */
#include <dlfcn.h>
#include <stdio.h>

#include "run.h"

int runner_run_module(const char* path, int argc, char** argv, const nbt_host_v1* host) {
  void* lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!lib) {
    printf("runner: cannot load %s: %s\n", path, dlerror());
    return RUNNER_CANNOT_START;
  }
  nbt_module_entry_v1 entry = (nbt_module_entry_v1)dlsym(lib, NBT_MODULE_ENTRY_V1);
  if (!entry) {
    printf("runner: no %s in %s\n", NBT_MODULE_ENTRY_V1, path);
    dlclose(lib);
    return RUNNER_NOT_RUNNABLE;
  }
  Dl_info info;
  if (dladdr((void*)entry, &info)) printf("runner: module loaded at %p\n", info.dli_fbase);
  fflush(stdout);
  const int status = entry(host, argc, argv);
  fflush(stdout);
  if (dlclose(lib) != 0) printf("runner: dlclose failed: %s\n", dlerror());
  return status;
}
