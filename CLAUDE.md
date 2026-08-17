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
  writes decoded output under `--export/_unresolved/` (briefly
  flattened 2026-08-16, reverted 2026-08-17 — see `DESIGN.md` §9's
  naming note: a CASC/BLTE-decoded blob isn't a real game asset until
  something resolves it to its real path, so it doesn't belong in the
  real tree yet) as `FILE########.<ext>`, `<ext>` content-sniffed
  (`src/content_sniff.cpp`, added 2026-08-17 — real magic bytes verified
  against real extracted files, not `.dat` by default anymore when the
  format is recognized: `.m2`/`.blp`/`.skin`/`.db2`/`.ogg`/`.avi`)
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
  before overwriting, giving real version history. `FetchCascFile`'s
  archive-info overload (`FetchRemoteArchiveRange`, added 2026-08-17)
  fetches only a file's actual byte range out of its containing CDN
  archive via a real, response-verified HTTP Range request, instead of
  the whole (up to 256 MB) archive segment CascLib's own unpatched
  `FetchCascFile` would otherwise pull — see `DESIGN.md` §9's live
  finding for why this was necessary and how it was verified (not just
  asserted). Everything else in CascLib's fetch/decode/verify pipeline
  is untouched.
  Verified against the real install and real network (see `## Resume` for
  what that verification actually found — a locale-mask bug, fixed; an
  architectural cost Luna resolved by making the cache an intentional,
  persistent, versioned asset; and a second architectural finding
  (whole-archive fetches) resolved with real Range-request support,
  verified per-file at 504 KB actual disk usage against ~1.07 GB of
  logical size for the files checked).
- **Target**: see `DESIGN.md` §7/§9. Everything described above is
  built **and verified end to end against the real install and real
  CDN** (2026-08-16 – 2026-08-17): a full, unbounded run completed the
  entire index bootstrap in ~2.5 minutes (399 MB, 1346 `.index` files
  for retail WoW/eu build 69299); a 400-ID random-sample smoke test
  (from a freshly-scanned, ground-truth-verified missing-files list, not
  the stale `casc_missing_fileids.txt` originally provided) confirmed
  real files fetch, decode, and hash-verify correctly, encrypted files
  get a clean marker, and — after finding and fixing the whole-archive
  cost — genuinely new archive fetches land as small, correctly sparse
  files (17–93 KB actual for individually-checked cases), and extension
  content-sniffing correctly identifies real formats instead of leaving
  everything `.dat`. Only thing still open, and **scoped out of
  tact-fetch entirely (2026-08-17, Luna)**: FileDataID → real-path
  resolution is `casc-tool`'s job, a separate pass over `_unresolved/`
  using its own exploration tooling — not a future tact-fetch flag, not
  built yet, different repo.
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
  5. **Second real cost, found live-testing (2026-08-17), resolved same
     day, verified per-file before trusting it**: a 400-ID random-sample
     smoke test (drawn from a freshly-scanned, ground-truth-verified
     missing-files list — the `casc_missing_fileids.txt` Luna originally
     pointed at was stale, only 1 of its 18,747 IDs was still actually
     missing) revealed that `FetchCascFile` fetches the *entire*
     containing CDN archive (confirmed up to 268,435,456 bytes exactly)
     for any file packed inside one, not just that file's own bytes —
     true of unpatched CascLib too, not something the libcurl patch
     introduced. Resolution: `FetchRemoteArchiveRange`
     (`vendor/CascLib/src/CascFiles.cpp`) issues a real HTTP Range
     request for exactly the needed span, verified as genuinely partial
     (206 + exact byte count, not a server silently sending the whole
     body) before being trusted, and writes it into a sparse local file
     at the correct offset — no whole-file fallback on failure. Per
     Luna's explicit instruction to validate this before trusting it:
     confirmed directly per-file, twice — 12 fresh archives at 504 KB
     actual disk usage against ~1.07 GB apparent/logical size, then 9
     more at 489 KB actual — not just asserted from the patch's intent.
     A companion fix: the encrypted-file raw-bytes-preservation logic in
     `src/fetch.cpp` was looking for cached content at the wrong path
     (the file's own EKey, not the archive's key it's actually stored
     under) and could never find anything — simplified to an honest
     empty `.encrypted` marker instead of a broken "preserve the
     ciphertext" promise.
  6. **Scope decision + a real analysis finding (2026-08-17)**: Luna
     confirmed FileDataID→real-path resolution belongs entirely to
     `casc-tool` (a separate pass over `_unresolved/`, its own
     exploration tooling, different repo) — not a future tact-fetch
     `--listfile` flag, closing that open question for good rather than
     leaving it as "not built yet." In the same session, added
     content-sniffed extension correction (`src/content_sniff.cpp`,
     finding 6 continued below) and generated a full missing-files
     report (`development/missing_report.csv`, gitignored, 156,299 rows:
     FileDataId/ContentSize/Encrypted/LocaleFlags/Extension/Path,
     cross-referenced against a local community listfile) for Luna's own
     analysis. That report surfaced something worth recording here even
     though it's not a tact-fetch code change: **the missing set for
     this install is ~87% locale audio (`sound/*.ogg`) and cinematics,
     essentially zero missing `.m2`/`.skin`/creature content** — so
     tact-fetch's fetch list is not a fix for husk's texture-gap problem
     (that's a "content already local but not read correctly, or
     genuinely TACT-encrypted" question, not a "not yet downloaded"
     one). Also fixed in generating that report: the local community
     listfile has CRLF line endings that silently leak into joined CSV
     fields if not stripped — cost an early version of the report
     corrupted exact-match filtering (values compared equal to eye but
     not to `==`); regenerated clean, worth remembering for any future
     listfile-based analysis in this repo.
- **Next step**: no open findings right now, and the biggest open
  question (who does path resolution) is now closed. Natural next
  pieces if picked back up: build the `casc-tool` side of path
  resolution (separate repo, separate session) -- read `casc-tool`'s own
  `CLAUDE.md`/`DESIGN.md` conventions first, this repo's own rules don't
  apply there; consider whether xattr-based FileDataID tagging
  (`user.tact.filedataid`, discussed but not built) is worth adding once
  that resolution step exists and actually renames files; actually
  exercising `_history/`'s accumulation behavior by running `fetch`
  again after a real CDN change (a new WoW build/patch) to confirm a
  second snapshot appears rather than none; optionally pruning the
  ~2.9 GB of fully-downloaded archives left in the persistent cache from
  before finding 5's fix existed (harmless leftover, not wrong, just no
  longer necessary at full size — the specific ranges within them are
  still valid, only the surrounding unused bytes are waste); and
  reconsidering whether the raw-ciphertext preservation from finding 2
  is worth a further CascLib patch exposing the EKey-to-archive mapping,
  now that finding 5 explains exactly why it silently never worked.
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
