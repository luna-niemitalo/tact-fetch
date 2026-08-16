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
the local ENCODING/ROOT manifest), so it vendors its own copy of CascLib,
the same way casc-tool does (`vendor/CascLib` git submodule, spliced in at
build time — see casc-tool's own README "Why this needed more than 'just
cmake'" section for the exact mechanism if useful as a reference). The only
sanctioned coupling between the two projects is a **plain text file
format**: one FileDataID per line, identical to casc-tool's own
`extract-batch --from-list` convention — this tool can optionally accept
such a file as a filter/seed (e.g. "only fetch these specific IDs," useful
for a targeted retry or a scoped test run), but must also be able to
compute its own worklist from scratch by scanning the storage directly.
Nothing about that coupling requires casc-tool's source, only its file
format.

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
- **No silent full-corpus default.** If invoked with no scope-limiting
  input at all (no `--from-list`, no explicit count cap), refuse to run
  rather than defaulting to "fetch everything this storage says is
  missing" — the scope of a run should always be something the caller
  named on purpose.
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
2. Build the worklist:
     - if --from-list <file> given: use exactly those FileDataIDs
       (still filtered through the storage's own bFileAvailable check --
       don't re-fetch something already local)
     - else: scan the whole storage (CascFindFirstFile/NextFile), collect
       every entry with a real FileDataID, bFileAvailable=false, and not
       content-flagged encrypted (encrypted-but-undownloaded needs a TACT
       key too -- fetching ciphertext nobody can decrypt yet isn't useful
       output; leave that bucket for a future pass once keys exist, don't
       silently fetch it as if it were done)
   Either way, resolve each ID's real EKey/CKey/ContentSize from the local
   manifest -- this is what makes the CDN URL and lets a post-fetch hash
   check happen.
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
   marker: bounded worker pool (§5),
   per-file: GET from the CDN path derived from .build.info's CDN
   Path/Servers + the TACT archive URL convention (EKey-based path,
   documented on wowdev.wiki -- verify against a real successful fetch
   before trusting the exact path shape), verify the response hash against
   the expected CKey/EKey, write to tact_export/<real-path-if-known, else
   an _unresolved/ convention matching casc-tool's own>, checkpoint
   (append to a resume log) on each success.
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
    instead of by replacing it. **Open follow-up, not yet resolved**:
    confirm `CascOpenStorageEx` actually accepts/exposes a way to set a
    custom `User-Agent` and to throttle concurrent/in-flight requests
    (§5's ceilings) — if the online-storage API can't be paced or
    identified from the outside, that reopens this decision rather than
    settling it. libcurl stays listed as the dependency this repo already
    vendors/links (`nix/flake.nix`, `CMakeLists.txt`) pending that
    confirmation; removing it is a follow-up if CascLib's path proves
    sufficient on its own.
- **`tact_export/` naming**: mirror `casc-tool`'s own `wow_export/`
  convention *exactly and precisely* — real listfile-resolved path when
  known, `_unresolved/FILE########.dat` when not. The directory name
  itself (`tact_export/` vs `wow_export/`) already carries the
  provenance signal ("this came from the CDN, not a local extraction");
  the naming scheme underneath it doesn't need to re-encode that a second
  way, and matching it exactly is what lets a downstream consumer (e.g.
  `husk`) treat the two trees as interchangeable path-for-path.
- **`tact_export/` stays a permanently separate tree** — no automatic or
  tool-driven merge into `wow_export/`. Different provenance/trust level
  (fetched live from Blizzard vs. extracted from a local install) stays
  visible for as long as both trees exist; if a human ever wants to
  collapse them, that's a manual, deliberate action outside this tool's
  scope, not a feature to build.