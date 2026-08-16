#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "casc_storage.h"

namespace tactfetch {

struct PlanSummary {
  std::vector<ResolvedEntry> to_fetch;
  size_t already_local = 0;
  size_t encrypted = 0;
  size_t not_in_manifest = 0;
  uint64_t total_bytes = 0;  // sum of ContentSize over to_fetch entries with known size only

  // CascLib's public API can't report content size or content flags for a
  // file that isn't locally available (see casc_storage.h) -- so a
  // missing file's encryption status is genuinely unknown until it's
  // actually fetched. This counts how many `to_fetch` entries fall into
  // that gap; `total_bytes` silently excludes their size rather than
  // reporting a false total.
  size_t to_fetch_unknown_size = 0;
};

// Resolves every ID in `worklist` against `storage` and buckets it by
// ResolveStatus (DESIGN.md #7 step 2). `to_fetch` is the only bucket a
// real run ever touches; the others are reported but never acted on.
PlanSummary BuildPlan(const std::vector<uint32_t>& worklist, const LocalStorage& storage);

// Stable digest over the resolved fetch scope (sorted FileDataIDs), used
// to key a dry-run marker to the exact set of files it was computed for
// (DESIGN.md #7 step 3/4).
std::string PlanScopeHash(const PlanSummary& plan);

}  // namespace tactfetch
