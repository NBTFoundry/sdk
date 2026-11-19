#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Install an NBT toolchain release for this host ("make toolchain").

  get_toolchain.py VERSION SUMS DIR

Downloads nbt-toolchain-VERSION for this host from the NBTFoundry/toolchains
releases, checks it against the SHA-256 the SDK pins for it (SUMS, a copy of
the release's SHA256SUMS) and unpacks it as DIR/nbt-toolchain-VERSION. Does
nothing if that is already there.
"""
import hashlib
import os
import platform
import shutil
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

URL = "https://github.com/NBTFoundry/toolchains/releases/download/v%s/%s"


def host():
    system, machine = platform.system(), platform.machine().lower()
    windows = system == "Windows" or system.startswith(("MINGW", "MSYS", "CYGWIN"))
    if machine in ("x86_64", "amd64"):
        if system == "Linux":
            return "linux-x86_64", ".tar.xz"
        if windows:
            return "windows-x86_64", ".zip"
    if system == "Darwin" and machine == "arm64":
        return "macos-arm64", ".tar.xz"
    sys.exit("get_toolchain: no NBT toolchain is built for %s %s" % (system, machine))


def pinned(sums, asset):
    with open(sums, encoding="utf-8") as f:
        for line in f:
            fields = line.split()
            if len(fields) == 2 and fields[1] == asset:
                return fields[0]
    sys.exit("get_toolchain: %s pins no SHA-256 for %s" % (sums, asset))


def download(url, path):
    h = hashlib.sha256()
    try:
        with urllib.request.urlopen(url, timeout=60) as r, open(path, "wb") as f:
            for block in iter(lambda: r.read(1 << 20), b""):
                h.update(block)
                f.write(block)
    except OSError as e:  # urllib's errors are OSErrors
        sys.exit("get_toolchain: %s: %s" % (url, e))
    return h.hexdigest()


def unpack(archive, dest):
    if archive.endswith(".zip"):
        with zipfile.ZipFile(archive) as z:
            z.extractall(dest)  # drops absolute and ".." names
        return
    with tarfile.open(archive) as t:
        if hasattr(tarfile, "data_filter"):
            t.extractall(dest, filter="data")
            return
        for m in t.getmembers():
            if m.name.startswith("/") or ".." in m.name.split("/") or not (m.isfile() or m.isdir() or m.issym()):
                sys.exit("get_toolchain: unsafe entry %s in %s" % (m.name, archive))
        t.extractall(dest)


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__.strip())
    version, sums, dest = sys.argv[1], sys.argv[2], os.path.abspath(sys.argv[3])
    name = "nbt-toolchain-" + version
    target = os.path.join(dest, name)
    if os.path.isfile(os.path.join(target, "VERSION")):
        print("%s: already installed" % target)
        return 0
    hostname, ext = host()
    asset = "%s-%s%s" % (name, hostname, ext)
    expected = pinned(sums, asset)
    os.makedirs(dest, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=dest) as tmp:
        archive = os.path.join(tmp, asset)
        print("downloading %s" % (URL % (version, asset)))
        if download(URL % (version, asset), archive) != expected:
            sys.exit("get_toolchain: %s does not match the SHA-256 in %s" % (asset, sums))
        unpack(archive, os.path.join(tmp, "x"))
        os.replace(os.path.join(tmp, "x", name), target)
    print("%s: installed" % target)
    return 0


if __name__ == "__main__":
    sys.exit(main())
