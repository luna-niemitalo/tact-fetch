#include "worklist.h"

#include <cctype>
#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace tactfetch {

namespace {

std::string_view Trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

}  // namespace

std::vector<uint32_t> LoadWorklist(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error(
        "LoadWorklist: expected a readable --from-list file, actual: could not open '" + path.string() + "'");
  }

  std::vector<uint32_t> ids;
  std::string raw_line;
  int line_no = 0;

  while (std::getline(in, raw_line)) {
    ++line_no;
    std::string_view line = Trim(raw_line);
    if (line.empty()) continue;

    uint32_t id = 0;
    auto [ptr, ec] = std::from_chars(line.data(), line.data() + line.size(), id);
    if (ec != std::errc{} || ptr != line.data() + line.size()) {
      std::ostringstream msg;
      msg << "LoadWorklist: expected a bare decimal FileDataID on " << path.string() << ':' << line_no
          << ", actual: '" << line << "'";
      throw std::runtime_error(msg.str());
    }

    ids.push_back(id);
  }

  return ids;
}

}  // namespace tactfetch
