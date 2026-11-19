// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_APP_SRC_VIDEO_QNX_HPP
#define NBT_APP_SRC_VIDEO_QNX_HPP

#include "nbt/app/video.hpp"

namespace nbt {
namespace app {

#ifdef __QNXNTO__
/* Opens the panel window and an ES2 context made current. */
bool qnx_video_open(int surface_id, VideoNative* native, EGLDisplay* dpy, EGLSurface* surf,
                    EGLContext* ctx, int* win_w, int* win_h);
void qnx_video_close(VideoNative* native, EGLDisplay* dpy, EGLSurface* surf, EGLContext* ctx);
#endif

}  // namespace app
}  // namespace nbt

#endif /* NBT_APP_SRC_VIDEO_QNX_HPP */
