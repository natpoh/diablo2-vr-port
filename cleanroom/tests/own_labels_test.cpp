// [hud] labels_own (vr/own_labels.h): the item labels painted by us in the game's places, F4. Offline, no game:
//  - the name: the game's colour codes taken out (0xFF and UTF-8 'ÿ'), one line, cut on a whole letter; the code's colour or the rarity;
//  - the size ladder (a cell is never shown larger than painted, and walking does not repaint it every step), the key;
//  - the atlas's shelves (no two cells touch, full says so), where a cell stands against the game's box, its texels;
//  - the painter (the name bright in its colour on the game's dark box, the pointed item's rim) and the worker's atlas (queued, painted, handed
//    to the upload before its cells).
// Run: own_labels_test.exe [--dump <prefix>] (the painted cells and the atlas as <prefix>_*.bmp, on black, to look at).

#include "../../vr/own_labels.h"
#include "../../vr/own_labels_paint.h"
#include "../../vr/own_labels_wire.h"
#include "../../vr/own_names.h"
#include "../../vr/game_ui.h"
#include "../../shared/game_ui_files.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef small   // (rpcndr.h / windef.h: test variables are called these)
#undef near
#undef far

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_failures = 0, g_checks = 0;
static void Expect(bool ok, const char* what, double value = 0.0) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}

using namespace ownlabels;

static void WriteBmp(const std::string& path, const std::vector<uint32_t>& px, int w, int h) {
    const int row = (w * 3 + 3) & ~3, size = 54 + row * h;
    std::vector<unsigned char> b(static_cast<size_t>(size), 0);
    const auto put32 = [&](int at, int v) { for (int i = 0; i < 4; ++i) b[static_cast<size_t>(at + i)] = static_cast<unsigned char>((v >> (8 * i)) & 255); };
    b[0] = 'B'; b[1] = 'M'; put32(2, size); put32(10, 54); put32(14, 40); put32(18, w); put32(22, h); b[26] = 1; b[28] = 24; put32(34, row * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint32_t p = px[static_cast<size_t>(h - 1 - y) * w + x];   // premultiplied over black: the colour as it is
            unsigned char* d = &b[static_cast<size_t>(54 + y * row + x * 3)];
            d[0] = static_cast<unsigned char>((p >> 16) & 255); d[1] = static_cast<unsigned char>((p >> 8) & 255); d[2] = static_cast<unsigned char>(p & 255);
        }
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") == 0 && f) { std::fwrite(b.data(), 1, b.size(), f); std::fclose(f); std::printf("  the picture: %s\n", path.c_str()); }
}

static void TestNames() {
    char out[kTextMax];
    char code = StripCodes("Short Sword", 0x80, out, sizeof out);
    Expect(code == 0 && std::strcmp(out, "Short Sword") == 0, "a plain name stays as it is");
    code = StripCodes("\xFF" "c4Shako", 0x80, out, sizeof out);
    Expect(code == '4' && std::strcmp(out, "Shako") == 0, "0xFF 'c' '4': taken out, the code kept");
    code = StripCodes("\xC3\xBF" "c8Ber Rune", 0x80, out, sizeof out);
    Expect(code == '8' && std::strcmp(out, "Ber Rune") == 0, "UTF-8 'y-diaeresis' 'c' '8': taken out too");
    code = StripCodes("\xFF" "c3Jagged \xFF" "c0Sword", 0x80, out, sizeof out);
    Expect(code == '3' && std::strcmp(out, "Jagged Sword") == 0, "every code out, the first one says the colour");
    code = StripCodes("Harlequin Crest\nShako", 0x80, out, sizeof out);
    Expect(std::strcmp(out, "Harlequin Crest Shako") == 0, "a line break is a space: one line");
    code = StripCodes("Gold  \n", 0x80, out, sizeof out);
    Expect(std::strcmp(out, "Gold") == 0, "no spaces at the end");
    char small[8];
    StripCodes("ab\xD0\x96\xD0\x96\xD0\x96", 0x80, small, sizeof small);   // ab + three 2-byte letters: 8 bytes, 7 fit with the 0
    Expect(std::strcmp(small, "ab\xD0\x96\xD0\x96") == 0, "cut on a whole letter (no half of a Cyrillic letter)");
    char raw[4] = {'A', 'B', 'C', 'D'};   // no 0 in the game's buffer: inMax holds it
    StripCodes(raw, 3, out, sizeof out);
    Expect(std::strcmp(out, "ABC") == 0, "the name's buffer is never read past its size");
    // Serg's headset, 0.205.3: "\xFF" "E" before the name was drawn as a box and an 'E'.
    code = StripCodes("\xFF" "EMinor Healing Potion", 0x80, out, sizeof out);
    Expect(code == 'E' && std::strcmp(out, "Minor Healing Potion") == 0, "0xFF 'E' (no 'c'): taken out, the code kept");
    code = StripCodes("\xC3\xBF" "cEMinor Healing Potion", 0x80, out, sizeof out);
    Expect(code == 'E' && std::strcmp(out, "Minor Healing Potion") == 0, "UTF-8 'y-diaeresis' 'c' 'E': taken out");
    code = StripCodes("\xC3\xBF" "EName", 0x80, out, sizeof out);
    Expect(code == 'E' && std::strcmp(out, "Name") == 0, "UTF-8 'y-diaeresis' 'E' (no 'c'): taken out");
    code = StripCodes("Minor \xFF" "c1Healing Potion", 0x80, out, sizeof out);
    Expect(code == '1' && std::strcmp(out, "Minor Healing Potion") == 0, "a code in the middle: taken out, its colour kept");
    code = StripCodes("Minor \xFF" "1Healing", 0x80, out, sizeof out);
    Expect(code == '1' && std::strcmp(out, "Minor Healing") == 0, "a code in the middle without 'c': taken out");
    code = StripCodes("Potion\xFF", 0x80, out, sizeof out);
    Expect(code == 0 && std::strcmp(out, "Potion") == 0, "a lone 0xFF at the very end: taken out");
    code = StripCodes("Potion\xFF" "c", 0x80, out, sizeof out);
    Expect(code == 0 && std::strcmp(out, "Potion") == 0, "0xFF 'c' at the very end: taken out");
    code = StripCodes("Potion\xC3\xBF", 0x80, out, sizeof out);
    Expect(code == 0 && std::strcmp(out, "Potion") == 0, "a lone UTF-8 'y-diaeresis' at the very end: taken out");
    code = StripCodes("\xFF", 0x80, out, sizeof out);
    Expect(code == 0 && out[0] == 0, "only a marker: an empty name (the game's label shows)");
    code = StripCodes("Gem\xFF Shard", 0x80, out, sizeof out);
    Expect(code == 0 && std::strcmp(out, "Gem Shard") == 0, "a marker before a space: only the marker goes");
    char cut[3] = {'\xFF', 'c', '4'};   // the buffer ends inside a code: inMax holds it
    code = StripCodes(cut, 2, out, sizeof out);
    Expect(code == 0 && out[0] == 0, "a code cut by the buffer's end: not read past it");

    // D2R's own form (0.205.5, Serg's headset: "[box]=Key"; the bytes read out of a full dump of the running game): the marker is the game's
    // "colorcode" string, U+E07E (UTF-8 EE 81 BE), then the colour's index + '0'.
    code = StripCodes("\xEE\x81\xBE" "=Key", 0x80, out, sizeof out);
    Expect(code == '=' && std::strcmp(out, "Key") == 0, "U+E07E '=' Key: taken out, the code kept");
    code = StripCodes("\xEE\x81\xBE" "EMinor Healing Potion", 0x80, out, sizeof out);
    Expect(code == 'E' && std::strcmp(out, "Minor Healing Potion") == 0, "U+E07E 'E' Minor Healing Potion: taken out");
    code = StripCodes("Ber \xEE\x81\xBE" "8Rune", 0x80, out, sizeof out);
    Expect(code == '8' && std::strcmp(out, "Ber Rune") == 0, "U+E07E in the middle: taken out, its colour kept");
    code = StripCodes("Key\xEE\x81\xBE", 0x80, out, sizeof out);
    Expect(code == 0 && std::strcmp(out, "Key") == 0, "a lone U+E07E at the very end: taken out");
    code = StripCodes("\xEE\x81\xBE" "cKey", 0x80, out, sizeof out);
    Expect(code == 'c' && std::strcmp(out, "Key") == 0, "U+E07E never takes a 'c' of D2's form: 'c' is the code (index 51: no colour)");
    code = StripCodes("\xE2\x80\x94Key", 0x80, out, sizeof out);
    Expect(code == 0 && std::strcmp(out, "\xE2\x80\x94Key") == 0, "another three-byte letter (an em dash) is kept");

    float rgb[3];
    Expect(CodeColour('=', rgb) && std::fabs(rgb[0] - 240.0f / 255) < 1e-5f && rgb[0] == rgb[2], "'=' (13): the game's item white 240,240,240 - the key's");
    Expect(CodeColour('E', rgb) && std::fabs(rgb[1] - 240.0f / 255) < 1e-5f, "'E' (21): white too - the potion's");
    Expect(CodeColour('4', rgb) && std::fabs(rgb[0] - 199.0f / 255) < 1e-5f && std::fabs(rgb[2] - 119.0f / 255) < 1e-5f, "'4': the game's unique gold 199,179,119");
    Expect(CodeColour('V', rgb) && rgb[0] == 1.0f && rgb[1] == 0.0f, "'V' (38): the last in the game's tables, red");
    Expect(!CodeColour('W', rgb) && !CodeColour('/', rgb) && !CodeColour(0, rgb), "past the tables: no colour");
    const float rare[3] = {1.0f, 1.0f, 0.39f};
    NameColour('4', rare, rgb);
    Expect(std::fabs(rgb[0] - 199.0f / 255) < 1e-4f && std::fabs(rgb[2] - 119.0f / 255) < 1e-4f, "the game's code wins over the rarity");
    NameColour(0, rare, rgb);
    Expect(rgb[0] == 1.0f && rgb[2] == 0.39f, "no code: the item's rarity");
    NameColour('Z', nullptr, rgb);
    Expect(rgb[0] == 1.0f && rgb[1] == 1.0f && rgb[2] == 1.0f, "not a code, no rarity: white");
}

