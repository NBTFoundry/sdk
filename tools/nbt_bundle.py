#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Bundle nbt-runner and an application payload into one executable, for test
packages that run on today's NBT Manager unchanged: the runner finds the
payload appended to itself (runner/payload.h), copies it to the application's
data folder and runs it.

    nbt_bundle.py <nbt-runner> <app.so|app.guest> <output>

Layout: <runner><payload><"NBTPAYLD"><payload size, uint64 little-endian>.
The payload must carry an NBT ABI note (tools/check_module.py checks the
rest)."""
import os
import struct
import sys

from nbt_elf import MODE_NAMES, nbt_note

TRAILER_MAGIC = b"NBTPAYLD"  # runner/payload.h


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    runner, payload, out = sys.argv[1:]
    with open(runner, "rb") as f:
        head = f.read()
    with open(payload, "rb") as f:
        body = f.read()
    if head[:4] != b"\x7fELF":
        sys.exit("%s is not an ELF executable" % runner)
    if len(head) >= 16 and head[-16:-8] == TRAILER_MAGIC:
        sys.exit("%s already carries a payload" % runner)
    note = nbt_note(body)
    if not note or note[2] not in MODE_NAMES:
        sys.exit("%s has no NBT ABI note" % payload)
    tmp = out + ".tmp"
    with open(tmp, "wb") as f:
        f.write(head)
        f.write(body)
        f.write(TRAILER_MAGIC + struct.pack("<Q", len(body)))
    os.chmod(tmp, 0o755)
    os.replace(tmp, out)
    print("%s: nbt-runner + %s (%s, ABI %d.%d, %d bytes)" % (
        out, os.path.basename(payload), MODE_NAMES[note[2]], note[0], note[1], len(body)))


if __name__ == "__main__":
    main()
