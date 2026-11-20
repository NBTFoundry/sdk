---
name: port-libretro
description: Port a libretro core (an emulator) to the NBT App SDK with libnbt-retro - the core linked statically, game list from the files folder, saves, states, pause menu, controls by pad family. Use for the application step of a libretro port.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# A libretro core on libnbt-retro

Example: nf-snes9x (`src/main.cpp`, `src/controls.*`). The SDK's
`tests/retro_app.cpp` is a complete small one.

1. **Build the core** from upstream's own libretro source list and defines
   (port-build), with `-D__LIBRETRO__`. Its objects link before
   `$(NBT_SDK_FRONT_*_LIBS)`; one core per application.
2. **`src/main.cpp`**: fill an `nbt::retro::HostConfig` and return
   `nbt::retro::run(argc, argv, config)`:
   - `tag` (log tag = app id), `title`, `system` (shown in the game list);
   - `extensions`: the files offered, lower case, no dot, `NULL`-ended; the
     same list as the package's `--bridge-files` (port-package);
   - `profiles`: the controls (port-controls); actions are
     `RETRO_DEVICE_ID_JOYPAD_*` of port 0;
   - `variables`: `VariableMap` rows feeding core variables from manager
     settings (setting value, core value pairs); a value the core does not
     list is never given to it;
   - `display_aspect` (4:3 for most consoles; 0 = the core's);
   - `max_file_bytes`: the largest game read into memory (a core with
     `need_fullpath` reads its files itself);
   - `system_in_files`: the core finds BIOS files in the files folder, where
     users put them (list their extensions in `--bridge-files`, not in
     `extensions`).
   Multi-disc games (`.m3u`) get a disc row in the pause menu when the core
   offers disc control; nothing to configure.
3. **Settings.** The host owns `aspect`, `filter`, `sound`, `frameskip`,
   `show_fps`, `debug_log`; declare those you keep, plus core settings, in
   `release/package.args` (12 at most). Check which core variables matter
   (`RETRO_ENVIRONMENT_SET_VARIABLES` in the core) and offer few.
4. **Limits of the host:** software rendering only; plain variables (no
   core options v2); battery saves in `<data>/saves/`, states in `<data>/states/`.
   A core needing more: port-sdk-gap.
5. **Check** on the desktop:
   `NBT_FILES_DIR=<games> NBT_DATA_DIR=<tmp> ./build/nbt-<name>-host`: the
   list shows, a game runs with sound, the menu saves and loads a state.
   `--game <file>` skips the list: the way to run a game in the VM, which has
   no input.
