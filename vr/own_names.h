#pragma once
// own_names.h - [hud] monster_names_own: the name over the monster you target drawn by us, sharp, in the game's own plate (stage 2 of
// docs/notes/custom_labels.md). Off: the game's plate as before.
//
// The game's plate over the target (HUDMonsterHealth, its Health bar with the name in it; vr/gamestate.cpp ReadPlate) is built by the
// game's target routine (0xCEA50: the name, the separator, the immunities), which sends the plate its "SetInfo" message: the plate's
// handler 0x1F84E0 puts the name, the Unique line and the Additional line in its three text boxes (0x86DD50) and the colour beside the name
// into each box's style (+0xBC). vr/hud/labels.cpp hooks that handler READ-ONLY (PlateInfo below: the three texts, the colour) and the text
// boxes' drawing, TextDraw 0x902E20 - the call the item-label code already hooks (HookTextDraw). A text drawn there that IS one of the
// plate's texts (by its bytes; 0.205.5: a test of the box alone found none) is RECORDED with its box, and once our cell for it is on the
// screen, the game's own drawing of that one text is skipped (the bar, its frame and every other text stay the game's). The effect (vr/render/effects/effects.cpp, the plate's
// part; D2R_DepthFog.fx GameLabels' plate branch) draws our cell over the bar in the plate's own place - so the plate's depth, eyes by
// turns, [hud] monster_size, monster_far and monster_alpha act on our name as on the plate: it is part of it.
//
// Only shown: nothing here touches the game's state; the target, the attack, the hover are the game's.
//
// This header is the pure part a test can hold: which text is the plate's, the colour of a name, the record of the texts, whether the
// game's text may be skipped, how our cell lies over the bar. No game calls, no GDI.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "own_labels.h"
#include "melee_bodies.h"   // BodyFor: a monster's body from its MonStats row

