# tact-fetch — design document

Handoff doc for scaffolding a new, standalone project. Not part of `casc-tool`
and never should be — see "Why a separate project" below; that's not a
soft preference, it's the whole reason this document exists instead of a
casc-tool feature branch. Written so a fresh agent (or human) with zero
prior context on this conversation can start a repo from it.

---

## 1. What this is, in one paragraph

A tool that reads a local, already-installed WoW CASC storage read-only
(same access pattern `casc-tool` already uses), finds FileDataIDs whose
manifest entry exists but whose actual bytes were never downloaded to this
install (`bFileAvailable=false` in CascLib's `CASC_FIND_DATA`), and fetches
those specific bytes from Blizzard's public CDN — politely, slowly, on
purpose — into a `tact_export/` directory that sits *alongside* an existing
`wow_export/` tree (a `casc-tool extract-batch` output directory), so a
downstream consumer (e.g. `husk`) has a second, independent source of
already-decoded CASC content to check when `wow_export/` doesn't have
something.

## 2. Why this exists (the real numbers behind it)

A full scan of one real, live retail install (2026-08-16, product `wow`,
build 69299) found:

```
total entries in this build's manifest: 3,237,724
real (nameable) files:                  1,930,512
not locally available:                    156,299  (8.1%)
  of those, also content-flagged encrypted: 28,698
  of those, TACT alone would fully unlock:  127,601
  of those, already have a real listfile name: 154,932 (99.1%)
```

That 127,601 is the real target size: known-to-CASC, named, not encrypted
(so no TACT-key problem, just a bytes-not-downloaded problem), genuinely
recoverable from the CDN if fetched politely. This is not a
one-file-at-a-time curiosity — it's a real, large, slow job, which is
exactly why it needs to be a separate, deliberately-paced tool rather than
a flag on something meant for quick interactive lookups.

## 3. Why a separate project, not a `casc-tool` feature

`casc-tool` is a local file browser: point it at a directory you already
have, it reads bytes that are already on disk, nothing it does has any
effect outside that one process. Adding network fetch to it — even
read-only, even well-intentioned — collapses that trust boundary: the same
binary that's "safe to run interactively to check one file" becomes "capable
of issuing tens of thousands of requests against Blizzard's live
infrastructure," and there's no flag spelling that makes that distinction
visible enough. `list`/`info`/one `extract` are all fine by construction;
"download everything" or "N requests/second, unattended, for hours" is a
fundamentally different kind of action and deserves a tool whose *name*
already tells you that, not a flag buried in `--help`.

Practical consequence: **this project should not import or link against
`casc-tool`'s code.** It needs the same *kind* of local-CASC-read
capability casc-tool has (resolving a FileDataID to its EKey/CKey/size via
the local ENCODING/ROOT manifest), so it vendors its own copy of CascLib
under `vendor/CascLib` — originally a git submodule mirroring casc-tool's
own convention, later replaced (§9, 2026-08-16) by fetching it through
the Nix flake's `casclib` input instead, once a local patch needed
keeping in sync made two separately-tracked copies (submodule commit +
flake.lock rev) worth collapsing into one. See casc-tool's own README
"Why this needed more than 'just cmake'" section for the underlying
splicing mechanism if useful as a reference. The only
sanctioned coupling between the two projects is a **plain text file
format**: one FileDataID per line, identical to casc-tool's own
`extract-batch --from-list` convention — this tool *requires* such a file
as its worklist seed (e.g. "fetch exactly these IDs," the output of a
targeted retry or a scoped test run someone already decided on). It never
computes its own worklist by independently scanning the storage — this is
a companion tool that fetches a named list, not a scraper. Nothing about
that coupling requires casc-tool's source, only its file format.

## 4. What was learned about the actual download mechanics (grounded, not guessed)

Checked directly against the vendored CascLib source (`vendor/CascLib/src/
CascFiles.cpp`'s `FetchCascFile`, and `vendor/CascLib/src/common/
FileStream.cpp`'s `BaseHttp_*` functions), not assumed:

- **CascLib's own online-fetch path is already about as conservative as
  possible**: one file, one raw-socket HTTP/1.1 connection, one plain
  `GET`, fully sequential — no concurrency, no retry, no backoff, no rate
  limiting anywhere in that code path. If this project builds on top of
  `CascOpenOnlineStorage`/`CASC_FEATURE_ONLINE` directly, it inherits that
  ceiling for free; if it hand-rolls HTTP instead (more control, e.g. for
  real concurrency or resumable range requests), it needs to add equivalent
  restraint itself — CascLib's own client-side code will not save it from
  being impolite if driven with N parallel worker threads each opening a
  different file.
- **No `User-Agent` at all in CascLib's raw HTTP client.** This project's
  own fetcher should set a real, honest one (tool name + version + contact,
  the way any responsible bulk client identifies itself) — a gap in
  CascLib worth not inheriting.
- **CDN host selection and Blizzard's own stated concurrency hint come from
  the local install's `.build.info`**, not from anything this tool invents:
  the `CDN Servers` field is a space-separated list of URLs, each carrying
  query hints like `?maxhosts=8` — that number is Blizzard's own stated
  ceiling for this specific CDN config. Anchor a concurrency cap to a
  *fraction* of that (see §5), never to or above it.
- **Resumability already exists as a pattern to copy**: `FetchCascFile`
  checks whether the local path already exists before touching the network
  at all. This project's own output convention (§7) should support the
  same skip-if-already-fetched check trivially — a killed/interrupted run
  must be safe to just re-run.
- **The Battle.net Agent's actual real-world pacing is not knowable from
  here** — it's closed-source. Don't claim parity with it; claim parity
  with the *public, inspectable* protocol mechanics (the URL scheme, the
  archive/index format) and be independently, deliberately conservative
  about pacing rather than trying to reverse-engineer Blizzard's own
  internal tuning.

## 5. Politeness policy — concrete defaults, not vague intentions

These are starting numbers, tunable, but the tool should ship with these as
defaults and make it *harder*, not easier, to go more aggressive than to
stay conservative:

- **Concurrency**: default **2** simultaneous connections. User-configurable
  up to a hard ceiling of **4** (half of the `.build.info`-stated
  `maxhosts=8`) — refuse (loud error, not silent clamp) any request to go
  higher than the hard ceiling; if someone genuinely needs more, that's a
  deliberate code change, not a flag.
- **Rate limit**: a token-bucket cap independent of concurrency — default
  **≤5 new requests started per second**, regardless of how many workers are
  configured. Concurrency controls *how many in flight*; this controls *how
  fast new ones start* — both axes matter and neither substitutes for the
  other.
- **Per-request timeout**: 30s. A hung connection should free its worker
  slot, not block the pool.
- **Retry/backoff**: exponential, starting at 1s, doubling, capped at 60s,
  max 5 attempts per file, then mark that file permanently failed *for this
  run* (loggable and retryable in a future invocation, not silently
  dropped, not retried forever either).
- **`429`/`503` handling**: back off harder than a generic failure — if the
  response carries a `Retry-After` header, honor it exactly rather than
  using the generic backoff schedule.
- **No fan-out across all CDN hosts at once.** Round-robin or
  sequential-with-fallback across the host list (matching `FetchCascFile`'s
  own pattern), never "hit every host simultaneously to go faster."
- **Mandatory dry-run before any real run.** Default behavior reports
  file count, total estimated bytes, and an estimated wall-clock time
  (from the rate limit above) *without* touching the network — matches
  `casc-tool extract-batch --dry-run`'s own convention. A real run requires
  an explicit, separate invocation/flag; there is no single command that
  goes from "nothing" to "127,601 live requests" by accident or by
  forgetting a flag.
- **No silent full-corpus default, no independent discovery.**
  `--from-list <file>` is mandatory — no file, no run (loud error). There
  is no mode that scans the local storage on its own to decide what's
  missing; the scope of a run is always a list the caller already
  produced on purpose (e.g. from a prior `casc-tool` dry-run), never
  something this tool goes and discovers for itself.
- **Every request logged**, plain append-only log (URL, host, timing,
  result, retry count) — auditable after the fact, not just a live progress
  bar that scrolls away. This is the same "state visibility" principle
  casc-tool's own CLI follows (see §6). **The append happens synchronously,
  in line, at the moment each event occurs** — not batched and flushed from
  a queue — so a crash or kill mid-run still leaves a log that accounts for
  everything that happened up to that instant. A logging strategy that
  buffers and flushes periodically would silently lose exactly the events
  most worth having: the ones right before an unclean exit.

## 6. Design principles to inherit from `casc-tool` (explicit, not vibes)

Pulled directly from casc-tool's own README/CLAUDE.md conventions —
inline the substance here since a fresh repo won't have casc-tool's files
open to reference:

- **One small verb per real workflow, not a kitchen-sink tool.** Likely
  shape: a `plan` (or `--dry-run`) step and a `fetch` step, nothing more.
  Resist the urge to grow flags for hypothetical futures.
- **CLI design per the "structure over memorization" standard**
  (`~/nix/claude-rules/CLI.md` if available in the new environment): one
  consistent grammar across the whole tool; errors state expected vs.
  actual, never a bare "failed"; state visibility — a fetch run should
  self-report progress/ETA/remaining without a second command to check;
  shorthands only for genuinely high-frequency flags; no inline shell
  computation in documented invocations.
- **Nix flake structure matching casc-tool's own**: `nix/flake.nix`
  exposing `packages.default`/`apps.default`; vendored C/C++ deps (CascLib)
  spliced in as a separate flake input rather than relying on `git
  submodule` at Nix-build time (see casc-tool's README "Why this needed
  more than 'just cmake'" for the exact rationale/mechanism); `direnv exec
  .` as the standard way to run any command, one command per shell block.
- **No multi-step shell commands, no ad hoc loops.** A pipeline longer than
  two stages (`cat x | grep y` is the ceiling, not the norm) is a script,
  not a command line — write it as one. Once a script passes ~10 lines,
  it's a persistent file in the repo, not a heredoc or an inline `for`.
  Readability wins over brevity even for something meant to run once:
  don't compress a script to fit a line budget at the cost of a reader
  being able to follow it.
- **`FAILURES.md` as an actual punch list, `CHANGELOG.md` as the archive.**
  Resolved findings move out of `FAILURES.md` into `CHANGELOG.md` the
  moment they're fixed — `FAILURES.md` should only ever list what's still
  genuinely open, not accumulate "[fixed]"-tagged history forever.
- **Two-tier testing**: pure-logic tests (URL construction, backoff timing
  math, worklist parsing) run always, no network, no dependencies.
  Integration tests that touch the real CDN are a *much* more sensitive
  case than casc-tool's own real-storage-gated tests — an automated test
  suite that itself hits Blizzard's production CDN on every CI run would
  be its own politeness violation. Gate real-network tests behind an
  explicit opt-in env var (mirroring `CASC_TOOL_TEST_STORAGE`'s pattern),
  default-skipped, and keep any such test scoped to a single small,
  known-stable file — never a bulk operation, even in "just testing" mode.
- **README structure**: "what you need before this works," Design notes
  section documenting real empirical findings (not aspirational behavior),
  a Disclaimer section matching casc-tool's own ("co-coded by AI, verified
  against the real thing").
- **Foreign-data discipline, extended to network responses**: every CDN
  response is untrusted foreign data crossing a trust boundary, same as any
  file `casc-tool` reads. Validate at the boundary — verify each fetched
  blob's content hash against the expected `CKey`/`EKey` (already known
  from the local manifest read) *before* treating it as a successfully
  fetched file. A corrupted or truncated download should be caught here,
  not discovered later by whatever reads `tact_export/`.
- **Never modifies the source WoW install or local CASC storage in any
  way.** Opens it strictly read-only, exactly like casc-tool. All writes
  go to `tact_export/`, nothing else.
- **[HARD] network-write boundary, restated for this project specifically**:
  this tool only ever issues `GET` requests for public Blizzard CDN
  content — no telemetry, no analytics, nothing that identifies the
  operator beyond a transparent, honest `User-Agent` string on the requests
  themselves (which Blizzard's CDN already sees regardless). Never anything
  that could be read as "pushing data associated with the operator to the
  network" beyond the ordinary, necessary fact of making an HTTP request.

## 7. Rough architecture

```
1. Open local CASC storage read-only (vendored CascLib), same access
   pattern as casc-tool.
2. Build the worklist -- `--from-list <file>` is mandatory, one mode only:
   no file, no run (loud error, not a silent fallback). This tool is a
   companion that fetches a named list someone already decided on ("I've
   done a dry run, here's the list I want, get the ones I'm missing"),
   never an independent scanner that goes and discovers what's missing on
   its own -- that's a fundamentally different, much larger action and
   deserves its own deliberate invocation elsewhere, not a default here.
   Use exactly the listed FileDataIDs, filtered through the storage's own
   bFileAvailable check (don't re-fetch something already local). Resolve
   each surviving ID's real EKey/CKey from the local manifest -- this is
   what makes the CDN URL and lets a post-fetch hash check happen.

   **Open gap, found while implementing this step (2026-08-16), not yet
   resolved**: the original plan for this step also pre-filtered
   content-flagged-encrypted entries before fetching (encrypted-but-
   undownloaded needs a TACT key too; fetching ciphertext nobody can
   decrypt yet isn't useful output). Verified against a real install that
   this filter is not achievable as planned: CascLib's only public way to
   read a file's content flags, `CascGetFileInfo(CascFileFullInfo)`, calls
   `EnsureFileSpanFramesLoaded` unconditionally (`CascReadFile.cpp:741`),
   which has to open the local data-archive stream -- and that fails by
   construction for a file this install never downloaded, before content
   flags are ever read. (Confirmed the flags themselves don't need the
   stream -- `TFileTreeRoot::GetInfo`, `common/RootHandler.cpp:106`, is a
   cheap in-memory lookup -- it's `CascGetFileInfo`'s public surface that
   bundles that cheap lookup behind the stream-dependent size query,
   with no way to ask for one without the other.) CKey/EKey don't have
   this problem (`CascFileContentKey`/`CascFileEncodedKey` read straight
   off the CKey-table entry, no stream needed) and content size has the
   same problem as content flags, so a missing file's size is equally
   unknown pre-fetch. Net effect: for a file this install never
   downloaded, tact-fetch can get its EKey/CKey but not its size or
   encryption status, through any public CascLib API, without either (a)
   a `CascFindFirstFile`-based lookup -- confirmed to be a linear scan
   internally (`TFileTreeRoot::Search`, `common/RootHandler.cpp:72`), the
   exact mechanism §7 step 2 above was written to avoid -- or (b) reaching
   into CascLib's internal `TCascStorage`/root-handler structures, which
   isn't a supported public interface.

   **Resolved by Luna (2026-08-16, refined same day): detect post-fetch,
   land at the real path with a postfixed extension, don't discard it.**
   Encryption status is no longer a pre-fetch filter -- every `to_fetch`
   entry gets fetched regardless of unknown flags (the raw CDN blob
   still downloads and hash-verifies at the encoded-bytes level either
   way). When `CascReadFile`'s BLTE decode surfaces
   `ERROR_FILE_ENCRYPTED` afterward: the file is **not** written into
   `tact_export/`'s normal tree under its real extension as if it were
   usable content, and it is **not** silently dropped either. It lands
   at its *real, otherwise-normal* path (or directly under
   `tact_export/` as `FILE########.dat`, §9's naming revision, if the
   path isn't known) with `.encrypted` appended to the filename --
   `character/foo.m2` becomes `character/foo.m2.encrypted`
   -- rather than in a separate subtree. The postfix is deliberately on
   the full real filename, extension included, specifically so a
   downstream consumer matching by extension (e.g. globbing `*.m2`)
   never matches the encrypted placeholder: `foo.m2.encrypted` doesn't
   end in `.m2`, so naive extension-based tooling skips it automatically
   without needing to know this convention exists, while a human
   browsing the tree still finds it sitting right next to where the real
   file would be. Logged loudly (§5's per-event log, a result value
   distinct from both "fetched" and "failed" -- "encrypted", not
   swallowed into either) and called out prominently in the run's final
   summary, not buried in a count. `to_fetch_unknown_size` in the current
   implementation (`src/plan.cpp`) is the pre-fetch bookkeeping this
   relies on: it counts entries whose encryption status is unknown until
   the real fetch step (§9, not yet built) actually runs and finds out.
3. --dry-run (default): report count, total bytes, estimated wall-clock
   time at the configured rate limit. No network touched. On success,
   writes a timestamped marker file to a known location (temp dir) keyed
   to the resolved worklist, recording that a dry-run for *this* scope was
   just completed.
4. Real run (separate, explicit invocation): refuses to start — loud
   error, not a silent no-op — unless a matching dry-run marker from step 3
   exists and is fresh (**10 minutes**, laxer acceptable during
   development but not the shipped default). This is a deliberate
   speed bump against "nothing → 127,601 live requests" happening because
   a stale mental model of the worklist got fed straight to a real run
   without a fresh look at what it actually contains. Given a fresh
   marker: bounded worker pool (§5), per-file fetch via a *second*,
   independent CascLib storage handle (`CascOpenStorageEx(...,
   bOnlineStorage=true)`) opened with `szLocalPath` pointed at our own
   scratch directory, never the real install (see §9's fetch-pipeline
   resolution) -- CascLib's own BLTE decode and CKey/EKey hash
   verification run unmodified on the result, write to
   tact_export/<real-path-if-known, else an _unresolved/ convention
   matching casc-tool's own>, checkpoint (append to a resume log) on each
   success.
5. Final summary: fetched / failed (with reasons) / skipped-already-present
   counts, matching casc-tool's own extract-batch summary shape.
```

## 8. Explicit non-goals

- Not a general-purpose Blizzard CDN client, not scoped to any game beyond
  WoW unless deliberately extended later.
- Not an entitlement/auth bypass — only fetches what a legitimate local
  install's own manifest already reveals it's allowed to know about (a
  file's EKey has to come from *this* install's real ENCODING/ROOT data;
  this tool never guesses or brute-forces FileDataID→EKey mappings).
- Not a TACT-key-discovery tool. Encrypted-and-undownloaded files are out
  of scope for fetching (see §7 step 2) — that's a separate, already-known
  moving-target problem (`wowdev/TACTKeys`), not this tool's job to solve
  or route around.
- Never writes into, or otherwise mutates, the source WoW install/CASC
  directory.
- No relationship to `casc-tool` beyond the shared plain-text FileDataID
  worklist format — no shared code, no dependency in either direction.

## 9. Architecture decisions (resolved 2026-08-16)

Previously left open; decided by Luna, recorded here so a fresh
reader/agent never has to re-derive them:

- **Hand-rolled HTTP, not `CascOpenOnlineStorage`.** Rejected the simpler
  "inherit CascLib's own sequential fetch path" option: a deliberately
  honest `User-Agent` plus real, chosen politeness limits (§5) is *more*
  polite than CascLib's own anonymous, single-connection, no-retry
  HTTP/1.1 client — anonymity isn't politeness, it's just untraceability.
  Hand-rolling also opens the door to modern transport features HTTP/1.1
  doesn't have — multiplexed/streaming requests, mid-request resume, real
  backpressure/pacing — instead of HTTP/1.1's "one shot, die on the first
  pothole" behavior. This means vendoring CascLib strictly for the local
  manifest read (FileDataID → EKey/CKey/ContentSize), never for the actual
  network fetch.
  - **HTTP layer: libcurl, HTTP/2.** Chosen over a raw-socket
    implementation or a minimal header-only client (`cpp-httplib`, HTTP/1.1
    only): libcurl already gives multiplexing/streaming, resumable range
    requests, per-request timeout, and easy custom `User-Agent` — the
    concrete politeness primitives §5 asks for — without reimplementing
    HTTP semantics from scratch. Available via nixpkgs; no vendoring
    needed the way CascLib requires.
  - **Revision (2026-08-16) — reversed in favor of `CascOpenStorageEx`
    online mode.** Luna had a separate agent manually fetch one real,
    critical file end to end against the live CDN. Empirical result:
    `CascOpenStorageEx(..., bOnlineStorage=true)` works cleanly and, for
    free, handles CDN URL resolution, **BLTE decode**, and content-hash
    verification — all using code CascLib already ships. The hand-rolled
    path above was scoped assuming the fetch itself was the hard part;
    it wasn't accounting for BLTE (Blizzard's own chunked/compressed
    archive format wrapping every CDN blob) needing a real decoder before
    a fetched file is actually usable content — reimplementing that from
    scratch for no benefit over what CascLib already does correctly is
    not a reasonable trade against the politeness/transport gains hand-
    rolling was chosen for. **Net decision: build on `CascOpenStorageEx`'s
    online-storage path directly** rather than hand-rolled libcurl HTTP.
    This does **not** relitigate §5's politeness policy — those limits
    (concurrency ceiling, rate limiting, retry/backoff, honest
    `User-Agent`) still apply and now have to be layered *around*
    CascLib's online-fetch calls (e.g. a request gate/pacer wrapping each
    `CascOpenFile`/read against an online storage handle) instead of
    inside a hand-rolled client — §4's finding that CascLib's own fetch
    path is anonymous and un-paced by default is still true and still a
    gap this tool must close, just from the outside of CascLib's API
    instead of by replacing it.
  - **Follow-up resolved (2026-08-16): `CascOpenStorageEx`'s online path
    cannot be paced or identified from outside via any public API.**
    Checked directly against the vendored source, not assumed: the actual
    per-file network fetch happens in `HttpDownloadFile`
    (`CascFiles.cpp:1241`, `static`, no external linkage — unreachable
    from outside the library), which opens a raw TCP socket
    (`common/Sockets.cpp`) and sends a hand-built plaintext
    `GET ... HTTP/1.1` (`common/FileStream.cpp:678`) on **port 80 only**
    — no TLS anywhere in the socket code, no `User-Agent` header, no
    concurrency, no retry, no `PfnProgressCallback`-style hook for
    per-request control. This closes the follow-up rather than reopening
    the decision: CascLib's online-fetch transport genuinely cannot be
    identified or paced from the outside as shipped.
  - **Fetch-pipeline resolution: keep CascLib's decode/verify, replace
    only its download function, and isolate the cache off the real
    install.** Two things make this tractable instead of a rewrite:
    - `HttpDownloadFile` downloads each CDN blob to a **local cache path
      first** (`CascFiles.cpp:1359`); BLTE decode and CKey/EKey hash
      verification happen later, when `CascReadFile` reads that now-local
      file through a completely separate provider
      (`common/FileStream.cpp`'s `BaseFile_*`, one of three
      interchangeable implementations of the same five-function-pointer
      `TFileStream` interface alongside `BaseHttp_*`/`BaseMap_*`). Fetch
      and decode/verify are already separate internally; only the fetch
      half needs to change.
    - That local cache path is **not hardcoded to the real install**:
      `CascOpenStorageEx`'s `szLocalPath` argument is caller-supplied and
      becomes `hs->szRootPath`/`szDataPath`, which is exactly what
      `HttpDownloadFile` writes under. Pointing a *second*, independent
      storage handle's `szLocalPath` at our own scratch directory (product
      code given separately, `bOnlineStorage=true`,
      `CascOpenStorage.cpp:1519` `CheckOnlineStorage` path) makes that
      scratch dir CascLib's entire cache root for that handle — the real
      install, opened separately and read-only for the worklist (§7 step
      1), is never touched by anything fetch-related.
    - Net shape: **two storage handles**, never one. Handle A: the real
      install, local-only, read-only, used once to resolve each worklist
      ID's EKey/CKey/ContentSize (§7 step 1) — no online feature, no
      writes. Handle B: `bOnlineStorage=true`, `szLocalPath` = our own
      scratch dir, used per-file to fetch (§7 step 4) — this is the
      handle whose `HttpDownloadFile` gets patched to route through
      libcurl (HTTP/2, real `User-Agent`, our own pacing/timeout/backoff
      per §5) instead of the raw-socket client, writing into the same
      scratch dir it already owns. Everything upstream (CDN host
      selection, retry-across-hosts loop) and downstream (BLTE decode,
      hash verification) of that one patched function is reused exactly
      as CascLib already implements it — no reimplementation of BLTE, no
      reimplementation of hash verification, and the real install's
      `Data/` directory never appears as a write target anywhere in this
      pipeline.
- **Implemented (2026-08-16), and one more real cost found in the
  process: opening handle B is not free, even for a one-file run.**
  The `HttpDownloadFile` patch above is built (also patched
  `RibbitDownloadFile`, the *other* raw-socket fetch function CascLib
  uses for the one-time versions/cdns bootstrap every online storage
  needs before `HttpDownloadFile` is ever called — same libcurl swap,
  same reasoning, found necessary only once live-tested: opening handle
  B against a real product without it left a live TCP connection to
  Blizzard's Ribbit endpoint using CascLib's original no-timeout raw
  socket client). Verified end to end against `us.patch.battle.net:1119`
  and `level3.blizzard.com` (real product `wow`, region `eu`): DNS,
  TCP, and the actual libcurl fetch all work correctly, HTTP status and
  bytes come back as expected, `SaveLocalFile` writes to the scratch
  dir exactly as designed, no writes ever touch the real install.
  **But**: `CascOpenStorageEx(bOnlineStorage=true)`'s `LoadIndexFiles`
  step (`CascIndexFiles.cpp`, `LoadArchiveIndexFiles` for
  `BuildFileType == CascVersions`) downloads the CDN's **entire archive
  index set** — not scoped to the worklist at all — before the handle
  finishes opening. A live test for a single-file worklist (FileDataID
  21) was still pulling individual `.index` files (463 of them, 41 MB,
  interrupted before completion — the real total is unknown and
  plausibly much larger for retail WoW's full archive set) minutes in.
  This is CascLib's own online-mode design, not a bug introduced by the
  libcurl patch — the raw-socket path would have paid the identical
  cost, just slower and less politely. It's a **one-time cost per
  scratch-dir lifetime** (the index files get cached under
  `state_dir/online-cache` exactly like any other fetched blob, so a
  second run against the same scratch dir wouldn't re-download them),
  but it means the *first* `fetch` invocation against a fresh scratch
  dir pays a large, worklist-size-independent bandwidth cost before a
  single requested file is fetched — directly in tension with fetching
  "exactly this small list, politely."

  **Resolved by Luna (2026-08-16): not a cost to avoid, an index to
  keep.** The archive index set is exactly what lets a CDN client (the
  real Battle.net Agent, or us) know what's available and diff against
  it later — reframed from "wasted bandwidth" to "a local asset worth
  keeping, centrally, permanently, once." Three changes:
    - **Central, persistent cache dir, not per-invocation scratch.**
      `src/cache_dir.h`'s `TactFetchCacheDir()`
      (`$XDG_CACHE_HOME/tact-fetch`, falling back to
      `$HOME/.cache/tact-fetch`) replaces `state_dir/online-cache` as
      handle B's `szLocalPath`, namespaced by `<product>-<region>`
      (`src/fetch.cpp`). The expensive index bootstrap is now paid
      **once per product/region, ever** — not once per scratch dir, and
      never again once paid, across every future invocation from any
      directory.
    - **`CASC_FEATURE_FORCE_DOWNLOAD` for the small, mutable files.**
      Without it, CascLib's own `LocalCaching()` would treat a
      once-cached `versions`/`cdns` as good forever (`LoadCsvFile`,
      `CascFiles.cpp`), meaning tact-fetch would never notice a new WoW
      build after the first run. `src/fetch.cpp` now opens handle B via
      `CascOpenStorageEx` directly (not the `CascOpenOnlineStorage`
      convenience wrapper, which doesn't expose `dwFlags`) with that
      flag set — `versions`/`cdns` (a few KB) are re-checked on every
      `fetch`, while the CDN's content-addressed archive indices
      (the actual expensive part, immutable once fetched) are
      unaffected and still only fetched once, on a genuine cache miss.
    - **Version history for `versions`/`cdns`.** These two files aren't
      content-addressed (unlike everything else CascLib caches, which
      is named by its own EKey hash and therefore naturally immutable),
      so re-checking them on every run would otherwise just silently
      overwrite whatever was there. The `RibbitDownloadFile` patch now
      compares each freshly-fetched copy against what's currently
      cached before overwriting it, and if it changed, snapshots the
      old content into `<cache-root>/_history/<filename>.<unix-time>`
      first -- a permanent, timestamped record of every build/CDN-config
      change this tool has ever observed for that product/region,
      not just whatever the CDN currently reports.
  Compiles clean and passes the byte-identical patch-regeneration check
  (`vendor/patches/README.md`). **Verified end to end (2026-08-16)**: a
  full, unbounded run (capped only by a deliberate external "stop past
  3 GiB" watchdog, never triggered) against the real install and real
  CDN completed the entire index bootstrap in ~2.5 minutes, landing at
  **399 MB, 1346 `.index` files** total for retail WoW/eu at build
  69299 -- large, but nowhere near the multi-GB worst case the earlier
  interrupted test left open. The one targeted FileDataID (21) fetched,
  decoded, and hash-verified correctly: the output file
  (`_unresolved/FILE00000015.dat`) is exactly 4,609,024 bytes, matching
  `casc-tool`'s independently-reported size for the same ID byte for
  byte. `_history/` correctly gained one snapshot each for `versions`
  and `cdns` (the only fetch that's happened against this cache, so
  exactly one snapshot per file is the correct outcome, not a bug --
  the next run against an unchanged CDN state should add none). The
  real install was confirmed untouched (`find -newer` against a
  pre-session file, empty result) both before and after.
- **Found live (2026-08-17), resolved same day: `FetchCascFile` doesn't
  fetch a small file's own bytes, it fetches the whole ~256 MB archive
  segment that file happens to live in.** Blizzard packs many small game
  files together into large numbered CDN archives. Confirmed directly in
  the vendored source (`CascFiles.cpp:1437-1448`, unpatched CascLib
  behavior, not something the libcurl patch introduced): the
  archive-info `FetchCascFile` overload looks up a requested file's EKey
  in the local archive index, then **substitutes the file's own key for
  the entire containing archive's key** before fetching -- always a
  whole-file fetch (`HttpDownloadFile(..., NULL, 0, 0)`, offset/size
  params always zero), never a byte range, so the *entire* archive lands
  on disk to extract one small file's worth of content. Confirmed
  empirically: cached files sitting at exactly 268,435,456 bytes (256
  MiB, to the byte). For a narrow/clustered worklist this amortizes fine
  (many requested files often share the same few archives); for a
  broad/scattered one (a random sample across the whole game, or the
  real 156k-file target list if it isn't clustered by content type) it
  could mean downloading a large fraction of the game's total archive
  data -- a 400-file random-sample smoke test touched 7+ distinct
  archives in under a dozen files.

  **Resolution (Luna's call): add real HTTP Range-request support**,
  keeping everything else CascLib already does. New functions in the
  `HttpDownloadFile`/`RibbitDownloadFile` patch
  (`vendor/CascLib/src/CascFiles.cpp`, same file, same posture -- small
  additive patch, not a fork):
    - `FetchRemoteArchiveRange` replaces the whole-file delegation in the
      archive-info `FetchCascFile` overload, for online storage with no
      local consolidated archives to fall back on (handle B). It issues
      a real `CURLOPT_RANGE` request for exactly `[ArchiveOffs,
      ArchiveOffs+EncodedSize)` -- both already known before the fetch,
      from the same local archive-index lookup that used to just get
      discarded in favor of the archive's key.
    - The response is **verified, not trusted**: HTTP `206 Partial
      Content` and the exact requested byte count are both required:
      anything else (a CDN host that ignores `Range` and answers `200`
      with the full body, say) is a hard failure, not silently accepted
      as "good enough" -- accepting a full body here would silently
      reintroduce the exact cost this patch exists to avoid.
    - The fetched range is written into the local archive-proxy file **at
      its real offset**, leaving everything else in that file as a
      filesystem hole (a sparse file) -- `CascReadFile`'s later flat read
      at that same offset works completely unaware anything is missing
      elsewhere in the file, since it never looks anywhere else. No
      fallback to the old whole-file path on Range failure: a failed
      range fetch surfaces as a real error for `src/fetch.cpp`'s own
      retry loop, never silently degrades into paying the avoided cost.
    - A companion `<archive-proxy-path>.ranges` sidecar (one
      `"offset length"` line per fetched span) tracks what's already
      been pulled into that archive proxy, since the same archive can
      accumulate ranges from files requested in different runs, and
      "does the file exist" no longer means "is it complete" once it's a
      sparse partial copy.
  **Verified, not just asserted** (per Luna's explicit ask to check
  this before trusting it): 12 freshly-created archive-proxy files from
  a real fetch totaled **504 KB of actual disk usage** (`du`, real
  filesystem blocks) against **~1.07 GB of apparent/logical size**
  (`stat`/`ls -la`, the offset-correct sparse-file size) -- roughly a
  2,175x reduction, individually confirmed per file, not just in
  aggregate. A second, larger run confirmed the same pattern (9 new
  archives, 489 KB actual) after an initial false alarm from a naive
  aggregate-cache-size watchdog that conflated genuinely new growth with
  ~2.9 GB of already-existing, fully-downloaded archives left over from
  *before* this patch existed in the same persistent cache directory --
  a measurement-methodology lesson (per-file verification is the
  reliable signal; whole-directory size deltas are not, once old and new
  data coexist), not a flaw in the fix itself.

  **Side effect, also fixed**: `src/fetch.cpp`'s "preserve the raw
  ciphertext for an encrypted file" logic (`DESIGN.md`'s earlier
  encryption-handling revision) looked for the cached blob at the
  *file's own* EKey path -- but the local cache was always keyed by the
  *archive's* key (true before this patch too, just never actually
  exercised/tested until now), so it could never find anything to
  preserve. Simplified to an honest empty `.encrypted` marker (still
  distinct from both "fetched" and "failed", never silently dropped) --
  preserving the actual raw bytes isn't achievable from outside CascLib
  without exposing its internal EKey-to-archive mapping, which is a
  further patch to consider later, not a quick fix to bolt on here.
- **`tact_export/` naming**: real listfile-resolved path when known (not
  implemented yet, no `--listfile` flag exists), `FILE########.dat`
  **directly under `tact_export/`** when not — revised 2026-08-16 (Luna):
  originally specified as `_unresolved/FILE########.dat`, mirroring
  `casc-tool`'s own `wow_export/` convention exactly; dropped the
  `_unresolved/` nesting once real usage showed it added a layer with no
  payoff while `--listfile` support doesn't exist to ever populate the
  other half of that split. If `--listfile` support is added later,
  revisit whether reintroducing a real/unresolved split is worth it once
  both cases genuinely occur side by side — for now, everything is
  unresolved, so a split has nothing to split.
- **`tact_export/` stays a permanently separate tree** — no automatic or
  tool-driven merge into `wow_export/`. Different provenance/trust level
  (fetched live from Blizzard vs. extracted from a local install) stays
  visible for as long as both trees exist; if a human ever wants to
  collapse them, that's a manual, deliberate action outside this tool's
  scope, not a feature to build.