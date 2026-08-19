#include <cstdio>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>

#include "casc_storage.h"
#include "dry_run.h"
#include "fetch.h"
#include "locale.h"
#include "plan.h"
#include "worklist.h"

namespace {

struct CommonArgs {
  std::filesystem::path from_list;
  std::filesystem::path install_path;
  std::filesystem::path export_root = "tact_export";
  std::string locale = "all";
};

void PrintUsage() {
  std::cerr
      << "usage: tact-fetch <dry-run|fetch> --from-list <file> --install <path> [--export <dir>] [--locale <code>]\n"
         "\n"
         "  --from-list <file>  required. Plain text, one FileDataID per line\n"
         "                      (casc-tool's own extract-batch --from-list convention).\n"
         "                      There is no mode that scans the local install on its own\n"
         "                      to decide what's missing -- see DESIGN.md #3/#7.\n"
         "  --install <path>    required. Local WoW install directory containing\n"
         "                      .build.info, opened strictly read-only.\n"
         "  --export <dir>      optional, default 'tact_export'. Fetched files land\n"
         "                      here (fetch only; unused by dry-run).\n"
         "  --locale <code>     optional, default 'all'. Which locale-variant CKey\n"
         "                      handle B (the actual fetch, fetch only; unused by\n"
         "                      dry-run) resolves each FileDataID to, e.g. 'enUS',\n"
         "                      'koKR', 'deDE' -- see locale.h for the full list.\n"
         "                      Local resolution (dry-run's already-local/missing\n"
         "                      check) always uses 'all' regardless of this flag.\n";
}

// Returns nullopt (and has already printed an expected-vs-actual usage
// error) if required flags are missing.
std::optional<CommonArgs> ParseCommonArgs(int argc, char** argv) {
  CommonArgs args;

  for (int i = 0; i < argc; ++i) {
    std::string_view flag = argv[i];
    if (flag == "--from-list" && i + 1 < argc) {
      args.from_list = argv[++i];
    } else if (flag == "--install" && i + 1 < argc) {
      args.install_path = argv[++i];
    } else if (flag == "--export" && i + 1 < argc) {
      args.export_root = argv[++i];
    } else if (flag == "--locale" && i + 1 < argc) {
      args.locale = argv[++i];
    } else {
      std::cerr << "tact-fetch: expected one of --from-list/--install/--export/--locale, actual: '" << flag
                << "'\n";
      return std::nullopt;
    }
  }

  if (args.from_list.empty()) {
    std::cerr << "tact-fetch: expected --from-list <file> (mandatory, no full-scan fallback exists), actual: not "
                  "given\n";
    return std::nullopt;
  }
  if (args.install_path.empty()) {
    std::cerr << "tact-fetch: expected --install <path>, actual: not given\n";
    return std::nullopt;
  }

  return args;
}

// Shared by both subcommands: load the worklist, open the real install
// read-only, and resolve every ID against it. No network touched here --
// resolution is a local manifest read only (DESIGN.md #7 step 1/2).
std::optional<tactfetch::PlanSummary> BuildPlanFromArgs(const CommonArgs& args) {
  std::vector<uint32_t> worklist;
  try {
    worklist = tactfetch::LoadWorklist(args.from_list);
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return std::nullopt;
  }

  if (worklist.empty()) {
    std::cerr << "tact-fetch: expected at least one FileDataID in '" << args.from_list.string()
              << "', actual: file has none -- refusing to run with an empty scope\n";
    return std::nullopt;
  }

  std::optional<tactfetch::LocalStorage> storage = tactfetch::LocalStorage::OpenReadOnly(args.install_path);
  if (!storage) return std::nullopt;

  return tactfetch::BuildPlan(worklist, *storage);
}

std::filesystem::path StateDir() {
  return std::filesystem::temp_directory_path() / "tact-fetch";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage();
    return 1;
  }

  std::string_view verb = argv[1];
  if (verb != "dry-run" && verb != "fetch") {
    std::cerr << "tact-fetch: expected 'dry-run' or 'fetch', actual: '" << verb << "'\n";
    PrintUsage();
    return 1;
  }

  std::optional<CommonArgs> args = ParseCommonArgs(argc - 2, argv + 2);
  if (!args) {
    PrintUsage();
    return 1;
  }

  std::optional<tactfetch::PlanSummary> plan = BuildPlanFromArgs(*args);
  if (!plan) return 1;

  if (verb == "dry-run") {
    tactfetch::RunDryRun(*plan, StateDir());
    return 0;
  }

  std::optional<uint32_t> locale_mask = tactfetch::ParseLocaleCode(args->locale);
  if (!locale_mask) {
    std::cerr << "tact-fetch: expected a known --locale code (e.g. 'all', 'enUS', 'koKR' -- see locale.h), actual: '"
              << args->locale << "'\n";
    return 1;
  }

  return tactfetch::RunFetch(*plan, args->install_path, StateDir(), args->export_root, *locale_mask) ? 0 : 1;
}
