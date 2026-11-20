---
name: port-tests
description: Test an application ported to the NBT App SDK without a car - unit tests, Python integration tests that start the host build as NBT Manager would, the package test, sanitizers, static-init check, upstream's own tests and the QNX 6.5 VM. Use when adding or running an nf-* app's tests.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# Tests

Examples: nf-snes9x `tests/` (Xvfb play test, made-up cartridge),
nf-android-auto `tests/` (fake companion, VM test).

`make test` runs:

1. **Unit tests** (`tests/<part>_test.cpp`, a plain binary that fails with a
   message): our logic only (controls, settings mapping, parsers).
2. **Integration tests** (`tests/test_<name>.py`, `unittest`): start the host
   build as the manager would, with `NBT_LAUNCH=user` or `check`,
   `NBT_DATA_DIR` and `NBT_FILES_DIR` (temporary folders), `NBT_SETTING_<KEY>`,
   `NBT_BRIDGE_CAPS` and the granted descriptors (`pass_fds`), `NBT_NO_AUDIO=1`,
   a display from `Xvfb -displayfd`, `LIBGL_ALWAYS_SOFTWARE=1`. Without
   `--ready-fd`/`--lease-fd` the app runs on its own; pass both to check
   readiness. Cover: the health check exits 0 without a window, a setting
   reaches upstream, saves are written, SIGTERM stops it cleanly.
3. **The package test:** builds the package with a throw-away key
   (`openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072`) and checks
   its declaration with the manager's `nbt-verify`; skips when the manager's
   tools are missing.
4. Checks that compare our facts with the pinned upstream (port-companion).

Test data is made by the tests (a tiny ROM, a small file): never a
copyrighted game.

Also:

- `make test-asan` (`ASAN_OPTIONS=detect_leaks=0`, `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`).
- `make check-static-init`.
- `make test-upstream`: upstream's own host tests at the pin, when it has some.
- **QNX 6.5 SP1 VM** (`make vm-test`, local only):
  `$(NBT_SDK_ROOT)/tools/qnx65-vm/run.sh -c "<command>" ... -x "<regex>" ... <files>`
  boots QNX under QEMU, runs the commands, and fails unless every `-x`
  pattern shows in the log. Run the health check and a user start with
  `NBT_DATA_DIR=/dev/shmem`. No GPU, sound or network; files go into
  `/proc/boot` (port-qnx-fixes for its quirks).

Before each commit: `make -j"$(nproc)" test test-asan check-static-init`.