namespace ownnames {

constexpr int kMax = 8;               // texts kept (the plate's name, its second line, their copies, a hover name): D2R_DepthFog.fx's PlateNameCells
constexpr uint64_t kFreshMs = 150;    // a record older than this is nothing to draw (no target)
constexpr uint64_t kShownMs = 250;    // our cell for a text shown this lately: the game's drawing of that text is skipped

// ---- which text is the plate's ------------------------------------------------------------------------------------------------------
// A text drawn with its box (x, y, w, h, the interface layer's pixels) whose middle stands inside the plate's box (l t r b, the same pixels),
// that box grown by a quarter of its height all round (the effect's box is a frame or two behind the one being laid out).
inline bool InPlate(const int32_t text[4], const float plate[4]) {
    if (text[2] <= 0 || text[3] <= 0 || !(plate[2] > plate[0]) || !(plate[3] > plate[1])) return false;
    const float g = 0.25f * (plate[3] - plate[1]);
    const float cx = (float)text[0] + 0.5f * (float)text[2], cy = (float)text[1] + 0.5f * (float)text[3];
    return cx >= plate[0] - g && cx <= plate[2] + g && cy >= plate[1] - g && cy <= plate[3] + g &&
           (float)text[3] <= 1.5f * (plate[3] - plate[1]) + 2.0f * g;   // no taller than the plate: not a panel's text behind it
}

// The box (x, y, w, h) of a text w x h laid out about the point (x, y) - how the plate's text boxes give TextDraw their place (a point, no
// size; its middle). False: no sane size.
inline bool BoxAbout(int32_t x, int32_t y, int32_t w, int32_t h, int32_t out[4]) {
    if (w <= 0 || h <= 0 || w > 8000 || h > 2000) return false;
    out[0] = x - w / 2;
    out[1] = y - h / 2;
    out[2] = w;
    out[3] = h;
    return true;
}

// ---- the names over NPCs and objects ([hud] npc_names_own) -------------------------------------------------------------------------------
// An NPC (MonStats says so: 0xC0420's test at 0xC0C42), an object, a portal or a warp gets no plate: the target routine hands it to 0xCE8F0,
// a name-only hover tooltip, and TooltipsPanel::Draw (0x880578) draws each of its tooltip records (0x2E8 bytes: + 0x00 the text, + 0x60 its
// box, + 0x28 a second line in + 0x70, + 0x80 the style, + 0x168 the background) with 0x8DA7E0 - its only caller. That one draws the texts
// through TextDraw from three places; a TextDraw whose return address is one of them is a hover name.
constexpr uint64_t kHoverSites[3] = {0x8DA8F6, 0x8DA9D2, 0x8DAA0F};   // after its calls of TextDraw at 0x8DA8F1 (with a life bar), 0x8DA9CD (the second line), 0x8DAA0A
inline int HoverSiteOf(uintptr_t ret, const uintptr_t sites[3]) {
    for (int i = 0; i < 3; ++i)
        if (sites[i] && ret == sites[i]) return i;
    return -1;
}
constexpr int kWhichHover = 10;   // a Text's `which` for a hover name (not one of the plate's)

// ---- our names' own three sliders ([hud] names_own_size_pct / names_own_far_pct / names_own_alpha) -------------------------------------------
// Serg (0.205.9): "the old sliders must not work for the new plates - like the items they need three of their own: size, size by distance,
// opacity". Our names over monsters and NPCs take none of the game plate's (monster_size, monster_far, monster_alpha, its depth) - the plate
// keeps those. Size: % of the game's text; by distance: at labels_own_far_m and farther this % of it, the item labels' smooth curve between
// (ownlabels::CurveK, its near / far distances); opacity: of a dark backdrop behind the name (0: none, the game's bar shows round the letters) -
// the letters stay whole.
struct NameLook {
    float sizePct = 100.0f;   // names_own_size_pct (0.205.10: 140 overflowed the plate)
    float farPct = 60.0f;     // names_own_far_pct: of the size, far off
    float alpha = 0.0f;       // names_own_alpha: the backdrop's
};
// The name's size against the game's text at groundM metres (have false: no distance known - a hover name, no target: the near size).
inline float NameK(const NameLook& l, float groundM, bool have, float nearM, float farM) {
    ownlabels::SizeCurve c;
    c.nearPct = l.sizePct;
    c.farPct = l.sizePct * std::clamp(l.farPct, 10.0f, 150.0f) * 0.01f;
    c.nearM = nearM;
    c.farM = farM;
    return ownlabels::CurveK(have ? groundM : 0.0f, c);
}
// The backdrop the name's cell gets: none below a sliver of opacity (the game's bar shows), else the game's dark box colour at that opacity.
inline void NameBackdrop(float alpha, float bg[4]) {
    bg[0] = bg[1] = bg[2] = 0.0f;
    bg[3] = alpha > 0.005f ? std::min(alpha, 1.0f) : -1.0f;
}

// ---- over the monster's head, in the world (0.205.10, Serg: "it is not over the head ... in stereo the text doubles") ----------------------
// The game's 2D plate stands where the game's own projection puts its anchor - beside the monster in VR, and at the plate's own depth. Our name
// stands at a point of the world: the top of the monster's body (its MonStats footprint and artwork height, meleetouch::BodyFor - the capsule hit
// by touch uses) plus kHeadGapM, centred on it; each eye projects that one point with its own camera (D2R_DepthFog.fx PhantomPixel), so both eyes
// meet at the monster.
constexpr float kHeadGapM = 0.10f;
constexpr uint64_t kTargetKeepMs = 600;   // the plate's hook missed a frame or two: its last target kept this long while the plate is still shown
// Why the target's body did not read (NamesTargetBody), for the log; a row that does not read keeps the anchor with kFallbackRow (the name still over
// its head, on its axis, at a common height) - 0.205.12 drew every name the 2D way and the log could not say why.
enum BodyWhy { kBodyOk = 0, kBodyNoUnit, kBodyNotMonster, kBodyNoData, kBodyRowOff, kBodyNoFoot };
inline const char* BodyWhyText(int w) {
    switch (w) {
        case kBodyOk: return "read";
        case kBodyNoUnit: return "no target unit (the plate's hook has none)";
        case kBodyNotMonster: return "the target is not a monster";
        case kBodyNoData: return "its monster data does not read";
        case kBodyRowOff: return "its MonStats row does not read as one (a common body used)";
        case kBodyNoFoot: return "its place on the grid does not read";
        default: return "?";
    }
}
constexpr meleetouch::MonsterRow kFallbackRow{2, 2, 0};   // (BodyFor: no height in the row - its 72, some 1.8 m at the hero's size)
// The anchor (render metres): over the body's top, on its axis. row: MonStats bytes 8, 9, 13; foot: its place on the grid (game WORLD units).
inline bool HeadAnchor(const meleetouch::MonsterRow& row, float footX, float footZ, float floorM, float upm, float out[3]) {
    stroke::Body b;
    if (!meleetouch::BodyFor(row, footX, footZ, floorM, upm, b)) return false;
    out[0] = b.top.x;
    out[1] = b.top.y + b.radius + kHeadGapM;
    out[2] = b.top.z;
    return std::isfinite(out[0] + out[1] + out[2]);
}
// A point relative to an eye -> its pixel on that eye's picture (bufW x bufH), as D2R_DepthFog.fx's PhantomPixel: the eye's right, up, back axes,
// its projection's M[0], M[5], M[8], M[9]. False: behind the eye.
inline bool EyePixel(const float v[3], const float R[3], const float U[3], const float B[3], const float proj[4], float bufW, float bufH, float out[2],
                     float* ahead = nullptr) {
    const float cx = v[0] * R[0] + v[1] * R[1] + v[2] * R[2], cy = v[0] * U[0] + v[1] * U[1] + v[2] * U[2], cz = v[0] * B[0] + v[1] * B[1] + v[2] * B[2];
    if (ahead) *ahead = -cz;
    if (!(-cz > 1e-4f)) return false;
    const float w = -cz;
    const float nx = cx / w * proj[0] - proj[2], ny = cy / w * proj[1] - proj[3];
    out[0] = (nx * 0.5f + 0.5f) * bufW;
    out[1] = (0.5f - ny * 0.5f) * bufH;
    return true;
}
// The line's rect on an eye's picture (pixels l t r b) about the point's pixel there: h tall, its cell's shape wide, its bottom `lift` above the
// point - as D2R_DepthFog.fx PlateNamesInWorld lays it out in each eye: the text and its backdrop are one cell, its middle over the point.
inline void WorldRect(const float atPx[2], float h, float cellAspect, float lift, float out[4]) {
    const float w = h * cellAspect;
    out[0] = atPx[0] - 0.5f * w;
    out[1] = atPx[1] - lift - h;
    out[2] = atPx[0] + 0.5f * w;
    out[3] = atPx[1] - lift;
}
// Where the game's plate (its bar) goes under our name, in an eye (uv of its picture): its box's middle half its shown height under the point's pixel -
// as D2R_DepthFog.fx's plate branch with PlateAt (0.205.13, Serg: "the name moved up, the plate stayed down"). boxH: the plate box's height (uv), scale
// its size (PlateScale).
inline void PlateUnder(const float atPx[2], float boxH, float scale, float bufW, float bufH, float outUv[2]) {
    outUv[0] = atPx[0] / bufW;
    outUv[1] = atPx[1] / bufH + 0.5f * boxH * scale;
}
// ---- our own plate over the head, in the game's style (Serg, 0.205.13: "draw our own life bar ... in the same style as the game's") -----------
// The game's target plate (HUDMonsterHealth): a frame strip - monster_healthbar_normal / _unique (16 x 72: a light rim above and below, black between)
// with its left and right caps (8 x 72) - and inside it the life bar, monsterhealth_console (1000 x 58, the red), as wide as the life; the name centred
// on it; the Unique / Additional line under it. Ours: the same pieces (D2R VR Settings copies them beside the plugin: gameuifiles::kPlateFiles), laid
// out at our point over the head - the line under the plate's bottom at the point, the plate above it - each eye projecting the point.
constexpr float kFrameH = 72.0f, kFrameCapW = 8.0f, kBarTop = 7.0f, kBarBottom = 65.0f;   // the frame's own pixels (the bar 58 of its 72)
struct PlateLayout {
    float frame[4] = {};   // px relative to the point's pixel (l t r b; y down: above the point is negative)
    float fill[4] = {};    // the life bar's whole room inside it (the fill takes the life's share of its width)
    float nameLift = 0.0f; // the name's bottom over the point (px), its middle on the bar's
    float lineLift = 0.0f; // the line under the plate: its bottom over the point (0: on it)
    float frameW = 0.0f, frameH = 0.0f;
};
// nameH / nameW: the name's cell on the eye (px); lineH: the line under it (0: none); gapPx between.
inline PlateLayout LayOutPlate(float nameH, float nameW, float lineH, float gapPx) {
    PlateLayout p;
    p.frameH = nameH * (kFrameH / (kBarBottom - kBarTop)) * 1.25f;   // the name sits in the bar with a little room above and below it
    const float cap = p.frameH * (kFrameCapW / kFrameH);
    p.frameW = std::max(nameW + 2.0f * cap + nameH * 0.6f, p.frameH * 3.5f);
    p.lineLift = 0.0f;
    const float bottom = lineH > 0.0f ? lineH + gapPx : 0.0f;   // the plate's bottom over the point
    p.frame[0] = -0.5f * p.frameW;
    p.frame[2] = 0.5f * p.frameW;
    p.frame[3] = -bottom;
    p.frame[1] = -bottom - p.frameH;
    const float k = p.frameH / kFrameH;
    p.fill[0] = p.frame[0] + cap;
    p.fill[2] = p.frame[2] - cap;
    p.fill[1] = p.frame[1] + kBarTop * k;
    p.fill[3] = p.frame[1] + kBarBottom * k;
    const float barMid = 0.5f * (p.fill[1] + p.fill[3]);
    p.nameLift = -(barMid + 0.5f * nameH);   // its bottom this far over the point: its middle on the bar's
    return p;
}
// The plate as the game lays it out (Serg's reference, 2026-11-07: "the right way to write the plates"): the bar is the game's Health widget - its
// size the plate's rect (gamestate PlateRect, layout units) times the interface scale, in the game's pixels; the name on it as the game measured it
// (its box's height); the Unique / Additional lines where the game's own copy of the plate stands them against its name. All of it at our name's
// scale (nameH over the game's name height). The frame's sprite fills that same rect - the plate's NormalMonsterBackground / UniqueMonsterBackground
// widgets have the Health bar's rect (the game's log: "Health at -78,-86 157x58, NormalMonsterBackground at -78,-86 157x58") - and the red lies
// inside its rims and caps (the sprite's own 7 of 72 above and below, its 8 px caps).
struct GamePlate {
    float nameBoxH = 0.0f;        // the game's name line height (its measured box), its pixels
    float barW = 0.0f, barH = 0.0f;   // its Health bar, its pixels
    float lineDy[3] = {};         // each line's middle under the name's middle, its pixels (0 the name itself)
    bool line[3] = {};            // which lines there are (1 Unique, 2 Additional)
};
struct GameLayout {
    PlateLayout plate;
    float lineLift[3] = {};       // each line's bottom over the point (px)
};
// nameH: our name line's height (px of the eye); lineH[i]: our lines' heights. The block's lowest edge on the point, everything above it.
inline bool LayOutGamePlate(float nameH, const GamePlate& g, const float lineH[3], GameLayout* out) {
    if (!(nameH > 0.0f) || !(g.nameBoxH > 0.0f) || !(g.barW > 0.0f) || !(g.barH > 0.0f)) return false;
    const float k = nameH / g.nameBoxH;   // our pixels for one of the game's
    const float frameW = g.barW * k, frameH = g.barH * k, u = frameH / kFrameH, cap = kFrameCapW * u;
    // about the name's middle (y down): the plate centred on it (the game centres its name on the bar), the lines by the game's offsets
    float top[4] = {-0.5f * frameH, 0, 0, 0}, bottom[4] = {0.5f * frameH, 0, 0, 0};
    float lowest = bottom[0];
    float mid[3] = {0.0f, 0.0f, 0.0f};
    for (int i = 1; i < 3; ++i)
        if (g.line[i] && lineH[i] > 0.0f) {
            mid[i] = g.lineDy[i] * k;
            lowest = std::max(lowest, mid[i] + 0.5f * lineH[i]);
        }
    GameLayout L;
    PlateLayout& p = L.plate;
    p.frameW = frameW;
    p.frameH = frameH;
    p.frame[0] = -0.5f * p.frameW;
    p.frame[2] = 0.5f * p.frameW;
    p.frame[1] = top[0] - lowest;
    p.frame[3] = bottom[0] - lowest;
    p.fill[0] = p.frame[0] + cap;
    p.fill[2] = p.frame[2] - cap;
    p.fill[1] = p.frame[1] + kBarTop * u;
    p.fill[3] = p.frame[1] + kBarBottom * u;
    p.nameLift = lowest - 0.5f * nameH;
    L.lineLift[0] = p.nameLift;
    for (int i = 1; i < 3; ++i) L.lineLift[i] = g.line[i] ? lowest - (mid[i] + 0.5f * lineH[i]) : 0.0f;
    *out = L;
    return true;
}

// How much of the bar is red at life `frac` (0..1) - px of its room.
inline float FillWidth(const PlateLayout& p, float frac) { return (p.fill[2] - p.fill[0]) * std::clamp(frac, 0.0f, 1.0f); }

// The lines of a plate in the world: one of each (the name 0, Unique 1, Additional 2), the name on top, the last one's bottom at the anchor. lift[i]:
// how many pixels above the anchor line i's bottom stands, given each line's height h[i] (0: not there).
inline void StackLines(const float h[3], float gapPx, float lift[3]) {
    float below = 0.0f;
    for (int i = 2; i >= 0; --i) {
        lift[i] = below;
        if (h[i] > 0.0f) below += h[i] + gapPx;
    }
}

// ---- the name's colour ------------------------------------------------------------------------------------------------------------------
// The game's own colour code in the text wins (ownlabels::StripCodes). Without one, the monster's kind: monster data + 0x1A, the bits the
// game itself tests (vr/mob_bars.h: 0x0E = super unique, champion, unique) - super unique and unique gold, champion blue, the rest (their
// minions, ordinary monsters) white, as the game shows them.
constexpr uint8_t kSuperUnique = 0x02, kChampion = 0x04, kUnique = 0x08, kMinion = 0x10;
inline char KindCode(uint8_t special) {
    if (special & (kSuperUnique | kUnique)) return '4';
    if (special & kChampion) return '3';
    return 0;
}
inline void NameColour(char code, uint8_t special, bool specialKnown, float rgb[3]) {
    if (ownlabels::CodeColour(code, rgb)) return;
    if (specialKnown && ownlabels::CodeColour(KindCode(special), rgb)) return;
    rgb[0] = rgb[1] = rgb[2] = 1.0f;
}

// ---- what the plate was told ------------------------------------------------------------------------------------------------------------
// The plate's SetInfo message (its id at message + 0x88; its data at message + 0x110): + 0x00 the name, + 0x08 the colour the three text
// boxes get (16 bytes), + 0x20 the Unique line, + 0x28 the Additional line (the handler takes + 0x28 when + 0x20 is empty).
constexpr uint64_t kSetInfo = 0x40FFBA2D98A3FF97ull;
constexpr uintptr_t kMsgId = 0x88, kMsgData = 0x110, kDataName = 0x00, kDataColour = 0x08, kDataUnique = 0x20, kDataAdditional = 0x28;
struct PlateInfo {
    char text[3][0x80] = {};   // the name, the Unique line, the Additional line, as the game sent them
    uint8_t colour[16] = {};
    int32_t life = -1;         // + 0x18: the life the bar is given, percent (the bar's setter 0x2AC890 clamps it to 0..100)
    uint8_t unique = 0;        // + 0x38: the handler shows UniqueMonsterBackground for it (else NormalMonsterBackground)
    uint64_t ms = 0;
};
constexpr uintptr_t kDataLife = 0x18, kDataUniqueBg = 0x38;
// The bar's fill from what the plate was told: 0..1 (the setter's own clamp); < 0: not known.
inline float LifeFrac(int32_t pct) { return pct < 0 ? -1.0f : (pct > 100 ? 1.0f : pct / 100.0f); }
constexpr uint64_t kInfoMs = 1000;   // the plate told this lately: its texts are looked for
// Which of the plate's texts this is (0 the name, 1, 2), or -1.
inline int PlateTextOf(const PlateInfo& p, const char* text, uint64_t nowMs) {
    if (!text || !text[0] || !p.ms || (nowMs > p.ms && nowMs - p.ms > kInfoMs)) return -1;
    for (int i = 0; i < 3; ++i)
        if (p.text[i][0] && std::strncmp(p.text[i], text, sizeof p.text[i] - 1) == 0) return i;
    return -1;
}
// The colour the game gave the plate's texts: 16 bytes - read as four floats 0..1 (red, green, blue, alpha) when they are that, else as four
// ints 0..255 (the game's other colour tables). False: neither (then the code in the text, or the target's kind).
inline bool InfoColour(const uint8_t c[16], float rgb[3]) {
    float f[4];
    std::memcpy(f, c, sizeof f);
    bool floats = true, any = false;
    for (int i = 0; i < 4; ++i) {
        floats = floats && f[i] == f[i] && (f[i] == 0.0f || (f[i] >= 1e-4f && f[i] <= 1.001f));   // (small ints read as floats are tiny: not a colour)
        any = any || f[i] > 0.0f;
    }
    if (floats && any && f[3] > 0.0f) {
        for (int i = 0; i < 3; ++i) rgb[i] = std::min(f[i], 1.0f);
        return true;
    }
    int32_t n[4];
    std::memcpy(n, c, sizeof n);
    bool ints = true;
    for (int i = 0; i < 4; ++i) ints = ints && n[i] >= 0 && n[i] <= 255;
    if (ints && n[3] > 0 && (n[0] | n[1] | n[2])) {
        for (int i = 0; i < 3; ++i) rgb[i] = n[i] / 255.0f;
        return true;
    }
    return false;
}

// ---- the record of the plate's texts ----------------------------------------------------------------------------------------------------
// Written on the game's interface thread (every text the plate draws, every frame), read on the frame's (the effect). The newest frame's
// texts: those recorded within kSameFrameMs of the newest one, one of each text (the newest).
constexpr uint64_t kSameFrameMs = 4;
// The same text drawn twice in one frame in two places is two texts (0.205.7, Serg's log: the plate's "Undead " line kept from a copy the game
// draws near the top middle, 852,254, while the one under the name stayed the game's): one place is within this many pixels of the other.
constexpr int32_t kSamePlacePx = 48;
inline bool SamePlace(const int32_t a[4], const int32_t b[4]) {
    const int32_t dx = (a[0] + a[2] / 2) - (b[0] + b[2] / 2), dy = (a[1] + a[3] / 2) - (b[1] + b[3] / 2);
    return dx * dx + dy * dy <= kSamePlacePx * kSamePlacePx;
}
struct Text {
    char raw[0x80] = {};      // the bytes as the game drew them (colour codes and all)
    int32_t box[4] = {};      // x, y, w, h in the interface layer's pixels
    uint64_t ms = 0;
    uint64_t seq = 0;         // the order they were drawn in (0: empty)
    int which = -1;           // the plate's text it is (PlateTextOf: 0 the name)
    float rgb[3] = {1.0f, 1.0f, 1.0f};
    bool rgbKnown = false;    // ... and the colour the game gave it (InfoColour)
    int8_t font = -1;         // its style's font (ownlabels::FontOfStyle)
};
class Record {
public:
    void Add(const char* text, const int32_t box[4], uint64_t ms, int which = -1, const float* rgb = nullptr, int8_t font = -1) {
        if (!text || !text[0]) return;
        Text& t = ring_[next_++ % kRing];
        t.font = font;
        t.which = which;
        t.rgbKnown = rgb != nullptr;
        for (int i = 0; i < 3; ++i) t.rgb[i] = rgb ? rgb[i] : 1.0f;
        size_t n = 0;
        while (n + 1 < sizeof t.raw && text[n]) { t.raw[n] = text[n]; ++n; }
        t.raw[n] = 0;
        std::copy_n(box, 4, t.box);
        t.ms = ms;
        t.seq = ++seq_;
    }
    // The newest frame's texts into out (kMax at most), top to bottom; how many.
    int Take(uint64_t nowMs, Text out[kMax]) const {
        const Text* last = nullptr;
        for (const Text& t : ring_)
            if (t.seq && (!last || t.seq > last->seq)) last = &t;
        if (!last || (nowMs > last->ms && nowMs - last->ms > kFreshMs)) return 0;   // (a record a millisecond "newer" than now: another thread's clock read)
        const uint64_t newest = last->ms;
        int n = 0;
        for (const Text& t : ring_) {
            if (!t.seq || t.ms > newest || newest - t.ms > kSameFrameMs) continue;
            int same = -1;
            for (int i = 0; i < n; ++i)
                if (std::strcmp(out[i].raw, t.raw) == 0 && SamePlace(out[i].box, t.box)) same = i;
            if (same >= 0) {
                if (t.seq > out[same].seq) out[same] = t;
                continue;
            }
            if (n < kMax) out[n++] = t;
        }
        std::sort(out, out + n, [](const Text& a, const Text& b) { return a.box[1] < b.box[1]; });
        return n;
    }
    void Clear() { for (Text& t : ring_) t = Text{}; }

private:
    static constexpr size_t kRing = 16;
    Text ring_[kRing];
    size_t next_ = 0;
    uint64_t seq_ = 0;
};

// One copy of the plate's lines - the game draws them in two places: the name's copy in the plate's box when there is one (else the first), and each
// other line the copy nearest to that name (the same plate). In place; returns how many are left, one of each line.
inline int OneCopyOfEach(Text* t, int n, const float* plateBox) {
    int name = -1;
    for (int i = 0; i < n; ++i)
        if (t[i].which == 0 && (name < 0 || (plateBox && InPlate(t[i].box, plateBox) && !InPlate(t[name].box, plateBox)))) name = i;
    Text keep[kMax];
    int k = 0;
    if (name >= 0) keep[k++] = t[name];
    for (int w = 1; w < 3; ++w) {
        int best = -1;
        float bestD = 0.0f;
        for (int i = 0; i < n; ++i) {
            if (t[i].which != w) continue;
            const float dx = name >= 0 ? (float)(t[i].box[0] + t[i].box[2] / 2 - t[name].box[0] - t[name].box[2] / 2) : 0.0f;
            const float dy = name >= 0 ? (float)(t[i].box[1] + t[i].box[3] / 2 - t[name].box[1] - t[name].box[3] / 2) : 0.0f;
            const float d = dx * dx + dy * dy;
            if (best < 0 || d < bestD) { best = i; bestD = d; }
        }
        if (best >= 0 && k < kMax) keep[k++] = t[best];
    }
    for (int i = 0; i < n; ++i)   // (texts of no line: kept as they were)
        if ((t[i].which < 0 || t[i].which > 2) && k < kMax) keep[k++] = t[i];
    for (int i = 0; i < k; ++i) t[i] = keep[i];
    return k;
}

// ---- the game's plate hidden on every frame while ours is on (0.206.5, Serg: "now and then the old plates flash in the frame") ------------------
// It was hidden only on the frames where ours was drawn - a new target's first frame, a frame the plate's hook missed (no target unit), a frame
// before our cell was painted showed the game's plate for a frame. Now: with monster_names_own on in first person its box is never drawn and every
// copy of what it was told is skipped, whether ours could be drawn this frame or not - nothing of it rather than a flash of it.
inline bool GamePlateHidden(bool namesOn, bool firstPerson) { return namesOn && firstPerson; }
// The game's drawing of a text skipped: a text the plate was told (which 0..2), with our names on - always; a hover name (npc_names_own) only once
// ours is shown in its place (the hover tooltip has no other text of ours behind it).
inline bool SkipGameText(bool namesOn, int which, bool oursShownLately) {
    if (which >= 0 && which <= 2) return namesOn;
    return oursShownLately;
}

// ---- the game's text skipped only while ours is seen ------------------------------------------------------------------------------------
// The effect marks the texts it drew our cell for (their bytes' key, the time); the hook skips the game's drawing of a text only when it is
// marked lately - so with an older effect, a name not painted yet or the plate not drawn by the effect, the game's text stays.
inline uint64_t KeyOf(const char* raw) { return ownlabels::Fnv(0xCBF29CE484222325ull, raw, std::strlen(raw)) | 1; }
struct Shown {
    uint64_t key[kMax] = {};
    uint64_t ms[kMax] = {};
    void Mark(uint64_t k, uint64_t nowMs) {
        int at = 0;
        for (int i = 0; i < kMax; ++i) {
            if (key[i] == k) { at = i; break; }
            if (ms[i] < ms[at]) at = i;
        }
        key[at] = k;
        ms[at] = nowMs;
    }
    bool Lately(uint64_t k, uint64_t nowMs) const {
        for (int i = 0; i < kMax; ++i)
            if (key[i] == k && nowMs >= ms[i] && nowMs - ms[i] <= kShownMs) return true;
        return false;
    }
};

// ---- our cell over the bar ---------------------------------------------------------------------------------------------------------------
// The layer's pixel (premultiplied colour, a = how much of the world shows through) with our cell's (premultiplied, a = cover) over it -
// what D2R_DepthFog.fx's plate branch does.
inline void Over(const float cell[4], const float layer[4], float out[4]) {
    const float k = 1.0f - std::clamp(cell[3], 0.0f, 1.0f);
    for (int i = 0; i < 3; ++i) out[i] = cell[i] + layer[i] * k;
    out[3] = layer[3] * k;
}

}  // namespace ownnames
