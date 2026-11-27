/* SPDX-License-Identifier: MPL-2.0 */
/* Mode B: runs a guest process (guest/, no QNX code in it) and serves its
 * requests (include/nbt/guest_proto.h) with the runner's host table. The
 * guest shares a memory arena with the runner at the same address. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/neutrino.h>
#include <sys/wait.h>
#include <unistd.h>

#include "gl_runner.h"
#include "host_sys.h"
#include "run.h"
#include "nbt/guest_proto.h"

static volatile uint8_t* g_arena;
static int g_gl_loaded;

/* What the guest keeps in its ring: two of the output's fragments, and one
 * more each time the ring ran empty RING_SHORT_TO_GROW times while it played
 * (the guest's audio thread came late), RING_FRAGS_MAX at most. A guest
 * without NBT_GUEST_CAP_AUDIO_TARGET cannot follow: it is told a fragment of
 * RING_OLD_GUEST_FRAG frames at least and keeps twice that. */
enum { RING_FRAGS = 2, RING_FRAGS_MAX = 4, RING_SHORT_TO_GROW = 3, RING_OLD_GUEST_FRAG = 512 };
static uint32_t g_ring_frag, g_ring_short;
static int g_ring_follows, g_ring_played;

/* The runner's playback thread: drains the guest's ring (silence when it is
 * behind). Indices are the guest's to write: bounded here. */
static void ring_render(void* context, int16_t* out, int32_t frames) {
  volatile uint32_t* ctl = (volatile uint32_t*)context;
  const int16_t* ring = (const int16_t*)(const void*)((const uint8_t*)context + NBT_AUDIO_OFF);
  uint32_t rd = ctl[NBT_AUDIO_READ_WORD];
  uint32_t avail = __atomic_load_n(&ctl[NBT_AUDIO_WRITE_WORD], __ATOMIC_ACQUIRE) - rd;
  if (avail > NBT_AUDIO_FRAMES) avail = 0; /* nonsense from the guest: play silence */
  if (avail >= (uint32_t)frames) {
    g_ring_played = 1;
  } else if (g_ring_played && g_ring_follows && ++g_ring_short >= RING_SHORT_TO_GROW) {
    const uint32_t target = ctl[NBT_AUDIO_TARGET_WORD];
    g_ring_short = 0;
    if (g_ring_frag && target < RING_FRAGS_MAX * g_ring_frag)
      __atomic_store_n(&ctl[NBT_AUDIO_TARGET_WORD], target + g_ring_frag, __ATOMIC_RELEASE);
  }
  for (int32_t i = 0; i < frames; ++i) {
    if ((uint32_t)i < avail) {
      out[2 * i] = ring[2 * (rd % NBT_AUDIO_FRAMES)];
      out[2 * i + 1] = ring[2 * (rd % NBT_AUDIO_FRAMES) + 1];
      ++rd;
    } else {
      out[2 * i] = out[2 * i + 1] = 0;
    }
  }
  __atomic_store_n(&ctl[NBT_AUDIO_READ_WORD], rd, __ATOMIC_RELEASE);
}

/* How the guest's frames go, for its log: the first frame presented, then a
 * count every 3600 (a minute at 60 Hz) and at the end, and the first GL error
 * the runner's context reports after a frame's commands. */
static uint32_t g_frames;
static GLenum g_first_gl_error = GL_NO_ERROR;

static void frame_presented(void) {
  const GLenum e = nbtgl_fn.GetError ? nbtgl_fn.GetError() : GL_NO_ERROR;
  ++g_frames;
  if (e != GL_NO_ERROR && g_first_gl_error == GL_NO_ERROR) {
    g_first_gl_error = e;
    printf("runner: GL error 0x%x in frame %u\n", (unsigned)e, (unsigned)g_frames);
  }
  if (g_frames == 1 || g_frames % 3600 == 0) printf("runner: %u frames presented\n", (unsigned)g_frames);
}

static uint8_t guest_msg[sizeof(nbt_guest_req) + NBT_GUEST_MAX_PAYLOAD + NBT_GUEST_MAX_TEXT];
static uint8_t guest_out[NBT_GUEST_MAX_PAYLOAD + 512];

static int c_string(const char* p, uint32_t n) { return n > 0 && p[n - 1] == '\0' && strlen(p) == n - 1; }

/* Output for the reply: `n` bytes of `p` (at most sizeof(guest_out)). */
static uint32_t reply_with(const void* p, uint32_t n) {
  memcpy(guest_out, p, n);
  return n;
}

/* A host call with no result of its own: 0 once made, -NBT_ENOSYS without it. */
#define CALLED(member, call) (NBT_HOST_HAS(H, member) ? ((call), 0) : -NBT_ENOSYS)