static void TestSizes() {
    int last = 0;
    bool monotonic = true, never_smaller = true, near = true;
    for (float px = 1.0f; px <= 200.0f; px += 0.5f) {
        const int s = StepPx(px);
        monotonic = monotonic && s >= last;
        last = s;
        if (px >= kMinPx && px <= kMaxPx) {
            never_smaller = never_smaller && s >= px;
            near = near && s <= std::ceil(px * 1.13f) + 1.0f;
        }
    }
    Expect(monotonic, "the ladder goes up with the size");
    Expect(never_smaller, "a cell is painted at least as tall as it is shown (never stretched up)");
    Expect(near, "... and at most one step (~12 %) taller");
    Expect(StepPx(0.0f) == kMinPx && StepPx(-3.0f) == kMinPx && StepPx(NAN) == kMinPx, "nothing / nonsense: the smallest");
    Expect(StepPx(5000.0f) == kMaxPx, "huge: the largest");
    int steps = 0;
    for (int s = kMinPx; s < kMaxPx; s = StepPx((float)s + 0.5f)) ++steps;
    Expect(steps < 30, "few steps between the smallest and the largest", steps);
    Expect(HeightFor(0.02f, 2000.0f) == StepPx(40.0f), "a box 2 % of the layer on a 2000-pixel eye: the 40-pixel step");
}

static void TestKeys() {
    Want a;
    strcpy_s(a.text, "Shako");
    a.rgb[0] = 0.9f;
    Want b = a;
    Expect(KeyOf(a) == KeyOf(b), "the same name, colour, size, look: the same cell");
    b.heightPx = 40;
    Expect(KeyOf(a) != KeyOf(b), "another size: another cell");
    b = a;
    b.lit = true;
    Expect(KeyOf(a) != KeyOf(b), "pointed: another cell");
    b = a;
    b.bg[3] = 0.9f;
    Expect(KeyOf(a) != KeyOf(b), "another box colour (the game's hover): another cell");
    b = a;
    strcpy_s(b.text, "Shakos");
    Expect(KeyOf(a) != KeyOf(b), "another name: another cell");
}

static bool Overlap(const Cell& a, const Cell& b) {
    return a.x < b.x + b.w + kGap && b.x < a.x + a.w + kGap && a.y < b.y + b.h + kGap && b.y < a.y + a.h + kGap;
}
static void TestAtlas() {
    Atlas at(256, 128);
    std::vector<Cell> cells;
    Cell c;
    int placed = 0;
    for (uint64_t k = 1; k <= 200; ++k) {
        const int w = 20 + (int)(k * 37 % 90), h = 14 + (int)(k * 11 % 20);
        if (!at.Place(k, w, h, &c)) break;
        cells.push_back(c);
        ++placed;
    }
    bool inside = true, apart = true;
    for (size_t i = 0; i < cells.size(); ++i) {
        inside = inside && cells[i].x >= kGap && cells[i].y >= kGap && cells[i].x + cells[i].w + kGap <= 256 && cells[i].y + cells[i].h + kGap <= 128;
        for (size_t j = 0; j < i; ++j) apart = apart && !Overlap(cells[i], cells[j]);
    }
    Expect(placed > 5 && placed < 200, "cells placed until the atlas is full", placed);
    Expect(inside, "every cell inside the atlas with its gap");
    Expect(apart, "no two cells within the gap of each other (the sampler never reaches a neighbour)");
    Expect(at.Find(1) && at.Find(1)->x == cells[0].x, "found by its key");
    Expect(!at.Find(100000), "a key never placed: none");
    Expect(!at.Place(999, 300, 10, &c) && !at.Place(998, 10, 200, &c), "too large for the atlas: never placed");
    at.Clear();
    Expect(at.Count() == 0 && !at.Find(1) && at.Place(1, 100, 20, &c) && c.x == kGap && c.y == kGap, "cleared: empty, the first cell at the corner again");
}

static void TestPlace() {
    const float box[4] = {0.40f, 0.50f, 0.50f, 0.52f};   // the game's box: 10 % wide, 2 % tall
    float r[4];
    Expect(ShownRect(box, 200, 40, 2000.0f, 2000.0f, r), "a cell's place");
    Expect(std::fabs(r[1] - 0.50f) < 1e-6f && std::fabs(r[3] - 0.52f) < 1e-6f, "as tall as the game's box, its bottom on the box's bottom");
    Expect(std::fabs(0.5f * (r[0] + r[2]) - 0.45f) < 1e-6f, "its middle over the box's middle");
    Expect(std::fabs((r[2] - r[0]) - 0.10f) < 1e-5f, "its own shape: 5:1 on a square eye, 2 % tall -> 10 % wide", r[2] - r[0]);
    Expect(ShownRect(box, 200, 40, 2000.0f, 1000.0f, r) && std::fabs((r[2] - r[0]) - 0.05f) < 1e-5f, "a wide eye picture: the same pixels, half the uv");
    const float flat[4] = {0.4f, 0.5f, 0.5f, 0.5f};
    Expect(!ShownRect(flat, 200, 40, 2000.0f, 2000.0f, r) && !ShownRect(box, 0, 40, 2000.0f, 2000.0f, r), "no height or no cell: no place");
    float uv[4];
    CellUv(Cell{2, 2, 100, 20}, 1024, 1024, uv);
    Expect(std::fabs(uv[0] - 2.0f / 1024) < 1e-7f && std::fabs(uv[3] - 22.0f / 1024) < 1e-7f, "its texels as uv");
}

