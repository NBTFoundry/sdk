---
name: port-upstream
description: Keep an nf-* port in step with its upstream project - move the submodule pin to a new release, and carry changes upstream needs on an NBTFoundry fork branch until a pull request merges. Use for upstream updates and seams.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# Upstream: moving the pin, sending changes

## Moving the pin

```sh
git -C third_party/<name> fetch --tags origin
git -C third_party/<name> checkout <new tag>
make -j"$(nproc)" test test-asan check-static-init     # plus test-upstream, check-protocol
make module | guest | qnx qnx-check                       # the app's head-unit build; plus vm-test where it exists
make notice                                              # when upstream's licences or credits changed
```

Read upstream's changelog between the two tags first: new settings, files,
protocol words or dependencies change our tables and checks. Update the
README's upstream version, commit "Upstream <tag>", and a car test when
behaviour on the car may change.

## A change upstream needs

1. Make sure no seam already exists: read upstream's open pull requests.
2. On the NBTFoundry fork, branch from the pinned tag; make the smallest
   change in upstream's style, with upstream's tests.
3. Push the branch to the fork; point the submodule at it; build and test as above.
4. **No pull request before the car test passes** and the user agrees. Then
   open it from the fork, following upstream's template.
5. Once merged, pin upstream's next tag again.

Never edit upstream files in the submodule's working tree for our build:
fixes for our toolchain live in `src/compat/` (port-qnx-fixes).
