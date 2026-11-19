/* SPDX-License-Identifier: MPL-2.0 */
/* memcpy for modules. picolibc's fast ARMv7-A copy needs unaligned accesses,
 * which modules avoid by default (-mno-unaligned-access), so it would fall
 * back to a byte-and-word loop. This one moves 32 bytes per step with
 * load/store-multiple when source and destination share their alignment, and
 * never makes an unaligned access. Built with -fno-builtin (module.mk): the
 * compiler must not turn these loops back into memcpy calls. */
#include <stddef.h>
#include <stdint.h>

#define NBT_ALIGNED_BLOCK(d, s)                                                        \
  do {                                                                                 \
    uint32_t a0 = (s)[0], a1 = (s)[1], a2 = (s)[2], a3 = (s)[3], a4 = (s)[4], a5 = (s)[5], \
             a6 = (s)[6], a7 = (s)[7];                                                 \
    (d)[0] = a0, (d)[1] = a1, (d)[2] = a2, (d)[3] = a3, (d)[4] = a4, (d)[5] = a5,      \
    (d)[6] = a6, (d)[7] = a7;                                                          \
  } while (0)

void* memcpy(void* restrict dst, const void* restrict src, size_t n) {
  unsigned char* d = (unsigned char*)dst;
  const unsigned char* s = (const unsigned char*)src;
  if ((((uintptr_t)d ^ (uintptr_t)s) & 3) == 0) {
    while (n && ((uintptr_t)d & 3)) {
      *d++ = *s++;
      --n;
    }
    uint32_t* dw = (uint32_t*)(void*)d;
    const uint32_t* sw = (const uint32_t*)(const void*)s;
    while (n >= 32) {
      NBT_ALIGNED_BLOCK(dw, sw);
      dw += 8;
      sw += 8;
      n -= 32;
    }
    while (n >= 4) {
      *dw++ = *sw++;
      n -= 4;
    }
    d = (unsigned char*)dw;
    s = (const unsigned char*)sw;
  } else if ((((uintptr_t)d ^ (uintptr_t)s) & 1) == 0) {
    if (n && ((uintptr_t)d & 1)) {
      *d++ = *s++;
      --n;
    }
    uint16_t* dh = (uint16_t*)(void*)d;
    const uint16_t* sh = (const uint16_t*)(const void*)s;
    while (n >= 2) {
      *dh++ = *sh++;
      n -= 2;
    }
    d = (unsigned char*)dh;
    s = (const unsigned char*)sh;
  }
  while (n--) *d++ = *s++;
  return dst;
}

/* The ARM EABI names the compiler uses for copies (aligned variants too). */
void __aeabi_memcpy(void* dst, const void* src, size_t n) { memcpy(dst, src, n); }
void __aeabi_memcpy4(void* dst, const void* src, size_t n) { memcpy(dst, src, n); }
void __aeabi_memcpy8(void* dst, const void* src, size_t n) { memcpy(dst, src, n); }
