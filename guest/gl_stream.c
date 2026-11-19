/* SPDX-License-Identifier: MPL-2.0 */
/* Mode B: OpenGL ES 2.0 in a guest, as commands for the runner
 * (include/nbt/gl_stream.h). The encoders are generated
 * (tools/gen_gl_stream.py); this holds the buffer, the state the stream needs
 * (buffer bindings, client-side vertex arrays, pixel alignment) and the
 * commands whose sizes depend on that state. */
#include <stdlib.h>
#include <string.h>

#include "gl_stream_guest.h"

static volatile uint8_t* arena;
static int (*run_commands)(uint32_t bytes);
static uint32_t used; /* bytes written in the command area */

void nbtgl_attach(volatile uint8_t* a, int (*run)(uint32_t bytes)) {
  arena = a;
  run_commands = run;
  used = 0;
}

static int by_name(const void* key, const void* entry) {
  return strcmp((const char*)key, ((const struct nbtgl_entry*)entry)->name);
}

void* nbtgl_proc(const char* name) {
  const struct nbtgl_entry* e =
      bsearch(name, nbtgl_entries, NBTGL_ID_END - 1, sizeof(nbtgl_entries[0]), by_name);
  return e ? e->fn : NULL;
}

uint32_t nbtgl_take(void) {
  uint32_t n = used;
  used = 0;
  return n;
}

uint32_t* nbtgl_begin(uint32_t id, uint64_t bytes) {
  bytes = NBTGL_PAD(bytes);
  if (!arena || bytes > NBTGL_CMD_SIZE) return NULL;
  if (used + bytes > NBTGL_CMD_SIZE && run_commands) run_commands(nbtgl_take());
  uint32_t* w = (uint32_t*)(void*)(arena + NBTGL_CMD_OFF + used);
  w[0] = id << 16;
  return w;
}

size_t nbtgl_put(uint32_t* w, size_t at, const void* data, size_t bytes) {
  w[at++] = (uint32_t)bytes;
  if (bytes) {
    if (data) memcpy(&w[at], data, bytes);
    else memset(&w[at], 0, bytes);
  }
  return at + (size_t)NBTGL_PAD(bytes) / 4;
}

void nbtgl_end(uint32_t* w, size_t at) {
  used = (uint32_t)((uint8_t*)&w[at] - (uint8_t*)(arena + NBTGL_CMD_OFF));
}

const uint8_t* nbtgl_sync(void) {
  if (!run_commands || run_commands(nbtgl_take()) < 0) return NULL;
  return (const uint8_t*)(arena + NBTGL_RESULT_OFF);
}

/* A command of fixed words: header and `count` arguments. */
static void send_words(uint32_t id, const uint32_t* args, size_t count) {
  uint32_t* w = nbtgl_begin(id, (1 + count) * 4);
  if (!w) return;
  memcpy(&w[1], args, count * 4);
  nbtgl_end(w, 1 + count);
}

/* --- tracked state --- */

enum { kAttribs = NBTGL_ATTRIBS };
static struct {
  int enabled, client;
  GLint size;
  GLenum type;
  GLboolean normalized;
  GLsizei stride;
  const void* pointer;
} attribs[kAttribs];
static GLuint array_buffer, element_buffer;
static GLint unpack_alignment = 4, pack_alignment = 4;

void nbtgl_track_glBindBuffer(GLenum target, GLuint buffer) {
  if (target == GL_ARRAY_BUFFER) array_buffer = buffer;
  if (target == GL_ELEMENT_ARRAY_BUFFER) element_buffer = buffer;
}
void nbtgl_track_glDeleteBuffers(GLsizei n, const GLuint* ids) {
  for (GLsizei i = 0; i < n; ++i) {
    if (ids[i] && ids[i] == array_buffer) array_buffer = 0;
    if (ids[i] && ids[i] == element_buffer) element_buffer = 0;
  }
}
void nbtgl_track_glPixelStorei(GLenum pname, GLint param) {
  if (param != 1 && param != 2 && param != 4 && param != 8) return; /* GL refuses it too */
  if (pname == GL_UNPACK_ALIGNMENT) unpack_alignment = param;
  if (pname == GL_PACK_ALIGNMENT) pack_alignment = param;
}
void nbtgl_track_glEnableVertexAttribArray(GLuint index) {
  if (index < kAttribs) attribs[index].enabled = 1;
}
void nbtgl_track_glDisableVertexAttribArray(GLuint index) {
  if (index < kAttribs) attribs[index].enabled = 0;
}

/* --- vertex data: client arrays travel with each draw --- */

