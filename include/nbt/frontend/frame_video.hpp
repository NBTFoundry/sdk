// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_FRONTEND_FRAME_VIDEO_HPP
#define NBT_FRONTEND_FRAME_VIDEO_HPP

#include <GLES2/gl2.h>
#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace frontend {

enum PixelFormat { kRgb565, kXrgb8888, k0rgb1555 };

/* How a frame fills the window: at its display aspect (4:3 for most
 * consoles), at an integer scale with square pixels, or stretched. */
enum Aspect { kAspectDisplay, kAspectInteger, kAspectStretch };
/* Sharp: crisp pixels with smoothed edges at any scale; smooth: bilinear;
 * nearest: plain point sampling. */
enum Filter { kFilterSharp, kFilterSmooth, kFilterNearest };

struct FrameOptions {
  Aspect aspect;
  Filter filter;
  float display_aspect; /* width / height when shown; 0: the frame's own */
};

/* A software-rendered frame (an emulator's) drawn on the current OpenGL ES 2
 * context: uploaded as a texture, then scaled into the window on black. */
class FrameVideo {
 public:
  enum { kMaxWidth = 1024, kMaxHeight = 1024 };

  FrameVideo();
  ~FrameVideo();
  /* With the context current. */
  bool init();
  void shutdown();
  bool ready() const { return program_ != 0; }

  /* A frame of `width` x `height` pixels, `pitch` bytes per row; false when
   * it is larger than kMaxWidth x kMaxHeight. */
  bool upload(const void* pixels, int width, int height, size_t pitch, PixelFormat format);
  /* Clears the window and draws the last frame; the rectangle it took. */
  void draw(int window_w, int window_h, const FrameOptions& options, int* x = 0, int* y = 0,
            int* w = 0, int* h = 0);
  int width() const { return width_; }
  int height() const { return height_; }

  /* Where a frame of w x h goes in a window of window_w x window_h. */
  static void place(int window_w, int window_h, int w, int h, const FrameOptions& options,
                    int* out_x, int* out_y, int* out_w, int* out_h);

 private:
  FrameVideo(const FrameVideo&);
  FrameVideo& operator=(const FrameVideo&);

  GLuint program_, texture_, buffer_;
  GLint position_, uv_, sampler_, size_, scale_, swizzle_, sharp_;
  int width_, height_, texture_w_, texture_h_;
  PixelFormat texture_format_;
  unsigned char* staging_; /* rows packed tightly for upload */
};

}  // namespace frontend
}  // namespace nbt

#endif /* NBT_FRONTEND_FRAME_VIDEO_HPP */
