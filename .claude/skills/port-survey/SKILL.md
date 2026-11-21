---
name: port-survey
description: First step of porting an application to the NBT App SDK. Study the upstream project (licence, build, platform layer, dependencies, ongoing work) and agree with the user on shape, build form and names before writing code.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# Survey the upstream project

Read only. Clone upstream into the scratchpad, not the workspace.

1. **Identity.** URL, latest release tag and its commit, release cadence.
   `gh pr list -R <owner>/<repo>` and recent issues: note work in progress on
   the parts you will touch (platform layer, build, libretro core).
2. **Licence** of everything that ends up in the binary (code, bundled
   libraries, data). It sets the build form:
   - GPL or LGPL: a guest (`sdk/docs/native-apps.md`), never linked with QNX
     code; our files then take a GPL-compatible licence.
   - MPL, permissive or custom non-commercial: a module, or a native QNX
     program built with the NBT toolchain's QNX target when it must stay one
     (`make qnx`). No application builds with the QNX SDP.
   - No licence, or no redistribution: stop and tell the user.
   - List every notice the licence requires (Required Notice lines, credits).
3. **Shape** (`sdk/docs/porting.md`):
   - a libretro core in the tree (`libretro/`, `Makefile.libretro`, `retro_run`): port-libretro;
   - its own platform layer (SDL, GLUT, `i_*`/`sys_*` files): port-native;
   - it must stay its own process (sockets, starts programs, loads
     libraries, a daemon with its own protocol): port-companion.
4. **Fit with the head unit.** Each "no" needs a plan, or ends the port:
   - C++11 / C99 at most for an SDP build (GCC 4.8.3); newer only as a module or guest.
   - Nothing may throw (`-fno-exceptions`; `qnx-check` rejects `__cxa_throw`).
   - Graphics: OpenGL ES 2, desktop GL 1.x through `libnbt-gles2`, or a
     software frame (`FrameVideo`). Desktop GL 2+ or Vulkan: a rewrite.
   - Sound: 16-bit stereo, pulled by `AudioPcm`'s thread.
   - Input: the iDrive controller and an optional USB gamepad. No keyboard,
     mouse or touch in the SDK.
   - Under NBT Manager: 256 MiB data, 512 MiB address space, 128 descriptors,
     64 threads; one executable of at most 64 MiB.
   - User data (games, media): the `files` capability.
   - Network, other processes, car services: ask the user; the stack may
     need a generic feature (port-sdk-gap).
5. **Build.** How upstream lists its sources (a Makefile fragment, CMake, a
   list), its defines, its host tests.
6. **Ask the user** (one AskUserQuestion, recommended option first):
   application id (`--app`, a lowercase token), repository `nf-<name>`,
   shape, build form, licence of our files, upstream tag to pin, whether
   upstream needs changes (then an org fork). Note the answers in memory.
