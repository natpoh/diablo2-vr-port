#pragma once
// The skill wheel's icons and the name keys of the skills, from a cache made on THIS machine out of the player's own
// installed game (tools/skill_icons/prepare_skill_icons.py reads the game's tables and its skill-icon sprites through
// tools/casc_extract). The cache is never in the repository and never in an installer; without it the wheel shows names.
//
//   <the plugin's folder>\d2r_vr_skill_icons\manifest.tsv      s<skill id> TAB <png file> TAB <the game's string key for its name>
//   <the plugin's folder>\d2r_vr_skill_icons\<sha-256>.png     one skill icon
//
// The names are not in the cache: the key is (an identifier, the game's own: "skillname36"), and the text is asked from the
// loader's localization service at run time, in the player's language.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace wheelicons {

struct Image {
    unsigned width = 0, height = 0;
    std::vector<uint8_t> rgba;   // straight (not premultiplied) R G B A
};

// Where the cache is (a folder holding manifest.tsv). Before the first Get / NameKey; the default is the folder of the
// module that holds this code + "\d2r_vr_skill_icons".
void SetDirectory(const std::wstring& dir);

// The manifest has icons in it.
bool Available();

// The game's string key for a skill's name (from the manifest), or "" when it has none.
std::string NameKey(int32_t skillId);

// The skill's icon: null at first (a worker decodes it) and for a skill with none; the same pointer afterwards.
std::shared_ptr<const Image> Get(int32_t skillId);

// + 1 whenever an icon was decoded: the picture is painted again.
uint32_t Revision();

// The worker ends, the images and the manifest are dropped (read again at the next use).
void Shutdown();

// -- pure, for the tests ----------------------------------------------------------------------------------------------------

// One manifest line: "s<id>\t<file>\t<key>" (the key may be missing). False for a line that is not one.
struct Entry {
    int32_t id = -1;
    std::string file, key;
};
bool ParseLine(const std::string& line, Entry* out);

}  // namespace wheelicons