// [hud] labels_size / labels_far / labels_alpha on our labels, as on the game's (Serg, 0.205.3: "на эту надпись должен влиять ползунок
// уменьшения надписей на расстоянии ... и прозрачность подложки").
static void TestSliders() {
    const float size = 0.8f, far = 0.4f;   // labels_size 80 %, labels_far 40 %
    Expect(std::fabs(SizeK(0.5f, size, far) - 0.8f) < 1e-6f, "nearer than 1.5 m: labels_size itself", SizeK(0.5f, size, far));
    Expect(std::fabs(SizeK(1.5f, size, far) - 0.8f) < 1e-6f, "1.5 m: labels_size itself");
    Expect(std::fabs(SizeK(10.0f, size, far) - 0.32f) < 1e-6f, "10 m: labels_far of labels_size (0.4 x 0.8)", SizeK(10.0f, size, far));
    Expect(std::fabs(SizeK(25.0f, size, far) - 0.32f) < 1e-6f, "farther than 10 m: no smaller");
    Expect(std::fabs(SizeK(5.75f, size, far) - 0.56f) < 1e-5f, "half way (5.75 m): half way between", SizeK(5.75f, size, far));
    bool down = true;
    for (float m = 0.0f, last = 10.0f; m < 15.0f; m += 0.25f) {
        const float k = SizeK(m, size, far);
        down = down && k <= last + 1e-6f;
        last = k;
    }
    Expect(down, "farther is never larger");
    Expect(std::fabs(SizeK(10.0f, 1.0f, 1.0f) - 1.0f) < 1e-6f, "the sliders at 100 %: the game's own size anywhere");
    Expect(std::fabs(SizeK(10.0f, 1.2f, 0.5f) - platelook::DistanceK(10.0f, 0.5f) * 1.2f) < 1e-6f, "the rule the game's labels get (LabelDistanceK)");

    // our own curve (labels_own_*): one smooth, even shrink by distance - the ends, no step, no corner, the right way
    SizeCurve c;   // 100 % at 1.5 m and nearer, 40 % at 20 m and farther
    Expect(CurveK(0.0f, c) == 1.0f && CurveK(1.5f, c) == 1.0f && std::fabs(CurveK(20.0f, c) - 0.4f) < 1e-6f && std::fabs(CurveK(80.0f, c) - 0.4f) < 1e-6f,
           "the curve's ends: the near size near, the far size far");
    bool mono = true, smooth = true;
    float last = CurveK(0.0f, c), maxStep = 0.0f;
    for (float m = 0.0f; m <= 30.0f; m += 0.01f) {
        const float k = CurveK(m, c);
        mono = mono && k <= last + 1e-6f;
        maxStep = std::max(maxStep, last - k);
        last = k;
    }
    smooth = maxStep < 0.002f;   // 1 cm farther never takes more than 0.2 % off: no step anywhere
    Expect(mono, "farther is never larger");
    Expect(smooth, "continuous: no step anywhere", maxStep);
    const float dNear = (CurveK(1.5f + 0.05f, c) - CurveK(1.5f, c)) / 0.05f, dFar = (CurveK(20.0f, c) - CurveK(20.0f - 0.05f, c)) / 0.05f;
    const float dMid = (CurveK(5.5f, c) - CurveK(5.45f, c)) / 0.05f;
    Expect(std::fabs(dNear) < 0.01f && std::fabs(dFar) < 0.01f && dMid < -0.02f, "flat into both ends (no corner), shrinking in between", dNear);
    const float k3 = CurveK(3.0f, c), k6 = CurveK(6.0f, c), k12 = CurveK(12.0f, c);
    Expect(k3 > k6 && k6 > k12 && k3 < 1.0f && k12 > 0.4f, "each doubling of the distance takes some off, inside the ends");
    Expect(std::isfinite(CurveK(std::nanf(""), c)) && CurveK(std::nanf(""), c) == 1.0f, "a distance not known: the near size");
    SizeCurve up;   // a far size larger than the near one: it grows, the same way
    up.nearPct = 50.0f; up.farPct = 120.0f;
    Expect(CurveK(1.0f, up) == 0.5f && std::fabs(CurveK(25.0f, up) - 1.2f) < 1e-6f && CurveK(5.0f, up) > 0.5f && CurveK(5.0f, up) < 1.2f, "growing when asked");
    SizeCurve bad;   // a far distance at or under the near one: still a curve, no division by zero
    bad.nearM = 5.0f; bad.farM = 2.0f;
    Expect(std::isfinite(CurveK(5.1f, bad)) && CurveK(4.0f, bad) == 1.0f && std::fabs(CurveK(9.0f, bad) - 0.4f) < 1e-6f, "the far distance under the near one: no division by zero");

    // labels_own_min_px (0.205.9: the plate's name at x0.51 was some 11 px on the eye, as soft as the game's): never smaller than that
    Expect(std::fabs(MinScale(11.0f, 24.0f) - 24.0f / 11.0f) < 1e-5f, "11 px with a 24 px floor: drawn 24 px");
    Expect(MinScale(30.0f, 24.0f) == 1.0f && MinScale(11.0f, 0.0f) == 1.0f && MinScale(0.0f, 24.0f) == 1.0f, "tall enough, no floor, no size: as it is");
    {
        const float tb[4] = {0.40f, 0.50f, 0.46f, 0.51f};   // a plate's text, 1 % tall
        float cr[4];
        Expect(CentredRect(tb, 120, 30, 1792.0f, 1792.0f, cr, 2.0f) && std::fabs(0.5f * (cr[1] + cr[3]) - 0.505f) < 1e-6f &&
                   std::fabs((cr[3] - cr[1]) - 0.02f) < 1e-6f, "a name made larger stays about its middle");
        Want plain, bold;
        strcpy_s(plain.text, "Quill Rat");
        strcpy_s(bold.text, "Quill Rat");
        plain.heightPx = bold.heightPx = 30;
        plain.bg[3] = bold.bg[3] = -1.0f;
        bold.bold = true;
        Expect(KeyOf(plain) != KeyOf(bold), "a bold name is its own cell");
        std::vector<uint32_t> pp, pb;
        int pw = 0, ph = 0, bw2 = 0, bh2 = 0;
        long inkP = 0, inkB = 0;
        if (Paint(plain, nullptr, &pp, &pw, &ph) && Paint(bold, nullptr, &pb, &bw2, &bh2)) {
            for (uint32_t v : pp) inkP += (v & 255) > 200 ? 1 : 0;
            for (uint32_t v : pb) inkB += (v & 255) > 200 ? 1 : 0;
        }
        Expect(NameFontPx(30) > FontPx(30) && inkB > inkP * 5 / 4, "the bold name: larger, heavier letters than an item label's", (double)inkB);
    }

    // the box made that size by the game (ownK 1): our cell as tall as the box; the box left (ownK = SizeK): the cell sized by it itself
    const float box[4] = {0.40f, 0.50f, 0.50f, 0.52f};
    float r[4];
    Expect(ShownRect(box, 200, 40, 2000.0f, 2000.0f, r, 0.5f), "a cell sized by the sliders itself");
    Expect(std::fabs(r[3] - 0.52f) < 1e-6f && std::fabs((r[3] - r[1]) - 0.01f) < 1e-6f, "half the box's height, its bottom on the box's bottom", r[3] - r[1]);
    Expect(std::fabs(0.5f * (r[0] + r[2]) - 0.45f) < 1e-6f && std::fabs((r[2] - r[0]) - 0.05f) < 1e-5f, "still over the box's middle, in its shape");
    Expect(HeightFor(0.02f * 0.5f, 2000.0f) < HeightFor(0.02f, 2000.0f), "painted smaller too (a smaller step), not only shown smaller");

    // labels_alpha: the backdrop, never the name
    const float game[4] = {0.0f, 0.0f, 0.0f, 0.6f};
    float bg[4];
    BackdropRgba(game, 0.25f, bg);
    Expect(std::fabs(bg[3] - 0.15f) < 1e-6f && bg[0] == 0.0f, "labels_alpha 25 %: the game's 0.6 backdrop at 0.15", bg[3]);
    BackdropRgba(game, 1.0f, bg);
    Expect(std::fabs(bg[3] - 0.6f) < 1e-6f, "labels_alpha 100 %: the game's own");
    const float none[4] = {0.0f, 0.0f, 0.0f, -1.0f};
    BackdropRgba(none, 0.5f, bg);
    Expect(bg[3] < 0.0f, "the game's no-box (below 0) stays no box");

    Want n;
    strcpy_s(n.text, "Minor Healing Potion");
    n.heightPx = 40;
    std::vector<uint32_t> full, faint;
    int w1 = 0, h1 = 0, w2 = 0, h2 = 0;
    const bool p1 = Paint(n, nullptr, &full, &w1, &h1);
    BackdropRgba(game, 0.1f, n.bg);
    const bool p2 = Paint(n, nullptr, &faint, &w2, &h2);
    Expect(p1 && p2 && w1 == w2 && h1 == h2, "the same name at both backdrops");
    if (p1 && p2 && w1 == w2 && h1 == h2) {
        const uint32_t a1 = full[(size_t)(h1 / 2) * w1 + 3] >> 24, a2 = faint[(size_t)(h2 / 2) * w2 + 3] >> 24;
        Expect(a1 > 130 && a2 < 40, "the backdrop's own pixels: 0.6 -> 0.06", a2);
        int ink = 0, inkSame = 0;
        for (size_t i = 0; i < full.size(); ++i)
            if ((full[i] >> 24) == 255 && (full[i] & 255) >= 250) {   // the inside of a white letter (its edges blend with what is under them)
                ++ink;
                inkSame += faint[i] == full[i] ? 1 : 0;
            }
        Expect(ink > 40 && inkSame == ink, "the letters stay whole and as bright whatever the backdrop", inkSame);
    }
}

