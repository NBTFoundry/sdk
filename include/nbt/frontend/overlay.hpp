// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_FRONTEND_OVERLAY_HPP
#define NBT_FRONTEND_OVERLAY_HPP

#include <GLES2/gl2.h>
#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace frontend {

/* Text (printable ASCII, an 8x8 bitmap font scaled up) and filled boxes
 * drawn over whatever the window holds: an emulator's frame or an
 * application's own OpenGL ES 2 renderer. Its program, texture and buffer are
 * its own; begin() saves the state it changes and end() restores it (program,
 * array buffer, texture 0, blending, depth, scissor and culling, viewport, and
 * whether attributes 0-2 are enabled; their pointers are left as it set them).
 * Colours are 0xRRGGBBAA. */
class Overlay {
 public:
  enum { kGlyph = 8, kMaxQuads = 2048 };

  Overlay();
  ~Overlay();
  bool init();
  void shutdown();
  bool ready() const { return program_ != 0; }

  void begin(int window_w, int window_h);
  void box(int x, int y, int w, int h, uint32_t rgba);
  /* At (x, y) the top left corner; `scale` pixels per font pixel. */
  void text(int x, int y, int scale, const char* text, uint32_t rgba);
  void end();

  static int text_width(const char* text, int scale);
  /* A scale for text on a window this high: about 30 lines of text. */
  static int scale_for(int window_h);

 private:
  Overlay(const Overlay&);
  Overlay& operator=(const Overlay&);

  void quad(float x0, float y0, float x1, float y1, int glyph, uint32_t rgba);
  void flush();

  GLuint program_, texture_, buffer_;
  GLint position_, uv_, color_, sampler_, window_;
  float* vertices_;
  size_t quads_;
  int window_w_, window_h_;
  /* The state begin() changed, for end(). */
  GLint saved_program_, saved_buffer_, saved_texture_, saved_active_, saved_viewport_[4];
  GLint saved_src_rgb_, saved_dst_rgb_, saved_src_alpha_, saved_dst_alpha_;
  GLboolean saved_blend_, saved_depth_, saved_scissor_, saved_cull_;
  GLint saved_enabled_[3];
  bool begun_;
};

}  // namespace frontend
}  // namespace nbt

#endif /* NBT_FRONTEND_OVERLAY_HPP */
