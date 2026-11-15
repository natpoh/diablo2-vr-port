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
//
// The running game is the other source, and a player's only one (the cache is a developer's extra; players have no tools folder):
// SetGameTables takes what the game's own compiled tables say (the loader's data tables, read on the UI thread by vr/skill_tables_game.cpp) -
// each skill's class, tree page, icon frame (skilldesc IconCel) and the string id of its name, the trees' name ids - and the icons are then cut
// out of the game's own skill-icon sheets that D2R VR Settings copies beside the plugin (shared/skill_icon_files.h). A skill the cache has is
// taken from the cache as before; everything else from the game.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gameui { struct Sheet; }

namespace wheelicons {

struct Image {
    unsigned width = 0, height = 0;
    std::vector<uint8_t> rgba;   // straight (not premultiplied) R G B A
};

// Where the cache is (a folder holding manifest.tsv). Before the first Get / NameKey; the default is the folder of the
// module that holds this code + "\d2r_vr_skill_icons".
void SetDirectory(const std::wstring& dir);

// Where the game's skill-icon sheets are (Settings' copies). The default: the folder of the module + "\d2r_vr_game_ui".
void SetSheetDirectory(const std::wstring& dir);

// Icons can come: the manifest has icons in it, or the game's tables are in and at least one of its icon sheets is beside the plugin.
bool Available();

// The game's string key for a skill's name (from the manifest), or "" when it has none.
std::string NameKey(int32_t skillId);
// The string id of a skill's name in the running game's tables (skilldesc "str name"), for the localization service's getStringById; 0 none.
uint32_t NameId(int32_t skillId);

// The skill trees: the running game's tables, else the manifest's class and tree page of each skill (and trees.tsv the trees' names).
bool HasTrees();
// A skill's class (the game's own index: the order of playerclass.txt) and its page on that class's tree (1..3; 0 = on none). False: the
// manifest has no row for the skill (the game's own commands and the like: they are on no tree).
bool SkillTree(int32_t skillId, int* cls, int* page);
// Whether a skill flies to where it is aimed (the manifest's aims, from the game's skills.txt): 1 yes, 0 no, -1 not known (no cache, an older one).
int Aims(int32_t skillId);
// The game's string key for the name of a class's tree 1..3 ("" when the cache has none).
std::string TreeKey(int cls, int tree);
// The string id of that name in the running game's tables (charstats StrSkillTab1..3); 0 none.
uint32_t TreeNameId(int cls, int tree);

// The skill's icon: null at first (a worker decodes it) and for a skill with none; the same pointer afterwards.
std::shared_ptr<const Image> Get(int32_t skillId);

// + 1 whenever an icon was decoded: the picture is painted again.
uint32_t Revision();

// The worker ends, the images and the manifest are dropped (read again at the next use).
void Shutdown();

// -- pure, for the tests ----------------------------------------------------------------------------------------------------

// One manifest line: "s<id>\t<file>\t<key>[\t<class>\t<page>[\t<aims>]]" (the key, the class, the page and aims may be missing). False for a line that is not one.
struct Entry {
    int32_t id = -1;
    std::string file, key;
    int cls = -1;    // the class (-1: not told), 0..15
    int page = 0;    // the tree page 1..3, 0 none
    int aims = -1;   // 1 the skill flies to where it is aimed (a server missile, not passive, not an aura), 0 not, -1 not told (an older cache)
};
bool ParseLine(const std::string& line, Entry* out);

// One trees.tsv line: "<class>\t<key1>\t<key2>\t<key3>". False for a line that is not one (a key that is not an identifier is dropped to "").
struct TreeLine {
    int cls = -1;
    std::string key[3];
};
bool ParseTreeLine(const std::string& line, TreeLine* out);

// -- the running game's tables (pure: rows in, a summary out) --------------------------------------------------------------------
// The compiled rows as the loader's data tables hand them (the same records as the game's .bin files; the offsets checked against all rows of
// skills / skilldesc / charstats / playerclass .bin of 3.3.93787 by matching their .txt columns):
//   Skills    (0x2EC): +0x00 u16 the id (= the row), +0x2C s8 charclass (the class's index, -1 none), +0x23C s16 skilldesc's row (-1 none)
//   SkillDesc (0x12C): +0x02 u8 SkillPage (0..3), +0x06 u8 IconCel, +0x08 u16 "str name"'s string id
//   CharStats (0xD4):  +0x20 the class's name (16 chars), +0x56 / +0x58 / +0x5A u16 StrSkillTab1..3's string ids
//   PlayerClass (0x34): +0x00 the class's name (48 chars), +0x30 Code (4 chars, "ama ")
// The class index is the row of CharStats / PlayerClass (the game keeps the coded rows only - the same index a player unit carries).
struct GameSkill {
    int8_t cls = -1;        // the class whose tree the skill is on (or that shares its description, for the icon's sheet), -1 none
    uint8_t page = 0;       // its page on that class's tree 1..3, 0 none
    int16_t iconCel = -1;   // its frame on the sheet, -1 none (no description)
    uint16_t nameId = 0;    // the string id of its name, 0 none
};
struct GameTables {
    std::vector<GameSkill> skill;   // by id
    int classes = 0;
    char code[16][4] = {};          // each class's code (3 letters)
    uint16_t treeName[16][3] = {};  // each class's trees' name ids
};
struct RawTable {
    const uint8_t* rows = nullptr;
    uint32_t count = 0, size = 0;
};
// False (and why) when the rows are not what this reading knows - another build's layout: the caller keeps the cache (or numbers).
bool ParseGameTables(const RawTable& skills, const RawTable& desc, const RawTable& charstats, const RawTable& playerclass, GameTables* out,
                     const char** why);
// Handed in by the UI thread (null: none); any thread reads it after. Icons decoded from the sheets are dropped when it changes.
void SetGameTables(std::shared_ptr<const GameTables> tables);
bool HasGameTables();

// One frame of a skill-icon sheet (vr/game_ui_sprite.h Decode) as an icon (pure). False for a frame the sheet does not have.
bool CutFrame(const gameui::Sheet& sheet, int frame, Image* out);

}  // namespace wheelicons
