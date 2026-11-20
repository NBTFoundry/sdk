/* SPDX-License-Identifier: MPL-2.0 */
/* strlen, strcmp and memchr for modules and guests, in ARM state. picolibc's
 * are hand-written Thumb-2 assembly, and Thumb code in a guest dies with
 * SIGILL on the car's QNX 6.5 kernel (in strcmp, in a car test of two
 * guests), while the same code in ARM state runs. These read whole words
 * only at aligned addresses, so never past the page a string or buffer ends
 * in, and make no unaligned access. Built with -fno-builtin (module.mk): the
 * compiler must not turn these loops back into calls to themselves. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ONES 0x01010101u
#define HIGHS 0x80808080u
/* Non-zero when one of w's bytes is zero. */
#define HAS_ZERO(w) (((w) - ONES) & ~(w) & HIGHS)
/* Words read over char data. */
typedef uint32_t __attribute__((may_alias)) word;

size_t strlen(const char* s) {
  const char* p = s;
  while ((uintptr_t)p & 3) {
    if (!*p) return (size_t)(p - s);
    ++p;
  }
  const word* w = (const word*)(const void*)p;
  while (!HAS_ZERO(*w)) ++w;
  p = (const char*)w;
  while (*p) ++p;
  return (size_t)(p - s);
}

int strcmp(const char* a, const char* b) {
  const unsigned char* x = (const unsigned char*)a;
  const unsigned char* y = (const unsigned char*)b;
  if ((((uintptr_t)x ^ (uintptr_t)y) & 3) == 0) {
    while ((uintptr_t)x & 3) {
      if (*x != *y || !*x) return *x - *y;
      ++x, ++y;
    }
    const word* wx = (const word*)(const void*)x;
    const word* wy = (const word*)(const void*)y;
    while (*wx == *wy && !HAS_ZERO(*wx)) ++wx, ++wy;
    x = (const unsigned char*)wx;
    y = (const unsigned char*)wy;
  }
  while (*x == *y && *x) ++x, ++y;
  return *x - *y;
}

void* memchr(const void* s, int c, size_t n) {
  const unsigned char* p = (const unsigned char*)s;
  const unsigned char ch = (unsigned char)c;
  while (n && ((uintptr_t)p & 3)) {
    if (*p == ch) return (void*)(uintptr_t)p;
    ++p, --n;
  }
  const uint32_t pattern = ch * ONES;
  const word* w = (const word*)(const void*)p;
  while (n >= 4) {
    const uint32_t v = *w ^ pattern;
    if (HAS_ZERO(v)) break;
    ++w, n -= 4;
  }
  p = (const unsigned char*)w;
  while (n) {
    if (*p == ch) return (void*)(uintptr_t)p;
    ++p, --n;
  }
  return NULL;
}
