---
name: port-module
description: Build an application ported to the NBT App SDK without the QNX SDP - as a module loaded into nbt-runner, or as a guest, nbt-runner's child process and the only form for GPL or LGPL code - with the payload and licence check, its package (bridge 1.7 runner), a bundle for older managers and a run in the QNX 6.5 VM. Use when an app needs a module or guest build.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# Modules and guests

Background: `sdk/docs/native-apps.md`. Examples: nf-snes9x's `module`
target; the `guest` targets of https://github.com/NBTFoundry/nf-doom and
nf-pcsx-rearmed.

| | Module | Guest |
| --- | --- | --- |
| For | code that isn't GPL or LGPL | GPL or LGPL code (required); any other may use it too |
| Target, output | `module`, `build/nbt-<name>.so` | `guest`, `build/nbt-<name>.guest` |
| Link | `$(call nbt_module_link,$@,<objects>,<SPDX>)` | `$(call nbt_guest_link,$@,<objects>,<SPDX>)` |
| Runs | inside nbt-runner, called directly | as nbt-runner's child process, GL as a command stream |

1. **Toolchain:** `make toolchain` in the SDK once (or `NBT_TOOLCHAIN`), GNU
   make 4.3 or newer. Nothing from the QNX SDP is used.
2. **Compile rules** in the app's Makefile (port-build has the rest):
   - objects in `$(OBJ_DIR)/mod/...`, each rule with the order-only
     prerequisite `| nbt-sdk-mod` (builds the SDK for modules);
   - upstream: `$(NBT_MOD_CC) $(NBT_MOD_CFLAGS)` or
     `$(NBT_MOD_CXX) $(NBT_MOD_CXXFLAGS)`, `-O2 -w`, upstream's defines;
   - ours: the same plus `-Wall -Wextra -Werror -fno-exceptions -fno-rtti`;
   - assembly: `$(NBT_MOD_CC) $(NBT_MOD_ARCH) -c`;
   - no QNX compat headers: picolibc and libc++ have their own gaps, not the
     QNX toolchain's. Newer C and C++ standards are fine here.
3. **Link:** `<payload>: <objects> $(NBT_SDK_MOD_DEPS)` with the macro above.
   Its third argument is the SPDX licence of every linked object, upstream's
   and ours (`LicenseRef-Snes9x AND MPL-2.0`, `GPL-2.0-or-later`). The macro
   writes `<payload>.map` and runs `tools/check_module.py`: shape, ABI note, no
   Thumb code, and the licence gate (GPL or LGPL only in a guest).
   A failure there is a build failure: fix the cause, never the check.
4. **Package the payload** itself: NBT Manager ships `nbt-runner` and starts
   it for a payload with the note. `release/package.args` declares
   `--bridge 1.7` and `runner` in `--bridge-uses` (never optional;
   port-package), and `make package` takes `build/nbt-<name>.so` or `.guest`.
   The CI release passes `qnx: false` to the manager's `app-release.yml`
   with `build: make -j"$(nproc)" module` (or `guest`) and that payload
   (port-release): no QNX SDP, no private image. Only for a manager before
   bridge 1.7, a bundle: `<name>-bundle: <payload> $(NBT_SDK_RUNNER)` running
   `$(NBT_SDK_BUNDLE) <payload> $@` (the runner needs `QNX_ENV` and
   `QNX_SCREEN_LIBDIR`), packaged as an ordinary executable (no `runner`).
5. **VM run** (no GPU, no sound: fake both), from the SDK after
   `make nbt-runner QNX_ENV=… QNX_SCREEN_LIBDIR=…`; payload file names must end
   in `.so`, `.guest` or `-bundle` (copied byte for byte):
   ```sh
   tools/qnx65-vm/run.sh \
     -c "NBT_LAUNCH=check NBT_DATA_DIR=/dev/shmem nbt-runner --nbt-payload /proc/boot/<payload> &" \
     -c "waitfor /none 10" -c "slay -s TERM nbt-runner" -c "waitfor /none 2" \
     -c "NBT_LAUNCH=user NBT_SURFACE=5 NBT_RUNNER_FAKE_GL=1 NBT_RUNNER_FAKE_AUDIO=1 NBT_DATA_DIR=/dev/shmem nbt-runner --nbt-payload /proc/boot/<payload> &" \
     -c "waitfor /none 20" -c "slay -s TERM nbt-runner" -c "waitfor /none 2" \
     -x '<a line the app prints>' build/nbt-runner <path to payload> <data files>
   ```
   The manager stops a health check as it stops a launch: SIGTERM
   (`--stop-after-ms` in `tools/qnx65-vm/vm.mk` is the smoke app's own
   option). Data files land in `/proc/boot`: `NBT_FILES_DIR=/proc/boot`, and a
   libretro application's `--game <file>` plays one without input. The VM
   faults on unaligned accesses, so code that relies on them (hand-written
   assembly, casts of byte pointers) shows there. `tools/qnx65-vm/vm.mk` and
   nf-pcsx-rearmed's `vm-test` are worked examples. A guest's clean end
   prints `runner: guest exited status=0`.

What a payload cannot do (`module/runtime/sys_none.c`): `fork`, `exec*`, `system` and `poll`
fail with `ENOSYS`; no `dlopen`; files only in `NBT_DATA_DIR` (the files folder
is read-only); `mmap` only anonymous, never fixed (a hint is ignored). A JIT
(dynarec) works: `mmap`/`mprotect` with `PROT_EXEC`, also on static memory, and
`__clear_cache` (`module/runtime/mman.c`). A guest has one GL thread; `glGetUniform*v` is not streamed; an
indexed draw from a bound element buffer takes all its vertex data from
buffers. Code that needs any of these: port-sdk-gap, or a QNX SDP build.

Defaults until the car session settles them: aligned accesses
(`NBT_MOD_UNALIGNED=0`; the VM faults on unaligned ones), no NEON, ARM state.

Check: `make module` (or `guest`) links and passes its check; the VM run
passes; the package installs on the car (port-car-test, a manager kit with
the runner).
