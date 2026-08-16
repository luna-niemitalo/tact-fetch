# CLAUDE.md — tact-fetch

Global rules apply (`~/.claude/CLAUDE.md`). Nix conventions: `.claude/rules/nix.md`.
Read `DESIGN.md` before any structural change.

## Purpose

Politely fetch CASC files a local WoW install's manifest knows about but never
downloaded, straight from Blizzard's CDN, into a `tact_export/` tree.

## Status

- **Current**: the full pipeline is implemented and builds clean
  (`direnv exec . cmake --build build`). `tact-fetch <dry-run|fetch>
  --from-list <file> --install <path> [--export <dir>]` CLI
  (`src/main.cpp`). Local-only handle A (`src/casc_storage.cpp`):
  direct per-FileDataID resolution via `CascOpenFile`, no storage scan.
  Worklist loading/validation (`src/worklist.cpp`), bucketing
  (`src/plan.cpp`), dry-run report + freshness marker (`src/dry_run.cpp`),
  `.build.info` product/region parsing for handle B (`src/build_info.cpp`),
  synchronous append-only event log (`src/log.cpp`). The fetch step
  (`src/fetch.cpp`) opens a second, online CascLib storage handle
  (handle B) rooted at `TactFetchCacheDir()/online-cache/<product>-<region>`
  (`src/cache_dir.h` — central and persistent, `$XDG_CACHE_HOME/tact-fetch`
  or `$HOME/.cache/tact-fetch`, not per-invocation), fetches each
  `to_fetch` entry through it (retry/backoff/rate-limit per §5), and
  writes decoded output to `--export/_unresolved/`
  (`.encrypted`-postfixed if BLTE decode fails with
  `ERROR_FILE_ENCRYPTED`). Vendored CascLib carries a **local patch**
  (not upstream): `HttpDownloadFile` and `RibbitDownloadFile`
  (`vendor/CascLib/src/CascFiles.cpp`) fetch via libcurl (HTTP/2, a real
  `User-Agent`, a real timeout) instead of CascLib's raw-socket client;
  handle B is opened with `CASC_FEATURE_FORCE_DOWNLOAD` so the small
  `versions`/`cdns` files are re-checked every run (never silently
  stale) while the CDN's big content-addressed archive indices still
  only fetch once; `RibbitDownloadFile` snapshots every actual change to
  `versions`/`cdns` into `<cache-root>/_history/<name>.<unix-time>`
  before overwriting, giving real version history. Everything else in
  CascLib's fetch/decode/verify pipeline is untouched.
  Verified against the real install and real network (see `## Resume` for
  what that verification actually found — a locale-mask bug, fixed, and
  an architectural cost that Luna resolved by making the cache an
  intentional, persistent, versioned asset rather than something to
  avoid paying for).
- **Target**: see `DESIGN.md` §7/§9. Everything described above is
  built **and verified end to end against the real install and real
  CDN** (2026-08-16): a full, unbounded run completed the entire index
  bootstrap in ~2.5 minutes (399 MB, 1346 `.index` files for retail
  WoW/eu build 69299), fetched and correctly decoded the one targeted
  FileDataID (output byte-for-byte matches `casc-tool`'s independently
  reported size), and `_history/` gained exactly the one snapshot each
  for `versions`/`cdns` that a single fresh-cache run should produce.
  Only thing still open: a `--listfile` flag (not built — every fetch
  currently lands under `_unresolved/`, never a real resolved path).
- Anything not listed under Current does not exist yet. Do not describe it as working.

## Boundaries

- **Local CASC storage** (read-only, vendored CascLib): same access
  pattern as `casc-tool`, never writes back. Handle A
  (`src/casc_storage.cpp`): `CascOpenStorage` (no `CASC_FEATURE_ONLINE`).
- **Central cache dir** (`TactFetchCacheDir()/online-cache/<product>-<region>`,
  `src/cache_dir.h` — persistent, not per-invocation): handle B's entire
  cache root. This is where every byte handle B's online mode reads or
  writes lives — never the real install.
- **Blizzard's public CDN** (network, read-only `GET` only, via the
  locally-patched `HttpDownloadFile`/`RibbitDownloadFile`): the CDN
  host/path/product/region come from the local install's own
  `.build.info` (`src/build_info.cpp`), never invented. CascLib's own
  BLTE decode + CKey/EKey hash verification is the trust boundary for
  "is this fetch actually correct" — see `DESIGN.md` §9.
