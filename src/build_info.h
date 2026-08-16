#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace tactfetch {

struct BuildInfo {
  std::string product;  // "Product" column, e.g. "wow" -- CascOpenOnlineStorage's code_name
  std::string region;   // "Branch" column, e.g. "eu" -- CascOpenOnlineStorage's region
};

// Reads the real install's own `.build.info` (foreign data, validated at
// the boundary) for the product/region pair handle B's online storage
// needs to open -- the same file DESIGN.md #4 already treats as the sole
// source of CDN host/path, never invented. Only the row with Active=1 is
// used, matching CascLib's own selection rule.
std::optional<BuildInfo> ReadBuildInfo(const std::filesystem::path& install_path);

}  // namespace tactfetch
