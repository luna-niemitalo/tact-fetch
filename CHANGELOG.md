# tact-fetch: changelog

Resolved entries move here out of `FAILURES.md` once fixed/closed, so that
file stays a punch list (what's still outstanding) instead of an
ever-growing archive. This file is the archive — historical record only,
nothing here is actionable. Newest first.

---

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
