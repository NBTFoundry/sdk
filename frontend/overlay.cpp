// SPDX-License-Identifier: MPL-2.0
#include "nbt/frontend/overlay.hpp"

#include <stdlib.h>
#include <string.h>

/* Code points 0-127 in 8x8 cells, rows of 8 bits, the least significant the
 * leftmost pixel (frontend/font8x8.c). */
extern "C" char nbt_font8x8_basic[128][8];

namespace nbt {
namespace frontend {

namespace {

/* Printable ASCII's 95 glyphs and a solid cell, in a 16 x 6 atlas. */
const int kColumns = 16, kRows = 6;
const int kAtlasW = kColumns * Overlay::kGlyph, kAtlasH = kRows * Overlay::kGlyph;
const int kSolid = 95;
const int kFloatsPerVertex = 8; /* x, y, u, v, r, g, b, a */

const char kVertex[] =
    "attribute vec2 a_position;\n"
    "attribute vec2 a_uv;\n"
    "attribute vec4 a_color;\n"
    "uniform vec2 u_window;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_color;\n"
    "void main() {\n"
    "  v_uv = a_uv;\n"
    "  v_color = a_color;\n"
    "  gl_Position = vec4(a_position.x / u_window.x * 2.0 - 1.0,\n"
    "                     1.0 - a_position.y / u_window.y * 2.0, 0.0, 1.0);\n"
    "}\n";

const char kFragment[] =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_color;\n"
    "uniform sampler2D u_font;\n"
    "void main() {\n"
    "  gl_FragColor = vec4(v_color.rgb, v_color.a * texture2D(u_font, v_uv).a);\n"
    "}\n";

GLuint compile(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, NULL);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

}  // namespace

Overlay::Overlay()
    : program_(0),
      texture_(0),
      buffer_(0),
      position_(-1),
      uv_(-1),
      color_(-1),
      sampler_(-1),
      window_(-1),
      vertices_(NULL),
      quads_(0),
      window_w_(0),
      window_h_(0),
      saved_program_(0),
      saved_buffer_(0),
      saved_texture_(0),
      saved_active_(0),
      saved_src_rgb_(0),
      saved_dst_rgb_(0),
      saved_src_alpha_(0),
      saved_dst_alpha_(0),
      saved_blend_(GL_FALSE),
      saved_depth_(GL_FALSE),
      saved_scissor_(GL_FALSE),
      saved_cull_(GL_FALSE),
      begun_(false) {
  memset(saved_viewport_, 0, sizeof(saved_viewport_));
  memset(saved_enabled_, 0, sizeof(saved_enabled_));
}

Overlay::~Overlay() {
  free(vertices_);
}

bool Overlay::init() {
  vertices_ = static_cast<float*>(malloc(kMaxQuads * 6 * kFloatsPerVertex * sizeof(float)));
  unsigned char* atlas = static_cast<unsigned char*>(calloc(kAtlasW * kAtlasH, 1));
  const GLuint vs = compile(GL_VERTEX_SHADER, kVertex), fs = compile(GL_FRAGMENT_SHADER, kFragment);
  if (!vertices_ || !atlas || !vs || !fs) {
    free(atlas);
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    return false;
  }
  for (int cell = 0; cell < 96; ++cell) {
    const int cx = (cell % kColumns) * kGlyph, cy = (cell / kColumns) * kGlyph;
    for (int row = 0; row < kGlyph; ++row) {
      for (int col = 0; col < kGlyph; ++col) {
        const unsigned bits = cell == kSolid
                                  ? 0xffu
                                  : static_cast<unsigned char>(nbt_font8x8_basic[0x20 + cell][row]);
        if ((bits >> col) & 1u) atlas[(cy + row) * kAtlasW + cx + col] = 255;
      }
    }
  }
  program_ = glCreateProgram();
  glAttachShader(program_, vs);
  glAttachShader(program_, fs);
  /* Locations 0-2, the ones end() restores. */
  glBindAttribLocation(program_, 0, "a_position");
  glBindAttribLocation(program_, 1, "a_uv");
  glBindAttribLocation(program_, 2, "a_color");
  glLinkProgram(program_);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(program_, GL_LINK_STATUS, &ok);
  if (!ok) {
    free(atlas);
    glDeleteProgram(program_);
    program_ = 0;
    return false;
  }
  position_ = 0;
  uv_ = 1;
  color_ = 2;
  sampler_ = glGetUniformLocation(program_, "u_font");
  window_ = glGetUniformLocation(program_, "u_window");
  GLint previous = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
  glGenTextures(1, &texture_);
  glBindTexture(GL_TEXTURE_2D, texture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, kAtlasW, kAtlasH, 0, GL_ALPHA, GL_UNSIGNED_BYTE, atlas);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
  free(atlas);
  glGenBuffers(1, &buffer_);
  return true;
}

void Overlay::shutdown() {
  if (texture_) glDeleteTextures(1, &texture_);
  if (buffer_) glDeleteBuffers(1, &buffer_);
  if (program_) glDeleteProgram(program_);
  texture_ = buffer_ = program_ = 0;
  free(vertices_);
  vertices_ = NULL;
}

int Overlay::text_width(const char* text, int scale) {
  return text ? static_cast<int>(strlen(text)) * kGlyph * scale : 0;
}

int Overlay::scale_for(int window_h) {
  const int scale = window_h / (30 * kGlyph);
  return scale < 1 ? 1 : scale;
}

void Overlay::begin(int window_w, int window_h) {
  if (!program_ || begun_) return;
  begun_ = true;
  window_w_ = window_w;
  window_h_ = window_h;
  quads_ = 0;
  glGetIntegerv(GL_CURRENT_PROGRAM, &saved_program_);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &saved_buffer_);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &saved_active_);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved_texture_);
  glGetIntegerv(GL_VIEWPORT, saved_viewport_);
  glGetIntegerv(GL_BLEND_SRC_RGB, &saved_src_rgb_);
  glGetIntegerv(GL_BLEND_DST_RGB, &saved_dst_rgb_);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &saved_src_alpha_);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &saved_dst_alpha_);
  saved_blend_ = glIsEnabled(GL_BLEND);
  saved_depth_ = glIsEnabled(GL_DEPTH_TEST);
  saved_scissor_ = glIsEnabled(GL_SCISSOR_TEST);
  saved_cull_ = glIsEnabled(GL_CULL_FACE);
  for (GLuint i = 0; i < 3; ++i) {
    glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &saved_enabled_[i]);
  }
  glViewport(0, 0, window_w, window_h);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_CULL_FACE);
}

