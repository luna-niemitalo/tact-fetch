# tact-fetch

A tool that reads a local, already-installed World of Warcraft CASC storage
read-only, finds FileDataIDs whose manifest entry exists but whose actual
bytes were never downloaded to this install, and fetches those specific
bytes from Blizzard's public CDN — politely, slowly, on purpose — into a
`tact_export/` directory alongside an existing `wow_export/` tree (a
`casc-tool extract-batch` output), so a downstream consumer (e.g. `husk`)
has a second, independent source of already-decoded CASC content.

This is a sibling project to [`casc-tool`](../casc-tool), not a fork or a
dependency of it — no shared code, only a shared plain-text FileDataID
worklist format. See `DESIGN.md` §3 for the full reasoning behind keeping
these separate.

## Status

The local-only half works: reading a real install's manifest, loading and
validating a `--from-list` worklist, and the dry-run report. **The actual
CDN fetch is not implemented** — it's a deliberate no-op scaffold that
touches no network. See `CLAUDE.md`'s `## Status`/`## Resume` sections for
exactly where things stand, and `DESIGN.md` for the full architecture.

## What you need before this works (once built)

1. **A build of the tool.** Not yet available — see `## Status` above.
2. **A WoW install to read locally.** Same access pattern as `casc-tool`:
   a directory containing a `.build.info` file, opened strictly read-only.
3. **A listfile**, from its actual upstream source:
   [wowdev/wow-listfile releases](https://github.com/wowdev/wow-listfile/releases).
4. **Network access to Blizzard's public CDN**, for the fetch step only —
   everything else in this tool (storage scan, worklist build, dry-run) is
   local-only and needs no network at all.

## Design notes

See `DESIGN.md` for the full handoff: why this is a separate project from
`casc-tool` (§3), the grounded findings on CascLib's own online-fetch
mechanics (§4), the concrete politeness policy this tool commits to before
any real run touches the network (§5), the design principles inherited
from `casc-tool` (§6), the rough architecture (§7), explicit non-goals
(§8), and the open questions left for whoever builds this (§9).

## Disclaimer

This tool is co-coded by AI. It's verified by a massively autistic
developer — every claim in this README and in `DESIGN.md`'s empirical
findings is checked against the real thing, not taken on faith. Until
`## Status` above says otherwise, nothing in this repo has been run
against a real install or a real CDN.