// [hud] monster_names_own (vr/own_names.h): which texts are the plate's, a name's colour, the record of the plate's texts, the game's text skipped only while
// ours is seen, our cell over the bar.
static void TestMonsterNames() {
    using namespace ownnames;
    const float plate[4] = {900.0f, 300.0f, 1100.0f, 340.0f};   // the plate's box, layer pixels: 200 x 40
    const int32_t name[4] = {940, 308, 120, 24};
    Expect(InPlate(name, plate), "the name in the plate's bar is the plate's");
    const int32_t below[4] = {960, 342, 80, 16};   // a second line just under the box (the effect's box a frame behind)
    Expect(InPlate(below, plate), "a text just past the box's edge (a frame behind) still the plate's");
    const int32_t belt[4] = {100, 1000, 30, 20}, panel[4] = {800, 100, 400, 400}, none[4] = {950, 310, 0, 20};
    Expect(!InPlate(belt, plate) && !InPlate(panel, plate) && !InPlate(none, plate), "the belt's numbers, a panel's text behind, an empty box: not the plate's");
    const float noPlate[4] = {0, 0, 0, 0};
    Expect(!InPlate(name, noPlate), "no plate: nothing is the plate's");
    // 0.205.6, Serg's log: the plate's texts come to TextDraw as a point ("  Zombie" at 867,956 0x0; "Undead" at 960,284 0x0) and the plate's
    // box was 786,914-945,997 px. The text measured by the game (here 96 x 28) about that point is in the plate; the other line is not.
    const float zPlate[4] = {786.0f, 914.0f, 945.0f, 997.0f};
    const int32_t zPoint[4] = {867, 956, 0, 0};
    Expect(!InPlate(zPoint, zPlate), "a point with no size is no box (why 0.205.6 matched nothing)");
    int32_t zBox[4];
    Expect(BoxAbout(867, 956, 96, 28, zBox) && zBox[0] == 819 && zBox[1] == 942 && zBox[2] == 96 && zBox[3] == 28, "the measured text about its point");
    Expect(InPlate(zBox, zPlate), "\"  Zombie\" measured about its point: in the plate - ours stands in for it");
    int32_t uBox[4];
    Expect(BoxAbout(960, 284, 80, 28, uBox) && !InPlate(uBox, zPlate), "\"Undead\" at 960,284: not in the plate - the game's stays");
    Expect(!BoxAbout(867, 956, 0, 28, zBox) && !BoxAbout(867, 956, 96, -1, zBox), "no sane measure: no box");
    char trimmed[kTextMax];
    StripCodes("  Zombie  ", 0x80, trimmed, sizeof trimmed);
    Expect(std::strcmp(trimmed, "Zombie") == 0, "the plate's padded name (\"  Zombie  \"): no spaces either side");

    float rgb[3];
    NameColour(0, kUnique, true, rgb);
    Expect(std::fabs(rgb[0] - 199.0f / 255) < 1e-4f && std::fabs(rgb[2] - 119.0f / 255) < 1e-4f, "a unique monster: gold");
    NameColour(0, kSuperUnique, true, rgb);
    Expect(std::fabs(rgb[0] - 199.0f / 255) < 1e-4f, "a super unique (Bishibosh): gold");
    NameColour(0, kChampion, true, rgb);
    Expect(std::fabs(rgb[2] - 1.00f) < 1e-4f && rgb[0] < 0.5f, "a champion: blue");
    NameColour(0, kMinion, true, rgb);
    Expect(rgb[0] == 1.0f && rgb[1] == 1.0f && rgb[2] == 1.0f, "a minion: white, as the game");
    NameColour('1', kUnique, true, rgb);
    Expect(rgb[0] == 1.0f && rgb[1] < 0.5f, "the game's own code in the text wins over the kind");
    NameColour(0, kChampion, false, rgb);
    Expect(rgb[0] == 1.0f && rgb[2] == 1.0f, "the kind not read: white");

    // what the plate is told (its SetInfo message): its three texts, matched by their bytes; the colour it gives them
    PlateInfo info;
    strcpy_s(info.text[0], "Bishibosh");
    strcpy_s(info.text[2], "Fire Enchanted");
    info.ms = 5000;
    Expect(PlateTextOf(info, "Bishibosh", 5100) == 0 && PlateTextOf(info, "Fire Enchanted", 5100) == 2, "the plate's name and its other line, by their bytes");
    Expect(PlateTextOf(info, "Bishibos", 5100) < 0 && PlateTextOf(info, "Fallen", 5100) < 0 && PlateTextOf(info, "", 5100) < 0, "any other text: not the plate's");
    Expect(PlateTextOf(info, "Bishibosh", 5000 + kInfoMs + 1) < 0, "the plate not told lately (no target): nothing is the plate's");
    Expect(PlateTextOf(info, "Bishibosh", 4999) == 0, "told a millisecond \"after\" the reader's clock: still the plate's");
    uint8_t colour[16];
    const float gold[4] = {0.78f, 0.70f, 0.47f, 1.0f};
    std::memcpy(colour, gold, sizeof gold);
    Expect(InfoColour(colour, rgb) && std::fabs(rgb[0] - 0.78f) < 1e-6f, "the colour as four floats");
    const int32_t blue[4] = {110, 110, 255, 255};
    std::memcpy(colour, blue, sizeof blue);
    Expect(InfoColour(colour, rgb) && std::fabs(rgb[2] - 1.0f) < 1e-6f && std::fabs(rgb[0] - 110.0f / 255) < 1e-6f, "the colour as four ints 0..255");
    std::memset(colour, 0, sizeof colour);
    Expect(!InfoColour(colour, rgb), "no colour (all zero): none");
    std::memset(colour, 0xCD, sizeof colour);
    Expect(!InfoColour(colour, rgb), "bytes that are no colour: none");

    Record rec;
    Text t[kMax];
    Expect(rec.Take(1000, t) == 0, "nothing recorded: nothing to draw");
    rec.Add("\xFF" "c4Bishibosh", name, 1000);
    rec.Add("Immune to Fire", below, 1001);
    int n = rec.Take(1005, t);
    Expect(n == 2 && std::strcmp(t[0].raw, "\xFF" "c4Bishibosh") == 0 && std::strcmp(t[1].raw, "Immune to Fire") == 0, "one frame's texts, top to bottom", n);
    const int32_t moved[4] = {944, 306, 120, 24};
    rec.Add("\xFF" "c4Bishibosh", moved, 1015);   // the next frame: the same name, moved with the head
    rec.Add("Immune to Fire", below, 1015);
    n = rec.Take(1016, t);
    Expect(n == 2 && t[0].box[0] == 944, "the same text in a newer frame: one of it, the newest place", t[0].box[0]);
    {   // 0.205.8, Serg's log (17:49:51): the plate told "  an evil force  "; the game drew it at 793,219 207x26 (the top place) and at 443,1321 169x22
        // (over the monster's head); the effect's plate box took turns: 877,156-1579,233 then 399,1293-607,1421 then 374,1344-607,1421. Every copy is
        // ours now, whichever box the plate has that frame: the one in the box with the plate, the other where the game drew it; the game skips both.
        PlateInfo evil;
        strcpy_s(evil.text[0], "  an evil force  ");
        const int32_t topCopy[4] = {793, 219, 207, 26}, headCopy[4] = {443, 1321, 169, 22};
        const float boxes[3][4] = {{877, 156, 1579, 233}, {399, 1293, 607, 1421}, {374, 1344, 607, 1421}};
        Record rec2;
        Shown shown2;
        bool everyFrame = true, headWithPlate = true, topWithPlate = true;
        for (int f = 0; f < 6; ++f) {
            const uint64_t ms = 9000 + 16 * f;
            evil.ms = ms;
            const int a = PlateTextOf(evil, "  an evil force  ", ms);
            rec2.Add("  an evil force  ", topCopy, ms, a);
            rec2.Add("  an evil force  ", headCopy, ms, a);
            Text u[kMax];
            const int k = rec2.Take(ms + 1, u);
            const float* box = boxes[f % 3];
            int drawn = 0, inBox = 0;
            for (int i = 0; i < k; ++i) {
                if (u[i].which != 0) continue;
                ++drawn;   // every copy of the told text is drawn by us
                const bool in = InPlate(u[i].box, box);
                inBox += in ? 1 : 0;
                if (u[i].box[1] == 1321) headWithPlate = headWithPlate && (in == (f % 3 != 0));
                if (u[i].box[1] == 219) topWithPlate = topWithPlate && (in == (f % 3 == 0));
                shown2.Mark(KeyOf(u[i].raw), ms);
            }
            everyFrame = everyFrame && a == 0 && k == 2 && drawn == 2 && inBox == 1 && shown2.Lately(KeyOf("  an evil force  "), ms + 5);
        }
        Expect(everyFrame, "the log's sequence: both copies ours every frame, one of them in the plate's box that frame, the game's skipped");
        Expect(headWithPlate && topWithPlate, "the copy over the head goes with the plate when the box is there; the top one when the box is at the top");
    }
    {   // 0.205.9, Serg: "why beside the monsters, not over them" - the plate's own stereo depth (monster_stereo_by_distance near 3.4 %, monster_depth_body 100)
        // puts it nearer than its monster; our name stands at the monster's own depth, as an item label at its item's (DepthShift, k 1)
        platelook::PlateIn pin;
        pin.firstPerson = true; pin.stereo = true; pin.depthPercent = 3.4f; pin.atMonster = 1.0f; pin.alpha = 0.5f; pin.size = 1.0f;
        pin.groundM = 8.0f; pin.groundUnits = 8.0f * 26.0f; pin.eyeHeightUnits = 45.0f; pin.m0 = 1.2f; pin.halfGap = 1.6f; pin.conv = 60.0f;
        const platelook::PlateUniforms pu = platelook::Plate(pin);
        const float nameShift = platelook::DepthShift(pin.groundUnits, pin.eyeHeightUnits, pin.m0, pin.halfGap, pin.conv, 1.0f);
        const float itemShift = platelook::DepthShift(8.0f * 26.0f, 45.0f, 1.2f, 1.6f, 60.0f, 1.0f);
        Expect(nameShift == itemShift, "the name at its monster's depth: the item labels' rule at that distance");
        Expect(pu.shift > nameShift + 0.01f, "the plate with monster_stereo on stands nearer than its monster (one eye: beside it); the name does not", pu.shift - nameShift);
    }
    {   // our names' own three sliders (Serg, 0.205.9: "the old sliders must not work for the new plates"): defaults, the curve, the backdrop
        const NameLook def;
        Expect(def.sizePct == 100.0f && def.farPct == 60.0f && def.alpha == 0.0f, "the defaults: the game's text size, 60 % of it far off, no backdrop");
        Expect(std::fabs(NameK(def, 1.0f, true, 1.5f, 20.0f) - 1.0f) < 1e-6f && std::fabs(NameK(def, 30.0f, true, 1.5f, 20.0f) - 0.6f) < 1e-5f,
               "near: the size; far: 60 % of it (1.0 -> 0.6)");
        Expect(std::fabs(NameK(def, 30.0f, false, 1.5f, 20.0f) - 1.0f) < 1e-6f, "no distance (a hover name, no target): the near size");
        bool down = true, cont = true;
        float last = NameK(def, 0.0f, true, 1.5f, 20.0f);
        for (float m = 0.0f; m < 40.0f; m += 0.01f) {
            const float k = NameK(def, m, true, 1.5f, 20.0f);
            down = down && k <= last + 1e-6f;
            cont = cont && last - k < 0.003f;
            last = k;
        }
        Expect(down && cont, "farther never larger, no step");
        NameLook same = def;
        same.farPct = 100.0f;
        Expect(std::fabs(NameK(same, 15.0f, true, 1.5f, 20.0f) - 1.0f) < 1e-6f, "far 100 %: the same size at any distance");
    }
    {   // over the monster's head in the world, both eyes meeting there (0.205.10: "not over the head, and in stereo it doubles")
        const meleetouch::MonsterRow fallen{2, 2, 18};   // a small monster: footprint 2, artwork height 18 (1.8 m at 1 unit / 0.1 m... at upm 1: clamped)
        float at[3];
        const float upm = 2.6f, floorM = 0.0f;
        Expect(HeadAnchor(fallen, 26.0f, 52.0f, floorM, upm, at), "a monster's head anchor from its MonStats row and place");
        stroke::Body b;
        meleetouch::BodyFor(fallen, 26.0f, 52.0f, floorM, upm, b);
        Expect(std::fabs(at[1] - (b.top.y + b.radius + kHeadGapM)) < 1e-6f && at[0] == b.top.x && at[2] == b.top.z, "the anchor: over the body's top by the gap, on its axis");
        // two eyes 64 mm apart looking down -z, the same projection; the point 6 m ahead: each eye's pixel, and the depth they meet at
        const float R[3] = {1, 0, 0}, U[3] = {0, 1, 0}, B[3] = {0, 0, 1}, proj[4] = {1.0f, 1.0f, 0.0f, 0.0f};
        const float bufW = 1792.0f, bufH = 1792.0f, e = 0.032f, d = 6.0f;
        const float vl[3] = {0.0f + e, 0.3f, -d}, vr[3] = {0.0f - e, 0.3f, -d};   // (the point relative to the left eye at -e, the right at +e)
        float pl[2], pr[2];
        Expect(EyePixel(vl, R, U, B, proj, bufW, bufH, pl) && EyePixel(vr, R, U, B, proj, bufW, bufH, pr), "both eyes see it");
        const float dispNdc = (pl[0] - pr[0]) / bufW * 2.0f;   // its two places apart (left eye's further right: nearer than infinity)
        const float meet = 2.0f * e * proj[0] / dispNdc;
        Expect(std::fabs(meet - d) < 1e-3f && std::fabs(pl[1] - pr[1]) < 1e-3f, "the two eyes' places meet at the point's own distance, at the same height", meet);
        // 0.205.11, Serg: "in one eye the text is against the plate's left edge, in the other against its right" - ours is one cell (text and backdrop)
        // laid out about the point in each eye: its middle is the point's pixel in both, so the text sits centred in its backdrop in each eye
        float rl[4], rr[4];
        WorldRect(pl, 30.0f, 4.0f, 0.0f, rl);
        WorldRect(pr, 30.0f, 4.0f, 0.0f, rr);
        Expect(std::fabs(0.5f * (rl[0] + rl[2]) - pl[0]) < 1e-4f && std::fabs(0.5f * (rr[0] + rr[2]) - pr[0]) < 1e-4f && rl[3] == pl[1] && rr[3] == pr[1],
               "each eye: the name's cell (text and backdrop) centred over the point, its bottom on it");
        const float meet2 = 2.0f * e * proj[0] / ((0.5f * (rl[0] + rl[2]) - 0.5f * (rr[0] + rr[2])) / bufW * 2.0f);
        Expect(std::fabs(meet2 - d) < 1e-3f && std::fabs((rl[2] - rl[0]) - (rr[2] - rr[0])) < 1e-4f, "the two eyes' cells meet at the point's distance, the same width");
        // the game's plate under our name (PlateAt): in each eye its middle under the point's pixel, its top on the point - so it too meets at the monster
        float ul[2], ur[2];
        PlateUnder(pl, 0.04f, 0.8f, bufW, bufH, ul);
        PlateUnder(pr, 0.04f, 0.8f, bufW, bufH, ur);
        Expect(std::fabs(ul[0] * bufW - pl[0]) < 1e-3f && std::fabs((ul[1] - 0.016f) * bufH - pl[1]) < 1e-2f, "the plate's middle under the point, its top on it (left eye)");
        Expect(std::fabs((ul[0] - ur[0]) * bufW - (pl[0] - pr[0])) < 1e-3f, "the plate apart between the eyes as much as the name: the same depth");
        const float behind[3] = {0.0f, 0.0f, 1.0f};
        float pb[2];
        Expect(!EyePixel(behind, R, U, B, proj, bufW, bufH, pb), "behind the eye: not drawn");
        float h[3] = {30.0f, 22.0f, 0.0f}, lift[3];
        StackLines(h, 2.0f, lift);
        Expect(lift[1] == 0.0f && lift[0] == 24.0f && lift[2] == 0.0f, "the lines over the head: the second line's bottom at the point, the name on top of it");
        float bg[4];
        NameBackdrop(0.0f, bg);
        Expect(bg[3] < 0.0f, "backdrop 0: no box (the game's bar shows round the letters)");
        NameBackdrop(0.45f, bg);
        Expect(std::fabs(bg[3] - 0.45f) < 1e-6f && bg[0] == 0.0f, "backdrop 0.45: a dark box at 0.45");
        Want nb;
        strcpy_s(nb.text, "Quill Rat");
        nb.heightPx = 30;
        nb.bold = true;
        NameBackdrop(1.0f, nb.bg);
        std::vector<uint32_t> px;
        int cw = 0, ch = 0;
        bool letters = false;
        if (Paint(nb, nullptr, &px, &cw, &ch))
            for (uint32_t v : px) letters = letters || ((v >> 24) == 255 && (v & 255) >= 250);
        Expect(letters && (px[(size_t)(ch / 2) * cw + 3] >> 24) > 240, "a solid backdrop: the box solid, the letters whole and bright on it");
    }
    {   // the game's own face (0.205.11: "the fonts are not the game's"): the style's first word is the font manager's id - Exocet 0, Formal 1
        Expect(FontOfStyle(0) == kFontExocet && FontOfStyle(1) == kFontFormal && FontOfStyle(2) == -1 && FontOfStyle(-7) == -1, "the style's font: Exocet, Formal, else not known");
        Want ex, fo;
        strcpy_s(ex.text, "Fallen");
        strcpy_s(fo.text, "Fallen");
        ex.font = kFontExocet;
        fo.font = kFontFormal;
        Expect(KeyOf(ex) != KeyOf(fo), "the same name in Exocet and in Formal: two cells");
    }
    {   // 0.205.12: "a box under the NPC's name - in the original the (A) icon": a hover line holding a private-use letter is the game's icon, left to it
        Expect(IconIn("Deckard Cain", 0x80) == 0, "a name: no icon");
        Expect(IconIn("\xEE\x81\xBE" "4Bishibosh", 0x80) == 0, "a colour code (U+E07E) is no icon: it is taken out first");
        Expect(IconIn("\xEE\x80\x81", 0x80) == 0xE001, "a private-use letter alone (a button sprite): an icon");
        Expect(IconIn("Talk \xEF\xA3\xBF", 0x80) == 0xF8FF, "one at the end of a line: an icon");
        Expect(IconIn("\xD0\x9A\xD0\xB0\xD0\xB8\xD0\xBD", 0x80) == 0, "a translated name (Cyrillic): no icon");
        Want icon;
        strcpy_s(icon.text, "\xEE\x80\x81");
        icon.heightPx = 30;
        std::vector<uint32_t> ipx;
        int iw = 0, ih = 0;
        Expect(!Paint(icon, nullptr, &ipx, &iw, &ih), "a letter the face has not (a game icon): not painted at all - never a box");
    }
    {   // 2026-11-06, Serg: "the A key on NPCs in our font too, green": a hover line's icon becomes our 'A', marked to be inked in the game's green
        char kt[kTextMax] = "\xEE\x80\x81";
        uint32_t keys = 0;
        Expect(IconsToKeys(kt, &keys) == 1 && std::strcmp(kt, "A") == 0 && keys == 1u, "the icon alone: 'A', the first letter marked");
        char t2[kTextMax] = "\xEE\x80\x81 Talk";
        keys = 0;
        Expect(IconsToKeys(t2, &keys) == 1 && std::strcmp(t2, "A Talk") == 0 && keys == 1u, "the icon then a word: the word kept, only the 'A' marked");
        char t3[kTextMax] = "\xD0\x9A\xD0\xB0 \xEF\xA3\xBF";
        keys = 0;
        Expect(IconsToKeys(t3, &keys) == 1 && std::strcmp(t3, "\xD0\x9A\xD0\xB0 A") == 0 && keys == (1u << 3), "after Cyrillic: its place counted in letters, not bytes");
        char t4[kTextMax] = "Deckard Cain";
        keys = 0;
        Expect(IconsToKeys(t4, &keys) == 0 && std::strcmp(t4, "Deckard Cain") == 0 && keys == 0, "no icon: the text as it was");
        float g[3] = {};
        Expect(CodeColour(kKeyCode, g) && g[0] == 0.0f && g[1] == 1.0f && g[2] == 0.0f, "the key's colour: the game's own green (code 2)");

        Want plain, key;
        strcpy_s(plain.text, "A Talk");
        strcpy_s(key.text, "A Talk");
        plain.heightPx = key.heightPx = 30;
        key.keys = 1u;
        Expect(KeyOf(plain) != KeyOf(key), "the line with a green key and without: two cells");
        std::vector<uint32_t> kp;
        int kw = 0, kh = 0;
        bool painted = Paint(key, nullptr, &kp, &kw, &kh);
        Expect(painted, "the line with the key painted");
        // green only in the left part (the 'A'), white letters to its right
        int greenL = 0, greenR = 0, whiteR = 0;
        for (int y = 0; painted && y < kh; ++y)
            for (int x = 0; x < kw; ++x) {
                const uint32_t p = kp[(size_t)y * kw + x];
                const int r = p & 0xFF, gg = (p >> 8) & 0xFF, b = (p >> 16) & 0xFF, a = p >> 24;
                if (a < 200) continue;
                const bool isGreen = gg > 150 && r < 90 && b < 90, isWhite = r > 150 && gg > 150 && b > 150;
                if (x < kw / 3) greenL += isGreen;
                else { greenR += isGreen; whiteR += isWhite; }
            }
        Expect(greenL > 10 && greenR == 0 && whiteR > 10, "the 'A' green, the word after it white");
    }
    {   // our own plate (0.205.13, Serg: "draw our own life bar, in the game's style"): the life the plate is told, the layout, the bar's fill
        Expect(LifeFrac(-1) < 0.0f && LifeFrac(0) == 0.0f && LifeFrac(37) == 0.37f && LifeFrac(100) == 1.0f && LifeFrac(250) == 1.0f,
               "the life as the bar's setter takes it: percent, 0..100 (none told: not known)");
        const PlateLayout p = LayOutPlate(30.0f, 120.0f, 22.0f, 2.0f);   // a name 30 px tall, 120 wide; a line under the plate 22 px
        Expect(p.frame[3] == -(22.0f + 2.0f) && p.frame[1] < p.frame[3] && std::fabs((p.frame[3] - p.frame[1]) - p.frameH) < 1e-4f,
               "the plate stands over the line under it (its bottom 24 px over the point)");
        Expect(std::fabs(p.frame[0] + p.frame[2]) < 1e-4f && p.frameW >= 120.0f + 30.0f * 0.6f, "the plate centred over the point, wider than the name");
        Expect(p.fill[0] > p.frame[0] && p.fill[2] < p.frame[2] && p.fill[1] > p.frame[1] && p.fill[3] < p.frame[3], "the bar inside the frame");
        const float nameMid = -(p.nameLift + 15.0f);   // the name's middle (its bottom p.nameLift over the point, 30 px tall)
        Expect(std::fabs(nameMid - 0.5f * (p.fill[1] + p.fill[3])) < 1e-3f, "the name's middle on the bar's middle, as the game's");
        Expect(std::fabs((p.fill[3] - p.fill[1]) / p.frameH - 58.0f / 72.0f) < 1e-4f, "the bar 58 of the frame's 72, as the game's sprites");
        Expect(FillWidth(p, 0.0f) == 0.0f && std::fabs(FillWidth(p, 0.5f) - 0.5f * (p.fill[2] - p.fill[0])) < 1e-4f && FillWidth(p, 3.0f) == p.fill[2] - p.fill[0],
               "the red as long as the life (none at 0, half at 50 %, all at 100 % and over)");
        const PlateLayout q = LayOutPlate(30.0f, 10.0f, 0.0f, 2.0f);
        Expect(q.frame[3] == 0.0f && std::fabs(q.frameW - q.frameH * 3.5f) < 1e-3f, "no line under it: its bottom on the point; a short name: the frame's least width");
    }
    {   // Serg's reference (2026-11-07, docs/notes/img/game_plate_reference.png): the game's letter spacing, its bar, its lines - from the game's data
        Expect(ExtraSpacing(200.0f, 150.0f, 11, 0, 8.0f, 30.0f) == 5, "letters spaced so the line is as wide as the game made it (50 px over 10 gaps)");
        Expect(ExtraSpacing(200.0f, 150.0f, 6, 4, 8.0f, 30.0f) == 2, "the game's padding spaces counted: 50 - 4 spaces of 8 = 18 px over 5 + 4 gaps");
        Expect(ExtraSpacing(140.0f, 150.0f, 11, 0, 8.0f, 30.0f) == 0 && ExtraSpacing(0.0f, 150.0f, 11, 0, 8.0f, 30.0f) == 0, "ours wider already, or no measure: as the face sets them");
        Expect(ExtraSpacing(900.0f, 150.0f, 11, 0, 8.0f, 30.0f) == 30, "never more than the most");
        GamePlate g;   // a name 26 px tall in the game's pixels, its bar 200 x 40, "Undead - Minion" 30 px under the name's middle, "Immune to Poison" 52
        g.nameBoxH = 26.0f; g.barW = 200.0f; g.barH = 40.0f;
        g.line[1] = g.line[2] = true; g.lineDy[1] = 30.0f; g.lineDy[2] = 52.0f;
        const float lh[3] = {39.0f, 30.0f, 30.0f};   // ours: the name 39 px (x1.5 the game's), the lines 30
        GameLayout gl;
        Expect(LayOutGamePlate(39.0f, g, lh, &gl), "the plate laid out from the game's own numbers");
        const PlateLayout& p = gl.plate;
        Expect(std::fabs(p.frameW - 300.0f) < 1e-3f && std::fabs(p.frameH - 60.0f) < 1e-3f, "the plate the game's Health rect, at our scale (200 x 40 -> 300 x 60)");
        Expect(std::fabs((p.fill[1] - p.frame[1]) - 60.0f * 7.0f / 72.0f) < 1e-3f && std::fabs((p.fill[0] - p.frame[0]) - 60.0f * 8.0f / 72.0f) < 1e-3f,
               "the red inside the frame sprite's own rims and caps");
        const float nameMid = -(gl.lineLift[0] + 19.5f);
        Expect(std::fabs(nameMid - 0.5f * (p.frame[1] + p.frame[3])) < 1e-3f, "the name centred on the plate, as the game");
        const float mid1 = -(gl.lineLift[1] + 15.0f), mid2 = -(gl.lineLift[2] + 15.0f);
        Expect(std::fabs((mid1 - nameMid) - 45.0f) < 1e-3f && std::fabs((mid2 - nameMid) - 78.0f) < 1e-3f, "the lines where the game stands them under the name, at our scale");
        Expect(std::fabs(gl.lineLift[2]) < 1e-3f && gl.lineLift[1] > gl.lineLift[2] && p.frame[3] < 0.0f, "the last line's bottom on the point, the rest above it");
        Expect(p.frame[0] < p.fill[0] && p.frame[2] > p.fill[2] && p.frame[1] < p.fill[1] && p.frame[3] > p.fill[3], "the red inside the frame");
        GamePlate noGame;
        Expect(!LayOutGamePlate(39.0f, noGame, lh, &gl), "no game numbers: not laid out this way (our own stack then)");
        // a name wider than the bar read (the last target's bar, our least size): the frame holds it, caps and all (sdfsdfsdfdscvx: "cut off")
        GamePlate wide = g;
        wide.nameW = 420.0f;
        Expect(LayOutGamePlate(39.0f, wide, lh, &gl) && gl.plate.frameW >= 420.0f + 2.0f * 60.0f * 8.0f / 72.0f - 1e-3f &&
                   gl.plate.fill[2] - gl.plate.fill[0] >= 420.0f - 1e-3f,
               "the frame never narrower than our name on it");
        wide.nameW = 100.0f;
        Expect(LayOutGamePlate(39.0f, wide, lh, &gl) && std::fabs(gl.plate.frameW - 300.0f) < 1e-3f, "a shorter name: the game's bar as it is");
        Expect(std::fabs(gl.plate.frameH - 60.0f) < 1e-3f, "the height stays the game's bar's");
    }
    {   // one copy of the plate: the game draws its lines twice (the top place and over the head) - the name in the plate's box, each line nearest it
        Text tt[kMax];
        const int32_t topName[4] = {793, 219, 207, 26}, topLine[4] = {852, 254, 89, 22}, headName[4] = {443, 1321, 169, 22}, headLine[4] = {470, 1348, 80, 20};
        auto put = [&](int i, const char* s, const int32_t* b, int which) { strcpy_s(tt[i].raw, s); std::copy_n(b, 4, tt[i].box); tt[i].which = which; tt[i].seq = (uint64_t)i + 1; };
        put(0, "Zombie", topName, 0); put(1, "Undead", topLine, 1); put(2, "Zombie", headName, 0); put(3, "Undead", headLine, 1);
        const float plateBox[4] = {399.0f, 1293.0f, 607.0f, 1421.0f};
        const int k = OneCopyOfEach(tt, 4, plateBox);
        Expect(k == 2 && tt[0].box[1] == 1321 && tt[1].box[1] == 1348, "the copy over the head: its name (in the plate's box) and the line beside it");
    }
    {   // 0.206.5, Serg: "now and then the old plates flash in the frame" - the game's plate hidden on EVERY frame our names are on, ours drawn or not
        struct Frame { const char* what; bool targetKnown, oursDrawn, oursMarked; };
        const Frame seq[] = {
            {"no target unit yet (the hook has none)", false, false, false},
            {"a new target's first frame (our cell not painted yet)", true, false, false},
            {"our plate drawn", true, true, true},
            {"the target lost for a frame (the hook missed it)", false, false, true},
            {"the first eye's pass (ours not marked yet this frame)", true, true, false},
            {"a plate the game shows before our hook saw its target", false, false, false},
        };
        bool everyHidden = true, everySkipped = true;
        for (const Frame& f : seq) {
            everyHidden = everyHidden && GamePlateHidden(true, true);
            for (int which = 0; which < 3; ++which) everySkipped = everySkipped && SkipGameText(true, which, f.oursMarked);
            (void)f.targetKnown; (void)f.oursDrawn;   // (none of these may let the game's plate through)
        }
        Expect(everyHidden, "names on, first person: the game's plate box never drawn, whatever the frame");
        Expect(everySkipped, "names on: every copy of the plate's told texts skipped, ours shown yet or not");
        Expect(!GamePlateHidden(false, true) && !GamePlateHidden(true, false), "names off, or not first person: the game's plate as before");
        Expect(!SkipGameText(false, 0, true), "names off: the game's text drawn");
        Expect(!SkipGameText(true, kWhichHover, false) && SkipGameText(true, kWhichHover, true), "a hover name: skipped only once ours is shown in its place");
    }
    {   // [hud] npc_names_own: a TextDraw is a hover name (an NPC's, an object's) when it returns into the tooltip drawer's three call sites
        const uintptr_t base = 0x140000000;
        const uintptr_t sites[3] = {base + kHoverSites[0], base + kHoverSites[1], base + kHoverSites[2]};
        Expect(HoverSiteOf(base + 0x8DAA0F, sites) == 2 && HoverSiteOf(base + 0x8DA8F6, sites) == 0, "the tooltip drawer's calls: hover names");
        Expect(HoverSiteOf(base + 0x86D6AE, sites) < 0 && HoverSiteOf(base + 0x1FA9DC, sites) < 0, "a text box's, an item label's: not hover names");
        const uintptr_t noSites[3] = {0, 0, 0};
        Expect(HoverSiteOf(0, noSites) < 0, "the sites not found (another build): nothing is a hover name");
        Record hov;
        const int32_t warriv[4] = {700, 600, 120, 30};
        hov.Add("Warriv", warriv, 11000, kWhichHover);
        Text h[kMax];
        Expect(hov.Take(11001, h) == 1 && h[0].which == kWhichHover && std::strcmp(h[0].raw, "Warriv") == 0, "a hover name recorded where the game drew it");
    }
    {   // 0.205.7, Serg's log: the plate's "Undead " also drawn near the top middle (852,254 89x22) besides the one under the name in the plate
        Record two;
        const int32_t top[4] = {852, 254, 89, 22}, under[4] = {980, 935, 89, 22}, zName[4] = {990, 905, 70, 26};
        two.Add("  Zombie  ", zName, 7000);
        two.Add("Undead ", top, 7000);
        two.Add("Undead ", under, 7001);
        Text u[kMax];
        const int k = two.Take(7002, u);
        int unders = 0, tops = 0;
        for (int i = 0; i < k; ++i) {
            unders += std::strcmp(u[i].raw, "Undead ") == 0 && u[i].box[1] == 935 ? 1 : 0;
            tops += std::strcmp(u[i].raw, "Undead ") == 0 && u[i].box[1] == 254 ? 1 : 0;
        }
        Expect(k == 3 && unders == 1 && tops == 1, "the same text in two places in one frame: both kept, apart", k);
        const float zPlate2[4] = {963.0f, 890.0f, 1111.0f, 967.0f};
        Expect(InPlate(under, zPlate2) && !InPlate(top, zPlate2), "the copy under the name is the plate's; the one at the top is not (left the game's)");
        const int32_t underMoved[4] = {990, 941, 89, 22};   // the next frame, moved with the head
        Expect(!SamePlace(under, top) && SamePlace(under, underMoved), "far apart: two places; moved a few pixels with the head: one");
    }
    Expect(rec.Take(1015 + kFreshMs + 5, t) == 0, "none drawn lately (no target): nothing");
    rec.Add("Fallen", name, 2000);   // another target
    n = rec.Take(2001, t);
    Expect(n == 1 && std::strcmp(t[0].raw, "Fallen") == 0, "a new target: only its own name, not the last one's", n);
    rec.Add("Fallen", name, 2010);
    Expect(rec.Take(2009, t) == 1, "a record a millisecond ahead of the reader's clock still counts");
    const float champ[3] = {0.43f, 0.43f, 1.0f};
    rec.Add("Champion Fallen", name, 3000, 0, champ);
    n = rec.Take(3001, t);
    Expect(n == 1 && t[0].which == 0 && t[0].rgbKnown && t[0].rgb[2] == 1.0f, "a record keeps which of the plate's texts it is and its colour");

    Shown shown;
    const uint64_t key = KeyOf("Fallen");
    Expect(!shown.Lately(key, 3000), "ours not shown: the game draws its text");
    shown.Mark(key, 3000);
    Expect(shown.Lately(key, 3100) && !shown.Lately(KeyOf("Fallen Shaman"), 3100), "ours shown: that text alone is skipped");
    Expect(!shown.Lately(key, 3000 + kShownMs + 1), "ours not shown lately (an older effect, no cell): the game's text is back");
    for (int i = 0; i < 6; ++i) shown.Mark(KeyOf(i % 2 ? "a" : "b") + (uint64_t)i * 2, 4000 + i);
    shown.Mark(key, 4010);
    Expect(shown.Lately(key, 4011), "marks beyond kMax: the oldest give way");

    const float cell[4] = {0.9f, 0.75f, 0.45f, 1.0f}, bar[4] = {0.5f, 0.0f, 0.0f, 0.1f};
    float o[4];
    Over(cell, bar, o);
    Expect(std::fabs(o[0] - 0.9f) < 1e-6f && o[3] == 0.0f, "a letter's whole pixel over the bar: the letter, nothing of the world through it");
    const float clear[4] = {0, 0, 0, 0};
    Over(clear, bar, o);
    Expect(o[0] == 0.5f && std::fabs(o[3] - 0.1f) < 1e-6f, "beside the letters: the game's bar as it was");
    const float half[4] = {0.1f, 0.1f, 0.1f, 0.5f};
    Over(half, bar, o);
    Expect(std::fabs(o[0] - 0.35f) < 1e-6f && std::fabs(o[3] - 0.05f) < 1e-6f, "a letter's edge: half over the bar");

    Want w;   // the plate's name is painted with no box of its own: the bar shows round the letters
    char code = StripCodes("\xFF" "c3Champion Fallen", 0x80, w.text, sizeof w.text);
    ownnames::NameColour(code, 0, false, w.rgb);
    w.bg[3] = -1.0f;
    w.heightPx = 30;
    std::vector<uint32_t> px;
    int cw = 0, ch = 0;
    Expect(Paint(w, nullptr, &px, &cw, &ch) && (px[(size_t)1 * cw + 1] >> 24) == 0 && (px[(size_t)(ch / 2) * cw + 2] >> 24) == 0,
           "the name painted with no box: the cell's corners and sides empty");
}

