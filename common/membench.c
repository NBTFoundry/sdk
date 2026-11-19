/* SPDX-License-Identifier: MPL-2.0 */
#include "membench.h"

#include <stdlib.h>
#include <string.h>

/* Results read back through this, so no loop is optimized away. */
static volatile unsigned sink;

static double mb_per_s(double bytes, int64_t ns) { return ns > 0 ? bytes / ((double)ns / 1e9) / 1e6 : 0; }

int nbt_membench_run(nbt_membench* out, int64_t (*now_ns)(void)) {
  enum { kSize = 4 << 20, kRounds = 32, kAllocs = 200000 };
  memset(out, 0, sizeof(*out));
  char* a = (char*)malloc(kSize);
  char* b = (char*)malloc(kSize);
  if (!a || !b) {
    free(a);
    free(b);
    return -1;
  }
  memset(a, 1, kSize);
  int64_t t = now_ns();
  for (int i = 0; i < kRounds; ++i) {
    memcpy(b, a, kSize);
    a[i] = b[kSize - 1 - i];
    sink += (unsigned char)b[i * 4099];
  }
  out->memcpy_mbs = mb_per_s((double)kSize * kRounds, now_ns() - t);
  t = now_ns();
  for (int i = 0; i < kRounds; ++i) {
    memset(b, i, kSize);
    sink += (unsigned char)b[i * 4099];
  }
  out->memset_mbs = mb_per_s((double)kSize * kRounds, now_ns() - t);
  free(a);
  free(b);
  t = now_ns();
  for (int i = 0; i < kAllocs; ++i) {
    char* volatile p = (char*)malloc(16 + (i & 255)); /* volatile: the pair stays */
    p[0] = (char)i;
    free(p);
  }
  out->malloc_ns = (double)(now_ns() - t) / kAllocs;
  return 0;
}
