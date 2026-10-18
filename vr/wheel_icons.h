#pragma once
// The skill wheel's icons and the name keys of the skills, from a cache made on THIS machine out of the player's own
// installed game (tools/skill_icons/prepare_skill_icons.py reads the game's tables and its skill-icon sprites through
// tools/casc_extract). The cache is never in the repository and never in an installer; without it the wheel shows names.
//
//   <the plugin's folder>\d2r_vr_skill_icons\manifest.tsv      s<skill id> TAB <png file> TAB <the game's string key for its name>
//                                                              [TAB <class> TAB <skill tree page>]
//   <the plugin's folder>\d2r_vr_skill_icons\trees.tsv         <class> TAB <key of tree 1's name> TAB <key of tree 2's> TAB <key of tree 3's>
//   <the plugin's folder>\d2r_vr_skill_icons\<sha-256>.png     one skill icon
// The class and the skill tree page (skills.txt charclass, skilldesc.txt SkillPage) and the trees' name keys (charstats.txt StrSkillTab1..3) are
// what the editor's category strip is made from; a cache made before them has neither (the editor then has the one list, All).
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

// The skill trees: the manifest carries each skill's class and tree page (and trees.tsv the trees' names).
bool HasTrees();
// A skill's class (the game's own index: the order of playerclass.txt) and its page on that class's tree (1..3; 0 = on none). False: the
// manifest has no row for the skill (the game's own commands and the like: they are on no tree).
bool SkillTree(int32_t skillId, int* cls, int* page);
// The game's string key for the name of a class's tree 1..3 ("" when the cache has none).
std::string TreeKey(int cls, int tree);

// The skill's icon: null at first (a worker decodes it) and for a skill with none; the same pointer afterwards.
std::shared_ptr<const Image> Get(int32_t skillId);

// + 1 whenever an icon was decoded: the picture is painted again.
uint32_t Revision();

// The worker ends, the images and the manifest are dropped (read again at the next use).
void Shutdown();

// -- pure, for the tests ----------------------------------------------------------------------------------------------------

// One manifest line: "s<id>\t<file>\t<key>[\t<class>\t<page>]" (the key, the class and the page may be missing). False for a line that is not one.
struct Entry {
    int32_t id = -1;
    std::string file, key;
    int cls = -1;    // the class (-1: not told), 0..15
    int page = 0;    // the tree page 1..3, 0 none
};
bool ParseLine(const std::string& line, Entry* out);

// One trees.tsv line: "<class>\t<key1>\t<key2>\t<key3>". False for a line that is not one (a key that is not an identifier is dropped to "").
struct TreeLine {
    int cls = -1;
    std::string key[3];
};
bool ParseTreeLine(const std::string& line, TreeLine* out);

}  // namespace wheelicons