static uint32_t Px(const std::vector<uint32_t>& p, int w, int x, int y) { return p[(size_t)y * w + x]; }
static void TestPaint(const char* dump) {
    std::vector<uint32_t> px;
    int w = 0, h = 0;
    Want n;
    Expect(!Paint(n, nullptr, &px, &w, &h), "no name: nothing painted");

    strcpy_s(n.text, "Harlequin Crest");
    n.rgb[0] = 0.90f; n.rgb[1] = 0.75f; n.rgb[2] = 0.45f;   // the unique gold
    n.heightPx = 40;
    Expect(Paint(n, nullptr, &px, &w, &h) && h == 40 && w > 100 && (int)px.size() == w * h, "a name painted, 40 pixels tall", w);
    int gold = 0, dark = 0;
    for (uint32_t p : px) {
        const int r = p & 255, g = (p >> 8) & 255, b = (p >> 16) & 255, a = p >> 24;
        if (a > 200 && r > 150 && g > 120 && b < g && r > b + 50) ++gold;
        if (a > 120 && a < 200 && r < 10 && g < 10 && b < 10) ++dark;
    }
    Expect(gold > 60, "the letters in their gold", gold);
    Expect(dark > w * h / 4, "the game's box behind: dark at its 0.6", dark);
    const uint32_t mid = Px(px, w, 3, h / 2);
    Expect((mid >> 24) > 130 && (mid >> 24) < 175, "the box's left edge: the game's 0.6 (premultiplied, cover in alpha)", mid >> 24);
    if (dump) WriteBmp(std::string(dump) + "_gold.bmp", px, w, h);

    const int wLong = w;
    strcpy_s(n.text, "Gold");
    Paint(n, nullptr, &px, &w, &h);
    Expect(w < wLong, "a shorter name: a narrower cell", w);
    n.heightPx = 80;
    Paint(n, nullptr, &px, &w, &h);
    Expect(h == 80, "the size asked for", h);

    strcpy_s(n.text, "Ber Rune");
    n.heightPx = 40;
    n.lit = true;
    n.rgb[0] = 1.0f; n.rgb[1] = 0.6f; n.rgb[2] = 0.1f;
    Paint(n, nullptr, &px, &w, &h);
    const uint32_t edge = Px(px, w, w / 2, 0);
    Expect((edge & 255) > 150 && ((edge >> 16) & 255) < 80, "pointed: a rim in the name's colour at its edge", edge & 255);
    if (dump) WriteBmp(std::string(dump) + "_lit.bmp", px, w, h);
    n.inReach = false;
    Paint(n, nullptr, &px, &w, &h);
    const uint32_t red = Px(px, w, w / 2, 0);
    Expect((red & 255) > 150 && ((red >> 8) & 255) < 90, "pointed out of reach: the rim red", (red >> 8) & 255);

    Want cyr;
    strcpy_s(cyr.text, "\xD0\x9A\xD0\xBE\xD1\x80\xD0\xBE\xD1\x82\xD0\xBA\xD0\xB8\xD0\xB9 \xD0\xBC\xD0\xB5\xD1\x87");   // Cyrillic (a translated game)
    cyr.heightPx = 36;
    int bright = 0;
    if (Paint(cyr, nullptr, &px, &w, &h))
        for (uint32_t p : px) bright += (p & 255) > 200 ? 1 : 0;
    Expect(bright > 40, "a translated name's letters painted (UTF-8)", bright);
    cyr.bg[3] = -1.0f;   // the game's "no box"
    Paint(cyr, nullptr, &px, &w, &h);
    Expect(Px(px, w, 1, 1) == 0, "the game draws no box (alpha below 0): none here either");

    Want key;   // the NPC's hover line with its button (IconsToKeys): to look at
    strcpy_s(key.text, "\xEE\x80\x81 Talk");
    IconsToKeys(key.text, &key.keys);
    key.heightPx = 40;
    key.bold = true;
    if (Paint(key, nullptr, &px, &w, &h) && dump) WriteBmp(std::string(dump) + "_key.bmp", px, w, h);
}

