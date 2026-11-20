---
name: port-controls
description: Map an application's controls for NBT head units - the iDrive controller (always), USB gamepads by pad family, and the desktop keyboard - with the SDK's pad profiles, following NBTFoundry's layout conventions. Use when porting or changing an app's input.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# Controls

Example: nf-snes9x `src/controls.cpp` (a `frontend::Profiles`) and its README
"Controls" table. API: `nbt/frontend/pad_profiles.hpp`, `nbt/app/back_gesture.hpp`,
`nbt/controller/input.hpp`, `nbt/gamepad/input.hpp`.

Conventions:

- **Everything works with the iDrive controller alone.** A gamepad is optional.
- **By position, not by label:** each pad family's buttons sit where the
  original controller has them; the bottom face button is the original's
  bottom button on every pad. One `PadProfile` per family, a
  `ControllerProfile` for the iDrive controller, a `KeyboardProfile` for the
  desktop build.
- **iDrive controller:** knob detents as pulses (turn, or held shoulders);
  push confirms; BACK tap goes back; BACK held opens the menu; BACK long held
  (1.4 s) quits. Never bind anything to BACK held 2 s or more: that is the
  manager's escape. MENU opens the pause menu; OPTION, MEDIA, NAV and the
  favourite keys are free for the app.
- **Menus:** D-pad, left stick, knob or tilts move; bottom button or push
  selects; right button or BACK goes back; shoulders page by ten.
- **Names as printed** on the pad in use ("Cross", "Push"): `nbt/frontend/hints.hpp`.
- A pad plugged in or removed shows a notice; the controller keeps working.

Steps:

1. List upstream's actions; group them: play, menu, system.
2. Write the profiles in `src/controls.cpp`, one table each.
3. A unit test drives each profile through `PadMapper` and checks the actions
   (nf-snes9x `tests/controls_test.cpp`).
4. README "Controls": one row per action, a column per pad family, the
   iDrive controller and the desktop keyboard.
5. Declare `controller` in `--bridge-uses` and `gamepad` in `--bridge-optional`
   (port-package).
