/* SPDX-License-Identifier: MPL-2.0 */
/* Feasibility runner for the QNX 6.5 VM: the runner's system services
 * (runner/host_sys.c) without the NBT Manager bridge, video or audio.
 *
 *   runner-probe-qnx <module.so> [module arguments...]
 *   runner-probe-qnx --guest [--expect-fault] [--timeout S] <guest> [arguments...]
 *
 * Mode A loads the module with dlopen(RTLD_NOW | RTLD_LOCAL), runs it,
 * unloads it, loads it again and runs it with --reload: PASS when both runs
 * return 0. Mode B runs a guest process (runner/guest_server.c): PASS on exit
 * status 0, or with --expect-fault on a crash or a hang. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_sys.h"
#include "run.h"
#include "nbt/host_abi.h"

static nbt_host_v1 host;

static void usage(const char* self) {
  fprintf(stderr,
          "usage: %s <module.so> [arguments...]\n"
          "       %s --guest [--expect-fault] [--timeout SECONDS] <guest> [arguments...]\n",
          self, self);
}

int main(int argc, char** argv) {
  if (argc < 2) {
    usage(argv[0]);
    return 2;
  }
  const char* dir = getenv("NBT_DATA_DIR");
  if (runner_sys_init(dir && *dir ? dir : "/tmp", getenv("NBT_FILES_DIR")) != 0) return 2;
  memset(&host, 0, sizeof(host));
  runner_sys_fill(&host);

  if (strcmp(argv[1], "--guest") == 0) {
    int i = 2, expect_fault = 0, timeout_s = 60;
    for (; i < argc && argv[i][0] == '-'; ++i) {
      if (strcmp(argv[i], "--expect-fault") == 0) {
        expect_fault = 1;
      } else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
        timeout_s = atoi(argv[++i]);
      } else {
        usage(argv[0]);
        return 2;
      }
    }
    if (i >= argc) {
      usage(argv[0]);
      return 2;
    }
    char* gargv[32];
    int gn = 0;
    gargv[gn++] = (char*)"probe";
    for (int k = i + 1; k < argc && gn < 31; ++k) gargv[gn++] = argv[k];
    gargv[gn] = NULL;
    int status = runner_run_guest(argv[i], gargv, &host, timeout_s);
    int faulted = status == RUNNER_HUNG || (status > RUNNER_SIGNALED && status < RUNNER_SIGNALED + 32);
    int pass = expect_fault ? faulted : status == 0;
    printf("runner: %s\n", pass ? "PASS" : "FAIL");
    fflush(stdout);
    return pass ? 0 : 1;
  }

  char* first[32];
  int n = 0;
  first[n++] = (char*)"probe";
  for (int i = 2; i < argc && n < 31; ++i) first[n++] = argv[i];
  first[n] = NULL;
  int status = runner_run_module(argv[1], n, first, &host);
  printf("runner: first run status=%d\n", status);

  /* A second load must start from a fresh copy: constructors run again. */
  host.thread_word_set(NULL);
  char* again[] = {(char*)"probe", (char*)"--reload", NULL};
  int reload = runner_run_module(argv[1], 2, again, &host);
  printf("runner: reload status=%d\n", reload);
  printf("runner: %s\n", status == 0 && reload == 0 ? "PASS" : "FAIL");
  fflush(stdout);
  return status == 0 && reload == 0 ? 0 : 1;
}
