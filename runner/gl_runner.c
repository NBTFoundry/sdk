/* SPDX-License-Identifier: MPL-2.0 */
#include "gl_runner.h"

#include <stdlib.h>
#include <string.h>

static uint8_t* result_area;

const void* nbtgl_get(const uint32_t* w, size_t n, size_t* at, uint32_t* bytes) {
  if (*at >= n) return NULL;
  const uint32_t b = w[*at];
  if (b > (n - *at - 1) * 4) return NULL;
  const void* p = &w[*at + 1];
  *at += 1 + (size_t)NBTGL_PAD(b) / 4;
  *bytes = b;
  return p;
}

uint8_t* nbtgl_result(uint64_t bytes) { return bytes <= NBTGL_RESULT_SIZE ? result_area : NULL; }

/* --- client-side vertex arrays: kept here between a batch's draws --- */

enum { kAttribs = NBTGL_ATTRIBS };
static struct {
  uint8_t* data;
  size_t cap;
  int enabled; /* glEnableVertexAttribArray */
  int backed;  /* points into data (a client array), not into a buffer */
  uint32_t start, bytes, elem, stride;
} client[kAttribs];

void nbtgl_track_glEnableVertexAttribArray(GLuint index) {
  if (index < kAttribs) client[index].enabled = 1;
}
void nbtgl_track_glDisableVertexAttribArray(GLuint index) {
  if (index < kAttribs) client[index].enabled = 0;
}

/* GL reads vertices [first, first + count) of every enabled attribute: those
 * backed by a copy here must cover them (the copy is all the guest sent). */
static int vertices_covered(uint32_t first, uint32_t count) {
  if (count == 0) return 1;
  for (unsigned i = 0; i < kAttribs; ++i) {
    if (!client[i].enabled || !client[i].backed) continue;
    const uint64_t lo = (uint64_t)first * client[i].stride;
    const uint64_t hi = ((uint64_t)first + count - 1) * client[i].stride + client[i].elem;
    if (lo < client[i].start || hi > (uint64_t)client[i].start + client[i].bytes) return 0;
  }
  return 1;
}

static int client_array(const uint32_t* w, size_t n, size_t* pos) {
  size_t at = *pos;
  if (n - at < 6) return -1;
  const uint32_t index = w[at], size = w[at + 1], type = w[at + 2], normalized = w[at + 3];
  const uint32_t stride = w[at + 4], start = w[at + 5];
  at += 6;
  uint32_t bytes;
  const void* data = nbtgl_get(w, n, &at, &bytes);
  if (!data || index >= kAttribs || size == 0 || size > 4) return -1;
  const uint32_t elem = size * nbtgl_type_size(type);
  if (stride < elem || bytes < elem) return -1;
  if (client[index].cap < bytes) {
    uint8_t* p = (uint8_t*)realloc(client[index].data, bytes);
    if (!p) return -1;
    client[index].data = p;
    client[index].cap = bytes;
  }
  memcpy(client[index].data, data, bytes);
  client[index].backed = 1;
  client[index].start = start;
  client[index].bytes = bytes;
  client[index].elem = elem;
  client[index].stride = stride;
  /* GL reads from element `first`: point `start` bytes before the copy. */
  nbtgl_fn.VertexAttribPointer(index, (GLint)size, type, (GLboolean)normalized, (GLsizei)stride,
                               client[index].data - start);
  *pos = at;
  return 0;
}

int nbtgl_run(volatile uint8_t* arena, uint32_t bytes) {
  static uint32_t* batch;
  static size_t batch_cap;
  if (bytes > NBTGL_CMD_SIZE || (bytes & 3)) return -1;
  result_area = (uint8_t*)(arena + NBTGL_RESULT_OFF);
  /* A private copy: the guest can write the arena while this decodes. */
  if (batch_cap < bytes) {
    uint32_t* p = (uint32_t*)realloc(batch, bytes);
    if (!p) return -1;
    batch = p;
    batch_cap = bytes;
  }
  memcpy(batch, (const void*)(arena + NBTGL_CMD_OFF), bytes);
  const uint32_t* w = batch;
  const size_t n = bytes / 4;
  size_t pos = 0;
  while (pos < n) {
    const uint32_t id = w[pos++] >> 16;
    int rc;
    if (id == NBTGL_CMD_CLIENT_ARRAY) rc = client_array(w, n, &pos);
    else if (id < NBTGL_ID_END && nbtgl_decoders[id]) rc = nbtgl_decoders[id](w, n, &pos);
    else rc = -1;
    if (rc < 0) return -1;
  }
  return 0;
}

/* --- the hand-written commands (guest/gl_stream.c) --- */

/* `k` argument words at w[*pos], else malformed. */
#define ARGS(k)                    \
  const uint32_t* a = &w[*pos];    \
  if (n - *pos < (k)) return -1;   \
  *pos += (k)

int nbtgl_special_glVertexAttribPointer(const uint32_t* w, size_t n, size_t* pos) {
  ARGS(6);
  if (a[0] < kAttribs) client[a[0]].backed = 0;
  nbtgl_fn.VertexAttribPointer(a[0], (GLint)a[1], a[2], (GLboolean)a[3], (GLsizei)a[4],
                               (const void*)(uintptr_t)a[5]); /* an offset into the bound buffer */
  return 0;
}

int nbtgl_special_glDrawArrays(const uint32_t* w, size_t n, size_t* pos) {
  ARGS(3);
  if ((int32_t)a[1] < 0 || !vertices_covered(a[1], a[2])) return -1;
  nbtgl_fn.DrawArrays(a[0], (GLint)a[1], (GLsizei)a[2]);
  return 0;
}

