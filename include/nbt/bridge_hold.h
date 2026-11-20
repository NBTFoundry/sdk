// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_BRIDGE_HOLD_H
#define NBT_BRIDGE_HOLD_H

/* The focus hold (capability `hold`, bridge 1.6) and car data (capability
 * `car-data`): one-line records in /dev/shmem, each rewritten in place with a
 * single write and read whole (docs/app-bridge.md, "Focus hold", "Car data").
 *
 *   application -> its host, the file on NBT_HOLD_FD:
 *       <sequence> hold|peek|release\n
 *   host -> iDrive plugin, /dev/shmem/nbt-bridge1-hmi:
 *       <host's monotonic ms> <request sequence> <app> hold|peek|release\n
 *   iDrive plugin -> application, the file named by NBT_HOLD_STATE:
 *       <sequence> held|aside\n
 *   iDrive plugin -> applications, /dev/shmem/nbt-bridge1-cds-<name>:
 *       <sequence> <value>\n   (the value as the car gives it, printable ASCII)
 *
 * Sequences are canonical uint64 decimal. Parsers are length based, reject
 * embedded NULs, trailing bytes and torn lines; allocation free. Shared
 * byte for byte with the SDK (its READERS). */

#include <stddef.h>
#include <stdint.h>

#define NBT_HOLD_LINE_MAX 80
/* A relay line older than this is a host that stopped refreshing it. */
#define NBT_HOLD_FRESH_MS 3000u
#define NBT_HOLD_APP_MAX 48
#define NBT_CDS_VALUE_MAX 200
#define NBT_CDS_LINE_MAX (20 + 1 + NBT_CDS_VALUE_MAX + 1)

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { NBT_HOLD_HOLD = 1, NBT_HOLD_PEEK = 2, NBT_HOLD_RELEASE = 3 } nbt_hold_word;
typedef enum { NBT_HOLD_HELD = 1, NBT_HOLD_ASIDE = 2 } nbt_hold_state;

int nbt_hold_request_parse(const char *line, size_t length, uint64_t *sequence,
                           nbt_hold_word *word);
/* The line's length, or -1. */
int nbt_hold_request_format(uint64_t sequence, nbt_hold_word word, char *out, size_t size);
int nbt_hold_relay_parse(const char *line, size_t length, uint64_t *ms, uint64_t *sequence,
                         char app[NBT_HOLD_APP_MAX + 1], nbt_hold_word *word);
int nbt_hold_relay_format(uint64_t ms, uint64_t sequence, const char *app, nbt_hold_word word,
                          char *out, size_t size);
int nbt_hold_state_parse(const char *line, size_t length, uint64_t *sequence,
                         nbt_hold_state *state);
int nbt_hold_state_format(uint64_t sequence, nbt_hold_state state, char *out, size_t size);
/* `value` gets the car's text, NUL terminated. */
int nbt_cds_value_parse(const char *line, size_t length, uint64_t *sequence,
                        char value[NBT_CDS_VALUE_MAX + 1]);
int nbt_cds_value_format(uint64_t sequence, const char *value, char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif
