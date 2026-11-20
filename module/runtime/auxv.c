/* SPDX-License-Identifier: MPL-2.0 */
/* CPU feature probes (<sys/auxv.h>, <asm/hwcap.h>): AT_HWCAP and AT_HWCAP2
 * report no optional feature, so code takes its baseline paths (ARMv7-A,
 * VFPv3-D16, no NEON), as payloads are built; any other entry is absent
 * (ENOENT), as on Linux. */
#include <errno.h>
#include <sys/auxv.h>

unsigned long getauxval(unsigned long type) {
  if (type != AT_HWCAP && type != AT_HWCAP2) errno = ENOENT;
  return 0;
}
