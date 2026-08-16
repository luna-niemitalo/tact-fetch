#include "casc_storage.h"

#include <cstring>
#include <iostream>

#include "CascLib.h"

namespace tactfetch {

std::optional<LocalStorage> LocalStorage::OpenReadOnly(const std::filesystem::path& install_path) {
  HANDLE handle = nullptr;
  // dwLocaleMask=0 is CASC_LOCALE_NONE, not "unfiltered" -- passing it
  // silently corrupts which locale-variant CKey entry the root handler
  // resolves for locale-flagged files (confirmed against a real install:
  // it resolved FileDataID 21 to the wrong, already-local entry instead
  // of correctly reporting it missing). CASC_LOCALE_ALL matches
  // casc-tool's own default and is what "resolve regardless of locale"
  // actually requires.
  if (!CascOpenStorage(install_path.string().c_str(), CASC_LOCALE_ALL, &handle)) {
    std::cerr << "LocalStorage::OpenReadOnly: expected '" << install_path.string()
              << "' to be a valid CASC storage directory (containing .build.info), actual: CascOpenStorage failed"
                 " with CascLib error code "
              << GetCascError() << '\n';
    return std::nullopt;
  }
  return LocalStorage(handle);
}

LocalStorage::LocalStorage(LocalStorage&& other) noexcept : handle_(other.handle_) {
  other.handle_ = nullptr;
}

LocalStorage& LocalStorage::operator=(LocalStorage&& other) noexcept {
  if (this != &other) {
    if (handle_ != nullptr) CascCloseStorage(handle_);
    handle_ = other.handle_;
    other.handle_ = nullptr;
  }
  return *this;
}

LocalStorage::~LocalStorage() {
  if (handle_ != nullptr) CascCloseStorage(handle_);
}

ResolveResult LocalStorage::Resolve(uint32_t file_data_id) const {
  HANDLE file_handle = nullptr;
  if (!CascOpenFile(handle_, CASC_FILE_DATA_ID(file_data_id), 0, CASC_OPEN_BY_FILEID, &file_handle)) {
    // The root handler has no entry at all for this ID -- not a real
    // FileDataID for this build, distinct from "known but not local".
    return {ResolveStatus::kNotInManifest, {}};
  }

  ResolvedEntry entry;
  entry.file_data_id = file_data_id;

  // CKey/EKey come straight off the CKey-table entry (no data-stream
  // access), so these succeed regardless of local availability.
  BYTE ckey_buf[16];
  BYTE ekey_buf[16];
  bool got_ckey = CascGetFileInfo(file_handle, CascFileContentKey, ckey_buf, sizeof(ckey_buf), nullptr);
  bool got_ekey = CascGetFileInfo(file_handle, CascFileEncodedKey, ekey_buf, sizeof(ekey_buf), nullptr);
  if (got_ckey) std::memcpy(entry.ckey.data(), ckey_buf, entry.ckey.size());
  if (got_ekey) std::memcpy(entry.ekey.data(), ekey_buf, entry.ekey.size());

  // CascFileFullInfo (content size, content flags) calls
  // EnsureFileSpanFramesLoaded internally, which has to open the local
  // data-archive stream -- that fails for a file this install never
  // downloaded. Its success/failure is therefore the direct, per-ID
  // signal for local availability, with no storage scan needed, but it
  // also means content_size/is_encrypted are only known for files that
  // turn out to already be local.
  CASC_FILE_FULL_INFO info{};
  bool locally_available = CascGetFileInfo(file_handle, CascFileFullInfo, &info, sizeof(info), nullptr);
  CascCloseFile(file_handle);

  if (locally_available) {
    entry.content_size = info.ContentSize;
    entry.metadata_complete = true;
    entry.is_encrypted = (info.ContentFlags & CASC_CFLAG_ENCRYPTED) != 0;
    return {entry.is_encrypted ? ResolveStatus::kEncrypted : ResolveStatus::kAlreadyLocal, entry};
  }

  return {ResolveStatus::kMissingLocally, entry};
}

}  // namespace tactfetch