void Overlay::quad(float x0, float y0, float x1, float y1, int glyph, uint32_t rgba) {
  if (!begun_) return;
  if (quads_ == kMaxQuads) flush();
  const float u0 = static_cast<float>((glyph % kColumns) * kGlyph) / kAtlasW;
  const float v0 = static_cast<float>((glyph / kColumns) * kGlyph) / kAtlasH;
  const float u1 = u0 + static_cast<float>(kGlyph) / kAtlasW;
  const float v1 = v0 + static_cast<float>(kGlyph) / kAtlasH;
  const float r = ((rgba >> 24) & 255u) / 255.0f, g = ((rgba >> 16) & 255u) / 255.0f;
  const float b = ((rgba >> 8) & 255u) / 255.0f, a = (rgba & 255u) / 255.0f;
  const float corners[6][4] = {{x0, y0, u0, v0}, {x1, y0, u1, v0}, {x0, y1, u0, v1},
                               {x1, y0, u1, v0}, {x1, y1, u1, v1}, {x0, y1, u0, v1}};
  float* out = vertices_ + quads_ * 6 * kFloatsPerVertex;
  for (int i = 0; i < 6; ++i) {
    const float vertex[kFloatsPerVertex] = {
        corners[i][0], corners[i][1], corners[i][2], corners[i][3], r, g, b, a};
    memcpy(out + i * kFloatsPerVertex, vertex, sizeof(vertex));
  }
  ++quads_;
}

void Overlay::box(int x, int y, int w, int h, uint32_t rgba) {
  if (w > 0 && h > 0) {
    quad(static_cast<float>(x), static_cast<float>(y), static_cast<float>(x + w),
         static_cast<float>(y + h), kSolid, rgba);
  }
}

void Overlay::text(int x, int y, int scale, const char* text, uint32_t rgba) {
  if (!text || scale < 1) return;
  const int step = kGlyph * scale;
  for (int i = 0; text[i]; ++i, x += step) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c <= 0x20 || c > 0x7e) continue; /* a space, or nothing to draw */
    quad(static_cast<float>(x), static_cast<float>(y), static_cast<float>(x + step),
         static_cast<float>(y + step), c - 0x20, rgba);
  }
}

void Overlay::flush() {
  if (!quads_) return;
  glUseProgram(program_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture_);
  glUniform1i(sampler_, 0);
  glUniform2f(window_, static_cast<GLfloat>(window_w_), static_cast<GLfloat>(window_h_));
  glBindBuffer(GL_ARRAY_BUFFER, buffer_);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(quads_ * 6 * kFloatsPerVertex * sizeof(float)), vertices_,
               GL_STREAM_DRAW);
  const GLsizei stride = kFloatsPerVertex * sizeof(float);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, 0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<const void*>(2 * sizeof(float)));
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<const void*>(4 * sizeof(float)));
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(quads_ * 6));
  quads_ = 0;
}

void Overlay::end() {
  if (!begun_) return;
  flush();
  begun_ = false;
  for (GLuint i = 0; i < 3; ++i) {
    if (saved_enabled_[i]) {
      glEnableVertexAttribArray(i);
    } else {
      glDisableVertexAttribArray(i);
    }
  }
  glUseProgram(static_cast<GLuint>(saved_program_));
  glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(saved_buffer_));
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(saved_texture_));
  glActiveTexture(static_cast<GLenum>(saved_active_));
  glViewport(saved_viewport_[0], saved_viewport_[1], saved_viewport_[2], saved_viewport_[3]);
  glBlendFuncSeparate(static_cast<GLenum>(saved_src_rgb_), static_cast<GLenum>(saved_dst_rgb_),
                      static_cast<GLenum>(saved_src_alpha_), static_cast<GLenum>(saved_dst_alpha_));
  if (saved_blend_)
    glEnable(GL_BLEND);
  else
    glDisable(GL_BLEND);
  if (saved_depth_)
    glEnable(GL_DEPTH_TEST);
  else
    glDisable(GL_DEPTH_TEST);
  if (saved_scissor_)
    glEnable(GL_SCISSOR_TEST);
  else
    glDisable(GL_SCISSOR_TEST);
  if (saved_cull_)
    glEnable(GL_CULL_FACE);
  else
    glDisable(GL_CULL_FACE);
}

}  // namespace frontend
}  // namespace nbt
