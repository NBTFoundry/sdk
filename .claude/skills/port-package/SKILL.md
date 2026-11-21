---
name: port-package
description: Describe and sign an NBT application package (release/package.args for the manager's tools/package.py) - app id, bridge version and capabilities, settings, files policy, versions and sequences - and check it with nbt-verify. Use when packaging a ported app.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# The package

`release/package.args` holds one `tools/package.py` argument per line (`#`
comments allowed). `make package` and the release workflow both pass it and
add the version, sequence, key, output and executable. Contract:
`manager/docs/app-bridge.md`.

```
--app
<id>
--compat
nbt_6.5_armv7
--bridge
1.5
--bridge-uses
show,controller
--bridge-optional
status,live-settings,gamepad
--setting
<key>|<Label>|<default>|<choice>,<choice>
```

- `--app`: the lowercase id from port-survey; never changes.
- `--bridge`: the bridge version, 1.5 or newer (1.6 for `hold`, `car-data`,
  `net`; 1.7 for `runner`). The manager does not start an application
  declaring an older one than 1.5.
- A module or guest (port-module) declares `--bridge 1.7` and `runner` in
  `--bridge-uses`, never optional: the manager's runner starts it. The
  packer refuses `runner` without the payload's NBT ABI note, and the note
  without `runner`.
- Capabilities (bridge 1.5): `show`, `status`, `live-settings`, `controller`,
  `gamepad`, `files`, `most`. `--bridge-uses`: those it cannot run without
  (`show` only if it draws on the manager's surface); `--bridge-optional`:
  those it uses when present. An empty value is written `--bridge-uses=` (the
  packer skips blank lines).
- `files` needs `--bridge-files ext,ext|<largest file MiB>|<quota MiB>`;
  `most` needs `--bridge-most` (`manager/docs/most-bridge.md`).
- Settings: at most 12; key `[a-z][a-z0-9_]{0,15}`; label up to 24
  characters; 1 to 6 choices of `[a-z0-9_-]{1,12}`; the default is one of
  them. The whole manifest stays under 2,048 bytes. The app reads them with
  `lifecycle.settings()` and treats an unknown value as the default.
- Version and sequence: tag `vX.Y.Z` gives sequence `X*1000000+Y*1000+Z`.
  The app's own numbers, not upstream's. Test packages before the first
  release: `0.0.N` with sequence `N`. The car refuses a sequence that does
  not rise.
- Health check: after install and update the manager starts the app with
  `NBT_LAUNCH=check`; it must report ready and exit 0 without screen or input.

Check:

```sh
make package APP_KEY=<test key> APP_VERSION=0.0.1 APP_SEQ=1
make -C ../manager host        # builds nbt-verify, if missing
../manager/build/nbt-verify build/<id>-0.0.1.nbt ../manager/release/apps-public.pem <id> nbt_6.5_armv7
```

The test key is the workspace's (`CLAUDE.md`); `nbt-verify` against
`apps-public.pem` passes only for packages signed with it.
