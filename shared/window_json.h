#pragma once
// The game's windowed size in its Settings.json ("Window Mode", "Screen Resolution (Windowed)") and what D2R VR does to it: VR
// (native OpenXR and FlatVR alike) gets a square window (D2R VR Settings' "Square game window"); flat mode keeps the window the player
// had. No Windows in it: shared by vrcam (at its start, before the game reads the file) and D2R VR Settings (on a switch of the
// platform and on START), and tested without either.
//
// The player's own window is kept before the square is written: [openxr] window_before (WxH) and window_mode_before.

#include <cstdlib>
#include <string>

namespace winjson {

struct Windowed {
    bool ok = false;    // both keys were found
    int mode = -1;      // "Window Mode" (-1: no such key)
    int w = 0, h = 0;   // "Screen Resolution (Windowed)": "WxH"
};

// The player's own window, as kept in the ini. A square is not "own": it is what the VR square made.
struct Own {
    int w = 0, h = 0, mode = -1;
    bool has() const { return w >= 320 && h >= 200 && w != h; }
};

namespace detail {
// The position of the first character of the number after `"key":` (spaces allowed), or npos.
inline size_t After(const std::string& t, const char* key) {
    const std::string k = std::string("\"") + key + "\"";
    size_t at = t.find(k);
    if (at == std::string::npos) return at;
    at = t.find(':', at + k.size());
    if (at == std::string::npos) return at;
    ++at;
    while (at < t.size() && (t[at] == ' ' || t[at] == '\t' || t[at] == '\r' || t[at] == '\n')) ++at;
    return at;
}
inline bool Digit(char c) { return c >= '0' && c <= '9'; }
}  // namespace detail

inline Windowed Read(const std::string& text) {
    Windowed r;
    if (size_t m = detail::After(text, "Window Mode"); m != std::string::npos && m < text.size() &&
                                                       (detail::Digit(text[m]) || (text[m] == '-' && m + 1 < text.size() && detail::Digit(text[m + 1]))))
        r.mode = std::atoi(text.c_str() + m);
    const size_t q = detail::After(text, "Screen Resolution (Windowed)");
    if (q == std::string::npos || q >= text.size() || text[q] != '"') return r;
    size_t i = q + 1, j = i;
    while (j < text.size() && detail::Digit(text[j])) ++j;
    if (j == i || j >= text.size() || text[j] != 'x') return r;
    const int w = std::atoi(text.c_str() + i);
    i = j + 1;
    j = i;
    while (j < text.size() && detail::Digit(text[j])) ++j;
    if (j == i || j >= text.size() || text[j] != '"') return r;
    r.w = w;
    r.h = std::atoi(text.c_str() + i);
    r.ok = r.w > 0 && r.h > 0;
    return r;
}

// The text with the windowed size set to w x h and, when mode >= 0, "Window Mode" to it - everything else byte for byte.
// The same text back (and `ok` false) when a key is missing.
inline std::string With(const std::string& text, int w, int h, int mode, bool* ok = nullptr) {
    std::string out = text;
    if (ok) *ok = false;
    const size_t q = detail::After(out, "Screen Resolution (Windowed)");
    if (q == std::string::npos || q >= out.size() || out[q] != '"') return text;
    const size_t end = out.find('"', q + 1);
    if (end == std::string::npos) return text;
    out.replace(q + 1, end - q - 1, std::to_string(w) + "x" + std::to_string(h));
    if (mode >= 0) {
        const size_t m = detail::After(out, "Window Mode");
        if (m != std::string::npos && m < out.size() && (detail::Digit(out[m]) || out[m] == '-')) {
            size_t e = m;
            if (out[e] == '-') ++e;
            while (e < out.size() && detail::Digit(out[e])) ++e;
            out.replace(m, e - m, std::to_string(mode));
        }
    }
    if (ok) *ok = true;
    return out;
}

struct Plan {
    bool saveOwn = false;   // keep `own` as the player's own window ([openxr] window_before, window_mode_before)
    Own own;
    bool write = false;     // write this windowed size into Settings.json (mode -1: leave "Window Mode")
    int w = 0, h = 0, mode = -1;
    bool unknownOwn = false;   // a square window, not allowed, and none of the player's kept: nothing is written
    const char* why = "";
};

// What to do about the file. The square window is for VR (`squareAllowed`: [mode] platform=1, native OpenXR and FlatVR alike); FLAT
// mode (the monitor) keeps the player's own window - the mod never chooses a resolution there.
// `squareOn` = [openxr] square_window, `own` = what the ini kept.
//  - A window that is not square is the player's own: kept ([openxr] window_before, window_mode_before) in every mode, so that it is
//    there before anything makes it square.
//  - squareAllowed: nothing else here - the square itself is the settings' to write (the tick box, the size), vrcam's to keep.
//  - not allowed: a square window in the file is VR's square (or the game's write-back of it - the game saves the size its window
//    had): the player's own window goes back, size and mode. None kept (a file squared by an older version): NOTHING is written - the
//    game uses what the file says - and `unknownOwn` lets the caller say so.
inline Plan ForPlatform(bool squareAllowed, bool squareOn, const Windowed& now, const Own& own) {
    Plan p;
    if (!now.ok) return p;
    if (now.w != now.h) {
        p.saveOwn = true;
        p.own = {now.w, now.h, now.mode};
        p.why = "the player's own window kept before a square";
        return p;
    }
    if (squareAllowed || !(squareOn || own.has())) return p;
    if (!own.has()) {
        p.unknownOwn = true;
        p.why = "a square window and no window of the player's kept";
        return p;
    }
    p.write = true;
    p.w = own.w;
    p.h = own.h;
    p.mode = own.mode;
    p.why = "the window the player had before the square";
    return p;
}

}  // namespace winjson
