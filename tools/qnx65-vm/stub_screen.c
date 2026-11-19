/* SPDX-License-Identifier: MPL-2.0 */
/* libscreen.so.1 for the QNX 6.5 VM, which has no Screen: every call fails
 * with ENOSYS, so programs linked with Screen start and report "no video". */
#include <errno.h>

#define STUB(name)   \
  int name() {       \
    errno = ENOSYS;  \
    return -1;       \
  }

STUB(screen_create_context)
STUB(screen_destroy_context)
STUB(screen_create_window)
STUB(screen_create_window_type)
STUB(screen_create_window_buffers)
STUB(screen_destroy_window)
STUB(screen_post_window)
STUB(screen_get_context_property_iv)
STUB(screen_get_context_property_pv)
STUB(screen_get_display_property_iv)
STUB(screen_get_window_property_iv)
STUB(screen_get_window_property_pv)
STUB(screen_get_buffer_property_iv)
STUB(screen_get_buffer_property_pv)
STUB(screen_set_window_property_cv)
STUB(screen_set_window_property_iv)
STUB(screen_set_window_property_pv)
