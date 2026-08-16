#pragma once

#include <filesystem>

namespace tactfetch {

// One central, persistent cache shared by every invocation of this tool,
// regardless of which directory it's run from or which install/worklist
// it's pointed at -- `$XDG_CACHE_HOME/tact-fetch` (falling back to
// `$HOME/.cache/tact-fetch`). This is where handle B's online-storage
// cache (CDN archive indices, config, fetched data) lives: that
// bootstrap is expensive (DESIGN.md #9's "index-bootstrap cost" finding)
// and worklist-independent, so paying it once and reusing it across
// every future run -- rather than per invocation, the way an ephemeral
// temp-dir-per-run scratch dir would -- is the whole point of making it
// central instead of throwaway.
std::filesystem::path TactFetchCacheDir();

}  // namespace tactfetch
