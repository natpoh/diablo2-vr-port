// vr/sky/sky_config.cpp - the sky's and the ceiling's configuration: the ceiling biomes and their looks, the acts' sky pictures, the cave fog, the table's background, the sky palettes, the mod's and ReShade's folders (cut from vr/vrcam.cpp, docs/plan_modules.md - the sky group).
// It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// Cave ceiling (docs/plan_cave_ceiling.md): the biomes that get a vault over the
// void, [ceiling] biomes: act 1's caves, then its crypts (2026-10-07); the other kinds
// of dungeon one by one.
std::vector<std::string> g_ceilBiomes;   // under g_biomeLock
// Each kind of dungeon its own: [ceiling] height_<biome>, brightness_<biome>,
// light_radius_<biome>; a key not there takes the common one (height, brightness,
// light_radius). The user sets them per dungeon in the settings window.
// And relief_<biome>; its picture texture_<biome> (LoadCeilingTextures, below), one of
// the shader's 4 slots: a crypt's brick vault is not a cave's rock.
// And wet_, detail_, contrast_<biome>: the barracks' smooth slabs shone and stood
// grainy with the cave rock's (the user, 2026-10-07).
// And a cathedral's groin vault instead of rock: vault_<biome> 1, bay_ (its bays' width),
// rib_width_, rib_depth_, bay_x_ / bay_z_ (the bays moved onto the game's columns);
// relief_ is then how far the vault rises from its springing to the crown.
// And dome_<biome> 1: over what the game draws higher than the ceiling (an altar's canopy)
// it rises as a sphere dome_radius_ round, by dome_max_ at most (the shader's height map).
// dome_<biome> 1: over what the game draws higher than dome_find_ (or the ceiling) - the cathedral's
// altar canopy - one smooth dome dome_radius_ round, by dome_max_ at most; tex_size_<biome>: world
// units one picture spans (0 = 4 tiles); relief_pic_<biome>: 0 noise, 1 light / -1 dark / 2 coloured
// parts of the picture stand out. (The groin vault, the vault on the columns or the walls and the
// walls at the floor's edge were tried on 2026-10-07 and taken out - commit 4ac321d has them.)
struct CeilBiomeCfg { std::string biome; float height, bright, light, relief, wet, detail, contrast; CeilVaultCfg vault; int slot = 0; };
std::vector<CeilBiomeCfg> g_ceilBiomeCfg;   // under g_biomeLock

// [ceiling] game_ceiling_biomes: the natural caves, where the game's own cave rock (game_ceiling) is the ceiling. Every other area on
// [ceiling] biomes keeps our effect's vault with its own settings, game_ceiling or not (Serg 2026-10-28: the barracks got the cave rock).
std::vector<std::string> g_gameCeilBiomes;   // under g_biomeLock
// Every room its own height (Serg 2026-10-29: "оставить только частные для каждого типа помещений"): height_<biome> for each room on
// [ceiling] biomes (g_ceilBiomeCfg) and on game_ceiling_biomes (here: those not on biomes - the game's ceiling only); a room with none of its
// own - one typed into a list by hand - the common [ceiling] height's last shipped value, which is no longer read.
std::vector<std::pair<std::string, float>> g_gameCeilHeights;   // under g_biomeLock

// [ceiling] height_from_walls, game_ceiling_beams: the rooms measured from the game's own models (shared/ceiling_measures.h; D2R_VR_Settings
// writes them into the mod), read at each area change (SkyTick)
std::atomic<float> g_wallsTopHere{0.0f};   // the area's walls' top, world units (0: not measured)
std::atomic<bool> g_beamsHere{false};      // the area is the barracks' (gameceiling::kBeamBiomes) and the frame was measured
ceilmeasure::Beam g_beam;                   // the frame as measured (under g_biomeLock)
// [ceiling] game_ceiling_dome: the area's dome as measured (the cathedral's), read at each area change with the rest
std::atomic<bool> g_chapelHere{false};       // the area is the dome's and it was measured
std::atomic<unsigned> g_chapelGen{0};        // a new area: the canopy is looked for again
std::vector<ceilmeasure::Dome> g_chapelDomes;   // the area's domes, the cathedral's and those over tall pieces (under g_biomeLock)

