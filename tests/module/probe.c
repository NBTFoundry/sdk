/* SPDX-License-Identifier: MPL-2.0 */
/* Feasibility probe: an app module built without any QNX file. Each check
 * prints "ok" or "FAIL"; the last line is "probe: N/M ok". Arguments:
 *   --reload   only check that constructors ran (second load of the module)
 *   --unaligned  also try an unaligned 32-bit load (may fault on strict boards)
 *   --crash    fault at once; --hang  spin forever (runner fault detection)
 * The same source builds as a Mode A module and as a Mode B guest. */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <asm/hwcap.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "check.h"

void probe_cxx(void); /* probe_cxx.cpp */

/* 1. constructors run before main */
static volatile int ctor_value;
__attribute__((constructor)) static void probe_ctor(void) { ctor_value = 0x1234; }

/* 2. softfp calls both ways */
static double callback(float a, double b, int64_t c, int32_t d) {
  return (double)a + b * 2.0 + (double)c + (double)d * 0.5;
}

static void check_abi(void) {
  if (!nbt_host->abi_test_mix) {
    check("abi: host hooks present", 0);
    return;
  }
  double mix = nbt_host->abi_test_mix(-7, 2.5, 1.25f, (int64_t)1 << 40, -0.75f);
  double want = -7.0 + 2.5 * 2.0 + 1.25 * 3.0 + (double)((int64_t)1 << 40) + -0.75 * 5.0;
  check("abi: module calls host", mix == want);
  double back = nbt_host->abi_test_callback(callback);
  double want_back = 1.5 + 2.25 * 2.0 + (double)((int64_t)3 << 33) + -9 * 0.5;
  check("abi: host calls module", back == want_back);
}

/* 3. 64-bit arithmetic helpers from compiler-rt */
static void check_division(void) {
  volatile uint64_t a = 0xfedcba9876543210ull, b = 0x12345ull;
  volatile int64_t c = -1234567890123ll, d = 977;
  int ok = a / b == 0xe0004fa01c4dull && a % b == 0x10a4full;
  ok = ok && c / d == -1263631412ll && c % d == -599ll;
  volatile double x = (double)a;
  ok = ok && (uint64_t)x == 0xfedcba9876543000ull;
  check("compiler-rt: 64-bit div", ok);
}

/* 4. thread-local storage and errno per thread */
static __thread int tls_value = 5;

static void* tls_thread(void* arg) {
  int id = (int)(intptr_t)arg;
  if (tls_value != 5) return (void*)1;
  tls_value = 100 + id;
  errno = 1000 + id;
  struct timespec ts = {0, 2000000};
  nanosleep(&ts, NULL);
  return (void*)(intptr_t)(tls_value == 100 + id && errno == 1000 + id ? 0 : 1);
}

static void check_tls(void) {
  pthread_t t[2];
  tls_value = 7;
  errno = 7;
  int ok = 1;
  for (int i = 0; i < 2; ++i) ok &= pthread_create(&t[i], NULL, tls_thread, (void*)(intptr_t)i) == 0;
  for (int i = 0; i < 2; ++i) {
    void* r = (void*)1;
    ok &= pthread_join(t[i], &r) == 0 && r == NULL;
  }
  check("tls: __thread and errno", ok && tls_value == 7 && errno == 7);
}

/* 5. threads, mutex, condition, semaphore */
static pthread_mutex_t counter_lock = PTHREAD_MUTEX_INITIALIZER;
static long counter;
static pthread_cond_t start_cond = PTHREAD_COND_INITIALIZER;
static int started;

static void* count_thread(void* arg) {
  (void)arg;
  pthread_mutex_lock(&counter_lock);
  ++started;
  pthread_cond_broadcast(&start_cond);
  pthread_mutex_unlock(&counter_lock);
  for (int i = 0; i < 50000; ++i) {
    pthread_mutex_lock(&counter_lock);
    ++counter;
    pthread_mutex_unlock(&counter_lock);
  }
  return NULL;
}

static sem_t ping, pong;

static void* pong_thread(void* arg) {
  int rounds = (int)(intptr_t)arg;
  for (int i = 0; i < rounds; ++i) {
    sem_wait(&ping);
    sem_post(&pong);
  }
  return NULL;
}

