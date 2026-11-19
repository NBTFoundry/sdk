#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Check that an app payload has the shape the runner and QNX 6.5 need
(include/nbt/host_abi.h). Both kinds:

  - ELF32 ARM, EABI5, soft-float base calling convention, int-size enums,
    4-byte wchar_t (in every input, with --map); no program interpreter, no
    TLS segment, no executable stack; no VFP/NEON code in Thumb state; an NBT
    ABI note.

A module (mode A, ET_DYN, loaded by the runner):
  - no DT_NEEDED, no text relocations, no loader-run constructors;
  - only R_ARM_RELATIVE relocations and no undefined dynamic symbols;
  - exactly one export, nbt_module_v1; note mode 1.

A guest (mode B, ET_EXEC, a separate process; may carry GPL code):
  - static: no dynamic section, no relocations, no undefined symbols;
  - loads at 0x100000 or above, page-aligned segments;
  - no QNX library code (none of its symbols); note mode 2.

With --map (the payload's LLD link map), also the licence gate: GPL/LGPL
inputs only in a guest.

Usage: check_module.py [--readelf PATH] [--objdump PATH] [--map PATH --app-license SPDX] payload...
"""
import argparse
import re
import subprocess
import sys

from nbt_elf import MODE_GUEST, MODE_MODULE, MODE_NAMES, read_nbt_note


def readelf(tool, *args):
    out = subprocess.run([tool, "-W", *args], check=True, capture_output=True, text=True)
    return out.stdout


def thumb_vfp_instructions(objdump, path):
    """Addresses (with their function) of VFP/NEON instructions in Thumb code.

    llvm-objdump follows the $a/$t mapping symbols; a Thumb instruction is
    printed as one or two 4-digit halfwords, an ARM one as 8 digits."""
    out = subprocess.run([objdump, "-d", path], check=True, capture_output=True, text=True)
    found, function = [], "?"
    line_re = re.compile(r"^\s*([0-9a-f]+):\s+([0-9a-f]{4}(?: [0-9a-f]{4})?)\s+(v[a-z0-9.]+)\b")
    for line in out.stdout.splitlines():
        head = re.match(r"^[0-9a-f]+ <([^>]+)>:", line)
        if head:
            function = head.group(1)
            continue
        m = line_re.match(line)
        if m:
            found.append("0x%s in %s (%s)" % (m.group(1), function, m.group(3)))
    return found


# Symbols that only come from QNX's C library or start-up code.
QNX_SYMBOLS = {"_init_libc", "__get_errno_ptr", "_cpupage_ptr", "_syspage_ptr", "__exit", "__ker_err",
               "MsgSendv", "MsgSend", "ThreadCreate", "_Stderr", "_Stdout", "_Stoul", "__SysCpupageGet"}


# ARM build attribute values (llvm-readelf -A) that break the payload ABI.
BAD_ATTRIBUTES = {
    ("ABI_VFP_args", "1"): "passes floats in VFP registers; build with -mfloat-abi=softfp",
    ("ABI_VFP_args", "2"): "uses a toolchain-specific float calling convention",
    ("ABI_enum_size", "1"): "has short enums; build with -fno-short-enums",
    ("ABI_PCS_wchar_t", "2"): "has a 2-byte wchar_t",
}


def attribute_errors(tool, paths):
    """Objects and archive members among paths built for another ABI. LLD
    keeps only one input's attributes in its output, so a payload's inputs
    are read, not the payload."""
    parts = re.split(r"^File: (.+)$", readelf(tool, "-A", *paths), flags=re.M)
    named = zip(parts[1::2], parts[2::2]) if len(parts) > 1 else [(paths[0], parts[0])]
    errors = []
    for name, body in named:
        for value, tag in re.findall(r"Value: (\S+)\s+TagName: (\w+)", body):
            problem = BAD_ATTRIBUTES.get((tag, value))
            if problem:
                errors.append("%s %s" % (name.strip(), problem))
    return errors


def check(tool, path, objdump=None, inputs=None):
    """Errors in the payload at path; inputs (from its link map) are what it
    was linked from."""
    errors = []

    header = readelf(tool, "-h", path)
    if not re.search(r"Class:\s+ELF32", header):
        errors.append("not ELF32")
    if not re.search(r"Machine:\s+ARM", header):
        errors.append("not ARM")
    guest = bool(re.search(r"Type:\s+EXEC", header))
    if not guest and not re.search(r"Type:\s+DYN", header):
        errors.append("neither a module (ET_DYN) nor a guest (ET_EXEC)")
    flags = re.search(r"Flags:\s+0x([0-9a-f]+)", header)
    value = int(flags.group(1), 16) if flags else 0
    if value >> 24 != 5:
        errors.append("not EABI version 5 (flags 0x%x)" % value)
    if value & 0x400:
        errors.append("hard-float ABI flag set")

    errors += attribute_errors(tool, inputs or [path])

    segments = readelf(tool, "-l", path)
    for kind in ("INTERP", "TLS"):
        if re.search(r"^\s+%s\s" % kind, segments, re.M):
            errors.append("has a PT_%s segment" % kind)
    stack = re.search(r"^\s+GNU_STACK\s.*\s(R?W?E?)\s+0x", segments, re.M)
    if not stack:
        errors.append("no PT_GNU_STACK (stack would be executable)")
    elif "E" in stack.group(1):
        errors.append("executable stack")

    if guest:
        errors += check_guest(tool, path, segments)
    else:
        errors += check_module(tool, path)

    # QNX 6.5's lazy FPU switch mishandles a thread's first VFP/NEON
    # instruction when it is in Thumb state (SIGILL on 6.5.0, a hang on SP1 in
    # QEMU). Thumb integer code is fine.
    if objdump:
        thumb_vfp = thumb_vfp_instructions(objdump, path)
        if thumb_vfp:
            errors.append("VFP/NEON instructions in Thumb state (build with -marm): " + ", ".join(thumb_vfp[:5]))

    note = read_nbt_note(path)
    want = MODE_GUEST if guest else MODE_MODULE
    if not note:
        errors.append("no NBT ABI note in a PT_NOTE segment")
    elif note[2] != want:
        errors.append("ABI note says mode %d, expected %d" % (note[2], want))

    return errors, MODE_NAMES[want], note


def symbols(tool, path, option):
    """(name, section index, binding) of each symbol readelf lists."""
    for line in readelf(tool, option, path).splitlines():
        cols = line.split()
        if len(cols) >= 8 and cols[0].rstrip(":").isdigit():
            yield cols[7], cols[6], cols[4]


def check_guest(tool, path, segments):
    errors = []
    if re.search(r"^\s+DYNAMIC\s", segments, re.M):
        errors.append("has a dynamic section (a guest must be static)")
    for m in re.finditer(r"^\s+LOAD\s+0x[0-9a-f]+ 0x([0-9a-f]+) 0x[0-9a-f]+ 0x[0-9a-f]+ 0x[0-9a-f]+ \S+\s+0x([0-9a-f]+)",
                         segments, re.M):
        vaddr, align = int(m.group(1), 16), int(m.group(2), 16)
        if vaddr < 0x100000:
            errors.append("segment at 0x%x, below 0x100000" % vaddr)
        if align < 0x1000:
            errors.append("segment alignment 0x%x, below a page" % align)
    if re.search(r"\bR_ARM_\w+", readelf(tool, "-r", path)):
        errors.append("has relocations (a guest must be fully linked)")
    undefined, qnx = [], []
    for name, ndx, bind in symbols(tool, path, "-s"):
        if ndx == "UND" and name and bind != "WEAK":
            undefined.append(name)
        if name in QNX_SYMBOLS:
            qnx.append(name)
    if undefined:
        errors.append("undefined symbols: " + ", ".join(sorted(undefined)))
    if qnx:
        errors.append("QNX library symbols present: " + ", ".join(sorted(set(qnx))))
    return errors


def check_module(tool, path):
    errors = []
    dynamic = readelf(tool, "-d", path)
    for tag in ("NEEDED", "TEXTREL", "INIT", "FINI", "INIT_ARRAY", "FINI_ARRAY", "PREINIT_ARRAY"):
        if re.search(r"\(%s\)" % tag, dynamic):
            errors.append("dynamic section has DT_%s" % tag)
    if not re.search(r"\(HASH\)", dynamic):
        errors.append("no DT_HASH (the 6.5 loader may need it)")

    relocs = readelf(tool, "-r", path)
    kinds = sorted(set(re.findall(r"\b(R_ARM_\w+)", relocs)))
    others = [k for k in kinds if k != "R_ARM_RELATIVE"]
    if others:
        errors.append("relocations other than R_ARM_RELATIVE: " + ", ".join(others))

    defined, undefined = [], []
    for name, ndx, bind in symbols(tool, path, "--dyn-syms"):
        if ndx == "UND":
            undefined.append(name)
        elif bind in ("GLOBAL", "WEAK"):
            defined.append(name)
    if undefined:
        errors.append("undefined dynamic symbols: " + ", ".join(sorted(undefined)))
    if defined != ["nbt_module_v1"]:
        errors.append("exports must be exactly nbt_module_v1, found: " + ", ".join(sorted(defined)))
    return errors


# Licences of what the SDK puts into every payload (by archive name).
KNOWN_INPUTS = [
    (re.compile(r"libnbt-[a-z0-9-]+\.a$"), "MPL-2.0"),
    (re.compile(r"/picolibc/lib/libc\.a$"), "BSD-3-Clause"),
    (re.compile(r"lib(c\+\+|c\+\+abi|unwind|clang_rt\.builtins)\.a$"), "Apache-2.0 WITH LLVM-exception"),
    (re.compile(r"/obj/guest/[a-z]+\.o$"), "MPL-2.0"),
]
COPYLEFT = re.compile(r"\b(A?GPL|LGPL)-")


def linked_inputs(map_path):
    """Archives and objects the linker took, from an LLD map: input lines end
    in "path:(section)" or "archive(member):(section)"."""
    inputs = set()
    with open(map_path, errors="replace") as f:
        for line in f:
            m = re.search(r"\s(\S+?\.(?:a|o|obj))(?:\([^()]*\))?:\(", line)
            if m:
                inputs.add(m.group(1))
    return sorted(inputs)


def check_licenses(path, inputs, app_license):
    """The QNX licence forbids linking QNX code with GPL code, and statically
    with LGPL code: a module (in the runner's process) must have none; a guest
    (its own process, no QNX code) may."""
    guest = path.endswith(".guest")
    errors = []
    for inp in inputs:
        lic = next((l for pattern, l in KNOWN_INPUTS if pattern.search(inp)), app_license)
        if lic is None:
            errors.append("no licence known for %s (pass --app-license)" % inp)
        elif COPYLEFT.search(lic) and not guest:
            errors.append("%s is %s: GPL/LGPL code must be built as a guest, not a module" % (inp, lic))
        if re.search(r"/qnx6/|/target/qnx|libc\.so\.3|ldqnx", inp):
            errors.append("%s is QNX code" % inp)
    return errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--readelf", default="readelf")
    parser.add_argument("--objdump", help="llvm-objdump, for the Thumb VFP check")
    parser.add_argument("--map", help="the link map (ld.lld -Map): the inputs' licences and ABI")
    parser.add_argument("--app-license", help="SPDX expression of the application's own objects")
    parser.add_argument("modules", nargs="+")
    args = parser.parse_args()
    status = 0
    inputs = linked_inputs(args.map) if args.map else None
    if args.map and not inputs:
        sys.exit("%s: no inputs found (not an LLD map?)" % args.map)
    for path in args.modules:
        errors, kind, note = check(args.readelf, path, args.objdump, inputs)
        if inputs:
            errors += check_licenses(path, inputs, args.app_license)
        if errors:
            status = 1
            for e in errors:
                print("%s: %s" % (path, e))
        else:
            print("OK: %s is a valid NBT %s (ABI %d.%d)" % (path, kind, note[0], note[1]))
    return status


if __name__ == "__main__":
    sys.exit(main())
