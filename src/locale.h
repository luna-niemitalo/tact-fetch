#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace tactfetch {

// Maps a locale code (casc-tool's own convention: "enUS", "koKR", ... or
// "all") to the CascLib CASC_LOCALE_* bitmask RunFetch's handle B should
// open with (fetch.h). Returns nullopt for an unrecognized code -- the
// caller prints expected-vs-actual and refuses, this function doesn't.
std::optional<uint32_t> ParseLocaleCode(std::string_view code);

}  // namespace tactfetch