/* One request from the guest. Everything in it is untrusted. */
static void serve(const nbt_host_v1* H, int rcvid, int len) {
  const nbt_guest_req* req = (const nbt_guest_req*)(const void*)guest_msg;
  if (len < (int)sizeof(*req) || req->type != NBT_GUEST_MSG_TYPE || req->len1 > NBT_GUEST_MAX_PAYLOAD ||
      req->len2 > NBT_GUEST_MAX_TEXT || sizeof(*req) + req->len1 + req->len2 > (uint32_t)len) {
    MsgError(rcvid, EINVAL);
    return;
  }
  const char* p1 = (const char*)guest_msg + sizeof(*req);
  const char* p2 = p1 + req->len1;
  const char* text = c_string(p1, req->len1) ? p1 : NULL; /* the payload, when it is a C string */
  nbt_guest_rep rep = {-NBT_ENOSYS};
  uint32_t out_len = 0;
  uint32_t room = req->b < 0 ? 0 : (uint32_t)req->b;
  int32_t r;
  switch (req->op) {
    case NBT_GUEST_OUTPUT: rep.result = H->write_output(req->a, p1, req->len1); break;
    case NBT_GUEST_OPEN: rep.result = text ? H->file_open(text, req->a, req->b) : -NBT_EINVAL; break;
    case NBT_GUEST_READ:
      r = H->file_read(req->a, guest_out, room < NBT_GUEST_MAX_PAYLOAD ? room : NBT_GUEST_MAX_PAYLOAD);
      rep.result = r;
      out_len = r > 0 ? (uint32_t)r : 0;
      break;
    case NBT_GUEST_WRITE: rep.result = H->file_write(req->a, p1, req->len1); break;
    case NBT_GUEST_SEEK: rep.result = H->file_seek(req->a, req->d, req->b); break;
    case NBT_GUEST_CLOSE: rep.result = H->file_close(req->a); break;
    case NBT_GUEST_SYNC: rep.result = H->file_sync(req->a); break;
    case NBT_GUEST_STAT:
    case NBT_GUEST_FSTAT: {
      nbt_stat st;
      if (req->op == NBT_GUEST_STAT) rep.result = text ? H->file_stat(text, &st) : -NBT_EINVAL;
      else rep.result = H->file_fstat(req->a, &st);
      if (rep.result == 0) out_len = reply_with(&st, sizeof(st));
      break;
    }
    case NBT_GUEST_UNLINK: rep.result = text ? H->file_unlink(text) : -NBT_EINVAL; break;
    case NBT_GUEST_MKDIR: rep.result = text ? H->file_mkdir(text, req->b) : -NBT_EINVAL; break;
    case NBT_GUEST_RENAME:
      rep.result = text && c_string(p2, req->len2) ? H->file_rename(text, p2) : -NBT_EINVAL;
      break;
    case NBT_GUEST_RANDOM:
      if (room > 256) room = 256;
      rep.result = H->random_bytes(guest_out, room);
      out_len = rep.result == (int64_t)room ? room : 0;
      break;
    case NBT_GUEST_APP_INFO:
      if (NBT_HOST_HAS(H, app_info)) {
        nbt_app_info info;
        memset(&info, 0, sizeof(info));
        rep.result = H->app_info(&info);
        if (rep.result == 0) out_len = reply_with(&info, sizeof(info));
      }
      break;
    case NBT_GUEST_APP_SETTINGS:
      if (NBT_HOST_HAS(H, app_settings)) {
        uint32_t generation = 0;
        if (room > sizeof(guest_out) - 4) room = sizeof(guest_out) - 4;
        rep.result = H->app_settings((char*)guest_out + 4, room, &generation);
        memcpy(guest_out, &generation, 4);
        out_len = 4 + (rep.result > 0 ? (uint32_t)rep.result : 0);
      }
      break;
    case NBT_GUEST_APP_READY:
      if (NBT_HOST_HAS(H, app_ready)) rep.result = H->app_ready();
      break;
    case NBT_GUEST_APP_TICK:
      if (NBT_HOST_HAS(H, app_tick)) rep.result = H->app_tick(text);
      break;
    case NBT_GUEST_APP_POLL:
      if (NBT_HOST_HAS(H, app_poll)) rep.result = H->app_poll();
      break;
    case NBT_GUEST_APP_STATUS: rep.result = text ? CALLED(app_status, H->app_status(text)) : -NBT_EINVAL; break;
    case NBT_GUEST_APP_STOP: rep.result = CALLED(app_request_stop, H->app_request_stop()); break;
    case NBT_GUEST_BRIDGE_RECV:
      if (NBT_HOST_HAS(H, bridge_recv)) {
        r = H->bridge_recv(req->a, guest_out, room < 4096 ? room : 4096);
        rep.result = r;
        out_len = r > 0 ? (uint32_t)r : 0;
      }
      break;
    case NBT_GUEST_DIR_OPEN:
      if (NBT_HOST_HAS(H, dir_open)) rep.result = text ? H->dir_open(text) : -NBT_EINVAL;
      break;
    case NBT_GUEST_DIR_READ:
      if (NBT_HOST_HAS(H, dir_read)) {
        uint32_t mode = 0;
        r = H->dir_read(req->a, (char*)guest_out + 4, 256, &mode);
        rep.result = r;
        if (r > 0) {
          memcpy(guest_out, &mode, 4);
          out_len = 4 + (uint32_t)strlen((char*)guest_out + 4) + 1;
        }
      }
      break;
    case NBT_GUEST_DIR_CLOSE:
      if (NBT_HOST_HAS(H, dir_close)) rep.result = H->dir_close(req->a);
      break;
    case NBT_GUEST_VIDEO_OPEN:
      if (NBT_HOST_HAS(H, video_open) && NBT_HOST_HAS(H, gl_proc)) {
        int32_t wh[2] = {0, 0};
        rep.result = H->video_open(req->a, &wh[0], &wh[1]);
        if (rep.result == 0 && !g_gl_loaded) {
          const int missing = nbtgl_load(H->gl_proc);
          if (missing) printf("runner: %d OpenGL ES entry points missing\n", missing);
          g_gl_loaded = 1;
        }
        out_len = reply_with(wh, sizeof(wh));
      }
      break;
    case NBT_GUEST_GL_RUN:
    case NBT_GUEST_VIDEO_SWAP:
      if (!g_gl_loaded) break;
      rep.result = nbtgl_run(g_arena, (uint32_t)req->a);
      if (rep.result < 0) printf("runner: malformed GL commands from the guest\n");
      if (req->op == NBT_GUEST_VIDEO_SWAP && rep.result == 0) {
        H->video_swap();
        frame_presented();
      }
      break;
    case NBT_GUEST_VIDEO_CLOSE: rep.result = CALLED(video_close, H->video_close()); break;
    case NBT_GUEST_AUDIO_OPEN:
      if (NBT_HOST_HAS(H, audio_open)) {
        int32_t rf[2] = {0, 0};
        volatile uint32_t* ctl = (volatile uint32_t*)g_arena;
        ctl[NBT_AUDIO_READ_WORD] = 0;
        ctl[NBT_AUDIO_TARGET_WORD] = 0;
        g_ring_follows = (ctl[NBT_ARENA_GUEST_CAPS_WORD] & NBT_GUEST_CAP_AUDIO_TARGET) != 0;
        g_ring_played = 0;
        g_ring_short = 0;
        g_ring_frag = 0;
        rep.result = H->audio_open(req->b, ring_render, (void*)g_arena, req->a, &rf[0], &rf[1]);
        if (rep.result >= 0 && rf[1] > 0) {
          g_ring_frag = (uint32_t)rf[1];
          if (g_ring_follows) {
            uint32_t target = RING_FRAGS * g_ring_frag;
            if (target > NBT_AUDIO_TARGET_MAX) target = NBT_AUDIO_TARGET_MAX;
            __atomic_store_n(&ctl[NBT_AUDIO_TARGET_WORD], target, __ATOMIC_RELEASE);
          } else if (rf[1] < RING_OLD_GUEST_FRAG) {
            rf[1] = RING_OLD_GUEST_FRAG;
          }
        }
        out_len = reply_with(rf, sizeof(rf));
      }
      break;
    case NBT_GUEST_AUDIO_CLOSE: rep.result = CALLED(audio_close, H->audio_close()); break;
    case NBT_GUEST_AUDIO_ROUTE: rep.result = CALLED(audio_set_route, H->audio_set_route(req->a)); break;
    default: break;
  }
  iov_t iov[2];
  SETIOV(&iov[0], &rep, sizeof(rep));
  SETIOV(&iov[1], guest_out, out_len);
  MsgReplyv(rcvid, EOK, iov, out_len ? 2 : 1);
}

