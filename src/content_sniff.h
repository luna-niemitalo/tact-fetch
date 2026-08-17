#pragma once

#include <string>
#include <vector>

namespace tactfetch {

// Sniffs decoded file content for a known Blizzard/standard format magic
// signature, to correct a fetched file's output extension beyond the
// generic ".dat" a FileDataID alone can't tell us. CascLib's own
// decode/verify already ran by the time this looks at anything -- this
// never changes what got written, only what it's named. Returns the
// extension without a leading dot (e.g. "m2"), or an empty string if
// nothing recognized. Magic bytes verified empirically against real
// files extracted from a real install (2026-08-17), not guessed from
// documentation alone.
std::string SniffExtension(const std::vector<unsigned char>& data);

}  // namespace tactfetch