// A comma-separated list of biome names from the ini, lower case
std::vector<std::string> IniBiomeList(const wchar_t* key, const wchar_t* fallback) {
    wchar_t buf[1024];
    GetPrivateProfileStringW(L"ceiling", key, fallback, buf, (DWORD)std::size(buf), g_iniPath);
    std::vector<std::string> list;
    std::string cur;
    for (const wchar_t* p = buf;; ++p) {
        if (*p == L',' || *p == 0) {
            if (!cur.empty()) list.push_back(cur);
            cur.clear();
            if (!*p) break;
        } else if (*p != L' ' && *p != L'\t' && *p < 128) cur += (char)towlower(*p);
    }
    return list;
}

// A room's own height as the ini has it: 0 (or none, or less) not set - from its walls (gameceiling::HeightOf); else 1 .. 500
float OwnHeight(float v) { return v > 0.0f ? std::clamp(v, 1.0f, 500.0f) : 0.0f; }

void LoadCeilingBiomes() {
    // (a list from before an area batch gets the batch: the installer keeps an upgrading player's lists - gameceiling::Upgraded)
    std::string addedVault, addedGame;
    const std::vector<std::string> list = gameceiling::Upgraded(
        IniBiomeList(L"biomes", L"act1_caves,act1_crypt,act1_barracks,act1_cathedral,act1_catacombs,act2_sewer,act2_palace_clean,act2_palace,act2_palace_cells"),
        false, &addedVault);
    const std::string all = gameceiling::AllBiomes();   // (every area of the table: each its own kind)
    const std::vector<std::string> caves = gameceiling::Upgraded(IniBiomeList(L"game_ceiling_biomes", std::wstring(all.begin(), all.end()).c_str()), true,
                                                                 &addedGame);
    static std::string toldAdded;
    if (addedVault + "|" + addedGame != toldAdded) {
        toldAdded = addedVault + "|" + addedGame;
        if (!addedVault.empty() || !addedGame.empty())
            LogF("vrcam: ceiling - the ini's area lists are from before the newer areas (an upgrade keeps them): read with them added - "
                 "[ceiling] biomes + %s; game_ceiling_biomes + %s", addedVault.empty() ? "nothing" : addedVault.c_str(),
                 addedGame.empty() ? "nothing" : addedGame.c_str());
    }
    std::vector<CeilBiomeCfg> cfg;
    for (const std::string& b : list) {
        const std::wstring w(b.begin(), b.end());
        cfg.push_back({b, OwnHeight(IniF(L"ceiling", (L"height_" + w).c_str(), 0.0f)),
                       std::clamp(IniF(L"ceiling", (L"brightness_" + w).c_str(), g_set.ceilBright.load()), 0.0f, 4.0f),
                       std::clamp(IniF(L"ceiling", (L"light_radius_" + w).c_str(), g_set.ceilLight.load()), 1.0f, 1000.0f),
                       std::clamp(IniF(L"ceiling", (L"relief_" + w).c_str(), g_set.ceilRelief.load()), 0.0f, 100.0f),
                       std::clamp(IniF(L"ceiling", (L"wet_" + w).c_str(), g_set.ceilWet.load()), 0.0f, 3.0f),
                       std::clamp(IniF(L"ceiling", (L"detail_" + w).c_str(), g_set.ceilDetail.load()), 0.0f, 10.0f),
                       std::clamp(IniF(L"ceiling", (L"contrast_" + w).c_str(), g_set.ceilContrast.load()), 0.0f, 5.0f)});
        CeilVaultCfg& v = cfg.back().vault;
        v.dome = IniB(L"ceiling", (L"dome_" + w).c_str(), false);
        v.domeRadius = std::clamp(IniF(L"ceiling", (L"dome_radius_" + w).c_str(), 30.0f), 5.0f, 200.0f);
        v.domeMax = std::clamp(IniF(L"ceiling", (L"dome_max_" + w).c_str(), 30.0f), 0.0f, 300.0f);
        v.domeFind = std::clamp(IniF(L"ceiling", (L"dome_find_" + w).c_str(), 0.0f), 0.0f, 500.0f);
        v.texSize = std::clamp(IniF(L"ceiling", (L"tex_size_" + w).c_str(), 0.0f), 0.0f, 1000.0f);
        v.reliefPic = std::clamp(IniF(L"ceiling", (L"relief_pic_" + w).c_str(), 0.0f), -1.0f, 2.0f);
    }
    std::vector<std::pair<std::string, float>> gameHeights;   // the game's ceiling's rooms with no vault of ours: their own height
    for (const std::string& b : caves) {
        if (std::find(list.begin(), list.end(), b) != list.end()) continue;
        const std::wstring w(b.begin(), b.end());
        gameHeights.emplace_back(b, OwnHeight(IniF(L"ceiling", (L"height_" + w).c_str(), 0.0f)));
    }
    AcquireSRWLockExclusive(&g_biomeLock);
    const bool same = g_ceilBiomes == list && g_gameCeilBiomes == caves;
    g_ceilBiomes = list;
    g_gameCeilBiomes = caves;
    g_gameCeilHeights = gameHeights;
    for (CeilBiomeCfg& c : cfg)   // the picture's slot kept until LoadCeilingTextures looks again: no flicker on a reload
        for (const CeilBiomeCfg& old : g_ceilBiomeCfg) if (old.biome == c.biome) c.slot = old.slot;
    g_ceilBiomeCfg = cfg;
    ReleaseSRWLockExclusive(&g_biomeLock);
    if (!same) g_biomeGen.fetch_add(1);
}

