#include "locale.h"

#include <string>
#include <unordered_map>

#include "CascLib.h"

namespace tactfetch {

std::optional<uint32_t> ParseLocaleCode(std::string_view code) {
  static const std::unordered_map<std::string, uint32_t> kCodes = {
      {"all", CASC_LOCALE_ALL},   {"enUS", CASC_LOCALE_ENUS}, {"enGB", CASC_LOCALE_ENGB},
      {"koKR", CASC_LOCALE_KOKR}, {"frFR", CASC_LOCALE_FRFR}, {"deDE", CASC_LOCALE_DEDE},
      {"zhCN", CASC_LOCALE_ZHCN}, {"esES", CASC_LOCALE_ESES}, {"zhTW", CASC_LOCALE_ZHTW},
      {"enCN", CASC_LOCALE_ENCN}, {"enTW", CASC_LOCALE_ENTW}, {"esMX", CASC_LOCALE_ESMX},
      {"ruRU", CASC_LOCALE_RURU}, {"ptBR", CASC_LOCALE_PTBR}, {"itIT", CASC_LOCALE_ITIT},
      {"ptPT", CASC_LOCALE_PTPT},
  };
  auto it = kCodes.find(std::string(code));
  if (it == kCodes.end()) return std::nullopt;
  return it->second;
}

}  // namespace tactfetch
