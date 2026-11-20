#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Append a payload to an executable, so that one signed executable carries a
second program (include/nbt/app/payload.h): nbt-runner's module or guest
(tools/nbt_bundle.py), or an application's companion program, which the
application copies out with nbt::app::extract_own_payload() and starts with
nbt::app::spawn_child() (include/nbt/app/child.hpp).

    nbt_payload.py <executable> <payload> <output>

Layout: <executable><payload><"NBTPAYLD"><payload size, uint64 little-endian>.
"""
import os
import struct
import sys

TRAILER_MAGIC = b"NBTPAYLD"  # NBT_PAYLOAD_TRAILER_MAGIC
MAX_PAYLOAD = 64 << 20  # src/payload.c


def append(executable, body, out):
    """Writes <executable><body><trailer> to `out` (mode 0755)."""
    with open(executable, "rb") as f:
        head = f.read()
    if head[:4] != b"\x7fELF":
        sys.exit("%s is not an ELF executable" % executable)
    if len(head) >= 16 and head[-16:-8] == TRAILER_MAGIC:
        sys.exit("%s already carries a payload" % executable)
    if not body or len(body) > MAX_PAYLOAD:
        sys.exit("payload size %d is outside 1..%d" % (len(body), MAX_PAYLOAD))
    tmp = out + ".tmp"
    with open(tmp, "wb") as f:
        f.write(head)
        f.write(body)
        f.write(TRAILER_MAGIC + struct.pack("<Q", len(body)))
    os.chmod(tmp, 0o755)
    os.replace(tmp, out)


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    executable, payload, out = sys.argv[1:]
    with open(payload, "rb") as f:
        body = f.read()
    append(executable, body, out)
    print("%s: %s + %s (%d bytes)" % (out, os.path.basename(executable), os.path.basename(payload),
                                      len(body)))


if __name__ == "__main__":
    main()
