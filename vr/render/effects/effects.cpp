// vr/render/effects/effects.cpp - our effect (D2R_DepthFog.fx) through ReShade or the plugin's own runtime: joining ReShade, DLSS for
// both eyes, the interface's boxes and uniforms, the sky pictures, the present (PresentCommon), [render] no_reshade's hooks, and every
// uniform of the effect each present (EffectsBegin) - the fx namespace, cut whole from vr/vrcam.cpp (docs/plan_modules.md M15; its split
// into per-feature uniform writers is a change of code, later). What it gives: vr/render/effects/effects.h. It sees vrcam.cpp's headers.
#include "../../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {
// [fog] game_fog: vrcam.cpp's gamefogvr (not vr/game_fog_vr.h's) - as the effect saw it there, with the pure parts
namespace gamefogvr {
using namespace ::gamefogvr;
bool Active();
Fog Current();
void Update();
}  // namespace gamefogvr
// (vrcam.cpp and vr/input/aim.cpp; declared here only: their defaults are their definitions')
void CeilingNow(float* height, float* bright, float* light, float* relief = nullptr, int* slot = nullptr, float* own = nullptr);
void CeilingLookNow(float* wet, float* detail, float* contrast, CeilVaultCfg* vault);
bool HandRay(float* o, float* d, float* dRaw = nullptr);


