#include "cache_dir.h"

#include <cstdlib>

namespace tactfetch {

std::filesystem::path TactFetchCacheDir() {
  const char* xdg_cache = std::getenv("XDG_CACHE_HOME");
  if (xdg_cache != nullptr && xdg_cache[0] != '\0') {
    return std::filesystem::path(xdg_cache) / "tact-fetch";
  }

  const char* home = std::getenv("HOME");
  if (home != nullptr && home[0] != '\0') {
    return std::filesystem::path(home) / ".cache" / "tact-fetch";
  }

  // No HOME, no XDG_CACHE_HOME -- fall back to a temp dir rather than
  // fail outright. Not "central" in that case, but every other path here
  // requires an environment this broken to already be unusable anyway.
  return std::filesystem::temp_directory_path() / "tact-fetch";
}

}  // namespace tactfetch