static void check_threads(void) {
  pthread_t t[4];
  int ok = 1;
  for (int i = 0; i < 4; ++i) ok &= pthread_create(&t[i], NULL, count_thread, NULL) == 0;
  pthread_mutex_lock(&counter_lock);
  while (started < 4) pthread_cond_wait(&start_cond, &counter_lock);
  pthread_mutex_unlock(&counter_lock);
  for (int i = 0; i < 4; ++i) ok &= pthread_join(t[i], NULL) == 0;
  check("threads: 4x mutex counter", ok && counter == 200000);

  sem_init(&ping, 0, 0);
  sem_init(&pong, 0, 0);
  pthread_t p;
  ok = pthread_create(&p, NULL, pong_thread, (void*)(intptr_t)2000) == 0;
  for (int i = 0; i < 2000 && ok; ++i) {
    sem_post(&ping);
    ok = sem_wait(&pong) == 0;
  }
  ok &= pthread_join(p, NULL) == 0;
  check("threads: semaphore ping-pong", ok);

  pthread_mutex_t rec;
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&rec, &attr);
  ok = pthread_mutex_lock(&rec) == 0 && pthread_mutex_lock(&rec) == 0 && pthread_mutex_unlock(&rec) == 0 &&
       pthread_mutex_unlock(&rec) == 0 && pthread_mutex_trylock(&rec) == 0 && pthread_mutex_unlock(&rec) == 0;
  check("threads: recursive mutex", ok);

  struct timespec abs;
  clock_gettime(CLOCK_REALTIME, &abs);
  abs.tv_nsec += 20000000;
  if (abs.tv_nsec >= 1000000000) {
    abs.tv_sec += 1;
    abs.tv_nsec -= 1000000000;
  }
  pthread_mutex_lock(&counter_lock);
  int64_t before = nbt_host->monotonic_ns();
  int rc = pthread_cond_timedwait(&start_cond, &counter_lock, &abs);
  int64_t waited = nbt_host->monotonic_ns() - before;
  pthread_mutex_unlock(&counter_lock);
  check("threads: cond timeout", rc == ETIMEDOUT && waited >= 10000000 && waited < 2000000000);
}

/* A thread the module did not start, like the host's audio thread calling a
 * render function: its first lock creates its thread record. */
static volatile int foreign_ok;

static void* foreign_thread(void* arg) {
  (void)arg;
  char* p = (char*)malloc(64); /* the allocator's lock */
  foreign_ok = p && snprintf(p, 64, "%d", 42) == 2 && pthread_self() != 0;
  free(p);
  return NULL;
}

static void check_foreign_thread(void) {
  uintptr_t t;
  int ok = nbt_host->thread_create(&t, foreign_thread, NULL, 65536) == 0 && nbt_host->thread_join(t, NULL) == 0;
  check("threads: host thread uses libc", ok && foreign_ok);
}

/* 6. malloc */
static void check_malloc(void) {
  enum { SLOTS = 512 };
  static unsigned char* slot[SLOTS];
  static size_t size[SLOTS];
  unsigned seed = 12345;
  int ok = 1;
  for (int round = 0; round < 20000 && ok; ++round) {
    seed = seed * 1103515245u + 12345u;
    int i = (int)((seed >> 8) % SLOTS);
    if (slot[i]) {
      for (size_t k = 0; k < size[i]; ++k) ok &= slot[i][k] == (unsigned char)(i + k);
      free(slot[i]);
      slot[i] = NULL;
    } else {
      size[i] = 1 + (seed >> 12) % 4096;
      if ((seed & 0xff) == 7) size[i] = 300000;
      slot[i] = (unsigned char*)malloc(size[i]);
      ok &= slot[i] != NULL;
      if (slot[i])
        for (size_t k = 0; k < size[i]; ++k) slot[i][k] = (unsigned char)(i + k);
    }
  }
  for (int i = 0; i < SLOTS; ++i) free(slot[i]);
  check("malloc: 20000 alloc/free", ok);
}

/* 7. files and formatted output */
static void check_files(void) {
  const char* name = "nbt-probe.txt";
  FILE* f = fopen(name, "w");
  int ok = f != NULL;
  if (f) {
    ok &= fprintf(f, "%d %lld %.3f %s\n", 42, -1234567890123ll, 3.14159, "line") > 0;
    ok &= fclose(f) == 0;
  }
  char buf[64] = {0};
  f = fopen(name, "r");
  ok &= f != NULL;
  if (f) {
    ok &= fgets(buf, sizeof(buf), f) != NULL;
    fclose(f);
  }
  ok &= strcmp(buf, "42 -1234567890123 3.142 line\n") == 0;
  ok &= unlink(name) == 0;
  ok &= fopen(name, "r") == NULL && errno == ENOENT;
  check("files: write/read/unlink", ok);

  char text[64];
  snprintf(text, sizeof(text), "%08.3f|%-5s|%llx", -2.5, "ab", 0xfedcba987654ull);
  check("stdio: number formatting", strcmp(text, "-002.500|ab   |fedcba987654") == 0);
}

/* 8. executable memory, as a JIT uses it */
static void check_exec_memory(void) {
  static const uint32_t code[] = {0xe3a0002au /* mov r0, #42 */, 0xe12fff1eu /* bx lr */};
  uint32_t* page = (uint32_t*)nbt_host->mem_map(4096, NBT_PROT_READ | NBT_PROT_WRITE, 0);
  int ok = page != NULL;
  if (page) {
    memcpy(page, code, sizeof(code));
    ok = nbt_host->mem_protect(page, 4096, NBT_PROT_READ | NBT_PROT_EXEC) == 0 &&
         nbt_host->icache_flush(page, sizeof(code)) == 0;
    if (ok) {
      int (*fn)(void) = (int (*)(void))(void*)page;
      ok = fn() == 42;
    }
    nbt_host->mem_unmap(page, 4096);
  }
  check("jit: write, protect, run", ok);
}

