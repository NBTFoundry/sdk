---
name: port-release
description: Set up CI and signed releases for an nf-* application repository - ci.yml, release.yml calling the manager's app-release.yml, the GitHub release environment and hardening, the apps signing secret, build-image access - and cut a release when the user asks.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# CI and signed releases

Reference: nf-android-auto `.github/workflows/`, manager
`.github/workflows/app-release.yml` (its header comment), manager
`ci/qnx/README.md`.

1. **`ci.yml`** (copy nf-fbneo's for a guest, nf-snes9x's for a module,
   nf-android-auto's for a native program): on push to `main` and pull
   requests, check out the app with submodules and the SDK at
   `release/sdk.ref`, install the host packages, run `make test`,
   `test-asan`, `check-static-init` (and `test-upstream`), and build the
   head-unit form after `make -C sdk toolchain`. Pin every action by commit
   SHA with the version in a comment.
2. **`release.yml`** (copy the same repository's): on `v*` tags, a `pins` job
   reads `release/sdk.ref`, then
   `uses: NBTFoundry/manager/.github/workflows/app-release.yml@<manager commit>`
   (the latest manager release's commit) with `secrets: inherit`,
   `qnx: false`, `build: make -j"$(nproc)" guest` (or `module`, or
   `qnx qnx-check` for a native program) and `payload: build/nbt-<name>.guest`
   (or `.so`, or `-qnx`): the job installs the SDK's toolchain and needs no
   build image (skip steps 5-6). A build that embeds Git LFS files adds
   `lfs: true` (https://github.com/NBTFoundry/nf-doom).
3. **GitHub settings**, same as the other nf-* repositories (`R=NBTFoundry/nf-<name>`):
   ```sh
   gh api -X PUT repos/$R/environments/release --input - <<'EOF'
   {"reviewers":[{"type":"User","id":<maintainer id: gh api users/<login> --jq .id>}],
    "prevent_self_review":false,"can_admins_bypass":false,
    "deployment_branch_policy":{"protected_branches":false,"custom_branch_policies":true}}
   EOF
   gh api -X POST repos/$R/environments/release/deployment-branch-policies -f name='v*' -f type=tag
   gh api -X POST repos/$R/rulesets --input - <<'EOF'
   {"name":"Release tags","target":"tag","enforcement":"active",
    "conditions":{"ref_name":{"include":["refs/tags/v*"],"exclude":[]}},
    "rules":[{"type":"creation"},{"type":"update"},{"type":"deletion"},{"type":"non_fast_forward"}],
    "bypass_actors":[{"actor_id":5,"actor_type":"RepositoryRole","bypass_mode":"always"}]}
   EOF
   gh api -X PUT repos/$R/actions/permissions -F enabled=true -f allowed_actions=selected -F sha_pinning_required=true
   gh api -X PUT repos/$R/actions/permissions/selected-actions --input - <<'EOF'
   {"github_owned_allowed":true,"verified_allowed":false,
    "patterns_allowed":["NBTFoundry/manager/.github/workflows/app-release.yml@*"]}
   EOF
   gh api -X PUT repos/$R/actions/permissions/fork-pr-contributor-approval -f approval_policy=all_external_contributors
   gh api -X PATCH repos/$R --input - <<'EOF'
   {"security_and_analysis":{"secret_scanning":{"status":"enabled"},"secret_scanning_push_protection":{"status":"enabled"}}}
   EOF
   ```
   Compare with `nf-snes9x` (`gh api repos/NBTFoundry/nf-snes9x/...`) when in doubt.
4. **The apps signing key**, as an environment secret only (no repository
   secrets: `secrets: inherit` would pass them too). Where the key is: the
   workspace `CLAUDE.md`. Never print it or write it to disk: check it pairs
   with `manager/release/apps-public.pem`
   (`openssl pkey -pubout | cmp - ../manager/release/apps-public.pem`), then
   pipe it `| base64 -w0 | gh secret set APPS_SIGNING_KEY --env release -R $R`.
5. **Build image access** (only for `qnx: true`, which no application uses today): the user grants the repository read access in the
   `qnx65-build` package settings ("Manage Actions access"); there is no API.
   Ask them.
6. **Build image contents:** in the manager,
   `QNX_SCREEN_LIBDIR=… RHMI_LIBDIR=… ci/qnx/trace.sh <every nf-* app dir>`.
   If `ci/qnx/files.txt` changes, the image needs a rebuild and push
   (`ci/qnx/build-image.sh --push`, the new digest in the manager's
   workflows); that is the user's call, and the apps then pin the new
   manager commit.
7. **Release** only when the user asks: tag `vX.Y.Z` on `main`, push it; the
   user approves the sign job under the run's "Review deployments"; the
   release gets `<id>-X.Y.Z.nbt`. Tags are never moved or deleted.
