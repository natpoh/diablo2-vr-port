#pragma once
// The corner map for the hand, kept in a corner while the game runs (docs/notes/minimap_extract.md).
//
// hud.cpp takes the map out of the picture by its scissor: a box in a TOP CORNER of the safe area
// (IsMapScissor). The game's third choice, the map in the middle ("AutoMapMode" 0, the whole-screen
// overlay), has no such box: nothing comes into the hand and the map is drawn across the view.
// MiniMapForHand (vrcam.cpp) puts the map on the right in Settings.json at the plugin's start - once,
// and only when the plugin runs before the game reads the file; the game's own options put it back in
// the middle for the rest of the session. So the game's live map mode is read and, while the map is to
// come into the hand and the game has it in the middle, set to a corner by the game's own setter.
// A map already in a corner (left or right) is never touched: a player with the map on the right sees
// no change.
//
// The setter and its two globals are bearstonem's finding (D2R VR Unleashed, github.com/bearstonem/d2r-pcvr,
// MIT: vr/gamestate.cpp MaintainMapLayout, steamvr/wrist_hud.h MapLayoutLease): D2R 3.3.93787 at RVA
// 0xd3d80, void(int mode) - 0 the whole screen, 1 the left corner, 2 the right corner - which changes the
// map's layout in the running game only, not the saved options. Its first bytes are checked before every
// use; the globals are not hand-written addresses but the targets of the setter's own RIP-relative
// operands in those checked bytes (MapGlobals) - the byte "left" (sete) and the int "small" (mov).

#include <cstdint>
#include <cstring>

namespace minimapside {

constexpr uint64_t kSetterRva = 0xd3d80;
// sub rsp,28h; cmp ecx,1; sete [left]; cmp ecx,[mini]; je +1d; xor eax,eax; test ecx,ecx; setne al; mov [mini],eax
constexpr uint8_t kSetterSig[] = {0x48,0x83,0xec,0x28,0x83,0xf9,0x01,0x0f,0x94,0x05,0x47,0x70,0x95,0x02,
    0x3b,0x0d,0x3c,0x70,0x95,0x02,0x74,0x1d,0x33,0xc0,0x85,0xc9,0x0f,0x95,0xc0,
    0x89,0x05,0x2d,0x70,0x95,0x02};

// The RVAs of the two globals, from the setter's bytes: each operand's disp32 from the end of its instruction.
struct Globals { uint64_t mini = 0, left = 0; };
inline Globals MapGlobals(uint64_t setterRva, const uint8_t* code) {
    auto target = [&](int dispAt, int end) {
        int32_t d;
        std::memcpy(&d, code + dispAt, sizeof d);
        return setterRva + (uint64_t)end + (uint64_t)(int64_t)d;
    };
    Globals g;
    g.left = target(10, 14);    // 0f 94 05 <disp32>: sete byte [rip+disp]
    g.mini = target(31, 35);   // 89 05 <disp32>:    mov [rip+disp], eax
    return g;
}

// The game's map mode from the two globals: 0 the whole screen, 1 left, 2 right; -1 when they make no sense.
inline int Mode(int32_t mini, uint8_t left) {
    if (mini < 0 || mini > 1 || left > 1) return -1;
    return mini ? (left ? 1 : 2) : 0;
}

// [hud] map_corner (0 either, 1 left, 2 right): the corner the map is put in when it is in the middle.
inline int CornerFor(int mapCorner) { return mapCorner == 1 ? 1 : 2; }

// One step on the UI thread: the mode to set now, or -1. want: VR, [hud] map=1, a hero in the game, no menu.
// A corner is never changed; the middle is set once, and asked again only after a second if it stays there.
class Keeper {
    uint64_t setAt = 0;
public:
    int Step(bool want, int mode, int mapCorner, uint64_t now) {
        if (!want || mode != 0) { setAt = 0; return -1; }
        if (setAt && now - setAt < 1000) return -1;
        setAt = now ? now : 1;
        return CornerFor(mapCorner);
    }
};

}   // namespace minimapside
