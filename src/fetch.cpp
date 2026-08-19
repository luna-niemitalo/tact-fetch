#include "fetch.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

#include "CascLib.h"
#include "build_info.h"
#include "cache_dir.h"
#include "content_sniff.h"
#include "dry_run.h"
#include "log.h"
#include "politeness.h"

// Set by the locally-patched HttpDownloadFile (vendor/CascLib/src/CascFiles.cpp)
// after every libcurl attempt -- the only channel that survives CascLib's
// coarse DWORD error codes up to here, needed to honor a real 429/503
// Retry-After per DESIGN.md #5 instead of always falling back to blind
// exponential backoff.
extern long g_tactFetchLastHttpStatus;
extern long g_tactFetchLastRetryAfterSeconds;

namespace tactfetch {

namespace {

// A fetched-and-decoded file is not yet a real game asset -- it's raw
// CASC/BLTE-decoded bytes named by FileDataID, not by its in-game path.
// It only becomes a real tree entry once something (a future
// --listfile-driven extraction step, not built yet) resolves that ID to
// a real path and writes it under `export_root` proper. Until then it
// stays under `_unresolved/`, matching casc-tool's own convention --
// never mixed into the real tree just because CascLib decoded it.
// `extension` defaults to "dat" (CASC's own generic convention for an
// unidentified blob) -- pass a sniffed one (content_sniff.h) when the
// decoded bytes were actually available to check.
std::filesystem::path OutputPath(const std::filesystem::path& export_root, uint32_t file_data_id, bool encrypted,
                                  const std::string& extension = "dat") {
  char name[32];
  std::snprintf(name, sizeof(name), "FILE%08X.%s", file_data_id, extension.c_str());
  std::filesystem::path path = export_root / "_unresolved" / name;
  if (encrypted) path += ".encrypted";
  return path;
}

bool WriteFile(const std::filesystem::path& path, const void* data, size_t size) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
  return out.good();
}

enum class FetchKind { kFetched, kEncrypted, kFailed };

struct FetchOutcome {
  FetchKind kind;
  std::string detail;
};

// A single attempt -- no retry logic in here, that lives in the caller so
// it can see politeness::k* limits and the curl patch's Retry-After
// channel above. Opens the file through the online handle (triggering
// CascLib's own fetch-if-missing + BLTE decode + CKey/EKey hash
// verification, all unmodified), then writes the result to
// `export_root`.
FetchOutcome FetchOneFile(HANDLE online_storage, const std::filesystem::path& export_root,
                           const ResolvedEntry& entry) {
  HANDLE file_handle = nullptr;
  // CASC_OVERCOME_ENCRYPTED: without it, a file whose *tail* falls in a
  // block encrypted with a key we don't have gets silently short --
  // CascGetFileSize64 reports a ContentSize smaller than the file's real
  // logical size, no error raised, no way to tell "genuinely this small"
  // from "truncated" from here. With it, that unrecoverable span gets
  // zero-filled instead, matching casc-tool's own fix for the identical
  // symptom (2026-08-16 CHANGELOG) -- see DESIGN.md #9.
  if (!CascOpenFile(online_storage, CASC_FILE_DATA_ID(entry.file_data_id), 0,
                     CASC_OPEN_BY_FILEID | CASC_OVERCOME_ENCRYPTED, &file_handle)) {
    return {FetchKind::kFailed, "CascOpenFile failed, CascLib error " + std::to_string(GetCascError())};
  }

  // Deliberately doesn't try to preserve the raw (still-encoded) fetched
  // bytes: those live in the online handle's own cache under the
  // *archive's* key (DESIGN.md #9's range-fetch patch), never the
  // individual file's own EKey, and CascLib doesn't expose that mapping
  // publicly -- there's no supported way to locate them from here. Just
  // an empty `.encrypted` marker: still visible, still distinct from
  // both "fetched" and "failed", never silently dropped, just honest
  // that the ciphertext itself isn't available to keep.
  auto handle_encrypted = [&]() -> FetchOutcome {
    std::filesystem::path out = OutputPath(export_root, entry.file_data_id, true);
    std::error_code ec;
    std::filesystem::create_directories(out.parent_path(), ec);
    std::ofstream marker(out, std::ios::binary | std::ios::trunc);
    if (!marker) {
      return {FetchKind::kFailed, "encrypted, but creating the marker at " + out.string() + " failed"};
    }
    return {FetchKind::kEncrypted, out.string()};
  };

  ULONGLONG size = 0;
  if (!CascGetFileSize64(file_handle, &size)) {
    DWORD err = GetCascError();
    CascCloseFile(file_handle);
    if (err == ERROR_FILE_ENCRYPTED) return handle_encrypted();
    return {FetchKind::kFailed, "CascGetFileSize64 failed, CascLib error " + std::to_string(err)};
  }

  std::vector<unsigned char> buffer(static_cast<size_t>(size));
  DWORD bytes_read = 0;
  bool ok = size == 0 || CascReadFile(file_handle, buffer.data(), static_cast<DWORD>(size), &bytes_read);
  DWORD err = ok ? 0 : GetCascError();
  CascCloseFile(file_handle);

  if (!ok) {
    if (err == ERROR_FILE_ENCRYPTED) return handle_encrypted();
    return {FetchKind::kFailed, "CascReadFile failed, CascLib error " + std::to_string(err)};
  }

  std::string extension = SniffExtension(buffer);
  std::filesystem::path out =
      OutputPath(export_root, entry.file_data_id, false, extension.empty() ? "dat" : extension);
  if (!WriteFile(out, buffer.data(), bytes_read)) {
    return {FetchKind::kFailed, "decoded and verified, but writing to " + out.string() + " failed"};
  }
  return {FetchKind::kFetched, out.string()};
}

int BackoffSeconds(int attempt) {
  int exp = politeness::kRetryBaseDelaySeconds << (attempt - 1);
  return std::min(exp, politeness::kRetryMaxDelaySeconds);
}

}  // namespace

