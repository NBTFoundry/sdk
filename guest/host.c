/* SPDX-License-Identifier: MPL-2.0 */
/* Mode B guest: the host ABI (include/nbt/host_abi.h) implemented inside a
 * static process with QNX kernel calls, memory-manager messages and requests
 * to the runner (include/nbt/guest_proto.h). No QNX library or header is
 * used, so the guest can carry GPL code: it is a separate program that talks
 * to the runner through messages and shared memory. This part: start-up,
 * environment, time, memory and the host table (the others: guest.h).
 *
 * Nothing here may touch errno before the module runtime has the host table
 * (errno is thread-local, and thread-local storage goes through this table). */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "guest.h"
#include "qnx_kernel.h"

int32_t nbt_module_v1(const nbt_host_v1* host, int32_t argc, char** argv);
void nbt_guest_start(int argc, char** argv, char** envp) __attribute__((noreturn, used));

volatile uint32_t* guest_arena;
static char** guest_env;

int32_t guest_error(int qnx_errno) {
  switch (qnx_errno) {
    case QK_EINTR: return -NBT_EINTR;
    case QK_EAGAIN: return -NBT_EAGAIN;
    case QK_ENOMEM: return -NBT_ENOMEM;
    case QK_EINVAL: return -NBT_EINVAL;
    case QK_ETIMEDOUT: return -NBT_ETIMEDOUT;
    default: return -NBT_EIO;
  }
}

/* --- time --- */

static int64_t clock_ns(int id) {
  uint64_t ns = 0;
  qk_clock_time(id, NULL, &ns);
  return (int64_t)ns;
}
int64_t guest_monotonic_ns(void) { return clock_ns(QK_CLOCK_MONOTONIC); }
static int64_t g_realtime_ns(void) { return clock_ns(QK_CLOCK_REALTIME); }

int32_t guest_sleep_ns(int64_t ns) {
  if (ns <= 0) return 0;
  uint64_t t = (uint64_t)ns;
  int rc = qk_timer_timeout(QK_CLOCK_MONOTONIC, QK_TIMEOUT_NANOSLEEP, NULL, &t, NULL);
  if (rc >= 0 || rc == -QK_ETIMEDOUT) return 0;
  return guest_error(-rc);
}

/* --- memory: messages to the process manager --- */

static uint32_t qnx_prot(int32_t p) {
  return (p & NBT_PROT_READ ? QK_PROT_READ : 0) | (p & NBT_PROT_WRITE ? QK_PROT_WRITE : 0) |
         (p & NBT_PROT_EXEC ? QK_PROT_EXEC : 0);
}

static void* map(uintptr_t addr, uint32_t len, uint32_t prot, uint32_t flags, int fd) {
  struct qk_mem_map m;
  struct qk_mem_map_reply r;
  memset(&m, 0, sizeof(m));
  m.type = QK_MEM_MAP;
  m.addr = addr;
  m.len = len;
  m.prot = prot;
  m.flags = flags;
  m.fd = fd;
  if (qk_msg_sendvnc(QK_SYSMGR_COID, &m, -(int)sizeof(m), &r, -(int)sizeof(r)) < 0) return NULL;
  return (void*)(uintptr_t)r.addr;
}

static int32_t mem_ctrl(uint16_t subtype, void* addr, uint32_t len, int32_t flags) {
  struct qk_mem_ctrl m;
  memset(&m, 0, sizeof(m));
  m.type = QK_MEM_CTRL;
  m.subtype = subtype;
  m.flags = flags;
  m.addr = (uintptr_t)addr;
  m.len = len;
  int rc = qk_msg_sendvnc(QK_SYSMGR_COID, &m, -(int)sizeof(m), NULL, 0);
  return rc < 0 ? guest_error(-rc) : 0;
}

static void* g_mem_map(uint32_t len, int32_t prot, int32_t flags) {
  return map(0, len, qnx_prot(prot), QK_MAP_PRIVATE | QK_MAP_ANON | (flags & NBT_MAP_LAZY ? QK_MAP_LAZY : 0),
             QK_NOFD);
}
static int32_t g_mem_unmap(void* addr, uint32_t len) { return mem_ctrl(QK_MEM_CTRL_UNMAP, addr, len, 0); }
static int32_t g_mem_protect(void* addr, uint32_t len, int32_t prot) {
  return mem_ctrl(QK_MEM_CTRL_PROTECT, addr, len, (int32_t)qnx_prot(prot));
}
static int32_t g_icache_flush(void* addr, uint32_t len) {
  return mem_ctrl(QK_MEM_CTRL_SYNC, addr, len, QK_MS_INVALIDATE_ICACHE);
}

/* --- environment, exit --- */

static const char* g_get_env(const char* name) {
  if (!name || strncmp(name, "NBT_", 4) != 0 || !guest_env) return NULL;
  size_t n = strlen(name);
  for (char** e = guest_env; *e; ++e) {
    if (strncmp(*e, name, n) == 0 && (*e)[n] == '=') return *e + n + 1;
  }
  return NULL;
}

static void g_exit_process(int32_t status) __attribute__((noreturn));
static void g_exit_process(int32_t status) {
  qk_thread_destroy(-1, -1, (void*)(intptr_t)status);
  for (;;) {
  }
}

/* --- ABI self-test (same arithmetic as the runner's) --- */

