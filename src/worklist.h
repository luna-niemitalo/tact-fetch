#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace tactfetch {

// Loads a `--from-list` worklist file: casc-tool's own convention, one
// FileDataID per line, plain text. Foreign data -- validated here, at the
// boundary, before anything downstream trusts it.
//
// Throws std::runtime_error with an expected-vs-actual message on the
// first invalid line or on any file-open failure. Blank lines are
// tolerated and skipped; everything else must be a bare decimal FileDataID.
std::vector<uint32_t> LoadWorklist(const std::filesystem::path& path);

}  // namespace tactfetch
