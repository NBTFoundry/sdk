/* SPDX-License-Identifier: MPL-2.0 */
/* Application payloads for nbt-runner: the NBT ABI note of a module or guest
 * (read with bounded parsing: the file is untrusted), and a payload appended
 * to a runner executable (test packages: one signed file). */
#ifndef NBT_RUNNER_PAYLOAD_H
#define NBT_RUNNER_PAYLOAD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { NBT_PAYLOAD_MODULE = 1, NBT_PAYLOAD_GUEST = 2 };

typedef struct nbt_payload_info {
  uint32_t abi_major;
  uint32_t abi_minor;
  uint32_t mode; /* NBT_PAYLOAD_* (the note's mode) */
} nbt_payload_info;

/* Reads the .note.nbt of the ELF file at `path` and checks it against the ELF
 * type (a module is ET_DYN, a guest ET_EXEC). 0, or -1 with `why` set. */
int nbt_payload_read(const char* path, nbt_payload_info* out, const char** why);

/* Same, on an ELF image in memory. */
int nbt_payload_parse(const uint8_t* data, size_t size, nbt_payload_info* out, const char** why);

/* Appended payload: <runner ELF><payload bytes><"NBTPAYLD"><uint64 LE size>. */
#define NBT_PAYLOAD_TRAILER_MAGIC "NBTPAYLD"
#define NBT_PAYLOAD_TRAILER_SIZE 16

/* When `exe` carries an appended payload, copies it to `out_path` (mode 0700,
 * replacing an older copy) and returns 1; 0 when it carries none; -1 on an
 * error, with `why` set. */
int nbt_payload_extract(const char* exe, const char* out_path, const char** why);

#ifdef __cplusplus
}
#endif

#endif
