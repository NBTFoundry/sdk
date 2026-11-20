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
- Upstream objects: `-w` and upstream's defines. Ours:
  `-std=c++11 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti`.
- Objects per kind in `$(OBJ_DIR)/{host,asan,qnx}/`, built with `-MMD -MP`;
  `-include` the `.d` files at the end.
- QNX compile: `. "$(QNX_ENV)" && $(QCC) -Vgcc_ntoarmv7le_gpp -D_QNX_SOURCE -O2
  -Wc,-std=c++11 -Wc,-fno-exceptions -Wc,-fno-rtti -Wp,-U__EXCEPTIONS ...`
  (`-Vgcc_ntoarmv7le` for C). An order-only `qnx-env` target stops early
  when `QNX_ENV` is unset.
- QNX link: `$(QCC) -Vgcc_ntoarmv7le_gpp -lang-c++ <objects> <SDK QNX libs>
  -L$(QNX_SCREEN_LIBDIR) $(NBT_SDK_QNX_LDFLAGS) $(NBT_SDK_QNX_LDLIBS)`, then
  `$(QNX_STRIP)` into the shipped binary; keep `-unstripped` for the checks.
- `qnx-check` on the unstripped binary:
  - no `GLIBCXX_3.4.14` or newer, no `CXXABI_1.3.4` or newer, no `__cxa_throw` in `$(QNX_NM) -u`;
  - `$(NBT_SDK_ROOT)/tools/check_qnx_imports.py --sysroot $(QNX65_SP1_SYSROOT) --readelf $(NBT_MOD_READELF) <binary>`,
    with `QNX65_SP1_SYSROOT ?= $(HOME)/src/tools/qnx-6.5sp1-tools/sdk/target/qnx6`;
  - `kdMain` exported (`$(QNX_NM) -D ... | grep -qw kdMain`) when it opens a window.
- Build with the SP1 toolchain (`QNX_ENV` in the workspace `CLAUDE.md`), as
  releases do. Build errors: port-qnx-fixes.

Check: `make -j"$(nproc)" host`, then
`make QNX_ENV=… QNX_SCREEN_LIBDIR=… qnx qnx-check`.
