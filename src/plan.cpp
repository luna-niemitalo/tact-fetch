#include "plan.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace tactfetch {

PlanSummary BuildPlan(const std::vector<uint32_t>& worklist, const LocalStorage& storage) {
  PlanSummary plan;

  for (uint32_t file_data_id : worklist) {
    ResolveResult result = storage.Resolve(file_data_id);

    switch (result.status) {
      case ResolveStatus::kMissingLocally:
        if (result.entry.metadata_complete) {
          plan.total_bytes += result.entry.content_size;
        } else {
          ++plan.to_fetch_unknown_size;
        }
        plan.to_fetch.push_back(result.entry);
        break;
      case ResolveStatus::kAlreadyLocal:
        ++plan.already_local;
        break;
      case ResolveStatus::kEncrypted:
        ++plan.encrypted;
        break;
      case ResolveStatus::kNotInManifest:
        ++plan.not_in_manifest;
        break;
    }
  }

  return plan;
}

std::string PlanScopeHash(const PlanSummary& plan) {
  std::vector<uint32_t> ids;
  ids.reserve(plan.to_fetch.size());
  for (const auto& entry : plan.to_fetch) ids.push_back(entry.file_data_id);
  std::sort(ids.begin(), ids.end());

  size_t seed = ids.size();
  for (uint32_t id : ids) {
    // boost::hash_combine's mixing constant -- fine for a non-cryptographic
    // scope-identity digest, not used for anything security-sensitive.
    seed ^= std::hash<uint32_t>{}(id) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  }

  std::ostringstream out;
  out << std::hex << std::setw(16) << std::setfill('0') << seed;
  return out.str();
}

}  // namespace tactfetch
