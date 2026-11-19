/* SPDX-License-Identifier: MPL-2.0 */
/* POSIX threads, mutexes, conditions, keys, once and semaphores over the host
 * ABI: threads come from thread_create, blocking from wait/wake on a word.
 * Uncontended locking never leaves the module. */
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "nbt_module.h"

#define LOAD(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define STORE(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#define XCHG(p, v) __atomic_exchange_n((p), (v), __ATOMIC_ACQ_REL)
#define ADD(p, v) __atomic_add_fetch((p), (v), __ATOMIC_ACQ_REL)
#define SUB(p, v) __atomic_sub_fetch((p), (v), __ATOMIC_ACQ_REL)

static int cas(volatile uint32_t* p, uint32_t expected, uint32_t desired) {
  return __atomic_compare_exchange_n(p, &expected, desired, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

/* --- threads --- */

static struct nbt_thread main_thread;

void nbt_thread_init_main(void) {
  if (!nbt_host->thread_word_get()) nbt_host->thread_word_set(&main_thread);
}

struct nbt_thread* nbt_thread_current(void) {
  struct nbt_thread* self = (struct nbt_thread*)nbt_host->thread_word_get();
  if (!self) {
    /* A thread the module did not start (a host callback thread, such as the
     * audio thread). Not from malloc: its lock asks for this record. */
    self = (struct nbt_thread*)nbt_host->mem_map(sizeof(*self), NBT_PROT_READ | NBT_PROT_WRITE, 0);
    if (!self) self = &main_thread;
    self->detached = 1;
    nbt_host->thread_word_set(self);
  }
  return self;
}

static void (*key_destructors[NBT_KEYS_MAX])(void*);
static volatile uint32_t key_used[NBT_KEYS_MAX];

/* C++ thread_local destructors (libc++abi leaves this to the platform). */
struct nbt_tls_dtor {
  void (*fn)(void*);
  void* obj;
  struct nbt_tls_dtor* next;
};

int __cxa_thread_atexit(void (*fn)(void*), void* obj, void* dso) {
  (void)dso;
  struct nbt_thread* self = nbt_thread_current();
  struct nbt_tls_dtor* d = (struct nbt_tls_dtor*)malloc(sizeof(*d));
  if (!d) return -1;
  d->fn = fn;
  d->obj = obj;
  d->next = self->tls_dtors;
  self->tls_dtors = d;
  return 0;
}

void nbt_thread_run_destructors(struct nbt_thread* self) {
  while (self->tls_dtors) { /* a destructor may register more */
    struct nbt_tls_dtor* d = self->tls_dtors;
    self->tls_dtors = d->next;
    d->fn(d->obj);
    free(d);
  }
  for (int round = 0; round < 4; ++round) {
    int again = 0;
    for (int k = 0; k < NBT_KEYS_MAX; ++k) {
      void* value = self->specific[k];
      if (value && LOAD(&key_used[k]) && key_destructors[k]) {
        self->specific[k] = NULL;
        key_destructors[k](value);
        again = 1;
      }
    }
    if (!again) break;
  }
}

static void* thread_start(void* p) {
  struct nbt_thread* self = (struct nbt_thread*)p;
  nbt_host->thread_word_set(self);
  self->result = self->fn(self->arg);
  nbt_thread_run_destructors(self);
  return self->result;
}

int pthread_attr_init(pthread_attr_t* attr) {
  attr->stack_size = NBT_APP_STACK_SIZE;
  attr->detach_state = PTHREAD_CREATE_JOINABLE;
  return 0;
}
int pthread_attr_destroy(pthread_attr_t* attr) {
  (void)attr;
  return 0;
}
int pthread_attr_setstacksize(pthread_attr_t* attr, size_t size) {
  if (size < PTHREAD_STACK_MIN) return EINVAL;
  attr->stack_size = (uint32_t)size;
  return 0;
}
int pthread_attr_getstacksize(const pthread_attr_t* attr, size_t* size) {
  *size = attr->stack_size;
  return 0;
}
int pthread_attr_setdetachstate(pthread_attr_t* attr, int state) {
  attr->detach_state = state;
  return 0;
}
int pthread_attr_getdetachstate(const pthread_attr_t* attr, int* state) {
  *state = attr->detach_state;
  return 0;
}

int pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*fn)(void*), void* arg) {
  struct nbt_thread* t = (struct nbt_thread*)calloc(1, sizeof(*t));
  if (!t) return EAGAIN;
  t->fn = fn;
  t->arg = arg;
  uint32_t stack = attr ? attr->stack_size : NBT_APP_STACK_SIZE;
  int32_t rc = nbt_host->thread_create(&t->handle, thread_start, t, stack);
  if (rc < 0) {
    free(t);
    return nbt_errno_from_host(-rc);
  }
  if (attr && attr->detach_state == PTHREAD_CREATE_DETACHED) pthread_detach((pthread_t)t);
  *thread = (pthread_t)t;
  return 0;
}

