/* SPDX-License-Identifier: MPL-2.0 */
/* The probes' checks: one line each on stdout, "<name> ok|FAIL [detail]",
 * counted for the summary. Probes also call the runner's table directly. */
#ifndef NBT_TESTS_MODULE_CHECK_H
#define NBT_TESTS_MODULE_CHECK_H

#include "nbt/host_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const nbt_host_v1* nbt_host __attribute__((visibility("hidden"))); /* module runtime */

void check(const char* name, int ok);
void check_detail(const char* name, int ok, const char* detail);
/* "probe N/M ok", for a status label. */
const char* check_status(void);
/* Prints "probe: N/M ok"; 0 when every check passed, else 1. */
int check_summary(void);

#ifdef __cplusplus
}
#endif

#endif
