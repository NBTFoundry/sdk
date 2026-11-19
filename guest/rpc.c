/* SPDX-License-Identifier: MPL-2.0 */
/* Mode B guest: what the runner serves (include/nbt/guest_proto.h), one
 * MsgSendv per request: output, files confined to the application's folders,
 * randomness and the application services (guest.h). */
#include <stddef.h>
#include <string.h>

#include "guest.h"
#include "qnx_kernel.h"

static int runner_coid = -1;

void guest_rpc_init(int coid) { runner_coid = coid; }

/* One request: its header filled here, up to two payloads, the output back. */
static int64_t request(nbt_guest_req* req, const void* p1, uint32_t n1, const void* p2, uint32_t n2, void* out,
                       uint32_t out_len) {
  req->type = NBT_GUEST_MSG_TYPE;
  req->len1 = n1;
  req->len2 = n2;
  struct qk_iov send[3] = {{req, sizeof(*req)}, {p1, n1}, {p2, n2}};
  nbt_guest_rep rep = {-NBT_EIO};
  struct qk_iov recv[2] = {{&rep, sizeof(rep)}, {out, out_len}};
  if (qk_msg_sendv(runner_coid, send, 3, recv, out ? 2 : 1) < 0) return -NBT_EIO;
  return rep.result;
}

int64_t guest_call(uint16_t op, int32_t a, int32_t b, const void* in, uint32_t in_len, void* out,
                   uint32_t out_len) {
  nbt_guest_req req = {0};
  req.op = op;
  req.a = a;
  req.b = b;
  return request(&req, in, in_len, NULL, 0, out, out_len);
}

/* A text's bytes with its \0, or 0 when there is none or it is too long. */
static uint32_t text_bytes(const char* text) {
  const size_t n = text ? strlen(text) + 1 : 0;
  return n <= NBT_GUEST_MAX_TEXT ? (uint32_t)n : 0;
}

static int32_t path_call(uint16_t op, const char* path, int32_t a, int32_t b, void* out, uint32_t out_len) {
  if (!path) return -NBT_EINVAL;
  const uint32_t n = text_bytes(path);
  if (!n) return -NBT_ENAMETOOLONG;
  return (int32_t)guest_call(op, a, b, path, n, out, out_len);
}

/* Optional text, dropped when too long; integers for the rest. */
static int64_t text_call(uint16_t op, const char* text) {
  const uint32_t n = text_bytes(text);
  return guest_call(op, 0, 0, n ? text : NULL, n, NULL, 0);
}

static int32_t fd_call(uint16_t op, int32_t fd, void* out, uint32_t out_len) {
  return (int32_t)guest_call(op, fd, 0, NULL, 0, out, out_len);
}

static int32_t no_args(uint16_t op) { return (int32_t)guest_call(op, 0, 0, NULL, 0, NULL, 0); }

/* Writes in payload-sized pieces: the bytes written, or the first error. */
static int32_t write_pieces(uint16_t op, int32_t fd, const void* buf, uint32_t len) {
  uint32_t done = 0;
  while (done < len) {
    const uint32_t n = len - done > NBT_GUEST_MAX_PAYLOAD ? NBT_GUEST_MAX_PAYLOAD : len - done;
    const int64_t rc = guest_call(op, fd, 0, (const char*)buf + done, n, NULL, 0);
    if (rc < 0) return done ? (int32_t)done : (int32_t)rc;
    done += (uint32_t)rc;
    if ((uint32_t)rc < n) break;
  }
  return (int32_t)done;
}

/* --- output, files, randomness --- */

int32_t guest_write_output(int32_t fd, const void* buf, uint32_t len) {
  if (fd != 1 && fd != 2) return -NBT_EBADF;
  return write_pieces(NBT_GUEST_OUTPUT, fd, buf, len);
}

static int32_t g_file_open(const char* path, int32_t flags, int32_t mode) {
  return path_call(NBT_GUEST_OPEN, path, flags, mode, NULL, 0);
}
static int32_t g_file_read(int32_t fd, void* buf, uint32_t len) {
  if (len > NBT_GUEST_MAX_PAYLOAD) len = NBT_GUEST_MAX_PAYLOAD;
  return (int32_t)guest_call(NBT_GUEST_READ, fd, (int32_t)len, NULL, 0, buf, len);
}
static int32_t g_file_write(int32_t fd, const void* buf, uint32_t len) {
  return write_pieces(NBT_GUEST_WRITE, fd, buf, len);
}
static int64_t g_file_seek(int32_t fd, int64_t offset, int32_t whence) {
  nbt_guest_req req = {0};
  req.op = NBT_GUEST_SEEK;
  req.a = fd;
  req.b = whence;
  req.d = offset;
  return request(&req, NULL, 0, NULL, 0, NULL, 0);
}
static int32_t g_file_close(int32_t fd) { return fd_call(NBT_GUEST_CLOSE, fd, NULL, 0); }
static int32_t g_file_sync(int32_t fd) { return fd_call(NBT_GUEST_SYNC, fd, NULL, 0); }
static int32_t g_file_fstat(int32_t fd, nbt_stat* out) { return fd_call(NBT_GUEST_FSTAT, fd, out, sizeof(*out)); }
static int32_t g_file_stat(const char* path, nbt_stat* out) {
  return path_call(NBT_GUEST_STAT, path, 0, 0, out, sizeof(*out));
}
static int32_t g_file_unlink(const char* path) { return path_call(NBT_GUEST_UNLINK, path, 0, 0, NULL, 0); }
static int32_t g_file_mkdir(const char* path, int32_t mode) {
  return path_call(NBT_GUEST_MKDIR, path, 0, mode, NULL, 0);
}
static int32_t g_file_rename(const char* from, const char* to) {
  if (!from || !to) return -NBT_EINVAL;
  const uint32_t a = text_bytes(from), b = text_bytes(to);
  if (!a || !b) return -NBT_ENAMETOOLONG;
  nbt_guest_req req = {0};
  req.op = NBT_GUEST_RENAME;
  return (int32_t)request(&req, from, a, to, b, NULL, 0);
}

