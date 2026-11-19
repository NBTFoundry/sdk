/* SPDX-License-Identifier: MPL-2.0 */
/* Mode B guest: video and GL as commands in the arena, run by the runner on
 * its context (gl_stream.c), and audio rendered by a guest thread into the
 * arena's ring, which the runner's playback thread drains (guest.h). */
#include <stddef.h>

#include "gl_stream_guest.h"
#include "guest.h"

static int arena_ready(void) { return guest_arena != NULL; }

/* --- video and GL --- */

static int gl_run(uint32_t bytes) {
  /* Every run waits: the runner answers once it is done. */
  return (int)guest_call(NBT_GUEST_GL_RUN, (int32_t)bytes, 0, NULL, 0, NULL, 0);
}

static int32_t g_video_open(int32_t surface_id, int32_t* width, int32_t* height) {
  if (!arena_ready()) return -NBT_ENOSYS;
  int32_t wh[2] = {0, 0};
  const int64_t rc = guest_call(NBT_GUEST_VIDEO_OPEN, surface_id, 0, NULL, 0, wh, sizeof(wh));
  if (rc < 0) return (int32_t)rc;
  *width = wh[0];
  *height = wh[1];
  nbtgl_attach((volatile uint8_t*)guest_arena, gl_run);
  return 0;
}
static void g_video_swap(void) {
  guest_call(NBT_GUEST_VIDEO_SWAP, (int32_t)nbtgl_take(), 0, NULL, 0, NULL, 0);
}
static void g_video_close(void) {
  nbtgl_take(); /* unrun commands of a closed context: dropped */
  guest_call(NBT_GUEST_VIDEO_CLOSE, 0, 0, NULL, 0, NULL, 0);
}
static int32_t g_video_poll_event(nbt_video_event* out) {
  (void)out; /* head-unit windows have no events */
  return 0;
}

/* --- audio --- */

static nbt_audio_render_fn audio_render;
static void* audio_context;
static int32_t audio_frag;
static volatile uint32_t audio_running;
static uintptr_t audio_thread;

static void* audio_loop(void* arg) {
  (void)arg;
  volatile uint32_t* ctl = guest_arena;
  int16_t* ring = (int16_t*)(void*)((uint8_t*)guest_arena + NBT_AUDIO_OFF);
  const uint32_t target = (uint32_t)audio_frag * 2; /* about two fragments queued */
  int16_t buf[2 * 1024];
  while (LOAD(&audio_running)) {
    const uint32_t queued = ctl[NBT_AUDIO_WRITE_WORD] - LOAD(&ctl[NBT_AUDIO_READ_WORD]);
    if (queued >= target) {
      guest_sleep_ns(2000000);
      continue;
    }
    uint32_t frames = target - queued;
    if (frames > 1024) frames = 1024;
    audio_render(audio_context, buf, (int32_t)frames);
    uint32_t wr = ctl[NBT_AUDIO_WRITE_WORD];
    for (uint32_t i = 0; i < frames; ++i, ++wr) {
      ring[2 * (wr % NBT_AUDIO_FRAMES)] = buf[2 * i];
      ring[2 * (wr % NBT_AUDIO_FRAMES) + 1] = buf[2 * i + 1];
    }
    STORE(&ctl[NBT_AUDIO_WRITE_WORD], wr);
  }
  return NULL;
}

static int32_t g_audio_open(int32_t rate, nbt_audio_render_fn render, void* context, int32_t route,
                            int32_t* out_rate, int32_t* out_frag) {
  if (!arena_ready() || !render || LOAD(&audio_running)) return -NBT_EINVAL;
  guest_arena[NBT_AUDIO_WRITE_WORD] = guest_arena[NBT_AUDIO_READ_WORD] = 0;
  int32_t rf[2] = {0, 0};
  const int64_t rc = guest_call(NBT_GUEST_AUDIO_OPEN, route, rate, NULL, 0, rf, sizeof(rf));
  if (rc < 0) return (int32_t)rc;
  audio_render = render;
  audio_context = context;
  audio_frag = rf[1] > 0 && rf[1] <= 4096 ? rf[1] : 512;
  STORE(&audio_running, 1);
  if (guest_thread_create(&audio_thread, audio_loop, NULL, NBT_APP_STACK_SIZE) != 0) {
    STORE(&audio_running, 0);
    guest_call(NBT_GUEST_AUDIO_CLOSE, 0, 0, NULL, 0, NULL, 0);
    return -NBT_EAGAIN;
  }
  *out_rate = rf[0];
  *out_frag = audio_frag;
  return 0;
}
static void g_audio_close(void) {
  if (!LOAD(&audio_running)) return;
  STORE(&audio_running, 0);
  guest_thread_join(audio_thread, NULL);
  guest_call(NBT_GUEST_AUDIO_CLOSE, 0, 0, NULL, 0, NULL, 0);
}
static void g_audio_set_route(int32_t wanted) { guest_call(NBT_GUEST_AUDIO_ROUTE, wanted, 0, NULL, 0, NULL, 0); }

void guest_media_fill(nbt_host_v1* h) {
  h->video_open = g_video_open;
  h->video_swap = g_video_swap;
  h->video_close = g_video_close;
  h->video_poll_event = g_video_poll_event;
  h->gl_proc = nbtgl_proc;
  h->audio_open = g_audio_open;
  h->audio_close = g_audio_close;
  h->audio_set_route = g_audio_set_route;
}