// Fog: D2R_DepthFog.fx runs inside ReShade, which this plugin joins as an
// add-on so the ini (and the settings window) can switch it and set its range.
// ReShade's own UI for the effect stays usable; what we write wins each frame.
namespace fx {
bool g_registered = false;
bool g_joined = false;   // reshade::register_addon(g_self) done - by TryRegister, or at the plugin's load for lightbounds
bool JoinReShade() {
    if (!g_joined && g_self) g_joined = reshade::register_addon(g_self);
    return g_joined;
}
// The effect's two techniques: [0] the left eye's frames (and mono), [1] the
// right eye's. The eye of a frame is which one is on, not a uniform: under
// D3D12 ReShade rewrites the effect's one constant buffer in place at every
// present, and with a pair per game frame the right eye's present wrote it
// before the GPU had run the left eye's effect - the left eye drew the right
// eye's sky. A technique's state is read when its commands are recorded.
reshade::api::effect_technique g_tech[2]{};
// The eye of the frame presented last (OnBeginEffects); the next is the other.
int g_fxEye = 0;

// DLSS with real stereo (2026-10-08): DLSS builds each picture out of the ones
// before it, and with the eyes by turns the one before is the OTHER eye's - its
// silhouettes showed through the other eye (with real stereo off DLSS looked
// right). So each eye gets a DLSS instance of its own: the driver's NGX
// (_nvngx.dll, which the game's own NGX library calls by name) is hooked, a
// second SuperSampling feature is made beside the game's, and the right eye's
// frames are evaluated on it. The motion vectors are still the game's, made
// against the previous frame - the other eye's (see the plan, next step).
namespace dlsseyes {
struct NgxHandle { unsigned int Id; };
using CreateFn = int(__cdecl*)(void* cmdList, int feature, void* params, NgxHandle** out);
using EvalFn = int(__cdecl*)(void* cmdList, const NgxHandle* h, const void* params, void* callback);
using ReleaseFn = int(__cdecl*)(NgxHandle* h);
CreateFn OrigCreate = nullptr;
EvalFn OrigEval = nullptr;
ReleaseFn OrigRelease = nullptr;
constexpr int kSuperSampling = 1;   // NVSDK_NGX_Feature_SuperSampling
SRWLOCK g_lock = SRWLOCK_INIT;
struct Twin { const NgxHandle* game; NgxHandle* twin; };
std::vector<Twin> g_twins;
std::vector<const NgxHandle*> g_noTwin;   // a twin was tried for these and failed: not again
bool Ok(int r) { return (r & 0xFFF00000) != 0xBAD00000; }

NgxHandle* TwinOf(const NgxHandle* h) {
    AcquireSRWLockShared(&g_lock);
    NgxHandle* t = nullptr;
    for (const Twin& x : g_twins) if (x.game == h) { t = x.twin; break; }
    ReleaseSRWLockShared(&g_lock);
    return t;
}
bool TriedBefore(const NgxHandle* h) {
    AcquireSRWLockShared(&g_lock);
    const bool tried = std::find(g_noTwin.begin(), g_noTwin.end(), h) != g_noTwin.end();
    ReleaseSRWLockShared(&g_lock);
    return tried;
}
// A second instance from the same parameters: at the game's own creation, or -
// for a feature made before the hook was in - from the evaluation's parameters
// (the game keeps one parameter set, creation keys and all).
NgxHandle* MakeTwin(void* cmdList, const NgxHandle* game, void* params, const char* when) {
    NgxHandle* twin = nullptr;
    const int r = OrigCreate(cmdList, kSuperSampling, params, &twin);
    AcquireSRWLockExclusive(&g_lock);
    if (Ok(r) && twin) g_twins.push_back({game, twin});
    else { g_noTwin.push_back(game); twin = nullptr; }
    ReleaseSRWLockExclusive(&g_lock);
    if (twin) LogF("vrcam: DLSS - a second instance for the right eye (%s): game's %u, the eye's %u", when, game->Id, twin->Id);
    else LogF("vrcam: DLSS - the right eye's instance could not be made (%s, 0x%08X): both eyes share the game's", when, (unsigned)r);
    return twin;
}

int __cdecl HookCreate(void* cmdList, int feature, void* params, NgxHandle** out) {
    const int r = OrigCreate(cmdList, feature, params, out);
    if (feature == kSuperSampling && Ok(r) && out && *out && g_set.dlssPerEye.load())
        MakeTwin(cmdList, *out, params, "with the game's");
    return r;
}
int __cdecl HookEval(void* cmdList, const NgxHandle* h, const void* params, void* callback) {
    if (h && g_set.dlssPerEye.load() && AfrOn()) {
        // the eye this frame draws: a pair per game frame sets it for each pass;
        // by turns, the one after the eye presented last
        const int eye = PairWanted() ? (g_eye.load() & 1) : (g_fxEye ^ 1);
        if (eye == 0 && replay::DlssBoth()) {   // the left pass of a replay: both instances, predicated
            NgxHandle* twin = TwinOf(h);
            if (!twin && !TriedBefore(h)) twin = MakeTwin(cmdList, h, const_cast<void*>(params), "for the replay");
            if (twin) {
                auto* cl = (ID3D12GraphicsCommandList*)cmdList;
                cl->SetPredication(replay::g_predBuf, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
                void* restore = g_inWorld.load() ? dlssmv::BeforeEvaluate(cmdList, params, 0) : nullptr;
                const int r = OrigEval(cmdList, h, params, callback);
                dlssmv::AfterEvaluate(params, restore);
                cl->SetPredication(replay::g_predBuf, 8, D3D12_PREDICATION_OP_EQUAL_ZERO);
                OrigEval(cmdList, twin, params, callback);
                cl->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
                if (replay::g_predEvals.fetch_add(1) == 0) Log("vrcam: DLSS - the replay's left pass carries both eyes' evaluations (predicated)");
                return r;
            }
        }
        if (eye == 1) {
            NgxHandle* twin = TwinOf(h);
            if (!twin && !TriedBefore(h)) twin = MakeTwin(cmdList, h, const_cast<void*>(params), "at the first evaluation");
            if (twin) {
                void* restore = g_inWorld.load() ? dlssmv::BeforeEvaluate(cmdList, params, eye) : nullptr;
                const int r = OrigEval(cmdList, twin, params, callback);
                dlssmv::AfterEvaluate(params, restore);
                return r;
            }
        }
        if (TwinOf(h)) {   // the left eye on the game's own instance
            void* restore = g_inWorld.load() ? dlssmv::BeforeEvaluate(cmdList, params, eye) : nullptr;
            const int r = OrigEval(cmdList, h, params, callback);
            dlssmv::AfterEvaluate(params, restore);
            return r;
        }
    }
    return OrigEval(cmdList, h, params, callback);
}
int __cdecl HookRelease(NgxHandle* h) {
    NgxHandle* twin = nullptr;
    AcquireSRWLockExclusive(&g_lock);
    for (size_t i = 0; i < g_twins.size(); ++i)
        if (g_twins[i].game == h) { twin = g_twins[i].twin; g_twins.erase(g_twins.begin() + i); break; }
    g_noTwin.erase(std::remove(g_noTwin.begin(), g_noTwin.end(), h), g_noTwin.end());
    ReleaseSRWLockExclusive(&g_lock);
    if (twin) OrigRelease(twin);
    return OrigRelease(h);
}

// Once the driver's NGX is in the process (the game loads it when it starts NGX).
void Install() {
    static bool done = false;
    if (done) return;
    HMODULE m = GetModuleHandleW(L"_nvngx.dll");
    if (!m) return;
    done = true;
    void* c = (void*)GetProcAddress(m, "NVSDK_NGX_D3D12_CreateFeature");
    void* e = (void*)GetProcAddress(m, "NVSDK_NGX_D3D12_EvaluateFeature");
    void* r = (void*)GetProcAddress(m, "NVSDK_NGX_D3D12_ReleaseFeature");
    const bool ok = c && e && r &&
                    MH_CreateHook(c, (void*)&HookCreate, (void**)&OrigCreate) == MH_OK &&
                    MH_CreateHook(e, (void*)&HookEval, (void**)&OrigEval) == MH_OK &&
                    MH_CreateHook(r, (void*)&HookRelease, (void**)&OrigRelease) == MH_OK &&
                    MH_EnableHook(c) == MH_OK && MH_EnableHook(e) == MH_OK && MH_EnableHook(r) == MH_OK;
    LogF("vrcam: DLSS hooks %s (_nvngx.dll) - with real stereo each eye gets its own DLSS instance", ok ? "in" : "NOT in");
}
}  // namespace dlsseyes

// The views of the toolbar's and the map's copies bound to the effect (hud::PictureNow), 0 = none yet.
uint64_t g_pieceBound[4] = {};   // the toolbar, the map, the labels' layer, the wrists' globes
uint64_t g_madeBound = 0;         // eyes by turns: the made eye's depth bound as D2RVR_DEPTH_MADE (0 after the effects reload)
uint32_t g_madeBoundGen = 0;      // ... which of the textures made (turnwarp::Gpu::MadeDepthGen: a new one may sit at the old one's address)

// The armed skill's icon uploaded into the effect's OrbIconTex ([gameplay] melee_skill_armed_icon): which skill, from which decode of the icon cache, into which texture variable. -1: none
// (a reload of the effects makes the texture anew: Forget says so, and the icon is uploaded again).
int g_iconUpSkill = -1, g_iconTry = -1;
uint32_t g_iconUpRev = 0;
uint64_t g_iconUpVar = 0;

void Forget(reshade::api::effect_runtime*) { g_tech[0] = g_tech[1] = {0}; replay::g_rightTech = {0}; replay::g_ovTech[0][0] = replay::g_ovTech[0][1] = replay::g_ovTech[1][0] = replay::g_ovTech[1][1] = {0}; g_pieceBound[0] = g_pieceBound[1] = g_pieceBound[2] = g_pieceBound[3] = 0; g_madeBound = 0; replay::g_depthBound = 0; g_iconUpSkill = g_iconTry = -1; }

// The effect's uniforms by name. RT: ReShade's effect_runtime, or [render] no_reshade's fxnative::Runtime (the same names and types).
template <class RT> void SetFloats(RT* rt, const char* name, const float* v, size_t n) {
    const reshade::api::effect_uniform_variable u = rt->find_uniform_variable("D2R_DepthFog.fx", name);
    if (u.handle) rt->set_uniform_value_float(u, v, n);
}
template <class RT> void SetFloat(RT* rt, const char* name, float v) { SetFloats(rt, name, &v, 1); }
// The monster's name plate as uv of the screen (2026-10-12): gamestate has it in the layout's units, off the
// panel's anchor (the top middle of the interface's area). The game's interface scale makes them pixels - the
// label code's, else asked here: the screen's height over 2160 times the Safe Screen Percent (0.711 = 1920 /
// 2160 x 0.80 in the logs) - and that area stands in the middle of the screen, so its top is half of what the
// percent leaves free. Margins round it for the background's caps and the frame's lines, drawn outside its
// rectangle (plateworld::BoxOf, the room bar's cut has the same box). Then moved to where the game hangs the plate
// in its last frames (plateworld::FollowBox): the read is up to a few frames old, the plate moves with the head.
// The first few go into the log, to check against where it shows.
void PlateBoxUv(const int32_t r[4], uint32_t sw, uint32_t sh, float box[4]) {
    float s = g_uiScaleSeen.load();
    if (!(s > 0.05f && s < 20.0f) && Matches(RVA_UI_SCALE, kSigUiScale, sizeof kSigUiScale)) s = ((UiScaleFn)d2rsig::Addr(RVA_UI_SCALE))();
    if (!(s > 0.05f && s < 20.0f)) s = sh / 2160.0f;
    float px[4];
    if (!plateworld::BoxOf(r, s, sw, sh, px)) { box[0] = box[1] = box[2] = box[3] = 0.0f; return; }
    const float read[4] = {px[0] / sw, px[1] / sh, px[2] / sw, px[3] / sh};
    static float widest = 0.0f;   // the widest plate read (uv): a new target's box that wide until its own rect is read
    if (plateworld::SanePlate(r)) widest = (std::max)(widest, read[2] - read[0]);
    uintptr_t readUnit = 0;
    float atRead[2] = {};
    const bool atReadOk = gamestate::PlateReadAt(&readUnit, atRead);
    plateworld::Follow f;
    if (!plateworld::GetFollow(&f, GetTickCount64())) f = plateworld::Follow{};
    plateworld::FollowBox(read, readUnit, atRead, atReadOk, f, widest, box);
    static int told = 0;
    static int32_t last[4] = {};
    if (told < 8 && memcmp(last, r, sizeof last) != 0) {
        ++told;
        memcpy(last, r, sizeof last);
        LogF("vrcam: the monster's name plate %d,%d %dx%d (layout) x scale %.3f -> %.0f,%.0f %.0fx%.0f on %ux%u; following the hook's anchor: %s (%d answers, %+.0f,%+.0f px since the read)",
             r[0], r[1], r[2], r[3], s, px[0], px[1], px[2] - px[0], px[3] - px[1], sw, sh, atReadOk && f.n > 0 ? "yes" : "no", f.n,
             f.n > 0 ? f.px[0][0] - atRead[0] : 0.0f, f.n > 0 ? f.px[0][1] - atRead[1] : 0.0f);
    }
}
// [hud] monster_bar_room (vr/plate_world.h), every frame: is the world bar in force (ticked, F4, native OpenXR, a game, no menu), where the plate stands in the
// layer for the cut (hud.cpp) - only when the game has hung it on the fixed point it was given (a test of test-54: the plate's widgets read the game's default,
// 0,-6 1000x58, and that strip of the picture was cut) - and where the hook's latest record of the targeted monster puts its head in the head's frame (for
// xr.cpp's HangPlate). One line in the log for each target: where its bar went, or why not (plateworld::Why).
void PlateWorldTick(ULONGLONG nowMs) {
#if D2RVR_FIRST_PERSON
    static plateworld::Tracker toldTarget, toldState;
    static ULONGLONG lastSeenMs = 0;
    xr::Eyes xe{};
    const bool wanted = plateworld::Wanted();
    int off = 0;   // why it is not in force (0: it is)
    if (!wanted) off = 1;
    else if (!g_enabled.load()) off = 2;
    else if (!g_inWorld.load()) off = 3;
    else if (!FullBody()) off = 4;
    else if (gamestate::MenuOpen()) off = 5;
    else if (!NativeEyes(&xe)) off = 6;
    static const char* const kOff[] = {"in force", "", "the mod is off", "no game yet (a menu or the title)", "not the F4 view (the game's own plate in every other view)",
                                       "a panel is open", "no native OpenXR eyes this frame"};
    const bool active = off == 0;
    if (wanted && toldState.Should(0, off, nowMs)) LogF("vrcam: monster_bar_room - %s%s", off ? "not in force now: " : "", kOff[off]);
    plateworld::SetActive(active);
    {   // the targeted monster's ground distance from the hero's feet (monster_far, monster_depth_body, the bar's size): from the hook's latest note
        plateworld::Seen ts;
        float hero[3], units = 0.0f, metres = 0.0f;
        if (plateworld::Tracking() || active) {
            if (plateworld::GetSeen(&ts, nowMs) && ts.footOk && LookAtNow(hero) &&
                plateworld::GroundDistance(ts.foot, hero, std::max(1.0f, ViewHeight()) / UserEyeHeightM(), &units, &metres))
                plateworld::SetTarget(metres, units, nowMs);
        }
    }
    // The plate is cut out of the picture only while the hook has a monster to hang it on (a record fresh) and the game has hung the plate on the point it was
    // given: without one - no ray, no foot, nothing targeted, the mouse layout - the game's plate is left where it is and is not lost.
    plateworld::Seen sn;
    const bool seen = active && plateworld::GetSeen(&sn, nowMs);
    plateworld::Capture c;
    const bool recorded = seen && sn.why == plateworld::Why::Hung && plateworld::GetCapture(&c, nowMs) && c.unit == sn.unit;
    int32_t rect[4] = {}, anchor[2] = {};
    const bool rectRead = recorded && gamestate::PlateRect(rect) && plateworld::SanePlate(rect);   // (a plate read lately: no target, no plate; and sized by a name)
    float s = g_uiScaleSeen.load();
    if (!(s > 0.05f && s < 20.0f) && Matches(RVA_UI_SCALE, kSigUiScale, sizeof kSigUiScale)) s = ((UiScaleFn)d2rsig::Addr(RVA_UI_SCALE))();
    const bool sOk = s > 0.05f && s < 20.0f;
    const float su = sOk ? s : (recorded ? c.view[1] / 2160.0f : 0.0f);
    const bool havePlate = rectRead && gamestate::PlateAnchor(anchor);
    const bool anchored = havePlate && plateworld::AnchorAtFixed(anchor, c.fixed, c.view, su);
    hud::SetPlateCut(anchored, rect, sOk ? s : 0.0f);
    if (plateworld::LiftWanted()) {   // [hud] monster_over_head: what the hook needs to lift the game's plate (the floor, the scale) and the game's own gap under it
        plateworld::SetWorld(g_heroPosNow[1] / (std::max(1.0f, ViewHeight()) / UserEyeHeightM()), std::max(1.0f, ViewHeight()) / UserEyeHeightM());
        int32_t pr[4], pa[2];
        if (sOk && gamestate::PlateRect(pr) && gamestate::PlateAnchor(pa) && plateworld::SanePlate(pr))
            plateworld::SetGameGap(static_cast<float>(pa[1] - (pr[1] + pr[3])) * s);   // the anchor's units under the plate's lower edge, in pixels
    }
    bool placed = false;
    plateworld::Anchor a;
    if (recorded) {
        bladeshape::HeadFrame view;
        {
            AcquireSRWLockShared(&g_stickLock); const StickFrame sf = g_stickFrame; ReleaseSRWLockShared(&g_stickLock);
            view.ok = sf.valid && g_eyeOk.load();
            for (int i = 0; i < 3; ++i) { view.right[i] = sf.camR[i]; view.fwd[i] = sf.camF[i]; view.eyeWorld[i] = g_eyeWorld[i].load(); }
        }
        view.upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
        float tm = 0.0f, tu = 0.0f;   // [hud] monster_far: a far monster's bar this share of its size (the distance rule of the item labels)
        const float farK = plateworld::GetTarget(&tm, &tu, nowMs) ? platelook::DistanceK(tm, g_set.plateFar.load()) : 1.0f;
        placed = plateworld::MakeAnchor(c, view, g_heroPosNow[1] / view.upm, plateworld::S().gapCm.load(), plateworld::S().sizePct.load() * farK, nowMs, &a);
        if (placed) plateworld::SetAnchor(a);
    }
    // One line for each target: where its bar went, or why not.
    if (seen) lastSeenMs = nowMs;
    else if (lastSeenMs && nowMs - lastSeenMs > 400) toldTarget.Gone();
    if (seen && (sn.why != plateworld::Why::Hung || recorded)) {
        const int pad = panelinput::PadLayout();
        const plateworld::Why why = plateworld::Resolve(sn.why, rectRead, anchored, pad, placed);
        if (toldTarget.Should(sn.unit, (int)why, nowMs)) {
            char extra[200] = "";
            if (why == plateworld::Why::Hung) {
                snprintf(extra, sizeof extra, " (%.0f cm over the head, %.1f m away; the plate %dx%d layout units)", a.gapM * 100.0f, a.distM, rect[2], rect[3]);
            } else if (why == plateworld::Why::NotAnchored || why == plateworld::Why::MouseLayout) {
                float want[2] = {};
                plateworld::ExpectedAnchor(c.fixed, c.view, su, want);
                if (havePlate) snprintf(extra, sizeof extra, " (anchor %d,%d layout units, the fixed point is at %.0f,%.0f; layout %s)", anchor[0], anchor[1], want[0], want[1],
                                        pad == 1 ? "controller" : pad == 0 ? "mouse" : "not known");
                else snprintf(extra, sizeof extra, " (no anchor widget read; layout %s)", pad == 1 ? "controller" : pad == 0 ? "mouse" : "not known");
            }
            LogF("vrcam: monster_bar_room - target class %u (unit %p): %s%s", sn.cls, (void*)sn.unit, plateworld::WhyText(why), extra);
        }
    }
#else
    (void)nowMs;
#endif
}
// [hud] notice_card (vr/notice_card.h), every frame: is the card in force (ticked, F4, a game, no menu, native OpenXR eyes) - xr.cpp draws it only
// then - and, while the game's notice panel is open, its box for the cut (hud.cpp), from the panel's widget rect read a few times a second. Unticked
// or not in force: nothing is cut, the notice is where it always was. One line in the log for each change of that.
void NoticeCardTick(ULONGLONG nowMs) {
#if D2RVR_FIRST_PERSON
    xr::Eyes xe{};
    const bool wanted = noticecard::Wanted();
    int off = 0;   // why it is not in force (0: it is)
    if (!wanted) off = 1;
    else if (!g_enabled.load()) off = 2;
    else if (!g_inWorld.load()) off = 3;
    else if (!FullBody() || !viewgate::F4()) off = 4;
    else if (gamestate::MenuOpen()) off = 5;
    else if (!NativeEyes(&xe)) off = 6;
    static int toldOff = -1;
    if (wanted && off != toldOff) {
        static const char* const kOff[] = {"in force", "", "the mod is off", "no game yet (a menu or the title)", "not the F4 view (the game's own notice in every other view)",
                                           "a panel is open", "no native OpenXR eyes this frame"};
        LogF("vrcam: notice_card - %s%s", off ? "not in force now: " : "", kOff[off]);
    }
    toldOff = wanted ? off : -1;
    const bool active = off == 0;
    noticecard::SetActive(active);
    const bool open = active && noticecard::PanelOpen();
    if (open) {
        static ULONGLONG nextRead = 0;
        if (nowMs >= nextRead) { nextRead = nowMs + 250; gamestate::PollNotice(); }
    }
    int32_t rect[4] = {};
    const bool read = open && gamestate::NoticeRect(rect);
    float s = g_uiScaleSeen.load();
    if (!(s > 0.05f && s < 20.0f) && Matches(RVA_UI_SCALE, kSigUiScale, sizeof kSigUiScale)) s = ((UiScaleFn)d2rsig::Addr(RVA_UI_SCALE))();
    hud::SetNoticeCut(open, read ? rect : nullptr, s > 0.05f && s < 20.0f ? s : 0.0f);
#else
    (void)nowMs;
#endif
}
// The allies' portraits' corner as uv of the screen (2026-10-12). The game draws the mercenary's and the party's
// portraits itself, at the top left - no widget of a layout - and the next thing down that side is the chat
// (HUDMessagePanelHD.json: 40, 910 in the layout's units, off the interface area's top left). So the corner is
// from the screen's top left to 420 units right of the area's left and 900 units under its top: the portraits
// and their names, a few of them under one another. The area: the game's interface scale s (PlateBoxUv) is the
// screen's height over 2160 times the Safe Screen Percent, and the area stands in the middle of the screen.
void AlliesBoxUv(uint32_t sw, uint32_t sh, float box[4]) {
    float s = g_uiScaleSeen.load();
    if (!(s > 0.05f && s < 20.0f) && Matches(RVA_UI_SCALE, kSigUiScale, sizeof kSigUiScale)) s = ((UiScaleFn)d2rsig::Addr(RVA_UI_SCALE))();
    if (!(s > 0.05f && s < 20.0f)) s = sh / 2160.0f;
    const float safe = std::clamp(s * 2160.0f / sh, 0.3f, 1.0f);
    const float left = 0.5f * sw * (1.0f - safe), top = 0.5f * sh * (1.0f - safe);
    box[0] = 0.0f; box[1] = 0.0f;
    box[2] = std::clamp((left + 420.0f * s) / sw, 0.0f, 0.5f); box[3] = std::clamp((top + 900.0f * s) / sh, 0.0f, 0.75f);
    static float told = 0.0f;
    if (std::abs(told - s) > 0.001f) {
        told = s;
        LogF("vrcam: the allies' portraits - the corner 0,0 - %.0f,%.0f on %ux%u (interface scale %.3f, its area %.0f%%), moved %+.0f%% across, %+.0f%% up",
             box[2] * sw, box[3] * sh, sw, sh, s, safe * 100.0f, g_set.alliesX.load() * 100.0f, -g_set.alliesY.load() * 100.0f);
    }
}
// [hud] durability_*: the item-condition figure's box as uv of the interface's layer, from the game's own widget (vr/durability_figure.h BoxUv: its rect off the
// top right corner of the interface area; where it was seen when the widget cannot be read). False: no figure now (nothing worn out), no box.
bool DurabilityBoxUv(uint32_t sw, uint32_t sh, float box[4]) {
    float s = g_uiScaleSeen.load();
    if (!(s > 0.05f && s < 20.0f) && Matches(RVA_UI_SCALE, kSigUiScale, sizeof kSigUiScale)) s = ((UiScaleFn)d2rsig::Addr(RVA_UI_SCALE))();
    if (!(s > 0.05f && s < 20.0f)) s = sh / 2160.0f;
    int32_t panel[4] = {}, widget[4] = {};
    bool widgetRead = false;
    if (!gamestate::DurabilityRect(panel, widget, &widgetRead)) widgetRead = false;   // (not read lately: where it was seen)
    bool fromWidget = false;
    const bool ok = durab::BoxUv(widgetRead, panel, widget, s, sw, sh, box, &fromWidget);
    static int told = 0;
    static float last[4] = {};
    static bool lastOk = false;
    if (told < 8 && (ok != lastOk || (ok && memcmp(last, box, sizeof last) != 0))) {
        ++told;
        lastOk = ok;
        if (ok) {
            memcpy(last, box, sizeof last);
            LogF("vrcam: item-condition figure - box %.0f,%.0f - %.0f,%.0f px on %ux%u (%s; panel %d,%d %dx%d, widget %d,%d %dx%d; interface scale %.3f), moved %+.1f%% across, %+.1f%% down, size %.0f%%%s",
                 box[0] * sw, box[1] * sh, box[2] * sw, box[3] * sh, sw, sh, fromWidget ? "from the widget's rect" : widgetRead ? "the widget's rect is not a figure's: where it was seen" : "the widget not read: where it was seen",
                 panel[0], panel[1], panel[2], panel[3], widget[0], widget[1], widget[2], widget[3], s, g_set.durX.load() * 100.0f, g_set.durY.load() * 100.0f,
                 g_set.durScale.load() * 100.0f, g_set.durHide.load() ? ", hidden" : "");
        } else {
            Log("vrcam: item-condition figure - none now (the widget has no size: nothing worn out)");
        }
    }
    return ok;
}
template <class RT> void SetInt(RT* rt, const char* name, int v) {
    const reshade::api::effect_uniform_variable u = rt->find_uniform_variable("D2R_DepthFog.fx", name);
    if (u.handle) rt->set_uniform_value_int(u, &v, 1);
}
// [hud] labels_depth_body: this frame's item labels for the effect - each one's box (uv of the interface's
// layer) and its shift per eye, its item at its own depth against where the game leaves the label (the
// convergence; with native OpenXR's eyes, infinity): 0.5 * M[0] * half the eyes' gap * (1/z - 1/conv), z the
// eyes' distance to the item (along the ground off the hero's feet, the camera's height above them) - times
// labels_depth_body, plus labels_near_body for all. The right eye's present (replayed) has no layout of its
// own: the left's labels stay for it.
template <class RT> void LabelDepthUniforms(RT* rt, uint64_t layerSrv, bool on, float base) {
    static LabelDepth shown[kLabelDepthMax];
    static int shownN = 0;
    static ULONGLONG at = 0;
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&g_labelDepthLock);
    if (g_labelDepthNewN > 0) {
        memcpy(shown, g_labelDepthNew, sizeof(LabelDepth) * g_labelDepthNewN);
        shownN = g_labelDepthNewN;
        g_labelDepthNewN = 0;
        at = now;
    } else if (now - at > 250) shownN = 0;   // none laid out lately: none shown
    ReleaseSRWLockExclusive(&g_labelDepthLock);
    int n = 0;
    float boxes[kLabelDepthMax * 4] = {}, shifts[kLabelDepthMax] = {}, turnH[kLabelDepthMax] = {};
    // eyes by turns, or each eye's own layout (LayerOwnEye): each label's half-disparity at its item's depth (D2R_DepthFog.fx LabelTurnH)
    const bool turns = replay::OverlaysWanted() || LayerOwnEye();
    if (on && layerSrv) {
        // the layer's size: ReShade's view, or (no ReShade) the stand-in's - hud.cpp's copy is made through it (vr/native_rsapi.h). Without
        // ReShade this was left out ("no view of the interface's layer") from before hud.cpp ran there: no label had its box, so by turns each
        // item label on the ground jumped by its whole disparity between the frames each eye was drawn and made (Quest 2 player, 2026-10-26:
        // "item text on ground ... pop up on each eye separately and blink back and forth")
        float W = 0.0f, H = 0.0f;
        if constexpr (std::is_same_v<RT, reshade::api::effect_runtime>) {
            reshade::api::device* dev = rt->get_device();
            const reshade::api::resource_desc d = dev->get_resource_desc(dev->get_resource_from_view(reshade::api::resource_view{layerSrv}));
            W = (float)d.texture.width;
            H = (float)d.texture.height;
        } else {
            uint32_t fmt = 0;
            if (ID3D12Resource* r = nrs::ResourceOfView(layerSrv, &fmt)) {
                const D3D12_RESOURCE_DESC d = r->GetDesc();
                W = (float)d.Width;
                H = (float)d.Height;
            }
        }
        const float m0 = g_projSx.load(), half = 0.5f * g_stereoIpd.load(), conv = g_stereoConv.load();
        const float eyeH = std::max(1.0f, ViewHeight()), k = g_set.labelsDepthBody.load();
        if (W > 0.0f && H > 0.0f && m0 > 0.0f && half > 0.0f) {
            for (int i = 0; i < shownN; ++i) {
                const LabelDepth& l = shown[i];
                boxes[n * 4 + 0] = l.x / W;
                boxes[n * 4 + 1] = l.y / H;
                boxes[n * 4 + 2] = (l.x + l.w) / W;
                boxes[n * 4 + 3] = (l.y + l.h) / H;
                shifts[n] = base + platelook::DepthShift(l.dist, eyeH, m0, half, conv, k);   // (the rule the monster plate has too: monster_depth_body)
                turnH[n] = turns ? platelook::TurnsHalf(l.dist, eyeH, m0, half, conv) : 0.0f;
                ++n;
            }
            static int told = 0;
            if (n && told < 3 && ++told)
                LogF("vrcam: item labels at their items' depth - %d labels, the first %d,%d %dx%d on a %.0fx%.0f layer, %.1f units off: "
                     "%.4f uv per eye (M0 %.3f, eyes %.3f apart, convergence %.1f)", n, shown[0].x, shown[0].y, shown[0].w, shown[0].h,
                     W, H, shown[0].dist, shifts[0], m0, 2.0f * half, conv);
        }
    }
    SetInt(rt, "LabelCount", n);
    if (n) {
        SetFloats(rt, "LabelBoxes", boxes, (size_t)n * 4);
        SetFloats(rt, "LabelShifts", shifts, (size_t)n);
        SetFloats(rt, "LabelTurnH", turnH, (size_t)n);
    }
}
template <class RT> void SetBool(RT* rt, const char* name, bool v) {
    const reshade::api::effect_uniform_variable u = rt->find_uniform_variable("D2R_DepthFog.fx", name);
    if (u.handle) rt->set_uniform_value_bool(u, &v, 1);
}
// [hud] low_hp_vignette (vr/low_hp.h): this pair's edge tint for D2R_DepthFog.fx, the same in both eyes. Off, or a life
// that is fine: nothing is sent (and a 0 once after the last tint, so the effect's picture is the game's again).
template <class RT> void LowHpUniforms(RT* rt) {
    static bool sent = false;
    lowhp::Uniforms u;
    const bool on = lowhp::Current(&u);
    if (!on && !sent) return;
    if (on && rt->find_uniform_variable("D2R_DepthFog.fx", "LowHpAlpha").handle == 0) {
        static bool told = false;
        if (!told) { told = true; Log("vrcam: low life vignette - D2R_DepthFog.fx has no LowHpAlpha: it is an older one, copy the new one in (the vignette needs the DLL and the effect together)"); }
        return;
    }
    if (on && u.fov[0] > 0.0f) {
        static bool toldFov = false;
        if (!toldFov && rt->find_uniform_variable("D2R_DepthFog.fx", "LowHpFov").handle == 0) {
            toldFov = true;
            Log("vrcam: low life vignette - D2R_DepthFog.fx has no LowHpFov: an older one, so the vignette's size follows the picture's own proportions, not the view's angle (copy the new effect in)");
        }
    }
    sent = on;
    SetFloat(rt, "LowHpAlpha", on ? u.alpha : 0.0f);
    if (on) {
        SetFloats(rt, "LowHpShape", u.shape, 4);
        SetFloats(rt, "LowHpColor", u.color, 3);
        SetFloats(rt, "LowHpFov", u.fov, 2);
    }
}

// Not in the main menu: its scene got the fog of act 2 (its biome is act2_frontend_biome).
// The HUD's per-eye shift (D2R_DepthFog.fx PS_Hud): only in AFR stereo, in a game area.
bool HudWanted() { return AfrOn() && g_set.uiShift.load() > 0.0f && g_inWorld.load(); }

// [debug] stab_debug (vr/stab_debug.h): the weight model's word of the stab, kept for the readout over the weapon hand (the thrust debug's plate) and the marks
// drawn with the melee boxes. The weight model's thread writes, the render thread reads.
SRWLOCK g_stabDbgLock = SRWLOCK_INIT;
stabdbg::Board g_stabDbg;
void StabNoteSink(const stabdbg::Note& n) {
    AcquireSRWLockExclusive(&g_stabDbgLock);
    g_stabDbg.Take(n);
    const stabdbg::Note last = g_stabDbg.Last();
    ReleaseSRWLockExclusive(&g_stabDbgLock);
    thrustdbg::Line lines[thrustdebugwire::kMaxLines];
    const size_t nl = stabdbg::Lines(last, lines, std::size(lines));
    thrustdebugwire::SetStabLines(lines, nl, last.side);
}
int StabMarks(uint64_t nowMs, const float eyeWorld[3], float (*a)[4], float (*b)[4], int max) {
    if (!g_set.stabDebug.load()) return 0;
    AcquireSRWLockShared(&g_stabDbgLock);
    const stabdbg::Note n = g_stabDbg.Last();
    const bool has = g_stabDbg.Has(nowMs);
    ReleaseSRWLockShared(&g_stabDbgLock);
    return has ? stabdbg::Marks(n, nowMs, n.upm, eyeWorld, a, b, max) : 0;
}

// The crossbow held as the staff, its shot line ([debug] phantom_ray) or its bone's axes ([debug] bone_axes) drawn.
#if D2RVR_FIRST_PERSON
bool XbowDrawable() { return g_enabled.load() && g_inWorld.load() && XbowLikeStaff() && StaffAim(); }
// [debug] bow_vectors: the bow draw's vectors drawn in the headset (bow_draw_wire.cpp builds them, the effect draws them)
bool BowVectorsWanted() { return bowdrawwire::DebugWanted() && g_enabled.load() && g_inWorld.load(); }
// [debug] bow_aim_ray: the arrow's ray (bow_draw_wire.cpp builds its line, the effect draws it: green / orange, a ball where it ends)
bool BowAimRayWanted() { return bowdrawwire::AimRayWanted() && g_enabled.load() && g_inWorld.load(); }
// [bow] pitch_aim: the ray ends where the shot's line meets what it goes to - the monster's body (a red ball) or the ground as the scene knows it (a full ball), up to
// the shot's reach - instead of the flat floor at the user's eye height or 8 m out. The line meets nothing: as it was. In the hands' frame (start s, direction d).
void PitchRayEnd(const bowtouch::Vec& s, const bowtouch::Vec& d, bowtouch::AimRay* ray) {
    if (!bowpitchwire::On() || !ray) return;
    AcquireSRWLockShared(&g_stickLock); const StickFrame f = g_stickFrame; ReleaseSRWLockShared(&g_stickLock);
    const float sh[3] = {s.x, s.y, s.z}, dh[3] = {d.x, d.y, d.z};
    float o[3], w[3];
    bowpitch::Result r;
    if (!HandsLineToRender(f, sh, dh, o, w) || !bowpitchwire::Meets(o, w, &r) || r.hit == bowpitch::Hit::None) return;
    monsterbodies::View v;
    for (int i = 0; i < 3; ++i) { v.eye[i] = g_eyeWorld[i].load(); v.camR[i] = f.camR[i]; v.camF[i] = f.camF[i]; }
    v.upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
    const float at[3] = {r.at.x, r.at.y, r.at.z};
    float h[3];
    if (!monsterbodies::RenderToHead(v, at, h)) return;
    ray->b = {h[0], h[1], h[2]};
    ray->floor = r.hit == bowpitch::Hit::Floor;
    ray->target = r.hit == bowpitch::Hit::Monster;
}
// [bow] quiver: the loaded skill's glow on the nocked arrow (bow_quiver_wire.cpp builds it, the effect draws it as it does the vectors above)
bool BowQuiverWanted() { return bowquiverwire::GlowWanted() && g_enabled.load() && g_inWorld.load(); }
// [bow] quiver_orbs: the energy orbs along the bow's stave (bow_quiver_wire.cpp lays them out, the effect's QuiverOrbs draws them)
bool BowOrbsWanted() { return bowquiverwire::OrbsWanted() && g_enabled.load() && g_inWorld.load(); }
bool BowArrowFxWanted() { return bowquiverwire::ArrowFxWanted() && g_enabled.load() && g_inWorld.load(); }
// the line the attack goes along, for any weapon in hand (only where HandRay has one)
bool PhantomWanted() { return (g_set.phantomRay.load() || g_set.boneAxes.load()) && g_enabled.load() && g_inWorld.load(); }
// the axes of whatever weapon is in hand, not only the crossbow's (2026-10-06)
bool BoneAxesWanted() { return g_set.boneAxes.load() && g_enabled.load() && g_inWorld.load(); }
// [debug] xbow_barrel_line: the crossbow's barrel as drawn and the left palm's place under it, VR first person (F4), the gun's hold
bool XbowBarrelWanted() { return g_set.xbowBarrelLine.load() && g_enabled.load() && g_inWorld.load() && FullBody() && !ThirdPerson() && XbowLikeStaff(); }
// [debug] spell_aim_ray: the ray a spell is aimed along, VR first person (F4), no bow, crossbow or thrown weapon in hand (vr/spell_aim_model.h)
bool SpellRayWanted() { return g_set.spellAimRay.load() && g_enabled.load() && g_inWorld.load() && FullBody() && !ThirdPerson() && !BowInHand() && !ThrownInHand(); }
// [debug] melee_boxes: the capsules hit by touch tests, only in VR first person (F4)
bool MeleeBoxesWanted() { return meleeboxes::Wanted() && g_enabled.load() && g_inWorld.load() && FullBody() && !ThirdPerson(); }
// [multiplayer] pose_share: our pose out, the other players' drawn and met (PoseShareTick, BodiesWithPlayers, ...): vr/mp/pose_share.cpp
// [gameplay] teleport_circle, with an effect older than the beam's own drawing (PaOn): the arc and the circle while a point skill's button is held - the arc as thin red tubes from the staff's tip down to the ground, a ring
// of them on the hero's ground round the circle's place and a ball in its middle, in world units from where the hands hang (the capsules' frame, each eye from its
// own camera). 0 when none is up.
bool g_paFxHas = false;   // the effect in use has the beam's own drawing (PaOn): the capsules below stand in only for an older one (the render thread)
int PointCastMarks(const float eyeAt[3], float (*a)[4], float (*b)[4], int max) {
    pointcast::Arc arc;
    if (max <= 0 || !pointwire::Shown(&arc)) return 0;
    const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
    const float tube = (float)meleeboxes::TouchKind(meleeboxes::kCRed, meleeboxes::kTube), ball = (float)meleeboxes::TouchKind(meleeboxes::kCRed, meleeboxes::kBall);
    int n = 0;
    auto put = [&](const float* p, const float* q, float r, float kind) {
        if (n >= max) return;
        a[n][0] = p[0] - eyeAt[0]; a[n][1] = p[1] - eyeAt[1]; a[n][2] = p[2] - eyeAt[2]; a[n][3] = r;
        b[n][0] = q[0] - eyeAt[0]; b[n][1] = q[1] - eyeAt[1]; b[n][2] = q[2] - eyeAt[2]; b[n][3] = kind;
        ++n;
    };
    for (int i = 0; i + 1 < arc.n; ++i) {
        const float p[3] = {arc.pts[i].x, arc.pts[i].y, arc.pts[i].z}, q[3] = {arc.pts[i + 1].x, arc.pts[i + 1].y, arc.pts[i + 1].z};
        put(p, q, 0.015f * upm, tube);
    }
    const float L[3] = {g_gazeLook[0].load(), g_gazeLook[1].load(), g_gazeLook[2].load()};
    const float c[3] = {L[0] + arc.land.x, L[1] + 0.03f * upm, L[2] + arc.land.z};
    constexpr int kSegs = 20;
    float ra[kSegs][3], rb[kSegs][3];
    const int rn = pointcast::Ring(c[0], c[1], c[2], 0.5f * upm, kSegs, ra, rb, kSegs);
    for (int i = 0; i < rn; ++i) put(ra[i], rb[i], 0.025f * upm, tube);
    const float top[3] = {c[0], c[1] + 0.02f * upm, c[2]};   // the middle: a short stub (the shader draws no capsule of zero length)
    put(c, top, 0.06f * upm, ball);
    return n;
}
// [debug] skill_triggers: what casts a skill (the shield bash -> Smite), the same
bool SkillTriggersWanted() { return skilltriggers::Wanted() && g_enabled.load() && g_inWorld.load() && FullBody() && !ThirdPerson(); }
// [hud] hand_vitals_style = orbs: the life and mana balls on the wrists, drawn by the effect, only in VR first person (F4)
bool HandOrbsWanted() { return handvitals::OrbsOn() && g_enabled.load() && g_inWorld.load() && FullBody() && !ThirdPerson(); }
// [hud] merc_tint: the wounded merc tinted red, drawn by the effect, only in VR first person (F4)
bool MercTintWanted() { return merctint::Enabled() && g_enabled.load() && g_inWorld.load() && FullBody() && !ThirdPerson(); }
#else
bool PhantomWanted() { return false; }
bool BoneAxesWanted() { return false; }
bool XbowBarrelWanted() { return false; }
bool SpellRayWanted() { return false; }
bool BowVectorsWanted() { return false; }
bool BowAimRayWanted() { return false; }
bool BowQuiverWanted() { return false; }
bool BowOrbsWanted() { return false; }
bool BowArrowFxWanted() { return false; }
bool MeleeBoxesWanted() { return false; }
bool SkillTriggersWanted() { return false; }
bool HandOrbsWanted() { return false; }
bool MercTintWanted() { return false; }
#endif

// The frame stamp strip (D2R_DepthFog.fx PS_Stamp): whenever the camera turns
// with the head, so that FlatVR can place its screen at the pose of the frame.
// [stereo] pipeline_depth where the engine pipelines: one picture a game frame (mono, the
// depth, eyes by turns). A pair from one game frame presents each eye from inside its own
// pass, the view just built: 0 there.
int PipeDepth() { return PairWanted() ? 0 : g_set.pipelineDepth.load(); }
SkyView SkyBack(int e) {   // under g_skyLock
    const int d = PipeDepth();
    if (d <= 0) return g_skyView[e];
    const SkyView& h = g_skyHist[e][(g_skyHistAt[e] - d) & 3];
    return h.axes_ok && h.proj_ok ? h : g_skyView[e];
}
// The frame's stamp in the picture is read by FlatVR alone: none while native OpenXR runs (FlatVR stopped) - a full-screen pass and a copy of
// the picture each present for 48 pixels nobody reads, and they showed in the headset's corner (PC2, 2026-10-26: PS_StampR 0.12 ms a frame)
bool StampWanted() { return g_enabled.load() && g_afrBlock && g_set.stamps.load() && g_set.stampPixels.load() && !xr::Running(); }
// [hud] low_hp_vignette rides on D2R_DepthFog.fx: the effect stays on for it in VR first person on native OpenXR.
bool LowHpWanted() { return lowhp::Enabled() && g_enabled.load() && FullBody() && !ThirdPerson() && xr::Running() && g_inWorld.load(); }

// Who draws the sky and the fog (OurFogWanted, FogCannot, FogWanted, OurSkyWanted): vr/sky/sky_choice.cpp
// The fog's colour as the post pass lays it: the act's own (its day colour, darker at night with the sky), a cave's own underground;
// false without one (the post pass then takes the sky's horizon; `rgb` a dark grey - what [fog] game_fog gives the game then)
bool FogColourNow(float rgb[3]) {
    static const float kDark[3] = {0.02f, 0.02f, 0.025f};
    SkyAct fa{};
    CaveFog cave{};
    AcquireSRWLockShared(&g_skyCfgLock);
    if (const int a = g_fogAct.load(); a > 0) fa = g_skyAct[a];
    cave = g_caveFog;
    ReleaseSRWLockShared(&g_skyCfgLock);
    if (g_underground.load() && cave.set) { fa.fogSet = true; memcpy(fa.fog, cave.rgb, sizeof fa.fog); }   // caves and dungeons: their own
    else if (fa.fogSet && !(g_set.fogKeepsColour.load() && gamefogvr::Active())) {   // in the open the act's fog (its day colour) darkens at night with the sky
        // ([fog] fog_keeps_colour: not while the game draws the fog - Serg on 0.188.4, "прозрачнее, а не чернее": the far fades into a light
        // haze of his color_<act>, never toward black; the game's light read 32 - "end of dawn, day" - put act 2's fog at x0.25. Our own
        // effect's fog as it was: Serg, "the old one was good")
        const float m = DayNightMul();
        for (float& f : fa.fog) f *= m;
    }
    memcpy(rgb, fa.fogSet ? fa.fog : kDark, 3 * sizeof(float));
    return fa.fogSet;
}
// The table (F5): the void black, the game lifted off black - D2R_DepthFog.fx TableKey.
bool TableKeyWanted() { return TableView() && g_inWorld.load() && !gamestate::MenuOpen(); }
// ... and the cave ceiling (CeilWanted, GameCeilWanted): vr/sky/sky_choice.cpp

// The pictures of each act as the shader's preprocessor definitions; only what
// differs is set, since every change recompiles the effect. True if it did.
// Done here, after the effect pass, like the technique switch below.
template <class RT> bool ApplySkyPictures(RT* rt) {
    static uint32_t applied = ~0u;
    const uint32_t gen = g_skyCfgGen.load();
    if (gen == applied) return false;
    applied = gen;
    bool set = false;
    for (int a = 1; a <= 6; ++a) {
        AcquireSRWLockShared(&g_skyCfgLock); const SkyAct act = g_skyAct[a]; ReleaseSRWLockShared(&g_skyCfgLock);
        for (int k = 0; k < 4; ++k) {   // the day band and cap, then the night ones
            char name[32];
            static const char* const kDef[4] = {"D2R_SKY_%s", "D2R_SKYCAP_%s", "D2R_SKYN_%s", "D2R_SKYCAPN_%s"};
            snprintf(name, sizeof name, kDef[k], kActDefs[a]);
            const bool ok = k == 0 ? act.bandOk : k == 1 ? act.capOk : act.nightOk;
            const std::string& file = k == 0 ? act.band : k == 1 ? act.cap : k == 2 ? act.nightBand : act.nightCap;
            // a slot without a picture keeps the act 1 file: never sampled, nothing to miss
            const std::string want = "\"" + (ok ? file : std::string(k & 1 ? "D2R_Sky_ours/D2R_SkyCap_act1.png" : "D2R_Sky_ours/D2R_Sky_act1.png")) + "\"";
            char have[1024] = {};
            size_t size = sizeof have;
            if (rt->get_preprocessor_definition_for_effect("D2R_DepthFog.fx", name, have, &size) && want == have) continue;
            rt->set_preprocessor_definition_for_effect("D2R_DepthFog.fx", name, want.c_str());
            LogF("vrcam: sky picture %s = %s", name, want.c_str());
            set = true;
        }
    }
    {   // the cave ceiling's pictures, a slot each; an empty slot keeps slot 1's (never sampled)
        std::string file[kCeilSlots + 1];
        bool ok[kCeilSlots + 1] = {};
        AcquireSRWLockShared(&g_skyCfgLock);
        for (int k = 1; k <= kCeilSlots; ++k) { file[k] = g_ceilSlot[k]; ok[k] = g_ceilSlotOk[k]; }
        ReleaseSRWLockShared(&g_skyCfgLock);
        // With no picture at all, no file (2026-10-13): the act 1 caves' one, named all the same, is
        // not in the repository, and a source not found fails ReShade's reload. An empty source it skips.
        const std::string spare = ok[1] ? file[1] : std::string();
        for (int k = 1; k <= kCeilSlots; ++k) {
            char name[24];
            snprintf(name, sizeof name, "D2R_CEILING_%d", k);
            const std::string want = "\"" + (ok[k] ? file[k] : spare) + "\"";
            char have[1024] = {};
            size_t size = sizeof have;
            if (rt->get_preprocessor_definition_for_effect("D2R_DepthFog.fx", name, have, &size) && want == have) continue;
            rt->set_preprocessor_definition_for_effect("D2R_DepthFog.fx", name, want.c_str());
            LogF("vrcam: cave ceiling picture %s = %s", name, want.c_str());
            set = true;
        }
    }
    {   // [sky] arcane_sky: its textures compiled in only once both pictures are there - a source not found fails ReShade's reload
        const char* want = g_arcaneReady.load() ? "1" : "0";
        char have[16] = {};
        size_t size = sizeof have;
        const bool known = rt->get_preprocessor_definition_for_effect("D2R_DepthFog.fx", "D2R_ARCANE_SKY", have, &size);
        if (known ? strcmp(have, want) != 0 : want[0] == '1') {   // never set and not wanted: the effect's own 0
            rt->set_preprocessor_definition_for_effect("D2R_DepthFog.fx", "D2R_ARCANE_SKY", want);
            LogF("vrcam: Arcane sky pictures D2R_ARCANE_SKY = %s", want);
            set = true;
        }
    }
    if (set) Forget(nullptr);
    return set;
}

// Switching the technique happens after the effect pass, never inside it:
// turned on from reshade_begin_effects, ReShade went on to draw an effect whose
// resources it had not created yet, and the game died in D3D12 (null+0x19C).
// From here it takes effect on the next frame, with everything in place.
// [stereo] pair_fx_sync: a fence after each pair's effects (pairfx): vr/render/stereo/pair_pace.cpp

// After each present's effects: the techniques switched for the next one, the sky's pictures as definitions. RT: ReShade's runtime or
// [render] no_reshade's own (vr/fx_native.h).
template <class RT> void EffectsFinish(RT* rt) {
    if constexpr (std::is_same_v<RT, reshade::api::effect_runtime>) pairfx::AfterRender(rt);   // [stereo] pair_fx_sync: the fence after this present's effects
    if (const double b = pairtime::g_effBegin.exchange(0.0); b > 0.0) pairtime::g_effSum.store(pairtime::g_effSum.load() + pairtime::UsNow() - b);
    if (ApplySkyPictures(rt)) return;   // the effect reloads with the new pictures
    if (!g_tech[0].handle) g_tech[0] = rt->find_technique("D2R_DepthFog.fx", "D2R_DepthFog");
    if (!g_tech[1].handle) g_tech[1] = rt->find_technique("D2R_DepthFog.fx", "D2R_DepthFog_R");
    replay::g_rightTech = g_tech[1];
    // eyes by turns: the overlays drawn apart (replay::DrawOverlays, or DrawOverlaysNative without ReShade); an older effect has none - drawn
    // in the picture, as before
    for (int e = 0; e < 2; ++e)
        for (int m = 0; m < 2; ++m)   // [0] the eye drawn's, [1] the eye made's
            if (!replay::g_ovTech[m][e].handle)
                replay::g_ovTech[m][e] = rt->find_technique("D2R_DepthFog.fx", m ? (e ? "D2R_OverlayMade_R" : "D2R_OverlayMade") : (e ? "D2R_Overlay_R" : "D2R_Overlay"));
    if (!g_tech[0].handle) { hud::SetPictureReady(false); return; }
    // Off while a menu is open: the fog lies over the whole picture, the
    // inventory and trade panels included, and darkened them.
    const bool want = FogWanted() || OurSkyWanted() || g_arcaneSky.load() || CeilWanted() || HudWanted() || StampWanted() || hud::PictureWanted() || TableKeyWanted() || PhantomWanted() || BoneAxesWanted() || BowVectorsWanted() || BowQuiverWanted() || MercTintWanted() || BowAimRayWanted() || XbowBarrelWanted() || SpellRayWanted() || MeleeBoxesWanted() || SkillTriggersWanted() || HandOrbsWanted() || LowHpWanted() || BowOrbsWanted() || BowArrowFxWanted();
    // The toolbar and the map are taken out of the game's picture to be drawn
    // back at their own size only while this effect runs, and is new enough to.
    {
        const bool effects = rt->get_effects_state(), fresh = rt->find_uniform_variable("D2R_DepthFog.fx", "GameBarOn").handle != 0;
        hud::SetPictureReady(want && effects && fresh);
        static int told = -1;   // why the toolbar can or cannot be moved in the picture, once per change
        const int now = !hud::PictureWanted() ? 0 : !effects ? 1 : !fresh ? 2 : 3;
        if (now != told) {
            told = now;
            static const char* const kWhy[] = {"not wanted (first person, or all sliders 0, or no AFR stereo)",
                                               "NOT possible - ReShade's effects are switched off",
                                               "NOT possible - D2R_DepthFog.fx is old or failed to compile (no GameBarOn)",
                                               "on - taken out and drawn back by D2R_DepthFog.fx"};
            LogF("vrcam: toolbar / labels moved in the picture: %s", kWhy[now]);
        }
    }
    // AFR: frames alternate, the next present is the other eye's. An older
    // shader without the right eye's technique keeps the left one for both.
    const int next = AfrOn() && g_tech[1].handle ? g_fxEye ^ 1 : 0;
    for (int k = 0; k < 2; ++k) {   // the next eye's first: the effect never has none on in between
        const int e = next ^ k;
        if (!g_tech[e].handle) continue;
        const bool on = want && e == next;
        if (rt->get_technique_state(g_tech[e]) != on) rt->set_technique_state(g_tech[e], on);
    }
}

// How much of the screen the game draws its scene at (renderscale): vr/render/depth/scene_depth.cpp

// The camera behind the depth, for FlatVR (depthcam): vr/render/depth/scene_depth.cpp

// [stereo] pace: the presents held to the headset's rate (pace): vr/render/stereo/pair_pace.cpp

// The scene's depth for our effects (OwnDepthNow, DepthEffectsWanted, NativeDepthNow, scenedepth): vr/render/depth/scene_depth.cpp

// Each present, before the real one: ReShade's reshade_present (rt), or [render] no_reshade's own present hook (rt nullptr). wnd: the swap
// chain's window; dev, q: the game's device and the swap chain's queue (nullptr: not D3D12); bb: the back buffer now; sw x sh: its size.
void PresentCommon(reshade::api::effect_runtime* rt, HWND wnd, ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12Resource* bb, uint32_t sw, uint32_t sh) {
    scenedepth::FrameEnd();
    gamefogvr::Update();   // [fog] game_fog: on or off, our numbers for the game's own fog (the game's thread writes them, VisApply)
    // [fog] game_fog_height: the game's own fog pass thinned with height (its pipeline swapped for a patched one; no_reshade's hooks)
    // ([fog] game_fog_mist: the same pass's mist fogged by distance - bound with or without the height thinning)
    {
        // (the thinning is for a sky overhead: underground none - with it the far cave wall above the eyes stood unfogged over the fogged
        // floor, a band at the horizon, PC1 2026-10-27 5.webp)
        const bool height = g_set.fogHeight.load() && !g_underground.load(), mist = g_set.fogMist.load();
        // ([fog] game_fog_caves_black: underground the void past the ceiling's rim left alone - the pass laid its mist there whole)
        const bool voidAlone = g_set.fogCavesBlack.load() && g_underground.load();
        // [fog] game_fog_before_fx: the same fog laid on the scene before the game's transparent resolve (smoke and fire then over it, unfogged);
        // while it is drawn the game's pass leaves the fog's colour out (its mist still fogged)
        // [fog] game_fog_our_curve: our effect's curve (D2R_DepthFog.fx FogAt) - [fog] start / end / curve, underground the caves' start / end
        // / strength - in the game's pass and the step before the effects
        fogheight::Curve curve;
        curve.on = g_set.fogOurCurve.load();
        if (curve.on) {
            const bool under = g_underground.load();
            curve.start = under ? g_set.fogCaveStart.load() : g_set.fogStart.load();
            curve.end = under ? g_set.fogCaveEnd.load() : g_set.fogEnd.load();
            curve.curve = g_set.fogCurve.load();
            curve.strength = under ? g_set.fogCaveStrength.load() : 1.0f;
            curve.ease = g_set.fogEaseIn.load();   // [fog] ease_in (D2R VR Unleashed's)
        }
        fogfx::Params fp;
        fp.on = g_set.fogBeforeFx.load() && gamefogvr::Active() && GameDrawnView();
        fp.ourCurve = curve.on;
        fp.start = curve.start; fp.end = curve.end; fp.curve = curve.curve; fp.strength = curve.strength; fp.ease = curve.ease;
        if (fp.on) {
            const gamefogvr::Fog gf = gamefogvr::Current();
            fp.plane = gf.plane;
            fp.density = gf.density;
            fp.colour[0] = gf.r; fp.colour[1] = gf.g; fp.colour[2] = gf.b;
            fp.nearPlane = g_lastNear.load();
            fp.sx = g_projSx.load();
            fp.sy = g_projSy.load();
            fp.height = height;
            fp.top = FogTopNow();
            fp.soft = g_set.fogSoft.load();
            fp.up[0] = g_viewRightY.load(); fp.up[1] = g_viewUpY.load(); fp.up[2] = g_viewFwdY.load();
        }
        fogfx::Configure(fp);
        fogfx::Frame();
        const bool noFog = fogfx::Drawn();
        fogheight::Frame((height || mist || voidAlone || noFog || curve.on) && gamefogvr::Active() && GameDrawnView(), height ? FogTopNow() : 1.0e9f,
                         height ? g_set.fogSoft.load() : 1.0f, mist, voidAlone, noFog, curve);
    }
    vcache::Apply(g_set.vcache.load(std::memory_order_relaxed), &Log);   // [render] vcache: on the draw thread (its priority); only when it changes
    // [render] fx_fence: the next frame's pass waits for these effects; by turns ([stereo] turn_fx_fence) for its overlays too - marked after them
    const bool markLater = scenedepth::MarkAfterOverlays();
    if (rt && !replay::g_capturing.load() && !markLater) scenedepth::MarkFx(rt);
    static bool firstSeen = false;
    if (!firstSeen) { firstSeen = true; crash::Mark(rt ? "the first picture presented (ReShade's present)" : "the first picture presented (no ReShade: the plugin's present hook)"); }
    // Native OpenXR: the runtime paces the game (xrWaitFrame); a present outside a pair goes
    // to the headset as a flat picture.
    xr::SetWindow(wnd);   // the swap chain's own window: where the pointer is
    if (dev) gpufault::Watch(dev);   // the game's device: its loss is told the moment it happens
    g_swapWnd.store(wnd);
    lightbounds::NoteScreen(sw, sh);   // what the game draws at, and the light grid that makes (a line when it changes)
    // No game area (the title and character screens, loading): the interface stays in the picture. A menu out as
    // a panel when the game was left (Esc, Save and Exit) is not kept there - the draw hook that sets it may not
    // run in the menus, and the main menu came up with no interface at all (2026-10-13).
    if (!g_inWorld.load() || !d2rcam::InWorld()) {
        g_menu3dNow.store(false);
        hud::SetPanelOut(false);
    }
    // FlatVR's laser on the menu panel is the mouse (its reader block): the pad's layout kept under it, as
    // native's LaserStep keeps it - 300 ms a call; parked, the focus is the pad's again
    if (FlatVrLaserWanted()) {
        const int s = hud::FlatVrLaserState();
        if (s == 1) panelinput::OwnPointer();
        else if (s == 2) panelinput::UseController();
    }
    {   // native OpenXR: straight ahead and the body taken again when a game is entered (the hero
        // appears) - the head is wherever it was in the menus; not at an area change, the hero stays
        static bool hadHero = false;
        const bool hero = gamestate::LocalPlayer() != 0;
        if (hero && !hadHero && xr::On()) { Recenter(); Log("vrcam: openxr - a game entered: recentred"); }
        // [flatvr] recenter_on_enter: the same on FlatVR - straight ahead was wherever the headset looked when BodyWalk started
        else if (hero && !hadHero && g_set.flatEnterCentre.load() && g_set.platform.load() == 1) { Recenter(); Log("vrcam: flatvr - a game entered: recentred ([flatvr] recenter_on_enter)"); }
        hadHero = hero;
    }
    // a panel open in a game: the flat picture at the panels' distance from its first frame
    xr::SetPanelOpen(gamestate::MenuOpen() && g_inWorld.load());
    // [openxr] laser: the flat picture's menus - a panel in a game, or the title and character screens
    xr::SetLaserWanted(gamestate::MenuOpen() || !g_inWorld.load());
    FlatRawTick();   // FlatVR: BodyWalk's raw controllers into the plugin's own layout ([direct] flatvr_controls)
    if (xr::On() || xr::Running() || xr::StopPending())
        replay::XrPresent(rt, dev, q, bb);
    else pace::Tick();
    replay::MonitorPresent(rt, wnd, q, bb);   // [render] monitor_eye: the window one eye, after the headset took this one
    if (rt && !replay::g_capturing.load() && markLater) scenedepth::MarkFx(rt);   // after the eye made's overlays (XrPresent)
    const double t = pairtime::UsNow();
    pairtime::g_presentAt.store(t);
    pairtime::g_presents.fetch_add(1, std::memory_order_relaxed);
    drawprof::Tick();
    // the game's window in front at this present (the 10 s line: a player's game ran far better with D2R VR Settings in front of it)
    if (GameFocused()) budgetasks::g_frontPresents.fetch_add(1, std::memory_order_relaxed);
    // Any mode (mono, stereo, menus): the presents a second and the game's busiest
    // threads, every 10 s - the frame budget's hunt (~7 ms a present, 2026-10-08). On a
    // thread of its own: the snapshot of every thread (Toolhelp) held this present up
    // 30-50 ms every 10 s - a frame far too late, a jerk in a head turn (2026-10-09).
    static std::atomic<DWORD> presentThread{0};
    if (!presentThread.load()) {
        presentThread.store(GetCurrentThreadId());
        if (HANDLE h = CreateThread(nullptr, 0, [](void*) -> DWORD {
                double since = pairtime::UsNow();
                uint32_t n0 = pairtime::g_presents.load();
                pairtime::BusyThreads(0.0);
                budgetasks::g_ourThread.store(GetCurrentThreadId());   // (gpufault::VideoMemory's own ask below: not the game's)
                for (;;) {
                    Sleep(10000);
                    const double now = pairtime::UsNow();
                    const uint32_t n = pairtime::g_presents.load();
                    if (!pairtime::g_drawThread.load()) pairtime::g_drawThread.store(presentThread.load());
                    uint64_t vu = 0, vb = 0;
                    char vram[64] = "";
                    if (gpufault::VideoMemory(&vu, &vb)) snprintf(vram, sizeof vram, "; video memory %llu of %llu MiB", (unsigned long long)(vu >> 20), (unsigned long long)(vb >> 20));
                    budgetasks::Install();
                    const uint32_t front = budgetasks::g_frontPresents.exchange(0), asks = budgetasks::g_gameAsks.exchange(0);
                    char more[160] = "";
                    if (n != n0)
                        snprintf(more, sizeof more, "; the game's window in front at %u%% of them; the game asked its video memory budget %u times%s",
                                 (unsigned)(100.0 * front / (n - n0) + 0.5), asks, budgetasks::g_in.load() ? "" : " (not watched)");
                    LogF("vrcam: %.1f presents/s; the game's busiest threads, %% of a core (present thread %lu): %s%s%s",
                         (n - n0) * 1e6 / (now - since), presentThread.load(), pairtime::BusyThreads(now - since).c_str(), vram, more);
                    since = now; n0 = n;
                }
            }, nullptr, 0, nullptr)) CloseHandle(h);
    }
}
void OnReshadePresent(reshade::api::effect_runtime* rt) {
    uint32_t sw = 0, sh = 0;
    rt->get_screenshot_width_and_height(&sw, &sh);
    const bool d3d12 = rt->get_device()->get_api() == reshade::api::device_api::d3d12;
    PresentCommon(rt, (HWND)rt->get_hwnd(), d3d12 ? (ID3D12Device*)rt->get_device()->get_native() : nullptr,
                  d3d12 ? (ID3D12CommandQueue*)rt->get_command_queue()->get_native() : nullptr, (ID3D12Resource*)rt->get_current_back_buffer().handle, sw, sh);
}

// [render] no_reshade (docs/plan_no_reshade.md, step 3): ReShade64.dll is not in the game; the plugin's own hooks (vr/native_d3d12.h) give what
// its events gave native OpenXR eyes by turns - the game's present (PresentCommon, as reshade_present), the scene's depth for the eye made
// (scenedepth: the binds and barriers counted, the copy before the present, as the effects' begin did) and the share of the screen the scene is
// drawn at (renderscale, DLSS). Nothing else: no effect, so no fog, sky, ceiling or overlays; no replay (one pass, two pictures), no light
// bounds, no FlatVR depth, no menu panel (hud's layer).
template <class RT> void EffectsBegin(RT* rt, reshade::api::command_list* cl);   // (below, with the effect's uniforms)
namespace noreshade {
// [render] native_effects (on): D2R_DepthFog.fx run by the plugin itself (vr/fx_native.h) - the sky, the fog, the cave ceiling and the rest of
// its main technique, built in the background at the start and at each new size; the same uniform code as with ReShade (EffectsBegin /
// EffectsFinish). Off: no effect at all, as no_reshade was first (the lightest).
fxnative::Runtime* g_fx = nullptr;
double g_cpuUs = 0.0, g_effUs = 0.0;   // the present hook's own time on the CPU, and the effects' part of it (the 10 s line)
uint32_t g_timed = 0;
double UsNow() {
    static const double k = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return 1e6 / (double)f.QuadPart; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * k;
}
void Tell() {
    static bool toldMode = false, wasByTurns = false;
    nors::Mode m;
    m.vr = g_set.platform.load() == 1;
    m.openxr = xr::On();
    m.byTurns = xr::ByTurns();
    m.pairPerTick = g_set.pairPerTick.load();
    m.turnWarp = xr::TurnWarpOn();
    const bool byTurns = nors::Fits(m);
    if (!toldMode || byTurns != wasByTurns) {
        toldMode = true;
        wasByTurns = byTurns;
        if (byTurns) Log("vrcam: no ReShade - native OpenXR eyes by turns: one eye drawn a frame, the other made on the GPU with the game's depth");
        else Log("vrcam: no ReShade - NOT native OpenXR eyes by turns with the eye made ([openxr] on, [stereo] afr=1 pair_per_tick=0 turn_warp=1): "
                 "[render] no_reshade is made for that mode only - other modes have no picture in the headset or no 3D without ReShade");
    }
    natived3d::WatchIndexCount(g_set.gameSky.load() ? engitem::DomeIndexCount() : 0);   // [sky] game_sky: the game's draws of the dome's mesh
    static ULONGLONG since = 0;
    const ULONGLONG now = GetTickCount64();
    if (!since) since = now;
    if (now - since < 10000) return;
    since = now;
    if (g_set.gameSky.load()) {
        uint32_t draws = 0, inst = 0;
        natived3d::TakeWatched(&draws, &inst);
        LogF("vrcam: game sky, 10 s: the game's draws of the dome's mesh (%u indices): %u (%u instances) - %s", engitem::DomeIndexCount(), draws, inst,
             !engitem::DomeShown() ? "the dome is put away now" : draws ? "the game draws it" : "NONE: the game does not draw it (culled, or its model not loaded)");
    }
    const natived3d::Stats st = natived3d::TakeStats();
    LogF("vrcam: no ReShade, 10 s: on the CPU our present work %.2f ms a present (the effects %.2f); our lists %u, %u of them waited for the "
         "graphics card (%.2f ms in all); %u presents of the game's swap chain; the game bound a depth-stencil %u times (%u more with a view made "
         "before the hook: not known); %u depth-stencil views known; the scene's depth %s",
         g_timed ? g_cpuUs / 1000.0 / g_timed : 0.0, g_timed ? g_effUs / 1000.0 / g_timed : 0.0, st.lists, st.listWaits, st.listWaitMs,
         st.presents, st.depthBinds, st.unknownViews, st.views, scenedepth::g_cache.Counting() ? "not picked yet" : "picked");
    g_cpuUs = g_effUs = 0.0;
    g_timed = 0;
    if (g_fx) {
        const fxnative::Stats fs = g_fx->TakeStats();
        // (the main technique's passes alone are timed; each eye's technique runs every other present - its time when run and how often)
        // what the effect was last told (its uniforms as vrcam set them): the sky (and its picture: 0 drawn by the shader, 1-6 act 1-5 / snow), fog, ceiling
        auto on = [](const char* n) {
            bool v = false;
            const reshade::api::effect_uniform_variable u = g_fx->find_uniform_variable("D2R_DepthFog.fx", n);
            if (u.handle) g_fx->get_uniform_value_bool(u, &v, 1);
            return v;
        };
        float skyTex = 0.0f;
        if (const reshade::api::effect_uniform_variable u = g_fx->find_uniform_variable("D2R_DepthFog.fx", "SkyTex"); u.handle) g_fx->get_uniform_value_float(u, &skyTex, 1);
        LogF("vrcam: native effects, 10 s: %s, sky %s (picture %d), fog %s, ceiling %s; on the graphics card %.2f ms a present (the overlays "
             "apart not timed); %u passes in %u presents and %u overlay draws, %u left out with nothing to draw (D2R_DepthFog.fx, %ux%u) - dearest "
             "passes (ms a present, and when run): %s",
             fs.ready ? "drawn" : fs.compiling ? "building" : fs.failed ? "NOT built (above)" : "waiting", on("SkyOn") ? "on" : "off",
             (int)(skyTex + 0.5f), !on("FogOn") ? "off" : on("FogLight") ? "light" : "full", on("CeilOn") && !on("FogLight") ? "on" : "off", fs.gpuMs,
             fs.passes, fs.renders, fs.techniques, fs.skipped, fs.width, fs.height, fs.dearest.empty() ? "-" : fs.dearest.c_str());
    }
}
// The effect of this present: its uniforms, then its techniques on our list on the game's queue (after the depth's copy), then the switches
// for the next present - as ReShade's begin effects / effects / finish effects. Nothing while its build is not there yet.
// Since when our effect has not drawn (its build for this size not there yet; 0: it draws) - the game's dome stands in for its sky (FxWaiting)
std::atomic<ULONGLONG> g_fxWaitSince{0};
bool FxWaiting() {
    const ULONGLONG s = g_fxWaitSince.load(std::memory_order_relaxed);
    return s && GetTickCount64() - s > 300;
}
void Effects(ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12Resource* bb) {
    if (!g_fx) return;
    static bool timed = [&] {   // its passes timed on the graphics card (the 10 s line)
        UINT64 hz = 0;
        if (SUCCEEDED(q->GetTimestampFrequency(&hz))) g_fx->SetTimestampFrequency(hz);
        return true;
    }();
    (void)timed;
    ApplySkyPictures(g_fx);   // the pictures' definitions before the first build too (a change builds it again)
    if (!g_fx->Begin(dev, bb)) {
        if (!g_fxWaitSince.load(std::memory_order_relaxed)) g_fxWaitSince.store(GetTickCount64());
        return;
    }
    g_fxWaitSince.store(0, std::memory_order_relaxed);
    if (scenedepth::g_nCopy) {   // the scene's depth as our copy has it (also when not copied this present: the last one)
        g_fx->BindSemantic("D2RVR_DEPTH", scenedepth::g_nCopy, scenedepth::g_nSrv);
        g_fx->BindSemantic("DEPTH", scenedepth::g_nCopy, scenedepth::g_nSrv);
    }
    EffectsBegin(g_fx, nullptr);
    {   // nothing placed by the depth on a depth missing or stale (vr/native_fx_depth.h): not the void - our sky and fog - over everything
        const bool usable = nativefxdepth::Usable(scenedepth::g_nCopy != nullptr, scenedepth::g_sinceCopy);
        static int told = -1;
        if (!usable && DepthEffectsWanted()) {
            SetBool(g_fx, "SkyOn", false);
            SetBool(g_fx, "ArcaneSkyOn", false);
            SetBool(g_fx, "FogOn", false);
            SetBool(g_fx, "SkyFogHorizon", false);
            SetBool(g_fx, "CeilOn", false);
        }
        const int now = usable ? 1 : DepthEffectsWanted() ? 0 : told;
        if (now != told && now >= 0) {
            if (now == 0)
                LogF("vrcam: native effects - the sky, the fog and the ceiling NOT drawn: %s (the scene's depth not copied for %u presents) - "
                     "nothing of ours rather than the void over the whole picture", scenedepth::g_nCopy ? "its copy is stale" : "no copy of the "
                     "scene's depth yet", scenedepth::g_sinceCopy);
            else if (told == 0) Log("vrcam: native effects - the scene's depth copied again: the sky, the fog and the ceiling drawn");
            told = now;
        }
    }
    if (ID3D12GraphicsCommandList* l = natived3d::OpenList(dev)) {
        g_fx->Render(l, bb);
        natived3d::Submit(q);
    }
    EffectsFinish(g_fx);
}
void Present(IDXGISwapChain3* sc, ID3D12CommandQueue* q) {
    if (!q) return;
    ID3D12Device* dev = nullptr;
    if (FAILED(q->GetDevice(__uuidof(ID3D12Device), (void**)&dev)) || !dev) return;
    ID3D12Resource* bb = nullptr;
    if (FAILED(sc->GetBuffer(sc->GetCurrentBackBufferIndex(), __uuidof(ID3D12Resource), (void**)&bb)) || !bb) {
        dev->Release();
        return;
    }
    HWND wnd = nullptr;
    sc->GetHwnd(&wnd);
    const double t0 = UsNow();
    const D3D12_RESOURCE_DESC bd = bb->GetDesc();
    const uint32_t w = (uint32_t)bd.Width, h = bd.Height;
    g_fxEye = g_eye.load() & 1;   // (the effects' begin had it so for a pass of its own: the eye presented)
    float s[2];
    renderscale::FrameOf(w, h, s);
    scenedepth::BindNative(dev, q, w, h);
    scenedepth::g_sinceCopy = nativefxdepth::NextSince(std::min(scenedepth::g_sinceCopy, 1000000u), scenedepth::g_copiedNow);
    // eyes by turns ([stereo] turn_warp): this present's depth, kept with its eye when its picture goes to the headset
    if (xr::ByTurns())
        xr::SetTurnDepth(scenedepth::g_copiedNow ? (void*)scenedepth::g_nCopy : nullptr, (uint32_t)scenedepth::g_nSrv, s[0], s[1]);
    const double t1 = UsNow();
    Effects(dev, q, bb);   // [render] native_effects: the sky and the fog into the back buffer, before it goes to the headset
    g_effUs += UsNow() - t1;
    PresentCommon(nullptr, wnd, dev, q, bb, w, h);
    g_cpuUs += UsNow() - t0;
    ++g_timed;
    Tell();
    bb->Release();
    dev->Release();
}
void SwapChainChanged() { scenedepth::g_cache.SwapChainChanged(); }
void BindDepth(ID3D12GraphicsCommandList* cl, ID3D12Resource* ds, unsigned colour) {
    scenedepth::OnBindNative(ds, colour);
    renderscale::OnBindNative(cl, ds);
}
void Barrier(ID3D12GraphicsCommandList*, unsigned n, const D3D12_RESOURCE_BARRIER* b) { scenedepth::OnBarrierNative(n, b); }
void Viewport(ID3D12GraphicsCommandList* cl, float w, float h) { renderscale::OnViewportNative(cl, w, h); }
// [stereo] turns_keep_own 3 / 4 without ReShade: the game's effects layer found from these hooks (effectslayer::OnBindNative) - beside
// [fog] game_fog_before_fx's own sinks for the same calls, when that is on
void (*g_fogTargets)(ID3D12GraphicsCommandList*, unsigned, const D3D12_CPU_DESCRIPTOR_HANDLE*, bool, ID3D12Resource*) = nullptr;
void (*g_fogBarrier)(ID3D12GraphicsCommandList*, unsigned, const D3D12_RESOURCE_BARRIER*) = nullptr;
void FxTargets(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_CPU_DESCRIPTOR_HANDLE* rts, bool single, ID3D12Resource* ds) {
    if (g_fogTargets) g_fogTargets(cl, n, rts, single, ds);
    effectslayer::OnBindNative(n, rts, single, ds);
}
void FxBarrier(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_RESOURCE_BARRIER* b) {
    if (g_fogBarrier) g_fogBarrier(cl, n, b);
    effectslayer::OnBarrierNative(n, b);
}
// the game's pipelines as made: [fog] game_fog_height's fog pass and [fog] game_fog_before_fx's transparent resolve
void GraphicsPso(ID3D12Device* dev, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d, ID3D12PipelineState* pso) {
    fogheight::OnGraphicsPso(dev, d, pso);
    fogfx::OnGraphicsPso(dev, d, pso);
}
void StreamPso(ID3D12Device* dev, const D3D12_PIPELINE_STATE_STREAM_DESC* d, ID3D12PipelineState* pso) {
    fogheight::OnStreamPso(dev, d, pso);
    fogfx::OnStreamPso(dev, d, pso);
    lightbounds::OnStreamPso(dev, d, pso);   // [render] light_bounds_fix without ReShade: the light bins' compute pipeline, if made from a stream
}
// a pipeline the game binds: [render] light_bounds_fix's light bins (compute), then [fog] game_fog_height's fog pass (graphics)
ID3D12PipelineState* PsoSwap(ID3D12PipelineState* pso) { return fogheight::Swap(lightbounds::Swap(pso)); }
// At the plugin's load, MinHook initialised, before the game makes its device and swap chain.
void Install() {
    natived3d::Sinks k;
    k.present = &Present;
    k.swapChainChanged = &SwapChainChanged;
    k.bindDepth = &BindDepth;
    k.barrier = &Barrier;
    k.viewport = &Viewport;
    // the game's interface layer ([hud] hide, the map, the labels, the wrists' globes): vr/hud.cpp's handlers, as with ReShade's events
    nrs::HudSinks(k);
    // [fog] game_fog_height: the game's fog pass's pipelines kept as made, a variant bound in their place (vr/fog_height.h)
    fogheight::SetLog(&Log);
    k.graphicsPso = &GraphicsPso;
    k.streamPso = &StreamPso;
    k.psoSwap = &PsoSwap;
    // [render] light_bounds_fix without ReShade: the game's light-bin depth bounds (vr/light_bounds.h) - a copy with our shader bound in its place
    lightbounds::SetLog(&Log);
    k.computePso = &lightbounds::OnComputePso;
    // [fog] game_fog_before_fx: the lists' bindings followed and the step recorded before the transparent resolve - its hooks only when the key
    // is on at the start (a list's state is followed from its first Reset); switched on later, from the next start
    fogfx::SetLog(&Log);
    if (IniB(L"fog", L"game_fog_before_fx", false)) {
        fogfx::FillSinks(k);
        Log("vrcam: fog before the effects - [fog] game_fog_before_fx on: the game's lists followed for the step before its transparent resolve");
    }
    g_fogTargets = k.fxTargets;
    g_fogBarrier = k.fxBarrier;
    k.fxTargets = &FxTargets;
    k.fxBarrier = &FxBarrier;
    effectslayer::SetLogNative(&Log);
    hud::SetLogger(&Log);
    natived3d::Install(k, &Log);
    if (IniB(L"render", L"native_effects", true)) {
        fxnative::Options o;
        // the effect and its pictures where D2R VR's own files are (the mod's reshade\), else beside the game (an older install, or files
        // copied by hand): the first folder with the effect in it
        wchar_t exe[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
        while (n > 0 && exe[n - 1] != L'\\') --n;
        exe[n] = 0;
        for (const std::wstring& dir : {OurFilesDir(), std::wstring(exe)}) {
            const std::wstring fx = dir + L"reshade-shaders\\Shaders\\D2R_DepthFog.fx";
            if (o.effectFile.empty() && GetFileAttributesW(fx.c_str()) != INVALID_FILE_ATTRIBUTES) o.effectFile = fx;
            o.textureDirs.push_back(dir + L"reshade-shaders\\Textures");
        }
        if (o.effectFile.empty()) o.effectFile = OurFilesDir() + L"reshade-shaders\\Shaders\\D2R_DepthFog.fx";   // (said missing below)
        std::wstring dir = g_iniPath;
        dir.resize(dir.find_last_of(L'\\') + 1);
        o.viewResource = [](uint64_t view, DXGI_FORMAT* f) {   // hud.cpp's textures (D2R_GAME_LAYER, D2R_GAME_GLOBES, ...), made through the stand-in
            uint32_t fmt = 0;
            ID3D12Resource* r = nrs::ResourceOfView(view, &fmt);
            *f = (DXGI_FORMAT)fmt;
            return r;
        };
        o.cacheDir = dir + L"d2r_vr_fx_cache";   // (beside d2r_vr.ini: the shaders built before, by their code's hash)
        o.log = &Log;
        o.reloaded = [] { Forget(nullptr); };
        o.keepPass = [](const char* entry) {   // a pass into the picture that would give it as it is: not drawn (vr/fx_pass_keep.h)
            return fxpasskeep::Keep(
                entry,
                [](const char* n, bool* v) {
                    const reshade::api::effect_uniform_variable u = g_fx->find_uniform_variable("D2R_DepthFog.fx", n);
                    if (u.handle) g_fx->get_uniform_value_bool(u, v, 1);
                    return u.handle != 0;
                },
                [](const char* n, float* v) {
                    const reshade::api::effect_uniform_variable u = g_fx->find_uniform_variable("D2R_DepthFog.fx", n);
                    if (u.handle) g_fx->get_uniform_value_float(u, v, 1);
                    return u.handle != 0;
                });
        };
        // only the act's sky and the cave ceiling's stone on the graphics card (vr/fx_picture_keep.h), as the effect was left last frame; the
        // last slot seen kept (a new build starts at the effect's own 0, a cave has no sky)
        o.pictureWanted = [](const char* name) {
            static int sky = 0, ceil = 0;
            auto num = [](const char* n) {
                float v = 0.0f;
                const reshade::api::effect_uniform_variable u = g_fx->find_uniform_variable("D2R_DepthFog.fx", n);
                if (u.handle) g_fx->get_uniform_value_float(u, &v, 1);
                return v;
            };
            if (const int s = (int)(num("SkyTex") + 0.5f); s > 0) sky = s;
            if (num("CeilOn") != 0.0f && num("CeilTexOn") != 0.0f)
                if (const int c = (int)(num("CeilTexSlot") + 0.5f); c > 0) ceil = c;
            return fxpicturekeep::Wanted(name, sky, ceil);
        };
        if (GetFileAttributesW(o.effectFile.c_str()) == INVALID_FILE_ATTRIBUTES) {
            LogF("vrcam: native effects - no %s: no sky or fog without ReShade (run D2R VR Setup again)", Utf8(o.effectFile.c_str()).c_str());
        } else {
            g_fx = new fxnative::Runtime(o);
            replay::g_nativeFx = g_fx;   // (its overlays apart: replay::XrPresent)
            LogF("vrcam: native effects on ([render] native_effects): D2R_DepthFog.fx run by the plugin itself - %s", Utf8(o.effectFile.c_str()).c_str());
        }
    } else {
        Log("vrcam: native effects off ([render] native_effects=0): no sky, fog or ceiling without ReShade");
    }
}
}  // namespace noreshade


// [render] fog_in_frame (vr/fog_in_frame.h), each effects pass once the fog's values are set: the fog drawn in the game's frame
// with the values the post pass has - read back from the effect, so what a player moved in ReShade's own window counts too -
// and the post pass leaving the scene's fog to it while it is drawn (FogInFrame). Only with the fog's colour fixed (the act's
// own, or no sky): the colour the sky gives the fog is by the view's direction, the post pass's.
template <class RT> float FxFloat(RT* rt, const char* name, float fallback) {
    const reshade::api::effect_uniform_variable u = rt->find_uniform_variable("D2R_DepthFog.fx", name);
    float v[3] = {fallback, fallback, fallback};
    if (u.handle) rt->get_uniform_value_float(u, v, 1);
    return v[0];
}
template <class RT> bool FxBool(RT* rt, const char* name) {
    const reshade::api::effect_uniform_variable u = rt->find_uniform_variable("D2R_DepthFog.fx", name);
    bool v = false;
    if (u.handle) rt->get_uniform_value_bool(u, &v, 1);
    return v;
}
std::atomic<int> g_fifPaint{0};   // [debug] fog_in_frame_paint (live)
template <class RT> void FogInFrameTick(RT* rt) {
    SetFloat(rt, "FogInFramePaint", (float)g_fifPaint.load());   // (the post pass's side paints even with the frame's fog off: it shows that)
    if (!fogframe::Wanted()) return;
    fogframe::Params p;
    p.paint = g_fifPaint.load();
    const bool sky = FxBool(rt, "SkyOn");
    // (not in the Arcane Sanctuary: [sky] arcane_fog takes the fog's colour from its sky by the direction, as the sky's horizon does)
    p.on = FogWanted() && viewgate::F4() && FxBool(rt, "FogOn") && (FxBool(rt, "FogFixed") || !sky) && !FxBool(rt, "ArcaneSkyOn") &&
           !FxBool(rt, "ShowDistance");
    p.nearPlane = FxFloat(rt, "NearPlane", p.nearPlane);
    p.fogStart = FxFloat(rt, "FogStart", p.fogStart);
    p.fogEnd = FxFloat(rt, "FogEnd", p.fogEnd);
    p.fogCurve = FxFloat(rt, "FogCurve", p.fogCurve);
    p.fogStrength = FxFloat(rt, "FogStrength", p.fogStrength);
    if (const reshade::api::effect_uniform_variable u = rt->find_uniform_variable("D2R_DepthFog.fx", "FogColor"); u.handle)
        rt->get_uniform_value_float(u, p.fogColour, 3);
    p.caveHalo = FxBool(rt, "CeilOn") && !sky ? std::clamp(FxFloat(rt, "CeilHalo", 1.0f), 0.0f, 1.0f) : 0.0f;
    fogframe::Configure(p);
    SetBool(rt, "FogInFrame", fogframe::Drawn());
}
// Before each present's effects: everything the effect reads that changes (uniforms, bindings). RT: ReShade's runtime (its
// reshade_begin_effects, cl its list) or [render] no_reshade's own (vr/fx_native.h; cl nullptr - the depth was copied before, BindNative).
template <class RT> void EffectsBegin(RT* rt, reshade::api::command_list* cl) {
    constexpr bool kReShade = std::is_same_v<RT, reshade::api::effect_runtime>;
    pairfx::BeforeWrite();   // [stereo] pair_fx_sync: this eye's last effects drawn before the constants go in (before the timing: a wait is not effects)
    pairtime::g_effBegin.store(pairtime::UsNow());
    const bool own = OwnDepthNow();
    SetBool(rt, "OwnDepth", own);   // ours (VR, the square window) or ReShade's DEPTH (scenedepth::)
    if constexpr (kReShade) scenedepth::Bind(rt, cl);   // before the replay reads which depth the effects have
    {   // eyes by turns, the other eye made: the overlays apart this present (replay::DrawOverlays). In the eye made the scene's depth bound
        // is the other eye's: nothing of it hides them there (D2R_OverlayMade, as in the try the author saw right) - unless [stereo]
        // turn_overlay_hide (on): then its own depth, moved with its picture (xr::TurnMadeDepth, bound once as D2RVR_DEPTH_MADE - a binding
        // drains the GPU, so only when it is another texture or the effects reloaded). Never the drawn eye's depth for the eye made: an
        // orb on the wrist set against the other eye's hand showed twice.
        const bool apart = replay::OverlaysNow();
        replay::g_ovApart.store(apart);
        bool made = false;
        if (apart) {
            static reshade::api::resource_view view{0};
            uint32_t gen = 0;
            const uint64_t res = (uint64_t)xr::TurnMadeDepth(&gen);
            if constexpr (!kReShade) {   // (no ReShade: the texture itself)
                if (res && (res != g_madeBound || gen != g_madeBoundGen)) {
                    rt->BindSemantic("D2RVR_DEPTH_MADE", (ID3D12Resource*)res, DXGI_FORMAT_R32_FLOAT);
                    g_madeBound = res;
                    g_madeBoundGen = gen;
                    Log("vrcam: eyes by turns - the made eye's depth bound for the overlays (D2RVR_DEPTH_MADE, no ReShade)");
                }
            } else
            if (res && (res != g_madeBound || gen != g_madeBoundGen)) {
                reshade::api::device* dev = rt->get_device();
                reshade::api::resource_view v{0};
                if (dev->create_resource_view(reshade::api::resource{res}, reshade::api::resource_usage::shader_resource,
                                              reshade::api::resource_view_desc(reshade::api::format::r32_float), &v)) {
                    rt->update_texture_bindings("D2RVR_DEPTH_MADE", v, v);
                    if (view.handle) dev->destroy_resource_view(view);
                    view = v;
                    g_madeBound = res;   // (cleared when the effects reload: Forget)
                    g_madeBoundGen = gen;
                    Log("vrcam: eyes by turns - the made eye's depth bound for the overlays (D2RVR_DEPTH_MADE)");
                }
            }
            made = res && res == g_madeBound && gen == g_madeBoundGen;
        }
        const int drawn = g_eye.load() & 1;
        replay::g_ovDrawnEye = drawn;   // (XrPresent: the present's eye should be the same)
        SetBool(rt, "OverlaysApart", apart);
        // the eye made's depth: its technique's (D2R_OverlayMade), never a flag per eye - those flip every frame (replay::g_ovTech); this one
        // changes only with the binding (cleanroom/tests/turn_math_test.cpp: overlay)
        SetBool(rt, "OverlayMadeOn", turnwarp::OverlayDepthOf(apart, made, drawn, drawn ^ 1) == turnwarp::OverlayDepthFrom::Made);
    }
    if constexpr (kReShade) if (replay::g_on.load()) replay::g_rt = rt;
    if constexpr (kReShade) if (replay::g_on.load()) {   // the depth buffer our effect reads: replay keeps the left eye's for its present
        const reshade::api::effect_texture_variable v = rt->find_texture_variable("D2R_DepthFog.fx", own ? "FogDepthTex" : "FogDepthRsTex");
        if (v.handle) {
            reshade::api::resource_view srv{0};
            rt->get_texture_binding(v, &srv, nullptr);
            const uint64_t res = srv.handle ? rt->get_device()->get_resource_from_view(srv).handle : 0;
            if (res && res != replay::g_depthRes.load()) {
                replay::g_depthRes.store(res);
                LogF("vrcam: replay - the effects' depth buffer: %p", (void*)res);
            }
        }
    }
    if constexpr (!kReShade) SetFloats(rt, "DepthScale", renderscale::g_last, 2);   // (no ReShade: the share and the eye made's depth set before, noreshade::Present)
    if constexpr (kReShade) {
        float s[2];
        renderscale::Frame(rt, s);
        SetFloats(rt, "DepthScale", s, 2);
        // eyes by turns ([stereo] turn_warp): this present's depth, kept with its eye when its picture goes to the headset
        if (xr::ByTurns())
            xr::SetTurnDepth(scenedepth::g_copiedNow && scenedepth::g_copy.handle ? (void*)scenedepth::g_copy.handle : nullptr,
                             (uint32_t)scenedepth::SrvFormat(scenedepth::g_copyDesc.texture.format), s[0], s[1]);
    }
    depthcam::Publish();
    {   // FlatVR's ring of finished pictures, on or off from D2R VR Settings: the addon
        // (FlatVR_DepthProvider 2.19+) reads ReShade.ini [FLATVR] ColourRing once a second
        static int told = -1;
        const int on = g_set.pictureRing.load() ? 1 : 0;
        if (on != told && kReShade) {   // (no ReShade: no ReShade.ini to tell)
            told = on;
            reshade::set_config_value(nullptr, "FLATVR", "ColourRing", on ? "1" : "0");
            LogF("vrcam: FlatVR's ring of finished pictures %s (ReShade.ini [FLATVR] ColourRing=%d, [stereo] picture_ring)",
                 on ? "on" : "off", on);
        }
    }
    if (!g_tech[0].handle || (!rt->get_technique_state(g_tech[0]) && !(g_tech[1].handle && rt->get_technique_state(g_tech[1])))) {
        if (fogframe::Armed()) fogframe::Configure(fogframe::Params{});   // [render] fog_in_frame: not without the post pass's values
        return;
    }
    SetFloat(rt, "NearPlane", g_lastNear.load());
    LowHpUniforms(rt);   // [hud] low_hp_vignette
    SetFloat(rt, "FogStart", g_underground.load() ? g_set.fogCaveStart.load() : g_set.fogStart.load());
    SetFloat(rt, "FogEnd", g_underground.load() ? g_set.fogCaveEnd.load() : g_set.fogEnd.load());
    SetFloat(rt, "FogStrength", g_underground.load() ? g_set.fogCaveStrength.load() : 1.0f);
    SetFloat(rt, "FogEaseIn", g_set.fogEaseIn.load() ? 1.0f : 0.0f);   // [fog] ease_in (D2R_FogEase_Unleashed.fxh)
    SetFloat(rt, "FogCurve", g_set.fogCurve.load());
    SetFloat(rt, "FogBlur", g_set.fogBlur.load());
    SetBool(rt, "FogOn", FogWanted());
    SetBool(rt, "FogLight", g_set.fogLight.load());   // [fog] light: the short way (the sky still drawn; no ceiling, no fog over glows)
    SetBool(rt, "TableKeyOn", TableKeyWanted());
    SetFloat(rt, "TableFloor", g_set.tableFloor.load());
    if (TableKeyWanted()) {   // the bounds: this eye's ray, and the game's own view to test the ground against
        SkyView sv[2];
        TableBox b;
        AcquireSRWLockShared(&g_skyLock); sv[0] = g_skyView[0]; sv[1] = g_skyView[1]; b = g_tableBox; ReleaseSRWLockShared(&g_skyLock);
        const bool on = b.ok && g_set.tableBounds.load() > 0.0f && sv[0].proj_ok && sv[0].axes_ok;
        SetBool(rt, "TableBoundsOn", on);
        SetFloat(rt, "TableBounds", g_set.tableBounds.load());
        if (on) {
            for (int e = 0; e < 2; ++e) {
                const SkyView& v = sv[e].axes_ok && sv[e].proj_ok ? sv[e] : sv[0];
                char n[16];
                snprintf(n, sizeof n, "SkyProj%d", e); SetFloats(rt, n, v.proj, 4);
                snprintf(n, sizeof n, "CamRight%d", e); SetFloats(rt, n, v.axes, 3);
                snprintf(n, sizeof n, "CamUp%d", e); SetFloats(rt, n, v.axes + 3, 3);
                snprintf(n, sizeof n, "CamBack%d", e); SetFloats(rt, n, v.axes + 6, 3);
                snprintf(n, sizeof n, "TableEye%d", e); SetFloats(rt, n, v.eyeRel, 3);
            }
            // row-major v * M: clip component j is the point dotted with column j
            const int cols[3] = {0, 1, 3};
            const char* const names[3] = {"TableGameX", "TableGameY", "TableGameW"};
            for (int k = 0; k < 3; ++k) {
                const int j = cols[k];
                const float c[4] = {b.vp[j], b.vp[4 + j], b.vp[8 + j], b.vp[12 + j]};
                SetFloats(rt, names[k], c, 4);
            }
            SetFloats(rt, "TableHero", b.hero, 3);
            const float t = g_set.tableTurnDeg.load() * 0.0174532925f;
            const float turn[2] = {cosf(t), sinf(t)};
            SetFloats(rt, "TableTurn", turn, 2);
        }
    }
    {
        float bg[3];
        AcquireSRWLockShared(&g_tableBgLock);
        for (int c = 0; c < 3; ++c) bg[c] = g_tableBg[c];
        ReleaseSRWLockShared(&g_tableBgLock);
        SetFloats(rt, "TableBackground", bg, 3);
    }
    {   // the act's own fog colour; without one the fog takes the sky's horizon, or stays dark
        float rgb[3];
        SetBool(rt, "FogFixed", FogColourNow(rgb));
        SetFloats(rt, "FogColor", rgb, 3);
        // the game drawing the fog ([fog] game_fog) and our effect the sky (no game dome there - every act but act 1): the sky's low part
        // sinks into the fog's colour as with our own fog, so the fogged land meets the sky without an edge (Serg, 2026-10-28, act 2: a hard
        // line between the tan fogged world and the sky)
        SetBool(rt, "SkyFogHorizon", gamefogvr::Active() && !engitem::DomeShown());
        // [sky] dome_fog_horizon: the game's dome shown - its low part sinks into the same fog colour (Serg: "резкая граница", "цвет не
        // совпадает" against our old sky), while a fog is drawn
        SetBool(rt, "DomeFogHorizon", g_set.domeFogHorizon.load() && engitem::DomeShown() && (FogWanted() || gamefogvr::Active()));
        // [fog] game_fog_match: the game's fog comes out through the area's colour grading - act 4 (Serg, 49 / 50.webp): his #5A2712 on the
        // sky's low part, (57, 29, 9) on the fogged far land under it, a hard silhouette. The effect measures what the game's fog shows where
        // it is complete and moves the fogged pixels by the game's own share toward the act's colour - the numbers the game's pass has now
        const bool match = g_set.fogMatch.load() && gamefogvr::Active();
        SetBool(rt, "GameFogMatch", match);
        if (match) {
            const gamefogvr::Fog gf = gamefogvr::Current();
            const bool under = g_underground.load();
            SetFloat(rt, "GameFogDensity", gf.density);
            SetFloat(rt, "GameFogPlane", gf.plane);
            // (the curve and the height thinning are in the game's pass only while our patched variant of it is bound: no_reshade's hooks)
            static uint32_t boundBefore = 0;
            static ULONGLONG boundAt = 0;
            const uint32_t bound = fogheight::Bound();
            if (bound != boundBefore) { boundBefore = bound; boundAt = GetTickCount64(); }
            const bool patched = boundAt && GetTickCount64() < boundAt + 500;
            SetBool(rt, "GameFogOurCurve", patched && g_set.fogOurCurve.load());
            const float curve[4] = {under ? g_set.fogCaveStart.load() : g_set.fogStart.load(), under ? g_set.fogCaveEnd.load() : g_set.fogEnd.load(),
                                    g_set.fogCurve.load(), under ? g_set.fogCaveStrength.load() : 1.0f};
            SetFloats(rt, "GameFogCurve", curve, 4);
            const bool height = patched && g_set.fogHeight.load() && !under;   // (as PresentCommon hands fogheight::Frame)
            const float topSoft[2] = {height ? FogTopNow() : 1.0e9f, height ? g_set.fogSoft.load() : 1.0f};
            SetFloats(rt, "GameFogTopSoft", topSoft, 2);
        }
        // [fog] fog_into_sky: in the open air under an act's sky (ours or the game's dome - the same painted pictures), the fog fades the far
        // into the sky straight behind it, as the Arcane Sanctuary's ([sky] arcane_fog). Underground nothing but rock is behind: as before
        SetBool(rt, "FogIntoSky", g_set.fogIntoSky.load() && gamefogvr::Active() && g_skyPalette.load() >= 0 && !g_arcaneSky.load() && !g_underground.load());
    }
    const int pal = g_skyPalette.load();
    // The eye of the frame being presented. Read straight from the block it
    // raced the game thread flipping the eye, so now and then the sky got the
    // other eye's convergence shift and shook sideways. Frames alternate, so the
    // eye simply flips each present, and every 30 presents its phase is checked
    // against those readings by majority - one wrong reading no longer shows.
    int eye = g_eye.load() & 1;
    // A pair per game frame presents from inside its own pass, on this thread,
    // after the pass set its eye: no race, the picture is the eye just set.
    // Without ReShade the same: the plugin's present hook runs on the present itself, the eye presented is g_eye - FlatVR's block (stopped
    // while OpenXR runs) toggled out of phase there, and each eye got the other's technique and overlay values every other frame (PC2,
    // 2026-10-26: "the medallions flicker - two frames in each eye").
    if (PairWanted() || !kReShade) eye = g_eye.load() & 1;
    else if (AfrOn() && g_afrBlock) {
        static int e = 0, samples = 0, disagree = 0;
        e ^= 1;
        if ((((g_eye.load() ^ (int)g_afrBlock->swap) & 1)) != e) ++disagree;
        if (++samples >= 30) {
            static ULONGLONG told = 0;   // how much the raced reading disagrees: a flickering HUD or sky would show here
            if (g_set.uiShift.load() > 0.0f && GetTickCount64() - told > 5000) {
                told = GetTickCount64();
                LogF("vrcam: eye phase - %d of 30 raced readings disagreed%s", disagree, disagree > samples / 2 ? ", phase flipped" : "");
            }
            if (disagree > samples / 2) e ^= 1;
            samples = disagree = 0;
        }
        eye = e;
    }
    g_fxEye = eye;
    // Both eyes' views: the technique on picks one. Without AFR the one eye there is fills both.
    SkyView sv[2];
    AcquireSRWLockShared(&g_skyLock);
    sv[0] = SkyBack(AfrOn() ? 0 : eye); sv[1] = SkyBack(AfrOn() ? 1 : eye);
    ReleaseSRWLockShared(&g_skyLock);
    const bool arcane = g_arcaneSky.load();   // [sky] arcane_sky: the Arcane Sanctuary's own sky, in place of any other
    // ([sky] game_sky: the game draws it - chosen, ours not at all, also while its dome is not shown; vr/sky_paths.h)
    const char* cannot = g_set.gameSky.load() && pal >= 0 ? engitem::SkyCannot() : nullptr;   // (chosen and unable here for good: ours instead)
    bool sky = skypaths::OurSky(pal >= 0, skypaths::GameHolds(g_set.gameSky.load(), cannot), arcane, engitem::DomeShown());
    {   // who draws the sky now, and why: each change once
        const char* by = arcane ? "our effect - the Arcane Sanctuary's own sky (the game has none there)"
                         : g_set.gameSky.load() ? (engitem::DomeShown() ? "the game's dome ([sky] game_sky=1; our effect's sky off)"
                                                   : cannot ? "our effect INSTEAD of the game's dome ([sky] game_sky=1)"
                                                            : "nothing yet - the game's dome is chosen ([sky] game_sky=1) and on its way; our effect's sky off")
                         : pal >= 0 ? "our effect ([sky] game_sky=0)" : "nothing - no sky wanted here";
        static const char* toldBy = nullptr;
        static const char* toldWhy = nullptr;
        if (by != toldBy || cannot != toldWhy) {
            toldBy = by;
            toldWhy = cannot;
            LogF("vrcam: the sky drawn by %s%s%s", by, cannot ? ": " : "", cannot ? cannot : "");
        }
    }
    {   // ... and the fog: our effect's instead of the game's chosen one, once a change
        const char* fogInstead = g_set.gameFog.load() && OurFogWanted() && !g_arcaneSky.load() ? FogCannot() : nullptr;
        static const char* toldFog = nullptr;
        if (fogInstead != toldFog) {
            if (fogInstead) LogF("vrcam: the fog drawn by our effect INSTEAD of the game's ([fog] game_fog=1): %s", fogInstead);
            else if (toldFog) Log("vrcam: the fog drawn by the path chosen ([fog] game_fog) again");
            toldFog = fogInstead;
        }
    }
    auto viewOk = [](const SkyView& v) { return v.proj_ok && v.axes_ok && v.proj[0] != 0.0f && v.proj[1] != 0.0f; };
    // AFR: this present's technique reads its own eye's view - the other eye's, not there yet, takes this one's until its own comes (PC2
    // without ReShade, 2026-10-26: "sky on" in the log, never drawn; PC1 with ReShade's replay: the right eye's projection comes with its
    // replay, and the sky stayed black for the first seconds in the world - both eyes' views were wanted)
    if (AfrOn() && viewOk(sv[eye & 1]) && !viewOk(sv[(eye & 1) ^ 1])) sv[(eye & 1) ^ 1] = sv[eye & 1];
    {   // wanted and not drawn: why, once per change
        auto why = [&](const SkyView& v) { return !v.proj_ok ? "no projection" : !v.axes_ok ? "no axes" : v.proj[0] == 0.0f || v.proj[1] == 0.0f ? "a projection of 0" : "ready"; };
        static std::string told;
        std::string now;
        if (sky && !(viewOk(sv[0]) && viewOk(sv[1])))
            now = std::string("the left eye's view ") + why(sv[0]) + ", the right eye's " + why(sv[1]) + " (the eye presented: " + (eye & 1 ? "right" : "left") + ")";
        if (now != told) {
            told = now;
            if (!now.empty()) LogF("vrcam: the sky is wanted but NOT drawn - %s", now.c_str());
        }
    }
    for (const SkyView& v : sv) sky = sky && viewOk(v);
    SetBool(rt, "SkyOn", sky);
    // [sky] game_sky: the game's dome far out (its depth reads as the void): the void's pixels are its - neither our sky nor our fog over them
    SetBool(rt, "SkyByGame", engitem::DomeShown());
    SetFloat(rt, "SkyDomeDepth", skydome::DomeDepth(engitem::DomeRadius(), g_lastNear.load()));   // (the dome's depth band and the void)
    SetBool(rt, "SkyOverRims", g_set.skyOverRims.load() && viewgate::F4());   // [sky] cover_rims: F4 only, elsewhere the rim as before
    // [fog] spare_effects: in VR F4 with our fog on, the game's transparent targets of this frame for the fog to spare
    fogspare::Configure(fogspare::Wanted() && FogWanted() && viewgate::F4() && !fogframe::Armed());
    if constexpr (kReShade) SetBool(rt, "SpareOn", fogspare::Bind(rt));
    else SetBool(rt, "SpareOn", false);   // (no ReShade: the game's transparent targets are not taken)
    SetBool(rt, "ArcaneSkyOn", sky && arcane);
    {   // the cave ceiling: a plane CeilHeight over the hero's floor, met by each eye's ray ([ceiling] game_ceiling: the game draws it)
        // [ceiling] game_ceiling: the game's from the first frame underground, laid or still loading - never our vault in between
        const int who = gameceiling::Drawer(sky, CeilWanted(), engitem::CeilingByGame(), engitem::CeilingShown());
        bool ceil = who == 1;
        {   // who draws it, each change (the first frames in a cave included)
            static int toldWho = -1;
            static ULONGLONG since = 0;
            if (who != toldWho) {
                const ULONGLONG now = GetTickCount64();
                if (toldWho <= 0) since = now;
                static const char* const kWho[] = {"none wanted here", "our effect's vault",
                                                   "the game's ([ceiling] game_ceiling) - its slab not laid yet: nothing drawn meanwhile, not our vault",
                                                   "the game's ([ceiling] game_ceiling) - its slab laid"};
                LogF("vrcam: cave ceiling drawn by: %s (%.2f s into the ceiling's area)%s", kWho[who], who ? (now - since) / 1000.0 : 0.0,
                     who != 1 || !g_set.gameCeiling.load() ? "" : !g_gameCeilBiome.load() ? " - not a natural cave: this room's own vault"
                     : viewgate::F4() ? " - the game's slab given up on (its lines say why)" : " - not VR F4");
                toldWho = who;
            }
        }
        for (const SkyView& v : sv) ceil = ceil && v.proj_ok && v.axes_ok && v.proj[0] != 0.0f && v.proj[1] != 0.0f;
        SetBool(rt, "CeilOn", ceil);
        if (health::On())   // [debug] health: who drew the sky, the fog and the ceiling in this present (ours as set just here, the game's passes)
            g_healthSky.Present(sky, engitem::DomeShown(), FogWanted(), gamefogvr::Active(), ceil, engitem::CeilingShown());
        if (ceil) {
            const float scale = g_set.ceilScale.load();
            // The hero's place wrapped on a whole number of tiles: the world's own
            // coordinates in a float would round the pattern into steps far from the
            // origin; what repeats every tile cannot tell the wrap.
            const double period = 64.0 * scale;
            // Heights over the area's floor, not the hero's feet: on a ledge the ceiling rose with
            // him (the user, 2026-10-07). The floor: the lowest the hero has stood since the area
            // changed; everything the shader measures (the ceiling, its height map) follows it.
            static float floorY = 0.0f;
            static uint32_t floorGen = ~0u;
            if (floorGen != g_biomeGen.load()) { floorGen = g_biomeGen.load(); floorY = sv[0].hero[1]; }
            floorY = std::min(floorY, sv[0].hero[1]);
            for (int e = 0; e < 2; ++e) {
                char n[16];
                snprintf(n, sizeof n, "SkyProj%d", e); SetFloats(rt, n, sv[e].proj, 4);
                snprintf(n, sizeof n, "CamRight%d", e); SetFloats(rt, n, sv[e].axes, 3);
                snprintf(n, sizeof n, "CamUp%d", e); SetFloats(rt, n, sv[e].axes + 3, 3);
                snprintf(n, sizeof n, "CamBack%d", e); SetFloats(rt, n, sv[e].axes + 6, 3);
                const float eyeOverFloor[3] = {sv[e].eyeRel[0], sv[e].eyeRel[1] + (sv[e].hero[1] - floorY), sv[e].eyeRel[2]};
                snprintf(n, sizeof n, "CeilEye%d", e); SetFloats(rt, n, eyeOverFloor, 3);
                double hx = fmod((double)sv[e].hero[0], period), hz = fmod((double)sv[e].hero[2], period);
                if (hx < 0.0) hx += period;
                if (hz < 0.0) hz += period;
                const float hero[2] = {(float)hx, (float)hz};
                snprintf(n, sizeof n, "CeilHero%d", e); SetFloats(rt, n, hero, 2);
            }
            float height, bright, light, relief;
            int slot;
            CeilingNow(&height, &bright, &light, &relief, &slot);
            SetFloat(rt, "CeilHeight", height);
            SetFloat(rt, "CeilScale", scale);
            SetFloat(rt, "CeilBrightness", bright);
            SetFloat(rt, "CeilRelief", relief);
            SetFloat(rt, "CeilTexSlot", (float)slot);
            SetFloat(rt, "CeilLightRadius", light);
            // [ceiling] fire_from_picture (2026-10-11, "any fire gives light, as at the start"): the
            // shader's own search of the picture for fire - torches, braziers, spells, burning
            // monsters - in place of the game's lights; it is what ran before those were found.
            const bool pictureFire = g_set.ceilPictureFire.load();
            SetBool(rt, "CeilTorches", g_set.ceilTorches.load() || pictureFire);
            SetFloat(rt, "CeilTorchBright", g_set.ceilTorchBright.load());
            SetFloat(rt, "CeilTorchRadius", g_set.ceilTorchRadius.load());
            SetFloat(rt, "CeilFireNearer", g_set.ceilFireNearer.load());
            SetFloat(rt, "CeilHalo", g_set.ceilHalo.load());
            SetFloat(rt, "CeilTorchWarm", g_set.ceilTorchWarm.load());
            SetFloat(rt, "CeilWallDist", g_set.ceilWall.load());
            SetFloat(rt, "CeilFloorDepth", g_set.ceilFloor.load());
            {   // the lit objects the game has near the hero, relative to him; the flame ~7 units up
                worldobj::Light l[worldobj::kMaxLights];
                const int n = worldobj::g_known.load() ? worldobj::Lights(l) : 0;
                float pos[worldobj::kMaxLights * 4] = {}, col[worldobj::kMaxLights * 4] = {};
                for (int i = 0; i < n; ++i) {
                    pos[i * 4 + 0] = l[i].x - sv[0].hero[0];
                    pos[i * 4 + 1] = l[i].y - floorY;   // over the area's floor, as the ceiling
                    pos[i * 4 + 2] = l[i].z - sv[0].hero[2];
                    pos[i * 4 + 3] = l[i].lit;
                    for (int c = 0; c < 3; ++c) col[i * 4 + c] = l[i].rgb[c];
                    // farther than [ceiling] torch_distance from the hero: gone, fading over its last quarter -
                    // the torches of the next hall lit the ceiling over the walls between (2026-10-07)
                    const float d = sqrtf(pos[i * 4] * pos[i * 4] + pos[i * 4 + 2] * pos[i * 4 + 2]), reach = g_set.ceilTorchDist.load();
                    col[i * 4 + 3] = std::clamp((reach - d) / (0.25f * reach), 0.0f, 1.0f) * (l[i].monster ? g_set.ceilMonsterBright.load() : 1.0f);
                }
                SetBool(rt, "GameLightsOn", worldobj::g_known.load() && !pictureFire);
                SetFloats(rt, "GameLightPos", pos, worldobj::kMaxLights * 4);
                SetFloats(rt, "GameLightCol", col, worldobj::kMaxLights * 4);
                static ULONGLONG told = 0;   // what goes to the shader, every 5 s: the nearest light, as the hero sees it
                if (GetTickCount64() - told > 5000) {
                    told = GetTickCount64();
                    const bool hasPos = rt->find_uniform_variable("D2R_DepthFog.fx", "GameLightPos").handle != 0;
                    LogF("worldobj: to the shader %d lights (uniform %s), nearest at %.1f %.1f %.1f radius %.0f colour %.2f %.2f %.2f; hero %.1f %.1f %.1f, eye %.1f %.1f %.1f, ceiling %.0f",
                         n, hasPos ? "found" : "MISSING", pos[0], pos[1], pos[2], pos[3], col[0], col[1], col[2],
                         sv[0].hero[0], sv[0].hero[1], sv[0].hero[2], sv[0].eyeRel[0], sv[0].eyeRel[1], sv[0].eyeRel[2], height);
                }
            }
            float wet, detail, contrast;
            CeilVaultCfg vault;
            CeilingLookNow(&wet, &detail, &contrast, &vault);
            SetBool(rt, "CeilDome", vault.dome);
            SetFloat(rt, "CeilTexSize", vault.texSize);
            SetFloat(rt, "CeilReliefPic", vault.reliefPic);
            SetFloat(rt, "CeilDomeRadius", vault.domeRadius);
            SetFloat(rt, "CeilDomeMax", vault.domeMax);
            SetFloat(rt, "CeilDomeFind", vault.domeFind);
            SetFloat(rt, "CeilMapGen", (float)(g_biomeGen.load() & 0xFFFFF));   // another area: the height map starts again
            SetFloat(rt, "CeilWet", wet);
            SetFloat(rt, "CeilBump", detail);
            SetFloat(rt, "CeilContrast", contrast);
            if (const reshade::api::effect_uniform_variable u = rt->find_uniform_variable("D2R_DepthFog.fx", "CeilSteps"); u.handle) {
                const int steps = g_set.ceilSteps.load();
                rt->set_uniform_value_int(u, &steps, 1);
            }
            SetBool(rt, "CeilTexOn", slot > 0);
        }
    }
    FogInFrameTick(rt);   // [render] fog_in_frame
    // Native OpenXR, both eyes from one game frame: the right eye's pass is the left's replayed, its interface layer laid out for the left eye's
    // projection - with off-centre eyes ([openxr] runtime_fov=2, fov_override) it moves by half the eyes' off-axis terms apart (g_projShift, VrProj)
    // so a label is in the same direction in both eyes; centred eyes: 0, as before (D2R_DepthFog.fx LabelAxisR). Not F1's window: its eyes are
    // off-centre so that the window, the game's picture with its labels, is the same in both. Only while the right eye IS replayed: before the
    // replay is on (240 pairs into an area, after a frame of over 150 ms) the right pass is the game's own, its layer laid out for the right eye -
    // moved too, its labels stood off by the off-axis terms and jumped whenever the replay paused or came back (Quest 2 player, fov_override,
    // 2026-10-26: "the names and stuff are still fucky with reshade on")
    SetFloat(rt, "LabelAxisR", NativeView() && NativeMode() != xr::kViewsWindow && !NativeByTurns() && replay::g_rightReplayed.load() ? g_projShift.load() : 0.0f);
    {   // from above: the labels on the tilted plane
        float keepBar[4], keepMap[4];
        uint64_t srv = 0;
        const bool on = hud::LabelsNow(keepBar, keepMap, &srv);
        if (srv && srv != g_pieceBound[2]) {
            rt->update_texture_bindings("D2R_GAME_LAYER", reshade::api::resource_view{srv}, reshade::api::resource_view{srv});
            g_pieceBound[2] = srv;
        }
        SetBool(rt, "LabelsOn", on && srv == g_pieceBound[2]);
        SetBool(rt, "LayerOwnEye", LayerOwnEye());   // (each eye its own layout: the main pass takes it off - D2R_DepthFog.fx)
        const bool mask = srv && srv == g_pieceBound[2] && hud::UiMaskNow();
        SetBool(rt, "UiMaskOn", mask);
        // flat, a menu open: a pixel of a panel keeps the game's colour whole, or the sky and the fog showed through its own see-through parts
        // (from sajmonsoft's flat fork; D2R_DepthFog.fx UiMaskSolid - an older effect has no such value, and its panels are as before)
        SetBool(rt, "UiMaskSolid", mask && g_set.platform.load() == 0 && gamestate::MenuOpen());
        if (on) {   // on the floor (or for the plate alone) flat where the game puts them, each eye's own: only faded
            const bool floor = FloorView();
            const bool tilted = !floor && LabelsTilted();
            const bool body = g_enabled.load();   // first person (F4); else from above
            const platelook::LabelUniforms lu = platelook::Labels(tilted, (body ? g_set.labelsNearBody : g_set.labelsNear).load(), !body, g_set.labelsTilt.load(), floor,
                                                                  LabelBoxNative(), g_set.labelsAlphaFloor.load());
            SetFloat(rt, "LabelBase", lu.base);
            SetFloat(rt, "LabelTilt", lu.tilt);
            // (by turns the boxes too, tilted or not: each label's half-disparity is what keeps it the same in each eye every frame; not tilted,
            // the rest of the layer's shift)
            LabelDepthUniforms(rt, srv, (tilted || replay::OverlaysWanted() || LayerOwnEye()) && body, tilted ? g_set.labelsNearBody.load() * 0.005f : lu.base);
            SetFloat(rt, "LabelAlpha", lu.alpha);
            float box[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            int32_t r[4];
            const uint32_t sw = renderscale::g_screenW.load(), sh = renderscale::g_screenH.load();
            if ((PlateWanted() || AlliesMoved() || PlateTurnsWanted()) && sw && sh && gamestate::PlateRect(r)) PlateBoxUv(r, sw, sh, box);
            SetFloats(rt, "PlateBox", box, 4);
            platelook::PlateIn pin;   // the plate's own settings and the monster it hangs on: none of the item labels'
            pin.firstPerson = PlateOwnDepth();
            pin.depthPercent = g_set.plateNearBody.load();
            pin.alpha = g_set.plateAlpha.load();
            pin.size = g_set.plateSize.load();
            pin.farFraction = g_set.plateFar.load();
            pin.atMonster = g_set.plateDepthBody.load();
            float tm = 0.0f, tu = 0.0f;
            if (plateworld::GetTarget(&tm, &tu, GetTickCount64())) { pin.groundM = tm; pin.groundUnits = tu; }
            pin.m0 = g_projSx.load(); pin.halfGap = 0.5f * g_stereoIpd.load(); pin.conv = g_stereoConv.load(); pin.eyeHeightUnits = std::max(1.0f, ViewHeight());
            pin.stereo = AfrOn() && g_enabled.load() && FullBody() && pin.m0 > 0.0f && pin.halfGap > 0.0f;   // as LabelDepthWanted
            const platelook::PlateUniforms pu = platelook::Plate(pin);
            // eyes by turns: the plate's half-disparity at its target's depth - each eye then shows it the same every frame (0: no target, not by turns)
            SetFloat(rt, "PlateTurnH", PlateTurnsWanted() && pin.groundM >= 0.0f
                                           ? platelook::TurnsHalf(pin.groundUnits, pin.eyeHeightUnits, pin.m0, pin.halfGap, pin.conv) : 0.0f);
            SetBool(rt, "PlateOwn", pu.own);   // first person: the plate at its own depth, none of the item labels'
            SetFloat(rt, "PlateShift", pu.shift);
            SetFloat(rt, "PlateAlpha", pu.alpha);
            SetFloat(rt, "PlateScale", pu.scale);
            float allies[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            if (AlliesMoved() && sw && sh) AlliesBoxUv(sw, sh, allies);
            SetFloats(rt, "AlliesBox", allies, 4);
            const float alliesOff[2] = {g_set.alliesX.load(), g_set.alliesY.load()};
            SetFloats(rt, "AlliesOff", alliesOff, 2);
            float dbox[4] = {0.0f, 0.0f, 0.0f, 0.0f};   // the item-condition figure: first person only; its box, where it goes, how large, or gone
            const bool dOn = DurabilityMoved() && sw && sh && DurabilityBoxUv(sw, sh, dbox);
            if (!dOn) memset(dbox, 0, sizeof dbox);
            SetBool(rt, "DurabilityOn", dOn);
            SetFloats(rt, "DurabilityBox", dbox, 4);
            const float dOff[2] = {g_set.durX.load(), g_set.durY.load()};
            SetFloats(rt, "DurabilityOff", dOff, 2);
            SetFloat(rt, "DurabilityScale", g_set.durScale.load());
            SetBool(rt, "DurabilityHide", g_set.durHide.load());
        }
        if (on || mask) {   // the toolbar and the map in the layer: never faded, never keyed away (TableKey)
            SetFloats(rt, "LabelKeep0", keepBar, 4);
            SetFloats(rt, "LabelKeep1", keepMap, 4);
        }
    }
    SetFloat(rt, "GameBarShift", BarNearWanted() ? BarNearNow() * 0.005f : 0.0f);
    SetFloat(rt, "GameMapShift", MapNearWanted() ? g_set.mapNearFloor.load() * 0.005f : 0.0f);
    for (int i = 0; i < 2; ++i) {   // the toolbar and the map back in the picture, at their own size and depth (classic views)
        float box[4];
        uint64_t srv = 0;
        const bool on = hud::PictureNow(i, box, &srv);
        if (srv && srv != g_pieceBound[i]) {
            rt->update_texture_bindings(i ? "D2R_GAME_MAP" : "D2R_GAME_BAR", reshade::api::resource_view{srv}, reshade::api::resource_view{srv});
            g_pieceBound[i] = srv;
        }
        SetBool(rt, i ? "GameMapOn" : "GameBarOn", on && srv == g_pieceBound[i]);
        if (on) SetFloats(rt, i ? "GameMapBox" : "GameBarBox", box, 4);
    }
    // the left eye's HUD moves right, the right eye's left: nearer than the screen
    SetFloat(rt, "HudShift", HudWanted() ? g_set.uiShift.load() * 0.005f : 0.0f);
#if D2RVR_FIRST_PERSON
    {   // [debug] phantom_ray (or bone_axes): the red line the left hand takes the weapon by -
        // a crossbow's is also where it shoots ("make the phantom's vector", 2026-10-05: the
        // model must lie on this line); a staff's, spear's or two-handed sword's is its shaft
        // or hilt as the skeleton hook laid it (skel::GrabLine)
        AcquireSRWLockShared(&g_stickLock); const StickFrame f = g_stickFrame; ReleaseSRWLockShared(&g_stickLock);
        auto world = [&](const float* v, float* w) {   // head turn-only frame -> world, as HandRay
            for (int i = 0; i < 3; ++i) w[i] = v[0] * f.camR[i] - v[2] * f.camF[i];
            w[1] += v[1];
        };
        {   // each eye's camera from where the hands hang, and its projection (set here too:
            // the sky's block sets them only while the sky is drawn). Without AFR (flat) only
            // the one eye there is is kept fresh: both take it, as the sky's block does - the
            // other's stale view, written here, froze the cave ceiling in flat F3 (2026-10-07).
            SkyView sv[2];
            AcquireSRWLockShared(&g_skyLock);
            sv[0] = SkyBack(AfrOn() ? 0 : eye); sv[1] = SkyBack(AfrOn() ? 1 : eye);
            ReleaseSRWLockShared(&g_skyLock);
            // eyes by turns: each eye's overlay camera against the camera its picture shows (the warp's, xr::TurnViewNow) - off by the eyes'
            // gap, an orb on the wrist lies where the other eye sees it and shows twice (replay::TellOverlays; turn_math_test: overlay camera)
            const xr::TurnView tv = replay::OverlaysWanted() ? xr::TurnViewNow() : xr::TurnView{};
            if (tv.ok) ++replay::g_ovTell.cams;
            for (int e = 0; e < 2; ++e) {
                const bool ownView = sv[e].axes_ok && sv[e].proj_ok;
                const SkyView& s = ownView ? sv[e] : sv[0];
                float off[3];
                for (int i = 0; i < 3; ++i) off[i] = s.eyeRel[i] - g_eyeFromLook[i].load();
                if (tv.ok) {
                    const turnwarp::Camera picture{{tv.at[e][0], tv.at[e][1], tv.at[e][2]}, {tv.turn[0], tv.turn[1], tv.turn[2], tv.turn[3]}, {}, true};
                    const turnwarp::Vec at{g_eyeWorld[0].load() + off[0], g_eyeWorld[1].load() + off[1], g_eyeWorld[2].load() + off[2]};   // (the effect: from where the hands hang)
                    const turnwarp::CameraGap g = turnwarp::GapOf(picture, at, {s.axes[0], s.axes[1], s.axes[2]}, {s.axes[3], s.axes[4], s.axes[5]},
                                                                  {s.axes[6], s.axes[7], s.axes[8]});
                    replay::g_ovTell.gapAt[e] = std::max(replay::g_ovTell.gapAt[e], g.at);
                    replay::g_ovTell.gapTurn[e] = std::max(replay::g_ovTell.gapTurn[e], g.turn);
                    if (!ownView) ++replay::g_ovTell.camFallback;
                }
                // eyes by turns: each eye's overlays (the wrist medallions, the bow's orbs, the lines) from the camera its picture shows
                // (the warp's, now) - not that eye's last pass, a frame old (or the left eye's where its own view was missing): drawn so,
                // the medallion stood off the warped wrist by the head's move in a frame, or by the eyes' gap - doubled (Serg, 2026-10-28,
                // no ReShade). The projection stays the eye's own.
                float axes[9];
                for (int i = 0; i < 9; ++i) axes[i] = s.axes[i];
                if (tv.ok && e != (eye & 1)) {   // (the eye made; the eye drawn's is its own this frame)
                    const turnwarp::Quat q{tv.turn[0], tv.turn[1], tv.turn[2], tv.turn[3]};
                    const turnwarp::Vec ax[3] = {turnwarp::Rotate(q, {1, 0, 0}), turnwarp::Rotate(q, {0, 1, 0}), turnwarp::Rotate(q, {0, 0, 1})};
                    for (int k = 0; k < 3; ++k) { axes[k * 3 + 0] = ax[k].x; axes[k * 3 + 1] = ax[k].y; axes[k * 3 + 2] = ax[k].z; }
                    for (int i = 0; i < 3; ++i) off[i] = tv.at[e][i] - g_eyeWorld[i].load();
                }
                char n[16];
                snprintf(n, sizeof n, "PhantomEye%d", e); SetFloats(rt, n, off, 3);
                if (!s.axes_ok || !s.proj_ok) continue;
                snprintf(n, sizeof n, "SkyProj%d", e); SetFloats(rt, n, s.proj, 4);
                snprintf(n, sizeof n, "CamRight%d", e); SetFloats(rt, n, axes, 3);
                snprintf(n, sizeof n, "CamUp%d", e); SetFloats(rt, n, axes + 3, 3);
                snprintf(n, sizeof n, "CamBack%d", e); SetFloats(rt, n, axes + 6, 3);
            }
        }
        float o[3], d[3], ga[3], gb[3];
        const bool xbow = XbowLikeStaff() && StaffAim();
        const bool ray = PhantomWanted() && xbow && HandRay(o, d);
        const bool grab = PhantomWanted() && !xbow && f.valid && (weight::ShaftEnds(ga, gb) || skel::GrabLine(ga, gb));   // [weight]: its whole shaft
        {   // [debug] melee_boxes: the capsules, in world units from where the hands hang - the cameras above are theirs too
            static bool boxesWere = false;
            float ha[meleeboxes::kShaderCapsules][4], hb[meleeboxes::kShaderCapsules][4];
            const float eyeAt[3] = {g_eyeWorld[0].load(), g_eyeWorld[1].load(), g_eyeWorld[2].load()};
            // [debug] skill_triggers (vr/skill_triggers.h) takes its share of the effect's 32 capsules first, melee_boxes what is left
            const uint64_t boxesNow = GetTickCount64();
            const int ns = SkillTriggersWanted() ? skilltriggers::Take(boxesNow, eyeAt, ha, hb) : 0;
            int nh = ns + (MeleeBoxesWanted() ? meleeboxes::Take(boxesNow, eyeAt, ha + ns, hb + ns, meleeboxes::kMaxCapsules - ns) : 0);
            if (MeleeBoxesWanted()) {   // ... and the monsters' bodies after them ([weight] monster_bodies: their limbs, vr/monster_bodies.h) - drawn whether the blocking is on or not
                weight::ContactNote touch;
                const bool on = weight::LastContact(&touch);
                nh += meleetouch::DrawBodies(boxesNow, eyeAt, on ? touch.id : 0u, touch.bone, ha + nh, hb + nh, meleeboxes::kShaderCapsules - nh);
                nh += StabMarks(boxesNow, eyeAt, ha + nh, hb + nh, meleeboxes::kShaderCapsules - nh);   // [debug] stab_debug: the wound, its line, its depth
            }
            nh += PoseShareMarkers(eyeAt, ha + nh, hb + nh, meleeboxes::kShaderCapsules - nh);   // [multiplayer] pose_share: the other players' heads and controllers
            if (!g_paFxHas) nh += PointCastMarks(eyeAt, ha + nh, hb + nh, meleeboxes::kShaderCapsules - nh);   // [gameplay] teleport_circle: an older effect - the arc and circle as capsules
            if (nh > 0 || boxesWere) SetBool(rt, "HitboxOn", nh > 0);
            if (nh > 0) {
                SetFloats(rt, "HitboxA", &ha[0][0], (size_t)nh * 4);
                SetFloats(rt, "HitboxB", &hb[0][0], (size_t)nh * 4);
                SetInt(rt, "HitboxCount", nh);
            }
            boxesWere = nh > 0;
        }
        {   // [hud] merc_tint (vr/merc_tint.h): the wounded merc's capsule, world units from where the hands hang like the capsules above
            static bool mercWas = false;
            SkyView hv;
            AcquireSRWLockShared(&g_skyLock); hv = SkyBack(AfrOn() ? 0 : eye); ReleaseSRWLockShared(&g_skyLock);
            const float eyeAt[3] = {g_eyeWorld[0].load(), g_eyeWorld[1].load(), g_eyeWorld[2].load()};
            merctint::Uniforms mu;
            const bool merc = MercTintWanted() && merctint::Current(std::max(1.0f, ViewHeight()) / UserEyeHeightM(), eyeAt, hv.hero[1], &mu);
            if (merc || mercWas) SetBool(rt, "MercOn", merc);
            if (merc) {
                SetFloats(rt, "MercA", mu.a, 4);
                SetFloats(rt, "MercB", mu.b, 4);
            }
            mercWas = merc;
        }
        {   // [gameplay] teleport_circle: the point skill's arc as an energy beam and its circle as a glowing ring (the effect's PointArc, reshade/D2R_DepthFog.fx) - the
            // arc's points shaken afresh every pointcast::kJitterMs (its crackle), [gameplay] point_arc_color / point_arc_opacity; world units from where the hands hang
            static bool paWas = false;
            static uint32_t paSeed = 0;
            static ULONGLONG paShookAt = 0;
            g_paFxHas = rt->find_uniform_variable("D2R_DepthFog.fx", "PaOn").handle != 0;   // (an older effect without it: the capsules above stand in)
            pointcast::Arc arc;
            const bool shown = pointwire::Shown(&arc);
            const bool on = g_paFxHas && shown;
            if (shown) pointwire::NoteDrawn(g_paFxHas);   // (the release's log line counts the presents that drew it)
            if (on || paWas) SetBool(rt, "PaOn", on);
            if (on) {
                const float eyeAt[3] = {g_eyeWorld[0].load(), g_eyeWorld[1].load(), g_eyeWorld[2].load()};
                const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
                if (const ULONGLONG now = GetTickCount64(); now - paShookAt >= pointcast::kJitterMs) { paShookAt = now; ++paSeed; }
                pointcast::Jitter(&arc, paSeed, pointcast::kJitterM * upm);
                float pts[pointcast::kArcPoints][4];
                for (int i = 0; i < pointcast::kArcPoints; ++i) {
                    const pointcast::V3& q = arc.pts[i < arc.n ? i : arc.n - 1];
                    pts[i][0] = q.x - eyeAt[0]; pts[i][1] = q.y - eyeAt[1]; pts[i][2] = q.z - eyeAt[2]; pts[i][3] = float(i) / float(pointcast::kArcPoints - 1);
                }
                SetFloats(rt, "PaPts", &pts[0][0], (size_t)pointcast::kArcPoints * 4);
                const float L[3] = {g_gazeLook[0].load(), g_gazeLook[1].load(), g_gazeLook[2].load()};
                const float ring[4] = {L[0] + arc.land.x - eyeAt[0], L[1] + 0.02f * upm - eyeAt[1], L[2] + arc.land.z - eyeAt[2], pointcast::kRingRadiusM * upm};
                SetFloats(rt, "PaRing", ring, 4);
                // the skill's colour: its own, else its element's ([gameplay] point_arc_by_skill), or the one for all
                const int sk = arc.skill;
                const uint32_t ownBits = sk >= 0 && sk < 1024 ? g_set.pointArcOwn[sk].load() : 0u;
                const float own[3] = {((ownBits >> 16) & 0xFF) / 255.0f, ((ownBits >> 8) & 0xFF) / 255.0f, (ownBits & 0xFF) / 255.0f};
                const float all[3] = {g_set.pointArcRgb[0].load(), g_set.pointArcRgb[1].load(), g_set.pointArcRgb[2].load()};
                float rgb[3];
                pointcast::SkillRgb(g_set.pointArcBySkill.load(), sk, ownBits != 0, own, g_set.pointArcAllSet.load(), all, rgb);
                const float look[4] = {rgb[0], rgb[1], rgb[2], g_set.pointArcOpacity.load()};
                SetFloats(rt, "PaLook", look, 4);
                const float size[4] = {pointcast::kBeamCoreM * upm, pointcast::kRingLineM * upm, (float)fmod((double)QpcUs() * 1e-6, 1000.0), pointcast::kMinLineRad};
                SetFloats(rt, "PaSize", size, 4);
            }
            paWas = on;
        }
        {   // [hud] hand_vitals_style orbs (vr/hand_orbs.h): the life and mana balls on the wrists - the hero's arm as posed, the numbers, into the effect's
            // uniforms (world units, relative to where the hands hang, like the capsules above); the effect ray-casts them per eye with the game's depth
            static handorbs::State orbState;
            static bool orbsWere = false;
            static handorbs::Tracker orbTrack;   // the wrists' frames: the drawn arm, or the last one carried on by the controller
            static unsigned orbSrcLogged[2] = {};
            handorbs::Uniforms ou;
            const bool wantGlobes = HandOrbsWanted() && handvitals::Settings().style == handvitals::kStyleGlobes;
            hud::SetGlobes(wantGlobes);   // the game's own globes cut out of its interface (hud.cpp), only while that look is on
            if (HandOrbsWanted() && f.valid) {
                handorbs::In in;
                in.cfg = handvitals::Settings();
                in.vitalsOk = handvitals::Snapshot(&in.v);
                in.time = pairtime::UsNow() * 1e-6;
                in.heroClass = skel::HeroClass();   // the class of the hero playing now (the model's folder): its depth offset is on both wrists' Z
                in.shieldWrist = viewgate::F4() ? handorbs::ShieldWrist(skel::ShieldStrapped(), gamestate::LeftShield()) : -1;   // the wrist under a shield rolls its ball to the other side (VR F4 only)
                {   // once per change: which hero, and the offset its class has
                    static int toldHero = -2;
                    if (in.heroClass != toldHero) {
                        toldHero = in.heroClass;
                        if (in.heroClass >= 0) LogF("vrcam: hero class: %s - the wrist medallions' depth offset for it is %+.1f cm ([hud] hand_vitals_orb_z_cm_%s); under a shield %+.1f cm deep, %+.1f cm along, turned %+.0f / %+.0f / %+.0f degrees ([hud] hand_vitals_orb_shield_up_cm_%s / _along_cm_%s / _rot_x_deg_%s ...)",
                                                    handvitals::HeroName(in.heroClass), handvitals::ClassZCm(in.cfg, in.heroClass), handvitals::HeroKey(in.heroClass), handvitals::ClassShieldUpCm(in.cfg, in.heroClass),
                                                    handvitals::ClassShieldAlongCm(in.cfg, in.heroClass), handvitals::ClassShieldRotDeg(in.cfg, in.heroClass).xDeg, handvitals::ClassShieldRotDeg(in.cfg, in.heroClass).yDeg,
                                                    handvitals::ClassShieldRotDeg(in.cfg, in.heroClass).zDeg, handvitals::HeroKey(in.heroClass), handvitals::HeroKey(in.heroClass), handvitals::HeroKey(in.heroClass));
                        else Log("vrcam: hero class: not known - the wrist medallions use their own depth, no class offset");
                    }
                }
                const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
                // Where the arm is DRAWN, the way a belt bottle rides the palm: the skeleton hook's elbow, wrist and hand frame in the WORLD, made at the very pose with the hero's
                // own model-to-world (skel::HeroArmWorld) - exact whatever the IK or the [arms] offsets do, and from the same instant as the drawn hand (no camera frame in
                // between, so no lag when the head turns). Eye-relative metres in the world's own axes: the frame handorbs works in.
                const float eyeW[3] = {g_eyeWorld[0].load(), g_eyeWorld[1].load(), g_eyeWorld[2].load()};
                // The wrist's frame every frame (vr/hand_orbs.h Tracker): the drawn arm when the skeleton read it this frame; when it did not (the entity tables are looked for only
                // twice a second after a miss, the hero is loading, a menu), the LAST drawn frame carried on by the controller's own pose - the hand is held to the controller, so
                // the wrist and the hand's axes keep their place in the grip's axes and the frame still turns with the hand; then the skeleton's hand-frame forearm through the
                // head's turn (no wrist bend); then the controller's point alone. Both eyes' passes use these one set of uniforms.
                const float off[3] = {g_set.handSide.load() * 0.01f, g_set.handUp.load() * 0.01f, -g_set.handFwd.load() * 0.01f};
                static const float kNoOff[3] = {};
                auto handToWorldM = [&](const float* v, const float* off3, handorbs::V3* out) {   // hand-frame metres (+ the offset) -> eye-relative world metres
                    const float vv[3] = {v[0] + off3[0], v[1] + off3[1], v[2] + off3[2]};
                    float w[3];
                    world(vv, w);
                    *out = {w[0], w[1], w[2]};
                };
                const D2RVR_Shared* sh = Shared();
                const bool shOk = sh && sh->version == D2RVR_SHARED_VERSION && sh->headValid;
                for (int side = 0; side < 2; ++side) {   // the skeleton's: 0 right, 1 left
                    handorbs::Wrist w;
                    if (shOk && (sh->handsValid & (side == 0 ? 1u : 2u))) {   // the controller: its point, and its axes from its orientation (the hand frame -> the world)
                        w.grip.ok = true;
                        handToWorldM(side == 0 ? sh->rightHand : sh->leftHand, kNoOff, &w.grip.pos);
                        handorbs::V3 ax[3];
                        if (handorbs::AxesFromQuat(side == 0 ? sh->rightRot : sh->leftRot, ax)) {
                            handorbs::V3* dst[3] = {&w.grip.x, &w.grip.y, &w.grip.z};
                            for (int i = 0; i < 3; ++i) {
                                const float v[3] = {ax[i].x, ax[i].y, ax[i].z};
                                float wv[3];
                                world(v, wv);
                                *dst[i] = hset::Unit({wv[0], wv[1], wv[2]});
                            }
                            w.grip.hasAxes = true;
                        }
                        in.grip[side] = w.grip;   // (the point fallback of Step adds the arms' offset the hand is drawn off the controller by)
                        handToWorldM(side == 0 ? sh->rightHand : sh->leftHand, off, &in.grip[side].pos);
                    }
                    skel::HeroArm ha;
                    unsigned ageMs = 0xFFFFFFFFu;
                    if (skel::HeroArmWorld(side, &ha, 1000u, &ageMs)) {
                        handorbs::WorldArm m;
                        m.elbow = {ha.elbow[0], ha.elbow[1], ha.elbow[2]};
                        m.wrist = {ha.wrist[0], ha.wrist[1], ha.wrist[2]};
                        m.across = {ha.across[0], ha.across[1], ha.across[2]};
                        m.point = {ha.point[0], ha.point[1], ha.point[2]};
                        m.face = {ha.face[0], ha.face[1], ha.face[2]};
                        m.hand = ha.hand;
                        m.box = ha.box;   // [hud] hand_vitals_orb_on_bone: the forearm bone's mesh box, posed with it
                        m.boxC = {ha.boxC[0], ha.boxC[1], ha.boxC[2]};
                        for (int i = 0; i < 3; ++i) m.boxH[i] = {ha.boxH[i][0], ha.boxH[i][1], ha.boxH[i][2]};
                        w.drawn = handorbs::ArmFromWorld(eyeW, upm, m);
                        w.drawnOk = w.drawn.ok;
                        w.drawnAgeS = (double)ageMs * 0.001;
                    }
                    float e[3], wr[3], x[3];
                    if (skel::HeroForearm(side, e, wr, x)) {
                        w.forearmOk = w.forearm.ok = true;
                        handToWorldM(e, off, &w.forearm.elbow);
                        handToWorldM(wr, off, &w.forearm.wrist);
                        float xw[3];
                        world(x, xw);
                        w.forearm.across = {xw[0], xw[1], xw[2]};
                    }
                    const handorbs::Resolved r = orbTrack.Resolve(side, w, in.time);
                    in.arm[side] = r.arm;
                    if (r.changed) {   // once per change: where the wrist's frame comes from, and how long the last one lasted
                        const unsigned n = ++orbSrcLogged[side];
                        if (n <= 60 || n % 25 == 0) {
                            const char* why = r.src == handorbs::Src::Drawn ? "" : skel::HeroArmWhy();
                            char b[420];
                            int len = snprintf(b, sizeof b, "vrcam: hand vitals - the %s wrist's frame: %s", side == 0 ? "right" : "left", handorbs::SrcName(r.src));
                            if (r.from != handorbs::Src::None && len > 0 && len < (int)sizeof b)
                                len += snprintf(b + len, sizeof b - len, " (%s lasted %.2f s)", handorbs::SrcName(r.from), r.lastedS);
                            if (why && *why && len > 0 && len < (int)sizeof b) len += snprintf(b + len, sizeof b - len, "; the drawn wrist was not read: %s", why);
                            if (n > 60 && len > 0 && len < (int)sizeof b) snprintf(b + len, sizeof b - len, " (every 25th change is logged now)");
                            Log(b);
                        }
                    }
                }
                const handorbs::Set set = handorbs::Step(orbState, in);
                float fit = 0.0f;
                uint64_t srv = 0;
                bool cutOk = true;
                if (set.globes) {   // the game's own globes: the cut must be there, else nothing is drawn and the log says why (once per reason)
                    cutOk = hud::GlobesNow(&fit, &srv);
                    if (!cutOk) {
                        static int toldWhy = -1;
                        const int why = hud::GlobesWhy();
                        if (set.any && why != toldWhy && why >= 2) {
                            toldWhy = why;
                            Log(why == 2 ? "vrcam: hand vitals - the game's own globes cannot be cut: its interface layer is too small, or its safe area does not hold them whole (Safe Screen Percent): nothing is drawn"
                                         : "vrcam: hand vitals - the texture for the game's own globes could not be made: nothing is drawn");
                        }
                    } else if (srv != g_pieceBound[3]) {
                        rt->update_texture_bindings("D2R_GAME_GLOBES", reshade::api::resource_view{srv}, reshade::api::resource_view{srv});
                        g_pieceBound[3] = srv;
                    }
                }
                static const float kWorldR[3] = {1.0f, 0.0f, 0.0f}, kWorldF[3] = {0.0f, 0.0f, -1.0f};   // identity: forward -z, so (x, y, z) goes through as it is
                ou = handorbs::Pack(set, kWorldR, kWorldF, upm, in.time, fit);
                if (set.globes && !cutOk) ou.on = false;
            } else {
                orbState.Reset();
                orbTrack.Reset();
            }
            if (ou.on && ou.picture && rt->find_uniform_variable("D2R_DepthFog.fx", "OrbPic").handle == 0) {
                static bool toldPic = false;
                if (!toldPic) { toldPic = true; Log("vrcam: hand vitals - D2R_DepthFog.fx has no OrbPic: it is an older one, copy the new one in (the game's own globes need the DLL and the effect together)"); }
                ou.on = false;
            }
            if (ou.on && rt->find_uniform_variable("D2R_DepthFog.fx", "OrbOn").handle == 0) {
                static bool told = false;
                if (!told) { told = true; Log("vrcam: hand vitals - D2R_DepthFog.fx has no OrbOn: it is an older one, copy the new one in (the orbs need the DLL and the effect together)"); }
                ou.on = false;
            }
            if (ou.on || orbsWere) SetBool(rt, "OrbOn", ou.on);
            if (ou.on) {
                SetFloats(rt, "OrbPos", &ou.pos[0][0], 8);
                SetFloats(rt, "OrbLook", &ou.look[0][0], 8);
                SetFloats(rt, "OrbAxis", &ou.axis[0][0], 8);
                SetFloats(rt, "OrbBack", &ou.back[0][0], 8);
                SetFloats(rt, "OrbAlong", &ou.along[0][0], 8);
                SetFloats(rt, "OrbMed", ou.med, 4);
                if (rt->find_uniform_variable("D2R_DepthFog.fx", "OrbMed").handle == 0) {
                    static bool toldMed = false;
                    if (!toldMed) { toldMed = true; Log("vrcam: hand vitals - D2R_DepthFog.fx has no OrbMed: it is an older one, so the balls stay full glass balls (copy the new effect in for the medallion)"); }
                }
                if (rt->find_uniform_variable("D2R_DepthFog.fx", "OrbAxis").handle == 0) {
                    static bool toldAxis = false;
                    if (!toldAxis) { toldAxis = true; Log("vrcam: hand vitals - D2R_DepthFog.fx has no OrbAxis: it is an older one, so the liquid stays level in the world (copy the new effect in)"); }
                }
                SetFloat(rt, "OrbTime", ou.time);
                const float pic[2] = {ou.picture ? 1.0f : 0.0f, ou.fit};
                SetFloats(rt, "OrbPic", pic, 2);
            }
            {   // [gameplay] melee_skill_armed_icon (vr/armed_icon.h): the armed melee skill's icon on the bottom of the mana medallion - full while the hero can pay the next hit, faint when
                // he cannot. The game's own icon (vr/wheel_icons.h, the skill panel's), baked to a circle and uploaded into OrbIconTex once per skill; OrbIcon says whether and how strong.
                static armedicon::Fader iconFade;
                static double iconLast = 0.0;
                static bool iconWas = false;
                const double nowS = pairtime::UsNow() * 1e-6;
                const float dt = iconLast > 0.0 ? (float)(nowS - iconLast) : 0.0f;
                iconLast = nowS;
                const handvitals::Config hc = handvitals::Settings();
                armedicon::In ii;
                // whose: the bow skill armed from the quiver while a bow or crossbow is in hand ([bow] quiver_icon), else the armed melee skill ([gameplay] melee_skill_armed_icon)
                bool meleeReady = false, bowReady = false;
                int bowSkill = -1;
                const int meleeSkill = meleearm::IconOn() && meleearm::On() ? meleearm::Armed(&meleeReady) : -1;
                bowquiverwire::ChosenIcon(&bowSkill, &bowReady);
                const armedicon::Source src = armedicon::Pick(meleeSkill, meleeReady, bowSkill, bowReady);
                ii.on = ii.armedOn = (meleearm::IconOn() && meleearm::On()) || bowSkill >= 0;
                ii.f4 = viewgate::F4();
                ii.medallion = ou.on && !ou.picture && hc.style == handvitals::kStyleOrbs && hc.orbShape == handvitals::kShapeMedallion;
                ii.armed = src.skill;
                ii.enough = src.enough;
                const armedicon::Place place = meleearm::IconPlace();   // the sliders (melee_skill_armed_icon_*): the bow's icon lies the same way
                ii.full = place.opacity;
                ii.faint = place.faint;
                armedicon::Out io = iconFade.Step(ii, dt);
                if (io.show) {
                    const reshade::api::effect_texture_variable tv = rt->find_texture_variable("D2R_DepthFog.fx", "OrbIconTex");
                    const uint32_t rev = wheelicons::Revision();
                    if (!tv.handle) {
                        static bool told = false;
                        if (!told) { told = true; Log("vrcam: melee armed - D2R_DepthFog.fx has no OrbIconTex: it is an older one, copy the new one in (the armed skill's icon needs the DLL and the effect together)"); }
                        io.show = false;
                    } else if ((io.skill != g_iconUpSkill || tv.handle != g_iconUpVar) && (io.skill != g_iconTry || rev != g_iconUpRev || tv.handle != g_iconUpVar)) {
                        // a try per skill, and again when the cache decoded something (Revision) or the effects were reloaded - not every frame while an icon is missing
                        g_iconUpSkill = -1;
                        g_iconTry = io.skill;
                        g_iconUpRev = rev;
                        g_iconUpVar = tv.handle;
                        const std::shared_ptr<const wheelicons::Image> img = wheelicons::Get(io.skill);   // (null while the worker decodes it, and for a skill the cache has no icon for)
                        static std::vector<uint8_t> baked;
                        if (img && armedicon::Bake(img->rgba.data(), img->width, img->height, baked)) {
                            rt->update_texture(tv, armedicon::kTex, armedicon::kTex, baked.data());
                            g_iconUpSkill = io.skill;
                            LogF("vrcam: skill icon - skill %d (%s) is in the mana medallion (%ux%u from the icon cache)", io.skill, meleearm::IsMelee(io.skill) ? meleearm::Name(io.skill) : "the bow's chosen skill", img->width, img->height);
                        } else if (!wheelicons::Available()) {
                            static bool told = false;
                            if (!told) { told = true; Log("vrcam: melee armed - no skill icons on this machine (d2r_vr_skill_icons, made by tools/skill_icons/prepare_skill_icons.py from the installed game): the mana medallion shows no icon"); }
                        }
                    }
                    if (io.skill != g_iconUpSkill) io.show = false;   // its icon is not in the texture (yet): nothing rather than the last skill's
                }
                float iu[4];
                armedicon::Uniform(io, iu, place);
                if (io.show || iconWas) {
                    SetFloats(rt, "OrbIcon", iu, 4);
                    SetFloats(rt, "OrbIconLift", &place.lift, 1);   // (an older effect has none: the icon stays on the bottom)
                    const float liftM = g_iconLiftMm.load() * 0.001f;
                    SetFloats(rt, "OrbIconLiftM", &liftM, 1);
                }
                iconWas = io.show;
            }
            if (ou.on && !orbsWere) Log(ou.picture ? "vrcam: hand vitals - the game's own globes are in the picture (life on the left wrist, mana on the right, by the settings)"
                                                  : "vrcam: hand vitals - the 3D orbs are in the picture (life on the left wrist, mana on the right, by the settings)");
            orbsWere = ou.on;
        }
        SetBool(rt, "PhantomOn", ray || grab);
        if (ray) {
            const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();   // world units per metre
            float a[3], b[3];
            for (int i = 0; i < 3; ++i) { a[i] = o[i] - g_eyeWorld[i].load(); b[i] = a[i] + d[i] * 1.5f * upm; }
            SetFloats(rt, "PhantomA", a, 3);
            SetFloats(rt, "PhantomB", b, 3);
        } else if (grab) {
            const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
            float a[3], b[3];
            world(ga, a); world(gb, b);
            for (int i = 0; i < 3; ++i) { a[i] *= upm; b[i] *= upm; }
            SetFloats(rt, "PhantomA", a, 3);
            SetFloats(rt, "PhantomB", b, 3);
        }
        // and the bone of the weapon in hand, its own axes from its origin, 0.5 m each: X green, Y blue, Z yellow
        float bo[3], bx[3][3];
        const bool bone = BoneAxesWanted() && skel::GunBone(bo, bx);
        SetBool(rt, "BoneOn", bone);
        if (bone && f.valid) {
            const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
            float o[3]; world(bo, o);
            for (float& v : o) v *= upm;
            SetFloats(rt, "BoneO", o, 3);
            static const char* const kNames[3] = {"BoneX", "BoneY", "BoneZ"};
            for (int i = 0; i < 3; ++i) {
                float d[3]; world(bx[i], d);
                for (int k = 0; k < 3; ++k) d[k] = o[k] + d[k] * 0.5f * upm;
                SetFloats(rt, kNames[i], d, 3);
            }
        }
        {   // [debug] bow_vectors: the bow draw's segments and balls (vr/bow_touch.h), from the hands' frame into the world, as the lines above
            bowtouch::VectorSet vs;
            bool on = BowVectorsWanted() && f.valid && bowdrawwire::DebugVectors(&vs);
            {   // [debug] bow_aim_ray: the arrow's ray, live while the string is drawn: green through the model's arrow rest point, orange the plain hand-set line; to the floor (a full ball)
                // or 8 m out (a ring). The hands' frame has the head at its origin: the floor is the user's eye height below it. bow_vectors' blue aim segment gives its place to it.
                bowtouch::Vec rs, rd;
                bool model = false;
                bowtouch::AimRay ray;
                if (BowAimRayWanted() && f.valid && bowdrawwire::AimRayLine(&rs, &rd, &model) && bowtouch::MakeAimRay(rs, rd, model, -UserEyeHeightM(), &ray)) {
                    PitchRayEnd(rs, rd, &ray);   // [bow] pitch_aim: to what the shot goes to (a red ball on a monster, a full one on the ground)
                    if (bowtouch::AddAimRay(vs, ray)) on = true;
                }
            }
            // [bow] quiver: the glow of the loaded skill on the nocked arrow, added to what is there (bow_vectors, when on, has the picture first; a full one keeps it)
            if (BowQuiverWanted() && f.valid && bowquiverwire::AddGlow(&vs)) on = true;
            // [debug] phantom_ray with [weight] grip_fit: on each fitted hand a yellow dot where it takes the shaft (the grasp point on the red line), a green ring where its palm's
            // middle really is, and a white line between them - zero corrections and a reaching arm, and the two coincide. (No bow in hand: the bow's vectors own the overlay.)
            if (!on && PhantomWanted() && f.valid) {
                for (int side = 0; side < 2; ++side) {
                    float g[3], p[3];
                    if (!skel::GripMarks(side, g, p) || vs.nball + 2 > bowtouch::kMaxBalls || vs.nseg >= bowtouch::kMaxSegs) continue;
                    bowtouch::Ball& dot = vs.ball[vs.nball++];
                    dot.c = {g[0], g[1], g[2]}; dot.r = 0.012f; dot.rgb[0] = 1.0f; dot.rgb[1] = 0.9f; dot.rgb[2] = 0.1f; dot.fill = 1.0f;
                    bowtouch::Ball& ring = vs.ball[vs.nball++];
                    ring.c = {p[0], p[1], p[2]}; ring.r = 0.02f; ring.rgb[0] = 0.2f; ring.rgb[1] = 1.0f; ring.rgb[2] = 0.3f; ring.fill = 0.0f;
                    bowtouch::Seg& line = vs.seg[vs.nseg++];
                    line.a = {g[0], g[1], g[2]}; line.b = {p[0], p[1], p[2]}; line.rgb[0] = line.rgb[1] = line.rgb[2] = 1.0f;
                }
                on = vs.nball > 0;
            }
            // [debug] xbow_barrel_line: the crossbow's barrel as drawn (its bolt_helper, cyan, 40 cm back and 30 ahead of the front of the stock)
            // and where [hands] xbow_support_model puts the left palm (a magenta ball) - lay the red shot line (phantom_ray) beside it to compare
            if (XbowBarrelWanted() && f.valid) {
                float ba[3], bb[3], bp[3];
                bool drawn = false;   // cyan: the barrel as the renderer draws the crossbow; orange: from the item's own transform (not how it is drawn)
                if (skel::XbowBarrelDebug(ba, bb, bp, &drawn) && vs.nseg < bowtouch::kMaxSegs && vs.nball < bowtouch::kMaxBalls) {
                    bowtouch::Seg& line = vs.seg[vs.nseg++];
                    line.a = {ba[0], ba[1], ba[2]}; line.b = {bb[0], bb[1], bb[2]};
                    line.rgb[0] = drawn ? 0.1f : 1.0f; line.rgb[1] = drawn ? 0.9f : 0.55f; line.rgb[2] = drawn ? 1.0f : 0.0f;
                    bowtouch::Ball& ball = vs.ball[vs.nball++];
                    ball.c = {bp[0], bp[1], bp[2]}; ball.r = 0.015f; ball.rgb[0] = 1.0f; ball.rgb[1] = 0.2f; ball.rgb[2] = 1.0f; ball.fill = 1.0f;
                    on = true;
                }
                // and where the shot goes, live: green along the drawn barrel ([hands] xbow_aim_barrel), orange along the hand-set shot line
                // (unticked, or the barrel not read) - from the muzzle to the floor (a full ball there) or 8 m out (a ring)
                float ra[3], rb[3];
                bool fromBarrel = false, floor = false;
                if (skel::XbowShotRay(ra, rb, &fromBarrel, &floor) && vs.nseg < bowtouch::kMaxSegs && vs.nball < bowtouch::kMaxBalls) {
                    const float rgb[3] = {fromBarrel ? 0.2f : 1.0f, fromBarrel ? 1.0f : 0.45f, fromBarrel ? 0.3f : 0.05f};
                    bowtouch::Seg& ray = vs.seg[vs.nseg++];
                    ray.a = {ra[0], ra[1], ra[2]}; ray.b = {rb[0], rb[1], rb[2]};
                    for (int k = 0; k < 3; ++k) ray.rgb[k] = rgb[k];
                    bowtouch::Ball& hit = vs.ball[vs.nball++];
                    hit.c = {rb[0], rb[1], rb[2]}; hit.r = 0.06f;
                    for (int k = 0; k < 3; ++k) hit.rgb[k] = rgb[k];
                    hit.fill = floor ? 1.0f : 0.0f;
                    on = true;
                }
                // and where the left palm's middle really is: a white ring - on the magenta ball when the hand holds by its palm
                float pp[3];
                if (skel::XbowPalmDebug(pp) && vs.nball < bowtouch::kMaxBalls) {
                    bowtouch::Ball& palm = vs.ball[vs.nball++];
                    palm.c = {pp[0], pp[1], pp[2]}; palm.r = 0.025f; palm.rgb[0] = palm.rgb[1] = palm.rgb[2] = 1.0f; palm.fill = 0.0f;
                    on = true;
                }
            }
            // [debug] spell_aim_ray: where a spell goes, live, as the crossbow's shot ray is drawn ([debug] xbow_barrel_line): from where the aim starts along
            // the direction it uses - green from the model ([hands] spell_aim_model: the staff, wand or orb as drawn, else the drawn hand), orange the old
            // controller line (unticked, or no fresh pose) - to the floor (a full ball there) or 8 m out (a ring). The game aims by its heading on the ground.
            if (SpellRayWanted() && f.valid && g_eyeOk.load() && vs.nseg < bowtouch::kMaxSegs && vs.nball < bowtouch::kMaxBalls) {
                float A[3], D[3];
                bool model = false, have = false;
                float armW = 0.0f;
                if (SpellModelWanted()) {
                    const spellmodel::Out m = SpellModelAim();
                    if (m.src != spellmodel::Src::None) {
                        for (int k = 0; k < 3; ++k) { A[k] = m.origin[k]; D[k] = m.dir[k]; }
                        model = have = true;
                        // [hands] staff_upright_arm: an upright staff's line leans on the arm - that arm drawn too, a thin light-blue line shoulder -> palm
                        armW = m.armW;
                        if (armW > 0.0f && vs.nseg + 1 < bowtouch::kMaxSegs) {
                            bowtouch::Seg& arm = vs.seg[vs.nseg++];
                            arm.a = {m.shoulder[0], m.shoulder[1], m.shoulder[2]};
                            arm.b = {m.armHand[0], m.armHand[1], m.armHand[2]};
                            arm.rgb[0] = 0.55f; arm.rgb[1] = 0.8f; arm.rgb[2] = 1.0f;
                        }
                    }
                }
                float wo[3], wd[3];
                if (!have && HandRay(wo, wd)) {   // (no model line: HandRay is the old one) world -> the head's turn-only frame (camR, camF on the ground), from the eye, metres
                    const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
                    auto head = [&](const float* v, float* h) {
                        h[0] = v[0] * f.camR[0] + v[2] * f.camR[2];
                        h[1] = v[1];
                        h[2] = -(v[0] * f.camF[0] + v[2] * f.camF[2]);
                    };
                    const float rel[3] = {(wo[0] - g_eyeWorld[0].load()) / upm, (wo[1] - g_eyeWorld[1].load()) / upm, (wo[2] - g_eyeWorld[2].load()) / upm};
                    head(rel, A); head(wd, D);
                    have = true;
                }
                const float dl = have ? sqrtf(D[0] * D[0] + D[1] * D[1] + D[2] * D[2]) : 0.0f;
                if (dl > 1e-6f && std::isfinite(dl) && std::isfinite(A[0] + A[1] + A[2])) {
                    for (float& v : D) v /= dl;
                    const float floorY = -std::max(0.8f, UserEyeHeightM());   // the floor: the user's eye height below the eye (the frame's y is from the eye)
                    float len = 8.0f;
                    bool floor = false;
                    float armBase = 0.0f, armFarCells = 0.0f;
                    if (SpellArmCells(&armBase, &armFarCells))
                        if (const float m = ArmRayMetres(armBase, armFarCells); m > 0.0f)   // [bow] aim_distance_by_arm (a spell's): the ray ends where the aim point is, on the ground along it
                            len = std::clamp(m / std::max(sqrtf(D[0] * D[0] + D[2] * D[2]), 0.05f), 1.0f, 60.0f);
                    if (D[1] < -0.02f) {
                        const float t = (floorY - A[1]) / D[1];
                        if (t > 0.0f && t < len) { len = t; floor = true; }
                    }
                    // the crossbow ray's colours; an upright staff's line, by the arm's share in it, from green toward blue (all arm)
                    const float rgb[3] = {model ? 0.2f : 1.0f, model ? 1.0f - 0.45f * armW : 0.45f, model ? 0.3f + 0.7f * armW : 0.05f};
                    bowtouch::Seg& ray = vs.seg[vs.nseg++];
                    ray.a = {A[0], A[1], A[2]};
                    ray.b = {A[0] + D[0] * len, A[1] + D[1] * len, A[2] + D[2] * len};
                    for (int k = 0; k < 3; ++k) ray.rgb[k] = rgb[k];
                    bowtouch::Ball& hit = vs.ball[vs.nball++];
                    hit.c = ray.b; hit.r = 0.06f;
                    for (int k = 0; k < 3; ++k) hit.rgb[k] = rgb[k];
                    hit.fill = floor ? 1.0f : 0.0f;
                    on = true;
                }
            }
            SetBool(rt, "DbgOn", on);
            if (on) {
                const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
                float a[bowtouch::kMaxSegs * 4] = {}, b[bowtouch::kMaxSegs * 4] = {}, sc[bowtouch::kMaxSegs * 4] = {};
                float ball[bowtouch::kMaxBalls * 4] = {}, bc[bowtouch::kMaxBalls * 4] = {};
                for (int i = 0; i < vs.nseg; ++i) {
                    world(&vs.seg[i].a.x, a + i * 4); world(&vs.seg[i].b.x, b + i * 4);
                    for (int k = 0; k < 3; ++k) { a[i * 4 + k] *= upm; b[i * 4 + k] *= upm; sc[i * 4 + k] = vs.seg[i].rgb[k]; }
                    sc[i * 4 + 3] = 1.0f;
                }
                for (int i = 0; i < vs.nball; ++i) {
                    world(&vs.ball[i].c.x, ball + i * 4);
                    for (int k = 0; k < 3; ++k) { ball[i * 4 + k] *= upm; bc[i * 4 + k] = vs.ball[i].rgb[k]; }
                    ball[i * 4 + 3] = vs.ball[i].r * upm;
                    bc[i * 4 + 3] = vs.ball[i].fill;
                }
                SetFloat(rt, "DbgSegN", (float)vs.nseg);
                SetFloats(rt, "DbgSegA", a, sizeof a / sizeof a[0]);
                SetFloats(rt, "DbgSegB", b, sizeof b / sizeof b[0]);
                SetFloats(rt, "DbgSegC", sc, sizeof sc / sizeof sc[0]);
                SetFloat(rt, "DbgBallN", (float)vs.nball);
                SetFloats(rt, "DbgBall", ball, sizeof ball / sizeof ball[0]);
                SetFloats(rt, "DbgBallC", bc, sizeof bc / sizeof bc[0]);
            }
        }
        {   // [bow] arrow_fx: the nocked arrow's fire, frost or arcane light (vr/bow_quiver.h Element), from the hands' frame into the world as the lines above; AfOn 0 draws nothing
            bowquiverwire::ArrowFxDraw af;
            bool on = BowArrowFxWanted() && f.valid && bowquiverwire::ArrowFx(&af);
            if (on && rt->find_uniform_variable("D2R_DepthFog.fx", "AfOn").handle == 0) {   // an older effect: the coloured glow stands in
                bowquiverwire::ArrowFxEffectMissing();
                on = false;
            }
            SetFloat(rt, "AfOn", on ? (float)af.kind : 0.0f);
            if (on) {
                const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
                float tail[4] = {}, head[4] = {};
                world(af.tail, tail);
                world(af.head, head);
                for (int k = 0; k < 3; ++k) { tail[k] *= upm; head[k] *= upm; }
                tail[3] = af.size * upm;
                head[3] = af.strength;
                SetFloats(rt, "AfA", tail, 4);
                SetFloats(rt, "AfB", head, 4);
                SetFloat(rt, "AfTime", (float)fmod((double)GetTickCount64() * 0.001, 600.0));
            }
        }
        {   // [bow] quiver_orbs: the energy orbs along the bow's stave (vr/bow_orbs.h), from the hands' frame into the world as the lines above; QvN 0 draws nothing
            bowquiverwire::OrbDraw od;
            const bool on = BowOrbsWanted() && f.valid && bowquiverwire::Orbs(&od);
            SetFloat(rt, "QvN", on ? (float)od.n : 0.0f);
            if (on) {
                const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
                float pos[12 * 4] = {}, col[12 * 4] = {};
                for (int i = 0; i < od.n && i < 12; ++i) {
                    world(od.pos[i], pos + i * 4);
                    for (int k = 0; k < 3; ++k) { pos[i * 4 + k] *= upm; col[i * 4 + k] = od.rgb[i][k]; }
                    pos[i * 4 + 3] = od.radius[i] * upm;
                    col[i * 4 + 3] = (od.current[i] ? 2.0f : 0.0f) + od.hover[i];   // (the effect: 2 * current + hover)
                }
                SetFloats(rt, "QvPos", pos, sizeof pos / sizeof pos[0]);
                SetFloats(rt, "QvCol", col, sizeof col / sizeof col[0]);
                SetFloat(rt, "QvTime", (float)fmod((double)GetTickCount64() * 0.001, 600.0));
                static bool toldOld = false;
                if (!toldOld && rt->find_uniform_variable("D2R_DepthFog.fx", "QvN").handle == 0) {
                    toldOld = true;
                    Log("vrcam: bow quiver orbs - D2R_DepthFog.fx has no QvN: it is an older effect, so the orbs are not drawn (copy the new effect in)");
                }
            }
        }
    }
#endif
    SetFloat(rt, "HudTop", g_set.hudTop.load());
    // the interface told by "the same in both eyes" only while presents alternate the eyes (VR stereo): in flat one view, its still world
    // below HudTop was taken for the interface - the sky stopped at a row (vr/sky_paths.h)
    SetBool(rt, "HudByEyes", skypaths::HudByEyes(AfrOn(), g_set.platform.load() == 1 && viewgate::F4()));   // (VR first person only)
    {   // each eye's stamp, into the picture
        const bool on = StampWanted() && g_afrBlock->stamp_magic == FLATVR_AFR_STAMP_MAGIC;
        SetBool(rt, "StampOn", on);
        if (on) {
            uint32_t v[2];
            for (int e = 0; e < 2; ++e) {
                const int from = AfrOn() ? e : (eye & 1);
                v[e] = g_stampHist[from][(g_stampHistAt[from] - PipeDepth()) & 3];
                const reshade::api::effect_uniform_variable u = rt->find_uniform_variable("D2R_DepthFog.fx", e ? "FrameStamp1" : "FrameStamp0");
                if (u.handle) rt->set_uniform_value_uint(u, &v[e], 1);
            }
            flog::Line("R,%u,%d,%u,%u", flog::Pass(), eye & 1, v[0], v[1]);
        }
    }
    // (the game's dome or fog drawn: our sky off, but DomeHorizon and GameFogTrue read the eyes' axes and projections too - left stale, the
    // dome's low band took the whole dome to the fog's colour: Serg on 0.188.6, act 4 town, game_sky=1 game_fog=1, "где небо")
    if (!sky && !engitem::DomeShown() && !gamefogvr::Active()) return;
    // (eyes by turns: the cameras the pictures show, as the overlays' above - the eye drawn's is its own, the eye made's the warp's)
    const xr::TurnView tvSky = replay::OverlaysWanted() ? xr::TurnViewNow() : xr::TurnView{};
    for (int e = 0; e < 2; ++e) {
        float axes[9];
        for (int i = 0; i < 9; ++i) axes[i] = sv[e].axes[i];
        if (tvSky.ok && e != (eye & 1)) {
            const turnwarp::Quat q{tvSky.turn[0], tvSky.turn[1], tvSky.turn[2], tvSky.turn[3]};
            const turnwarp::Vec ax[3] = {turnwarp::Rotate(q, {1, 0, 0}), turnwarp::Rotate(q, {0, 1, 0}), turnwarp::Rotate(q, {0, 0, 1})};
            for (int k = 0; k < 3; ++k) { axes[k * 3 + 0] = ax[k].x; axes[k * 3 + 1] = ax[k].y; axes[k * 3 + 2] = ax[k].z; }
        }
        char n[16];
        snprintf(n, sizeof n, "SkyProj%d", e); SetFloats(rt, n, sv[e].proj, 4);
        snprintf(n, sizeof n, "CamRight%d", e); SetFloats(rt, n, axes, 3);
        snprintf(n, sizeof n, "CamUp%d", e); SetFloats(rt, n, axes + 3, 3);
        snprintf(n, sizeof n, "CamBack%d", e); SetFloats(rt, n, axes + 6, 3);
    }
    // (the act's picture only for our sky, or the game's fog fading into the sky behind - [fog] fog_into_sky: no picture uploaded for nothing)
    if (!sky && !(g_set.fogIntoSky.load() && gamefogvr::Active())) return;
    if (arcane || pal < 0) return;   // the Arcane sky: no palette, sun, daylight or drift in the void
    const SkyPalette& p = kPalettes[pal];
    SetFloats(rt, "SkyZenith", p.zenith, 3);
    SetFloats(rt, "SkyHorizon", p.horizon, 3);
    const float el = p.sunElevDeg * 0.0174532925f, az = p.sunAzDeg * 0.0174532925f;
    const float sun[3] = {cosf(el) * sinf(az), sinf(el), cosf(el) * cosf(az)};
    SetFloats(rt, "SunDir", sun, 3);
    SetFloat(rt, "SkySun", p.sun);
    SetFloat(rt, "SkyClouds", std::clamp(p.clouds * g_set.skyClouds.load(), 0.0f, 1.0f));
    SetFloat(rt, "SkyStars", p.stars);
    AcquireSRWLockShared(&g_skyCfgLock); const SkyAct act = g_skyAct[p.act]; ReleaseSRWLockShared(&g_skyCfgLock);
    SetFloat(rt, "SkyTex", act.bandOk ? (float)p.act : 0.0f);
    SetBool(rt, "SkyCapOn", act.capOk);
    // An act with a night picture: the picture itself goes dark (SkyNightMix), the
    // brightness stays; without one the day picture is dimmed.
    SetFloat(rt, "SkyNightMix", act.nightOk && act.bandOk ? 1.0f - DayNightK() : 0.0f);
    SetFloat(rt, "SkyDrift", g_set.skyDrift.load() ? 1.0f : 0.0f);
    SetFloat(rt, "SkyBrightness", act.nightOk && act.bandOk ? act.bright : act.bright * DayNightMul());
}

void OnBeginEffects(reshade::api::effect_runtime* rt, reshade::api::command_list* cl, reshade::api::resource_view, reshade::api::resource_view) {
    if (replay::t_overlay) return;   // our own overlay render: not a present's effects
    EffectsBegin(rt, cl);
}
void OnFinishEffects(reshade::api::effect_runtime* rt, reshade::api::command_list*, reshade::api::resource_view, reshade::api::resource_view) {
    if (replay::t_overlay) return;   // our own overlay render: not a present's effects
    EffectsFinish(rt);
}

// ReShade may load after us; tried twice a second until it is there.
void TryRegister() {
    if (g_registered || !g_self || g_noReShade) return;   // ([render] no_reshade: there is none to join)
    if (!JoinReShade()) return;
    if (lightbounds::Wanted()) lightbounds::Register(&Log);   // (already, from the plugin's load, when ReShade was there then)
    if (fogspare::Wanted()) fogspare::Register(&Log);
    effectslayer::Register(&Log);   // (it looks at the game's bindings only while eyes by turns keep the effects: effectslayer::SetWanted)
    if (fogframe::Wanted()) fogframe::Register(&Log);
    reshade::register_event<reshade::addon_event::reshade_begin_effects>(&OnBeginEffects);
    reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(&d2rvr::D3D12Only<&scenedepth::OnBind>::Call);
    reshade::register_event<reshade::addon_event::barrier>(&d2rvr::D3D12Only<&scenedepth::OnBarrier>::Call);
    reshade::register_event<reshade::addon_event::destroy_resource>(&d2rvr::D3D12Only<&scenedepth::OnDestroy>::Call);
    reshade::register_event<reshade::addon_event::init_swapchain>(&d2rvr::D3D12Only<&scenedepth::OnSwapChain>::Call);
    reshade::register_event<reshade::addon_event::destroy_swapchain>(&d2rvr::D3D12Only<&scenedepth::OnSwapChain>::Call);
    reshade::register_event<reshade::addon_event::reshade_finish_effects>(&OnFinishEffects);
    reshade::register_event<reshade::addon_event::reshade_present>(&OnReshadePresent);
    reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(&Forget);
    reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(&d2rvr::D3D12Only<&renderscale::OnBind>::Call);
    reshade::register_event<reshade::addon_event::bind_viewports>(&d2rvr::D3D12Only<&renderscale::OnViewports>::Call);
    {   // Ctrl + F10: one frame's render targets to d2r_vr_uitrace.txt - its per-call events only with [debug] ui_trace (see uitrace.cpp)
        wchar_t sp[64] = L"", ss[256] = L"";
        GetPrivateProfileStringW(L"debug", L"skip_pipeline", L"0", sp, (DWORD)std::size(sp), g_iniPath);
        GetPrivateProfileStringW(L"render", L"skip_shaders", L"", ss, (DWORD)std::size(ss), g_iniPath);
        const bool trace = IniB(L"debug", L"ui_trace", false);
        const bool skip = wcstoull(sp, nullptr, 16) != 0 || ss[0] != 0;
        const bool depthViaReShade = !IniB(L"debug", L"replay_proto", true);   // camfix's root CBV hook comes with the replay's
        uitrace::Register(trace, skip, depthViaReShade);
        LogF("vrcam: the game's draws watched by ReShade events: %s%s%s", trace ? "the Ctrl+F10 trace's ([debug] ui_trace) " : "",
             skip ? "draws and pipelines (skip_shaders / skip_pipeline) " : "", depthViaReShade ? "root CBVs for depth_fix " : "",
             (!trace && !skip && !depthViaReShade) ? "none (the pipelines' creation only)" : "");
    }
    cbcmp::Register();     // [debug] cb_compare: one pair's constant buffers, left against right
    rtrace::Register();    // [debug] replay_trace: one pair's command list submissions
    replay::Register();    // [debug] replay_proto: the right eye from the left eye's command lists
    // native OpenXR's session gone before the game's device is (the game closing)
    reshade::register_event<reshade::addon_event::destroy_device>([](reshade::api::device* d) {
        if (d->get_api() == reshade::api::device_api::d3d12) gpufault::Unwatch((ID3D12Device*)d->get_native());
        xr::DeviceGone((ID3D12Device*)d->get_native());
    });
    hud::SetLogger(&Log);
    dlssmv::SetLogger(&Log);
    hud::Register();       // the interface's own layer: [hud] hide
    g_registered = true;
    Log("vrcam: joined ReShade - the fog and the sky follow d2r_vr.ini [fog] [sky]");
    crash::Mark("joined ReShade (its present events)");
    // Both read ReShade's depth, which Generic Depth provides (FlatVR's addon
    // only corrects its pick); without it the shader sees no depth and stays off.
    const std::wstring ini = ReShadeDir() + L"ReShade.ini";
    wchar_t off[512] = {};
    GetPrivateProfileStringW(L"ADDON", L"DisabledAddons", L"", off, 512, ini.c_str());
    if (wcsstr(off, L"Generic Depth"))
        Log("vrcam: ReShade's Generic Depth add-on is OFF (ReShade.ini DisabledAddons) - the fog and the sky get no depth and stay off; tick it on ReShade's Add-ons tab");
}
}  // namespace fx

}  // namespace vrc