struct UpLog { int calls = 0; bool ok = true; std::vector<uint32_t> px; };
static bool Upload(void* ctx, const uint32_t* px, int w, int h) {
    UpLog* u = (UpLog*)ctx;
    ++u->calls;
    if (u->ok) u->px.assign(px, px + (size_t)w * h);
    return u->ok;
}
static void TestWire(const char* dump) {
    Want ws[3];
    strcpy_s(ws[0].text, "Shako");
    strcpy_s(ws[1].text, "Ber Rune");
    ws[2] = ws[0];   // the same name twice on the ground: one cell
    for (Want& w : ws) w.heightPx = 32;
    Cell cells[3];
    uint32_t rev = 0;
    UpLog up;
    int got = Resolve(ws, 3, cells, &rev, &Upload, &up);
    Expect(got == 0 && cells[0].w == 0 && up.calls == 0, "first ask: nothing painted yet, nothing uploaded (the game's pixels this frame)");
    Expect(Drain(5000), "the worker paints what was asked");
    got = Resolve(ws, 3, cells, &rev, &Upload, &up);
    Expect(got == 3 && up.calls == 1 && rev != 0, "then every label has its cell, the atlas uploaded once before them", got);
    Expect(cells[0].x == cells[2].x && cells[0].y == cells[2].y && (cells[0].x != cells[1].x || cells[0].y != cells[1].y), "the same name: the same cell");
    bool painted = false;
    for (int y = 0; y < cells[1].h && !painted; ++y)
        for (int x = 0; x < cells[1].w && !painted; ++x) painted = (up.px[(size_t)(cells[1].y + y) * kAtlasW + cells[1].x + x] & 255) > 150;
    Expect(painted, "the uploaded atlas holds the name at its cell");
    Resolve(ws, 3, cells, &rev, &Upload, &up);
    Expect(up.calls == 1, "nothing new: no upload again");
    uint32_t fresh = 0;   // a texture made again (a reloaded effect): the atlas goes up again
    UpLog up2;
    up2.ok = false;
    got = Resolve(ws, 3, cells, &fresh, &Upload, &up2);
    Expect(got == 0 && cells[0].w == 0 && up2.calls == 1 && fresh == 0, "an upload refused (an older effect): no cell handed out");
    if (dump && !up.px.empty()) WriteBmp(std::string(dump) + "_atlas.bmp", up.px, kAtlasW, kAtlasH);
}