// The ceiling's height, brightness, light reach, relief and picture slot (0 = none: the
// shader's own stone) in the area now - its biome's own, or the common ones. Its height: the room's own when set, else on its walls'
// top ([ceiling] height_from_walls, measured), else 18 (gameceiling::HeightOf; `own` the room's own, 0 not set).
void CeilingNow(float* height, float* bright, float* light, float* relief = nullptr, int* slot = nullptr, float* own = nullptr) {
    float r = g_set.ceilRelief.load();
    int sl = 0;
    *height = 0.0f; *bright = g_set.ceilBright.load(); *light = g_set.ceilLight.load();
    AcquireSRWLockShared(&g_biomeLock);
    for (const auto& h : g_gameCeilHeights)
        if (h.first == g_biome) { *height = h.second; break; }
    for (const CeilBiomeCfg& c : g_ceilBiomeCfg)
        if (c.biome == g_biome) { *height = c.height; *bright = c.bright; *light = c.light; r = c.relief; sl = c.slot; break; }
    ReleaseSRWLockShared(&g_biomeLock);
    if (own) *own = *height;
    *height = gameceiling::HeightOf(*height, g_set.ceilFromWalls.load(std::memory_order_relaxed), g_wallsTopHere.load(std::memory_order_relaxed), 0.0f, 1.0f);
    if (relief) *relief = r;
    if (slot) *slot = sl;
}

// The stone's look in the area now: its shine, how far the picture's light parts stand out, its fine detail.
void CeilingLookNow(float* wet, float* detail, float* contrast, CeilVaultCfg* vault) {
    *wet = g_set.ceilWet.load(); *detail = g_set.ceilDetail.load(); *contrast = g_set.ceilContrast.load();
    *vault = CeilVaultCfg{};
    AcquireSRWLockShared(&g_biomeLock);
    for (const CeilBiomeCfg& c : g_ceilBiomeCfg)
        if (c.biome == g_biome) { *wet = c.wet; *detail = c.detail; *contrast = c.contrast; *vault = c.vault; break; }
    ReleaseSRWLockShared(&g_biomeLock);
}

bool IsCeilingBiome(const std::string& biome) {
    bool on = false;
    AcquireSRWLockShared(&g_biomeLock);
    for (const std::string& b : g_ceilBiomes) if (b == biome) { on = true; break; }
    ReleaseSRWLockShared(&g_biomeLock);
    return on;
}
bool IsGameCeilingBiome(const std::string& biome) {   // [ceiling] game_ceiling_biomes: a natural cave
    bool on = false;
    AcquireSRWLockShared(&g_biomeLock);
    for (const std::string& b : g_gameCeilBiomes) if (b == biome) { on = true; break; }
    ReleaseSRWLockShared(&g_biomeLock);
    return on;
}

