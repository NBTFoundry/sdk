/* SPDX-License-Identifier: MPL-2.0 */
/* The NBT host ABI, version 1: what the runner gives an application module.
 *
 * A module is a self-contained ARM ELF shared object built without any QNX
 * file. It exports one symbol, nbt_module_v1, which the runner calls with a
 * table of these functions. Everything QNX-specific stays behind the table.
 *
 * Rules (checked by tools/check_module.py):
 *  - AAPCS base calling convention (softfp): float and double travel in core
 *    registers; int-size enums; 4-byte wchar_t.
 *  - Plain C types only: no QNX, libc or C++ type crosses this boundary.
 *  - Errors are negative NBT_E* values; flags and modes use the NBT_* values
 *    below, never a C library's own numbering.
 *  - Nothing throws across the table.
 *
 * Minor versions only append members; a module checks a member newer than
 * v1.0 with NBT_HOST_HAS before using it. */
#ifndef NBT_HOST_ABI_H
#define NBT_HOST_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NBT_HOST_ABI_MAJOR 1
#define NBT_HOST_ABI_MINOR 1

/* The stack of a thread that runs application code without asking for a
 * size (pthread's default, a guest's audio thread). */
#define NBT_APP_STACK_SIZE (256u << 10)

/* Whether table `host` has `member`: within its size, and set. */
#define NBT_HOST_HAS(host, member) \
  ((host)->size >= offsetof(nbt_host_v1, member) + sizeof((host)->member) && (host)->member)

/* Error values (returned negated). */
enum {
  NBT_EPERM = 1,
  NBT_ENOENT = 2,
  NBT_EINTR = 4,
  NBT_EIO = 5,
  NBT_EBADF = 9,
  NBT_EAGAIN = 11,
  NBT_ENOMEM = 12,
  NBT_EACCES = 13,
  NBT_EEXIST = 17,
  NBT_ENOTDIR = 20,
  NBT_EISDIR = 21,
  NBT_EINVAL = 22,
  NBT_EMFILE = 24,
  NBT_ENOSPC = 28,
  NBT_EROFS = 30,
  NBT_ENAMETOOLONG = 36,
  NBT_ENOSYS = 38,
  NBT_ENOTEMPTY = 39,
  NBT_ETIMEDOUT = 110
};

/* file_open flags */
enum {
  NBT_O_RDONLY = 0,
  NBT_O_WRONLY = 1,
  NBT_O_RDWR = 2,
  NBT_O_ACCMODE = 3,
  NBT_O_CREAT = 0x100,
  NBT_O_TRUNC = 0x200,
  NBT_O_APPEND = 0x400,
  NBT_O_EXCL = 0x800
};

/* file_seek whence */
enum { NBT_SEEK_SET = 0, NBT_SEEK_CUR = 1, NBT_SEEK_END = 2 };

/* nbt_stat.mode file types (POSIX values) */
enum { NBT_S_IFMT = 0170000, NBT_S_IFDIR = 0040000, NBT_S_IFREG = 0100000 };

/* mem_map prot and flags */
enum { NBT_PROT_NONE = 0, NBT_PROT_READ = 1, NBT_PROT_WRITE = 2, NBT_PROT_EXEC = 4 };
enum { NBT_MAP_LAZY = 1 };

/* log levels */
enum { NBT_LOG_INFO = 0, NBT_LOG_ERROR = 1, NBT_LOG_DEBUG = 2 };

typedef struct nbt_stat {
  uint64_t size;
  int64_t mtime_ns;
  uint32_t mode;
  uint32_t reserved;
} nbt_stat;

/* v1.1: how the application was launched (the runner's NBT Manager bridge). */
enum {
  NBT_APP_CHECK = 1,      /* NBT_LAUNCH=check: health check, no display or input */
  NBT_APP_USER = 2,       /* a user launch */
  NBT_APP_CAN_SHOW = 4,   /* show requests (SIGUSR1) are delivered */
  NBT_APP_STATUS = 8,     /* status lines reach the manager */
  NBT_APP_CONTROLLER = 16, /* bridge input: iDrive controller datagrams */
  NBT_APP_GAMEPAD = 32    /* bridge input: gamepad datagrams */
};
typedef struct nbt_app_info {
  uint32_t flags;      /* NBT_APP_* */
  int32_t surface_id;  /* the manager's surface (NBT_SURFACE), 0 when none */
  char caps[256];      /* NBT_BRIDGE_CAPS, comma-separated */
} nbt_app_info;

/* app_tick and app_poll results. */
enum { NBT_APP_EV_STOP = 1, NBT_APP_EV_SHOW = 2, NBT_APP_EV_SETTINGS = 4 };

/* bridge_recv sources. */
enum { NBT_BRIDGE_CONTROLLER = 0, NBT_BRIDGE_GAMEPAD = 1 };

/* Fills `frames` stereo S16 frames (2 * frames values). */
typedef void (*nbt_audio_render_fn)(void* context, int16_t* out_lr, int32_t frames);

/* Host window events (desktop runners only). */
typedef struct nbt_video_event {
  int32_t type; /* 0 none, 1 key down, 2 key up, 3 resize, 4 close */
  int32_t key;
  int32_t width;
  int32_t height;
} nbt_video_event;

