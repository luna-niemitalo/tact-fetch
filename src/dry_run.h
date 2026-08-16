#pragma once

#include <filesystem>

#include "plan.h"

namespace tactfetch {

// Prints the dry-run report (file count, total bytes, estimated
// wall-clock at the configured rate limit -- DESIGN.md #7 step 3) and
// writes a timestamped marker under `state_dir`, keyed to this exact
// resolved plan scope. Touches no network.
void RunDryRun(const PlanSummary& plan, const std::filesystem::path& state_dir);

// True if a marker for this exact plan scope exists and is still fresh
// (DESIGN.md #7 step 4's freshness gate,
// politeness::kDryRunMarkerFreshnessMinutes). A real run refuses to start
// without this.
bool HasFreshDryRunMarker(const PlanSummary& plan, const std::filesystem::path& state_dir);

}  // namespace tactfetch