int runner_run_guest(const char* path, char* const argv[], const nbt_host_v1* H, int timeout_s) {
  int chid = ChannelCreate(_NTO_CHF_DISCONNECT);
  if (chid < 0) {
    printf("runner: ChannelCreate failed: %s\n", strerror(errno));
    return RUNNER_CANNOT_START;
  }
  char name[64];
  snprintf(name, sizeof(name), "/nbt-arena-%d", (int)getpid());
  int shm = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
  if (shm < 0) {
    printf("runner: shm_open failed: %s\n", strerror(errno));
    ChannelDestroy(chid);
    return RUNNER_CANNOT_START;
  }
  shm_unlink(name);
  volatile uint32_t* arena = MAP_FAILED;
  if (ftruncate(shm, NBT_ARENA_SIZE) == 0)
    arena = (volatile uint32_t*)mmap(NULL, NBT_ARENA_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm, 0);
  if (arena == MAP_FAILED) {
    printf("runner: arena failed: %s\n", strerror(errno));
    close(shm);
    ChannelDestroy(chid);
    return RUNNER_CANNOT_START;
  }
  arena[NBT_ARENA_MAGIC_WORD] = NBT_GUEST_ARENA_MAGIC;
  arena[NBT_ARENA_RUNNER_VERSION_WORD] = NBT_GUEST_PROTO_VERSION;
  g_arena = (volatile uint8_t*)arena;

  /* The guest's environment: the channel, the arena and the NBT_* settings. */
  char channel[64], arena_spec[64];
  snprintf(channel, sizeof(channel), "NBT_GUEST_CHANNEL=%d/%d", (int)getpid(), chid);
  snprintf(arena_spec, sizeof(arena_spec), "NBT_GUEST_ARENA=4/0x%lx/%u", (unsigned long)(uintptr_t)arena,
           NBT_ARENA_SIZE);
  char* envp[48];
  int ne = 0;
  envp[ne++] = channel;
  envp[ne++] = arena_spec;
  extern char** environ;
  for (char** e = environ; *e && ne < 47; ++e)
    if (strncmp(*e, "NBT_", 4) == 0 && strncmp(*e, "NBT_GUEST_", 10) != 0) envp[ne++] = *e;
  envp[ne] = NULL;

  int fd_map[5] = {0, 1, 2, SPAWN_FDCLOSED, shm};
  struct inheritance inherit;
  memset(&inherit, 0, sizeof(inherit));
  uint64_t started = runner_now_ms();
  pid_t pid = spawn(path, 5, fd_map, &inherit, argv, envp);
  close(shm);
  if (pid < 0) {
    printf("runner: spawn %s failed: %s\n", path, strerror(errno));
    munmap((void*)arena, NBT_ARENA_SIZE);
    ChannelDestroy(chid);
    return RUNNER_CANNOT_START;
  }
  printf("runner: guest %s started, pid %d\n", path, (int)pid);
  fflush(stdout);

  int status = 0, hung = 0;
  for (;;) {
    uint64_t wait_ns = 200000000ull;
    TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_RECEIVE, NULL, &wait_ns, NULL);
    struct _msg_info info;
    int rcvid = MsgReceive(chid, guest_msg, sizeof(guest_msg), &info);
    if (rcvid > 0) {
      if (info.pid != pid) {
        MsgError(rcvid, EPERM);
        continue;
      }
      serve(H, rcvid, info.msglen);
      continue;
    }
    if (rcvid == 0) {
      const struct _pulse* pulse = (const struct _pulse*)(const void*)guest_msg;
      if (pulse->code == _PULSE_CODE_DISCONNECT) ConnectDetach(pulse->scoid);
    } else if (errno != ETIMEDOUT && errno != EINTR) {
      printf("runner: MsgReceive failed: %s\n", strerror(errno));
      kill(pid, SIGKILL);
    }
    if (waitpid(pid, &status, WNOHANG) == pid) break;
    if (timeout_s > 0 && runner_now_ms() - started > (uint64_t)timeout_s * 1000u) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      hung = 1;
      break;
    }
  }
  ChannelDestroy(chid);
  if (NBT_HOST_HAS(H, audio_close)) H->audio_close(); /* before the ring goes */
  const int arena_ok = arena[NBT_ARENA_ANSWER_WORD] == ~NBT_GUEST_ARENA_MAGIC;
  if (arena_ok)
    printf("runner: arena at %p shared ok, guest protocol %u\n", (void*)arena,
           (unsigned)arena[NBT_ARENA_GUEST_VERSION_WORD]);
  else
    printf("runner: arena at %p not answered\n", (void*)arena);
  munmap((void*)arena, NBT_ARENA_SIZE);
  if (g_gl_loaded) printf("runner: %u frames presented in all\n", (unsigned)g_frames);
  if (hung) {
    printf("runner: guest hung, killed after %d s\n", timeout_s);
    return RUNNER_HUNG;
  }
  if (WIFSIGNALED(status)) {
    printf("runner: guest killed by signal %d\n", WTERMSIG(status));
    return RUNNER_SIGNALED + WTERMSIG(status);
  }
  printf("runner: guest exited status=%d\n", WEXITSTATUS(status));
  return arena_ok ? WEXITSTATUS(status) : RUNNER_NOT_RUNNABLE;
}
