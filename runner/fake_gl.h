/* SPDX-License-Identifier: MPL-2.0 */
#ifndef NBT_RUNNER_FAKE_GL_H
#define NBT_RUNNER_FAKE_GL_H

#include "nbt/host_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Replaces the video and GL members of `host` with a recording fake. */
void runner_fake_gl(nbt_host_v1* host);
/* An audio device that pulls 512-frame fragments at 48 kHz and counts. */
void runner_fake_audio(nbt_host_v1* host);

#ifdef __cplusplus
}
#endif

#endif
