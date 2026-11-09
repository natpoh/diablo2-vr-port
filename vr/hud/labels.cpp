// vr/hud/labels.cpp - cut from vr/vrcam.cpp (docs/plan_modules.md M9); what it gives: vr/hud/hud_vr.h. It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"
#include "../own_plate_why.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// -- Item labels on the ground, through the game's own code (F3 floor) --------
// Recon on the D2RLoader-layout snapshot: docs/labels_recon.md. A label is a
// record of the game's ground-label list: +0x00 its box (x, y, w, h, ints),
// +0x14 the box's colour (float RGBA, the game's own {0, 0, 0, 0.6}), +0x24
// the name, +0xA4 the name's text style.
//   LabelLayout 0x1FA9F0 (unit, name, box out, min x, max x, flag) -> shown:
//     measures the name (TextMeasure 0x909560 at the scale UiScale 0x8460F0
//     returns), puts the box round it with the padding (the profile's
//     inGameTargetPadding times that scale), centred over the item, its
//     bottom at the item.
//   LabelPaint 0x1FA8E0 (box, name, &colour): fills the box (0x657B90 reads
//     the colour there and then), then draws the name in it with TextDraw
//     0x902E20 (name, box, style, scale = UiScale again).
// [hud_floor] labels_alpha: LabelPaint gets a copy of the colour, its alpha
// that much lower - the box fades, the name stays as it is.
// [hud_floor] labels_size: the box made again round the name measured at the
// other scale, about the same bottom middle, and the name drawn at that
// scale - inside LabelPaint only (t_labelScale), every other text untouched.
// Only on the floor; the hooks go in the first time the floor wants them.
constexpr uint64_t RVA_LABEL_LAYOUT = 0x1FA9F0, RVA_LABEL_PAINT = 0x1FA8E0, RVA_TEXT_DRAW = 0x902E20;
// push rbp, rbx, rsi, r12, r15; mov rbp, rsp; sub rsp, 0x50; mov r12d, r9d; mov byte [rdx], 0; mov rsi, r8
const uint8_t kSigLabelLayout[24] = {0x40,0x55,0x53,0x56,0x41,0x54,0x41,0x57,0x48,0x8B,0xEC,0x48,0x83,0xEC,0x50,0x45,0x8B,0xE1,0xC6,0x02,0x00,0x49,0x8B,0xF0};
// mov [rsp+0x10], rbx; mov [rsp+0x18], rsi; push rdi; sub rsp, 0x40; mov rbx, r8 (the colour)
const uint8_t kSigLabelPaint[18] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x40,0x49,0x8B,0xD8};
// mov [rsp+8], rbx; mov [rsp+0x10], rsi; push rdi; sub rsp, 0x70; movaps [rsp+0x60], xmm6 (the scale is kept in xmm6)
const uint8_t kSigTextDraw[20] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x70,0x0F,0x29,0x74,0x24,0x60};
// called, never hooked: push rbx, rbp, rsi, rdi; mov eax, 0x26A8 (its frame, then __chkstk)
const uint8_t kSigTextMeasure[10] = {0x40,0x53,0x55,0x56,0x57,0xB8,0xA8,0x26,0x00,0x00};
// push rbx; sub rsp, 0x20; mov rbx, [rip + 0x2BFA073] (the interface's scale object)
const uint8_t kSigUiScale[13] = {0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x1D,0x73,0xA0,0xBF,0x02};
// mov rax, [rip + 0x2BF9F59]; test rax, rax; je +8; movzx eax, byte [rax + 0xB9]; ret - the HD graphics on
const uint8_t kSigIsHd[20] = {0x48,0x8B,0x05,0x59,0x9F,0xBF,0x02,0x48,0x85,0xC0,0x74,0x08,0x0F,0xB6,0x80,0xB9,0x00,0x00,0x00,0xC3};
using LabelLayoutFn = bool (*)(void* unit, char* name, int32_t* box, int32_t minX, int32_t maxX, uint8_t flag);
using LabelPaintFn = uintptr_t (*)(const int32_t* box, const char* name, const float* colour);
using TextDrawFn = uintptr_t (*)(const char* text, const int32_t* box, const void* style, float scale);
using TextMeasureFn = uintptr_t (*)(const char* text, const void* style, int32_t* size, float scale, const int32_t* most);
using IsHdFn = bool (*)();
LabelLayoutFn OrigLabelLayout;
LabelPaintFn OrigLabelPaint;
TextDrawFn OrigTextDraw;
thread_local float t_labelScale = 0.0f;   // inside LabelPaint: the name drawn at its scale times this (0 = not inside)

// [hud_floor] labels_size now, 1 = the game's own.
float LabelSizeNow() {
    float alpha, size;
    LabelLookNow(&alpha, &size);
    return g_set.labelsNativeFloor.load() && g_labelSizeIn.load() ? size : 1.0f;
}

// [hud] labels_far, first person (F4): nearer labels larger, farther ones smaller, as signs in the room
// (2026-10-12) - labels_size at 1.5 m and nearer, labels_far of it at 10 m and farther, a straight line
// between; the distance from the hero's feet to the item's own place on the ground (its static path).
bool LabelsByDistance() { return FullBody() && g_enabled.load() && g_set.labelsFarBody.load() < 0.995f; }
// An item's distance on the ground from the hero's feet, world units.
bool LabelItemDistance(void* unit, float* out) {
    uintptr_t path = 0;
    uint32_t sx = 0, sy = 0;
    float hero[3];
    if (!unit || !SafeRead(&path, (const uint8_t*)unit + 0x38, sizeof path) || path < 0x10000 ||
        !SafeRead(&sx, (const void*)(path + 0x10), 4) || !SafeRead(&sy, (const void*)(path + 0x14), 4) || !LookAtNow(hero))
        return false;
    const float dx = 2.0f * sx + 1.0f - hero[0], dz = 2.0f * sy + 1.0f - hero[2];
    *out = sqrtf(dx * dx + dz * dz);
    return std::isfinite(*out);
}
// [hud] labels_own_*: our labels' own size by distance (ownlabels::CurveK).
ownlabels::SizeCurve OwnSizeCurve() {
    ownlabels::SizeCurve c;
    c.nearPct = g_set.ownSizeNear.load();
    c.farPct = g_set.ownSizeFar.load();
    c.nearM = g_set.ownNearM.load();
    c.farM = g_set.ownFarM.load();
    return c;
}
// The item's ground distance in metres; false when it cannot be read.
bool LabelItemMetres(void* unit, float* m) {
    float d;
    if (!LabelItemDistance(unit, &d)) return false;
    *m = d / (std::max(1.0f, ViewHeight()) / UserEyeHeightM());   // world units per metre, as TrueScale
    return std::isfinite(*m);
}
// [hud] labels_own: our labels' size for this item - our curve, in place of labels_size x labels_far (the game's labels keep those).
float OwnLabelK(void* unit) {
    float m;
    return LabelItemMetres(unit, &m) ? ownlabels::CurveK(m, OwnSizeCurve()) : ownlabels::CurveK(0.0f, OwnSizeCurve());
}
float LabelDistanceK(void* unit) {
    float d;
    if (!LabelsByDistance() || !LabelItemDistance(unit, &d)) return 1.0f;
    const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();   // world units per metre, as TrueScale
    const float m = d / upm;
    return platelook::DistanceK(m, g_set.labelsFarBody.load());   // (the rule the monster plate has too: monster_far)
}
// Each label's own scale from its layout to its paint (the name's buffer and the box are the label's own;
// both run on the game's interface thread), the newest first.
struct LabelScale { const void* name; const void* box; float k; };
LabelScale g_labelScale[128];
uint32_t g_labelScaleNext = 0;
void NoteLabelScale(const void* name, const void* box, float k) { g_labelScale[g_labelScaleNext++ % 128] = {name, box, k}; }
// This label was laid out for a unit that is not an item (k -1): its paint is not the labels' settings' either.
bool LabelIsOtherThanItem(const void* name, const void* box) {
    for (uint32_t i = 1; i <= 128; ++i) {
        const LabelScale& e = g_labelScale[(g_labelScaleNext - i) % 128];
        if (e.name == name || e.box == box) return e.k < 0.0f;   // the newest record of this label says
    }
    return false;
}
float LabelScaleOf(const void* name, const void* box, float fallback) {
    for (uint32_t i = 1; i <= 128; ++i) {
        const LabelScale& e = g_labelScale[(g_labelScaleNext - i) % 128];
        if (e.k > 0.0f && (e.name == name || e.box == box)) return e.k;
    }
    return fallback;
}

