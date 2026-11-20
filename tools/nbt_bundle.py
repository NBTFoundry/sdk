#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Bundle nbt-runner and an application payload into one executable, for test
packages that run on today's NBT Manager unchanged: the runner finds the
payload appended to itself (include/nbt/app/payload.h), copies it to the
application's data folder and runs it.

    nbt_bundle.py <nbt-runner> <app.so|app.guest> <output>

The payload must carry an NBT ABI note (tools/check_module.py checks the
rest); tools/nbt_payload.py writes the bundle."""
import os
import sys

from nbt_elf import MODE_NAMES, nbt_note
from nbt_payload import append


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    runner, payload, out = sys.argv[1:]
    with open(payload, "rb") as f:
        body = f.read()
    note = nbt_note(body)
    if not note or note[2] not in MODE_NAMES:
        sys.exit("%s has no NBT ABI note" % payload)
    append(runner, body, out)
    print("%s: nbt-runner + %s (%s, ABI %d.%d, %d bytes)" % (
        out, os.path.basename(payload), MODE_NAMES[note[2]], note[0], note[1], len(body)))


if __name__ == "__main__":
    main()
