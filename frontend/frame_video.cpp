// SPDX-License-Identifier: MPL-2.0
#include "nbt/frontend/frame_video.hpp"

#include <stdlib.h>
#include <string.h>

#include "nbt/app/log.hpp"

namespace nbt {
namespace frontend {

namespace {

const char kVertex[] =
    "attribute vec2 a_position;\n"
    "attribute vec2 a_uv;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  v_uv = a_uv;\n"
    "  gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "}\n";

/* Sharp: each texel keeps its colour across its middle and blends only over
 * the last output pixel at its edges (bilinear on moved coordinates). */
const char kFragment[] =
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
    "precision highp float;\n"
    "#else\n"
    "precision mediump float;\n"
    "#endif\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D u_texture;\n"
    "uniform vec2 u_size;\n"
    "uniform vec2 u_scale;\n"
    "uniform float u_swizzle;\n"
    "uniform float u_sharp;\n"
    "void main() {\n"
    "  vec2 uv = v_uv;\n"
    "  if (u_sharp > 0.5) {\n"
    "    vec2 texel = v_uv * u_size;\n"
    "    vec2 f = fract(texel) - 0.5;\n"
    "    vec2 range = 0.5 - 0.5 / u_scale;\n"
    "    uv = (floor(texel) + (f - clamp(f, -range, range)) * u_scale + 0.5) / u_size;\n"
    "  }\n"
    "  vec4 c = texture2D(u_texture, uv);\n"
    "  gl_FragColor = vec4(u_swizzle > 0.5 ? c.bgr : c.rgb, 1.0);\n"
    "}\n";

GLuint compile(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, NULL);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[256] = "";
    glGetShaderInfoLog(shader, sizeof(log), NULL, log);
    app::log_info("frame shader: %s", log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

}  // namespace

FrameVideo::FrameVideo()
    : program_(0),
      texture_(0),
      buffer_(0),
      position_(-1),
      uv_(-1),
      sampler_(-1),
      size_(-1),
      scale_(-1),
      swizzle_(-1),
      sharp_(-1),
      width_(0),
      height_(0),
      texture_w_(0),
      texture_h_(0),
      texture_format_(kRgb565),
      staging_(NULL) {}

FrameVideo::~FrameVideo() {
  free(staging_);
}

bool FrameVideo::init() {
  const GLuint vs = compile(GL_VERTEX_SHADER, kVertex);
  const GLuint fs = compile(GL_FRAGMENT_SHADER, kFragment);
  staging_ = static_cast<unsigned char*>(malloc(kMaxWidth * kMaxHeight * 4));
  if (!vs || !fs || !staging_) {
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    return false;
  }
  program_ = glCreateProgram();
  glAttachShader(program_, vs);
  glAttachShader(program_, fs);
  glLinkProgram(program_);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(program_, GL_LINK_STATUS, &ok);
  if (!ok) {
    glDeleteProgram(program_);
    program_ = 0;
    return false;
  }
  position_ = glGetAttribLocation(program_, "a_position");
  uv_ = glGetAttribLocation(program_, "a_uv");
  sampler_ = glGetUniformLocation(program_, "u_texture");
  size_ = glGetUniformLocation(program_, "u_size");
  scale_ = glGetUniformLocation(program_, "u_scale");
  swizzle_ = glGetUniformLocation(program_, "u_swizzle");
  sharp_ = glGetUniformLocation(program_, "u_sharp");
  glGenTextures(1, &texture_);
  glGenBuffers(1, &buffer_);
  /* Two triangles over the target rectangle; row 0 of a frame at the top. */
  static const GLfloat kQuad[] = {-1, -1, 0, 1, 1, -1, 1, 1, -1, 1, 0, 0, 1, 1, 1, 0};
  glBindBuffer(GL_ARRAY_BUFFER, buffer_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  return true;
}

void FrameVideo::shutdown() {
  if (texture_) glDeleteTextures(1, &texture_);
  if (buffer_) glDeleteBuffers(1, &buffer_);
  if (program_) glDeleteProgram(program_);
  texture_ = buffer_ = program_ = 0;
  width_ = height_ = texture_w_ = texture_h_ = 0;
  free(staging_);
  staging_ = NULL;
}

bool FrameVideo::upload(const void* pixels, int width, int height, size_t pitch,
                        PixelFormat format) {
  if (!program_ || !pixels || width <= 0 || height <= 0 || width > kMaxWidth ||
      height > kMaxHeight) {
    return false;
  }
  const size_t bpp = format == kXrgb8888 ? 4 : 2;
  const size_t row = static_cast<size_t>(width) * bpp;
  if (pitch < row) return false;
  const unsigned char* src = static_cast<const unsigned char*>(pixels);
  const void* data = pixels;
  if (format == k0rgb1555) { /* to RGB565: G widened to 6 bits */
    uint16_t* out = reinterpret_cast<uint16_t*>(staging_);
    for (int y = 0; y < height; ++y) {
      const uint16_t* in = reinterpret_cast<const uint16_t*>(src + y * pitch);
      for (int x = 0; x < width; ++x) {
        const unsigned v = in[x], g = (v >> 5) & 31u;
        *out++ =
            static_cast<uint16_t>(((v >> 10) & 31u) << 11 | (g << 1 | g >> 4) << 5 | (v & 31u));
      }
    }
    data = staging_;
  } else if (pitch != row) { /* GLES2 has no row length: pack the rows */
    for (int y = 0; y < height; ++y) memcpy(staging_ + y * row, src + y * pitch, row);
    data = staging_;
  }
  const bool wide = format == kXrgb8888;
  glBindTexture(GL_TEXTURE_2D, texture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, wide ? 4 : 2);
  const PixelFormat stored = wide ? kXrgb8888 : kRgb565;
  if (width != texture_w_ || height != texture_h_ || stored != texture_format_) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, wide ? GL_RGBA : GL_RGB, width, height, 0,
                 wide ? GL_RGBA : GL_RGB, wide ? GL_UNSIGNED_BYTE : GL_UNSIGNED_SHORT_5_6_5, data);
    texture_w_ = width;
    texture_h_ = height;
    texture_format_ = stored;
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, wide ? GL_RGBA : GL_RGB,
                    wide ? GL_UNSIGNED_BYTE : GL_UNSIGNED_SHORT_5_6_5, data);
  }
  width_ = width;
  height_ = height;
  return true;
}

