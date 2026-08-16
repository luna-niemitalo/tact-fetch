#include "log.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace tactfetch {

namespace {

std::string TimestampNow() {
  auto now = std::chrono::system_clock::now();
  std::time_t now_c = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
  gmtime_r(&now_c, &utc);

  std::ostringstream out;
  out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

}  // namespace

EventLog::EventLog(const std::filesystem::path& path) {
  std::filesystem::create_directories(path.parent_path());
  stream_.open(path, std::ios::app);
  if (!stream_) {
    throw std::runtime_error("EventLog: could not open '" + path.string() + "' for append");
  }
}

void EventLog::Write(std::string_view line) {
  stream_ << TimestampNow() << ' ' << line << '\n';
  stream_.flush();
}

}  // namespace tactfetch