// [hud] labels_depth_body, F4 in stereo: each item label at its item's depth - this frame's labels, their boxes
// in the interface's pixels and their items' distances, for the effect (LabelDepthUniforms).
// ... and eyes by turns: each label's own depth is what keeps it the same in each eye every frame (D2R_DepthFog.fx LabelTurnH) - read then too, the
// labels' look as labels_depth_body sets it (0: where the game leaves them)
// [hud] labels_own (vr/own_labels.h, docs/notes/custom_labels.md): in F4 the item labels drawn by us - the name painted sharp at the size it has in
// the eye, in the game's own box (its layout, its hover and its pick-up untouched), at its item's depth. The effect needs each label's box for it.
bool OwnLabelsNow() { return g_set.labelsOwn.load() && g_enabled.load() && FullBody() && viewgate::F4(); }
// [hud] own_labels_drop_game: with our labels / names on, the game's own label, plate and hover layer not drawn at all (3c0ee102: its plate's Draw,
// LabelPaint, its tooltip drawer skipped). Its own tick since 0.206.8's device-removed crash (DXGI_ERROR_INVALID_CALL): 0.206.6-0.206.8, the builds
// with it, lost the device 2-3 minutes into every run with plates shown; 0.206.5, without it, ran clean. Unticked: none of those hooks is put in -
// the game's layer drawn and hidden in our effect (0.206.5's way).
bool DropGameLayer() { return g_set.ownDropGame.load(std::memory_order_relaxed); }
bool LabelDepthWanted() {
    return AfrOn() && g_enabled.load() && FullBody() && (g_set.labelsDepthBody.load() > 0.005f || replay::OverlaysWanted() || OwnLabelsNow());
}
SRWLOCK g_labelDepthLock = SRWLOCK_INIT;
LabelDepth g_labelDepthNew[kLabelDepthMax];
int g_labelDepthNewN = 0;
// What our cell of this label shows: the name without the game's colour codes, in the code's colour or the item's rarity (item data +0x10:
// quality +0x00, flags +0x18 - as point_pickup reads it), the pointed item's look. The game's box colour comes later, from the paint (NoteOwnBox).
void OwnWantOf(void* unit, const char* name, bool lit, ownlabels::Want* w) {
    const char code = ownlabels::StripCodes(name, 0x80, w->text, sizeof w->text);
    float rarity[3] = {1.0f, 1.0f, 1.0f};
    uintptr_t data = 0;
    uint32_t quality = 0, flags = 0;
    if (SafeRead(&data, (const uint8_t*)unit + 0x10, sizeof data) && data >= 0x10000 && SafeRead(&quality, (const void*)data, sizeof quality) &&
        SafeRead(&flags, (const void*)(data + 0x18), sizeof flags))
        unleashed::lootaim::RarityColor(quality, flags, rarity);
    ownlabels::NameColour(code, rarity, w->rgb);
    w->lit = lit;
    w->inReach = !lit || pointpickup::LitInReach();
}
// sizeK: the label's labels_size x labels_far (x point_pickup_label_pct when pointed); resized: the layout hook made the game's box that size.
void NoteLabelDepth(void* unit, const int32_t* box, const char* name, bool lit, float sizeK, bool resized) {
    float d;
    if (!LabelDepthWanted() || !LabelItemDistance(unit, &d)) return;
    LabelDepth l{box[0], box[1], box[2], box[3], d};
    if (name && OwnLabelsNow()) {
        OwnWantOf(unit, name, lit, &l.want);
        int32_t first = -1;   // the label's style (its name's buffer + 0x80, as the game passes it): its font, ours in the same face
        if (SafeRead(&first, name + 0x80, sizeof first)) l.want.font = ownlabels::FontOfStyle(first);
        l.own = l.want.text[0] != 0;
        l.sizeK = sizeK;
        l.ownK = resized ? 1.0f : sizeK;   // (the box not made again: our cell takes the sliders' size itself, ShownRect)
    }
    AcquireSRWLockExclusive(&g_labelDepthLock);
    if (g_labelDepthNewN < kLabelDepthMax) g_labelDepthNew[g_labelDepthNewN++] = l;
    ReleaseSRWLockExclusive(&g_labelDepthLock);
}
// The paint of a label we draw: the colour the game gives its box (its own {0, 0, 0, 0.6}, or another if it marks the hovered one) to our cell,
// its alpha by [hud] labels_alpha as the game's own paint gets it (HookLabelPaint; ownlabels::BackdropRgba).
void NoteOwnBox(const int32_t* box, const float* colour) {
    float alpha, size, bg[4];
    LabelLookNow(&alpha, &size);
    ownlabels::BackdropRgba(colour, alpha, bg);
    AcquireSRWLockExclusive(&g_labelDepthLock);
    for (int i = g_labelDepthNewN - 1; i >= 0; --i) {
        LabelDepth& l = g_labelDepthNew[i];
        if (l.own && l.x == box[0] && l.y == box[1] && l.w == box[2] && l.h == box[3]) {
            memcpy(l.want.bg, bg, sizeof l.want.bg);
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_labelDepthLock);
}

bool ResizeLabelBox(char* name, int32_t* box, float k);
std::atomic<float> g_uiScaleSeen{0.0f};   // the game's interface scale, as the label code last asked it
// The game's interface scale (pixels a layout unit) asked now, for hud.cpp's corner map (vr/minimap_box.h: the interface area is 2160 of
// them tall, whatever the window, the render size or Safe Screen Percent): 0 when its bytes are not 3.3.93787's or the call faults.
float GameUiScaleNow() noexcept {
    if (!Matches(RVA_UI_SCALE, kSigUiScale, sizeof kSigUiScale)) return 0.0f;
    __try {
        const float s = ((UiScaleFn)d2rsig::Addr(RVA_UI_SCALE))();
        return s > 0.05f && s < 20.0f ? s : 0.0f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0.0f;
    }
}
// The label code is the ITEMS' (platelook::IsLabelOfItem): a unit that is read and is not an item is none of the labels' settings - its label is laid out and
// painted as the game does, and the paint is told so (k -1).
bool LabelUnitIsItem(void* unit) {
    return platelook::IsLabelOfItem(unit, [](const unsigned char* at, unsigned& type) { return SafeRead(&type, at, sizeof type); });
}
// [input] point_pickup (vr/unleashed_loot_aim.h, MPL 2.0 from D2R VR Unleashed's loot pick): the item the pointing hand's ray is on
// (pointpickup::Lit) gets its name always shown, a rim round its label in its rarity colour (red out of reach), a dark shade of that colour
// behind it and the name larger; while Show items is held for it (pointpickup::OnlyLit) every other name is cut. Which unit a paint is:
// the label owners, kept from the layouts (lootaim::LabelOwners has the rule and why). The game's UI thread, both hooks.
bool PointHighlightWanted() { return g_set.pointPickup.load() && viewgate::F4(); }
unleashed::lootaim::LabelOwners<64> g_labelOwners;
SRWLOCK g_labelOwnersLock = SRWLOCK_INIT;
uintptr_t LabelOwner(const void* box, const void* name) {
    AcquireSRWLockShared(&g_labelOwnersLock);
    const uintptr_t unit = g_labelOwners.Unit(box, name);
    ReleaseSRWLockShared(&g_labelOwnersLock);
    return unit;
}
float LabelScaleOf(const void* name, const void* box, float fallback);
uintptr_t PaintPointed(const int32_t* box, const char* name, const float* colour) {
    namespace la = unleashed::lootaim;
    const la::NameLook& look = la::LookFor(pointpickup::LitInReach());
    float rarity[3], rim[4], c[4];
    pointpickup::LitRarity(rarity);
    la::RimRgba(look, rarity, GetTickCount64(), rim);
    int32_t ring[4][4];
    const int strips = la::Ring(box, la::RimPx, ring);
    for (int i = 0; i < strips; ++i) OrigLabelPaint(ring[i], "", rim);
    la::BoxRgba(look, colour, rarity, c);
    const float keep = t_labelScale;
    t_labelScale = LabelScaleOf(name, box, LabelSizeNow());   // (the layout made room for the larger name: HookLabelLayout)
    const uintptr_t r = OrigLabelPaint(box, name, c);
    t_labelScale = keep;
    return r;
}

bool HookLabelLayout(void* unit, char* name, int32_t* box, int32_t minX, int32_t maxX, uint8_t flag) {
    bool shown = OrigLabelLayout(unit, name, box, minX, maxX, flag);
    const bool item = LabelUnitIsItem(unit);
    const uintptr_t lit = item && PointHighlightWanted() ? pointpickup::Lit() : 0;
    if (lit && (uintptr_t)unit == lit) {
        pointpickup::NoteLaidOut();
        if (!shown && box && name && name[0]) shown = true;   // the pointed item always shows its name: the game laid it out but hid it (at the hero's feet)
    } else if (shown && item && pointpickup::OnlyLit()) {
        return false;   // Show items is held for the pointed item alone: every other name cut
    }
    if (shown && box && name && item && PointHighlightWanted()) {
        AcquireSRWLockExclusive(&g_labelOwnersLock);
        g_labelOwners.Remember(box, name, (uintptr_t)unit);
        ReleaseSRWLockExclusive(&g_labelOwnersLock);
    }
    float k = item ? (OwnLabelsNow() ? OwnLabelK(unit) : LabelSizeNow() * LabelDistanceK(unit)) : 1.0f;   // (labels_own: our curve, the game's box made that size too)
    if (lit && (uintptr_t)unit == lit) {   // [input] point_pickup_label_pct (Serg, 0.199.1: "bigger"): its size, live - in place of Unleashed's 180% (130% out of reach)
        namespace la = unleashed::lootaim;
        const float pct = g_set.pointPickupLabelPct.load() * 0.01f * (pointpickup::LitInReach() ? 1.0f : la::OutOfReachLook.nameScale / la::InReachLook.nameScale);
        k = std::min(4.0f, k * pct);
    }
    if (shown && name) NoteLabelScale(name, box, item ? k : -1.0f);
    const bool resized = shown && box && name && item && (std::abs(k - 1.0f) < 0.005f || ResizeLabelBox(name, box, k));
    if (shown && box && item) NoteLabelDepth(unit, box, name, lit && (uintptr_t)unit == lit, k, resized);
    return shown;
}
// The box made again round the name at k times its scale, about the same bottom middle. False: left as it was (the old graphics, a
// measure not trusted) - [hud] labels_own then sizes its cell itself.
bool ResizeLabelBox(char* name, int32_t* box, float k) {
    if (!((IsHdFn)d2rsig::Addr(RVA_IS_HD))()) return false;   // the old graphics: their text has no scale
    const float s = ((UiScaleFn)d2rsig::Addr(RVA_UI_SCALE))();
    if (!(s > 0.05f && s < 20.0f)) return false;
    g_uiScaleSeen.store(s);
    const int32_t most[2] = {0x7FFFFFFF, 0x7FFFFFFF};   // as the layout asks: one line, no limit
    int32_t was[2] = {}, now[2] = {};
    const TextMeasureFn measure = (TextMeasureFn)d2rsig::Addr(RVA_TEXT_MEASURE);
    measure(name, name + 0x80, was, s, most);
    measure(name, name + 0x80, now, s * k, most);
    // the padding the game put round the name, scaled with it; anything else is not the box expected
    const int32_t padW = box[2] - was[0], padH = box[3] - was[1];
    if (was[0] <= 0 || was[1] <= 0 || now[0] <= 0 || now[1] <= 0 || padW < 0 || padH < 0 || padW > 400 || padH > 200)
        return false;
    const int32_t w = now[0] + (int32_t)std::ceil((float)padW * k), h = now[1] + (int32_t)std::ceil((float)padH * k);
    const int32_t mid = box[0] + box[2] / 2, bottom = box[1] + box[3];
    box[0] = mid - w / 2; box[1] = bottom - h; box[2] = w; box[3] = h;
    return true;
}

uintptr_t HookLabelPaint(const int32_t* box, const char* name, const float* colour) {
    // [hud] labels_own: we draw this label - its box colour noted for our cell; the pointed item's rim is ours too (the game's strips round the
    // box would stand in the layer at the wrong depth), the game paints the plain label
    const bool own = box && OwnLabelsNow() && !LabelIsOtherThanItem(name, box);
    if (own && colour) NoteOwnBox(box, colour);
    if (own && DropGameLayer()) return 0;   // (Serg, 2026-11-07: the old labels' layer removed - the game's box and name not painted at all; its layout, picking and hover stay; the callers take no result)
    if (box && PointHighlightWanted() && !own)   // [input] point_pickup: the pointed item's label
        if (const uintptr_t lit = pointpickup::Lit(); lit && LabelOwner(box, name) == lit) return PaintPointed(box, name, colour);
    float alpha, size;
    LabelLookNow(&alpha, &size);
    if (LabelIsOtherThanItem(name, box)) return OrigLabelPaint(box, name, colour);   // not an item's label: the game's own
    if (!colour || !g_set.labelsNativeFloor.load() || (alpha >= 0.995f && std::abs(size - 1.0f) < 0.005f && !LabelsByDistance()))
        return OrigLabelPaint(box, name, colour);
    float c[4];
    memcpy(c, colour, sizeof c);
    if (c[3] > 0.0f) c[3] *= alpha;   // below 0 the game skips the fill: left so
    const float keep = t_labelScale;
    t_labelScale = LabelScaleOf(name, box, LabelSizeNow());
    const uintptr_t r = OrigLabelPaint(box, name, c);
    t_labelScale = keep;
    return r;
}

// [hud] monster_names_own (vr/own_names.h): the texts of the plate over the target. The plate's SetInfo handler (hooked read-only below) says what they are;
// the effect says whether they are wanted and where the plate stands (NamesSetPlate); the text hook records each of the plate's texts as it is drawn (the
// game's interface thread) and skips the game's drawing of one only while the effect has shown our cell for it lately (NamesMark) - so a name not painted
// yet, an older effect, a plate the effect does not draw: the game's text stays.
std::atomic<bool> g_namesOn{false};
std::atomic<float> g_namesPlate[4] = {};
std::atomic<double> g_namesPlateMs{0.0};
SRWLOCK g_namesLock = SRWLOCK_INIT;
ownnames::Record g_namesRecord;
ownnames::Shown g_namesShown;
ownnames::PlateInfo g_namesInfo;   // (g_namesLock)
double NamesNowMs() {
    static const double perMs = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (double)f.QuadPart / 1000.0; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / perMs;
}
// F4 as ever; with [hud] monster_names_own_all_views (Serg, 2026-11-09: "новые плашки над монстрами включались на всех видах") also VR F1, F2, F3 while
// our camera draws a stereo pair there (AfrOn: its eyes project our plate over the head; the game's own camera's flat F1 keeps the game's plate).
// (Not given way to the room bar, monster_bar_room, since 2026-11-09: with ours on that bar stands aside - PlateWorldTick - as every old plate way does.)
bool MonsterNamesOwnNow() {
    if (!g_set.monsterNamesOwn.load() || !g_enabled.load()) return false;
    if (FullBody() && viewgate::F4()) return true;
    return g_set.monsterNamesOwnAllViews.load() && g_set.platform.load() == 1 && !FullBody() && AfrOn();
}
// [hud] npc_names_own: the hover tooltip's names (ownnames::kHoverSites) - recorded like the plate's texts, skipped once ours is shown.
bool NpcNamesOwnNow() { return g_set.npcNamesOwn.load() && g_enabled.load() && FullBody() && viewgate::F4(); }
std::atomic<bool> g_hoverOn{false};
std::atomic<bool> g_names3d{false};
void NamesSet3d(bool on) { g_names3d.store(on, std::memory_order_relaxed); }
bool PlateStillShown() { int32_t r[4]; return gamestate::PlateRect(r); }
bool NamesTargetPlate(float* life, bool* unique) {
    AcquireSRWLockShared(&g_namesLock);
    const ownnames::PlateInfo p = g_namesInfo;
    ReleaseSRWLockShared(&g_namesLock);
    const uint64_t now = (uint64_t)NamesNowMs();
    if (!p.ms || (now > p.ms && now - p.ms > ownnames::kInfoMs)) return false;
    *life = ownnames::LifeFrac(p.life);
    *unique = p.unique != 0;
    return *life >= 0.0f;
}
bool NamesTargetHidden() {
    if (!mobbars::Enabled() || !viewgate::F4()) return false;   // (the hook hides ordinary monsters' plates in F4 only: elsewhere ours shows, as the game's)
    uintptr_t unit = 0;
    plateworld::Follow f;
    float px[2];
    if (plateworld::GetFollow(&f, GetTickCount64())) unit = f.unit;
    else if (!gamestate::PlateReadAt(&unit, px)) return false;
    return mobbars::HidesPlate(unit, [](uintptr_t at, auto& v) { return SafeRead(&v, (const void*)at, sizeof v); });
}
uintptr_t g_hoverSites[3] = {};   // the return addresses, once each was checked to be a call of TextDraw (InstallHoverSites)
ownnames::Record g_hoverRecord;   // (g_namesLock)
void NamesSetHover(bool on) { g_hoverOn.store(on && g_hoverSites[0] != 0, std::memory_order_relaxed); }
int HoverTake(ownnames::Text* out, int most) {
    ownnames::Text t[ownnames::kMax];
    AcquireSRWLockShared(&g_namesLock);
    const int n = g_hoverRecord.Take((uint64_t)NamesNowMs(), t);
    ReleaseSRWLockShared(&g_namesLock);
    int k = 0;
    for (int i = 0; i < n && k < most; ++i) {
        t[i].which = ownnames::kWhichHover;
        out[k++] = t[i];
    }
    return k;
}
void NamesSetPlate(bool on, const float* platePx) {
    g_namesOn.store(on, std::memory_order_relaxed);
    if (!platePx) return;
    for (int i = 0; i < 4; ++i) g_namesPlate[i].store(platePx[i], std::memory_order_relaxed);
    g_namesPlateMs.store(NamesNowMs(), std::memory_order_relaxed);
}
int NamesTake(ownnames::Text out[ownnames::kMax]) {
    AcquireSRWLockShared(&g_namesLock);
    const int n = g_namesRecord.Take((uint64_t)NamesNowMs(), out);
    ReleaseSRWLockShared(&g_namesLock);
    return n;
}
void NamesMark(uint64_t key) {
    AcquireSRWLockExclusive(&g_namesLock);
    g_namesShown.Mark(key, (uint64_t)NamesNowMs());
    ReleaseSRWLockExclusive(&g_namesLock);
}
// The target the plate is of: the unit the anchor hook last hung it for (plateworld's follow, or the plate's last read), a monster (type 1); its data + 0x1A.
bool NamesTargetKind(uint8_t* special) {
    uintptr_t unit = 0;
    plateworld::Follow f;
    float px[2];
    if (plateworld::GetFollow(&f, GetTickCount64())) unit = f.unit;
    else if (!gamestate::PlateReadAt(&unit, px)) unit = 0;
    uint32_t type = 0;
    uintptr_t data = 0;
    return unit >= 0x10000 && SafeRead(&type, (const void*)unit, sizeof type) && type == 1 && SafeRead(&data, (const uint8_t*)unit + 0x10, sizeof data) &&
           data >= 0x10000 && SafeRead(special, (const uint8_t*)data + 0x1A, 1);
}
// A string of the game's, copied safely (a text it is about to draw, or one its message carries).
void CopyGameText(const char* src, char* dst, size_t n) {
    size_t i = 0;
    for (; src && i + 1 < n; ++i) {
        char c = 0;
        if (!SafeRead(&c, src + i, 1) || !c) break;
        dst[i] = c;
    }
    dst[i] = 0;
}
// The plate's SetInfo handler 0x1F84E0 (MonsterHealthPanel: rcx the panel's object, rdx the message, r8 the panel, r9b a flag) - read, never changed.
// mov [rsp+18h],rbx; mov [rsp+20h],r9b; mov [rsp+10h],rdx; mov [rsp+8],rcx (no RIP-relative operand in the first 20 bytes)
constexpr uint64_t RVA_PLATE_INFO = 0x1F84E0;
const uint8_t kSigPlateInfo[20] = {0x48,0x89,0x5C,0x24,0x18,0x44,0x88,0x4C,0x24,0x20,0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4C,0x24,0x08};
using PlateInfoFn = uintptr_t (*)(void* self, const uint8_t* msg, void* panel, uint8_t flag);
PlateInfoFn OrigPlateInfo = nullptr;
std::atomic<bool> g_plateInfoIn{false};
void NotePlateInfo(const uint8_t* msg) {
    uint64_t id = 0;
    uintptr_t data = 0, name = 0, unique = 0, more = 0;
    if (!msg || !SafeRead(&id, msg + ownnames::kMsgId, sizeof id) || id != ownnames::kSetInfo || !SafeRead(&data, msg + ownnames::kMsgData, sizeof data) ||
        data < 0x10000 || !SafeRead(&name, (const void*)(data + ownnames::kDataName), sizeof name))
        return;
    ownnames::PlateInfo p;
    CopyGameText((const char*)name, p.text[0], sizeof p.text[0]);
    if (SafeRead(&unique, (const void*)(data + ownnames::kDataUnique), sizeof unique)) CopyGameText((const char*)unique, p.text[1], sizeof p.text[1]);
    if (SafeRead(&more, (const void*)(data + ownnames::kDataAdditional), sizeof more)) CopyGameText((const char*)more, p.text[2], sizeof p.text[2]);
    SafeRead(p.colour, (const void*)(data + ownnames::kDataColour), sizeof p.colour);
    SafeRead(&p.life, (const void*)(data + ownnames::kDataLife), sizeof p.life);
    SafeRead(&p.unique, (const void*)(data + ownnames::kDataUniqueBg), sizeof p.unique);
    p.ms = (uint64_t)NamesNowMs();
    AcquireSRWLockExclusive(&g_namesLock);
    g_namesInfo = p;
    ReleaseSRWLockExclusive(&g_namesLock);
    static std::atomic<int> told{0};
    if (told.fetch_add(1) < 2) {
        char b[600];
        snprintf(b, sizeof b, "vrcam: [hud] monster_names_own - the plate told: name \"%.60s\", unique \"%.60s\", additional \"%.60s\"; colour bytes "
                 "%02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X", p.text[0], p.text[1], p.text[2], p.colour[0], p.colour[1], p.colour[2],
                 p.colour[3], p.colour[4], p.colour[5], p.colour[6], p.colour[7], p.colour[8], p.colour[9], p.colour[10], p.colour[11], p.colour[12], p.colour[13],
                 p.colour[14], p.colour[15]);
        Log(b);
    }
}
uintptr_t HookPlateInfo(void* self, const uint8_t* msg, void* panel, uint8_t flag) {
    if (MonsterNamesOwnNow()) NotePlateInfo(msg);   // (ours on: what the plate is told, whether or not ours reached the picture yet)
    return OrigPlateInfo(self, msg, panel, flag);
}
// [hud] monster_names_own: the game's plate never drawn - not hidden, moved or painted over (Serg, 2026-11-07: "the old plates' layer removed fully").
// MonsterHealthPanel's own Draw is its vtable's slot 3 (0x1CDF3B8 + 3 x 8: the widgets' Draw 0x855B00, which draws its children - the frame
// pictures, the Health bar, the text boxes; TooltipsPanel's vtable has its own Draw 0x880550 in the same slot). That slot is pointed at ours: while
// it runs with our names on, the game's picture draw 0x858510 and its fills 0x657AB0 / 0x657B90 draw nothing (t_inPlate) and its texts are skipped
// at TextDraw - so nothing of the plate reaches the layer, while its texts still pass TextDraw for our plate's measures. Its messages (SetInfo: the
// name, the life) are the panel's own and keep coming. Off: the slot calls the widgets' Draw as the game does.
constexpr uint64_t RVA_PLATE_DRAW_SLOT = 0x1CDF3D0, RVA_WIDGET_DRAW = 0x855B00, RVA_PICTURE_DRAW = 0x858510, RVA_FILL7 = 0x657AB0, RVA_FILL6 = 0x657B90;
const uint8_t kSigPictureDraw[14] = {0x40,0x55,0x53,0x57,0x41,0x56,0x48,0x8D,0x6C,0x24,0xD8,0x48,0x81,0xEC};
const uint8_t kSigFill7[10] = {0x44,0x3B,0xC1,0x7C,0x65,0x53,0x48,0x83,0xEC,0x50};
const uint8_t kSigFill6[12] = {0x44,0x3B,0xC1,0x0F,0x8C,0xD3,0x02,0x00,0x00,0x4C,0x8B,0xDC};
const uint8_t kSigWidgetDraw[15] = {0x40,0x53,0x56,0x57,0x41,0x57,0x48,0x83,0xEC,0x38,0x4C,0x89,0x74,0x24,0x28};
thread_local bool t_inPlate = false;
using WidgetDrawFn = uintptr_t (*)(void* self, void* a, void* b, void* c);
using PictureDrawFn = uintptr_t (*)(void* picture, void* rect, void* style, float scale, uintptr_t e0, uintptr_t e1, uintptr_t e2, uintptr_t e3);
using Fill7Fn = uintptr_t (*)(int x0, int y0, int x1, int y1, uintptr_t e0, uintptr_t e1, uintptr_t e2, uintptr_t e3);
using Fill6Fn = uintptr_t (*)(int x0, int y0, int x1, int y1, uintptr_t e0, uintptr_t e1, uintptr_t e2, uintptr_t e3);
WidgetDrawFn g_widgetDraw = nullptr;
PictureDrawFn OrigPictureDraw = nullptr;
Fill7Fn OrigFill7 = nullptr;
Fill6Fn OrigFill6 = nullptr;
std::atomic<bool> g_plateDrawIn{false};
std::atomic<bool> g_textDrawIn{false};   // the game's TextDraw hooked (InstallLabelHooks): the plate's texts reach ours
std::atomic<uint32_t> g_plateDrawsSkipped{0};
uintptr_t HookPlateDraw(void* self, void* a, void* b, void* c) {
    // Ours on, the game's plate never drawn - not waiting for the effect to say ours made it to the picture this frame (Serg, 2026-11-09: "их не
    // надо обрабатывать вовсе, а просто прятать этот слой"; until then a frame the effect had not taken the interface out drew the game's plate).
    // Still behind own_labels_drop_game: this hook is the 0.206.6 - 0.206.8 lost-device suspect (59584aad). Its Draw still runs with its pictures and
    // fills drawing nothing: its texts pass TextDraw to be recorded for ours.
    if (!DropGameLayer() || !ownnames::GamePlateHidden(MonsterNamesOwnNow(), true)) return g_widgetDraw(self, a, b, c);
    t_inPlate = true;   // (its pictures and fills draw nothing, its texts are recorded and skipped)
    const uintptr_t r = g_widgetDraw(self, a, b, c);
    t_inPlate = false;
    g_plateDrawsSkipped.fetch_add(1, std::memory_order_relaxed);
    return r;
}
uintptr_t HookPictureDraw(void* picture, void* rect, void* style, float scale, uintptr_t e0, uintptr_t e1, uintptr_t e2, uintptr_t e3) {
    if (t_inPlate) return 0;
    return OrigPictureDraw(picture, rect, style, scale, e0, e1, e2, e3);
}
uintptr_t HookFill7(int x0, int y0, int x1, int y1, uintptr_t e0, uintptr_t e1, uintptr_t e2, uintptr_t e3) {
    if (t_inPlate) return 0;
    return OrigFill7(x0, y0, x1, y1, e0, e1, e2, e3);
}
uintptr_t HookFill6(int x0, int y0, int x1, int y1, uintptr_t e0, uintptr_t e1, uintptr_t e2, uintptr_t e3) {
    if (t_inPlate) return 0;
    return OrigFill6(x0, y0, x1, y1, e0, e1, e2, e3);
}
// The plate panel's Draw slot pointed at ours, and the three draws it makes taken: once, the first time our names are on (each checked first).
void InstallPlateDraw() {
    static bool tried = false;
    if (tried || g_plateDrawIn.load() || !MonsterNamesOwnNow() || !DropGameLayer()) return;
    if (!Matches(RVA_WIDGET_DRAW, kSigWidgetDraw, sizeof kSigWidgetDraw) || !Matches(RVA_PICTURE_DRAW, kSigPictureDraw, sizeof kSigPictureDraw) ||
        !Matches(RVA_FILL7, kSigFill7, sizeof kSigFill7) || !Matches(RVA_FILL6, kSigFill6, sizeof kSigFill6))
        return;   // not decrypted yet, or another build
    tried = true;
    const uintptr_t slot = d2rsig::Addr(RVA_PLATE_DRAW_SLOT), draw = d2rsig::Addr(RVA_WIDGET_DRAW);
    uintptr_t now = 0;
    if (!slot || !draw || !SafeRead(&now, (const void*)slot, sizeof now) || now != draw) {
        Log("vrcam: [hud] monster_names_own - the plate panel's Draw slot is not the widgets' Draw (another game build?): the game's plate stays");
        return;
    }
    const bool pic = d2rsig::Hook(RVA_PICTURE_DRAW, kSigPictureDraw, sizeof kSigPictureDraw, (void*)&HookPictureDraw, (void**)&OrigPictureDraw);
    const bool f7 = d2rsig::Hook(RVA_FILL7, kSigFill7, sizeof kSigFill7, (void*)&HookFill7, (void**)&OrigFill7);
    const bool f6 = d2rsig::Hook(RVA_FILL6, kSigFill6, sizeof kSigFill6, (void*)&HookFill6, (void**)&OrigFill6);
    bool patched = false;
    if (pic && f7 && f6) {
        g_widgetDraw = (WidgetDrawFn)draw;
        DWORD old = 0;
        if (VirtualProtect((void*)slot, sizeof(uintptr_t), PAGE_READWRITE, &old)) {
            *(volatile uintptr_t*)slot = (uintptr_t)&HookPlateDraw;
            VirtualProtect((void*)slot, sizeof(uintptr_t), old, &old);
            FlushInstructionCache(GetCurrentProcess(), (void*)slot, sizeof(uintptr_t));
            patched = true;
        }
    }
    g_plateDrawIn.store(patched);
    char m[260];
    snprintf(m, sizeof m, "vrcam: [hud] monster_names_own - the game's plate %s (its Draw slot %s; picture %s, fills %s / %s)",
             patched ? "is not drawn while our names are on" : "STAYS (a hook failed)", patched ? "ours" : "the game's", pic ? "in" : "FAILED", f7 ? "in" : "FAILED",
             f6 ? "in" : "FAILED");
    Log(m);
}

void InstallPlateInfoHook() {
    InstallPlateDraw();
    static bool failed = false;
    if (g_plateInfoIn.load() || failed || !MonsterNamesOwnNow()) return;
    if (!Matches(RVA_PLATE_INFO, kSigPlateInfo, sizeof kSigPlateInfo)) return;   // not decrypted yet, or another build
    const bool in = d2rsig::Hook(RVA_PLATE_INFO, kSigPlateInfo, sizeof kSigPlateInfo, (void*)&HookPlateInfo, (void**)&OrigPlateInfo);
    failed = !in;
    g_plateInfoIn.store(in);
    Log(in ? "vrcam: [hud] monster_names_own - the plate's SetInfo hook in (read only: what the plate is told)"
           : "vrcam: [hud] monster_names_own - the plate's SetInfo hook FAILED: the game's names stay");
}
// Which plate is in use now, ours or the game's, and why: a line each change, with the view and the Quality preset's render stages (Serg,
// 2026-11-09: on Potato "старые таблички" over the monsters instead of ours) - vr/own_plate_why.h. From the frame tick, a few times a second.
void MonsterPlateWhichTick() {
    if (!g_inWorld.load()) return;
    ownplatewhy::In s;
    s.own = g_set.monsterNamesOwn.load();
    s.allViews = g_set.monsterNamesOwnAllViews.load();
    s.firstPerson = FullBody() && viewgate::F4();
    s.vrOurs = g_enabled.load() && g_set.platform.load() == 1;
    s.stereo = AfrOn();
    s.dropGame = DropGameLayer();
    s.drawHook = g_plateDrawIn.load();
    s.textHook = g_textDrawIn.load();
    s.effectReady = hud::PictureReady();
    float keepBar[4], keepMap[4];
    uint64_t srv = 0;
    s.layerOut = hud::LabelsNow(keepBar, keepMap, &srv);
    s.namesOn = g_namesOn.load(std::memory_order_relaxed);
    const ownplatewhy::Why why = ownplatewhy::Of(s);
    const int view = s.vrOurs ? g_set.vrView.load() : 0, stages = g_set.gameStages.load();
    static int toldWhy = -1, toldView = -1, toldStages = -1;
    static ULONGLONG since = 0;
    const ULONGLONG now = GetTickCount64();
    if ((int)why == toldWhy && view == toldView && stages == toldStages) { since = 0; return; }
    // (a change held for half a second: a menu's frame or a view switch's first frames do not fill the log)
    if (!since) since = now;
    if (now - since < 500) return;
    since = 0;
    toldWhy = (int)why; toldView = view; toldStages = stages;
    char m[400];
    snprintf(m, sizeof m, "vrcam: monster plate in %s%d, %s preset ([render] game_stages %d): %s", view ? "VR F" : "the flat game, view ", view ? view : g_set.vrView.load(),
             ownplatewhy::StagesName(stages), stages, ownplatewhy::Text(why));
    Log(m);
}
// The plate's text boxes give TextDraw a point, not a box (0.205.6, Serg's log: "  Zombie" at 867,956 0x0, the middle of the plate's box
// 786,914-945,997): the text is laid out about it. Its size is the game's own measure with the same style and scale (TextMeasure 0x909560, as
// ResizeLabelBox asks it), the box that size about the point (ownnames::BoxAbout). False: the old graphics, or no sane measure.
bool MeasuredBox(const char* text, const int32_t* box, const void* style, float scale, int32_t out[4]) {
    if (box[2] > 0 && box[3] > 0) { memcpy(out, box, 4 * sizeof(int32_t)); return true; }
    if (!style || !(scale > 0.05f && scale < 20.0f) || !((IsHdFn)d2rsig::Addr(RVA_IS_HD))()) return false;
    const int32_t most[2] = {0x7FFFFFFF, 0x7FFFFFFF};
    int32_t size[2] = {};
    ((TextMeasureFn)d2rsig::Addr(RVA_TEXT_MEASURE))(text, style, size, scale, most);
    return ownnames::BoxAbout(box[0], box[1], size[0], size[1], out);
}
// The three calls of TextDraw in the hover tooltip's drawer (0x8DA7E0), each checked: E8 and the distance to TextDraw.
void InstallHoverSites() {
    static bool done = false;
    if (done || !NpcNamesOwnNow()) return;
    done = true;
    int ok = 0;
    for (int i = 0; i < 3; ++i) {
        const uint64_t call = ownnames::kHoverSites[i] - 5;
        const int32_t rel = (int32_t)(RVA_TEXT_DRAW - ownnames::kHoverSites[i]);
        uint8_t want[5] = {0xE8};
        memcpy(want + 1, &rel, 4);
        if (Matches(call, want, sizeof want)) { g_hoverSites[i] = (uintptr_t)d2rsig::Addr(ownnames::kHoverSites[i]); ++ok; }
    }
    if (ok < 3) memset(g_hoverSites, 0, sizeof g_hoverSites);
    Log(ok == 3 ? "vrcam: [hud] npc_names_own - the hover names' three TextDraw calls found (0x8DA8F1 / 0x8DA9CD / 0x8DAA0A)"
                : "vrcam: [hud] npc_names_own - the hover names' TextDraw calls NOT as expected (another game build?): the game's names stay");
}
bool MeasuredBox(const char* text, const int32_t* box, const void* style, float scale, int32_t out[4]);
bool NoteHoverText(const char* text, const int32_t* box, const void* style, float scale);
// [hud] npc_names_own: the hover tooltip never drawn (Serg, 2026-11-07: the old names' layer removed). TooltipsPanel::Draw draws each tooltip
// record with 0x8DA7E0 (its only caller; the result unused): with our hover names on it is not run - its record read instead (+0x00 the text, +0x60
// its box, +0x80 the style; a second line +0x28 in +0x70 when +0x30 says so) and recorded for ours.
constexpr uint64_t RVA_TOOLTIP_DRAW = 0x8DA7E0;
const uint8_t kSigTooltipDraw[9] = {0x40,0x56,0x48,0x81,0xEC,0x10,0x01,0x00,0x00};   // push rsi; sub rsp, 0x110 (then the stack cookie, RIP-relative)
using TooltipDrawFn = uintptr_t (*)(const uint8_t* record);
TooltipDrawFn OrigTooltipDraw = nullptr;
// Only a hover NAME is ours: one line in the world. Every other tooltip the same drawer draws - an item's description in the inventory, stash,
// vendor or cube, a belt potion, a skill, a stat - is the game's and drawn as it is (Serg 2026-11-08: with npc_names_own + own_labels_drop_game
// the items' descriptions were gone). A menu open, or a text of more than one line: the game's own.
bool TooltipIsHoverName(const char* text) {
    if (gamestate::MenuOpen()) return false;
    for (const char* p = text; *p; ++p)
        if (*p == '\n' || *p == '\r') return false;
    return true;
}
uintptr_t HookTooltipDraw(const uint8_t* rec) {
    if (!g_hoverOn.load(std::memory_order_relaxed) || !rec || !DropGameLayer()) return OrigTooltipDraw(rec);
    const float scale = GameUiScaleNow();
    uintptr_t text = 0, more = 0, moreLen = 0;
    char first[0x80] = {};
    if (SafeRead(&text, rec + 0x00, sizeof text) && text >= 0x10000) CopyGameText((const char*)text, first, sizeof first);
    if (!first[0] || !TooltipIsHoverName(first)) {
        static std::atomic<int> toldPass{0};
        if (toldPass.fetch_add(1) < 2)
            Log(gamestate::MenuOpen() ? "vrcam: [hud] npc_names_own - a tooltip with a menu open: the game's own, drawn as it is (an item's description...)"
                                      : "vrcam: [hud] npc_names_own - a tooltip of more than one line: the game's own, drawn as it is (not a hover name)");
        return OrigTooltipDraw(rec);
    }
    NoteHoverText(first, (const int32_t*)(rec + 0x60), rec + 0x80, scale);
    if (SafeRead(&moreLen, rec + 0x30, sizeof moreLen) && moreLen && SafeRead(&more, rec + 0x28, sizeof more) && more >= 0x10000) {
        char buf[0x80];
        CopyGameText((const char*)more, buf, sizeof buf);
        if (buf[0]) NoteHoverText(buf, (const int32_t*)(rec + 0x70), rec + 0x80, scale);
    }
    return 0;   // (not drawn: ours stands in its place)
}
std::atomic<bool> g_tooltipDrawIn{false};
void InstallTooltipDraw() {
    static bool tried = false;
    if (tried || !NpcNamesOwnNow() || !DropGameLayer() || !Matches(RVA_TOOLTIP_DRAW, kSigTooltipDraw, sizeof kSigTooltipDraw)) return;
    tried = true;
    const bool in = d2rsig::Hook(RVA_TOOLTIP_DRAW, kSigTooltipDraw, sizeof kSigTooltipDraw, (void*)&HookTooltipDraw, (void**)&OrigTooltipDraw);
    g_tooltipDrawIn.store(in);
    Log(in ? "vrcam: [hud] npc_names_own - the game's hover names are not drawn while ours are on (its tooltip drawer read, not run)"
           : "vrcam: [hud] npc_names_own - the tooltip drawer's hook FAILED: the game's hover names stay");
}
// A hover name drawn: recorded where it is; true: skipped (ours is on the screen in its place).
bool NoteHoverText(const char* text, const int32_t* box, const void* style, float scale) {
    if (const uint32_t icon = ownlabels::IconIn(text, 0x80)) {   // the game's button icon (the (A) under an NPC's name): ours draws it as a green 'A' (IconsToKeys)
        static std::atomic<int> toldIcon{0};
        if (toldIcon.fetch_add(1) < 2) {
            char m[200];
            snprintf(m, sizeof m, "vrcam: [hud] npc_names_own - a hover line with the game's icon U+%04X (%d bytes): ours draws it as a green A", icon, (int)strlen(text));
            Log(m);
        }
    }
    int32_t b[4];
    if (!MeasuredBox(text, box, style, scale, b)) return false;
    const uint64_t now = (uint64_t)NamesNowMs();
    int32_t first = -1;
    SafeRead(&first, style, sizeof first);
    AcquireSRWLockExclusive(&g_namesLock);
    g_hoverRecord.Add(text, b, now, ownnames::kWhichHover, nullptr, ownlabels::FontOfStyle(first));
    const bool skip = g_namesShown.Lately(ownnames::KeyOf(text), now);
    ReleaseSRWLockExclusive(&g_namesLock);
    static std::atomic<int> told{0};
    if (told.fetch_add(1) < 2) {
        char m[240];
        snprintf(m, sizeof m, "vrcam: [hud] npc_names_own - a hover name drawn: \"%.60s\" at %d,%d %dx%d", text, b[0], b[1], b[2], b[3]);
        Log(m);
    }
    return skip;
}
// The target's body for our name over its head: its place on the grid (its path) and its MonStats row's footprint and height (monster data + 0x00 the
// row, its id the unit's class: vr/mob_bars.h), read now.
int NamesTargetBody(float foot[2], uint8_t row[3]) {
    uintptr_t unit = 0;
    plateworld::Follow f;
    float px[2];
    // the plate's hook does not answer every frame (0.205.13: "over the head" took turns with "no target unit"): the last target is kept a little while
    static uintptr_t lastUnit = 0;
    static ULONGLONG lastAt = 0;
    const ULONGLONG now = GetTickCount64();
    if (plateworld::GetFollow(&f, now)) unit = f.unit;
    else if (!gamestate::PlateReadAt(&unit, px)) unit = 0;
    if (unit >= 0x10000) { lastUnit = unit; lastAt = now; }
    else if (lastUnit && now - lastAt <= ownnames::kTargetKeepMs && PlateStillShown()) unit = lastUnit;
    uint32_t type = 0, cls = 0;
    uintptr_t data = 0, r = 0;
    uint16_t rowId = 0;
    uint8_t b[16] = {};
    auto read = [](uintptr_t at, auto& v) { return SafeRead(&v, (const void*)at, sizeof v); };
    if (unit < 0x10000) return ownnames::kBodyNoUnit;
    if (!read(unit, type) || type != 1 || !read(unit + 0x04, cls)) return ownnames::kBodyNotMonster;
    if (!read(unit + 0x10, data) || data < 0x10000) return ownnames::kBodyNoData;
    if (!plateworld::FootOf(unit, read, &foot[0], &foot[1])) return ownnames::kBodyNoFoot;
    const bool rowOk = read(data, r) && r >= 0x10000 && read(r, rowId) && rowId == cls && SafeRead(b, (const void*)r, sizeof b) && b[8] >= 1 && b[8] <= 8 && b[9] >= 1 && b[9] <= 8;
    if (!rowOk) {
        row[0] = ownnames::kFallbackRow.footprintA; row[1] = ownnames::kFallbackRow.footprintB; row[2] = ownnames::kFallbackRow.height;
        return ownnames::kBodyRowOff;
    }
    row[0] = b[8]; row[1] = b[9]; row[2] = b[13];
    return ownnames::kBodyOk;
}
// The plate's texts left out: with its layer dropped (own_labels_drop_game) as soon as ours is on - as the plate's frame (HookPlateDraw), not
// waiting for the effect (Serg, 2026-11-09: "просто прятать этот слой"); without the drop, only once the effect drew ours (as before: never the
// frame left with no name in it).
bool PlateTextsHidden() {
    return MonsterNamesOwnNow() && (DropGameLayer() || g_namesOn.load(std::memory_order_relaxed));
}
// True: the game's drawing of this text is skipped (ours is on the screen in its place).
bool NoteNameText(const char* text, const int32_t* box, const void* style, float scale) {
    if (ownlabels::IconIn(text, 0x80)) return false;   // (a game icon in it: the game draws it, never our box)
    const uint64_t now = (uint64_t)NamesNowMs();
    AcquireSRWLockShared(&g_namesLock);
    const int which = ownnames::PlateTextOf(g_namesInfo, text, now);
    float rgb[3];
    const bool known = which >= 0 && ownnames::InfoColour(g_namesInfo.colour, rgb);
    ReleaseSRWLockShared(&g_namesLock);
    if (which < 0) return false;
    int32_t b[4];
    if (!MeasuredBox(text, box, style, scale, b)) {   // (measured outside the lock: the game's own text code)
        static std::atomic<int> told{0};
        if (told.fetch_add(1) < 2) {
            char m[200];
            snprintf(m, sizeof m, "vrcam: [hud] monster_names_own - the plate's text at %d,%d %dx%d could not be measured: not drawn (the game's plate stays hidden)", box[0], box[1], box[2], box[3]);
            Log(m);
        }
        return ownnames::SkipGameText(true, which, false);   // (ours or nothing: never a flash of the game's)
    }
    int32_t first = -1;
    SafeRead(&first, style, sizeof first);
    static std::atomic<bool> toldFont{false};
    if (!toldFont.exchange(true)) {
        char m[160];
        snprintf(m, sizeof m, "vrcam: [hud] monster_names_own - the plate's text style's font %d (0 Exocet, 1 Formal): ours in the same face", first);
        Log(m);
    }
    AcquireSRWLockExclusive(&g_namesLock);
    g_namesRecord.Add(text, b, now, which, known ? rgb : nullptr, ownlabels::FontOfStyle(first));
    // every copy of a told text, every frame our names are on (0.206.5: the game's text flashed on the frames ours was not marked yet) - ours or nothing
    const bool skip = ownnames::SkipGameText(PlateTextsHidden(), which, g_namesShown.Lately(ownnames::KeyOf(text), now));
    ReleaseSRWLockExclusive(&g_namesLock);
    return skip;
}

uintptr_t HookTextDraw(const char* text, const int32_t* box, const void* style, float scale) {
    const float k = t_labelScale;
    if (t_inPlate) {   // [hud] monster_names_own: inside the game's plate's Draw - recorded for ours, never drawn
        if (text && box && text[0]) NoteNameText(text, box, style, scale);
        return OrigTextDraw("", box, style, scale);
    }
    if (k <= 0.0f && text && box && text[0] && g_hoverOn.load(std::memory_order_relaxed) &&
        ownnames::HoverSiteOf(reinterpret_cast<uintptr_t>(_ReturnAddress()), g_hoverSites) >= 0) {   // [hud] npc_names_own: a hover name
        if (NoteHoverText(text, box, style, scale)) return OrigTextDraw("", box, style, scale);
        return OrigTextDraw(text, box, style, scale);
    }
    if (k <= 0.0f && text && box && text[0] && PlateTextsHidden() && NoteNameText(text, box, style, scale))
        return OrigTextDraw("", box, style, scale);   // [hud] monster_names_own: ours is drawn in its place (an empty text, as LabelPaint's rim strips)
    return OrigTextDraw(text, box, style, k > 0.0f ? scale * k : scale);
}

// 0.197.1 (Serg, 2026-11-02): all of the game's item-label code is left alone until the freezes are understood - rivas_10's game hung in the
// game's text layout (0x90xxxx, between TextDraw and TextMeasure, which these hooks wrap) on 0.194 and 0.197. No label hook goes in: labels_size,
// labels_alpha on the floor, labels_far, labels_depth_body and labels_whole_eye do nothing; the labels are the game's own.
constexpr bool kLabelHooksOff = false;   // 0.197.4: back on - without them the labels doubled in the headset (Serg); the laser tooltips stay off
void TellLabelHooksOff() {
    static std::atomic<bool> told{false};
    if (!told.exchange(true)) Log("vrcam: item-label hooks OFF in this build (0.197.1: the game's label and text code left alone while a freeze is "
                                  "looked into) - labels_size / labels_alpha on the floor / labels_far / labels_depth_body / labels_whole_eye do nothing");
}

// The first time the floor wants them; quiet while the label code is still
// encrypted (D2R decrypts a page the first time it runs: labels shown once).
void InstallLabelHooks() {
    if (kLabelHooksOff) { TellLabelHooksOff(); return; }
    if (g_ctx && g_inWorld.load()) InstallPlateInfoHook();   // [hud] monster_names_own: what the plate is told (read only)
    if (g_ctx && g_inWorld.load()) InstallHoverSites();   // [hud] npc_names_own: where the hover names are drawn from
    if (g_ctx && g_inWorld.load()) InstallTooltipDraw();   // ... and the game's own tooltip not drawn while ours are on
    static bool paintFailed = false, sizeFailed = false, drawIn = false;
    static int misses = 0;
    if (!g_ctx || !g_inWorld.load() || !g_set.labelsNativeFloor.load()) return;
    float alpha, size;
    LabelLookNow(&alpha, &size);   // the view now: the hooks go in the first time any view wants them
    const bool wantBox = alpha < 0.995f;
    const bool wantSize = std::abs(size - 1.0f) > 0.005f || LabelsByDistance() || LabelDepthWanted() || PointHighlightWanted() || OwnLabelsNow() ||
                          MonsterNamesOwnNow() || NpcNamesOwnNow();   // (point_pickup: whose label; monster_names_own, npc_names_own: the text hook)
    if (!wantBox && !wantSize) return;
    if (!g_labelPaintIn.load()) {
        if (paintFailed) return;
        if (!Matches(RVA_LABEL_PAINT, kSigLabelPaint, sizeof kSigLabelPaint)) {
            if (++misses == 120)   // a minute on the floor
                Log("vrcam: the game's item-label code not found yet (no labels shown so far, or another game build) - "
                    "[hud_floor] labels_alpha fades them as a picture, labels_size does nothing");
            return;
        }
        const bool in = d2rsig::Hook(RVA_LABEL_PAINT, kSigLabelPaint, sizeof kSigLabelPaint, (void*)&HookLabelPaint, (void**)&OrigLabelPaint);
        paintFailed = !in;
        g_labelPaintIn.store(in);
        Log(in ? "vrcam: item-label hook in - on the floor the labels' box fades by the game's own code ([hud_floor] labels_alpha)"
               : "vrcam: item-label hook FAILED - [hud_floor] labels_alpha fades them as a picture, labels_size does nothing");
        if (!in) return;
    }
    if (!wantSize || g_labelSizeIn.load() || sizeFailed) return;
    if (!Matches(RVA_LABEL_LAYOUT, kSigLabelLayout, sizeof kSigLabelLayout) || !Matches(RVA_TEXT_DRAW, kSigTextDraw, sizeof kSigTextDraw))
        return;   // not decrypted yet
    if (!Matches(RVA_TEXT_MEASURE, kSigTextMeasure, sizeof kSigTextMeasure) || !Matches(RVA_UI_SCALE, kSigUiScale, sizeof kSigUiScale) ||
        !Matches(RVA_IS_HD, kSigIsHd, sizeof kSigIsHd)) {
        sizeFailed = true;   // these run all the time, so not encrypted: another build
        Log("vrcam: item-label size NOT possible - the game's text code is not as expected (another build?)");
        return;
    }
    // The name drawn at the other scale first: a smaller box round a name at the old size would wrap it.
    if (!drawIn) drawIn = d2rsig::Hook(RVA_TEXT_DRAW, kSigTextDraw, sizeof kSigTextDraw, (void*)&HookTextDraw, (void**)&OrigTextDraw);
    g_textDrawIn.store(drawIn, std::memory_order_relaxed);
    const bool in = drawIn && d2rsig::Hook(RVA_LABEL_LAYOUT, kSigLabelLayout, sizeof kSigLabelLayout, (void*)&HookLabelLayout, (void**)&OrigLabelLayout);
    sizeFailed = !in;
    g_labelSizeIn.store(in);
    Log(in ? "vrcam: item-label size hooks in - on the floor [hud_floor] labels_size sets the labels' size"
           : "vrcam: item-label size hooks FAILED - labels_size does nothing");
}

// [hud] labels_whole_eye (vr/label_area.h): LabelLayout asks InterfaceSize 0x8462E0 (the interface root's rectangle times its scale; width low, height high) at
// 0x1FAC2A and keeps a label while its bottom is above the interface's origin y plus that height. To that one call, while ticked, the whole picture's height.
// Found by bearstonem/d2r-pcvr (MIT, v0.3.23: vr/vrcam.cpp InstallLabelArea).
constexpr uint64_t RVA_INTERFACE_SIZE = 0x8462E0, RVA_LABEL_AREA_CALL = 0x1FAC2A;
// push rbx; sub rsp, 0x40; mov rbx, [rip + 0x2BF9E83] (the interface's root - the same object UiScale 0x8460F0 reads); test rbx, rbx; je +0x6E
const uint8_t kSigInterfaceSize[18] = {0x40,0x53,0x48,0x83,0xEC,0x40,0x48,0x8B,0x1D,0x83,0x9E,0xBF,0x02,0x48,0x85,0xDB,0x74,0x6E};
using InterfaceSizeFn = uint64_t (*)();
InterfaceSizeFn OrigInterfaceSize = nullptr;
std::atomic<bool> g_labelAreaIn{false};
uintptr_t g_labelAreaRet = 0;

uint64_t HookInterfaceSize() {
    const uint64_t size = OrigInterfaceSize();
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) != g_labelAreaRet || !g_set.labelsWholeEye.load(std::memory_order_relaxed)) return size;
    uint32_t pictureH = 0;
    vrres::Size game;
    RECT rc{};
    if (vrres::GameSize(g_gameWnd, &game)) pictureH = game.h;   // [render] vr_resolution: the game's picture, not the window
    else if (g_gameWnd && GetClientRect(g_gameWnd, &rc)) pictureH = (uint32_t)std::max<LONG>(0, rc.bottom - rc.top);
    const uint64_t area = labelarea::LabelArea(size, pictureH);
    static std::atomic<bool> told{false};
    if (area != size && !told.exchange(true))
        LogF("vrcam: [hud] labels_whole_eye - item labels kept down to the picture's bottom (%u px), not the interface's %u px", unsigned(area >> 32), unsigned(size >> 32));
    return area;
}