bool RunFetch(const PlanSummary& plan, const std::filesystem::path& install_path,
              const std::filesystem::path& state_dir, const std::filesystem::path& export_root,
              uint32_t locale_mask) {
  if (!HasFreshDryRunMarker(plan, state_dir)) {
    std::cerr << "RunFetch: expected a fresh dry-run marker (< " << politeness::kDryRunMarkerFreshnessMinutes
              << " minutes old) for this exact scope, actual: none found or stale -- run the dry-run step again "
                 "first\n";
    return false;
  }

  std::optional<BuildInfo> build_info = ReadBuildInfo(install_path);
  if (!build_info) return false;

  // Central and persistent (TactFetchCacheDir, not state_dir): the CDN
  // archive-index bootstrap this pays for (DESIGN.md #9) is expensive
  // and worklist-independent, so it's worth keeping across every future
  // run instead of re-paying it per invocation. Namespaced by
  // product/region since different installs (retail vs. PTR, different
  // regions) have genuinely different CDN content under the hood.
  std::filesystem::path online_cache_dir =
      TactFetchCacheDir() / "online-cache" / (build_info->product + "-" + build_info->region);
  std::filesystem::create_directories(online_cache_dir);
  // CASC_PARAM_SEPARATOR ('*', CascLib.h) -- the doc comment above
  // CascOpenOnlineStorage says ':' but that's stale; ParseOpenParams
  // (CascOpenStorage.cpp) actually splits on '*'.
  std::string storage_params = online_cache_dir.string() + "*" + build_info->product + "*" + build_info->region;

  // CascOpenStorageEx directly, not the CascOpenOnlineStorage wrapper: we
  // need CASC_FEATURE_FORCE_DOWNLOAD, which only the full args struct
  // exposes. Without it, CascLib treats a once-cached "versions"/"cdns"
  // as good forever (LoadCsvFile's LocalCaching() early-return) and this
  // cache would never notice a new build again. The expensive part (the
  // CDN's content-addressed archive indices, DESIGN.md #9) is unaffected
  // by this flag -- those are still only fetched once, on a genuine
  // cache miss, regardless.
  CASC_OPEN_STORAGE_ARGS open_args = {sizeof(CASC_OPEN_STORAGE_ARGS)};
  open_args.dwLocaleMask = locale_mask;
  open_args.dwFlags = CASC_FEATURE_FORCE_DOWNLOAD;

  HANDLE online_storage = nullptr;
  if (!CascOpenStorageEx(storage_params.c_str(), &open_args, true, &online_storage)) {
    std::cerr << "RunFetch: expected to open an online CascLib storage for product '" << build_info->product
              << "' region '" << build_info->region << "' cached at '" << online_cache_dir.string()
              << "', actual: CascOpenOnlineStorage failed with CascLib error " << GetCascError() << '\n';
    return false;
  }

  EventLog log(state_dir / "events.log");
  log.Write("fetch-run-start scope_hash=" + PlanScopeHash(plan) +
            " file_count=" + std::to_string(plan.to_fetch.size()));

  size_t fetched = 0, encrypted = 0, failed = 0;

  for (const auto& entry : plan.to_fetch) {
    FetchOutcome outcome{FetchKind::kFailed, "not attempted"};

    for (int attempt = 1; attempt <= politeness::kRetryMaxAttempts; ++attempt) {
      outcome = FetchOneFile(online_storage, export_root, entry);
      if (outcome.kind != FetchKind::kFailed) break;

      if (attempt == politeness::kRetryMaxAttempts) break;

      int delay = g_tactFetchLastRetryAfterSeconds > 0 ? static_cast<int>(g_tactFetchLastRetryAfterSeconds)
                                                        : BackoffSeconds(attempt);
      log.Write("fetch-retry file_data_id=" + std::to_string(entry.file_data_id) +
                " attempt=" + std::to_string(attempt) + " http_status=" + std::to_string(g_tactFetchLastHttpStatus) +
                " delay_seconds=" + std::to_string(delay) + " detail=\"" + outcome.detail + "\"");
      std::this_thread::sleep_for(std::chrono::seconds(delay));
    }

    const char* result = outcome.kind == FetchKind::kFetched ? "fetched"
                          : outcome.kind == FetchKind::kEncrypted ? "encrypted" : "failed";
    log.Write("fetch file_data_id=" + std::to_string(entry.file_data_id) + " result=" + result + " detail=\"" +
              outcome.detail + "\"");

    if (outcome.kind == FetchKind::kFetched) ++fetched;
    else if (outcome.kind == FetchKind::kEncrypted) ++encrypted;
    else ++failed;

    // §5's rate limit: paces new fetch *starts*, independent of this
    // loop's inherent concurrency=1 (sequential, well under the
    // concurrency ceiling on its own).
    std::this_thread::sleep_for(
        std::chrono::duration<double>(1.0 / politeness::kMaxNewRequestsPerSecond));
  }

  CascCloseStorage(online_storage);

  log.Write("fetch-run-end fetched=" + std::to_string(fetched) + " encrypted=" + std::to_string(encrypted) +
            " failed=" + std::to_string(failed));

  std::cout << "fetch run:\n"
            << "  fetched:                 " << fetched << "\n"
            << "  encrypted (see " << export_root.string() << "/_unresolved/*.encrypted): " << encrypted << "\n"
            << "  failed:                  " << failed << "\n"
            << "  skipped-already-present: " << plan.already_local << "\n";

  return true;
}

}  // namespace tactfetch