typedef struct nbt_host_v1 {
  uint32_t abi_major; /* NBT_HOST_ABI_MAJOR */
  uint32_t abi_minor; /* the host's minor version */
  uint32_t size;      /* sizeof(nbt_host_v1) as the host knows it */
  uint32_t reserved;

  /* Output: fd 1 is the application's standard output, fd 2 its error
   * output; both end up in the application's log. Returns len or -NBT_E*. */
  int32_t (*write_output)(int32_t fd, const void* buf, uint32_t len);

  /* Time */
  int64_t (*monotonic_ns)(void);
  int64_t (*realtime_ns)(void);
  int32_t (*sleep_ns)(int64_t ns);

  /* Memory: page-granular mappings. NBT_MAP_LAZY commits pages on first use. */
  void* (*mem_map)(uint32_t len, int32_t prot, int32_t flags);
  int32_t (*mem_unmap)(void* addr, uint32_t len);
  int32_t (*mem_protect)(void* addr, uint32_t len, int32_t prot);
  int32_t (*icache_flush)(void* addr, uint32_t len);

  /* Threads. thread_create starts fn(arg) on a new thread with at least
   * stack_size bytes of stack; the handle is opaque. */
  int32_t (*thread_create)(uintptr_t* handle, void* (*fn)(void*), void* arg, uint32_t stack_size);
  int32_t (*thread_join)(uintptr_t handle, void** result);
  int32_t (*thread_detach)(uintptr_t handle);
  void (*thread_yield)(void);
  /* One word of per-thread storage owned by the module's runtime. */
  void* (*thread_word_get)(void);
  void (*thread_word_set)(void* value);

  /* Address-keyed waiting (futex semantics): wait returns 0 when woken (or
   * spuriously), -NBT_EAGAIN when *addr != expected, -NBT_ETIMEDOUT after
   * timeout_ns (< 0: no timeout). wake wakes at most count waiters
   * (count < 0: all) and returns how many it may have woken. */
  int32_t (*wait)(volatile uint32_t* addr, uint32_t expected, int64_t timeout_ns);
  int32_t (*wake)(volatile uint32_t* addr, int32_t count);

  /* Files, confined by the host to the application's own folders. Relative
   * paths resolve against the data folder. */
  int32_t (*file_open)(const char* path, int32_t flags, int32_t mode);
  int32_t (*file_read)(int32_t fd, void* buf, uint32_t len);
  int32_t (*file_write)(int32_t fd, const void* buf, uint32_t len);
  int64_t (*file_seek)(int32_t fd, int64_t offset, int32_t whence);
  int32_t (*file_close)(int32_t fd);
  int32_t (*file_stat)(const char* path, nbt_stat* out);
  int32_t (*file_fstat)(int32_t fd, nbt_stat* out);
  int32_t (*file_unlink)(const char* path);
  int32_t (*file_mkdir)(const char* path, int32_t mode);
  int32_t (*file_rename)(const char* from, const char* to);
  int32_t (*file_sync)(int32_t fd);

  /* Environment, read-only: NULL when absent. */
  const char* (*get_env)(const char* name);
  /* Random bytes for seeding (stack protector, hashes). */
  int32_t (*random_bytes)(void* buf, uint32_t len);

  /* Ends the process with this status (the module does not return). */
  void (*exit_process)(int32_t status);

  /* ABI self-test hooks (probe only; the runner may leave them NULL). */
  double (*abi_test_mix)(int32_t a, double b, float c, int64_t d, float e);
  double (*abi_test_callback)(double (*fn)(float, double, int64_t, int32_t));

  /* --- v1.1: application services --- */

  /* Launch information. */
  int32_t (*app_info)(nbt_app_info* out);
  /* The current settings in the manager's format ("NBTVALUES1\n" then
   * "key=value\n" lines); returns the length, or -NBT_ENOSPC. *generation
   * changes whenever they change. */
  int32_t (*app_settings)(char* buf, uint32_t len, uint32_t* generation);
  /* Readiness: 0, or -NBT_EIO once the manager is gone. */
  int32_t (*app_ready)(void);
  /* Called from the application's main loop: heartbeat (at most once a
   * second), settings reload, status line (may be NULL). NBT_APP_EV_* bits. */
  uint32_t (*app_tick)(const char* status_text);
  /* Stop (sticky) and show (consumed) requests, without a heartbeat. */
  uint32_t (*app_poll)(void);
  void (*app_status)(const char* text);
  void (*app_request_stop)(void);

  /* Bridge input datagrams, non-blocking: bytes, or -NBT_EAGAIN when none,
   * -NBT_EBADF when that input wasn't granted. */
  int32_t (*bridge_recv)(int32_t source, void* buf, uint32_t len);

  /* Directory listing, confined like the file calls (the files folder is
   * readable). dir_read returns 1 with one entry, 0 at the end. */
  int32_t (*dir_open)(const char* path);
  int32_t (*dir_read)(int32_t dir, char* name, uint32_t name_len, uint32_t* mode);
  int32_t (*dir_close)(int32_t dir);

  /* Video: a full-panel window bound to the manager's surface with an OpenGL
   * ES 2 context current on the calling thread, showing a black frame. */
  int32_t (*video_open)(int32_t surface_id, int32_t* width, int32_t* height);
  void (*video_swap)(void);
  void (*video_close)(void);
  int32_t (*video_poll_event)(nbt_video_event* out);
  /* An OpenGL ES 2 entry point by name ("glClear"), or NULL. */
  void* (*gl_proc)(const char* name);

  /* Audio: stereo S16 output. `render` runs on a host thread. Returns 0 and
   * the device's rate and fragment size, or -NBT_E*. */
  int32_t (*audio_open)(int32_t preferred_rate, nbt_audio_render_fn render, void* context,
                        int32_t route_wanted, int32_t* rate, int32_t* frag_frames);
  void (*audio_close)(void);
  void (*audio_set_route)(int32_t wanted);
} nbt_host_v1;

/* The single export of a module. argv[0] is the application's name. Returns
 * the application's exit status. */
typedef int32_t (*nbt_module_entry_v1)(const nbt_host_v1* host, int32_t argc, char** argv);
#define NBT_MODULE_ENTRY_V1 "nbt_module_v1"

#ifdef __cplusplus
}
#endif

#endif