void FrameVideo::place(int window_w, int window_h, int w, int h, const FrameOptions& options,
                       int* out_x, int* out_y, int* out_w, int* out_h) {
  int rw = window_w, rh = window_h;
  if (w > 0 && h > 0 && options.aspect != kAspectStretch) {
    const int k = window_w / w < window_h / h ? window_w / w : window_h / h;
    if (options.aspect == kAspectInteger && k >= 1) {
      rw = w * k;
      rh = h * k;
    } else {
      const double aspect =
          options.display_aspect > 0.0f ? options.display_aspect : static_cast<double>(w) / h;
      if (static_cast<double>(window_w) / window_h > aspect) {
        rw = static_cast<int>(window_h * aspect + 0.5);
      } else {
        rh = static_cast<int>(window_w / aspect + 0.5);
      }
    }
  }
  *out_x = (window_w - rw) / 2;
  *out_y = (window_h - rh) / 2;
  *out_w = rw;
  *out_h = rh;
}

void FrameVideo::draw(int window_w, int window_h, const FrameOptions& options, int* x, int* y,
                      int* w, int* h) {
  int rx = 0, ry = 0, rw = 0, rh = 0;
  glViewport(0, 0, window_w, window_h);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_CULL_FACE);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  if (program_ && width_ && height_) {
    place(window_w, window_h, width_, height_, options, &rx, &ry, &rw, &rh);
    glViewport(rx, ry, rw, rh);
    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_);
    const GLint filter = options.filter == kFilterNearest ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glUniform1i(sampler_, 0);
    glUniform2f(size_, static_cast<GLfloat>(width_), static_cast<GLfloat>(height_));
    const GLfloat sx = static_cast<GLfloat>(rw) / width_, sy = static_cast<GLfloat>(rh) / height_;
    glUniform2f(scale_, sx > 1.0f ? sx : 1.0f, sy > 1.0f ? sy : 1.0f);
    glUniform1f(swizzle_, texture_format_ == kXrgb8888 ? 1.0f : 0.0f);
    glUniform1f(sharp_, options.filter == kFilterSharp ? 1.0f : 0.0f);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glEnableVertexAttribArray(static_cast<GLuint>(position_));
    glEnableVertexAttribArray(static_cast<GLuint>(uv_));
    glVertexAttribPointer(static_cast<GLuint>(position_), 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(GLfloat), 0);
    glVertexAttribPointer(static_cast<GLuint>(uv_), 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                          reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(static_cast<GLuint>(position_));
    glDisableVertexAttribArray(static_cast<GLuint>(uv_));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glViewport(0, 0, window_w, window_h);
  }
  if (x) *x = rx;
  if (y) *y = ry;
  if (w) *w = rw;
  if (h) *h = rh;
}

}  // namespace frontend
}  // namespace nbt
