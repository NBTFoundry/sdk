// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_COMPANION_WIRE_H
#define NBT_COMPANION_WIRE_H

/* Phone companions (capability `companion`, bridge 1.9,
 * docs/companion-bridge.md):
 *
 *   application -> phone, the file on NBT_COMPANION_FD, rewritten at offset 0:
 *       <sequence>\n then one widget per line, fields separated by tabs
 *   phone -> application, the file named by NBT_COMPANION_EVENTS, replaced
 *   whole (the last NBT_COMPANION_EVENTS_MAX):
 *       <sequence>\t<id>\t<value>\n
 *
 * Sequences are canonical uint64 decimal. The worker checks a page's frame
 * (its sequence line, size, line count, no control characters but tab and
 * newline); the phone checks each widget. Allocation free. Shared byte for
 * byte with the SDK (its READERS). */

#include <stddef.h>
#include <stdint.h>

#define NBT_COMPANION_PAGE_MAX 4080 /* its reply, with the 16-byte header, fits 4096 */
#define NBT_COMPANION_WIDGETS_MAX 32
#define NBT_COMPANION_ID_MAX 24
#define NBT_COMPANION_TEXT_MAX 120
#define NBT_COMPANION_VALUE_MAX 24
#define NBT_COMPANION_CHOICES_MAX 12
#define NBT_COMPANION_EVENTS_MAX 16
#define NBT_COMPANION_EVENT_LINE_MAX                                                               \
    (20 + 1 + NBT_COMPANION_ID_MAX + 1 + NBT_COMPANION_VALUE_MAX + 1)

#ifdef __cplusplus
extern "C" {
#endif

/* A page's frame: 0 and its sequence, or -1. */
int nbt_companion_page_check(const char *page, size_t length, uint64_t *sequence);
/* 1-24 of a-z 0-9 _ -. */
int nbt_companion_id_valid(const char *id, size_t length);
/* 0-24 bytes without control characters, tabs or commas. */
int nbt_companion_value_valid(const char *value, size_t length);
/* One event line; its length or -1. */
int nbt_companion_event_format(uint64_t sequence, const char *id, const char *value, char *out,
                               size_t size);
/* One event line, '\n' included: 0 or -1. */
int nbt_companion_event_parse(const char *line, size_t length, uint64_t *sequence,
                              char id[NBT_COMPANION_ID_MAX + 1],
                              char value[NBT_COMPANION_VALUE_MAX + 1]);

#ifdef __cplusplus
}
#endif

#endif
