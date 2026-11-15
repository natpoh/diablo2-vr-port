#pragma once
// skill_icon_files.h - the game's own skill-icon sheets ('SpA1' sprites, one frame per icon): which file of the player's game holds which class's
// icons, and under which name the copy waits beside the plugin. D2R VR Settings copies them out of the game's archives (CascLib, read only;
// tools/settings/menu_assets.cpp PrepareSkillIcons) into the game-art folder (shared/game_ui_files.h kFolder) while the skill panel is on; the
// plugin cuts a skill's frame out of its class's sheet (vr/wheel_icons.cpp) - which frame (skilldesc IconCel) and which class the running game's
// own tables say. Blizzard's files on the player's own machine: never in the repository, an installer or a release.
//
// The class codes are the game's (playerclass.txt Code, as its compiled rows hold them: three letters and a space); a skill on no class - the
// town portal, the basic attack, the game's commands - is on the shared sheet. Which sheet holds which class: tools/skill_icons/prepare_skill_icons.py
// SHEETS (the developer's PNG cache, the same files).

#include <cstring>

namespace skilliconfiles {

struct Sheet {
    const char* code;      // playerclass Code, 3 letters ("" = the shared sheet)
    const char* archive;   // in the game's archives (CascLib, after "data:")
    const wchar_t* leaf;   // the copy's name in the game-art folder
};

constexpr Sheet kSheets[] = {
    {"ama", "data\\hd\\global\\ui\\Spells\\amazon\\AmSkillIcon.sprite", L"skill_icons_ama.sprite"},
    {"sor", "data\\hd\\global\\ui\\Spells\\Sorceress\\SoSkillIcon.sprite", L"skill_icons_sor.sprite"},
    {"nec", "data\\hd\\global\\ui\\Spells\\Necromancer\\NeSkillIcon.sprite", L"skill_icons_nec.sprite"},
    {"pal", "data\\hd\\global\\ui\\Spells\\paladin\\PaSkillIcon.sprite", L"skill_icons_pal.sprite"},
    {"bar", "data\\hd\\global\\ui\\Spells\\Barbarian\\BaSkillIcon.sprite", L"skill_icons_bar.sprite"},
    {"dru", "data\\hd\\global\\ui\\Spells\\Druid\\DrSkillIcon.sprite", L"skill_icons_dru.sprite"},
    {"ass", "data\\hd\\global\\ui\\Spells\\Assassin\\AsSkillIcon.sprite", L"skill_icons_ass.sprite"},
    {"war", "data\\hd\\global\\ui\\Spells\\Warlock\\WaSkillIcon.sprite", L"skill_icons_war.sprite"},
    {"", "data\\hd\\global\\ui\\Spells\\SubMenu\\SkillIcon.sprite", L"skill_icons_shared.sprite"},
};
constexpr int kSheetCount = (int)(sizeof kSheets / sizeof kSheets[0]);
constexpr int kShared = kSheetCount - 1;

// The sheet of a class code (its first three letters; the compiled row pads it with a space): kShared for none, -1 for a class this list does
// not know (a later class: a plain plate rather than another sheet's picture).
inline int SheetOf(const char* code) {
    if (!code || !code[0]) return kShared;
    for (int i = 0; i < kShared; ++i)
        if (std::strncmp(code, kSheets[i].code, 3) == 0) return i;
    return -1;
}

}  // namespace skilliconfiles
