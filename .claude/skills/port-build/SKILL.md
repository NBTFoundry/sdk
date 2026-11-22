---
name: port-build
description: Write the Makefile of an application ported to the NBT App SDK - host, sanitizer and QNX builds of upstream's sources and ours, the SDK built in, the qnx-check gate. Use when creating or changing an nf-* Makefile.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# The Makefile

Start from the closest example's Makefile (nf-snes9x, nf-android-auto) and
keep its target names: the other skills and the release workflow use them.

| Target | Does |
| --- | --- |
| `host` | the Linux (X11) binary `build/nbt-<name>-host` and the unit tests |
| `test` | unit tests, then the Python integration tests |
| `test-asan` | the same under AddressSanitizer, ours under UBSan too |
| `check-static-init` | no `.init_array`/`.ctors` in our objects, then the SDK's check |
| `qnx` | `build/nbt-<name>-qnx` (stripped) and `-unstripped` |
| `qnx-check` | the binary only imports what the car has |
| `package` | the signed package (port-package) |
| `format`, `clean` | |

Rules:

- `NBT_SDK_DIR ?= ../sdk`, `NBT_MGR_DIR ?= ../manager`,
  `NBT_SDK_BUILD_DIR := $(abspath $(BUILD_DIR))/sdk`, then
  `include $(NBT_SDK_DIR)/nbt-sdk.mk`. It provides `NBT_SDK_INC`,
  `NBT_SDK_{HOST,ASAN,QNX}_LIBS`, `NBT_SDK_FRONT_*_LIBS`,
  `NBT_SDK_LIBRETRO_INC`, `NBT_SDK_HOST_LDLIBS`, `NBT_SDK_QNX_LDFLAGS`,
  `NBT_SDK_QNX_LDLIBS`, `NBT_SDK_APPEND`, `NBT_SDK_BUNDLE` and the module flags.
- Upstream's source list comes from upstream: `include` its fragment
  (nf-snes9x: `libretro/Makefile.common`) or run its own Makefile
  (nf-android-auto: `$(MAKE) -C <dir> <target>`). Never copy the list.
  When upstream's Makefile writes objects next to its sources or sets flags
  per object, run it in a copy of the submodule per build kind
  (`rsync -a --exclude .git` into `$(BUILD_DIR)/core/<kind>/`, objects kept)
  and link the static library it makes (nf-pcsx-rearmed). Pass flags it
  appends to through the environment (`CFLAGS=... $(MAKE)`): on the command
  line they would override its own; set `GIT_DIR` to the submodule's for its
  version string. GNU ld takes from an archive only what is already wanted:
  list a core archive after the SDK libraries that call it.
- Upstream objects: `-w` and upstream's defines. Ours:
  `-std=c++11 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti`.
- Objects per kind in `$(OBJ_DIR)/{host,asan,qnx}/`, built with `-MMD -MP`;
  `-include` the `.d` files at the end.
- Every object rule lists its kind's build stamp (`nbt-stamp.mk`, from
  `nbt-sdk.mk`): `stamp = $(OBJ_DIR)/$(1).stamp`, `$(OBJ_DIR)/host/%.o:
  %.cpp $(call stamp,host)`, and at the end of the Makefile one
  `$(call nbt_stamp,$(call stamp,<kind>),<compiler and every flag of the
  kind>,<host compilers>,<$(NBT_TOOLCHAIN)/VERSION for the NBT toolchain>)`
  per kind. Objects then rebuild when the compiler, the toolchain or the
  flags change, and only then. A core built by upstream's own make in a copy
  gets a `core-<kind>` stamp; its build deletes the copy's objects when that
  stamp is not the one the objects were built with, and keeps our command
  line's `CFLAGS`/`CXXFLAGS` out of upstream's `MAKEFLAGS` (nf-pcsx-rearmed).
- Applications are built without the QNX SDP. Prefer a module or a guest
  (port-module). A module or guest repository never names QNX (Makefile,
  README, workflow comments, package.args, sources): it is built with the NBT
  toolchain and run by the manager's `nbt-runner`. Its Makefile declares no
  `QNX_ENV` or `QNX_SCREEN_LIBDIR` (they reach the SDK's runner build from the
  command line) and runs its VM test with `$(NBT_SDK_VM_RUN)`; the toolchain
  does not define `__QNXNTO__` for modules and guests. A program that must stay a native QNX executable uses the
  NBT toolchain's QNX target (`sdk/docs/native-apps.md`, "Ordinary QNX
  programs without the SDP"):
  - compile: `$(NBT_QNX_CXX) $(NBT_QNX_CXXFLAGS) $(NBT_QNX_SDK_INC) -O2
    -std=c++11 ...` (`$(NBT_QNX_CC) $(NBT_QNX_CFLAGS)` for C);
  - link: `$(call nbt_qnx_link,$@,<objects> <SDK QNX libs>,$(NBT_SDK_QNX_LDLIBS))`,
    then `$(NBT_LLVM_BIN)/llvm-strip` into the shipped binary; keep
    `-unstripped` for the checks;
  - the car's libraries come as link stubs (`-lsocket`, `-lscreen`, `-lusbdi`,
    `-lasound`, `-lssl`, ...); a library outside the toolchain's list needs its
    stub added there (the toolchain's `qnx/stubs`).
- `qnx-check` on the unstripped binary:
  `$(NBT_SDK_ROOT)/tools/check_qnx_imports.py --sysroot $(QNX65_SP1_SYSROOT) --readelf $(NBT_MOD_READELF) <binary>`,
  with `QNX65_SP1_SYSROOT ?= $(HOME)/src/tools/qnx-6.5sp1-tools/sdk/target/qnx6`
  (skipped without it); `kdMain` exported (`llvm-nm -D --defined-only`) when it
  opens a window. Build errors: port-qnx-fixes.

Check: `make -j"$(nproc)" host`, then `make qnx qnx-check` (after
`make -C ../sdk toolchain` once); a second `make` builds nothing, and
`make CXXFLAGS=-DX host` rebuilds the host objects only.
