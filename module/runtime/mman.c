/* SPDX-License-Identifier: MPL-2.0 */
/* Memory mappings, as JIT compilers and emulators use them: anonymous
 * mappings committed on first use, protection changes (executable pages
 * included) and the instruction cache flush behind __clear_cache, all over
 * the host's page-granular memory calls (host_abi.h). There are no file
 * mappings and no fixed addresses; an address hint is ignored, as POSIX
 * allows. */
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>

#include "nbt_module.h"

#define PAGE 4096u

/* `len` rounded up to whole pages, or 0 when zero or too large. */
static uint32_t pages(size_t len) {
  if (len == 0 || len > UINT32_MAX - (PAGE - 1)) return 0;
  return ((uint32_t)len + PAGE - 1) & ~(PAGE - 1);
}

static int32_t host_prot(int prot) {
  return (prot & PROT_READ ? NBT_PROT_READ : 0) | (prot & PROT_WRITE ? NBT_PROT_WRITE : 0) |
         (prot & PROT_EXEC ? NBT_PROT_EXEC : 0);
}

void* mmap(void* addr, size_t len, int prot, int flags, int fd, off_t offset) {
  (void)addr;
  (void)fd; /* ignored for anonymous mappings, as on Linux */
  (void)offset;
  int share = flags & (MAP_SHARED | MAP_PRIVATE);
  if ((prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) || (flags & MAP_FIXED) ||
      (share != MAP_SHARED && share != MAP_PRIVATE)) {
    errno = EINVAL;
    return MAP_FAILED;
  }
  if (!(flags & MAP_ANONYMOUS)) {
    errno = ENODEV; /* no file mappings */
    return MAP_FAILED;
  }
  uint32_t size = pages(len);
  if (!size) {
    errno = len ? ENOMEM : EINVAL;
    return MAP_FAILED;
  }
  /* Without processes to share with, a shared anonymous mapping is a private
   * one. The host's anonymous pages start zeroed. */
  void* p = nbt_host->mem_map(size, host_prot(prot), NBT_MAP_LAZY);
  if (!p) {
    errno = ENOMEM;
    return MAP_FAILED;
  }
  return p;
}

static int page_call(void* addr, size_t len, int prot, int unmap) {
  uint32_t size = pages(len);
  if (((uintptr_t)addr & (PAGE - 1)) || !size) {
    errno = EINVAL;
    return -1;
  }
  int32_t rc = unmap ? nbt_host->mem_unmap(addr, size) : nbt_host->mem_protect(addr, size, host_prot(prot));
  return rc < 0 ? nbt_fail(rc) : 0;
}

int munmap(void* addr, size_t len) { return page_call(addr, len, 0, 1); }

int mprotect(void* addr, size_t len, int prot) {
  if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) {
    errno = EINVAL;
    return -1;
  }
  return page_call(addr, len, prot, 0);
}

/* What the compiler calls for __builtin___clear_cache: makes code written to
 * [start, end) visible to instruction fetch. compiler-rt's bare-metal one
 * aborts, so the toolchain leaves it out. */
void __clear_cache(void* start, void* end) {
  if ((uintptr_t)end > (uintptr_t)start)
    nbt_host->icache_flush(start, (uint32_t)((uintptr_t)end - (uintptr_t)start));
}
