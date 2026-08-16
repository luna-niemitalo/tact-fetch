# CLAUDE.md — tact-fetch

Global rules apply (`~/.claude/CLAUDE.md`). Nix conventions: `.claude/rules/nix.md`.
Read `DESIGN.md` before any structural change.

## Purpose

Politely fetch CASC files a local WoW install's manifest knows about but never
downloaded, straight from Blizzard's CDN, into a `tact_export/` tree.

## Status

- **Current**: repo scaffolding — `README.md`, `DESIGN.md`, this file, nix dev
  shell + `packages.default`, `CMakeLists.txt`, `vendor/CascLib` (git
  submodule) + matching `casclib` flake input. `src/main.cpp` is a loud stub
  (`tact-fetch: not yet implemented`, exit 1) — nothing beyond that. No CLI,
  no CDN fetch logic, no CascLib usage yet, nothing has touched a real install
  or the real CDN.
- **Target**: see `DESIGN.md` §7 (rough architecture) and §9 (architecture
  decisions — **reversed 2026-08-16**: fetch builds on `CascOpenStorageEx`'s
  online-storage mode, not hand-rolled libcurl HTTP, since it handles CDN
  resolution/BLTE decode/hash verification for free and reimplementing BLTE
  isn't worth it; `tact_export/` mirrors `wow_export/` naming exactly; the
  two trees never auto-merge). §9's revision note has an unresolved
  follow-up: whether `CascOpenStorageEx` can be paced/identified from the
  outside to satisfy §5's politeness policy.
- Anything not listed under Current does not exist yet. Do not describe it as working.

## Boundaries

- **Local CASC storage** (read-only, vendored CascLib once vendored — not yet
  present in this repo): same access pattern as `casc-tool`, never writes back.
- **Blizzard's public CDN** (network, read-only `GET` only): the CDN host/path
  comes from the local install's own `.build.info`, never invented. Every
  fetched blob's hash must be verified against the expected CKey/EKey (already
  known from the local manifest) before being treated as a successful fetch —
  see `DESIGN.md` §6's foreign-data note and `~/nix/claude-rules/FOREIGN_DATA.md`.
- **`--from-list` worklist files** (plain text, one FileDataID per line,
  `casc-tool`'s own convention): foreign data, validated at load, same as any
  file input.
- No other boundaries. This tool never writes to the source WoW install/CASC
  directory, never accepts arbitrary URLs, never proxies for another operator.

## Resume

- **Last state**: `DESIGN.md` §9's HTTP-layer decision was **reversed**
  (2026-08-16): a manual real-world test (separate agent, one real file,
  live CDN) showed `CascOpenStorageEx(..., bOnlineStorage=true)` gives CDN
  resolution, BLTE decode, and hash verification for free, so the fetch
  builds on that instead of hand-rolled libcurl HTTP — reimplementing BLTE
  decoding wasn't worth avoiding CascLib's own online path for. §5's
  politeness policy (concurrency/rate limits, honest `User-Agent`,
  backoff) is unchanged and still mandatory, but now has to be layered
  *around* CascLib's online calls rather than inside a hand-rolled client
  — whether that's actually possible (custom `User-Agent`, external
  pacing) is confirmed **not yet**. `libcurl` is still wired into
  `nix/flake.nix`/`CMakeLists.txt` pending that check. Earlier resolved
  items unaffected: `tact_export/` naming mirrors `wow_export/` exactly;
  the two trees never auto-merge; §5's synchronous per-event logging;
  §7's dry-run-freshness gate. No source code beyond the loud
  `src/main.cpp` stub exists yet.
- **Next step**: confirm whether `CascOpenStorageEx`'s online mode exposes
  a way to set a custom `User-Agent` and to throttle/pace requests from
  the outside (§9's open follow-up) — this determines whether `libcurl`
  stays a real dependency or gets dropped. Independent of that answer,
  local-storage read via CascLib (FileDataID → EKey/CKey/ContentSize) is
  the first piece of code to write either way, followed by worklist
  building (`--from-list` and full-scan modes, §7 step 2), then the
  dry-run report + freshness marker (§7 step 3), then the fetch loop
  itself (§7 step 4, shape now pending the pacing-support answer above).
- **Hazards**: none yet — no code exists to be half-finished. The politeness
  policy in `DESIGN.md` §5 and §9's now-open "can CascLib's online path be
  paced/identified externally" question are hard constraints on whatever
  gets built first, not suggestions to revisit under time pressure. Don't
  start the fetch-loop step without resolving that question — building it
  against an API that turns out unpaceable would mean redoing it.
