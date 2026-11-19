/* SPDX-License-Identifier: MPL-2.0 */
#include "check.h"

#include <stdio.h>

static int passed, total;
static char status[32] = "probe starting";

void check_detail(const char* name, int ok, const char* detail) {
  ++total;
  if (ok) ++passed;
  printf("%-28s %s%s%s\n", name, ok ? "ok" : "FAIL", detail ? "  " : "", detail ? detail : "");
  fflush(stdout);
  snprintf(status, sizeof(status), "probe %d/%d ok", passed, total);
}

void check(const char* name, int ok) { check_detail(name, ok, NULL); }

const char* check_status(void) { return status; }

int check_summary(void) {
  printf("probe: %d/%d ok\n", passed, total);
  fflush(stdout);
  return passed == total ? 0 : 1;
}