void nbtgl_special_glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                                          GLsizei stride, const void* pointer) {
  if (index >= kAttribs) return;
  attribs[index].client = array_buffer == 0;
  attribs[index].size = size;
  attribs[index].type = type;
  attribs[index].normalized = normalized;
  attribs[index].stride = stride;
  attribs[index].pointer = pointer;
  if (attribs[index].client) return; /* sent with the draws */
  const uint32_t args[] = {index, (uint32_t)size, type, normalized, (uint32_t)stride,
                           (uint32_t)(uintptr_t)pointer /* an offset into the bound buffer */};
  send_words(NBTGL_ID_glVertexAttribPointer, args, 6);
}

static void send_client_arrays(uint32_t first, uint32_t count) {
  for (unsigned i = 0; i < kAttribs; ++i) {
    if (!attribs[i].enabled || !attribs[i].client || !attribs[i].pointer || count == 0) continue;
    const size_t elem = (size_t)attribs[i].size * nbtgl_type_size(attribs[i].type);
    const size_t stride = attribs[i].stride ? (size_t)attribs[i].stride : elem;
    const size_t start = (size_t)first * stride, bytes = (size_t)(count - 1) * stride + elem;
    uint32_t* w = nbtgl_begin(NBTGL_CMD_CLIENT_ARRAY, 7 * 4 + 4 + (uint64_t)bytes);
    if (!w) return;
    w[1] = i, w[2] = (uint32_t)attribs[i].size, w[3] = attribs[i].type, w[4] = attribs[i].normalized;
    w[5] = (uint32_t)stride, w[6] = (uint32_t)start;
    nbtgl_end(w, nbtgl_put(w, 7, (const uint8_t*)attribs[i].pointer + start, bytes));
  }
}

void nbtgl_special_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
  if (first >= 0 && count > 0) send_client_arrays((uint32_t)first, (uint32_t)count);
  const uint32_t args[] = {mode, (uint32_t)first, (uint32_t)count};
  send_words(NBTGL_ID_glDrawArrays, args, 3);
}

void nbtgl_special_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
  if (count <= 0) return;
  const size_t bytes = (size_t)count * nbtgl_type_size(type);
  if (!element_buffer) { /* client indices: their vertex range goes along */
    uint32_t lo, hi;
    nbtgl_index_range(indices, (uint32_t)count, type, &lo, &hi);
    send_client_arrays(lo, hi - lo + 1);
  }
  uint32_t* w = nbtgl_begin(NBTGL_ID_glDrawElements, 5 * 4 + 4 + (element_buffer ? 0 : (uint64_t)bytes));
  if (!w) return;
  w[1] = mode, w[2] = (uint32_t)count, w[3] = type, w[4] = element_buffer != 0;
  size_t at = 5;
  if (element_buffer) w[at++] = (uint32_t)(uintptr_t)indices;
  else at = nbtgl_put(w, at, indices, bytes);
  nbtgl_end(w, at);
}

void nbtgl_special_glGetVertexAttribPointerv(GLuint index, GLenum pname, void** pointer) {
  (void)pname; /* GL_VERTEX_ATTRIB_ARRAY_POINTER, the only one */
  *pointer = index < kAttribs ? (void*)attribs[index].pointer : NULL;
}

/* --- textures and pixels --- */

/* args: the call's eight scalars, `width` and `height` at the given places. */
static void tex_image(uint32_t id, const uint32_t args[8], unsigned width_at, const void* pixels) {
  const uint32_t width = args[width_at], height = args[width_at + 1], format = args[6], type = args[7];
  const size_t bytes = pixels ? nbtgl_image_size(width, height, format, type, (uint32_t)unpack_alignment) : 0;
  uint32_t* w = nbtgl_begin(id, 9 * 4 + 4 + (uint64_t)bytes);
  if (!w) return;
  memcpy(&w[1], args, 8 * 4);
  nbtgl_end(w, nbtgl_put(w, 9, pixels, bytes));
}

void nbtgl_special_glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                                 GLint border, GLenum format, GLenum type, const void* pixels) {
  const uint32_t args[] = {target, (uint32_t)level, (uint32_t)internalformat, (uint32_t)width,
                           (uint32_t)height, (uint32_t)border, format, type};
  tex_image(NBTGL_ID_glTexImage2D, args, 3, pixels);
}

void nbtgl_special_glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                                    GLsizei height, GLenum format, GLenum type, const void* pixels) {
  const uint32_t args[] = {target, (uint32_t)level, (uint32_t)xoffset, (uint32_t)yoffset,
                           (uint32_t)width, (uint32_t)height, format, type};
  tex_image(NBTGL_ID_glTexSubImage2D, args, 4, pixels);
}

void nbtgl_special_glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
                                 void* pixels) {
  const size_t bytes =
      nbtgl_image_size((uint32_t)width, (uint32_t)height, format, type, (uint32_t)pack_alignment);
  if (bytes == 0 || bytes + 4 > NBTGL_RESULT_SIZE) return;
  const uint32_t args[] = {(uint32_t)x, (uint32_t)y, (uint32_t)width, (uint32_t)height,
                           format,      type,        (uint32_t)pack_alignment, (uint32_t)bytes};
  send_words(NBTGL_ID_glReadPixels, args, 8);
  const uint8_t* r = nbtgl_sync();
  if (r && pixels) memcpy(pixels, r + 4, bytes);
}

