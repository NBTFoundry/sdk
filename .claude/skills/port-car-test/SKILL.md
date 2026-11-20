---
name: port-car-test
description: Prepare a car test of an application ported to NBT Manager - test packages signed with the test key, a step-by-step procedure for the user, a manager test kit when the stack changed, all copied to the kit folder. Use before the first release or when car-side behaviour changed.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# Car test

Tests run in the user's own car, parked, by the user. There is no bench.
Workstation values (test key, kit folder, toolchain): the workspace `CLAUDE.md`.

1. **Manager kit.** If the port needed changes in the manager, the phone app
   or the installer, the car needs a new kit first: the manager's
   `manager-kit` skill. The procedure names the kit version (`devN`).
2. **Two test packages**, to test install then update:
   `make package APP_KEY=<test key> APP_VERSION=0.0.N APP_SEQ=N`, for the next
   two N above anything sent before. Verify both with `nbt-verify`
   (port-package). Put them in `build/car-test/` with `SHA256SUMS.txt`.
3. **`docs/car-test.md`**, for the user in the car:
   - what is needed (kit version, phone app paired, SSH for logs);
   - parts in order, each step with what to look at: A install and health
     check, settings; B start and use (every control, sound, files); C stop,
     start again, update while running; D failures over SSH; then the logs
     to collect before switching off (`/dev/shmem` is RAM);
   - how to go back (uninstall);
   - a result table, one row per step.
4. **Copy** the packages, `SHA256SUMS.txt` and the procedure (as
   `<id>-car-test.md`) to the kit folder; never overwrite a file there;
   check the sums at the destination.
5. **Hand over** in a few lines: where the files are, which kit, what to run.
   Results come back as the table; fix, and send the next N.

The car test gates upstream pull requests and the first release.