/* The same through the C library, as JIT compilers port: mmap, mprotect and
 * __clear_cache on a range inside a page, writable and executable at once,
 * code rewritten in place, and executable static memory. */
static uint32_t jit_bss[2048] __attribute__((aligned(4096)));

/* Writes "mov r0, #value; bx lr" at `at`, flushes those 8 bytes, runs them. */
static int run_code(uint32_t* at, uint32_t value) {
  at[0] = 0xe3a00000u | value;
  at[1] = 0xe12fff1eu;
  __builtin___clear_cache((char*)at, (char*)(at + 2));
  int (*fn)(void) = (int (*)(void))(void*)at;
  return fn() == (int)value;
}

static void check_exec_mman(void) {
  uint32_t* page = (uint32_t*)mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  int ok = page != MAP_FAILED && page[100] == 0;
  if (ok) {
    page[16] = 0xe3a0002bu; /* mov r0, #43 */
    page[17] = 0xe12fff1eu;
    ok = mprotect(page, 8192, PROT_READ | PROT_EXEC) == 0;
    __builtin___clear_cache((char*)(page + 16), (char*)(page + 18));
    ok = ok && ((int (*)(void))(void*)(page + 16))() == 43;
    ok &= munmap(page, 8192) == 0;
  }
  check("jit: mmap, mprotect, __clear_cache", ok);

  page = (uint32_t*)mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ok = page != MAP_FAILED;
  if (ok) {
    ok = run_code(page + 8, 44) && run_code(page + 8, 45); /* rewritten in place */
    ok &= munmap(page, 4096) == 0;
  }
  check("jit: writable and executable mapping", ok);

  ok = mprotect(jit_bss, sizeof(jit_bss), PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
  ok = ok && run_code(jit_bss + 3, 46) && run_code(jit_bss + 3, 47);
  ok &= mprotect(jit_bss, sizeof(jit_bss), PROT_READ | PROT_WRITE) == 0;
  check("jit: executable static memory", ok);

  errno = 0;
  ok = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, 3, 0) == MAP_FAILED && errno == ENODEV;
  errno = 0;
  ok &= mmap((void*)0x10000000, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED &&
        errno == EINVAL;
  check("jit: no file or fixed mappings", ok);
}

/* 9. what portable code asks and is told no: thread scheduling attributes
 * (accepted, unused), CPU features (none beyond the baseline) */
static void check_portable(void) {
  pthread_attr_t attr;
  struct sched_param param;
  memset(&param, 0, sizeof(param));
  param.sched_priority = 10;
  pthread_attr_init(&attr);
  int policy = -1, inherit = -1;
  int ok = pthread_attr_setschedpolicy(&attr, SCHED_RR) == 0 && pthread_attr_setschedparam(&attr, &param) == 0 &&
           pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED) == ENOTSUP &&
           pthread_attr_getinheritsched(&attr, &inherit) == 0 && inherit == PTHREAD_INHERIT_SCHED &&
           pthread_attr_getschedpolicy(&attr, &policy) == 0 && policy == SCHED_OTHER;
  pthread_t t;
  ok = ok && pthread_create(&t, &attr, tls_thread, (void*)(intptr_t)7) == 0 && pthread_join(t, NULL) == 0;
  check("threads: scheduling attributes", ok);
  errno = 0;
  ok = (getauxval(AT_HWCAP) & HWCAP_NEON) == 0 && getauxval(AT_HWCAP2) == 0 && errno == 0;
  ok = ok && getauxval(3) == 0 && errno == ENOENT;
  check("cpu: no optional features reported", ok);
}

/* 10. unaligned access (only on request) */
static void check_unaligned(void) {
  static unsigned char bytes[8] = {0, 1, 2, 3, 4, 5, 6, 7};
  volatile uint32_t* p = (volatile uint32_t*)(void*)(bytes + 1);
  printf("unaligned: loading...\n");
  fflush(stdout);
  uint32_t v = *p;
  check("cpu: unaligned 32-bit load", v == 0x04030201u);
}

int main(int argc, char** argv) {
  if (argc > 1 && strcmp(argv[1], "--reload") == 0) {
    check("reload: constructors ran", ctor_value == 0x1234);
    return check_summary();
  }
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--crash") == 0) {
      printf("probe: crashing on purpose\n");
      fflush(stdout);
      *(volatile int*)(uintptr_t)16 = 1;
    }
    if (strcmp(argv[i], "--hang") == 0) {
      printf("probe: hanging on purpose\n");
      fflush(stdout);
      for (volatile int spin = 1; spin;) {
      }
    }
  }
  check("crt: constructors ran", ctor_value == 0x1234);
  check_abi();
  check_division();
  check_tls();
  check_threads();
  check_foreign_thread();
  check_malloc();
  check_files();
  check_exec_memory();
  check_exec_mman();
  check_portable();
  probe_cxx();
  for (int i = 1; i < argc; ++i)
    if (strcmp(argv[i], "--unaligned") == 0) check_unaligned();
  return check_summary();
}