// The game's own font files (D2R VR Settings' game-art cache, d2r_vr_game_ui): with --fonts <that folder>, their families as GDI names them, taken in,
// GDI knowing each face, and a name painted in each (--dump: the pictures). 0.205.12: Exocet was asked for by its typographic family and never found.
// Our plate's pieces painted from the game's own sprites (gameuifiles::kPlateFiles in <dir>: menu_assets_test makes them from the game): the frame,
// normal and unique, and the red bar; their colours as the sprites have them; --dump: the pieces and a whole plate (frame, 60 % red, a name) as .bmp.
static void TestPlatePieces(const char* dir, const char* dump) {
    using namespace ownnames;
    if (!dir) { std::printf("  (no --plate <d2r_vr_game_ui with the plate's pieces>: the game's plate sprites not tried)\n"); return; }
    const std::string d(dir);
    gameui::SetDir(std::wstring(d.begin(), d.end()));
    Expect(gameui::PlateSprite(gameuifiles::kPlateNormal) && gameui::PlateSprite(gameuifiles::kPlateConsole), "the game's plate sprites read");
    Want fr, un, lf;
    fr.piece = kPieceFrameNormal; fr.heightPx = 48; fr.widthPx = 240;
    un.piece = kPieceFrameUnique; un.heightPx = 48; un.widthPx = 240;
    lf.piece = kPieceLife; lf.heightPx = 38; lf.widthPx = 64;
    std::vector<uint32_t> a, b, c;
    int aw = 0, ah = 0, bw = 0, bh = 0, cw = 0, ch = 0;
    Expect(Paint(fr, nullptr, &a, &aw, &ah) && aw == 240 && ah == 48, "the normal frame painted, as wide as asked");
    Expect(Paint(un, nullptr, &b, &bw, &bh) && Paint(lf, nullptr, &c, &cw, &ch), "the unique frame and the red bar painted");
    const uint32_t mid = a[(size_t)24 * aw + 120], top = a[(size_t)1 * aw + 120], utop = b[(size_t)1 * bw + 120], red = c[(size_t)19 * cw + 32];
    Expect((mid & 255) < 10 && (mid >> 24) > 240, "the frame's middle black and solid (the empty bar), as the game's sprite");
    Expect((top & 255) > 150 && ((top >> 8) & 255) > 150 && ((top >> 16) & 255) > 150, "the normal frame's rim light grey");
    Expect((utop & 255) > 200 && ((utop >> 16) & 255) < ((utop >> 8) & 255), "the unique frame's rim gold (red and green over blue)");
    Expect((red & 255) > 70 && ((red >> 8) & 255) < 20 && ((red >> 16) & 255) < 20, "the bar red, the game's");
    if (dump) {
        WriteBmp(std::string(dump) + "_frame_normal.bmp", a, aw, ah);
        WriteBmp(std::string(dump) + "_frame_unique.bmp", b, bw, bh);
        WriteBmp(std::string(dump) + "_life.bmp", c, cw, ch);
        // a whole plate as the shader lays it: the frame, the red to 60 %, the name over it (on black)
        const PlateLayout p = LayOutPlate(30.0f, 150.0f, 0.0f, 2.0f);
        const int W = (int)std::ceil(p.frameW), Hh = (int)std::ceil(p.frameH);
        std::vector<uint32_t> plate((size_t)W * Hh, 0xFF000000u);
        auto over = [&](int x, int y, uint32_t s) {
            const uint32_t dpx = plate[(size_t)y * W + x];
            const float ka = 1.0f - (s >> 24) / 255.0f;
            uint32_t o = 0xFF000000u;
            for (int k = 0; k < 3; ++k) o |= (uint32_t)std::min(255.0f, ((s >> (8 * k)) & 255) + ((dpx >> (8 * k)) & 255) * ka) << (8 * k);
            plate[(size_t)y * W + x] = o;
        };
        for (int y = 0; y < Hh; ++y)
            for (int x = 0; x < W; ++x) {
                over(x, y, a[(size_t)std::min(ah - 1, y * ah / Hh) * aw + std::min(aw - 1, x * aw / W)]);
                const float fx = x + p.frame[0], fy = y + p.frame[1];
                if (fx >= p.fill[0] && fx < p.fill[0] + FillWidth(p, 0.6f) && fy >= p.fill[1] && fy < p.fill[3])
                    over(x, y, c[(size_t)std::min(ch - 1, (int)((fy - p.fill[1]) / (p.fill[3] - p.fill[1]) * ch)) * cw + cw / 2]);
            }
        Want nm;
        strcpy_s(nm.text, "Bishibosh");
        nm.heightPx = 30; nm.bold = true; nm.bg[3] = -1.0f; nm.rgb[0] = 199 / 255.0f; nm.rgb[1] = 179 / 255.0f; nm.rgb[2] = 119 / 255.0f;
        std::vector<uint32_t> n;
        int nw = 0, nh = 0;
        if (Paint(nm, nullptr, &n, &nw, &nh)) {
            const int x0 = (W - nw) / 2, y0 = (int)(0.5f * (p.fill[1] + p.fill[3]) - p.frame[1] - nh / 2.0f);
            for (int y = 0; y < nh; ++y)
                for (int x = 0; x < nw; ++x)
                    if (x0 + x >= 0 && x0 + x < W && y0 + y >= 0 && y0 + y < Hh) over(x0 + x, y0 + y, n[(size_t)y * nw + x]);
        }
        WriteBmp(std::string(dump) + "_plate.bmp", plate, W, Hh);

        // Serg's reference plate ("DEFILED WARRIOR" / "Undead - Minion" / "Immune to Poison"), laid out as ours is - the game's numbers read off that
        // picture (its pixels): the name's line 24 tall and 250 wide as measured, the bar 345 x 38, the lines 45 and 67 under the name's middle
        GamePlate g;
        g.nameBoxH = 24.0f; g.barW = 345.0f; g.barH = 38.0f; g.line[1] = g.line[2] = true; g.lineDy[1] = 45.0f; g.lineDy[2] = 67.0f;
        const float lh[3] = {24.0f, 22.0f, 22.0f};
        GameLayout gl;
        if (LayOutGamePlate(24.0f, g, lh, &gl)) {
            const PlateLayout& q = gl.plate;
            const float top = std::min(q.frame[1], -(gl.lineLift[0] + lh[0])) - 4.0f;
            const int RW = (int)std::ceil(q.frameW) + 40, RH = (int)std::ceil(-top) + 4;
            const float ox = RW * 0.5f, oy = (float)RH - 2.0f;   // the point: the bottom middle
            std::vector<uint32_t> ref((size_t)RW * RH, 0xFF000000u);
            auto put = [&](int x, int y, uint32_t s) {
                if (x < 0 || y < 0 || x >= RW || y >= RH) return;
                const uint32_t dpx = ref[(size_t)y * RW + x];
                const float ka = 1.0f - (s >> 24) / 255.0f;
                uint32_t o = 0xFF000000u;
                for (int k = 0; k < 3; ++k) o |= (uint32_t)std::min(255.0f, ((s >> (8 * k)) & 255) + ((dpx >> (8 * k)) & 255) * ka) << (8 * k);
                ref[(size_t)y * RW + x] = o;
            };
            Want fw;
            fw.piece = kPieceFrameNormal; fw.heightPx = StepPx(q.frameH); fw.widthPx = (int)std::lround(q.frameW * fw.heightPx / q.frameH);
            std::vector<uint32_t> fpx, lpx;
            int fwW = 0, fwH = 0, lwW = 0, lwH = 0;
            Want lw;
            lw.piece = kPieceLife; lw.heightPx = StepPx(q.fill[3] - q.fill[1]); lw.widthPx = 64;
            if (Paint(fw, nullptr, &fpx, &fwW, &fwH) && Paint(lw, nullptr, &lpx, &lwW, &lwH)) {
                for (int y = (int)(q.frame[1] + oy); y < (int)(q.frame[3] + oy); ++y)
                    for (int x = (int)(q.frame[0] + ox); x < (int)(q.frame[2] + ox); ++x) {
                        const float u = (x - (q.frame[0] + ox)) / q.frameW, v = (y - (q.frame[1] + oy)) / q.frameH;
                        put(x, y, fpx[(size_t)std::min(fwH - 1, (int)(v * fwH)) * fwW + std::min(fwW - 1, (int)(u * fwW))]);
                        const float fx = x - ox, fy = y - oy;
                        if (fx >= q.fill[0] && fx < q.fill[2] && fy >= q.fill[1] && fy < q.fill[3])
                            put(x, y, lpx[(size_t)std::min(lwH - 1, (int)((fy - q.fill[1]) / (q.fill[3] - q.fill[1]) * lwH)) * lwW + lwW / 2]);
                    }
            }
            const char* text[3] = {"  Defiled Warrior  ", "Undead \xE2\x80\xA2 Minion", "\xEE\x81\xBE" "2Immune to Poison"};
            const float wph[3] = {250.0f / 24.0f, 0.0f, 0.0f};
            for (int i = 0; i < 3; ++i) {
                Want w;
                const char code = StripCodes(text[i], 0x80, w.text, sizeof w.text);
                w.heightPx = (int)lh[i]; w.bold = i == 0; w.bg[3] = -1.0f; w.font = i == 0 ? kFontExocet : kFontFormal;
                w.gameWPerH = wph[i]; w.padSpaces = i == 0 ? 4 : 0;
                if (!CodeColour(code, w.rgb)) w.rgb[0] = w.rgb[1] = w.rgb[2] = 240.0f / 255.0f;
                std::vector<uint32_t> tp;
                int tw = 0, th = 0;
                const wchar_t* face = i == 0 ? gameui::TitleFaceAlone() : gameui::TextFaceAlone();
                if (!Paint(w, face, &tp, &tw, &th)) continue;
                const int x0 = (int)(ox - tw / 2.0f), y0 = (int)(oy - gl.lineLift[i] - th);
                for (int y = 0; y < th; ++y)
                    for (int x = 0; x < tw; ++x) put(x0 + x, y0 + y, tp[(size_t)y * tw + x]);
            }
            WriteBmp(std::string(dump) + "_reference_plate.bmp", ref, RW, RH);
        }
    }
}