static int32_t g_random_bytes(void* buf, uint32_t len) {
  for (uint32_t done = 0; done < len;) {
    const uint32_t n = len - done > 256 ? 256 : len - done;
    const int64_t rc = guest_call(NBT_GUEST_RANDOM, 0, (int32_t)n, NULL, 0, (char*)buf + done, n);
    if (rc != (int64_t)n) return rc < 0 ? (int32_t)rc : -NBT_EIO;
    done += n;
  }
  return (int32_t)len;
}

/* --- application services --- */

static int32_t g_app_info(nbt_app_info* out) {
  return (int32_t)guest_call(NBT_GUEST_APP_INFO, 0, 0, NULL, 0, out, sizeof(*out));
}

static int32_t g_app_settings(char* buf, uint32_t len, uint32_t* generation) {
  char reply[4 + 1024];
  if (len > sizeof(reply) - 4) len = sizeof(reply) - 4;
  const int64_t n = guest_call(NBT_GUEST_APP_SETTINGS, 0, (int32_t)len, NULL, 0, reply, 4 + len);
  if (generation) memcpy(generation, reply, 4);
  if (n > 0) memcpy(buf, reply + 4, (size_t)n);
  return (int32_t)n;
}

/* Events, or a stop when the runner is gone. */
static uint32_t events(int64_t r) { return r < 0 ? NBT_APP_EV_STOP : (uint32_t)r; }

static int32_t g_app_ready(void) { return no_args(NBT_GUEST_APP_READY); }
static uint32_t g_app_tick(const char* status) { return events(text_call(NBT_GUEST_APP_TICK, status)); }
static uint32_t g_app_poll(void) { return events(no_args(NBT_GUEST_APP_POLL)); }
static void g_app_status(const char* text) { text_call(NBT_GUEST_APP_STATUS, text); }
static void g_app_request_stop(void) { no_args(NBT_GUEST_APP_STOP); }

static int32_t g_bridge_recv(int32_t source, void* buf, uint32_t len) {
  if (len > 4096) len = 4096;
  return (int32_t)guest_call(NBT_GUEST_BRIDGE_RECV, source, (int32_t)len, NULL, 0, buf, len);
}

static int32_t g_dir_open(const char* path) { return path_call(NBT_GUEST_DIR_OPEN, path, 0, 0, NULL, 0); }
static int32_t g_dir_read(int32_t dir, char* name, uint32_t name_len, uint32_t* mode) {
  char reply[4 + 256];
  memset(reply, 0, sizeof(reply));
  const int32_t r = fd_call(NBT_GUEST_DIR_READ, dir, reply, sizeof(reply));
  if (r <= 0) return r;
  reply[sizeof(reply) - 1] = '\0';
  const size_t n = strlen(reply + 4);
  if (n >= name_len) return -NBT_ENAMETOOLONG;
  memcpy(name, reply + 4, n + 1);
  if (mode) memcpy(mode, reply, 4);
  return 1;
}
static int32_t g_dir_close(int32_t dir) { return fd_call(NBT_GUEST_DIR_CLOSE, dir, NULL, 0); }

void guest_rpc_fill(nbt_host_v1* h) {
  h->write_output = guest_write_output;
  h->file_open = g_file_open;
  h->file_read = g_file_read;
  h->file_write = g_file_write;
  h->file_seek = g_file_seek;
  h->file_close = g_file_close;
  h->file_stat = g_file_stat;
  h->file_fstat = g_file_fstat;
  h->file_unlink = g_file_unlink;
  h->file_mkdir = g_file_mkdir;
  h->file_rename = g_file_rename;
  h->file_sync = g_file_sync;
  h->random_bytes = g_random_bytes;
  h->app_info = g_app_info;
  h->app_settings = g_app_settings;
  h->app_ready = g_app_ready;
  h->app_tick = g_app_tick;
  h->app_poll = g_app_poll;
  h->app_status = g_app_status;
  h->app_request_stop = g_app_request_stop;
  h->bridge_recv = g_bridge_recv;
  h->dir_open = g_dir_open;
  h->dir_read = g_dir_read;
  h->dir_close = g_dir_close;
}
