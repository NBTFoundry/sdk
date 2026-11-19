#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Install an NBT toolchain release for this host ("make toolchain").

  get_toolchain.py VERSION DIR

Downloads nbt-toolchain-VERSION for this host from the NBTFoundry/toolchains
releases with the GitHub CLI (the repository is private: "gh auth login"
first), checks it against its published SHA-256 and unpacks it as
DIR/nbt-toolchain-VERSION. Does nothing if that is already there.
"""
import hashlib
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile

REPO = "NBTFoundry/toolchains"


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


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
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
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip())
    version, dest = sys.argv[1], os.path.abspath(sys.argv[2])
    name = "nbt-toolchain-" + version
    target = os.path.join(dest, name)
    if os.path.isfile(os.path.join(target, "VERSION")):
        print("%s: already installed" % target)
        return 0
    if not shutil.which("gh"):
        sys.exit("get_toolchain: needs the GitHub CLI (gh), or unpack %s from "
                 "https://github.com/%s/releases yourself" % (name, REPO))
    hostname, ext = host()
    asset = "%s-%s%s" % (name, hostname, ext)
    os.makedirs(dest, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=dest) as tmp:
        subprocess.run(["gh", "release", "download", "v" + version, "-R", REPO, "-D", tmp,
                        "-p", asset, "-p", asset + ".sha256"], check=True)
        archive = os.path.join(tmp, asset)
        with open(archive + ".sha256", encoding="utf-8") as f:
            expected = f.read().split()[0]
        if sha256(archive) != expected:
            sys.exit("get_toolchain: %s does not match its SHA-256" % asset)
        unpack(archive, os.path.join(tmp, "x"))
        os.replace(os.path.join(tmp, "x", name), target)
    print("%s: installed" % target)
    return 0


if __name__ == "__main__":
    sys.exit(main())
