---
name: port-repo
description: Create the nf-<name> repository of an application being ported to the NBT App SDK - GitHub repository, NBTFoundry fork when upstream needs changes, upstream as an SSH submodule, and the standard skeleton files.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# Create the repository

Needs the answers from port-survey. Ask before creating anything on GitHub.

1. **Repository.** `gh repo create NBTFoundry/nf-<name> --public --description "<one line>"`.
   The folder is `nf-<name>` next to `sdk`; if it already exists and is
   empty, run `git init -b main` there and add
   `git@github.com:NBTFoundry/nf-<name>.git` as `origin`.
2. **Upstream.**
   - If upstream will need changes, fork it first:
     `gh repo fork <owner>/<repo> --org NBTFoundry --clone=false`, and use the fork below.
   - `git submodule add git@github.com:<owner>/<repo>.git third_party/<name>`
     (SSH, never HTTPS), check out the agreed tag in it, commit the pin.
3. **Skeleton.** Copy from the closest example and rename (nf-snes9x:
   libretro; nf-android-auto: companion; https://github.com/NBTFoundry/nf-doom: platform layer):
   - `LICENSE` (ours), `LICENSES/<SPDX-id>.txt` for each licence in the package;
   - `DISCLAIMER.md`, verbatim from the SDK, except its last paragraph: only
     the trademark owners the repository names (no BlackBerry/QNX sentence in
     a module or guest repository, port-build);
   - `README.md`: logo block, the warning banner, tagline, supported head
     units, "Part of NBTFoundry" links, trademarks line; then Status, Build,
     Layout, License (later steps add the rest);
   - `docs/assets/logo-light.svg` and `logo-dark.svg`, copied;
   - `.gitignore` (`build/`, `*.o`, `*.d`, `*.nbt`, `__pycache__/`, `*.pyc`) and `.clang-format`, copied;
   - `release/sdk.ref`: the SDK commit to build against, on the SDK's `main`
     (`git -C ../sdk rev-parse origin/main`); CI and releases read it.
4. An SPDX line heads every file we write (`# SPDX-License-Identifier: MPL-2.0`;
   `<!-- ... -->` in Markdown, after any front matter).
5. Commit ("Skeleton: <upstream> <tag> as a submodule") and push `main`.
