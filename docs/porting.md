<!-- SPDX-License-Identifier: MPL-2.0 -->
# Porting an application

How an existing program (a game, an emulator, a tool, a daemon) becomes an
application NBT Manager installs and runs: a repository `NBTFoundry/nf-<name>`
next to this SDK, the upstream project unmodified in a submodule, our code on
the SDK around it, and one signed package `<app>-X.Y.Z.nbt`.

The work is meant to be done by an AI agent, one step at a time, with the
skills in [`.claude/skills/`](../.claude/skills/). They are plain Markdown:
people can follow them too. Start with
[`port-app`](../.claude/skills/port-app/SKILL.md).

## Three shapes

| Shape | When | SDK parts | Example |
| --- | --- | --- | --- |
| Libretro core | upstream has a libretro core | `libnbt-retro` (game list, saves, states, menu, controls) | [nf-snes9x](https://github.com/NBTFoundry/nf-snes9x) and [nf-picodrive](https://github.com/NBTFoundry/nf-picodrive) (modules), [nf-fbneo](https://github.com/NBTFoundry/nf-fbneo) and [nf-pcsx-rearmed](https://github.com/NBTFoundry/nf-pcsx-rearmed) (guests) |
| Platform layer | upstream has its own platform layer (SDL, GLUT, `i_*`/`sys_*` files) | `libnbt-sdk`, `libnbt-gles2`, `libnbt-frontend` as needed | [nf-doom](https://github.com/NBTFoundry/nf-doom) (guest) |
| Companion | the program must stay its own process (sockets, other processes, its own protocol) | `nbt/app/child.hpp`, an appended payload | [nf-android-auto](https://github.com/NBTFoundry/nf-android-auto) (native, the toolchain's QNX target) |

## The licence sets the build form

| Upstream code in the binary | Build form |
| --- | --- |
| GPL or LGPL, even one file of it | a guest ([native-apps.md](native-apps.md)): the QNX licence forbids linking GPL code with QNX code, and statically linking LGPL code with it. Check every file the build compiles, not only upstream's main licence (Snes9x's NTSC filter is LGPL: its module leaves it out) |
| MPL, permissive, custom non-commercial | a module; a program that needs the car's own libraries (a companion) a native QNX program built with the NBT toolchain's QNX target. No QNX SDP either way |
| none, or no redistribution | not portable: stop |

Our own files are MPL-2.0 unless the combination requires a GPL-compatible
licence.

## Rules

- **Upstream stays unmodified.** A change it needs goes to a branch of an
  NBTFoundry fork and becomes an upstream pull request, after the car test.
- **Nothing application-specific in the stack.** What the SDK, the manager or
  the phone app lacks is added there as a generic feature
  ([`port-sdk-gap`](../.claude/skills/port-sdk-gap/SKILL.md)).
- **One source per fact.** Upstream's source lists, headers and protocol are
  included or checked against the pin, never copied.
- **Our code:** C++11 (C99), no exceptions, no RTTI, nothing that runs before
  `main`, bounded buffers. Upstream's own static constructors are allowed.
- **Declare the bridge version the build needs:** 1.7 with `runner` for a
  module or a guest. NBT Manager does not start an application declaring one
  older than 1.5.

## The head unit

- QNX 6.5.0 SP1 on ARMv7. Releases are built with the NBT toolchain (Clang,
  picolibc, libc++), never the QNX SDP: modules and guests run under the
  manager's `nbt-runner`; a native program uses the toolchain's QNX target and
  links the car's own libraries at run time.
- Under NBT Manager: 256 MiB of data, 512 MiB of address space, 128
  descriptors, 64 threads.
- A package holds one executable of at most 64 MiB, at most 12 settings and a
  manifest of at most 2,048 bytes.
- OpenGL ES 2 on a panel-sized window; 16-bit stereo sound; the iDrive
  controller and an optional USB gamepad; files from the phone or a USB stick.

## The steps

| Step | Skill |
| --- | --- |
| Study upstream, agree on shape, build form and names | `port-survey` |
| Repository, upstream submodule, skeleton | `port-repo` |
| Makefile: host, sanitizer and head-unit builds | `port-build`, `port-qnx-fixes` |
| Without the QNX SDP: a module, or a guest for GPL or LGPL code | `port-module` |
| The application itself | `port-libretro`, `port-native` or `port-companion` |
| Controls | `port-controls` |
| Package description and health check | `port-package` |
| Tests | `port-tests` |
| Licence and notices | `port-licence` |
| CI and signed releases | `port-release` |
| Car test | `port-car-test` |
| Speed in the car | `port-perf` |
| Upstream changes and pin moves | `port-upstream` |

## References

- [api.md](api.md): the SDK's API.
- [native-apps.md](native-apps.md): modules, guests and `nbt-runner`.
- NBTFoundry/manager: `docs/app-bridge.md` (the bridge and its capabilities),
  `docs/files-bridge.md`, `docs/gamepad-bridge.md`, `docs/controller-input.md`,
  `docs/manager-release.md` ("CI releases"), `.github/workflows/app-release.yml`.