// Twice a second while ticked and in a world; once in, the hook stays (the tick is read in it, live). LabelLayout's page is encrypted until a label is first laid out.
void InstallLabelArea() {
    if (kLabelHooksOff) { TellLabelHooksOff(); return; }
    static bool failed = false;
    if (g_labelAreaIn.load() || failed || !g_ctx || !g_inWorld.load() || !g_set.labelsWholeEye.load()) return;
    uint8_t call[5] = {0xE8};
    const int32_t delta = int32_t(int64_t(RVA_INTERFACE_SIZE) - int64_t(RVA_LABEL_AREA_CALL + 5));
    memcpy(call + 1, &delta, sizeof delta);
    if (!Matches(RVA_LABEL_AREA_CALL, call, sizeof call)) {
        if (!g_labelSizeIn.load() && !Matches(RVA_LABEL_LAYOUT, kSigLabelLayout, sizeof kSigLabelLayout)) return;   // not decrypted yet
        failed = true;
        Log("vrcam: [hud] labels_whole_eye NOT possible - the game's label code is not as 3.3.93787 has it (another build?)");
        return;
    }
    g_labelAreaRet = d2rsig::Addr(RVA_LABEL_AREA_CALL + 5);
    const bool in = g_labelAreaRet && d2rsig::Hook(RVA_INTERFACE_SIZE, kSigInterfaceSize, sizeof kSigInterfaceSize, (void*)&HookInterfaceSize, (void**)&OrigInterfaceSize);
    failed = !in;
    g_labelAreaIn.store(in);
    Log(in ? "vrcam: [hud] labels_whole_eye - item-label area hook in: labels kept over the whole picture, not only the interface's height"
           : "vrcam: [hud] labels_whole_eye - item-label area hook FAILED: labels as the game lays them out");
}

}  // namespace vrc
