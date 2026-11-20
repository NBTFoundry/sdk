---
name: port-app
description: Port an existing application (game, emulator, libretro core, tool or daemon) to the NBT App SDK so NBT Manager installs and runs it on BMW NBT Evo head units. Start here; it orders the steps and names the skill for each.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# Port an application to the NBT App SDK

The result: a repository `NBTFoundry/nf-<name>`, next to `sdk` and `manager`,
that builds one signed package NBT Manager installs. Overview and rules:
`sdk/docs/porting.md`. Workstation values (toolchain, libraries, keys, kit
folder): the workspace's `CLAUDE.md`.

Do the steps in order. Each is a skill: read it when you reach it.

| # | Step | Skill | Done when |
| --- | --- | --- | --- |
| 1 | Study upstream; agree on shape, build form, names | port-survey | the user answered |
| 2 | Repository, submodule, skeleton | port-repo | first commit pushed |
| 3 | Makefile, host build | port-build | `make host` builds |
| 4 | The application | port-libretro, port-native or port-companion | it runs on the desktop |
| 5 | Controls | port-controls | every action reachable with the iDrive controller |
| 6 | Package description | port-package | `make package` and `nbt-verify` pass |
| 7 | Tests | port-tests | `make test test-asan check-static-init` pass |
| 8 | QNX build | port-build, port-qnx-fixes | `make qnx qnx-check` pass |
| 9 | Licence and notices | port-licence | `legal/NOTICE.txt` current |
| 10 | CI and release setup | port-release | `ci.yml` green, nothing tagged |
| 11 | Car test | port-car-test | the user reports results |
| 12 | Upstream changes, pin moves | port-upstream | after the car test only |
| any | The SDK, manager or phone app lacks something | port-sdk-gap | |

Throughout:

- Upstream stays unmodified in `third_party/`; submodules use SSH URLs.
- Nothing application-specific goes into sdk, manager or mobile-app.
- One source per fact: include or check upstream's lists and headers, never copy them.
- Ask the user before: the shape, build form and licence; creating a GitHub
  repository or fork; merging into `main`; pushing a tag; opening an upstream
  pull request; anything done in the car.
- Commit each completed step with a message saying what and why. Keep a
  memory note of the port's state and decisions.
- Report shortly: what works, what the user must do next.
