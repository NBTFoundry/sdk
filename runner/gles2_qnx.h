/* SPDX-License-Identifier: MPL-2.0 */
/* Included first in the runners' objects (runner/runner.mk): QNX 6.5 SP1's
 * <GLES2/gl2.h> predates GLchar, which the GL decoder and the fake GL use;
 * later ones (QNX 6.6) declare it. Repeating a typedef with the same type is
 * valid C11 and C++, and GCC 4.8 accepts it in gnu99 too. */
#ifndef NBT_RUNNER_GLES2_QNX_H
#define NBT_RUNNER_GLES2_QNX_H

#include <GLES2/gl2.h>

typedef char GLchar;

#endif