// Starting palettes, by eye; picked by a part of the biome's name.
// act: the act slot 1..6 (6 = act 5 snow) whose brightness and pictures the area uses.
// bright: the ini key of the area's own brightness ([sky] brightness_*, on top of the common one).
const SkyPalette kPalettes[] = {
    {"snow",      {0.45f, 0.50f, 0.58f}, {0.80f, 0.82f, 0.85f}, 0.80f, 0.35f, 12, 140, 0.0f, 6},   // expansion_ruins_snow
    {"expansion", {0.20f, 0.25f, 0.32f}, {0.55f, 0.58f, 0.62f}, 0.60f, 0.30f, 10, 140, 0.0f, 5},   // cold ruins
    {"act5",      {0.20f, 0.25f, 0.32f}, {0.55f, 0.58f, 0.62f}, 0.60f, 0.30f, 10, 140, 0.0f, 5},
    {"act1",      {0.10f, 0.13f, 0.18f}, {0.35f, 0.30f, 0.28f}, 0.70f, 0.50f,  5, 230, 0.20f, 1},  // stormy night; painted by default (D2R_Sky_act1.png)
    {"act2",      {0.25f, 0.45f, 0.75f}, {0.85f, 0.75f, 0.55f}, 0.15f, 1.00f, 45,  60, 0.0f, 2},   // desert
    {"act3",      {0.15f, 0.30f, 0.35f}, {0.55f, 0.60f, 0.45f}, 0.50f, 0.40f, 35, 100, 0.0f, 3},   // humid haze
    {"act4",      {0.12f, 0.02f, 0.02f}, {0.60f, 0.15f, 0.05f}, 0.60f, 0.00f, 20,   0, 0.0f, 4},   // hell: smoke, no sun
    {"summit",    {0.20f, 0.25f, 0.32f}, {0.55f, 0.58f, 0.62f}, 0.60f, 0.30f, 10, 140, 0.0f, 5},   // UberArreatSummit (level 137): act 5's
};
constexpr int kDefaultPalette = 3;   // act1
// Per act, from [sky]: brightness_<act>, texture_<act> (the band round the
// viewer), cap_<act> (the zenith). An empty texture = the shader draws the sky
// from the palette. File names go to ReShade as preprocessor definitions.
const char* const kActKeys[7] = {"", "act1", "act2", "act3", "act4", "act5", "snow"};
const char* const kActDefs[7] = {"", "ACT1", "ACT2", "ACT3", "ACT4", "ACT5", "SNOW"};
SRWLOCK g_skyCfgLock = SRWLOCK_INIT;
SkyAct g_skyAct[7];
// [fog] color_caves: one colour for every area off the outdoor list - caves,
// dungeons, crypts, towers. Empty = the act's own colour, as before.
CaveFog g_caveFog{};
std::atomic<uint32_t> g_skyCfgGen{0};   // bumped when a picture name changes

std::string Utf8(const wchar_t* w) {
    char b[1024];
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, b, sizeof b, nullptr, nullptr);
    return n > 0 ? std::string(b) : std::string();
}

// "#RRGGBB" (a leading # and spaces optional) to 0..1; false = empty or not a colour.
bool ParseHexColor(const wchar_t* text, float rgb[3]) {
    unsigned v = 0;
    while (*text == L' ' || *text == L'#') ++text;
    const bool ok = wcslen(text) >= 6 && swscanf_s(text, L"%6x", &v) == 1;
    for (int c = 0; c < 3; ++c) rgb[c] = ok ? ((v >> (16 - 8 * c)) & 0xFF) / 255.0f : 0.0f;
    return ok;
}

// [table] background: the void's fill in the table view; empty or unreadable = black.
SRWLOCK g_tableBgLock = SRWLOCK_INIT;
float g_tableBg[3] = {0.0f, 0.0f, 0.0f};

