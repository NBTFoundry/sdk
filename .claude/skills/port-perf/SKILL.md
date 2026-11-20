---
name: port-perf
description: Make an application ported to the NBT App SDK run at full speed on the head unit - what to measure on the desktop, in the QNX 6.5 VM and in the car, and what to change when it runs late (frame skip, filter, resolution, a lighter core, the module build). Use when a port is slow, stutters, crackles or the user reports frame rates.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# Performance

The head unit's ARMv7 CPU is shared with iDrive, and applications run at a
lower priority (`sdk/docs/porting.md`, "The head unit"). Only the car gives
real numbers; the desktop and the VM find regressions and hot spots.

Where each measure is valid:

| Where | Measures | Not valid for |
| --- | --- | --- |
| Desktop host build | hot spots (`perf record`/`perf report` on `build/nbt-<name>-host`), allocation counts, relative change between two commits | absolute speed |
| QNX 6.5 VM | that it runs; crash and hang behaviour | any timing: no GPU, emulated CPU |
| Car | frame rate (the app's **Show FPS** setting), sound, CPU (`pidin` over SSH, if the user agrees) | |

In the car (the user runs it, port-car-test writes the steps): one scene, the
same game or level each time, for a minute; note the FPS shown, whether sound
crackles, and whether the controls lag. Ask for the app's log too (car-logs,
in the manager repository), with **Debug log** on.

When it runs late, in this order (cheapest first; ask before changing defaults):

1. **Settings the user can already choose:** `frameskip` `auto` (frames not
   drawn while late; the core must ask with `GET_AUDIO_VIDEO_ENABLE`), filter
   `nearest`, `sound` `off` to see whether audio is the cost.
2. **Our code:** draw once per frame, one texture upload of the frame (no
   per-pixel conversion on the CPU when GL can do it), no allocation or file
   I/O in the frame loop, no `glFinish`/`glReadPixels`.
3. **Upstream's own speed options** (core options, compile-time switches,
   a dynarec): expose them as settings (port-package) or set them at build.
4. **A lighter upstream:** another core or an older branch of the same
   project (for example a 2010-era libretro core), with the same application
   around it. That is a new survey (port-survey): ask the user.
5. **The module build** (port-module): modern Clang with LTO may be faster
   than GCC 4.8.3; measure both on the same scene. Guests pay for the GL
   stream: count the GL calls per frame first.

Rules:

- Change one thing at a time and measure it the same way; keep the numbers
  (scene, setting, FPS) in the memory note and in the commit message.
- Never trade correctness for speed silently: a default that drops frames or
  sound is the user's decision.
- NEON and unaligned memory access stay off until the car session says they
  are safe (`sdk/docs/native-apps.md`, "The car session").