int pthread_join(pthread_t thread, void** result) {
  struct nbt_thread* t = (struct nbt_thread*)thread;
  if (!t || t->detached) return EINVAL;
  void* r = NULL;
  int32_t rc = nbt_host->thread_join(t->handle, &r);
  if (rc < 0) return nbt_errno_from_host(-rc);
  if (result) *result = t->result;
  free(t);
  return 0;
}

int pthread_detach(pthread_t thread) {
  struct nbt_thread* t = (struct nbt_thread*)thread;
  if (!t || t->detached) return EINVAL;
  t->detached = 1;
  /* The record of a detached thread is not freed: it is small and the thread
   * may still be running. */
  int32_t rc = nbt_host->thread_detach(t->handle);
  return rc < 0 ? nbt_errno_from_host(-rc) : 0;
}

pthread_t pthread_self(void) { return (pthread_t)nbt_thread_current(); }

int pthread_equal(pthread_t a, pthread_t b) { return a == b; }

/* --- mutexes --- */

int pthread_mutexattr_init(pthread_mutexattr_t* attr) {
  attr->type = PTHREAD_MUTEX_DEFAULT;
  return 0;
}
int pthread_mutexattr_destroy(pthread_mutexattr_t* attr) {
  (void)attr;
  return 0;
}
int pthread_mutexattr_settype(pthread_mutexattr_t* attr, int type) {
  if (type < PTHREAD_MUTEX_NORMAL || type > PTHREAD_MUTEX_ERRORCHECK) return EINVAL;
  attr->type = (uint32_t)type;
  return 0;
}
int pthread_mutexattr_gettype(const pthread_mutexattr_t* attr, int* type) {
  *type = (int)attr->type;
  return 0;
}

int pthread_mutex_init(pthread_mutex_t* m, const pthread_mutexattr_t* attr) {
  memset((void*)m, 0, sizeof(*m));
  m->type = attr ? attr->type : PTHREAD_MUTEX_DEFAULT;
  return 0;
}
int pthread_mutex_destroy(pthread_mutex_t* m) { return LOAD(&m->lock) ? EBUSY : 0; }

static void lock_word(volatile uint32_t* w) {
  if (cas(w, 0, 1)) return;
  /* Contended: mark waiters (2) and sleep until the word is released. */
  while (XCHG(w, 2) != 0) nbt_host->wait(w, 2, -1);
}

static void unlock_word(volatile uint32_t* w) {
  if (XCHG(w, 0) == 2) nbt_host->wake(w, 1);
}

int pthread_mutex_lock(pthread_mutex_t* m) {
  uintptr_t self = (uintptr_t)nbt_thread_current();
  if (m->type != PTHREAD_MUTEX_NORMAL && m->owner == self) {
    if (m->type == PTHREAD_MUTEX_ERRORCHECK) return EDEADLK;
    ++m->count;
    return 0;
  }
  lock_word(&m->lock);
  m->owner = self;
  m->count = 1;
  return 0;
}

