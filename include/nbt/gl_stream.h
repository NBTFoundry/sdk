/* SPDX-License-Identifier: MPL-2.0 */
/* The OpenGL ES 2.0 command stream of a Mode B guest: its gl* calls become
 * commands in the arena's command area (guest_proto.h), which the runner
 * executes on its own context. The ids, and the encoders and decoders of
 * most commands, are generated from Khronos' gl.xml (tools/gen_gl_stream.py).
 *
 * A command is 32-bit words: a header (id << 16), the scalar arguments, then
 * [byte count][bytes, padded to a word] per input pointer and [byte count]
 * per output pointer. A command with a result or outputs is synchronous: the
 * guest runs the batch and waits, and the runner writes [return value]
 * [each output, padded] to the result area. The helpers below are shared so
 * both sides compute every size the same way. */
#ifndef NBT_GL_STREAM_H
#define NBT_GL_STREAM_H

#include <GLES2/gl2.h>
#include <stddef.h>
#include <stdint.h>

#include "nbt/guest_proto.h"

/* 64-bit, so a hostile or negative count cannot wrap. */
#define NBTGL_PAD(n) (((uint64_t)(n) + 3u) & ~(uint64_t)3u)

/* Commands of the stream's own, above gl.xml's ids. */
enum {
  /* index, size, type, normalized, stride, start, [bytes]: a client-side
   * vertex array from byte `start`, ahead of the draw that reads it */
  NBTGL_CMD_CLIENT_ARRAY = 0xfff0
};

/* Limits both ends hold to. */
#define NBTGL_MAX_IMAGE_SIDE 8192u     /* pixels */
#define NBTGL_MAX_VALUES 64u           /* of one glGet*v */
#define NBTGL_MAX_SHADER_STRINGS 64    /* of one glShaderSource */
#define NBTGL_MAX_STRING 1023u         /* bytes of a glGetString answer */
#define NBTGL_ATTRIBS 16u              /* vertex attributes tracked */

/* Bytes of one component of a vertex attribute, index or pixel `type`. */
static inline uint32_t nbtgl_type_size(uint32_t type) {
  switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE: return 1;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT: return 2;
    default: return 4;
  }
}

/* Bytes of a width x height image, rows aligned to `alignment` (1, 2, 4 or
 * 8; anything else counts as 4); 0 for an empty or oversized image. */
static inline size_t nbtgl_image_size(uint32_t width, uint32_t height, uint32_t format, uint32_t type,
                                      uint32_t alignment) {
  if (width == 0 || height == 0 || width > NBTGL_MAX_IMAGE_SIDE || height > NBTGL_MAX_IMAGE_SIDE) return 0;
  size_t pixel;
  if (type == GL_UNSIGNED_SHORT_5_6_5 || type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1) {
    pixel = 2;
  } else {
    const size_t channels = format == GL_RGBA ? 4 : format == GL_RGB ? 3 : format == GL_LUMINANCE_ALPHA ? 2 : 1;
    pixel = channels * nbtgl_type_size(type);
  }
  const size_t a = alignment == 1 || alignment == 2 || alignment == 8 ? alignment : 4;
  const size_t row = ((size_t)width * pixel + a - 1) / a * a;
  return row * (height - 1) + (size_t)width * pixel;
}

/* The smallest and largest of `count` (> 0) indices of `type`. */
static inline void nbtgl_index_range(const void* indices, uint32_t count, uint32_t type, uint32_t* lo,
                                     uint32_t* hi) {
  const uint32_t size = nbtgl_type_size(type);
  uint32_t l = 0xffffffffu, h = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t v = size == 1   ? ((const uint8_t*)indices)[i]
                       : size == 2 ? ((const uint16_t*)indices)[i]
                                   : ((const uint32_t*)indices)[i];
    if (v < l) l = v;
    if (v > h) h = v;
  }
  *lo = l;
  *hi = h;
}

#endif
