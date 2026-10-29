#pragma once
// mod_layout.h - D2R VR as a D2RLoader mod: where its files are (the plugin, D2R VR Settings, D2R VR Setup's tests).
//
// D2RLoader loads "global" plugins from <game>\d2rloader\plugins always, and a mod's plugins from
// <game>\mods\<mod>\d2rloader\plugins only while that mod is active (D2RLoader.exe -mod <mod>, or [d2rloader] default_mod in
// d2rloader\config\d2rloader.toml). Installed as the mod D2R-VR, D2R VR sits beside another VR mod (bearstonem's d2r-pcvr as
// D2R-PCVR) without one file of either in the other's way:
//   <game>\mods\D2R-VR\d2rloader\plugins\   d2rl-vrcam.dll, d2r_vr.ini (and the other d2r_vr*.ini), d2r_vr_skill_icons\
//   <game>\mods\D2R-VR\reshade\             ReShade64.dll, ReShade.ini, ReShadePreset.ini, FlatVR's add-on, reshade-shaders\
// ReShade is one per process; the plugin loads the mod's own copy, with that folder as ReShade's base (its ini, presets,
// shaders, add-ons). The old layout - the plugin in <game>\d2rloader\plugins, ReShade beside D2R.exe - keeps working as it is.
// Pure: paths only (no file system here; the caller says what exists).
#include <cwchar>
#include <string>

namespace modlayout {

constexpr const wchar_t* kModName = L"D2R-VR";            // the folder under <game>\mods and D2RLoader's -mod name (an id: it stays)
constexpr const wchar_t* kDisplayName = L"D2R VR Authentic";   // the name players see (bearstonem's d2r-pcvr: "D2R VR Unleashed")
constexpr const wchar_t* kTagline = L"Diablo II as it is, in VR: the game's own rules, your body and hands.";
constexpr const wchar_t* kReShadeFolder = L"reshade\\";   // under the mod's folder
constexpr const wchar_t* kPluginsTail = L"d2rloader\\plugins\\";

inline bool EndsWithI(const std::wstring& s, const std::wstring& tail) {
    return s.size() >= tail.size() && _wcsicmp(s.c_str() + s.size() - tail.size(), tail.c_str()) == 0;
}
inline std::wstring WithSlash(std::wstring d) {
    if (!d.empty() && d.back() != L'\\' && d.back() != L'/') d += L'\\';
    return d;
}
// The folder above "<x>\d2rloader\plugins\" (the plugin's own folder): <x>\, or "" when the folder is not a plugins folder.
inline std::wstring ScopeRootOfPluginDir(const std::wstring& pluginDir) {
    const std::wstring d = WithSlash(pluginDir);
    const std::wstring tail = kPluginsTail;
    return EndsWithI(d, tail) ? d.substr(0, d.size() - tail.size()) : std::wstring();
}
// "<game>\mods\<name>\" when the plugin runs from a mod's plugins folder, else "".
inline std::wstring ModRootOfPluginDir(const std::wstring& pluginDir) {
    const std::wstring root = ScopeRootOfPluginDir(pluginDir);
    if (root.size() < 2) return std::wstring();
    const size_t cut = root.find_last_of(L'\\', root.size() - 2);   // the name's start
    if (cut == std::wstring::npos) return std::wstring();
    const std::wstring parent = root.substr(0, cut + 1);             // "<game>\mods\"
    return EndsWithI(parent, L"\\mods\\") ? root : std::wstring();
}
// The game's folder for a plugin folder of either layout ("" when it is neither).
inline std::wstring GameOfPluginDir(const std::wstring& pluginDir) {
    const std::wstring mod = ModRootOfPluginDir(pluginDir);
    if (!mod.empty()) {
        const size_t cut = mod.find_last_of(L'\\', mod.size() - 2);  // "<game>\mods\"
        const std::wstring mods = mod.substr(0, cut + 1);
        return mods.substr(0, mods.size() - std::wcslen(L"mods\\"));
    }
    return ScopeRootOfPluginDir(pluginDir);
}
// Where ReShade lives (its base: ReShade.ini, its log): the mod's reshade\ folder whenever the plugin runs as a mod - whether its
// ReShade64.dll is there yet or not - unless the player has their own ReShade as dxgi.dll beside the game (`playerDxgi`: the game
// loads that one, its base is the game's folder); else the game's folder (the old layout). A ReShade64.dll beside the game in the
// mod layout is NOT D2R VR's (another mod's, or one left from before 0.170 that Setup did not mark as ours): a player's logs of
// 2026-10-25 had the plugin load such a one, it refused to start (error 1114), the sky stayed black, and Settings said "installed"
// and hid Install ReShade.
inline std::wstring ReShadeDir(const std::wstring& gameDir, const std::wstring& modRoot, bool playerDxgi) {
    return !modRoot.empty() && !playerDxgi ? WithSlash(modRoot) + kReShadeFolder : WithSlash(gameDir);
}
// Where D2R VR's OWN ReShade files are - D2R_DepthFog.fx, the sky / ceiling / Arcane pictures, FlatVR's add-on: the mod's reshade\ whenever D2R VR is
// the mod (whoever loads ReShade: its own ReShade64.dll with this folder as base, or a dxgi.dll the player has beside the game, which finds them through
// two search paths in its ReShade.ini - shared/reshade_paths.h), else the game's folder.
inline std::wstring OurFilesDir(const std::wstring& gameDir, const std::wstring& modRoot) {
    return !modRoot.empty() ? WithSlash(modRoot) + kReShadeFolder : WithSlash(gameDir);
}
// The mod's folders under a game folder (for D2R VR Settings and the installer's checks).
inline std::wstring ModRoot(const std::wstring& gameDir) { return WithSlash(gameDir) + L"mods\\" + kModName + L"\\"; }
inline std::wstring ModPlugins(const std::wstring& gameDir) { return ModRoot(gameDir) + kPluginsTail; }
inline std::wstring ModReShade(const std::wstring& gameDir) { return ModRoot(gameDir) + kReShadeFolder; }
// D2RLoader's arguments for a launch of this mod.
inline std::wstring LaunchArguments() { return std::wstring(L"-mod ") + kModName; }

}  // namespace modlayout
