#include "build_info.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace tactfetch {

namespace {

std::vector<std::string> SplitPipe(const std::string& line) {
  std::vector<std::string> parts;
  std::stringstream ss(line);
  std::string part;
  while (std::getline(ss, part, '|')) parts.push_back(part);
  return parts;
}

// ".build.info" column headers look like "Product!STRING:0" -- strip the
// "!TYPE:size" suffix CascLib's own CSV parser also strips.
std::string ColumnName(const std::string& header) {
  size_t bang = header.find('!');
  return bang == std::string::npos ? header : header.substr(0, bang);
}

}  // namespace

std::optional<BuildInfo> ReadBuildInfo(const std::filesystem::path& install_path) {
  std::filesystem::path path = install_path / ".build.info";
  std::ifstream in(path);
  if (!in) {
    std::cerr << "ReadBuildInfo: expected a readable '.build.info' in '" << install_path.string()
              << "', actual: could not open '" << path.string() << "'\n";
    return std::nullopt;
  }

  std::string header_line;
  if (!std::getline(in, header_line)) {
    std::cerr << "ReadBuildInfo: expected a header line in '" << path.string() << "', actual: empty file\n";
    return std::nullopt;
  }
  std::vector<std::string> headers = SplitPipe(header_line);

  int product_col = -1, branch_col = -1, active_col = -1;
  for (size_t i = 0; i < headers.size(); ++i) {
    std::string name = ColumnName(headers[i]);
    if (name == "Product") product_col = static_cast<int>(i);
    if (name == "Branch") branch_col = static_cast<int>(i);
    if (name == "Active") active_col = static_cast<int>(i);
  }
  if (product_col < 0 || branch_col < 0 || active_col < 0) {
    std::cerr << "ReadBuildInfo: expected 'Product'/'Branch'/'Active' columns in '" << path.string()
              << "', actual: header was '" << header_line << "'\n";
    return std::nullopt;
  }

  std::string row_line;
  while (std::getline(in, row_line)) {
    if (row_line.empty()) continue;
    std::vector<std::string> row = SplitPipe(row_line);
    size_t max_col = static_cast<size_t>(std::max({product_col, branch_col, active_col}));
    if (row.size() <= max_col) continue;
    if (row[active_col] != "1") continue;

    return BuildInfo{row[product_col], row[branch_col]};
  }

  std::cerr << "ReadBuildInfo: expected a row with Active=1 in '" << path.string() << "', actual: none found\n";
  return std::nullopt;
}

}  // namespace tactfetch