void LoadTableBackground() {
    wchar_t buf[64];
    GetPrivateProfileStringW(L"table", L"background", L"", buf, (DWORD)std::size(buf), g_iniPath);
    float rgb[3];
    if (!ParseHexColor(buf, rgb)) rgb[0] = rgb[1] = rgb[2] = 0.0f;
    AcquireSRWLockExclusive(&g_tableBgLock);
    for (int c = 0; c < 3; ++c) g_tableBg[c] = rgb[c];
    ReleaseSRWLockExclusive(&g_tableBgLock);
}

void LoadCaveFog() {
    wchar_t buf[64];
    GetPrivateProfileStringW(L"fog", L"color_caves", L"#100D0B", buf, (DWORD)std::size(buf), g_iniPath);
    CaveFog f{};
    f.set = ParseHexColor(buf, f.rgb);
    AcquireSRWLockExclusive(&g_skyCfgLock); g_caveFog = f; ReleaseSRWLockExclusive(&g_skyCfgLock);
}

// The folder ReShade lives in (shared/mod_layout.h): the mod's own reshade\ when this plugin runs as the D2RLoader mod D2R-VR
// (<game>\mods\D2R-VR\d2rloader\plugins), unless the player's dxgi.dll is beside the game (its base is the game's folder); else
// the game's folder, as before. Its ReShade.ini and add-ons are there. Taken once, with this plugin's own path.
const std::wstring& ReShadeDir() {
    static const std::wstring dir = [] {
        wchar_t self[MAX_PATH] = L"", game[MAX_PATH] = L"";
        HMODULE me = g_self;
        if (!me) GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&ReShadeDir, &me);
        DWORD n = GetModuleFileNameW(me, self, MAX_PATH);
        while (n && self[n - 1] != L'\\') --n;
        self[n] = 0;
        n = GetModuleFileNameW(nullptr, game, MAX_PATH);
        while (n && game[n - 1] != L'\\') --n;
        game[n] = 0;
        const bool playerDxgi = GetFileAttributesW((std::wstring(game) + L"dxgi.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
        return modlayout::ReShadeDir(game, modlayout::ModRootOfPluginDir(self), playerDxgi);
    }();
    return dir;
}

// Where D2R VR's OWN ReShade files are (shared/mod_layout.h OurFilesDir): the mod's reshade\ whenever the plugin runs as the mod D2R-VR - D2R_DepthFog.fx, the
// sky / ceiling / Arcane pictures and FlatVR's add-on ship there, and ReShade finds them there both when it is the mod's own ReShade64.dll (that folder is its
// base, ReShadeDir) and when it is a dxgi.dll the player has beside the game (two search paths in its ReShade.ini, shared/reshade_paths.h; its base is the
// game's folder). Else the game's folder, as before the mod layout.
const std::wstring& OurFilesDir() {
    static const std::wstring dir = [] {
        wchar_t self[MAX_PATH] = L"", game[MAX_PATH] = L"";
        HMODULE me = g_self;
        if (!me) GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&OurFilesDir, &me);
        DWORD n = GetModuleFileNameW(me, self, MAX_PATH);
        while (n && self[n - 1] != L'\\') --n;
        self[n] = 0;
        n = GetModuleFileNameW(nullptr, game, MAX_PATH);
        while (n && game[n - 1] != L'\\') --n;
        game[n] = 0;
        return modlayout::OurFilesDir(game, modlayout::ModRootOfPluginDir(self));
    }();
    return dir;
}

// A bare name is looked for where ReShade looks for D2R VR's pictures (reshade-shaders\Textures of D2R VR's own files, OurFilesDir).
bool SkyFileExists(const std::wstring& name) {
    if (name.find(L':') != std::wstring::npos || name.rfind(L"\\\\", 0) == 0) return GetFileAttributesW(name.c_str()) != INVALID_FILE_ATTRIBUTES;
    return GetFileAttributesW((OurFilesDir() + L"reshade-shaders\\Textures\\" + name).c_str()) != INVALID_FILE_ATTRIBUTES;
}

void LoadCrosshairFile();   // [input] crosshair (the flat crosshair's picture), with the cursor code