static void TestGameFonts(const char* dir, const char* dump) {
    if (!dir) { std::printf("  (no --fonts <d2r_vr_game_ui>: the game's font files not tried)\n"); return; }
    const std::string base(dir);
    const char* files[2] = {"ExocetBlizzardOT-Medium.otf", "Formal436BT.ttf"};
    const wchar_t* want[2] = {L"ExocetBlizzardMixedCapsOTMedium", L"Formal436 BT"};
    for (int i = 0; i < 2; ++i) {
        const std::string path = base + "\\" + files[i];
        FILE* f = nullptr;
        std::string bytes;
        if (fopen_s(&f, path.c_str(), "rb") == 0 && f) {
            char buf[4096];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.append(buf, n);
            std::fclose(f);
        }
        const std::wstring fam = gameui::FamilyInFontFile(bytes);
        Expect(fam == want[i], "the font file's family as GDI names it (name 1)");
        std::printf("  %s: family \"%ls\"\n", files[i], fam.c_str());
        const std::wstring wpath(path.begin(), path.end());
        Expect(AddFontResourceExW(wpath.c_str(), FR_PRIVATE, nullptr) > 0, "taken in");
        HDC dc = CreateCompatibleDC(nullptr);
        HFONT font = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, fam.c_str());
        HGDIOBJ old = SelectObject(dc, font);
        wchar_t got[LF_FACESIZE] = L"";
        GetTextFaceW(dc, LF_FACESIZE, got);
        SelectObject(dc, old);
        DeleteObject(font);
        DeleteDC(dc);
        Expect(_wcsicmp(got, fam.c_str()) == 0, "GDI draws in that face itself, not a stand-in");
        std::printf("  GDI's face for it: \"%ls\"\n", got);
        Want w;
        strcpy_s(w.text, "Fallen Shaman");
        w.heightPx = 48;
        w.bold = true;
        w.font = i == 0 ? kFontExocet : kFontFormal;
        ownnames::NameBackdrop(0.6f, w.bg);
        std::vector<uint32_t> px;
        int cw = 0, ch = 0;
        Expect(Paint(w, fam.c_str(), &px, &cw, &ch), "a name painted in the game's face");
        if (dump) WriteBmp(std::string(dump) + (i == 0 ? "_exocet.bmp" : "_formal.bmp"), px, cw, ch);
    }
}

int main(int argc, char** argv) {
    const char* dump = nullptr;
    const char* fonts = nullptr;
    const char* plate = nullptr;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--plate") == 0) plate = argv[i + 1];
        if (std::strcmp(argv[i], "--dump") == 0) dump = argv[i + 1];
        if (std::strcmp(argv[i], "--fonts") == 0) fonts = argv[i + 1];
    }
    TestGameFonts(fonts, dump);
    TestPlatePieces(plate, dump);
    TestNames();
    TestSizes();
    TestKeys();
    TestAtlas();
    TestPlace();
    TestSliders();
    TestMonsterNames();
    TestPaint(dump);
    TestWire(dump);
    std::printf("own_labels_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
