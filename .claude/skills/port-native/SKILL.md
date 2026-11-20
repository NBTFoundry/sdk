---
name: port-native
description: Port a program with its own platform layer (SDL, GLUT, i_*/sys_* files) to the NBT App SDK by writing that layer on the SDK - lifecycle, window and GL, sound, input, files, status. Use for the application step of a platform-layer port.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# A platform layer on the SDK

Example: https://github.com/NBTFoundry/nf-doom (`src/i_*_nbt.cpp`, `src/*_compat/`).
API: `sdk/docs/api.md`.

Replace upstream's platform files with ours in `src/`, compiled instead of
upstream's (leave them out of the source list; never edit them). Stubs for
headers upstream includes (SDL, GL) go in `src/<upstream>_compat/`.

1. **Start-up** (`main`): `log_init(tag)`, `Lifecycle::init(argc, argv)`
   (return its code), `log_install_crash_handlers()`.
   - `is_check_mode()`: the health check. Load what can be checked without
     the screen (data, settings), `send_ready()`, exit 0. Never open the window.
   - User mode: `VideoEgl::init(lifecycle.surface_id())`, then `send_ready()`
     within 5 s of start.
2. **The loop**, once per frame: poll input, run, draw, `video.swap()`,
   `lifecycle.tick(now, status, &changed)` (heartbeats must be under 3 s
   apart: keep long loads in steps), apply settings when `changed`, leave
   when `stopping()`. `consume_show_requested()` after SIGUSR1.
3. **Video:** GLES2 directly; desktop GL 1.x through `nbt/gles2/gles2_compat.h`
   in place of `GL/gl.h` (`gles2_batch_init()` after the context exists); a
   software frame through `frontend::FrameVideo`. Overlays and menus:
   `frontend::Overlay`, `frontend::Menu`.
4. **Sound:** `AudioPcm::open(rate, render, ctx, route_wanted)` pulls
   samples from its own thread; `frontend::AudioQueue` when upstream pushes;
   `AudioMixer` for sample channels and music. The car's audio connection is
   `AudioRoute`'s job.
5. **Input:** port-controls.
6. **Files:** configuration and saves in `data_dir()` (whole-file writes:
   `nbt/frontend/save_file.hpp`); user files in `files_dir()` (`FileList`); data that
   ships with the app embedded in `.rodata` only if its licence allows.
7. **Status line:** short and useful (`"E1M1 H:100%"`, the game played); `tick()` sends it.
8. **Log:** stderr lines become the app's log; `log_stage()` names where a
   crash happened; a `debug_log` setting turns on tracing.
9. **Check** on the desktop: `NBT_DATA_DIR=<tmp> ./build/nbt-<name>-host`
   plays with sound; `NBT_LAUNCH=check` exits 0 without a window.
