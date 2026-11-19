/* SPDX-License-Identifier: MPL-2.0 */
/* Mode B guest: threads (kernel ThreadCreate) and address-keyed waiting
 * (parking on per-thread channels), with no QNX library (guest.h). */
#include <stddef.h>
#include <string.h>

#include "guest.h"
#include "qnx_kernel.h"

static const uint8_t* const* cpupage; /* word 0: the running thread's local storage */
static int self_pid;

/* --- per-thread state, indexed by the kernel's thread id --- */

#define MAX_THREADS 256

struct guest_thread {
  void* word;     /* thread_word_get/set */
  int chid, coid; /* the thread's own channel and a connection to it, for parking */
  volatile uint32_t woken;
  volatile uint32_t* wait_addr;
  struct guest_thread* next; /* in its wait bucket */
};
static struct guest_thread threads[MAX_THREADS + 1];

static struct guest_thread* self(void) {
  const int tid = *(const int32_t*)(const void*)(cpupage[0] + QK_TLS_TID_OFFSET);
  return &threads[tid > 0 && tid <= MAX_THREADS ? tid : 0];
}

void guest_sync_init(int pid, const uint8_t* const* page) {
  self_pid = pid;
  cpupage = page;
}

/* --- threads --- */

static void thread_exit(void* status) {
  struct guest_thread* t = self();
  if (t->coid > 0) {
    qk_connect_detach(t->coid);
    qk_channel_destroy(t->chid);
  }
  t->coid = t->chid = 0;
  t->word = NULL;
  qk_thread_destroy(0, -1, status);
  for (;;) {
  }
}

int32_t guest_thread_create(uintptr_t* handle, void* (*fn)(void*), void* arg, uint32_t stack_size) {
  struct qk_thread_attr attr;
  memset(&attr, 0, sizeof(attr));
  if (stack_size < 65536) stack_size = 65536;
  attr.stack_size = (stack_size + 4095u) & ~4095u;
  attr.exit_func = thread_exit;
  attr.guard_size = 4096;
  int tid = qk_thread_create(0, fn, arg, &attr);
  if (tid < 0) return guest_error(-tid);
  *handle = (uintptr_t)tid;
  return 0;
}

int32_t guest_thread_join(uintptr_t handle, void** result) {
  void* status = NULL;
  int rc = qk_thread_join((int)handle, &status);
  if (rc < 0) return guest_error(-rc);
  if (result) *result = status;
  return 0;
}

static int32_t g_thread_detach(uintptr_t handle) {
  int rc = qk_thread_detach((int)handle);
  return rc < 0 ? guest_error(-rc) : 0;
}

static void g_thread_yield(void) { qk_sched_yield(); }
static void* g_thread_word_get(void) { return self()->word; }
static void g_thread_word_set(void* value) { self()->word = value; }

/* --- address-keyed waiting ---
 * A waiter queues itself in its address's bucket after checking the word
 * under the bucket lock, then blocks receiving a pulse on its own channel. A
 * waker dequeues waiters under the lock, marks them woken and pulses them.
 * Stale pulses (a waiter that timed out as it was woken) only cause a
 * spurious loop on the next wait. */

#define WAIT_BUCKETS 64
static struct bucket {
  volatile uint32_t lock;
  struct guest_thread* head;
} buckets[WAIT_BUCKETS];

static struct bucket* bucket_of(volatile uint32_t* addr) {
  uintptr_t a = (uintptr_t)addr >> 2;
  return &buckets[(a ^ (a >> 6) ^ (a >> 12)) % WAIT_BUCKETS];
}

static void bucket_lock(struct bucket* b) {
  while (XCHG(&b->lock, 1)) {
    while (LOAD(&b->lock)) qk_sched_yield();
  }
}
static void bucket_unlock(struct bucket* b) { STORE(&b->lock, 0); }

static int ensure_channel(struct guest_thread* t) {
  if (t->coid > 0) return 0;
  int chid = qk_channel_create(0);
  if (chid < 0) return chid;
  int coid = qk_connect_attach(0, self_pid, chid, QK_SIDE_CHANNEL, 0);
  if (coid < 0) {
    qk_channel_destroy(chid);
    return coid;
  }
  t->chid = chid;
  t->coid = coid;
  return 0;
}

/* Removes t from its bucket; returns 0 when a waker took it first. */
static int dequeue(struct bucket* b, struct guest_thread* t) {
  for (struct guest_thread** p = &b->head; *p; p = &(*p)->next) {
    if (*p == t) {
      *p = t->next;
      t->next = NULL;
      return 1;
    }
  }
  return 0;
}

static int32_t g_wait(volatile uint32_t* addr, uint32_t expected, int64_t timeout_ns) {
  struct guest_thread* t = self();
  if (ensure_channel(t) < 0) {
    qk_sched_yield(); /* no channel: behave as a spurious wake */
    return 0;
  }
  struct bucket* b = bucket_of(addr);
  bucket_lock(b);
  if (LOAD(addr) != expected) {
    bucket_unlock(b);
    return -NBT_EAGAIN;
  }
  t->wait_addr = addr;
  STORE(&t->woken, 0);
  t->next = b->head;
  b->head = t;
  bucket_unlock(b);

  int64_t deadline = timeout_ns >= 0 ? guest_monotonic_ns() + timeout_ns : -1;
  int timed_out = 0;
  for (;;) {
    if (deadline >= 0) {
      int64_t left = deadline - guest_monotonic_ns();
      if (left <= 0) {
        timed_out = 1;
        break;
      }
      uint64_t ns = (uint64_t)left;
      qk_timer_timeout(QK_CLOCK_MONOTONIC, QK_TIMEOUT_RECEIVE, NULL, &ns, NULL);
    }
    struct qk_pulse pulse;
    int rc = qk_msg_receive_pulsev(t->chid, &pulse, -(int)sizeof(pulse), NULL);
    if (LOAD(&t->woken)) return 0;
    if (rc == -QK_ETIMEDOUT) {
      timed_out = 1;
      break;
    }
    if (rc < 0 && rc != -QK_EINTR) break; /* unexpected: report a spurious wake */
  }
  bucket_lock(b);
  int still_queued = dequeue(b, t);
  bucket_unlock(b);
  return still_queued && timed_out ? -NBT_ETIMEDOUT : 0;
}

static int32_t g_wake(volatile uint32_t* addr, int32_t count) {
  struct bucket* b = bucket_of(addr);
  int32_t woken = 0;
  for (;;) {
    int coids[32];
    int n = 0;
    bucket_lock(b);
    for (struct guest_thread** p = &b->head; *p && n < 32 && (count < 0 || woken < count);) {
      struct guest_thread* w = *p;
      if (w->wait_addr == addr) {
        *p = w->next;
        w->next = NULL;
        coids[n++] = w->coid;
        STORE(&w->woken, 1);
        ++woken;
      } else {
        p = &w->next;
      }
    }
    bucket_unlock(b);
    for (int i = 0; i < n; ++i) qk_msg_send_pulse(coids[i], -1, 1, 0);
    if (n < 32 || (count >= 0 && woken >= count)) break;
  }
  return woken;
}

void guest_sync_fill(nbt_host_v1* h) {
  h->thread_create = guest_thread_create;
  h->thread_join = guest_thread_join;
  h->thread_detach = g_thread_detach;
  h->thread_yield = g_thread_yield;
  h->thread_word_get = g_thread_word_get;
  h->thread_word_set = g_thread_word_set;
  h->wait = g_wait;
  h->wake = g_wake;
}