// The ceiling's pictures, one per kind of dungeon: [ceiling] texture_<biome>, by default
// D2R_Sky_ours/D2R_Ceiling_<biome>.png, else [ceiling] texture (the act 1 caves' stone).
// The different files go to the shader's 4 slots as D2R_CEILING_1..4 (like the skies);
// a biome whose file is missing gets slot 0: the shader draws the stone itself.
// 8: a fifth dungeon picture (the act 2 sewers' brick) found no slot and drew the shader's own stone (2026-10-07)
std::string g_ceilSlot[kCeilSlots + 1];   // [1..8] the files, under g_skyCfgLock
bool g_ceilSlotOk[kCeilSlots + 1] = {};

std::wstring IniPath(const wchar_t* key, const wchar_t* def) {
    wchar_t buf[MAX_PATH];
    GetPrivateProfileStringW(L"ceiling", key, def, buf, MAX_PATH, g_iniPath);
    std::wstring f = buf;
    while (!f.empty() && (f.back() == L' ' || f.back() == L'"')) f.pop_back();
    while (!f.empty() && (f.front() == L' ' || f.front() == L'"')) f.erase(0, 1);
    return f;
}

void LoadCeilingTextures() {
    const std::wstring common = IniPath(L"texture", L"D2R_Sky_ours/D2R_Ceiling_act1_caves_walls.png");
    AcquireSRWLockExclusive(&g_biomeLock);
    std::vector<CeilBiomeCfg> cfg = g_ceilBiomeCfg;
    ReleaseSRWLockExclusive(&g_biomeLock);
    std::string slot[kCeilSlots + 1];
    int used = 0;
    for (CeilBiomeCfg& c : cfg) {
        const std::wstring w(c.biome.begin(), c.biome.end());
        std::wstring f = IniPath((L"texture_" + w).c_str(), (L"D2R_Sky_ours/D2R_Ceiling_" + w + L".png").c_str());
        if (f.empty() || !SkyFileExists(f)) f = common;
        c.slot = 0;
        if (f.empty() || !SkyFileExists(f)) { LogF("vrcam: cave ceiling %s - no picture found, the shader draws the stone itself", c.biome.c_str()); continue; }
        for (wchar_t& ch : f) if (ch == L'\\') ch = L'/';   // an FX string takes no backslashes
        const std::string name = Utf8(f.c_str());
        for (int k = 1; k <= used && !c.slot; ++k) if (slot[k] == name) c.slot = k;
        if (!c.slot && used < kCeilSlots) { slot[++used] = name; c.slot = used; }
    }
    AcquireSRWLockExclusive(&g_biomeLock);
    for (CeilBiomeCfg& now : g_ceilBiomeCfg)
        for (const CeilBiomeCfg& c : cfg) if (c.biome == now.biome) now.slot = c.slot;
    ReleaseSRWLockExclusive(&g_biomeLock);
    bool changed = false;
    AcquireSRWLockExclusive(&g_skyCfgLock);
    for (int k = 1; k <= kCeilSlots; ++k) {
        const bool ok = k <= used;
        if (g_ceilSlot[k] != slot[k] || g_ceilSlotOk[k] != ok) changed = true;
        g_ceilSlot[k] = slot[k]; g_ceilSlotOk[k] = ok;
    }
    ReleaseSRWLockExclusive(&g_skyCfgLock);
    if (changed) g_skyCfgGen.fetch_add(1);
}