int nbtgl_special_glDrawElements(const uint32_t* w, size_t n, size_t* pos) {
  ARGS(4);
  const uint32_t mode = a[0], count = a[1], type = a[2], bound = a[3];
  const void* indices;
  if (bound) {
    if (*pos >= n) return -1;
    indices = (const void*)(uintptr_t)w[(*pos)++];
    if (!vertices_covered(0, 0x10000u)) return -1; /* index range unknown: buffers only */
  } else {
    uint32_t bytes;
    indices = nbtgl_get(w, n, pos, &bytes);
    if (!indices || (uint64_t)count * nbtgl_type_size(type) > bytes) return -1;
    uint32_t lo, hi;
    nbtgl_index_range(indices, count, type, &lo, &hi);
    if (count && !vertices_covered(lo, hi - lo + 1)) return -1;
  }
  nbtgl_fn.DrawElements(mode, (GLsizei)count, type, indices);
  return 0;
}

/* a: the call's eight scalars, `width` and `height` at the given places. */
static int tex_image(const uint32_t* w, size_t n, size_t* pos, unsigned width_at, int sub) {
  ARGS(8);
  uint32_t bytes;
  const void* pixels = nbtgl_get(w, n, pos, &bytes);
  if (!pixels) return -1;
  if (bytes && bytes < nbtgl_image_size(a[width_at], a[width_at + 1], a[6], a[7], 1))
    return -1; /* fewer bytes than the smallest valid image */
  if (!bytes) pixels = NULL;
  if (sub)
    nbtgl_fn.TexSubImage2D(a[0], (GLint)a[1], (GLint)a[2], (GLint)a[3], (GLsizei)a[4], (GLsizei)a[5], a[6], a[7],
                           pixels);
  else
    nbtgl_fn.TexImage2D(a[0], (GLint)a[1], (GLint)a[2], (GLsizei)a[3], (GLsizei)a[4], (GLint)a[5], a[6], a[7],
                        pixels);
  return 0;
}

int nbtgl_special_glTexImage2D(const uint32_t* w, size_t n, size_t* pos) { return tex_image(w, n, pos, 3, 0); }
int nbtgl_special_glTexSubImage2D(const uint32_t* w, size_t n, size_t* pos) { return tex_image(w, n, pos, 4, 1); }

int nbtgl_special_glReadPixels(const uint32_t* w, size_t n, size_t* pos) {
  ARGS(8);
  const uint32_t bytes = a[7];
  uint8_t* r = nbtgl_result(4 + (uint64_t)bytes);
  if (!r || bytes < nbtgl_image_size(a[2], a[3], a[4], a[5], a[6])) return -1;
  nbtgl_fn.PixelStorei(GL_PACK_ALIGNMENT, (GLint)a[6]);
  nbtgl_fn.ReadPixels((GLint)a[0], (GLint)a[1], (GLsizei)a[2], (GLsizei)a[3], a[4], a[5], r + 4);
  memcpy(r, &bytes, 4);
  return 1;
}

static int get_values(const uint32_t* w, size_t n, size_t* pos, void (*get)(GLenum pname, uint8_t* out)) {
  ARGS(2);
  const size_t room = 4 + NBTGL_MAX_VALUES * 4;
  uint8_t* r = nbtgl_result(room);
  if (!r || a[1] > NBTGL_MAX_VALUES) return -1;
  memset(r, 0, room);
  get(a[0], r + 4);
  return 1;
}

static void get_integers(GLenum pname, uint8_t* out) { nbtgl_fn.GetIntegerv(pname, (GLint*)(void*)out); }
static void get_floats(GLenum pname, uint8_t* out) { nbtgl_fn.GetFloatv(pname, (GLfloat*)(void*)out); }
static void get_booleans(GLenum pname, uint8_t* out) { nbtgl_fn.GetBooleanv(pname, (GLboolean*)out); }

int nbtgl_special_glGetIntegerv(const uint32_t* w, size_t n, size_t* pos) {
  return get_values(w, n, pos, get_integers);
}
int nbtgl_special_glGetFloatv(const uint32_t* w, size_t n, size_t* pos) { return get_values(w, n, pos, get_floats); }
int nbtgl_special_glGetBooleanv(const uint32_t* w, size_t n, size_t* pos) {
  return get_values(w, n, pos, get_booleans);
}

int nbtgl_special_glShaderSource(const uint32_t* w, size_t n, size_t* pos) {
  ARGS(2);
  const uint32_t shader = a[0], count = a[1];
  const GLchar* strings[NBTGL_MAX_SHADER_STRINGS];
  GLint lengths[NBTGL_MAX_SHADER_STRINGS];
  if (count == 0 || count > NBTGL_MAX_SHADER_STRINGS) return -1;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t bytes;
    strings[i] = (const GLchar*)nbtgl_get(w, n, pos, &bytes);
    if (!strings[i]) return -1;
    lengths[i] = (GLint)bytes;
  }
  nbtgl_fn.ShaderSource(shader, (GLsizei)count, strings, lengths);
  return 0;
}

int nbtgl_special_glGetString(const uint32_t* w, size_t n, size_t* pos) {
  ARGS(1);
  const GLubyte* s = nbtgl_fn.GetString(a[0]);
  uint8_t* r = nbtgl_result(4 + NBTGL_MAX_STRING);
  if (!r) return -1;
  uint32_t len = s ? (uint32_t)strlen((const char*)s) : 0;
  if (len > NBTGL_MAX_STRING) len = NBTGL_MAX_STRING;
  memcpy(r, &len, 4);
  if (len) memcpy(r + 4, s, len);
  return 1;
}
