#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace tactfetch {

enum class ResolveStatus {
  kMissingLocally,  // known to the manifest, bytes not downloaded -- fetch candidate
  kAlreadyLocal,    // known to the manifest, bytes already present -- skip, nothing to do
  kEncrypted,       // content-flagged encrypted -- out of scope, needs a TACT key first
  kNotInManifest,   // FileDataID not present in this build's manifest at all
};

struct ResolvedEntry {
  uint32_t file_data_id = 0;
  std::array<unsigned char, 16> ckey{};
  std::array<unsigned char, 16> ekey{};

  // Only reliably known when the file is already available locally --
  // CascLib's public API gates content size/flags behind opening the
  // local data stream (CascGetFileInfo(CascFileFullInfo) calls
  // EnsureFileSpanFramesLoaded unconditionally), which fails by
  // construction for a file this install never downloaded. CKey/EKey
  // don't have that problem (read straight off the CKey-table entry via
  // CascFileContentKey/CascFileEncodedKey, no stream needed), so those
  // are always populated once CascOpenFile succeeds.
  uint64_t content_size = 0;         // 0 if !metadata_complete
  bool metadata_complete = false;    // true iff content_size/is_encrypted are real, not placeholders
  bool is_encrypted = false;         // only meaningful when metadata_complete
};

struct ResolveResult {
  ResolveStatus status;
  ResolvedEntry entry;  // only meaningful when status != kNotInManifest
};

// Read-only wrapper around a local CASC storage (CascOpenStorage, no
// CASC_FEATURE_ONLINE) -- "handle A" from DESIGN.md #9. Resolves worklist
// FileDataIDs to EKey/CKey/ContentSize and local-availability status via
// direct per-ID lookups (CascOpenFile CASC_OPEN_BY_FILEID), never a
// CascFindFirstFile/NextFile storage scan -- this tool fetches a
// caller-named list, it never discovers its own scope.
class LocalStorage {
 public:
  // Returns std::nullopt and prints an expected-vs-actual diagnostic to
  // stderr on failure (missing/invalid install path, not a CASC storage).
  static std::optional<LocalStorage> OpenReadOnly(const std::filesystem::path& install_path);

  LocalStorage(LocalStorage&& other) noexcept;
  LocalStorage& operator=(LocalStorage&& other) noexcept;
  LocalStorage(const LocalStorage&) = delete;
  LocalStorage& operator=(const LocalStorage&) = delete;
  ~LocalStorage();

  ResolveResult Resolve(uint32_t file_data_id) const;

 private:
  explicit LocalStorage(void* handle) : handle_(handle) {}

  void* handle_ = nullptr;
};

}  // namespace tactfetch
