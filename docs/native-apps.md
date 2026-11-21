<!-- SPDX-License-Identifier: MPL-2.0 -->
# Applications without the QNX SDP

An NBT application can be built with open-source tools only — Clang, picolibc, libc++ and this SDK — with no QNX SDP file. On the head unit, `nbt-runner` runs it. The runner is the only application-side program linked with QNX libraries, and it ships with NBT Manager.

## The two kinds of payload

| | Module (mode A) | Guest (mode B) |
|---|---|---|
| For | applications that aren't GPL/LGPL | GPL/LGPL applications |
| Is | an ARM shared object: no imports, one export (`nbt_module_v1`) | a static ARM executable: no QNX code in it |
| Runs | inside the runner's process, loaded with `dlopen` | as the runner's child process |
| Host calls | direct function calls through `nbt_host_v1` | kernel calls for threads, memory and time; messages to the runner for the rest |
| GL | `gl*` jumps straight into the driver (a generated table) | `gl*` calls become a command stream in shared memory, run by the runner |
| Audio | the runner's playback thread calls the app's render function | a guest thread fills a ring in shared memory; the runner's playback thread drains it |

The QNX licence forbids linking QNX code with GPL code, and statically linking it with LGPL code. A guest keeps GPL code in its own process, and that process contains no QNX code. `tools/check_module.py --map` enforces this: it rejects a module that links a GPL or LGPL input.

Both kinds use the same source code and the same SDK classes (`Lifecycle`, `VideoEgl`, `AudioPcm`, the bridge readers, the front end and libretro host). Only the link step differs.

## Building an application

Include `nbt-sdk.mk` as today, compile with the module toolchain, then link with one of two macros. Each macro links the payload, writes a link map and runs `tools/check_module.py` on the result, including the licence gate. Its third argument is the SPDX licence of the application's own objects:

```make
include $(NBT_SDK_DIR)/nbt-sdk.mk

$(OBJ)/mod/%.o: %.cpp | nbt-sdk-mod
	$(NBT_MOD_CXX) $(NBT_MOD_CXXFLAGS) -O2 -std=c++11 $(APP_INC) -c $< -o $@

app.so: $(OBJS) $(NBT_SDK_MOD_DEPS)          # a module
	$(call nbt_module_link,$@,$(OBJS),MPL-2.0)

app.guest: $(OBJS) $(NBT_SDK_MOD_DEPS)       # a guest
	$(call nbt_guest_link,$@,$(OBJS),GPL-2.0-or-later)
```

`$(NBT_SDK_BUNDLE) app.so app-bundle` appends a payload to `nbt-runner`, giving one executable that today's manager installs unchanged.

Worked examples: the `module` and `guest` targets of the NBTFoundry application repositories.

Requirements: GNU make 4.3 or newer, Python 3, and the NBT toolchain (NBTFoundry/toolchains) for Linux x86-64, macOS arm64 or Windows x86-64 (MSYS2 make). The toolchain is Clang and LLD plus the runtime every payload links: picolibc, compiler-rt and libc++, and the C library headers that `module/runtime` implements.
- `make toolchain` installs the release this SDK is pinned to (`NBT_TOOLCHAIN_VERSION` in `module/flags.mk`) in `sdk/toolchain/`, where every application finds it. It checks the download against the SHA-256 the SDK pins (`module/toolchain.sha256`).
- Or unpack a release anywhere and set `NBT_TOOLCHAIN` to its directory.

Payloads statically contain picolibc, whose BSD-style licences require shipping its notices with them: `share/licenses/picolibc/` in the toolchain. The SDK's own code is MPL-2.0: say where its source is.

What a module or guest cannot do:
- start processes or wait on descriptors: `fork`, `exec*`, `system` and `poll` fail with `ENOSYS`;
- load libraries;
- open files outside the application's data folder. The files folder is readable;
- map files or map at a fixed address: `mmap` takes only anonymous mappings (`MAP_FIXED` fails with `EINVAL`, a file with `ENODEV`) and ignores an address hint.

The C library still declares these functions, so portable code links (`module/runtime/sys_none.c`).

JIT compilers work as on Linux (`module/runtime/mman.c`, toolchain 1.1.0 or newer): anonymous `mmap` (pages committed on first use, zeroed), `munmap`, `mprotect` with `PROT_EXEC` (writable and executable at once, or one after the other; static memory too), and `__builtin___clear_cache` before running written code.

