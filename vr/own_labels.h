#pragma once
// own_labels.h - [hud] labels_own: the item labels on the ground drawn by us, sharp, in the game's own places (docs/notes/custom_labels.md).
//
// The game still lays out and paints every label in its interface layer (its boxes are what the mouse, the laser, Show items and the pointed
// pick-up go by: none of that is touched). In first person (F4) D2R_DepthFog.fx draws each recorded item label back at its item's depth
// (GameLabels); with labels_own it draws OUR cell there instead of the game's pixels: the name painted at the size it has in the eye's
// pixels (vr/own_labels_paint.cpp), from one atlas texture (vr/own_labels_wire.cpp).
//
// This header is the pure part a test can hold: the name's colour codes, the colour it is drawn in, the size steps, the cell's key, the
// atlas's shelves, and where a cell is shown against the game's box. No game calls, no GDI.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "plate_look.h"   // DistanceK: [hud] labels_far's rule

namespace ownlabels {

constexpr int kAtlasW = 1024, kAtlasH = 1024;   // D2R_DepthFog.fx's OwnLabelTex is this size
constexpr int kTextMax = 96;                     // a name's bytes kept (UTF-8; the game's buffer is 0x80)

// ---- the name: the game's colour codes taken out ---------------------------------------------------------------------------------
// The game's strings carry colour codes: a marker and one character, the colour's index + '0' ('0'..'V'). The marker is the game's string
// "colorcode" (0xCD6E0 puts it and "%c" before an item's name), which in D2R is U+E07E, a private-use letter - UTF-8 EE 81 BE (read out of
// a full dump of the running game, 2026-11-06: an item label drew as a box and "=" before "Key", a box and "E" before "Minor Healing
// Potion"). Diablo II's own 'y' 'c' form is taken too: the 'y-diaeresis' as the byte 0xFF or UTF-8 C3 BF, then maybe 'c'. None of it is
// drawn, anywhere in the name; a marker with nothing after it (or not a code character) goes alone. The first code says the colour
// (CodeColour). Returns that code, 0 when there is none.
inline bool IsCodeChar(unsigned char c) { return c > 0x20 && c < 0x7F; }
inline size_t MarkerLen(const char* in, size_t i, size_t inMax) {
    const auto at = [&](size_t k) { return k < inMax ? (unsigned char)in[k] : (unsigned char)0; };
    if (at(i) == 0xEE && at(i + 1) == 0x81 && at(i + 2) == 0xBE) return 3;   // U+E07E, D2R's "colorcode"
    if (at(i) == 0xFF) return 1;
    if (at(i) == 0xC3 && at(i + 1) == 0xBF) return 2;
    return 0;
}
inline char StripCodes(const char* in, size_t inMax, char* out, size_t outN) {
    char code = 0;
    size_t o = 0;
    if (!out || !outN) return 0;
    for (size_t i = 0; in && i < inMax && in[i];) {
        const unsigned char c = (unsigned char)in[i];
        if (const size_t len = MarkerLen(in, i, inMax)) {
            size_t j = i + len;
            const auto at = [&](size_t k) { return k < inMax ? (unsigned char)in[k] : (unsigned char)0; };
            if (len < 3 && at(j) == 'c' && IsCodeChar(at(j + 1))) ++j;   // "y-diaeresis c 4": the 'c' then the code
            else if (len < 3 && at(j) == 'c' && !at(j + 1)) { i = j + 1; continue; }   // ... "y-diaeresis c" at the very end
            if (IsCodeChar(at(j))) {
                if (!code) code = (char)at(j);
                ++j;
            }
            i = j;
            continue;
        }
        if (c == '\n' || c == '\r' || (c == ' ' && !o)) {   // one line: a break is a space; none in front (the plate pads its name: "  Zombie  ")
            if (o + 1 < outN && o && out[o - 1] != ' ') out[o++] = ' ';
            ++i;
            continue;
        }
        // whole UTF-8 sequences only: a name cut at outN never ends in half a letter
        size_t seq = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        if (o + seq >= outN) break;
        for (size_t k = 0; k < seq && i < inMax && in[i]; ++k) out[o++] = in[i++];
    }
    while (o && out[o - 1] == ' ') --o;
    out[o] = 0;
    return code;
}

// A game icon in the text: a private-use letter (U+E000..U+F8FF) left once the colour codes are out - the game's text code draws its button
// sprites for those (0.205.12, Serg: "a box under the NPC's name - in the original the (A) icon"; Deckard Cain's second hover line). Our fonts have no
// such letter: a text that holds one is left for the game to draw itself (its icon and all). Returns the first such code point, 0 for none.
inline uint32_t IconIn(const char* utf8, size_t max) {
    char clean[kTextMax * 2];
    StripCodes(utf8, max, clean, sizeof clean);
    for (size_t i = 0; clean[i];) {
        const unsigned char c = (unsigned char)clean[i];
        uint32_t cp = c;
        size_t n = 1;
        if ((c >> 5) == 0x6 && clean[i + 1]) { cp = ((c & 0x1Fu) << 6) | ((unsigned char)clean[i + 1] & 0x3Fu); n = 2; }
        else if ((c >> 4) == 0xE && clean[i + 1] && clean[i + 2]) {
            cp = ((c & 0x0Fu) << 12) | (((unsigned char)clean[i + 1] & 0x3Fu) << 6) | ((unsigned char)clean[i + 2] & 0x3Fu);
            n = 3;
        } else if ((c >> 3) == 0x1E && clean[i + 1] && clean[i + 2] && clean[i + 3]) n = 4;
        if (cp >= 0xE000 && cp <= 0xF8FF) return cp;
        i += n;
    }
    return 0;
}

// The game's own colours for the codes, 0..1 - its text code (0x90C1C9) takes the index (code - '0') below 39: 0..12 from the classic
// palette (0x34B2670), 13..29 and 30..38 from two tables the game fills as it starts (0x2536C40, 0x2470750); all read out of the same dump.
// False: not a colour code.
inline bool CodeColour(char code, float rgb[3]) {
    static const uint8_t k[39][3] = {
        {255, 255, 255}, {255, 77, 77},   {0, 255, 0},     {105, 105, 255}, {199, 179, 119}, {105, 105, 105}, {0, 0, 0},
        {208, 194, 125}, {255, 168, 0},   {255, 255, 100}, {0, 128, 0},     {174, 0, 255},   {0, 200, 0},                       // 0..12
        {240, 240, 240}, {199, 179, 119}, {255, 255, 100}, {255, 168, 0},   {0, 128, 0},     {110, 110, 255}, {0, 252, 0},
        {199, 179, 119}, {240, 240, 240}, {240, 240, 240}, {240, 240, 240}, {240, 240, 240}, {99, 99, 99},    {255, 168, 0},
        {99, 99, 99},    {255, 168, 0},   {154, 72, 252},                                                                     // 13..29
        {238, 214, 141}, {6, 165, 221},   {255, 128, 255}, {170, 170, 255}, {0, 255, 0},     {255, 255, 118}, {208, 62, 62},
        {130, 197, 255}, {255, 0, 0}};                                                                                        // 30..38
    const int i = (unsigned char)code - '0';
    if (i < 0 || i >= 39) return false;
    for (int c = 0; c < 3; ++c) rgb[c] = k[i][c] / 255.0f;
    return true;
}

// The name's colour: the game's own code in the name when it put one there, else the item's rarity (`rarity`, from the item's quality -
// unleashed::lootaim::RarityColor; white when nothing is known).
inline void NameColour(char code, const float* rarity, float rgb[3]) {
    if (CodeColour(code, rgb)) return;
    for (int i = 0; i < 3; ++i) rgb[i] = rarity ? std::clamp(rarity[i], 0.0f, 1.0f) : 1.0f;
}

// ---- sizes -------------------------------------------------------------------------------------------------------------------------
// A cell is painted at a height of the eye's pixels taken from a ladder of ~12 % steps: walking changes a label's size all the time
// (labels_far), and a new size is a new picture - in steps it is painted again only now and then, and shown a little smaller than painted
// (never larger) so it stays sharp.
constexpr int kMinPx = 12, kMaxPx = 160;
inline int StepPx(float px) {
    if (!(px > 0.0f) || !std::isfinite(px)) return kMinPx;
    float s = (float)kMinPx;
    while (s < px && s < (float)kMaxPx) s = std::ceil(s * 1.12f);
    return std::min(kMaxPx, (int)s);
}
// The name's letters in a cell `h` pixels tall, and the room left and right of it (a bold name over a monster: larger letters).
inline int FontPx(int h) { return std::max(8, (int)std::lround(h * 0.70f)); }
inline int NameFontPx(int h) { return std::max(8, (int)std::lround(h * 0.82f)); }
// [hud] labels_own_min_px: our labels and names never smaller than this many pixels of the eye's picture (0.205.9, Serg: the plate's name at x0.51
// came out some 11 px tall, as small and soft as the game's). The factor a cell of eyePx pixels is made larger by (1: it is tall enough).
inline float MinScale(float eyePx, float minPx) { return eyePx > 0.0f && minPx > eyePx ? minPx / eyePx : 1.0f; }
inline int PadX(int h) { return std::max(3, (int)std::lround(h * 0.28f)); }

// ---- the game's fonts -----------------------------------------------------------------------------------------------------------------
// The game's text style starts with its font's id - the font manager's own names in order (0x8BA61E): Exocet 0, Formal 1, BlizzardGlobal 2,
// Philosopher 3. Ours is painted in the same face from D2R VR Settings' game-art cache (ExocetBlizzardOT-Medium.otf, Formal436BT.ttf; vr/game_ui.h),
// never made bold by GDI (Serg, 0.205.11: "the fonts are not the game's").
constexpr int8_t kFontExocet = 0, kFontFormal = 1;
inline int8_t FontOfStyle(int32_t first) { return first == kFontExocet || first == kFontFormal ? (int8_t)first : (int8_t)-1; }

// ---- the cell's key ----------------------------------------------------------------------------------------------------------------
// What makes two cells the same picture: the name, its colour, the box's colour (the game's hover, if it changes it), the height step and
// whether it is the pointed item ([input] point_pickup's look).
struct Want {
    char text[kTextMax] = {};
    float rgb[3] = {1.0f, 1.0f, 1.0f};
    float bg[4] = {0.0f, 0.0f, 0.0f, 0.6f};   // the game's own box colour
    int heightPx = kMinPx;                     // a StepPx value
    bool lit = false;                          // the pointed item
    bool inReach = true;                       // ... and A takes it from here (else its rim is red)
    bool bold = false;                         // a name over a monster / NPC: larger letters in its cell (bold only in our own face)
    int8_t font = -1;                          // the game's font for this text (its style's first word): kFontExocet, kFontFormal; -1 not known (Formal)
};
inline uint64_t Fnv(uint64_t h, const void* p, size_t n) {
    const unsigned char* b = (const unsigned char*)p;
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 0x100000001B3ull;
    return h;
}
inline uint64_t KeyOf(const Want& w) {
    uint64_t h = 0xCBF29CE484222325ull;
    h = Fnv(h, w.text, std::strlen(w.text));
    uint8_t q[9];
    for (int i = 0; i < 3; ++i) q[i] = (uint8_t)std::lround(std::clamp(w.rgb[i], 0.0f, 1.0f) * 255.0f);
    for (int i = 0; i < 4; ++i) q[3 + i] = (uint8_t)std::lround(std::clamp(w.bg[i], 0.0f, 1.0f) * 255.0f);
    q[7] = (uint8_t)std::clamp(w.heightPx, 0, 255);
    q[8] = (uint8_t)((w.lit ? 1 : 0) | (w.inReach ? 2 : 0) | (w.bold ? 4 : 0) | (w.font == kFontExocet ? 8 : 0));
    h = Fnv(h, q, sizeof q);
    return h ? h : 1;
}

// ---- the atlas: shelves ------------------------------------------------------------------------------------------------------------
// Cells go on shelves left to right, a new shelf under the last when a row is full. Full: Place fails and the owner starts it again
// empty (every name is painted again as it is asked for - a frame of the game's pixels for those).
struct Cell { int x = 0, y = 0, w = 0, h = 0; };
constexpr int kGap = 2;   // empty pixels round each cell: the linear sampler never reaches a neighbour
class Atlas {
public:
    Atlas(int w = kAtlasW, int h = kAtlasH) : w_(w), h_(h) {}
    void Clear() { entries_.clear(); shelves_.clear(); }
    const Cell* Find(uint64_t key) const {
        for (const Entry& e : entries_)
            if (e.key == key) return &e.cell;
        return nullptr;
    }
    // A place for a w x h cell, remembered under `key`; false when there is no room (or it can never fit).
    bool Place(uint64_t key, int w, int h, Cell* out) {
        if (w <= 0 || h <= 0 || w + 2 * kGap > w_ || h + 2 * kGap > h_) return false;
        for (Shelf& s : shelves_)
            if (h + 2 * kGap <= s.h && s.x + w + 2 * kGap <= w_) return Put(key, s, w, h, out);
        const int top = shelves_.empty() ? 0 : shelves_.back().y + shelves_.back().h;
        if (top + h + 2 * kGap > h_) return false;
        shelves_.push_back({top, h + 2 * kGap, 0});
        return Put(key, shelves_.back(), w, h, out);
    }
    size_t Count() const { return entries_.size(); }
    int W() const { return w_; }
    int H() const { return h_; }

private:
    struct Shelf { int y, h, x; };
    struct Entry { uint64_t key; Cell cell; };
    bool Put(uint64_t key, Shelf& s, int w, int h, Cell* out) {
        Cell c{s.x + kGap, s.y + kGap, w, h};
        s.x += w + 2 * kGap;
        entries_.push_back({key, c});
        if (out) *out = c;
        return true;
    }
    int w_, h_;
    std::vector<Shelf> shelves_;
    std::vector<Entry> entries_;
};

// ---- the labels' own sliders, as the game's labels have them -------------------------------------------------------------------------
// [hud] labels_size (`sizeFrac`, the F4 view's size, 1 = the game's) times [hud] labels_far's rule (`farFrac`: that size 1.5 m off and nearer,
// this share of it 10 m off and farther, a straight line between - platelook::DistanceK, vr/hud/labels.cpp LabelDistanceK). The layout hook
// makes the game's box this much larger or smaller (ResizeLabelBox) and our cell stands as tall as that box; when the box could not be made
// again (the old graphics, a measure the hook does not trust) the cell is sized by it itself (ShownRect's `scale`).
inline float SizeK(float groundM, float sizeFrac, float farFrac) { return sizeFrac * platelook::DistanceK(groundM, farFrac); }

// ---- our labels' own size by distance ([hud] labels_own_near_pct / _far_pct / _near_m / _far_m) -------------------------------------------
// Serg (0.205.6): "for the new names an even shrinking by distance of their own". The game's rule above is a straight line between 1.5 m and
// 10 m with corners at both ends - a label shrinks, then suddenly stops. Ours is one smooth curve: the size `nearPct` (% of the game's own) at
// nearM and nearer, `farPct` at farM and farther, eased between by the distance's logarithm (each doubling of the distance takes the same
// share of the way, as the eye sees distance) through smoothstep - no step, no corner, flat at both ends. Our item labels (the game's box is
// made that size, so pointing and picking match what is seen) and, with [hud] monster_names_own, the target's plate use it; the game's
// labels and plates keep their sliders.
struct SizeCurve {
    float nearPct = 100.0f, farPct = 40.0f;   // % of the game's own size
    float nearM = 1.5f, farM = 20.0f;         // ground distance from the hero's feet, metres
};
inline float CurveK(float groundM, const SizeCurve& c) {
    const float nearK = std::clamp(c.nearPct, 10.0f, 300.0f) * 0.01f, farK = std::clamp(c.farPct, 10.0f, 300.0f) * 0.01f;
    const float n = std::max(0.1f, c.nearM), f = std::max(n * 1.05f, c.farM);
    if (!(groundM > n)) return nearK;   // (not a number: as near)
    if (groundM >= f) return farK;
    float t = std::log(groundM / n) / std::log(f / n);
    t = t * t * (3.0f - 2.0f * t);
    return nearK + (farK - nearK) * t;
}
// [hud] labels_alpha (the backdrop's opacity, the F4 view's; [hud_top] / [hud_third] / [hud_floor] for theirs - vr/hud/looks.cpp LabelLookNow):
// the box colour the game paints (its {0, 0, 0, 0.6}) with its alpha that much lower, as HookLabelPaint gives the game's own paint. Below 0
// the game draws no box: left so. The name is not touched: it stays whole.
inline void BackdropRgba(const float game[4], float alpha, float out[4]) {
    std::copy_n(game, 4, out);
    if (out[3] > 0.0f) out[3] *= std::clamp(alpha, 0.0f, 1.0f);
}

// ---- where a cell is shown -----------------------------------------------------------------------------------------------------------
// Against the game's box (uv of the layer, l t r b): as tall as the box (times `scale`, when the box was not sized by the sliders), as wide as
// the cell's own shape makes it on a bufW x bufH eye picture, its middle over the box's middle, its bottom on the box's bottom (where the game
// hangs the label over its item). Out: uv l t r b.
inline bool ShownRect(const float box[4], int cellW, int cellH, float bufW, float bufH, float out[4], float scale = 1.0f) {
    const float bh = (box[3] - box[1]) * scale;
    if (!(bh > 0.0f) || !std::isfinite(bh) || cellW <= 0 || cellH <= 0 || !(bufW > 0.0f) || !(bufH > 0.0f)) return false;
    const float w = bh * ((float)cellW / (float)cellH) * (bufH / bufW);
    const float mid = 0.5f * (box[0] + box[2]);
    out[0] = mid - 0.5f * w;
    out[1] = box[3] - bh;
    out[2] = mid + 0.5f * w;
    out[3] = box[3];
    return true;
}
// The same, its middle kept where the box's middle is (a name in a plate, a hover name: the text is laid out about its middle).
inline bool CentredRect(const float box[4], int cellW, int cellH, float bufW, float bufH, float out[4], float scale = 1.0f) {
    if (!ShownRect(box, cellW, cellH, bufW, bufH, out, scale)) return false;
    const float dy = 0.5f * (box[1] + box[3]) - 0.5f * (out[1] + out[3]);
    out[1] += dy;
    out[3] += dy;
    return true;
}
// The cell's texels in the atlas as uv (l t r b), for the effect's sampler.
inline void CellUv(const Cell& c, int atlasW, int atlasH, float out[4]) {
    out[0] = (float)c.x / (float)atlasW;
    out[1] = (float)c.y / (float)atlasH;
    out[2] = (float)(c.x + c.w) / (float)atlasW;
    out[3] = (float)(c.y + c.h) / (float)atlasH;
}
// The step for a label whose box is `boxHuv` of the layer's height, on an eye picture bufH pixels tall.
inline int HeightFor(float boxHuv, float bufH) { return StepPx(boxHuv * bufH); }

}  // namespace ownlabels
