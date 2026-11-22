---
name: port-licence
description: Licences and notices for an application ported to the NBT App SDK - our licence, upstream's terms, LICENSES/, the generated legal/NOTICE.txt, README licence section, GPL and non-commercial rules. Use when setting up or changing an nf-* repository's legal files.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# Licence and notices

Examples: nf-snes9x (custom non-commercial core), nf-android-auto (PolyForm
Noncommercial with a Required Notice).

1. **Our files:** MPL-2.0, unless they are combined with GPL code that needs
   a compatible licence (then GPL-2.0-or-later, as in
   https://github.com/NBTFoundry/nf-doom). `LICENSE` holds ours.
2. **GPL or LGPL upstream:** a guest only, never linked with QNX code
   (`sdk/docs/native-apps.md`, port-module; `tools/check_module.py` enforces
   it at link for the licence you declare). Read the header of every file the
   build compiles, not only upstream's main licence: one LGPL file makes a
   module wrong (Snes9x's `filter/snes_ntsc.c`; its module leaves it out and
   links a stand-in of ours, as the application never uses it).
   Write nothing else about QNX licensing.
3. **Non-commercial upstream:** the package as a whole is non-commercial.
   Say so in the README's License section and in the notice header.
4. `LICENSES/<SPDX-id>.txt`: the full text of every licence in the package
   (ours, upstream's, bundled libraries'). Custom licences as
   `LicenseRef-<Name>.txt`.
5. **`tools/make_notice.py` writes `legal/NOTICE.txt`:** a header (project,
   copyright, licence, source URL), the disclaimer, our licence, upstream's
   licence with every notice it requires (read from upstream's files, never
   retyped), bundled libraries, then the SDK's notices
   (`python3 ../sdk/tools/third_party_notices.py`). `make notice` runs it;
   run it again whenever a dependency or the pin changes.
6. `legal/README.md`: each component, its licence and where its text is.
7. **README License section:** copyright line, our licence, upstream's
   terms, artwork MPL-2.0, trademarks ("Not affiliated with or endorsed by
   BMW AG ..."; and upstream's owners where a name is a trademark).
8. **Data and assets** (games, firmware, fonts): ship only what the licence
   allows; otherwise the user supplies it through `files`.
9. If upstream's README limits who may bundle it, tell the user before any
   public release.
