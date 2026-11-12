#pragma once
// mod_files.h - D2R VR Headset Report's look at the mod's own files (tools/headset_report/headset_report.cpp "D2R VR's files"): which
// D2R_DepthFog.fx ReShade can pick up and whether it is the current one, ReShade.ini's search paths, the key ini values of the path in use, and
// another VR mod in the same folders. Pure: the parsing and the hints, no file system (tools/headset_report_test.cpp).
//
// Why (0.189, .zzangu, Quest 3 over Air Link, the ReShade path): vrcam said "D2R_DepthFog.fx has no QvN: it is an older effect" and
// "toolbar / labels moved in the picture: NOT possible - ... (no GameBarOn)" after the 0.189.0 installer had put the current effect in, so the
// toolbar stayed mid-picture. vrcam asks ReShade for the uniforms GameBarOn and QvN (vr/render/effects/effects.cpp); an effect without them
// is an older one - the report names every copy ReShade can see and what each holds.

#include <cctype>
#include <string>
#include <vector>

#include "report_stats.h"   // Fmt

namespace modfiles {

// One entry of ReShade.ini's EffectSearchPaths / TextureSearchPaths: a folder, "\**" at its end = its subfolders too.
struct SearchPath {
    std::string path;
    bool recursive = false;
};
inline std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (std::isspace((unsigned char)s[a]) || s[a] == '"')) ++a;
    while (b > a && (std::isspace((unsigned char)s[b - 1]) || s[b - 1] == '"')) --b;
    return s.substr(a, b - a);
}
inline std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
inline std::vector<SearchPath> SplitSearchPaths(const std::string& value) {
    std::vector<SearchPath> out;
    size_t from = 0;
    while (from <= value.size()) {
        size_t at = value.find(',', from);
        if (at == std::string::npos) at = value.size();
        std::string p = Trim(value.substr(from, at - from));
        from = at + 1;
        if (p.empty()) continue;
        SearchPath sp;
        auto cut = [&](const char* tail) {
            const size_t n = std::char_traits<char>::length(tail);
            if (p.size() >= n && p.compare(p.size() - n, n, tail) == 0) { p.resize(p.size() - n); return true; }
            return false;
        };
        if (cut("\\**") || cut("/**")) sp.recursive = true;
        else cut("\\*") || cut("/*");
        while (p.size() > 1 && (p.back() == '\\' || p.back() == '/') && !(p.size() == 3 && p[1] == ':')) p.pop_back();
        sp.path = p;
        out.push_back(sp);
    }
    return out;
}
inline bool IsAbsolute(const std::string& p) {
    return (p.size() >= 2 && std::isalpha((unsigned char)p[0]) && p[1] == ':') || (p.size() >= 2 && (p[0] == '\\' || p[0] == '/') && (p[1] == '\\' || p[1] == '/'));
}
// A search path as ReShade takes it: relative to its base folder (the folder of ReShade.ini and its DLL).
inline std::string Resolve(const std::string& base, std::string p) {
    if (IsAbsolute(p)) return p;
    while (p.size() >= 2 && p[0] == '.' && (p[1] == '\\' || p[1] == '/')) p.erase(0, 2);
    if (p == ".") p.clear();
    std::string b = base;
    while (!b.empty() && (b.back() == '\\' || b.back() == '/')) b.pop_back();
    return p.empty() ? b : b + "\\" + p;
}

// A key of an ini's top, before its first [section] - where ReShadePreset.ini keeps Techniques= (the Windows ini calls cannot read it). "" not there.
inline std::string TopValue(const std::string& text, const char* key) {
    const std::string k = Lower(key);
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = Trim(text.substr(at, end - at));
        at = end + 1;
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) line.erase(0, 3);
        if (!line.empty() && line[0] == '[') break;
        const size_t eq = line.find('=');
        if (eq != std::string::npos && Lower(Trim(line.substr(0, eq))) == k) return Trim(line.substr(eq + 1));
    }
    return {};
}

// What an effect file holds of what vrcam looks for: the uniforms of the toolbar and map taken out of the picture, and the quiver's orbs.
struct Markers {
    bool bar = false, map = false, qvn = false;
    bool Current() const { return bar && map && qvn; }
};
inline bool HasWord(const std::string& text, const char* word) {
    const size_t n = std::char_traits<char>::length(word);
    auto ident = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };
    for (size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1))
        if ((at == 0 || !ident(text[at - 1])) && (at + n >= text.size() || !ident(text[at + n]))) return true;
    return false;
}
inline Markers FindMarkers(const std::string& text) { return {HasWord(text, "GameBarOn"), HasWord(text, "GameMapOn"), HasWord(text, "QvN")}; }
inline std::string MarkersText(const Markers& m) {
    return headsetreport::Fmt("GameBarOn %s, GameMapOn %s, QvN %s", m.bar ? "yes" : "NO", m.map ? "yes" : "NO", m.qvn ? "yes" : "NO");
}

