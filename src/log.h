#pragma once

#include <filesystem>
#include <fstream>
#include <string_view>

namespace tactfetch {

// Plain append-only event log -- DESIGN.md #5: every request logged,
// synchronously, in line, at the moment each event occurs. A crash or kill
// mid-run must still leave a log that accounts for everything that
// happened up to that instant, so every Write() flushes before returning.
class EventLog {
 public:
  explicit EventLog(const std::filesystem::path& path);

  void Write(std::string_view line);

 private:
  std::ofstream stream_;
};

}  // namespace tactfetch
