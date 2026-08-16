# vendor/patches

`vendor/CascLib` is a git submodule (upstream, unmodified in its checked-out
commit) — but the working tree checked out at build time carries one local
modification that never goes upstream. `casclib-libcurl-fetch.patch` is that
modification, captured here so it survives outside the submodule's own
working tree (a `git submodule update` would otherwise silently discard it,
and `nix build`'s `packages.default` fetches CascLib fresh from the
`casclib` flake input, never from this repo's own submodule checkout, so it
needs the patch applied explicitly too — see `nix/flake.nix`'s `postUnpack`).

## What it does

Rewrites `HttpDownloadFile` and `RibbitDownloadFile`
(`vendor/CascLib/src/CascFiles.cpp`) — the only two places in CascLib that
open a network connection — to fetch via libcurl (HTTP/2, TLS, a real
`User-Agent`, a real timeout) instead of CascLib's own raw-socket HTTP/1.1
client. See `DESIGN.md` #9's fetch-pipeline resolution for the full
reasoning: this is the smallest patch that lets `tact-fetch` reuse CascLib's
BLTE decode and CKey/EKey hash verification pipeline unmodified while
getting real transport-level politeness (DESIGN.md #5) instead of CascLib's
anonymous, unpaced, no-timeout client.

## For local dev (git submodule checkout)

The patch is already applied directly to `vendor/CascLib`'s working tree —
`cmake --build` picks it up as-is. If the submodule is ever reset or
re-cloned (`git submodule update`, a fresh checkout), reapply it:

```
git -C vendor/CascLib apply ../patches/casclib-libcurl-fetch.patch
```

If you change `CascFiles.cpp` again, regenerate the patch file to keep it in
sync (this file is the single source of truth for the Nix build, not the
submodule's own working-tree state):

```
git -C vendor/CascLib diff -- src/CascFiles.cpp > vendor/patches/casclib-libcurl-fetch.patch
```

## For `nix build` (flake input)

`nix/flake.nix`'s `packages.default` applies this same patch in its
`postUnpack` phase, right after copying the `casclib` flake input's source
into `vendor/CascLib/` and before `cmake` configures — so the packaged
build gets the identical patched behavior without needing the git submodule
at all.