/* --- state queries --- */

static void get_values(uint32_t id, GLenum pname, void* out, size_t elem);

/* How many values a glGet*v of `pname` writes (OpenGL ES 2.0). */
static uint32_t values_of(GLenum pname) {
  switch (pname) {
    case GL_VIEWPORT:
    case GL_SCISSOR_BOX:
    case GL_COLOR_WRITEMASK:
    case GL_COLOR_CLEAR_VALUE:
    case GL_BLEND_COLOR: return 4;
    case GL_DEPTH_RANGE:
    case GL_ALIASED_POINT_SIZE_RANGE:
    case GL_ALIASED_LINE_WIDTH_RANGE:
    case GL_MAX_VIEWPORT_DIMS: return 2;
    case GL_COMPRESSED_TEXTURE_FORMATS:
    case GL_SHADER_BINARY_FORMATS: {
      GLint n = 0;
      get_values(NBTGL_ID_glGetIntegerv,
                 pname == GL_COMPRESSED_TEXTURE_FORMATS ? GL_NUM_COMPRESSED_TEXTURE_FORMATS
                                                        : GL_NUM_SHADER_BINARY_FORMATS,
                 &n, sizeof(n));
      return n < 0 ? 0 : (uint32_t)n > NBTGL_MAX_VALUES ? NBTGL_MAX_VALUES : (uint32_t)n;
    }
    default: return 1;
  }
}

static void get_values(uint32_t id, GLenum pname, void* out, size_t elem) {
  const uint32_t args[] = {pname, values_of(pname)};
  send_words(id, args, 2);
  const uint8_t* r = nbtgl_sync();
  if (r && out) memcpy(out, r + 4, args[1] * elem);
}

void nbtgl_special_glGetIntegerv(GLenum pname, GLint* data) {
  get_values(NBTGL_ID_glGetIntegerv, pname, data, sizeof(*data));
}
void nbtgl_special_glGetFloatv(GLenum pname, GLfloat* data) {
  get_values(NBTGL_ID_glGetFloatv, pname, data, sizeof(*data));
}
void nbtgl_special_glGetBooleanv(GLenum pname, GLboolean* data) {
  get_values(NBTGL_ID_glGetBooleanv, pname, data, sizeof(*data));
}

/* Not streamed: the values are left as they were. */
void nbtgl_special_glGetUniformfv(GLuint program, GLint location, GLfloat* params) {
  (void)program, (void)location, (void)params;
}
void nbtgl_special_glGetUniformiv(GLuint program, GLint location, GLint* params) {
  (void)program, (void)location, (void)params;
}

/* --- strings --- */

void nbtgl_special_glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length) {
  if (count <= 0 || count > NBTGL_MAX_SHADER_STRINGS) return;
  uint64_t bytes = 3 * 4;
  for (GLsizei i = 0; i < count; ++i)
    bytes += 4 + NBTGL_PAD(length && length[i] >= 0 ? (size_t)length[i] : strlen(string[i]));
  uint32_t* w = nbtgl_begin(NBTGL_ID_glShaderSource, bytes);
  if (!w) return;
  w[1] = shader, w[2] = (uint32_t)count;
  size_t at = 3;
  for (GLsizei i = 0; i < count; ++i)
    at = nbtgl_put(w, at, string[i], length && length[i] >= 0 ? (size_t)length[i] : strlen(string[i]));
  nbtgl_end(w, at);
}

/* GL's strings outlive the call: kept here, one per name. */
const GLubyte* nbtgl_special_glGetString(GLenum name) {
  static struct {
    GLenum name;
    char text[NBTGL_MAX_STRING + 1];
  } cache[6];
  enum { kCached = sizeof(cache) / sizeof(cache[0]) };
  for (unsigned i = 0; i < kCached; ++i)
    if (cache[i].name == name && cache[i].text[0]) return (const GLubyte*)cache[i].text;
  const uint32_t args[] = {name};
  send_words(NBTGL_ID_glGetString, args, 1);
  const uint8_t* r = nbtgl_sync();
  if (!r) return NULL;
  uint32_t len;
  memcpy(&len, r, 4);
  if (len == 0 || len > NBTGL_MAX_STRING) return NULL;
  for (unsigned i = 0; i < kCached; ++i) {
    if (!cache[i].text[0]) {
      cache[i].name = name;
      memcpy(cache[i].text, r + 4, len);
      cache[i].text[len] = '\0';
      return (const GLubyte*)cache[i].text;
    }
  }
  return NULL;
}