int pthread_mutex_trylock(pthread_mutex_t* m) {
  uintptr_t self = (uintptr_t)nbt_thread_current();
  if (m->type == PTHREAD_MUTEX_RECURSIVE && m->owner == self) {
    ++m->count;
    return 0;
  }
  if (!cas(&m->lock, 0, 1)) return EBUSY;
  m->owner = self;
  m->count = 1;
  return 0;
}

int pthread_mutex_unlock(pthread_mutex_t* m) {
  if (m->type != PTHREAD_MUTEX_NORMAL) {
    if (m->owner != (uintptr_t)nbt_thread_current()) return EPERM;
    if (--m->count > 0) return 0;
  }
  m->owner = 0;
  unlock_word(&m->lock);
  return 0;
}

/* --- conditions --- */

int pthread_condattr_init(pthread_condattr_t* attr) {
  attr->clock = CLOCK_REALTIME;
  return 0;
}
int pthread_condattr_destroy(pthread_condattr_t* attr) {
  (void)attr;
  return 0;
}
int pthread_condattr_setclock(pthread_condattr_t* attr, clockid_t clock) {
  attr->clock = (int32_t)clock;
  return 0;
}
int pthread_cond_init(pthread_cond_t* c, const pthread_condattr_t* attr) {
  c->seq = 0;
  c->reserved = attr ? (uint32_t)attr->clock : (uint32_t)CLOCK_REALTIME;
  return 0;
}
int pthread_cond_destroy(pthread_cond_t* c) {
  (void)c;
  return 0;
}

static int64_t timeout_from_abs(const struct timespec* abstime, int monotonic) {
  int64_t now = monotonic ? nbt_host->monotonic_ns() : nbt_host->realtime_ns();
  int64_t until = (int64_t)abstime->tv_sec * 1000000000 + abstime->tv_nsec;
  return until > now ? until - now : 0;
}

static int cond_wait(pthread_cond_t* c, pthread_mutex_t* m, int64_t timeout) {
  uint32_t seq = LOAD(&c->seq);
  uint32_t count = m->count;
  uintptr_t owner = m->owner;
  m->owner = 0;
  unlock_word(&m->lock);
  int32_t rc = nbt_host->wait(&c->seq, seq, timeout);
  lock_word(&m->lock);
  m->owner = owner;
  m->count = count;
  return rc == -NBT_ETIMEDOUT ? ETIMEDOUT : 0;
}

int pthread_cond_wait(pthread_cond_t* c, pthread_mutex_t* m) { return cond_wait(c, m, -1); }

int pthread_cond_timedwait(pthread_cond_t* c, pthread_mutex_t* m, const struct timespec* abstime) {
  int64_t t = timeout_from_abs(abstime, c->reserved == (uint32_t)CLOCK_MONOTONIC);
  if (t == 0) return ETIMEDOUT;
  return cond_wait(c, m, t);
}

int pthread_cond_signal(pthread_cond_t* c) {
  ADD(&c->seq, 1);
  nbt_host->wake(&c->seq, 1);
  return 0;
}

int pthread_cond_broadcast(pthread_cond_t* c) {
  ADD(&c->seq, 1);
  nbt_host->wake(&c->seq, -1);
  return 0;
}

/* --- keys --- */

int pthread_key_create(pthread_key_t* key, void (*destructor)(void*)) {
  for (uint32_t k = 0; k < NBT_KEYS_MAX; ++k) {
    if (cas(&key_used[k], 0, 1)) {
      key_destructors[k] = destructor;
      *key = k;
      return 0;
    }
  }
  return EAGAIN;
}

int pthread_key_delete(pthread_key_t key) {
  if (key >= NBT_KEYS_MAX) return EINVAL;
  key_destructors[key] = NULL;
  STORE(&key_used[key], 0);
  return 0;
}