static double g_abi_test_mix(int32_t a, double b, float c, int64_t d, float e) {
  return (double)a + b * 2.0 + (double)c * 3.0 + (double)d + (double)e * 5.0;
}
static double g_abi_test_callback(double (*fn)(float, double, int64_t, int32_t)) {
  return fn(1.5f, 2.25, (int64_t)3 << 33, -9);
}

static nbt_host_v1 guest_host = {
    .abi_major = NBT_HOST_ABI_MAJOR,
    .abi_minor = NBT_HOST_ABI_MINOR,
    .size = sizeof(nbt_host_v1),
    .monotonic_ns = guest_monotonic_ns,
    .realtime_ns = g_realtime_ns,
    .sleep_ns = guest_sleep_ns,
    .mem_map = g_mem_map,
    .mem_unmap = g_mem_unmap,
    .mem_protect = g_mem_protect,
    .icache_flush = g_icache_flush,
    .get_env = g_get_env,
    .exit_process = g_exit_process,
    .abi_test_mix = g_abi_test_mix,
    .abi_test_callback = g_abi_test_callback,
};

/* --- start-up --- */

/* Parses "<n>/<n>[/<n>]" (decimal, or hex with 0x). */
static int parse_numbers(const char* s, uint32_t* out, int count) {
  for (int i = 0; i < count; ++i) {
    if (!s || !*s) return 0;
    uint32_t v = 0, base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
      base = 16;
      s += 2;
    }
    const char* start = s;
    for (;; ++s) {
      uint32_t d;
      if (*s >= '0' && *s <= '9') d = (uint32_t)(*s - '0');
      else if (base == 16 && *s >= 'a' && *s <= 'f') d = (uint32_t)(*s - 'a' + 10);
      else if (base == 16 && *s >= 'A' && *s <= 'F') d = (uint32_t)(*s - 'A' + 10);
      else break;
      v = v * base + d;
    }
    if (s == start) return 0;
    out[i] = v;
    if (i + 1 < count) {
      if (*s != '/') return 0;
      ++s;
    }
  }
  return 1;
}

static void say(const char* text) { guest_write_output(2, text, (uint32_t)strlen(text)); }

static void put_hex(char* out, uint32_t v) {
  for (int i = 0; i < 8; ++i) out[i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15];
}

/* Maps the runner's arena at the runner's address, checks its magic and
 * protocol version, and answers. */
static int map_arena(void) {
  uint32_t v[3];
  const char* spec = g_get_env("NBT_GUEST_ARENA");
  if (!spec) return 1; /* optional */
  if (!parse_numbers(spec, v, 3) || v[2] < NBT_ARENA_SIZE) {
    say("guest: bad NBT_GUEST_ARENA\n");
    return 0;
  }
  /* The address is a hint first: the kernel takes it when the range is free,
   * and nothing of the guest's can be overwritten. */
  void* p = map(v[1], v[2], QK_PROT_READ | QK_PROT_WRITE, QK_MAP_SHARED, (int)v[0]);
  if (p && (uintptr_t)p != v[1]) {
    g_mem_unmap(p, v[2]);
    say("guest: arena hint not taken, mapping it fixed\n");
    p = map(v[1], v[2], QK_PROT_READ | QK_PROT_WRITE, QK_MAP_SHARED | QK_MAP_FIXED, (int)v[0]);
  }
  if ((uintptr_t)p != v[1]) {
    say("guest: arena not mapped at the runner's address\n");
    return 0;
  }
  volatile uint32_t* arena = (volatile uint32_t*)p;
  if (arena[NBT_ARENA_MAGIC_WORD] != NBT_GUEST_ARENA_MAGIC) {
    say("guest: arena does not hold the runner's magic\n");
    return 0;
  }
  if (arena[NBT_ARENA_RUNNER_VERSION_WORD] < NBT_GUEST_PROTO_VERSION) {
    say("guest: the runner is older than this application; update NBT Manager\n");
    return 0;
  }
  arena[NBT_ARENA_GUEST_VERSION_WORD] = NBT_GUEST_PROTO_VERSION;
  arena[NBT_ARENA_ANSWER_WORD] = ~NBT_GUEST_ARENA_MAGIC;
  guest_arena = arena;
  char line[] = "guest: arena at 0x........ ok\n";
  put_hex(line + 18, (uint32_t)(uintptr_t)p);
  say(line);
  return 1;
}

void nbt_guest_start(int argc, char** argv, char** envp) {
  guest_env = envp;
  const uint8_t* const* cpupage = (const uint8_t* const*)qk_cpupage_get(QK_CPUPAGE_ADDR);
  guest_sync_init(*(const int32_t*)(const void*)(cpupage[0] + QK_TLS_PID_OFFSET), cpupage);

  uint32_t ch[2];
  const char* spec = g_get_env("NBT_GUEST_CHANNEL");
  if (!spec || !parse_numbers(spec, ch, 2)) g_exit_process(120);
  const int coid = qk_connect_attach(0, (int)ch[0], (int)ch[1], QK_SIDE_CHANNEL, 0);
  if (coid < 0) g_exit_process(121);
  guest_rpc_init(coid);
  if (!map_arena()) g_exit_process(122);

  guest_sync_fill(&guest_host);
  guest_rpc_fill(&guest_host);
  guest_media_fill(&guest_host);
  g_exit_process(nbt_module_v1(&guest_host, argc, argv));
}
