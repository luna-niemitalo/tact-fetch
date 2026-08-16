#include "dry_run.h"

#include <chrono>
#include <fstream>
#include <iostream>

#include "politeness.h"

namespace tactfetch {

namespace {

std::filesystem::path MarkerPath(const PlanSummary& plan, const std::filesystem::path& state_dir) {
  return state_dir / (PlanScopeHash(plan) + ".dryrun");
}

}  // namespace

void RunDryRun(const PlanSummary& plan, const std::filesystem::path& state_dir) {
  double estimated_seconds = static_cast<double>(plan.to_fetch.size()) / politeness::kMaxNewRequestsPerSecond;

  std::cout << "dry run (no network touched):\n"
            << "  to fetch:          " << plan.to_fetch.size() << " files, " << plan.total_bytes
            << " bytes (known-size only";
  if (plan.to_fetch_unknown_size > 0) {
    std::cout << " -- " << plan.to_fetch_unknown_size
              << " of those have unknown size AND unknown encryption status until actually fetched; "
                 "CascLib's local-only API can't report either for a file this install never downloaded)";
  } else {
    std::cout << ")";
  }
  std::cout << "\n"
            << "  already local:     " << plan.already_local << " (skipped)\n"
            << "  encrypted:         " << plan.encrypted
            << " (skipped, needs a TACT key first -- only counts files confirmed encrypted; see the unknown-size "
               "note above for missing files whose encryption status couldn't be checked)\n"
            << "  not in manifest:   " << plan.not_in_manifest << " (skipped, not a real FileDataID for this build)\n"
            << "  estimated time:    " << estimated_seconds << "s at " << politeness::kMaxNewRequestsPerSecond
            << " new requests/sec (concurrency and per-file size not factored in -- a floor, not a ceiling)\n";

  std::filesystem::create_directories(state_dir);
  std::ofstream marker(MarkerPath(plan, state_dir), std::ios::trunc);
  auto created_at = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
  marker << "scope_hash=" << PlanScopeHash(plan) << '\n'
         << "file_count=" << plan.to_fetch.size() << '\n'
         << "total_bytes=" << plan.total_bytes << '\n'
         << "created_at_epoch_seconds=" << created_at << '\n';

  std::cout << "marker written: " << MarkerPath(plan, state_dir).string() << " (fresh for "
            << politeness::kDryRunMarkerFreshnessMinutes << " minutes)\n";
}

bool HasFreshDryRunMarker(const PlanSummary& plan, const std::filesystem::path& state_dir) {
  std::filesystem::path path = MarkerPath(plan, state_dir);
  std::ifstream marker(path);
  if (!marker) return false;

  int64_t created_at = -1;
  std::string line;
  while (std::getline(marker, line)) {
    constexpr std::string_view kKey = "created_at_epoch_seconds=";
    if (line.rfind(kKey, 0) == 0) {
      created_at = std::stoll(line.substr(kKey.size()));
      break;
    }
  }
  if (created_at < 0) return false;

  auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                 .count();
  int64_t age_minutes = (now - created_at) / 60;
  return age_minutes >= 0 && age_minutes < politeness::kDryRunMarkerFreshnessMinutes;
}

}  // namespace tactfetch
