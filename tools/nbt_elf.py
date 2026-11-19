# SPDX-License-Identifier: MPL-2.0
"""The NBT ABI note of an application payload (module/runtime/crt.c writes it,
runner/payload.c reads it), for the Python tools. The image is untrusted:
every read is bounded, like the runner's."""
import struct

NOTE_NAME = b"NBT\0"
NOTE_TYPE = 1
MODE_MODULE = 1  # ET_DYN, loaded into the runner
MODE_GUEST = 2  # ET_EXEC, run as the runner's child
MODE_NAMES = {MODE_MODULE: "module", MODE_GUEST: "guest"}


def is_arm32(data):
    """Whether `data` starts an ELF32 little-endian ARM image (the head unit's)."""
    return (len(data) >= 52 and data[:4] == b"\x7fELF" and data[4] == 1 and data[5] == 1 and
            struct.unpack_from("<H", data, 18)[0] == 40)


def nbt_note(data):
    """(abi_major, abi_minor, mode) from the PT_NOTE segments of an ELF32
    little-endian ARM image, or None."""
    if not is_arm32(data):
        return None
    phoff, = struct.unpack_from("<I", data, 28)
    phentsize, phnum = struct.unpack_from("<HH", data, 42)
    if phentsize < 32 or phnum > 64 or phoff + phnum * phentsize > len(data):
        return None
    for i in range(phnum):
        p_type, p_offset, _, _, p_filesz = struct.unpack_from("<IIIII", data, phoff + i * phentsize)
        end = p_offset + p_filesz
        if p_type != 4 or end > len(data):
            continue
        pos = p_offset
        while pos + 12 <= end:
            namesz, descsz, ntype = struct.unpack_from("<III", data, pos)
            desc = pos + 12 + ((namesz + 3) & ~3)
            following = desc + ((descsz + 3) & ~3)
            if following > end:
                break
            if data[pos + 12:pos + 12 + namesz] == NOTE_NAME and ntype == NOTE_TYPE and descsz >= 12:
                return struct.unpack_from("<III", data, desc)
            pos = following
    return None


def read_nbt_note(path):
    with open(path, "rb") as f:
        return nbt_note(f.read())
