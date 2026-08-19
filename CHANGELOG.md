# tact-fetch: changelog

Resolved entries move here out of `FAILURES.md` once fixed/closed, so that
file stays a punch list (what's still outstanding) instead of an
ever-growing archive. This file is the archive — historical record only,
nothing here is actionable. Newest first.

---

## 2026-08-19 — `--locale` flag added; encrypted-tail truncation fixed

Two real bugs, both found by the `husk` session live-testing a real
fetch of 3 real FileDataIDs (character customization name tables:
`ChrCustomizationOption`/`Choice`/`Category.db2`), both fixed the same
day. Full technical detail in `DESIGN.md` §9; summary here:

1. **Silent tail truncation, fixed**: `FetchOneFile` (`src/fetch.cpp`)
   now always opens with `CASC_OVERCOME_ENCRYPTED`, matching `casc-tool`'s
   own prior fix for the identical symptom -- an unrecoverable
   encrypted tail block used to silently shrink `ContentSize` instead of
   erroring, with no way to tell "genuinely small" from "truncated" from
   outside CascLib. Verified directly against the real 3 files: sizes
   went from a silently-short 30893/329311/2803 bytes to the manifest's
   real 31325/330518/2839, tail zero-filled and visible, not dropped.
2. **`--locale <code>` added** (`src/locale.{h,cpp}`, new CLI flag in
   `main.cpp`, default `all` -- existing behavior unchanged unless
   passed): threads a real `CASC_LOCALE_*` mask through to `RunFetch`'s
   handle B (the handle that actually fetches bytes), which previously
   had no locale control at all -- it was hardcoded to `CASC_LOCALE_ALL`.
   Root cause of "locale override does nothing" from an earlier same-day
   investigation: CascLib's WoW root handler filters by locale
   independently *per storage handle*, at root-parse time
   (`CascRootFile_WoW.cpp:450`) -- handle A (local resolve,
   `casc_storage.cpp`, deliberately still `CASC_LOCALE_ALL`: its job is
   "known to the manifest at all," not locale selection) and handle B
   are genuinely separate knobs, so an earlier fix attempt that only
   touched handle A had zero effect on what got fetched. Verified
   directly: `--locale enUS` against the same 3 FileDataIDs produced
   different bytes than the (Korean-landing, for these particular IDs)
   default, and the decoded strings are real English ("Skin Color",
   "Hair Style", "Tattoo Style", ...).

## 2026-08-17 — Extension content-sniffing added; listfile resolution scoped to `casc-tool` for good

`src/content_sniff.{h,cpp}` added: fetched files under `_unresolved/`
now get a real extension when the content is recognizable
(`.m2`/`.blp`/`.skin`/`.db2`/`.ogg`/`.avi`) instead of always `.dat` --
the decoded, hash-verified bytes are already in hand right before
writing, so checking a magic signature costs nothing extra. Every
signature was verified against a real file extracted from the real
install first, not taken from documentation (`MD21`/`MD20`, `BLP2`,
`SKIN`, `WDC*`, `OggS`, `RIFF`+`AVI `). Deliberately skips `.wmo`/`.adt`/
`.wdt`/`.wdl`: all four share the same leading `MVER` chunk, so a
magic-only check can't disambiguate them -- staying `.dat` beats a
confident wrong guess. Live-verified with one real fetch (FileDataID
1047886 → `FILE000FFD4E.ogg`, content confirmed to actually start with
`OggS`).

Also: FileDataID→real-path resolution, previously described as "a
future tact-fetch `--listfile` flag, not built yet," is now explicitly
scoped to `casc-tool` instead -- a separate pass over `_unresolved/`
using its own exploration tooling, never a tact-fetch feature. Closes
that open question rather than leaving it open-ended.

Generated a full missing-files report for Luna's own analysis
(`development/missing_report.csv`, gitignored, 156,299 rows) by
cross-referencing a fresh local scan against a local community listfile
CSV. Found and fixed a real data-quality bug in generating it: the
listfile has CRLF line endings that silently leak a trailing `\r` into
every joined field -- invisible when printed, but breaks exact-string
filtering (`$5=="ogg"` silently matching nothing). Regenerated with `\r`
stripped. The report itself surfaced something worth recording even
though it's not a code change: this install's missing set is ~87%
locale audio and cinematics, essentially zero missing models/creature
content -- tact-fetch's fetch list isn't a fix for husk's texture-gap
problem, that content is either already local or genuinely encrypted,
not "not yet downloaded."

## 2026-08-17 — `_unresolved/` output nesting restored

Reverted the previous entry's naming change on the same day, per Luna:
a CASC/BLTE-decoded blob named only by FileDataID isn't a real game
asset yet -- it's still a "casc-like" intermediate, not a `.m2`/`.blp`/
etc. the way a real extraction is. It belongs under
`tact_export/_unresolved/`, matching `casc-tool`'s own convention;
`tact_export/`'s real tree should only ever be populated by resolving a
FileDataID to its actual in-game path (a future `--listfile`-driven
extraction step, not built yet), never by CascLib's own decode alone.

## 2026-08-17 — Whole-archive fetch found and fixed with real HTTP Range support, verified per-file

Ran a 400-ID random-sample fetch smoke test as requested. First had to
fix the test input: `casc_missing_fileids.txt` (provided as the source
list) turned out stale -- a fresh scan confirmed only 1 of its 18,747
IDs was still actually missing, the rest had since been downloaded by
the client. Wrote a small one-off scanner (not part of tact-fetch, which
deliberately never scans) to get a current, ground-truth missing-files
list (156,299 entries, matching `DESIGN.md` #2's original count exactly)
and sampled 400 from that instead.

The smoke test surfaced a second real architectural cost, bigger than
the index-bootstrap one: `FetchCascFile` fetches the *entire* CDN
archive (confirmed at exactly 268,435,456 bytes -- 256 MiB) that a
requested file happens to be packed into, not just that file's own
bytes. Confirmed in the unpatched vendored source (`CascFiles.cpp`): the
archive-info overload substitutes the archive's own key for the
requested file's key before ever fetching, always whole-file. True of
CascLib before this project touched it, not something the libcurl patch
introduced.

Resolution (Luna's call, with an explicit instruction to validate before
trusting it): `FetchRemoteArchiveRange`, a new function in the same
CascLib patch, issues a real `CURLOPT_RANGE` request for exactly the
needed span, verifies the response is genuinely partial (HTTP 206 and
the exact requested byte count -- a server that ignores `Range` and
answers `200` with the full body is treated as a hard failure, not
silently accepted), and writes the fetched bytes into a sparse local
file at the correct offset with a `.ranges` sidecar tracking what's
already been pulled into that archive proxy. No whole-file fallback on
Range failure.

Verified per-file, twice, not just asserted from the patch's intent: 12
freshly-created archive-proxy files totaled 504 KB of actual disk usage
(`du`, real filesystem blocks) against ~1.07 GB of apparent/logical size
(the offset-correct sparse-file size) -- individually confirmed, not
just in aggregate (a naive whole-directory `du` delta was initially
misleading, conflating new growth with ~2.9 GB of already-existing
archives left over from before this patch existed in the same
persistent cache). A second run confirmed the same pattern (9 new
archives, 489 KB actual).

Side effect, also fixed: `src/fetch.cpp`'s encrypted-file raw-bytes
preservation was looking for cached content at the file's own EKey path,
but the cache has always been keyed by the archive's key -- it could
never have worked, just never got exercised until this session's smoke
test actually hit encrypted files. Simplified to an honest empty
`.encrypted` marker.

`vendor/patches/casclib-libcurl-fetch.patch` regenerated and verified
byte-identical after a clean re-apply; both `cmake --build` and
`nix build ./nix#default` rebuilt clean from scratch.

## 2026-08-16 — Full pipeline verified end to end against the real install and real CDN

Ran `fetch` for real, watched externally with a deliberate "interrupt if
the cache exceeds 3 GiB" cap (never triggered) rather than an unbounded
live test. Results:

- The index bootstrap (previous entry's open question) completed in
  ~2.5 minutes: **399 MB, 1346 `.index` files** for retail WoW/eu build
  69299 — substantial, but well short of the multi-GB worst case the
  earlier interrupted test left open.
- The one targeted FileDataID (21) fetched, decoded, and hash-verified
  correctly through CascLib's own unmodified pipeline: output
  (`_unresolved/FILE00000015.dat`) is exactly 4,609,024 bytes, matching
  `casc-tool`'s independently-reported size for the same ID byte for
  byte.
- `_history/` gained exactly one snapshot each for `versions` and
  `cdns` — correct for a single run against a freshly-created cache.
- The real install directory was confirmed untouched both before and
  after (`find -newer` against a pre-session file, empty result both
  times).

This closes out the local-patch and central-cache work as verified, not
just build-tested. Committed to `master` locally (not pushed).

## 2026-08-16 — Index-bootstrap cost reframed as a persistent asset, not overhead to avoid

Luna's resolution to the previous entry's open finding: the CDN archive
index set `LoadIndexFiles` downloads on every online-storage open isn't
wasted bandwidth to minimize, it's the same kind of local index a real
CDN client keeps to know what's available and diff against later — worth
keeping deliberately, centrally, and with history, not avoiding.

- `src/cache_dir.h`/`.cpp` added: `TactFetchCacheDir()`
  (`$XDG_CACHE_HOME/tact-fetch`, falling back to `$HOME/.cache/tact-fetch`)
  is now handle B's cache root (`src/fetch.cpp`), namespaced by
  `<product>-<region>`, replacing the old per-invocation
  `state_dir/online-cache`. The expensive index bootstrap is paid once,
  ever, per product/region — not once per run.
- `src/fetch.cpp` now opens handle B via `CascOpenStorageEx` directly
  (not the `CascOpenOnlineStorage` wrapper) with
  `CASC_FEATURE_FORCE_DOWNLOAD` set, so the small, non-content-addressed
  `versions`/`cdns` files are re-checked every run instead of trusted
  forever once cached (CascLib's own `LocalCaching()` default) — the
  CDN's big content-addressed archive indices are unaffected and still
  only fetch once.
- The `RibbitDownloadFile` patch (`vendor/CascLib/src/CascFiles.cpp`)
  now compares each freshly-fetched `versions`/`cdns` against what's
  currently cached, and if it changed, snapshots the previous content
  into `<cache-root>/_history/<filename>.<unix-time>` before
  overwriting — a permanent, timestamped record of every build/CDN-config
  change this tool has ever observed for that product/region.
- Patch regenerated (`vendor/patches/casclib-libcurl-fetch.patch`)
  against the pristine `casclib` flake input and verified
  byte-identical after a clean re-apply. Full pipeline rebuilt from
  scratch via both `cmake --build` and `nix build ./nix#default`;
  non-network CLI/dry-run smoke tests re-passed. **Not yet run against
  the real CDN** — the live test that found the original index-bootstrap
  cost was interrupted before this resolution existed; confirming it
  behaves as designed (central location used, `_history/` actually
  accumulates on change) needs one more live run.

## 2026-08-16 — `vendor/CascLib` git submodule removed, replaced by the flake input

Once the libcurl patch needed keeping in sync across two separate CascLib
copies (the git submodule's working tree, patched by hand; the `casclib`
flake input, patched by `packages.default`'s `postUnpack`), maintaining
both was pure duplication with no benefit — `flake.lock` already pinned
`casclib` to the exact same commit the submodule had. Removed the
submodule (`git submodule deinit`, `.gitmodules` gone,
`.gitignore`d `vendor/CascLib/`); `devShells.default`'s shellHook now
materializes `vendor/CascLib` from the `casclib` flake input +
`vendor/patches/casclib-libcurl-fetch.patch` on entry (a marker file
keyed to the input path + patch hash skips redoing it when nothing
changed), mirroring exactly what `postUnpack` already did for `nix
build`. Verified both paths produce a working, patched build:
`direnv exec . cmake --build build` and `nix build ./nix#default`.

## 2026-08-16 — Fetch pipeline implemented and live-tested; one real cost found, not yet resolved

Built the rest of the tool: `src/fetch.cpp` now really fetches (handle B,
`CascOpenStorageEx(bOnlineStorage=true)`, `szLocalPath` = scratch dir,
retry/backoff/rate-limit per `DESIGN.md` §5), writing decoded output to
`--export/_unresolved/` (`.encrypted`-postfixed on BLTE decode failure,
per the convention decided the same day). `src/build_info.cpp` reads
`.build.info`'s product/region for handle B. Vendored CascLib gained a
**local patch** (`vendor/patches/casclib-libcurl-fetch.patch`, applied to
the git submodule's working tree for local dev and via the Nix flake's
`postUnpack` for `packages.default`): `HttpDownloadFile` *and*
`RibbitDownloadFile` (`CascFiles.cpp`) now fetch via libcurl (HTTP/2,
TLS, a real `User-Agent`, a real timeout) instead of CascLib's raw-socket
client. `RibbitDownloadFile` wasn't in the original `DESIGN.md` #9 plan —
found necessary only by live-testing: it's the *other* raw-socket fetch
function, used for the one-time versions/cdns bootstrap every online
storage needs before `HttpDownloadFile` is ever reachable.

Live-verified against the real install and the real CDN (`us.patch.battle.net:1119`,
`level3.blizzard.com`, product `wow` region `eu`): DNS, TCP, libcurl fetch,
`SaveLocalFile`, and the scratch-dir isolation all work exactly as
designed. **Found live, not yet resolved**: `CascOpenStorageEx`'s
`LoadIndexFiles` step downloads the CDN's entire archive index set before
the handle finishes opening, regardless of worklist size — a single-file
test was still pulling `.index` files (463 of them, 41 MB, stopped
deliberately before completion) minutes in. One-time cost per scratch-dir
lifetime, but large and disproportionate to a small `--from-list`; not a
bug in the libcurl patch (CascLib's own online-storage design), but a real
tension with "fetch exactly this small list, politely" that `DESIGN.md`
§9 now documents as open. No further live-CDN testing until this is
resolved.

## 2026-08-16 — Verified against a real install: locale-mask bug found and fixed, encryption-detection gap found

Tested the local-only pipeline end to end against a real WoW install
(`/media/luna/games/World of Warcraft`, read-only) for the first time,
cross-checked against `casc-tool`'s own `info` command as ground truth.
Two real findings, not implementation nitpicks:

- **Bug, fixed**: `LocalStorage::OpenReadOnly` opened storage with
  `dwLocaleMask=0`. `0` is `CASC_LOCALE_NONE`, not "unfiltered" --
  passing it silently corrupted which locale-variant CKey entry the root
  handler resolved for locale-flagged files. Concretely: FileDataID 21,
  which `casc-tool` correctly reports as "known but not available
  locally", resolved through the buggy code as already-local with a
  *different* content size than `casc-tool` reported for the same ID --
  the wrong CKey entry entirely. Fixed to `CASC_LOCALE_ALL`, matching
  `casc-tool`'s own default; re-verified against the same real install,
  now matches `casc-tool` exactly across all four FileDataIDs tested (one
  missing-locally, two already-local, one not-in-manifest).
- **Design gap, not yet resolved**: found while fixing the above --
  `CascGetFileInfo(CascFileFullInfo)` (content size + content flags) is
  gated behind `EnsureFileSpanFramesLoaded`, which requires opening the
  local data-archive stream. That fails for any file this install never
  downloaded, meaning content size and encryption status are structurally
  unavailable pre-fetch for exactly the files this tool exists to fetch.
  `DESIGN.md` §7 step 2 written up with the full finding and two
  candidate resolutions (post-fetch encryption detection vs. a small
  CascLib patch exposing the cheap, stream-independent flags lookup that
  already exists internally); not decided yet. `PlanSummary` gained
  `to_fetch_unknown_size` so the dry-run report says "unknown" instead of
  silently implying 0 bytes / not-encrypted for those entries.
- **Design gap resolved by Luna, same day (refined same day)**: detect
  encryption post-fetch, not pre-fetch. Every `to_fetch` entry gets
  fetched regardless of unknown flags; when CascReadFile's BLTE decode
  surfaces `ERROR_FILE_ENCRYPTED` afterward, the file lands at its real
  path with `.encrypted` appended to the filename (`character/foo.m2` ->
  `character/foo.m2.encrypted`) rather than under a separate subtree --
  postfixing the whole real filename, extension included, so
  extension-matching consumers (globbing `*.m2`) skip it automatically
  without needing to know the convention exists, while it's still
  sitting right next to where the real file would go for a human to
  find. Gets its own loud result value in the log and run summary, not
  folded into "fetched" or "failed", and is never silently dropped. See
  `DESIGN.md` §7 step 2 for the full writeup.

## 2026-08-16 — Local-only pipeline implemented; `--from-list` made mandatory; §9 pacing follow-up resolved

Three related changes:

- **`--from-list` is now the only worklist mode.** The full-storage-scan
  fallback described in the original `DESIGN.md` §7 step 2 was never what
  was wanted — this is a companion tool that fetches a caller-named list
  (typically the output of a prior `casc-tool` dry-run), not a scanner
  that discovers its own scope. `DESIGN.md` §3/§5/§7 and `CLAUDE.md`
  updated; no code ever implemented the old fallback, so nothing to
  remove there.
- **§9's pacing/identity follow-up resolved, not just reopened**: read
  directly against vendored CascLib source, `CascOpenStorageEx`'s online
  fetch (`HttpDownloadFile`, `CascFiles.cpp:1241`) is `static`
  (unreachable from outside the library), opens a raw socket, and speaks
  plaintext HTTP/1.1 on port 80 only — no TLS, no `User-Agent`, no pacing
  hook. Cannot be paced or identified from outside as shipped. Resolution
  written into `DESIGN.md` §9: two independent CascLib storage handles —
  one against the real install (read-only, worklist resolution only) and
  one `bOnlineStorage=true` handle whose `szLocalPath` is our own scratch
  dir, with `HttpDownloadFile` patched to fetch via libcurl instead of
  the raw-socket client. BLTE decode and hash verification, downstream of
  that one function, stay exactly CascLib's own.
- **Everything that needs no network is implemented**: `src/worklist.cpp`
  (loads/validates `--from-list`), `src/casc_storage.cpp` (handle A —
  direct per-FileDataID `CascOpenFile`/`CascGetFileInfo` lookups, no
  `CascFindFirstFile` scan), `src/plan.cpp` (buckets by
  local/missing/encrypted/not-in-manifest), `src/dry_run.cpp` (report +
  timestamped marker file), `src/log.cpp` (synchronous append-only
  event log), `src/main.cpp` (`dry-run`/`fetch` CLI). The network fetch
  itself (`src/fetch.cpp`) is a deliberate no-op scaffold: it enforces
  the dry-run-marker freshness gate, then returns a "blocked, not
  implemented yet — verify the `HttpDownloadFile` patch against a real
  file first" result for every entry, with no socket/libcurl/CascLib
  online call anywhere in that path. Built clean with `cmake --build`;
  CLI error paths (missing flags, bad worklist lines, bad install path,
  empty worklist, fetch without a fresh marker) exercised by hand. Not
  yet run against a real install or real CDN — needs both to verify end
  to end, per the project's own disclaimer standard.

## 2026-08-16 — §9 HTTP-layer decision reversed: `CascOpenStorageEx` online mode, not hand-rolled libcurl

A real, manual test (separate agent, one real critical file, live CDN)
found `CascOpenStorageEx(..., bOnlineStorage=true)` handles CDN
resolution, BLTE decode, and content-hash verification for free, using
CascLib's own shipped code. Reimplementing BLTE decoding from scratch to
keep the hand-rolled libcurl path isn't worth it. See `DESIGN.md` §9's
2026-08-16 revision note for the full reasoning and the still-open
follow-up (whether `CascOpenStorageEx` can be paced/identified from the
outside to satisfy §5's politeness policy — unconfirmed). `libcurl` stays
wired into `nix/flake.nix`/`CMakeLists.txt` for now pending that check.

## 2026-08-16 — Build-system scaffolding: CascLib vendoring, libcurl, CMake

HTTP layer decided: libcurl with HTTP/2 (see `DESIGN.md` §9), covering the
resume/streaming/timeout/custom-`User-Agent` primitives §5's politeness
policy needs, without vendoring a second dependency the way CascLib
already requires.

`vendor/CascLib` added as a git submodule (mirrors `casc-tool`); `nix/
flake.nix` gained a matching `casclib` flake input (spliced into `vendor/
CascLib` at build time, same mechanism as `casc-tool`'s own flake, since a
submodule's file contents aren't visible to a plain `nix build` of this
repo's own git-tracked source) plus a real `packages.default`/`apps.default`
and `curl` in the dev shell. `CMakeLists.txt` added: builds `vendor/CascLib`
as a static lib, requires `CURL`, and compiles `src/main.cpp` — currently a
loud stub (prints "not yet implemented", exits 1) — into `tact-fetch`.
Verified end to end: `nix flake check` passes, `nix build .#default`
produces a working binary that runs and exits with the stub message.

## 2026-08-16 — DESIGN.md architecture decisions resolved

`DESIGN.md` §9's three open architecture questions decided by Luna:
hand-rolled HTTP client (vendoring CascLib only for local manifest reads,
never the network fetch — a deliberate honest `User-Agent` plus real
politeness limits beats CascLib's own anonymous HTTP/1.1 path, and a
modern transport supports resume/streaming/backpressure HTTP/1.1 doesn't);
`tact_export/` naming mirrors `wow_export/` exactly; the two trees never
auto-merge. §5's per-event logging requirement made explicit (synchronous
append, not batched-and-flushed, so a crash still leaves a complete log).
§7 gained an explicit dry-run-freshness gate (10-minute marker file; a
real run refuses to start without one).

## 2026-08-16 — Repo scaffolding

`README.md`, `CLAUDE.md`, `nix/flake.nix` (dev shell), `.envrc`,
`.claude/rules/nix.md` symlink, `FAILURES.md`/`CHANGELOG.md` stubs added.
`tact-fetch-DESIGN.md` renamed to `DESIGN.md` to match the project
convention (`~/nix/claude-rules/project-CLAUDE.template.md`). No source
code yet — see `CLAUDE.md`'s `## Status`.