void* pthread_getspecific(pthread_key_t key) {
  return key < NBT_KEYS_MAX ? nbt_thread_current()->specific[key] : NULL;
}

int pthread_setspecific(pthread_key_t key, const void* value) {
  if (key >= NBT_KEYS_MAX) return EINVAL;
  nbt_thread_current()->specific[key] = (void*)value;
  return 0;
}

/* --- once --- */

int pthread_once(pthread_once_t* once, void (*fn)(void)) {
  if (LOAD(&once->state) == 2) return 0;
  if (cas(&once->state, 0, 1)) {
    fn();
    STORE(&once->state, 2);
    nbt_host->wake(&once->state, -1);
    return 0;
  }
  while (LOAD(&once->state) != 2) nbt_host->wait(&once->state, 1, -1);
  return 0;
}

/* --- semaphores --- */

int sem_init(sem_t* s, int pshared, unsigned value) {
  if (pshared) {
    errno = ENOSYS;
    return -1;
  }
  s->value = value;
  s->waiters = 0;
  return 0;
}

int sem_destroy(sem_t* s) {
  (void)s;
  return 0;
}

static int sem_take(sem_t* s) {
  uint32_t v = LOAD(&s->value);
  while (v > 0) {
    if (cas(&s->value, v, v - 1)) return 1;
    v = LOAD(&s->value);
  }
  return 0;
}

static int sem_wait_for(sem_t* s, int64_t timeout) {
  int64_t deadline = timeout < 0 ? -1 : nbt_host->monotonic_ns() + timeout;
  for (;;) {
    if (sem_take(s)) return 0;
    int64_t left = -1;
    if (deadline >= 0) {
      left = deadline - nbt_host->monotonic_ns();
      if (left <= 0) {
        errno = ETIMEDOUT;
        return -1;
      }
    }
    ADD(&s->waiters, 1);
    nbt_host->wait(&s->value, 0, left);
    SUB(&s->waiters, 1);
  }
}

int sem_wait(sem_t* s) { return sem_wait_for(s, -1); }

int sem_trywait(sem_t* s) {
  if (sem_take(s)) return 0;
  errno = EAGAIN;
  return -1;
}

int sem_timedwait(sem_t* s, const struct timespec* abstime) {
  return sem_wait_for(s, timeout_from_abs(abstime, 0));
}

int sem_post(sem_t* s) {
  ADD(&s->value, 1);
  if (LOAD(&s->waiters)) nbt_host->wake(&s->value, 1);
  return 0;
}

int sem_getvalue(sem_t* s, int* value) {
  *value = (int)LOAD(&s->value);
  return 0;
}

int sched_yield(void) {
  nbt_host->thread_yield();
  return 0;
}

/* --- picolibc's internal locks --- */

struct __lock {
  pthread_mutex_t m;
};

struct __lock __lock___libc_recursive_mutex = {PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP};

void __retarget_lock_init(struct __lock** lock) {
  *lock = (struct __lock*)calloc(1, sizeof(struct __lock));
}
void __retarget_lock_init_recursive(struct __lock** lock) {
  *lock = (struct __lock*)calloc(1, sizeof(struct __lock));
  if (*lock) (*lock)->m.type = PTHREAD_MUTEX_RECURSIVE;
}
void __retarget_lock_close(struct __lock* lock) { free(lock); }
void __retarget_lock_close_recursive(struct __lock* lock) { free(lock); }
void __retarget_lock_acquire(struct __lock* lock) {
  if (lock) pthread_mutex_lock(&lock->m);
}
void __retarget_lock_acquire_recursive(struct __lock* lock) {
  if (lock) pthread_mutex_lock(&lock->m);
}
void __retarget_lock_release(struct __lock* lock) {
  if (lock) pthread_mutex_unlock(&lock->m);
}
void __retarget_lock_release_recursive(struct __lock* lock) {
  if (lock) pthread_mutex_unlock(&lock->m);
}
