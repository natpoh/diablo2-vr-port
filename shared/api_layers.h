// api_layers.h - [openxr] block_listed_layers (on): the implicit OpenXR layers KNOWN to break this game's picture kept out of its process -
// only those on a list ([openxr] blocked_layers), and only while the eyes are drawn our own way (not [openxr] runtime_fov).
//
// An implicit layer is one the OpenXR loader puts into EVERY OpenXR app on the PC: a program registers its manifest under
// HKLM / HKCU SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit (a DWORD value named by the manifest's path, 0 = enabled), and the layer
// then sits between the game and the runtime. Most are wanted in the game too - overlays (XSOverlay, fpsVR, OpenKneeboard), OpenXR
// Toolkit, hand tracking, the runtime's own - and are never touched: players use them ("many use XS Overlay and other programs; blocking
// them would be bad", the author, 2026-10-25). A layer written for an app that draws exactly the runtime's view can break ours while we
// draw our own crop of each eye (vr/xr.cpp Cropped, vr/eye_fov.h); runtime_fov draws as such an app does and needs no blocking.
//
// The list starts with mbucchia's Quad-Views-Foveated (a player's 0.179 report: cross-eyed and distorted while the game had the focus,
// right with SteamVR's dashboard up; it was the one layer on his PC. Its source steps aside for a D3D12 app that does not ask for quad
// views, so it may well be innocent there: the list errs on the safe side, and runtime_fov is the real fix).
//
// A listed layer is switched off through the variable its manifest names for that ("disable_environment" - the loader refuses an
// implicit layer without one), set in the game's own process before the loader first looks for layers: nothing changes for any other
// program, nothing is written to the registry. Every implicit layer found is logged, and D2R VR Settings names a listed one, switched
// off or not - never silently.
//
// Plain data and parsing, shared by the plugin (vr/api_layers_lib.cpp), D2R VR Settings and the test (cleanroom/tests/api_layers_test.cpp).
#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace apilayers {

// What a layer manifest says that matters here (its "api_layer" object's strings). ok: it parsed and has a name.
struct Manifest {
    bool ok = false;
    std::string name, disableEnv, enableEnv;
};

namespace detail {
// A small JSON reader: enough to walk a manifest and take api_layer's string members; anything malformed fails.
struct Reader {
    const std::string& s;
    size_t i = 0;
    int depth = 0;
    void Ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i; }
    bool Str(std::string* out) {
        Ws();
        if (i >= s.size() || s[i] != '"') return false;
        ++i;
        std::string v;
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c == '\\') {
                if (i >= s.size()) return false;
                const char e = s[i++];
                switch (e) {
                    case 'n': c = '\n'; break;
                    case 't': c = '\t'; break;
                    case 'r': c = '\r'; break;
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                    case 'u':   // (names are ASCII; a \u escape is kept as '?')
                        if (i + 4 > s.size()) return false;
                        i += 4;
                        c = '?';
                        break;
                    default: c = e; break;   // \" \\ \/
                }
            }
            v += c;
        }
        if (i >= s.size()) return false;
        ++i;
        if (out) *out = v;
        return true;
    }
    // Any value; strings of the object called "api_layer" at the top go to `m`.
    bool Value(Manifest* m, bool layerObject) {
        Ws();
        if (i >= s.size() || ++depth > 64) return false;
        bool ok = true;
        const char c = s[i];
        if (c == '{') {
            ++i;
            Ws();
            if (i < s.size() && s[i] == '}') { ++i; --depth; return true; }
            for (;;) {
                std::string key;
                if (!Str(&key)) return false;
                Ws();
                if (i >= s.size() || s[i++] != ':') return false;
                Ws();
                if (layerObject && i < s.size() && s[i] == '"') {
                    std::string v;
                    if (!Str(&v)) return false;
                    if (key == "name") m->name = v;
                    else if (key == "disable_environment") m->disableEnv = v;
                    else if (key == "enable_environment") m->enableEnv = v;
                } else if (!Value(m, depth == 1 && key == "api_layer")) {
                    return false;
                }
                Ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; break; }
                return false;
            }
        } else if (c == '[') {
            ++i;
            Ws();
            if (i < s.size() && s[i] == ']') { ++i; --depth; return true; }
            for (;;) {
                if (!Value(m, false)) return false;
                Ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; break; }
                return false;
            }
        } else if (c == '"') {
            ok = Str(nullptr);
        } else {   // a number, true, false, null
            const size_t at = i;
            while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '+' || s[i] == '.')) ++i;
            ok = i > at;
        }
        --depth;
        return ok;
    }
};
inline std::string Upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace detail

// A manifest file's text (UTF-8, a BOM allowed).
inline Manifest Parse(const std::string& text) {
    Manifest m;
    const std::string body = text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? text.substr(3) : text;
    detail::Reader r{body};
    r.Ws();
    if (r.i >= body.size() || body[r.i] != '{' || !r.Value(&m, false)) return {};
    r.Ws();
    if (r.i != body.size()) return {};
    m.ok = !m.name.empty();
    return m;
}

