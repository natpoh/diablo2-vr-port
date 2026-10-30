// vr/hud/labels.cpp - cut from vr/vrcam.cpp (docs/plan_modules.md M9); what it gives: vr/hud/hud_vr.h. It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

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
bool LabelDepthWanted() { return AfrOn() && g_enabled.load() && FullBody() && (g_set.labelsDepthBody.load() > 0.005f || replay::OverlaysWanted()); }
SRWLOCK g_labelDepthLock = SRWLOCK_INIT;
LabelDepth g_labelDepthNew[kLabelDepthMax];
int g_labelDepthNewN = 0;
void NoteLabelDepth(void* unit, const int32_t* box) {
    float d;
    if (!LabelDepthWanted() || !LabelItemDistance(unit, &d)) return;
    AcquireSRWLockExclusive(&g_labelDepthLock);
    if (g_labelDepthNewN < kLabelDepthMax) g_labelDepthNew[g_labelDepthNewN++] = {box[0], box[1], box[2], box[3], d};
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
bool HookLabelLayout(void* unit, char* name, int32_t* box, int32_t minX, int32_t maxX, uint8_t flag) {
    const bool shown = OrigLabelLayout(unit, name, box, minX, maxX, flag);
    const bool item = LabelUnitIsItem(unit);
    const float k = item ? LabelSizeNow() * LabelDistanceK(unit) : 1.0f;
    if (shown && name) NoteLabelScale(name, box, item ? k : -1.0f);
    if (shown && box && name && item && std::abs(k - 1.0f) >= 0.005f) ResizeLabelBox(name, box, k);
    if (shown && box && item) NoteLabelDepth(unit, box);
    return shown;
}
// The box made again round the name at k times its scale, about the same bottom middle.
bool ResizeLabelBox(char* name, int32_t* box, float k) {
    const bool shown = true;
    if (!((IsHdFn)d2rsig::Addr(RVA_IS_HD))()) return shown;   // the old graphics: their text has no scale
    const float s = ((UiScaleFn)d2rsig::Addr(RVA_UI_SCALE))();
    if (!(s > 0.05f && s < 20.0f)) return shown;
    g_uiScaleSeen.store(s);
    const int32_t most[2] = {0x7FFFFFFF, 0x7FFFFFFF};   // as the layout asks: one line, no limit
    int32_t was[2] = {}, now[2] = {};
    const TextMeasureFn measure = (TextMeasureFn)d2rsig::Addr(RVA_TEXT_MEASURE);
    measure(name, name + 0x80, was, s, most);
    measure(name, name + 0x80, now, s * k, most);
    // the padding the game put round the name, scaled with it; anything else is not the box expected
    const int32_t padW = box[2] - was[0], padH = box[3] - was[1];
    if (was[0] <= 0 || was[1] <= 0 || now[0] <= 0 || now[1] <= 0 || padW < 0 || padH < 0 || padW > 400 || padH > 200)
        return shown;
    const int32_t w = now[0] + (int32_t)std::ceil((float)padW * k), h = now[1] + (int32_t)std::ceil((float)padH * k);
    const int32_t mid = box[0] + box[2] / 2, bottom = box[1] + box[3];
    box[0] = mid - w / 2; box[1] = bottom - h; box[2] = w; box[3] = h;
    return shown;
}

uintptr_t HookLabelPaint(const int32_t* box, const char* name, const float* colour) {
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

uintptr_t HookTextDraw(const char* text, const int32_t* box, const void* style, float scale) {
    const float k = t_labelScale;
    return OrigTextDraw(text, box, style, k > 0.0f ? scale * k : scale);
}

// The first time the floor wants them; quiet while the label code is still
// encrypted (D2R decrypts a page the first time it runs: labels shown once).
void InstallLabelHooks() {
    static bool paintFailed = false, sizeFailed = false, drawIn = false;
    static int misses = 0;
    if (!g_ctx || !g_inWorld.load() || !g_set.labelsNativeFloor.load()) return;
    float alpha, size;
    LabelLookNow(&alpha, &size);   // the view now: the hooks go in the first time any view wants them
    const bool wantBox = alpha < 0.995f;
    const bool wantSize = std::abs(size - 1.0f) > 0.005f || LabelsByDistance() || LabelDepthWanted();
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
