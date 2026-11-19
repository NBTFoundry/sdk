/* SPDX-License-Identifier: MPL-2.0 */
/* The few QNX Neutrino kernel interfaces a Mode B guest uses, declared from
 * their documented behaviour and the ARM calling convention (interoperability
 * facts; no QNX header or library is used).
 *
 * Kernel call convention on ARM: call number in r12, arguments in r0-r3 then
 * r4, r5; "svc #0". On success the kernel returns to the next instruction
 * with the result in r0; on failure it returns one instruction further with
 * the errno value in r0. The stubs in start.S return the result, or minus
 * the errno value. */
#ifndef NBT_GUEST_QNX_KERNEL_H
#define NBT_GUEST_QNX_KERNEL_H

#include <stdint.h>

/* errno values the guest handles (QNX numbering). */
enum {
  QK_EINTR = 4,
  QK_EAGAIN = 11,
  QK_ENOMEM = 12,
  QK_EINVAL = 22,
  QK_ESRCH = 3,
  QK_ETIMEDOUT = 260
};

enum { QK_CLOCK_REALTIME = 0, QK_CLOCK_MONOTONIC = 2 };

/* Blocking states for TimerTimeout's flags (1 << state). */
enum { QK_TIMEOUT_RECEIVE = 1 << 5, QK_TIMEOUT_NANOSLEEP = 1 << 12 };

/* Connection ids at or above this are "side channels": not file descriptors.
 * The first one is the process manager (memory requests). */
#define QK_SIDE_CHANNEL 0x40000000
#define QK_SYSMGR_COID QK_SIDE_CHANNEL

/* Memory manager messages. */
enum { QK_MEM_MAP = 0x40, QK_MEM_CTRL = 0x41 };
enum { QK_MEM_CTRL_UNMAP = 0, QK_MEM_CTRL_PROTECT = 1, QK_MEM_CTRL_SYNC = 2 };
enum { QK_PROT_READ = 0x100, QK_PROT_WRITE = 0x200, QK_PROT_EXEC = 0x400 };
enum {
  QK_MAP_SHARED = 0x1,
  QK_MAP_PRIVATE = 0x2,
  QK_MAP_FIXED = 0x10,
  QK_MAP_LAZY = 0x80,
  QK_MAP_ANON = 0x80000
};
#define QK_MS_INVALIDATE_ICACHE 0x1000000
#define QK_NOFD (-1)

struct qk_mem_map {
  uint16_t type; /* QK_MEM_MAP */
  uint16_t zero;
  uint32_t reserved1;
  uint64_t addr;
  uint64_t len;
  uint32_t prot;
  uint32_t flags;
  int32_t fd;
  uint32_t preload;
  uint64_t align;
  uint64_t offset;
};
struct qk_mem_map_reply {
  uint64_t real_size;
  uint64_t real_addr;
  uint64_t addr;
};
struct qk_mem_ctrl {
  uint16_t type; /* QK_MEM_CTRL */
  uint16_t subtype;
  int32_t flags;
  uint64_t addr;
  uint64_t len;
};

/* ThreadCreate attributes. A NULL stack_addr lets the kernel allocate (and
 * free) the stack; exit_func receives the thread function's return value. */
struct qk_thread_attr {
  int32_t flags; /* 0: joinable, inherit scheduling, lazy stack */
  uint32_t stack_size;
  void* stack_addr;
  void (*exit_func)(void* status);
  int32_t policy;
  int32_t sched_priority;
  int32_t sched_curpriority;
  int32_t sched_reserved[8];
  uint32_t guard_size;
  uint32_t prealloc;
  int32_t spare[2];
};

struct qk_pulse {
  uint16_t type;
  uint16_t subtype;
  int8_t code;
  uint8_t zero[3];
  int32_t value;
  int32_t scoid;
};

struct qk_iov {
  const void* base;
  uint32_t len;
};

/* The per-CPU page: word 0 points at the running thread's local storage,
 * which holds the process and thread ids. */
#define QK_CPUPAGE_ADDR (-1)
#define QK_TLS_PID_OFFSET 20
#define QK_TLS_TID_OFFSET 24

_Static_assert(sizeof(struct qk_mem_map) == 56, "mem_map layout");
_Static_assert(sizeof(struct qk_mem_map_reply) == 24, "mem_map reply layout");
_Static_assert(sizeof(struct qk_mem_ctrl) == 24, "mem_ctrl layout");
_Static_assert(sizeof(struct qk_thread_attr) == 76, "thread attr layout");
_Static_assert(sizeof(struct qk_pulse) == 16, "pulse layout");

/* Kernel calls (start.S). A negative parts count means one buffer of that
 * many bytes instead of an iov array. */
intptr_t qk_cpupage_get(int index);
int qk_msg_sendv(int coid, const void* smsg, int sparts, void* rmsg, int rparts);
int qk_msg_sendvnc(int coid, const void* smsg, int sparts, void* rmsg, int rparts);
int qk_msg_send_pulse(int coid, int priority, int code, int value);
int qk_msg_receive_pulsev(int chid, void* rmsg, int rparts, void* info);
int qk_signal_kill(uint32_t nd, int pid, int tid, int signo, int code, int value);
int qk_channel_create(unsigned flags);
int qk_channel_destroy(int chid);
int qk_connect_attach(uint32_t nd, int pid, int chid, unsigned index, int flags);
int qk_connect_detach(int coid);
int qk_thread_create(int pid, void* (*fn)(void*), void* arg, const struct qk_thread_attr* attr);
int qk_thread_destroy(int tid, int priority, void* status);
int qk_thread_detach(int tid);
int qk_thread_join(int tid, void** status);
int qk_clock_time(int id, const uint64_t* new_ns, uint64_t* old_ns);
int qk_timer_timeout(int id, int flags, const void* event, const uint64_t* ntime, uint64_t* otime);
int qk_sched_yield(void);

#endif
