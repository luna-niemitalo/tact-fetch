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

std::string HexLower(const std::array<unsigned char, 16>& key) {
  static const char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(32);
  for (unsigned char b : key) {
    out.push_back(kHex[b >> 4]);
    out.push_back(kHex[b & 0x0F]);
  }
  return out;
}

// Mirrors CASC_PATH::AppendEKey's on-disk convention exactly
// (vendor/CascLib/src/common/Path.h, vendor/CascLib/src/CascFiles.cpp's
// FetchCascFile): <root>/data/<hex[0:2]>/<hex[2:4]>/<full 32-char hex
// EKey>, no extension. This is where the raw (still BLTE-encoded) fetched
// bytes for a "data" file land in the online handle's own local cache,
// regardless of whether CascReadFile can decode them afterward.
std::filesystem::path RawCachePath(const std::filesystem::path& online_cache_dir,
                                    const std::array<unsigned char, 16>& ekey) {
  std::string hex = HexLower(ekey);
  return online_cache_dir / "data" / hex.substr(0, 2) / hex.substr(2, 2) / hex;
}

std::filesystem::path OutputPath(const std::filesystem::path& export_root, uint32_t file_data_id, bool encrypted) {
  char name[32];
  std::snprintf(name, sizeof(name), "FILE%08X.dat", file_data_id);
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
FetchOutcome FetchOneFile(HANDLE online_storage, const std::filesystem::path& online_cache_dir,
                           const std::filesystem::path& export_root, const ResolvedEntry& entry) {
  HANDLE file_handle = nullptr;
  if (!CascOpenFile(online_storage, CASC_FILE_DATA_ID(entry.file_data_id), 0, CASC_OPEN_BY_FILEID, &file_handle)) {
    return {FetchKind::kFailed, "CascOpenFile failed, CascLib error " + std::to_string(GetCascError())};
  }

  auto handle_encrypted = [&]() -> FetchOutcome {
    std::filesystem::path raw = RawCachePath(online_cache_dir, entry.ekey);
    std::error_code ec;
    if (!std::filesystem::exists(raw, ec)) {
      return {FetchKind::kEncrypted, "encrypted; the raw fetch itself did not leave a cached blob to preserve"};
    }
    std::filesystem::path out = OutputPath(export_root, entry.file_data_id, true);
    std::filesystem::create_directories(out.parent_path(), ec);
    std::filesystem::copy_file(raw, out, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
      return {FetchKind::kFailed, "encrypted, but copying the raw bytes to " + out.string() + " failed: " +
                                       ec.message()};
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

  std::filesystem::path out = OutputPath(export_root, entry.file_data_id, false);
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
              const std::filesystem::path& state_dir, const std::filesystem::path& export_root) {
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
  open_args.dwLocaleMask = CASC_LOCALE_ALL;
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
      outcome = FetchOneFile(online_storage, online_cache_dir, export_root, entry);
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
