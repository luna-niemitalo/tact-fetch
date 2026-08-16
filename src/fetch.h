#pragma once

#include <filesystem>

#include "plan.h"

namespace tactfetch {

// Runs the fetch step: refuses (loud error, no network touched) unless a
// fresh dry-run marker exists for this exact plan scope (DESIGN.md #7
// step 4). On success, opens a second, independent CascLib storage
// handle in online mode (DESIGN.md #9's fetch-pipeline resolution):
// `szLocalPath` is `TactFetchCacheDir()/online-cache/<product>-<region>`
// (cache_dir.h) -- central and persistent, not per-invocation, and never
// `install_path` -- every byte that handle's own local-cache bookkeeping
// touches stays off the real install. Its per-file network fetch goes
// through vendored CascLib's `HttpDownloadFile`, locally patched to use
// libcurl (HTTP/2, an honest User-Agent, a real timeout) instead of
// CascLib's raw-socket client -- everything downstream of that one
// patched function (BLTE decode, CKey/EKey hash verification) is exactly
// CascLib's own, unmodified. `install_path` is read only for
// `.build.info`'s product/region (needed to open the online handle),
// never written to.
//
// Successfully decoded files land under `export_root/_unresolved/` (no
// --listfile support yet, so no real path is ever known). A file whose
// BLTE decode fails with ERROR_FILE_ENCRYPTED lands at the same path
// with `.encrypted` appended to the filename, holding the raw
// (still-encoded) fetched bytes -- visible and inspectable later, never
// silently dropped, and never mistaken for real decoded content by an
// extension-matching consumer.
bool RunFetch(const PlanSummary& plan, const std::filesystem::path& install_path,
              const std::filesystem::path& state_dir, const std::filesystem::path& export_root);

}  // namespace tactfetch