- **`--from-list` worklist files** (plain text, one FileDataID per line,
  `casc-tool`'s own convention): foreign data, validated at load
  (`src/worklist.cpp`).
- No other boundaries. This tool never writes to the source WoW install/CASC
  directory, never accepts arbitrary URLs, never proxies for another operator.

## Resume

- **Last state** (2026-08-16): the full pipeline was built and
  live-tested against a real install (`/media/luna/games/World of
  Warcraft`) and the real CDN in the same session. Three real findings,
  not implementation nitpicks — full detail in `DESIGN.md` §9 and
  `CHANGELOG.md`, summarized here:
  1. **Bug, found and fixed**: handle A's `dwLocaleMask=0` silently
     resolved locale-flagged files to the wrong CKey entry (`0` is
     `CASC_LOCALE_NONE`, not "unfiltered"). Fixed to `CASC_LOCALE_ALL`;
     re-verified against `casc-tool`'s own output as ground truth, now
     matches exactly.
  2. **Design gap, found and resolved by Luna**: CascLib's public API
     can't report a missing file's size/encryption status pre-fetch
     (`CascGetFileInfo(CascFileFullInfo)` requires the local data
     stream, which doesn't exist for a file never downloaded).
     Resolution: detect encryption *post*-fetch; a file that fails BLTE
     decode with `ERROR_FILE_ENCRYPTED` lands at its real path with
     `.encrypted` appended to the full filename (extension included, so
     extension-matching consumers skip it automatically), not a
     separate subtree, never silently dropped.
  3. **Real cost, found live-testing, resolved by Luna the same day**:
     opening handle B (`CascOpenStorageEx(bOnlineStorage=true)`)
     triggers `LoadIndexFiles` → `LoadArchiveIndexFiles`, which
     downloads the CDN's **entire archive index set** before the handle
     finishes opening — completely independent of worklist size. A live
     test for a single FileDataID was still pulling individual `.index`
     files (463 of them, 41 MB, interrupted deliberately before
     completion — the real total is unknown, plausibly much larger)
     minutes in. Luna's call: this isn't wasted bandwidth to minimize,
     it's the same kind of local index a real CDN client keeps to know
     what's available and diff against later — worth keeping
     deliberately rather than avoiding. Resolution (`DESIGN.md` §9):
     `src/cache_dir.h`'s `TactFetchCacheDir()` makes the cache central
     and persistent (`$XDG_CACHE_HOME/tact-fetch`, namespaced by
     product/region) instead of per-scratch-dir, so the expensive part
     is paid once, ever, not once per run; handle B now opens with
     `CASC_FEATURE_FORCE_DOWNLOAD` so the small `versions`/`cdns` files
     (not content-addressed, unlike everything else CascLib caches) get
     re-checked every run instead of trusted forever; the
     `RibbitDownloadFile` patch snapshots every actual change to those
     two files into `_history/` before overwriting, so there's a
     permanent, timestamped record of every build/CDN-config change
     ever observed, not just whatever's currently live.
  4. **Verified end to end (2026-08-16)**: a full run (watched
     externally, capped at "interrupt past 3 GiB" — never triggered)
     against the real install and real CDN completed the entire index
     bootstrap in ~2.5 minutes: **399 MB, 1346 `.index` files** total
     for retail WoW/eu build 69299 — large, but well short of the
     multi-GB worst case finding 3 left open. The one targeted
     FileDataID (21) fetched, decoded, and hash-verified correctly:
     output is exactly 4,609,024 bytes, matching `casc-tool`'s
     independently-reported size byte for byte. `_history/` gained
     exactly one snapshot each for `versions`/`cdns` (correct for a
     single run against a fresh cache). Real install confirmed
     untouched before and after (`find -newer`, empty both times).
- **Next step**: no open findings right now. Natural next pieces if
  picked back up: a `--listfile` flag (so fetched files can land at
  their real resolved path instead of always `_unresolved/`), and
  actually exercising `_history/`'s accumulation behavior by running
  `fetch` again after a real CDN change (a new WoW build/patch) to
  confirm a second snapshot appears rather than none.
- **Hazards**: `vendor/CascLib` is **not a git submodule** (removed 2026-08-16, was
  redundant with the flake input once a local patch needed keeping in
  sync across two places): it's materialized from the pinned `casclib`
  flake input + `vendor/patches/casclib-libcurl-fetch.patch` by
  `devShells.default`'s shellHook (local dev) and `packages.default`'s
  `postUnpack` (`nix build`), both in `nix/flake.nix`. If you edit
  `vendor/CascLib/src/CascFiles.cpp` directly, that edit is **local and
  disposable** — the shellHook will overwrite it back to
  flake-input-plus-patch the next time its marker check decides to
  re-materialize (patch file changed, or the directory's gone). Change
  the *patch*, not the checked-out file, and regenerate it (command in
  `vendor/patches/README.md`) or the change won't survive. §5's
  politeness policy lives in `src/fetch.cpp`'s retry/backoff/rate-limit
  loop, not inside the patched CascLib functions themselves (those stay
  thin, single-shot transport swaps) — keep it that way if this gets
  touched again.