void LoadSkyActs() {
    bool changed = false;
    for (int a = 1; a <= 6; ++a) {
        wchar_t key[32], buf[MAX_PATH];
        const std::wstring act(kActKeys[a], kActKeys[a] + strlen(kActKeys[a]));
        swprintf_s(key, L"brightness_%ls", act.c_str());
        const float bright = std::clamp(IniF(L"sky", key, 1.0f), 0.0f, 4.0f);
        std::string files[2]; bool ok[2];
        for (int k = 0; k < 2; ++k) {
            swprintf_s(key, k ? L"cap_%ls" : L"texture_%ls", act.c_str());
            wchar_t def[96];   // tools/gen_sky.py's names, in their own folder (the game's skies sit beside it)
            swprintf_s(def, k ? L"D2R_Sky_ours/D2R_SkyCap_%ls.png" : L"D2R_Sky_ours/D2R_Sky_%ls.png", act.c_str());
            GetPrivateProfileStringW(L"sky", key, def, buf, MAX_PATH, g_iniPath);
            std::wstring f = buf;
            while (!f.empty() && (f.back() == L' ' || f.back() == L'"')) f.pop_back();
            while (!f.empty() && (f.front() == L' ' || f.front() == L'"')) f.erase(0, 1);
            ok[k] = !f.empty() && SkyFileExists(f);
            if (!f.empty() && !ok[k]) LogF("vrcam: sky %s %s '%s' NOT FOUND - %s", kActKeys[a], k ? "cap" : "texture", Utf8(f.c_str()).c_str(),
                                          k ? "the zenith takes the palette colour" : "the shader draws this sky itself");
            for (wchar_t& c : f) if (c == L'\\') c = L'/';   // an FX string takes no backslashes
            files[k] = Utf8(f.c_str());
        }
        // The night pictures: both must be there (the cap blends with the band).
        std::string night[2]; bool nightOk = true;
        for (int k = 0; k < 2; ++k) {
            swprintf_s(key, k ? L"night_cap_%ls" : L"night_%ls", act.c_str());
            wchar_t def[96];
            swprintf_s(def, k ? L"D2R_Sky_ours/D2R_SkyCapNight_%ls.png" : L"D2R_Sky_ours/D2R_SkyNight_%ls.png", act.c_str());
            GetPrivateProfileStringW(L"sky", key, def, buf, MAX_PATH, g_iniPath);
            std::wstring f = buf;
            while (!f.empty() && (f.back() == L' ' || f.back() == L'"')) f.pop_back();
            while (!f.empty() && (f.front() == L' ' || f.front() == L'"')) f.erase(0, 1);
            nightOk = nightOk && !f.empty() && SkyFileExists(f);
            for (wchar_t& ch : f) if (ch == L'\\') ch = L'/';
            night[k] = Utf8(f.c_str());
        }
        // [fog] color_<act>: #RRGGBB; empty = the sky's horizon (or the dark default where there is no sky)
        swprintf_s(key, L"color_%ls", act.c_str());
        // defaults: each painted sky's horizon, a little darker (key missing; an empty key = from the sky)
        static const wchar_t* const kFogDef[7] = {L"", L"#242323", L"#683114", L"#50512F", L"#6C1809", L"#322A24", L"#7E8D99"};   // (only without the key: the shipped ini has Serg's own)
        GetPrivateProfileStringW(L"fog", key, kFogDef[a], buf, MAX_PATH, g_iniPath);
        float fog[3] = {};
        const bool fogSet = ParseHexColor(buf, fog);
        AcquireSRWLockExclusive(&g_skyCfgLock);
        SkyAct& s = g_skyAct[a];
        if (s.band != files[0] || s.cap != files[1] || s.bandOk != ok[0] || s.capOk != ok[1] ||
            s.nightBand != night[0] || s.nightCap != night[1] || s.nightOk != nightOk) changed = true;
        s = {bright, files[0], files[1], ok[0], ok[1], {fog[0], fog[1], fog[2]}, fogSet, night[0], night[1], nightOk};
        ReleaseSRWLockExclusive(&g_skyCfgLock);
    }
    if (changed) g_skyCfgGen.fetch_add(1);
    LoadCaveFog();
}

// The act (1..6) a biome belongs to by its name, outdoors or not; 0 = cannot tell.
int ActOfBiome(const std::string& biome) {
    for (const SkyPalette& p : kPalettes) if (biome.find(p.part) != std::string::npos) return p.act;
    return 0;
}


// -1 = no sky here. Called on the timer thread.
int SkyPaletteFor(const std::string& biome, bool always) {
    if (!always && !IsOutdoorBiome(biome)) return -1;
    for (int i = 0; i < (int)std::size(kPalettes); ++i) if (biome.find(kPalettes[i].part) != std::string::npos) return i;
    return kDefaultPalette;
}

}  // namespace vrc
