#pragma once
// reshade_env.h - ReShade's environment variables another mod left behind, which the plugin takes out of the game's own process
// before it loads ReShade64.dll. ReShade reads RESHADE_BASE_PATH_OVERRIDE (and its other RESHADE_* switches) from the environment:
// d2r-pcvr keeps its own ReShade in mods\D2R-PCVR\reshade and points ReShade there with that variable. Left set for the user, our
// ReShade looked there too - a player's game had no ReShade.ini or ReShade.log of its own and ReShade64.dll refused to start
// (error 1114) on every launch, so no headset session came up (2026-10-18). Only this process's copy is changed; Windows' own
// settings and other programs keep theirs.
// Found / Plan are pure: they read an environment block as GetEnvironmentStringsW gives it (NAME=value\0 ... \0\0).
//
// The ORDER matters, and PrepareProcess is the one place that has it: FIRST every RESHADE_* variable the process inherited is cleared,
// THEN - in a mod-layout install (the plugin is the D2RLoader mod D2R-VR) - the plugin's own RESHADE_BASE_PATH_OVERRIDE (the mod's
// reshade\ folder) is set. The other way round would wipe the plugin's own value together with the stranger's. In the game's own
// folder (no mod) nothing is set and ReShade uses the game's folder. tools/reshade_env_test.cpp pins this on the real environment.

#include <cwctype>
#include <string>
#include <vector>

#include <windows.h>

namespace reshadeenv {

struct Var {
    std::wstring name;
    std::wstring value;
};

// The variables whose name starts with RESHADE (any case). Entries that begin with '=' (the per-drive current directories
// Windows keeps there, "=C:=C:\...") are skipped.
inline std::vector<Var> Found(const wchar_t* block) {
    std::vector<Var> out;
    if (!block) return out;
    for (const wchar_t* p = block; *p; ) {
        const std::wstring entry(p);
        p += entry.size() + 1;
        if (entry.empty() || entry[0] == L'=') continue;
        const size_t eq = entry.find(L'=');
        const std::wstring name = entry.substr(0, eq);
        static const wchar_t kPrefix[] = L"RESHADE";
        if (name.size() < 7) continue;
        bool match = true;
        for (size_t i = 0; i < 7; ++i)
            if ((wchar_t)std::towupper(name[i]) != kPrefix[i]) { match = false; break; }
        if (!match) continue;
        out.push_back({name, eq == std::wstring::npos ? std::wstring() : entry.substr(eq + 1)});
    }
    return out;
}

// One change to the process' environment: Clear (value = what the variable held, for the log) or Set.
struct Step {
    enum Kind { Clear, Set } kind;
    std::wstring name;
    std::wstring value;
};

// The value ReShade wants for RESHADE_BASE_PATH_OVERRIDE: the folder without its last backslash (a drive root keeps it).
inline std::wstring OverrideValue(std::wstring folder) {
    if (folder.size() > 3 && folder.back() == L'\\') folder.pop_back();
    return folder;
}

// What PrepareProcess does to this environment block, in order: every inherited RESHADE_* cleared, then (ownBase not empty) the
// plugin's own RESHADE_BASE_PATH_OVERRIDE set to ownBase.
inline std::vector<Step> Plan(const wchar_t* block, const std::wstring& ownBase) {
    std::vector<Step> out;
    for (const Var& v : Found(block)) out.push_back({Step::Clear, v.name, v.value});
    if (!ownBase.empty()) out.push_back({Step::Set, L"RESHADE_BASE_PATH_OVERRIDE", OverrideValue(ownBase)});
    return out;
}

// Plan on this process' own environment, carried out. ownBase: the mod's reshade\ folder in a mod-layout install, else empty.
// Returns the steps taken (for the log).
inline std::vector<Step> PrepareProcess(const std::wstring& ownBase) {
    wchar_t* env = GetEnvironmentStringsW();
    const std::vector<Step> steps = Plan(env, ownBase);
    if (env) FreeEnvironmentStringsW(env);
    for (const Step& s : steps) SetEnvironmentVariableW(s.name.c_str(), s.kind == Step::Clear ? nullptr : s.value.c_str());
    return steps;
}

}  // namespace reshadeenv
