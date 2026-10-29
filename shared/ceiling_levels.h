#pragma once
// ceiling_levels.h - which of the game's levels each ceiling room is (Serg 2026-10-29, the Ceiling tab: "эти блоки расположи не хаотично а
// иерархично и какие они охватывают уровни 10-20 например"): the rooms in the order the game comes to them, each with its levels by the
// game's own numbers (levels.txt's Id - "level 33", "47", "52", as Serg names them) and names (its LevelName). Settings makes its groups'
// titles and tips from it. The table is checked against the player's game by dome_assets_test (domeassets::LevelsByBiome: each level's
// LevelType, its tiles' folder and the biome of the presets there - levels.txt, lvltypes.txt, lvlprest.txt, data/hd/env/preset/).
#include <string>

namespace ceillevels {

struct Level { int id; const char* name; };
struct Area {
    const char* biome;       // the game's biome name
    const wchar_t* room;     // the group's name for it ("act 2 sewers")
    Level levels[16];        // (a 0 id ends them)
};

constexpr Area kAreas[] = {
    {"act1_caves", L"act 1 caves", {{8, "Den of Evil"}, {9, "Cave Level 1"}, {10, "Underground Passage Level 1"}, {11, "Hole Level 1"},
                                    {12, "Pit Level 1"}, {13, "Cave Level 2"}, {14, "Underground Passage Level 2"}, {15, "Hole Level 2"},
                                    {16, "Pit Level 2"}}},
    {"act1_crypt", L"act 1 crypts and tower", {{18, "Crypt"}, {19, "Mausoleum"}, {20, "Forgotten Tower"}, {21, "Tower Cellar Level 1"},
                                                {22, "Tower Cellar Level 2"}, {23, "Tower Cellar Level 3"}, {24, "Tower Cellar Level 4"},
                                                {25, "Tower Cellar Level 5"}, {133, "Pandemonium Run 1"}}},
    {"act1_barracks", L"act 1 barracks and jail", {{28, "Barracks"}, {29, "Jail Level 1"}, {30, "Jail Level 2"}, {31, "Jail Level 3"}}},
    {"act1_court_to_barracks", L"act 1 barracks' courtyard", {{28, "Barracks"}}},
    {"act1_cathedral", L"act 1 cathedral", {{33, "Cathedral"}}},
    {"act1_catacombs", L"act 1 catacombs", {{34, "Catacombs Level 1"}, {35, "Catacombs Level 2"}, {36, "Catacombs Level 3"},
                                            {37, "Catacombs Level 4"}}},
    {"act2_sewer", L"act 2 sewers", {{47, "Sewers Level 1"}, {48, "Sewers Level 2"}, {49, "Sewers Level 3"}, {65, "Ancient Tunnels"}}},
    {"act2_palace_clean", L"act 2 harem", {{50, "Harem Level 1"}}},
    {"act2_palace", L"act 2 corrupt harem", {{51, "Harem Level 2"}}},
    {"act2_palace_cells", L"act 2 palace cellar", {{52, "Palace Cellar Level 1"}, {53, "Palace Cellar Level 2"}, {54, "Palace Cellar Level 3"}}},
    {"act2_tomb", L"act 2 tombs", {{55, "Stony Tomb Level 1"}, {56, "Halls of the Dead Level 1"}, {57, "Halls of the Dead Level 2"},
                                   {58, "Claw Viper Temple Level 1"}, {59, "Stony Tomb Level 2"}, {60, "Halls of the Dead Level 3"},
                                   {61, "Claw Viper Temple Level 2"}, {66, "Tal Rasha's Tomb"}, {67, "Tal Rasha's Tomb"}, {68, "Tal Rasha's Tomb"},
                                   {69, "Tal Rasha's Tomb"}, {70, "Tal Rasha's Tomb"}, {71, "Tal Rasha's Tomb"}, {72, "Tal Rasha's Tomb"},
                                   {73, "Duriel's Lair"}}},
    {"act2_maggot", L"act 2 maggot lair", {{62, "Maggot Lair Level 1"}, {63, "Maggot Lair Level 2"}, {64, "Maggot Lair Level 3"}}},
    {"act3_spider", L"act 3 spider caves", {{84, "Spider Cave"}, {85, "Spider Cavern"}}},
    {"act3_jungle_dungeon", L"act 3 jungle dungeons", {{86, "Swampy Pit Level 1"}, {87, "Swampy Pit Level 2"}, {88, "Flayer Dungeon Level 1"},
                                                       {89, "Flayer Dungeon Level 2"}, {90, "Swampy Pit Level 3"}, {91, "Flayer Dungeon Level 3"}}},
    {"act3_sewer", L"act 3 sewers", {{93, "Sewers Level 2"}}},
    {"act3_temple", L"act 3 temples", {{94, "Ruined Temple"}, {95, "Disused Fane"}, {96, "Forgotten Reliquary"}, {97, "Forgotten Temple"},
                                       {98, "Ruined Fane"}, {99, "Disused Reliquary"}}},
    {"act3_travincal", L"act 3 Durance of Hate", {{102, "Durance of Hate Level 3"}}},
    {"expansion_icecave", L"act 5 ice caves", {{113, "Crystalized Cavern Level 1"}, {114, "Cellar of Pity"}, {115, "Crystalized Cavern Level 2"},
                                                {116, "Echo Chamber"}, {118, "Glacial Caves Level 1"}, {119, "Glacial Caves Level 2"}}},
    {"expansion_wildtemple", L"act 5 Nihlathak's halls", {{122, "Halls of Anguish"}, {123, "Halls of Death's Calling"}, {124, "Halls of Vaught"}}},
    {"expansion_baallair", L"act 5 Worldstone Keep", {{128, "The Worldstone Keep Level 1"}, {129, "The Worldstone Keep Level 2"},
                                                      {130, "The Worldstone Keep Level 3"}}},
    {"expansion_baallair_throneroom", L"act 5 throne of destruction", {{128, "The Worldstone Keep Level 1"}, {129, "The Worldstone Keep Level 2"},
                                                                       {130, "The Worldstone Keep Level 3"}, {131, "Throne of Destruction"},
                                                                       {132, "The Worldstone Chamber"}}},
};

inline const Area* Find(const std::string& biome) {
    for (const Area& a : kAreas) if (biome == a.biome) return &a;
    return nullptr;
}
inline int Count(const Area& a) {
    int n = 0;
    while (n < 16 && a.levels[n].id) ++n;
    return n;
}

// Its levels' numbers, runs joined: "8-16", "18-25, 133", "33"
inline std::wstring Ranges(const Area& a) {
    std::wstring s;
    const int n = Count(a);
    for (int i = 0; i < n;) {
        int j = i;
        while (j + 1 < n && a.levels[j + 1].id == a.levels[j].id + 1) ++j;
        s += (s.empty() ? L"" : L", ") + std::to_wstring(a.levels[i].id) + (j > i ? L"-" + std::to_wstring(a.levels[j].id) : L"");
        i = j + 1;
    }
    return s;
}
// The group's title: "Ceiling: act 2 sewers (levels 47-49, 65)"
inline std::wstring Title(const Area& a) {
    return L"Ceiling: " + std::wstring(a.room) + (Count(a) == 1 ? L" (level " : L" (levels ") + Ranges(a) + L")";
}
// Its levels by name for the group's tip: "Sewers Level 1 (47), Sewers Level 2 (48), ..."
inline std::wstring Names(const Area& a) {
    std::wstring s;
    for (int i = 0; i < Count(a); ++i) {
        const std::string n = a.levels[i].name;
        s += (i ? L", " : L"") + std::wstring(n.begin(), n.end()) + L" (" + std::to_wstring(a.levels[i].id) + L")";
    }
    return s;
}

}  // namespace ceillevels