// A file of D2R VR Unleashed / d2r-pcvr (bearstonem's; it ships an effect of the same name, D2R_DepthFog.fx) by its name: its plugin, ini and
// install record, its frame ini, a name with "pcvr" - or "unleashed", but not the one file D2R VR ships under that word (D2R_FogEase_Unleashed.fxh,
// MPL 2.0, beside our effect). `inGameRoot`: openxr_loader.dll beside the game is d2r-pcvr's too (ours is linked into the plugin).
inline bool UnleashedFile(const std::string& name, bool inGameRoot) {
    const std::string n = Lower(name);
    if (n == "d2r_fogease_unleashed.fxh" || n == "d2r_campfire_unleashed.fxh") return false;   // (ours: D2R_DepthFog.fx includes them, MPL 2.0)
    return n.find("pcvr") != std::string::npos || n.find("unleashed") != std::string::npos || n == "d2r_frame_reshade.ini" ||
           (inGameRoot && n == "openxr_loader.dll");
}

// One D2R_DepthFog.fx a ReShade.ini's search paths reach, in the order ReShade meets them.
struct FxSeen {
    std::string path;
    Markers m;
};
// The compile a file of ReShade's effect cache belongs to: "reshade-D2R_DepthFog-49152-2334929096412360845-0-F__PS_StampR.cso" -> "49152-2334929096412360845"
// (the shader model and the hash of what was compiled; every entry point of one compile shares it). "" when it is not one of D2R_DepthFog's.
inline std::string CacheCompile(const std::string& name) {
    static const std::string pre = "reshade-d2r_depthfog-";
    if (Lower(name).compare(0, pre.size(), pre) != 0) return {};
    const size_t a = pre.size(), b = name.find('-', a);
    if (b == std::string::npos) return {};
    const size_t c = name.find('-', b + 1);
    return c == std::string::npos ? std::string() : name.substr(a, c - a);
}

// One ReShade.ini found and what its search paths reach.
struct IniSeen {
    std::string path;
    bool dllBeside = false;            // a ReShade DLL (ReShade64.dll / dxgi.dll ...) is in its folder: this ini is one in use
    bool ours = true;                  // D2R VR's own (in mods\D2R-VR, or beside the game): another mod's ini is listed, not judged
    std::vector<FxSeen> depthFog;      // D2R_DepthFog.fx in search order
    std::string cache;                 // its effect cache folder, as ReShade takes it
    int cacheEntries = -1;             // files named D2R_DepthFog* in its effect cache (-1: the cache folder not there)
};
struct ModFacts {
    bool modFound = false;             // mods\D2R-VR (or the global plugin) beside this program
    std::vector<IniSeen> inis;
    int noReshade = -1, noReshadeDefault = -1;   // [render] no_reshade in d2r_vr.ini and in d2r_vr.default.ini (-1 not set)
    std::vector<std::string> otherVrMods;        // another mod folder with a d2rloader plugin, or a VR mod's file
    std::vector<std::string> unleashedSigns;     // files of D2R VR Unleashed in D2R VR's folders
};

inline std::vector<std::string> Hints(const ModFacts& f) {
    using headsetreport::Fmt;
    std::vector<std::string> h;
    if (!f.modFound) return h;
    if (f.noReshade == 0 && f.noReshadeDefault != 0)
        h.push_back("[render] no_reshade=0 in d2r_vr.ini (the shipped default is 1): the picture goes through ReShade and its D2R_DepthFog.fx.");
    bool anyInUse = false;
    for (const IniSeen& i : f.inis) anyInUse = anyInUse || i.dllBeside;
    for (const IniSeen& i : f.inis) {
        if ((anyInUse && !i.dllBeside) || !i.ours) continue;   // an ini with no ReShade beside it is not read; another mod's is its own
        if (i.depthFog.empty()) {
            h.push_back(Fmt("%s: its EffectSearchPaths reach no D2R_DepthFog.fx.", i.path.c_str()));
            continue;
        }
        if (i.depthFog.size() > 1)
            h.push_back(Fmt("%s: its EffectSearchPaths reach %d copies of D2R_DepthFog.fx - ReShade loads only one of them (the first: %s).", i.path.c_str(),
                            (int)i.depthFog.size(), i.depthFog[0].path.c_str()));
        for (const FxSeen& fx : i.depthFog)
            if (!fx.m.Current())
                h.push_back(Fmt("%s is an older effect (%s): vrcam cannot move the toolbar and the map out of the picture with it.", fx.path.c_str(),
                                MarkersText(fx.m).c_str()));
    }
    // one effect cache folder for D2R VR's ReShade and another mod's, each with an effect named D2R_DepthFog.fx
    bool shared = false;
    for (const IniSeen& a : f.inis)
        for (const IniSeen& o : f.inis)
            if (!shared && a.ours && !o.ours && a.dllBeside && o.dllBeside && !a.cache.empty() && Lower(a.cache) == Lower(o.cache) &&
                !a.depthFog.empty() && !o.depthFog.empty()) {
                shared = true;
                h.push_back(Fmt("D2R VR's ReShade and another mod's (%s) share one effect cache, %s, and both have an effect named D2R_DepthFog.fx.",
                                o.path.c_str(), a.cache.c_str()));
            }
    for (const std::string& m : f.otherVrMods) h.push_back("Another VR mod's files are here: " + m + ".");
    if (!f.unleashedSigns.empty())
        h.push_back(Fmt("Files of D2R VR Unleashed / d2r-pcvr are beside the game or in D2R VR's folders (%d, the first: %s) - two VR mods in one game "
                        "folder can mix their files.",
                        (int)f.unleashedSigns.size(), f.unleashedSigns[0].c_str()));
    return h;
}

}  // namespace modfiles
