#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Check that a QNX ARM binary only imports symbols the head unit's libraries
export.

Apps are built with the QNX 6.6 SDP, but the car runs QNX 6.5.0 SP1: a symbol
that only 6.6 libraries export (such as _Sinx, which the 6.6 C++ <math.h>
calls for sin and cos) links fine and fails in the car the first time it is
called. This resolves every non-weak undefined dynamic symbol against the
binary's DT_NEEDED libraries, and theirs, in a 6.5.0 SP1 sysroot.

Libraries the sysroot doesn't have (the car's own libscreen) are named with
--car-lib NAME:PREFIX[,PREFIX]: imports with those prefixes are assumed to
come from it. libscreen.so.1:screen_ is always known.

Usage: check_qnx_imports.py --sysroot DIR [--readelf PATH] [--car-lib NAME:PREFIX]... binary...
"""
import argparse
import os
import re
import subprocess
import sys

from nbt_elf import is_arm32

LIB_DIRS = ("armle-v7/lib", "armle-v7/usr/lib", "armle-v7/lib/dll")


class Elf:
    def __init__(self, readelf):
        self.readelf = readelf
        self.cache = {}

    def run(self, *args):
        return subprocess.run([self.readelf, "-W", *args], check=True, capture_output=True, text=True).stdout

    def needed(self, path):
        return re.findall(r"\(NEEDED\)\s+Shared library: \[([^\]]+)\]", self.run("-d", path))

    def symbols(self, path):
        """(defined names, [(undefined name, binding)])"""
        if path in self.cache:
            return self.cache[path]
        defined, undefined = set(), []
        for line in self.run("--dyn-syms", path).splitlines():
            cols = line.split()
            if len(cols) < 8 or not cols[0].rstrip(":").isdigit():
                continue
            name = cols[7].split("@")[0]
            if cols[6] == "UND":
                if name:
                    undefined.append((name, cols[4]))
            elif cols[4] in ("GLOBAL", "WEAK"):
                defined.add(name)
        self.cache[path] = (defined, undefined)
        return self.cache[path]


# The car's GNU C++ runtime is QNX 6.5's (GCC 4.4.2, GLIBCXX_3.4.13); the
# toolchain's libstdc++.so.6 link points at a newer GCC's copy.
LIB_OVERRIDES = {"libstdc++.so.6": "libstdc++.so.6.0.13"}


def find_lib(sysroot, name):
    name = LIB_OVERRIDES.get(name, name)
    for d in LIB_DIRS:
        p = os.path.join(sysroot, d, name)
        if os.path.exists(p):
            return p
    return None


CAR_LIBS = {"libscreen.so.1": ("screen_",)}


def check(elf, sysroot, car_libs, binary):
    """(missing symbols, symbols left to car libraries, libraries neither has)"""
    exported, unknown, seen = set(), [], set()
    queue = list(elf.needed(binary))
    while queue:
        lib = queue.pop()
        if lib in seen:
            continue
        seen.add(lib)
        path = find_lib(sysroot, lib)
        if not path:
            unknown.append(lib)
            continue
        exported |= elf.symbols(path)[0]
        queue += elf.needed(path)
    _, undefined = elf.symbols(binary)
    prefixes = tuple(p for lib in unknown if lib in car_libs for p in car_libs[lib])
    absent = sorted({n for n, bind in undefined if bind != "WEAK" and n not in exported})
    from_car = [n for n in absent if prefixes and n.startswith(prefixes)]
    missing = [n for n in absent if n not in from_car]
    return missing, from_car, [lib for lib in unknown if lib not in car_libs]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sysroot", required=True, help="QNX 6.5.0 SP1 target directory (…/target/qnx6)")
    parser.add_argument("--readelf", default="llvm-readelf")
    parser.add_argument("--car-lib", action="append", default=[],
                        help="NAME:PREFIX[,PREFIX]: a library the car has but the sysroot lacks")
    parser.add_argument("binaries", nargs="+")
    args = parser.parse_args()
    car_libs = dict(CAR_LIBS)
    for spec in args.car_lib:
        name, _, prefixes = spec.partition(":")
        car_libs[name] = tuple(p for p in prefixes.split(",") if p)
    elf = Elf(args.readelf)
    status = 0
    for binary in args.binaries:
        with open(binary, "rb") as f:
            if not is_arm32(f.read(52)):
                print("%s: not an ELF32 ARM binary (a host build?)" % binary)
                status = 1
                continue
        missing, from_car, missing_libs = check(elf, args.sysroot, car_libs, binary)
        if missing_libs:
            print("%s: needs libraries QNX 6.5.0 SP1 lacks: %s" % (binary, ", ".join(missing_libs)))
        if missing:
            print("%s: imports missing from QNX 6.5.0 SP1: %s" % (binary, ", ".join(missing)))
        if missing or missing_libs:
            status = 1
        else:
            note = " (%d left to the car's libraries)" % len(from_car) if from_car else ""
            print("OK: %s imports only what QNX 6.5.0 SP1 provides%s" % (binary, note))
    return status


if __name__ == "__main__":
    sys.exit(main())
