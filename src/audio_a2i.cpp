// SPDX-License-Identifier: MPL-2.0
#include "nbt/app/audio_a2i.hpp"

#include <dlfcn.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "nbt/app/log.hpp"

namespace nbt {
namespace app {

namespace {

typedef int (*OpenFn)(void** handle, const A2iParams* params);
typedef int (*HandleFn)(void* handle);
typedef int (*CloseFn)(void** handle);
typedef int (*GetBufferFn)(void* handle, void** buffer, uint32_t* bytes);
typedef int (*WriteFn)(void* handle, const void* buffer, uint32_t bytes, uint32_t flags);

enum { F_OPEN = 0, F_START, F_STOP, F_CLOSE, F_NOTIFY, F_GET_BUFFER, F_WRITE, F_COUNT };
const char* const kNames[F_COUNT] = {
    "A2I_Client_Open",     "A2I_Client_Start",           "A2I_Client_Stop",
    "A2I_Client_Close",    "A2I_Client_GetNotification", "A2I_Client_GetBuffer",
    "A2I_Client_Out_Write"};

#ifdef __QNXNTO__
/* The head unit's layout: the endpoint name at 0x24, 0x50 bytes in all. */
typedef char A2iEndpointOffset[offsetof(A2iParams, endpoint) == 0x24 ? 1 : -1];
typedef char A2iParamsSize[sizeof(A2iParams) == 0x50 ? 1 : -1];
#endif

/* GetNotification waits at most the params' timeout: give up on a buffer
 * after this many waits (several seconds without one coming free). */
const int kMaxWaits = 20;

}  // namespace

void a2i_fill_params(A2iParams* params, const char* endpoint, int rate, int buffer_bytes,
                     int buffers) {
  memset(params, 0, sizeof(*params));
  params->magic = kA2iMagic;
  params->direction = 0;
  params->one = 1;
  params->buffers = static_cast<uint32_t>(buffers);
  params->buffer_bytes = static_cast<uint32_t>(buffer_bytes);
  params->channels = 2;
  params->sample_bytes = 2;
  params->rate = static_cast<uint32_t>(rate);
  params->endpoint = endpoint;
  params->stereo = 1;
  params->format = 0;
  params->cookie = kA2iCookie;
  const uint64_t ms = 2000ull * static_cast<uint64_t>(buffer_bytes) *
                      static_cast<uint64_t>(buffers) / (4ull * static_cast<uint64_t>(rate));
  params->timeout_ms = static_cast<uint32_t>(ms);
}

bool a2i_endpoint_valid(const char* endpoint) {
  if (!endpoint || !endpoint[0]) return false;
  size_t n = 0;
  for (; endpoint[n]; ++n) {
    const char c = endpoint[n];
    const bool ok =
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    if (!ok || n >= 31) return false;
  }
  return true;
}

A2iOutput::A2iOutput()
    : lib_(NULL), handle_(NULL), cur_(NULL), cur_size_(0), cur_fill_(0), errors_(0) {
  memset(fn_, 0, sizeof(fn_));
}

A2iOutput::~A2iOutput() {
  close();
}

bool A2iOutput::load() {
  if (lib_) return true;
  const char* path = getenv("NBT_A2I_LIB");
  if (!path || !path[0]) path = "/opt/mm/lib/liba2i_client.so";
  lib_ = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!lib_) {
    log_info("a2i: cannot load %s: %s", path, dlerror());
    return false;
  }
  for (int i = 0; i < F_COUNT; ++i) {
    fn_[i] = dlsym(lib_, kNames[i]);
    if (!fn_[i]) {
      log_info("a2i: %s has no %s", path, kNames[i]);
      dlclose(lib_);
      lib_ = NULL;
      return false;
    }
  }
  return true;
}

bool A2iOutput::open(const char* endpoint, int rate, int buffer_bytes, int buffers) {
  if (handle_) return true;
  if (!a2i_endpoint_valid(endpoint) || !load()) return false;
  A2iParams params;
  a2i_fill_params(&params, endpoint, rate, buffer_bytes, buffers);
  void* handle = NULL;
  const int rc = reinterpret_cast<OpenFn>(fn_[F_OPEN])(&handle, &params);
  if (rc != 0 || !handle) {
    log_info("a2i: open %s failed: %d", endpoint, rc);
    return false;
  }
  handle_ = handle;
  errors_ = 0;
  /* As the head unit's own clients: every buffer queued with silence, then Start. */
  int primed = 0;
  while (primed < buffers && next_buffer(false)) {
    memset(cur_, 0, cur_size_);
    cur_fill_ = cur_size_;
    if (!flush_buffer()) break;
    ++primed;
  }
  const int started = reinterpret_cast<HandleFn>(fn_[F_START])(handle_);
  log_info("a2i: %s open, %d Hz, %d-byte buffers x%d, %d primed, start=%d", endpoint, rate,
           buffer_bytes, buffers, primed, started);
  return true;
}

bool A2iOutput::next_buffer(bool wait) {
  for (int waits = 0; waits <= kMaxWaits; ++waits) {
    void* buffer = NULL;
    uint32_t bytes = 0;
    const int rc = reinterpret_cast<GetBufferFn>(fn_[F_GET_BUFFER])(handle_, &buffer, &bytes);
    if (rc == 0 && buffer && bytes > 0) {
      cur_ = static_cast<uint8_t*>(buffer);
      cur_size_ = bytes;
      cur_fill_ = 0;
      return true;
    }
    if (rc != kA2iNoBuffer) {
      if (++errors_ <= 5) log_info("a2i: get buffer failed: %d", rc);
      return false;
    }
    if (!wait) return false;
    reinterpret_cast<HandleFn>(fn_[F_NOTIFY])(handle_);
  }
  if (++errors_ <= 5) log_info("a2i: no buffer came free");
  return false;
}

bool A2iOutput::flush_buffer() {
  const int rc = reinterpret_cast<WriteFn>(fn_[F_WRITE])(handle_, cur_, cur_fill_, 0);
  cur_ = NULL;
  cur_size_ = cur_fill_ = 0;
  if (rc != 0 && ++errors_ <= 5) log_info("a2i: write failed: %d", rc);
  return rc == 0;
}

bool A2iOutput::write(const int16_t* lr, int frames) {
  if (!handle_ || frames <= 0) return false;
  const uint8_t* src = reinterpret_cast<const uint8_t*>(lr);
  uint32_t left = static_cast<uint32_t>(frames) * 4u;
  while (left > 0) {
    if (!cur_ && !next_buffer(true)) return false;
    uint32_t n = cur_size_ - cur_fill_;
    if (n > left) n = left;
    memcpy(cur_ + cur_fill_, src, n);
    cur_fill_ += n;
    src += n;
    left -= n;
    if (cur_fill_ == cur_size_ && !flush_buffer()) return false;
  }
  return true;
}

void A2iOutput::close() {
  if (handle_) {
    cur_ = NULL;
    cur_size_ = cur_fill_ = 0;
    reinterpret_cast<HandleFn>(fn_[F_STOP])(handle_);
    reinterpret_cast<CloseFn>(fn_[F_CLOSE])(&handle_);
    handle_ = NULL;
    log_info("a2i: closed");
  }
  if (lib_) {
    dlclose(lib_);
    lib_ = NULL;
  }
}

}  // namespace app
}  // namespace nbt
