---
name: port-qnx-fixes
description: Known QNX 6.5 build and run problems when porting code to the NBT head unit, with their fixes - toolchain gaps, C++ runtime symbols, missing POSIX features, VM quirks. Use when a QNX build, qnx-check or VM run fails.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# QNX 6.5 problems and fixes

Fixes for upstream code go in `src/compat/` (headers forced with `-include`,
or extra objects), never in the submodule.

| Symptom | Cause | Fix |
| --- | --- | --- |
| `qcc` does nothing, or make error 127 | the QNX environment was not sourced | each QNX command runs as `. "$(QNX_ENV)" && ...`; a 6.6 env script that returns 1 needs a wrapper ending in `true` |
| `_Sinx` (or another libm symbol) missing on the car | built with the QNX 6.6 toolchain | build with the 6.5 SP1 toolchain |
| `__cxa_throw` or `GLIBCXX_3.4.14`+ in `qnx-check` | something throws, or a newer libstdc++ symbol | `-fno-exceptions` everywhere, plus `-Wp,-U__EXCEPTIONS`: qcc defines `__EXCEPTIONS` anyway, which turns libstdc++'s `__try`/`__catch` into real ones |
| `std::__throw_out_of_range_fmt` undefined | GCC 4.9 headers call it; the car's libstdc++ 6.0.13 lacks it | define it in `src/compat/` (nf-snes9x `libstdcxx_qnx.cpp`) |
| type trait clashes after `<math.h>` | QNX's Dinkumware headers before libstdc++'s | a compat header forced first with `-include` (nf-snes9x `snes9x_qnx.h`) |
| `trunc` and other C99 maths missing from `std` | QNX 6.5 declares them only with `_HAS_C9X` | declare them in the compat header |
| `std::to_string` missing | this libstdc++ lacks it | `snprintf` |
| `GLchar` undeclared | the SP1 GLES2 header lacks it | `typedef char GLchar;` in a compat header |
| `O_NOFOLLOW` undeclared | QNX 6.5 has none | `lstat` first, create with `O_EXCL`, check `fstat` for a regular file |
| `fork()` fails | QNX 6.5's `fork` fails in a process with threads | `nbt::app::spawn_child` (QNX `spawn()`) |
| parallel make fails inside upstream's Makefile | upstream's own race | build those targets one by one |
| a static constructor in our object | `check-static-init` | constant-initialised globals; upstream's own are allowed |
| VM: an appended payload is gone | mkifs rewrites executables | name the file `*-bundle` |
| VM: no network | io-pkt does not come up on its QEMU board | test networking in the car |
| VM: a fault on the first FPU instruction in Thumb code | QEMU's kernel bug | modules and guests are built `-marm` (the SDK does it) |
| Car: a guest or module dies with SIGILL at a 2-byte-aligned `ip` (`terminated SIGILL code=1 fltno=1`) | Thumb code; the VM does not show it | ARM state only: `check_module.py` refuses Thumb code; assembly in ARM state (the module runtime replaces picolibc's Thumb `strlen`, `strcmp`, `memchr`) |
| VM: `cat` or another tool missing | the boot image holds only what you pass | add it from `$(QNX65_SP1_SYSROOT)/armle-v7/bin` |

A new problem you solve: add a row here (an SDK change, port-sdk-gap) and a
note in the application's README ("on the QNX toolchain").