// [openxr] blocked_layers when the key is not in the ini: the layers known to break the picture drawn our own way.
constexpr char kDefaultList[] = "XR_APILAYER_MBUCCHIA_quad_views_foveated";

// Whether a layer is on the list: names (or parts of them) comma-, semicolon- or space-separated, any case; empty parts match nothing.
inline bool Listed(const std::string& name, const std::string& list) {
    const std::string n = detail::Upper(name), k = detail::Upper(list);
    size_t at = 0;
    while (at < k.size()) {
        size_t end = k.find_first_of(", ;\t", at);
        if (end == std::string::npos) end = k.size();
        if (end > at && n.find(k.substr(at, end - at)) != std::string::npos) return true;
        at = end + 1;
    }
    return false;
}

// One implicit layer found for this process, and what is done with it.
struct Found {
    std::string path;          // the manifest's path (UTF-8)
    Manifest manifest;
    bool asleep = false;       // its enable_environment is not set: the loader leaves it out anyway
    bool listed = false;       // on [openxr] blocked_layers
    bool turnedOff = false;    // its disable_environment set by us for this process
};
// Decides each one: `block` = [openxr] block_listed_layers, `ownCrop` = the eyes drawn our own way (runtime_fov off), `list` = blocked_layers.
// A layer is turned off only when blocking, drawing our own way, listed, loadable, and it names its variable (the loader would refuse one
// that does not). Nothing else is ever turned off.
inline void Decide(std::vector<Found>& layers, bool block, bool ownCrop, const std::string& list) {
    for (Found& f : layers) {
        f.listed = f.manifest.ok && Listed(f.manifest.name, list);
        f.turnedOff = block && ownCrop && f.listed && !f.asleep && !f.manifest.disableEnv.empty();
    }
}
// The names, comma-separated, of the loadable layers of one kind - for the log and D2R VR Settings' Status ([session] in d2r_vr_headset.ini).
enum class Kind {
    Off,          // listed and turned off (layers_off)
    ListedIn,     // listed but loading with the game: blocking off, runtime_fov on, or no variable to switch it off (layers_listed_in)
    In,           // any other, loading with the game as always (layers_in)
};
inline std::string Names(const std::vector<Found>& layers, Kind kind) {
    std::string out;
    for (const Found& f : layers) {
        if (!f.manifest.ok || f.asleep) continue;
        const Kind k = f.turnedOff ? Kind::Off : f.listed ? Kind::ListedIn : Kind::In;
        if (k != kind) continue;
        if (!out.empty()) out += ", ";
        out += f.manifest.name;
    }
    return out;
}

// The words of that line, {layers} the names: English here; D2R VR Settings passes them in the player's language (its lang files).
struct Words {
    const wchar_t* off = L"OpenXR layer {layers} turned off for D2R VR: it is known to distort this game's picture (in the game's process only - other "
                         L"apps keep it). \"Each eye exactly as the headset gives it\" draws as other games do and keeps it; or untick \"Turn off OpenXR layers "
                         L"known to break the picture\".";
    const wchar_t* listedInRuntimeFov = L"OpenXR layer {layers} loads with the game, left in: the eyes are drawn exactly as the headset gives them, as other "
                                        L"games draw, so it should do no harm.";
    const wchar_t* listedIn = L"OpenXR layer {layers} loads with the game: it is known to distort its picture (cross-eyed, stretched) when the eyes "
                              L"are drawn the mod's own way - tick \"Each eye exactly as the headset gives it\".";
    const wchar_t* in = L"Other OpenXR layers in the game (left as they are): {layers}.";
};
inline std::wstring WithLayers(const wchar_t* words, const std::wstring& layers) {
    std::wstring s = words;
    for (size_t at = s.find(L"{layers}"); at != std::wstring::npos; at = s.find(L"{layers}", at + layers.size())) s.replace(at, 8, layers);
    return s;
}

// D2R VR Settings' Status line from the three lists and [openxr] runtime_fov, and how loud it is (0 nothing to say, 1 for information,
// 2 a warning).
inline std::wstring StatusLine(const std::wstring& off, const std::wstring& listedIn, const std::wstring& in, bool runtimeFov, int* level,
                               const Words& w = Words()) {
    std::wstring s;
    int lv = 0;
    if (!off.empty()) {
        s = WithLayers(w.off, off);
        lv = 2;
    }
    if (!listedIn.empty()) {
        if (!s.empty()) s += L" ";
        if (runtimeFov) {
            s += WithLayers(w.listedInRuntimeFov, listedIn);
            if (lv < 1) lv = 1;
        } else {
            s += WithLayers(w.listedIn, listedIn);
            lv = 2;
        }
    }
    if (!in.empty()) {
        if (!s.empty()) s += L" ";
        s += WithLayers(w.in, in);
        if (!lv) lv = 1;
    }
    if (level) *level = lv;
    return s;
}

}  // namespace apilayers