Portable code's probes get plain answers (toolchain 1.1.0 or newer): thread scheduling attributes are accepted and unused, as POSIX has it under `PTHREAD_INHERIT_SCHED` (`PTHREAD_EXPLICIT_SCHED` fails with `ENOTSUP`); `getauxval(AT_HWCAP)` and `AT_HWCAP2` report no optional CPU feature (`<asm/hwcap.h>` has the bits), so code takes its baseline paths: no NEON.

## The runner

`make nbt-runner` builds it with the QNX SDP (`QNX_ENV`, `QNX_SCREEN_LIBDIR`). It is an ordinary NBT application: it takes the manager's bridge descriptors, then runs the payload named by `--nbt-payload PATH`. With no `--nbt-payload`, it runs a payload appended to its own executable, copied to the data folder (or `/dev/shmem`). Before running anything, it checks the payload's `.note.nbt` (ABI major and minor, module or guest).

Manager integration is deferred until the car experiment succeeds; see "Not done yet" below.

## Testing without a head unit

`make test` runs the guest GL stream on the desktop's own driver (`tests/gl_loopback.cpp`, Mesa through surfaceless EGL): the front end draws the same scene with direct GL calls, then through the guest's encoders and the runner's decoders, and both must give the same pixels with no GL error. It needs no QNX file; without Mesa's EGL it says SKIPPED.

`make vm-test QNX_ENV=… QNX_SCREEN_LIBDIR=…` boots QNX 6.5.0 SP1 for ARMv7 under QEMU (`tools/qnx65-vm/run.sh`) and checks:
- C and C++ in both modes, plus crash and hang detection;
- the lifecycle and settings through `nbt-runner`;
- the guest GL stream and audio ring against direct calls, using the runner's recording fakes (`NBT_RUNNER_FAKE_GL=1`, `NBT_RUNNER_FAKE_AUDIO=1`).

The VM has no GPU or sound card. Its kernel mishandles a thread's first FPU instruction in Thumb state, and on the car Thumb code in a guest died with SIGILL where ARM code runs (picolibc's Thumb `strcmp`). So modules, guests and the runner's own code are built in ARM state, the module runtime replaces picolibc's three Thumb functions (`strlen`, `strcmp`, `memchr`) and `tools/check_module.py` refuses any Thumb code. The VM loads guests from its boot image, not from a disk file, and did not show the car's fault.

## The car session

The car session answers what the VM can't. It runs packages built from bundles (`tools/nbt_bundle.py`: the runner with the payload appended), so today's manager installs them unchanged:
- `car-probe.so`, run with `NBT_RUNNER_BENCH=1`:
  - GL frame times (p50/p99);
  - a 10 s tone;
  - `memcpy`, `memset` and `malloc` speed against the car's `libc.so.3`;
  - whether NEON registers d16–d31 survive context switches.
- `make NBT_MOD_UNALIGNED=1 BUILD_DIR=build/unaligned car-probe`: the same probe allowing unaligned accesses. If it runs, unaligned access is allowed on the car, and picolibc's faster ARMv7-A `memcpy` becomes usable.
- an emulator built as a module and bundled (`make module-bundle`): frame time and CPU against its SDP build on the same content.
- a GPL GL game as a guest: the GL stream on the real driver.

## Compatibility

Applications and the runner are built and shipped apart: a payload keeps working across manager updates.
- **Host ABI** (`include/nbt/host_abi.h`):
  - The payload's `.note.nbt` records the ABI it was built for. The runner refuses another major, or a newer minor.
  - Minor versions only append members to `nbt_host_v1`. Code checks a member newer than v1.0 with `NBT_HOST_HAS(host, member)` before calling it (`NBT_HAS(member)` inside the module runtime).
- **Guest protocol** (`include/nbt/guest_proto.h`):
  - Covers the messages, the shared arena and the GL stream. Ops, arena words and GL commands are only ever added; an unknown op answers `-NBT_ENOSYS`.
  - A change an older guest couldn't follow raises `NBT_GUEST_PROTO_VERSION`. The runner writes its version into the arena, and the guest refuses to start under an older runner.
- **GL stream ids** come from OpenGL ES 2.0 sorted by name (`tools/glregistry.py`). That set is frozen, so an id never changes.

## Not done yet

- **Manager integration:**
  - launching through the runner when a payload carries `.note.nbt`;
  - checking the note at install;
  - adding the runner as a component of the signed manager release.
- **Guest GL limits:**
  - one GL thread;
  - `glGetUniform*v` is not streamed;
  - an indexed draw from a bound element buffer must take all its vertex data from buffers.
- **Defaults the car session decides:** NEON (`-mfpu=neon`) and unaligned access.
