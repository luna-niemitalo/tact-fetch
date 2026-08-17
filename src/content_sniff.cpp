#include "content_sniff.h"

#include <algorithm>

namespace tactfetch {

namespace {

bool StartsWith(const std::vector<unsigned char>& data, std::initializer_list<unsigned char> magic) {
  if (data.size() < magic.size()) return false;
  return std::equal(magic.begin(), magic.end(), data.begin());
}

}  // namespace

std::string SniffExtension(const std::vector<unsigned char>& data) {
  // .m2 (models): modern chunked files start with an "MD21" wrapper
  // chunk around an embedded "MD20" body; older files start with "MD20"
  // directly. Verified against a real install's
  // item/objectcomponents/ammo/arrowflight_01.m2.
  if (StartsWith(data, {'M', 'D', '2', '1'}) || StartsWith(data, {'M', 'D', '2', '0'})) return "m2";

  // .blp (textures). Verified against
  // interface/icons/inv_misc_questionmark.blp.
  if (StartsWith(data, {'B', 'L', 'P', '2'})) return "blp";

  // .skin (model geometry LODs). Verified against
  // item/objectcomponents/ammo/arrowflight_0100.skin.
  if (StartsWith(data, {'S', 'K', 'I', 'N'})) return "skin";

  // .db2 (game data tables). "WDC" + a version digit -- WDC1 through
  // WDC5 are all real variants across WoW's history; only WDC5 verified
  // directly here (dbfilesclient/battlepetabilityeffect.db2, build
  // 69299), but the "WDC" prefix is the stable part across versions.
  if (data.size() >= 3 && data[0] == 'W' && data[1] == 'D' && data[2] == 'C') return "db2";

  // .ogg (audio) -- the single largest category in a typical
  // missing-file set (locale voice/music). Verified against
  // sound/ambience/wmoambience/ahnqirajinterirorfireflyroom.ogg.
  if (StartsWith(data, {'O', 'g', 'g', 'S'})) return "ogg";

  // .avi (cinematics): RIFF container with an "AVI " form type at
  // offset 8 -- standard RIFF/AVI structure, not WoW-specific.
  if (data.size() >= 12 && StartsWith(data, {'R', 'I', 'F', 'F'}) && data[8] == 'A' && data[9] == 'V' &&
      data[10] == 'I' && data[11] == ' ') {
    return "avi";
  }

  // Deliberately not attempting .wmo/.adt/.wdt/.wdl detection: all of
  // these share the same leading "MVER" chunk (stored reversed as
  // "REVM" in the file), so a magic-only check can't tell them apart --
  // staying generic is better than confidently guessing wrong.
  return "";
}

}  // namespace tactfetch
