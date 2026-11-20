---
name: port-companion
description: Port a program that must stay its own process (a daemon with its own protocol, sockets, child processes) to NBT Manager with an SDK adapter that carries it as an appended payload and supervises it. Use for the application step of a companion port.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# An adapter around a companion program

Example: nf-android-auto (`src/daemon.*`, `src/protocol.hpp`,
`src/settings.def`, `tools/check_protocol.py`). API: "Other programs" in
`sdk/docs/api.md`.

The manager starts the adapter, an ordinary SDK application. The adapter
carries upstream's unmodified executable, built with upstream's own Makefile,
and runs it.

1. **One file:** `$(NBT_SDK_APPEND) <adapter> <companion> <out>` in the
   Makefile; the host build appends a fake companion (a small Python script
   speaking the same protocol) for the tests.
2. **The protocol, written once:** every fact the adapter relies on (port,
   command words, state files, environment variables, arguments) in one
   header with a `// upstream: <file>` comment each; `tools/check_protocol.py`
   finds each one in the pinned sources and runs in `make test`. Settings go
   in one table (`src/settings.def`) that also generates the package's
   `--setting` lines (`tools/package_args.py --check`).
3. **Start:** `extract_own_payload(data_dir, name, ...)`; refuse to start when
   another copy already answers (stop a leftover from a killed adapter
   first); `spawn_child` with an explicit descriptor map (`/dev/null` or a log
   for stdio) and `child_environment` (settings as upstream's variables;
   `keep_preload` when it draws on the screen).
4. **Ready** only when the companion answers its protocol; heartbeat only
   while it keeps answering; the status line from its state.
5. **Stop** (SIGTERM, or when the companion dies): upstream's own quit
   command, then `stop_child(pid, group, wait_ms, term_ms)`; restore what it
   changed (screen layers, files) if it cannot.
6. **Health check** (`NBT_LAUNCH=check`): prove the payload runs on this
   head unit (a version or licence run), nothing more: no screen, no hardware.
7. **Logs:** the app log holds 64 KiB; let the companion keep its own larger
   log in RAM or on USB, and say where in the README.
8. Upstream's own resource needs (threads, descriptors, memory) count
   against the sandbox: measure them in the VM or the car.
