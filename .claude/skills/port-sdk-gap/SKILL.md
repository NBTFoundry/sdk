---
name: port-sdk-gap
description: Add what a port needs to the NBTFoundry stack (SDK, NBT Manager, phone app) as a generic feature, never application-specific code. Use when an application being ported needs an API, capability, setting limit or tool the stack lacks.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# Adding to the stack

The SDK, the manager and the phone app never name an application. A port's
need becomes a feature any application could use, declared by the package
where it grants anything.

1. **Where it goes:**
   - an app-side API, helper or reader: the SDK;
   - a capability, car access, packaging, lifecycle: the manager (the
     bridge; `docs/app-bridge.md`), then the SDK's side of it;
   - something the user sets or sends from the phone: the phone app.
   A new or changed bridge capability: ask the user first.
2. **Compatibility:** bridge minors only add; existing applications run
   unchanged. Package fields and record formats change only with a version.
3. **A branch per repository**, named after the feature. Commit each step.
4. **SDK:** C++11 without exceptions, RTTI or static constructors; bounded
   buffers; a stub returning `ENOSYS` for modules where it cannot work there;
   a section in `docs/api.md`; tests in `tests/`. Run
   `make -j"$(nproc)" test test-asan check-static-init check-generic` and
   `make qnx qnx-check` (the NBT toolchain's QNX target). The bridge readers are
   the manager's copies: `make check-readers NBT_MGR_DIR=../manager`.
5. **Manager:** follow its `AGENTS.md`; `nice make -j"$(nproc)" pre-commit test`;
   document in `PROTOCOL.md` or `docs/`.
6. **Phone app:** its typecheck, tests and contract script.
7. **The app** builds against the branch while it is open (`NBT_SDK_DIR`);
   once the user merges it into the SDK's `main`, point `release/sdk.ref` at
   that commit.
8. A fix to a known build problem goes into port-qnx-fixes.
