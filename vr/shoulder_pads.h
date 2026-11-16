#pragma once
// shoulder_pads.h - [body] flat_hide_shoulder_right / flat_hide_shoulder_left: the monitor's first person (flat F3), the hero's shoulder pads
// not drawn. A Barbarian's swing lifts his collarbones and the pads on them come up in front of the camera and fill the screen (2026-11-16).
//
// What the game's data says (data/hd/character/player/<class>/, read with casc_extract + gr2dump, 3.3): every class's armour parts special1 and
// special2 are the right and the left pad (meshes rshoulder_* / lshoulder_*), nothing else in them. Each is skinned to pad bones of the hero's
// skeleton that hang under the collarbone: the Barbarian's R_Clavicle -> R_hvy_shoulder_pad -> R_hvy_pad_pivot (and _med_, and R_shoulder_pad ->
// R_pad_pivot, clip_adjust for the light armour); the Paladin's R_shoulderPad{,_med,_hvy}_bind_jnt; the Sorceress's R_shoulder_pad; the Necromancer's
// R_heavy_shoulderPad(_pivot), R_pad_pivot; the Druid's shoulderPad_r_*; the Assassin's HVY_/MED_/LIT_shoulderPad_R_JNT and plates; the Amazon's
// R_shoulderPad_*_bind_jnt. Those bones and everything under them are shrunk to a point (their places kept) in the pose the game draws, so
// what is skinned to them is not seen. Known gaps: the Amazon's left heavy and light pads are skinned to her upper arm, the Assassin's light ones
// to her spine and collarbone - those stay.
//
// Visual only, the drawn bones: the shadow loses the pads too (as the head's does in F3). Pure: no game (cleanroom/tests/shoulder_pads_test.cpp).

#include <cctype>
#include <string>
#include <vector>

namespace shoulderpads {

enum Side : char { kNone = 0, kRight = 1, kLeft = 2, kEither = 3 };   // kEither: a pad bone whose name says no side - hidden by either box

inline std::string Lower(const std::string& s) {
    std::string lo = s;
    for (char& ch : lo) ch = (char)std::tolower((unsigned char)ch);
    return lo;
}

// A bone that carries a shoulder pad: "shoulder" and "pad" in its name (shoulder_pad, shoulderPad, heavy_shoulderPad), or a "pad_pivot".
// Not a knee or hip pad, not the shoulder joint itself (R_shoulder_bind_jnt: no "pad").
inline bool IsPadName(const std::string& name) {
    const std::string lo = Lower(name);
    return (lo.find("shoulder") != std::string::npos && lo.find("pad") != std::string::npos) || lo.find("pad_pivot") != std::string::npos;
}

// Its side from the name's parts between underscores: r / right, l / left (R_hvy_pad_pivot, HVY_shoulderPad_L_JNT, shoulderPad_r_hvy_bind_jnt).
inline Side SideOfName(const std::string& name) {
    const std::string lo = Lower(name);
    Side side = kNone;
    size_t at = 0;
    while (at <= lo.size()) {
        const size_t end = lo.find('_', at);
        const std::string part = lo.substr(at, end == std::string::npos ? std::string::npos : end - at);
        if (part == "r" || part == "right") side = side == kLeft ? kEither : kRight;
        else if (part == "l" || part == "left") side = side == kRight ? kEither : kLeft;
        if (end == std::string::npos) break;
        at = end + 1;
    }
    return side == kNone ? kEither : side;
}

// One mark a bone (kNone: not a pad's): each pad bone by its name, every bone under one by its parent's. A rig lists a parent before its children;
// a later parent (a data mod's rig) is followed all the same.
inline std::vector<char> Marks(const std::vector<std::string>& names, const std::vector<int>& parent) {
    const int n = (int)names.size();
    std::vector<char> mark((size_t)n, kNone);
    if ((int)parent.size() != n) return mark;
    for (int pass = 0; pass < 2; ++pass)   // (the second: children listed before their parent)
        for (int i = 0; i < n; ++i) {
            if (mark[(size_t)i] != kNone) continue;
            if (IsPadName(names[(size_t)i])) mark[(size_t)i] = SideOfName(names[(size_t)i]);
            else if (const int p = parent[(size_t)i]; p >= 0 && p < n && mark[(size_t)p] != kNone) mark[(size_t)i] = mark[(size_t)p];
        }
    return mark;
}

// Is a bone of this mark hidden with these boxes?
inline bool Hidden(char mark, bool right, bool left) {
    return (mark == kRight && right) || (mark == kLeft && left) || (mark == kEither && (right || left));
}

// The shrink: each hidden bone's 3 x 3 part (row-vector 4 x 4 world matrices, m[0..2], m[4..6], m[8..10]) times kScale, its place kept - what is
// skinned to it collapses onto the bone. How many bones were shrunk.
inline constexpr float kScale = 0.001f;
inline int Shrink(float (*m)[16], int count, const std::vector<char>& marks, bool right, bool left) {
    if (!m || count <= 0 || (int)marks.size() < count || !(right || left)) return 0;
    int k = 0;
    for (int i = 0; i < count; ++i) {
        if (!Hidden(marks[(size_t)i], right, left)) continue;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) m[i][r * 4 + c] *= kScale;
        ++k;
    }
    return k;
}

// For the log: the pad bones a side has ("" none).
inline std::string List(const std::vector<std::string>& names, const std::vector<char>& marks, char side) {
    std::string s;
    for (size_t i = 0; i < names.size() && i < marks.size(); ++i)
        if (marks[i] == side) s += (s.empty() ? "" : " ") + names[i];
    return s;
}

}  // namespace shoulderpads
