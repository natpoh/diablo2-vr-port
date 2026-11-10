// vr/render/effects/effects.cpp - our effect (D2R_DepthFog.fx) through ReShade or the plugin's own runtime: joining ReShade, DLSS for
// both eyes, the interface's boxes and uniforms, the sky pictures, the present (PresentCommon), [render] no_reshade's hooks, and every
// uniform of the effect each present (EffectsBegin) - the fx namespace, cut whole from vr/vrcam.cpp (docs/plan_modules.md M15; its split
// into per-feature uniform writers is a change of code, later). What it gives: vr/render/effects/effects.h. It sees vrcam.cpp's headers.
#include "../../vrcam_headers.h"
#include "../../flatvr_feed.h"   // [render] flatvr_feed: FlatVR's picture and depth without ReShade
#include "../../resize_drain.h"   // [render] resize_drain: the GPU drained before the game resizes or releases DLSS, no stale replay
#include "vrs_probe.h"   // [render] foveated: variable rate shading asked of D3D12 at the load (shared/)
#include "../../ecl_hook.h"   // [fog] game_fog_before_fx: the submission order seen (fogfx::OnExecute, the tap)
#include "../../phantom_rect.h"   // [render] phantom_rects: the vignette pass only where its carriers are

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {
// [fog] game_fog: vrcam.cpp's gamefogvr (not vr/game_fog_vr.h's) - as the effect saw it there, with the pure parts
namespace gamefogvr {
using namespace ::gamefogvr;
bool Active();
Fog Current();
void ColourNow(float rgb[3]);
bool Written(Fog* out);
void Update();
}  // namespace gamefogvr
// (vrcam.cpp and vr/input/aim.cpp; declared here only: their defaults are their definitions')
void CeilingNow(float* height, float* bright, float* light, float* relief = nullptr, int* slot = nullptr, float* own = nullptr);
void CeilingLookNow(float* wet, float* detail, float* contrast, CeilVaultCfg* vault);
float CeilingHeadroomNeed(float heroOverFloor, float hang);   // [ceiling] min_headroom_m (vr/sky/sky_config.cpp)
bool HandRay(float* o, float* d, float* dRaw = nullptr);


// Fog: D2R_DepthFog.fx runs inside ReShade, which this plugin joins as an
// add-on so the ini (and the settings window) can switch it and set its range.
// ReShade's own UI for the effect stays usable; what we write wins each frame.
namespace fx {
bool g_registered = false;
bool g_joined = false;   // reshade::register_addon(g_self) done - by TryRegister, or at the plugin's load for lightbounds
// Since 0.203 D2R VR never joins a ReShade (vr/plugin/reshade_load.cpp): not ours (never loaded) and not the player's own dxgi.dll one -
// joining let our defines rebuild the effect in ReShade's present (crash reports 231 / 232 / 251). Nothing registers with ReShade, so none of
// its events ever reach the plugin; the code behind them stays only as the stand-in's shape (vr/native_rsapi.h).
bool JoinReShade() {
    return false;
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
    // [render] resize_drain: the game releases a feature (a DLSS mode or render size change in its video options) after waiting for its own
    // frame - not for our replay of that frame, which evaluates this feature again (and the pass now recorded would): drained first, not replayed
    resizedrain::Event(resizedrain::Why::kDlssRelease);
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
// The game's plate up now (plateworld::PlateUp): in the pad's layout only while the anchor hook answers for it - else no box at all (a box left at
// the last plate's place cut the Stash's name out of mid-view: sdfsdfsdfdscvx, 2026-11-10). unitNow: the unit the plate hangs on (0: not known).
bool PlateUpNow(uintptr_t* unitNow) {
    int32_t anchor[2];
    const bool attached = gamestate::PlateAnchor(anchor);
    const int view = viewgate::Number();
    const bool notes = mobbars::Hooked() && plateworld::Following() &&
                       (viewgate::F4() || plateworld::Ours() || (view >= 1 && view <= 3 && plateworld::Elsewhere()));
    plateworld::Follow f;
    const bool fresh = plateworld::GetFollow(&f, GetTickCount64());
    *unitNow = fresh ? f.unit : 0;
    const bool up = plateworld::PlateUp(attached, notes, fresh);
    static int told = -1;
    const int now = !attached ? 2 : !notes ? 3 : up ? 1 : 0;
    if (now != told && (told >= 0 || now != 0)) {
        told = now;
        static int lines = 0;
        if (lines < 12 && ++lines)
            LogF("vrcam: the monster's plate box - %s", now == 1 ? "a plate up (the hook answers for it): the box follows it"
                                                         : now == 0 ? "no plate up (no answer from the hook): no box - nothing cut out of the interface"
                                                         : now == 2 ? "the mouse layout's plate (at the top): the box from its rect"
                                                                    : "the anchor hook not noting here: the box from the plate's rect");
    }
    return up;
}
void PlateBoxUv(const int32_t r[4], uint32_t sw, uint32_t sh, float box[4]) {
    float s = g_uiScaleSeen.load();
    if (!(s > 0.05f && s < 20.0f) && Matches(RVA_UI_SCALE, kSigUiScale, sizeof kSigUiScale)) s = ((UiScaleFn)d2rsig::Addr(RVA_UI_SCALE))();
    if (!(s > 0.05f && s < 20.0f)) s = sh / 2160.0f;
    const float sGame = s;
    uintptr_t readUnit = 0;
    float atRead[2] = {};
    const bool atReadOk = gamestate::PlateReadAt(&readUnit, atRead);
    plateworld::Follow f;
    if (!plateworld::GetFollow(&f, GetTickCount64())) f = plateworld::Follow{};
    int32_t anchor[2] = {};
    // Where the game hung the plate at the read, and its anchor widget then: the box from there (plateworld::BoxAtAnchor), the scale measured by the anchor
    // when it hangs well off the middle (ScaleFromAnchor) and kept - a plate near the middle uses the last one measured, else the game's.
    static float sMeasured = 0.0f, measuredOn = 0.0f;   // (a measure is of one viewport: another render size starts again)
    const bool atAnchor = atReadOk && f.n > 0 && gamestate::PlateAnchor(anchor);
    if (atAnchor && f.view[0] != measuredOn) { sMeasured = 0.0f; measuredOn = f.view[0]; }
    float sm = 0.0f;
    if (atAnchor && plateworld::ScaleFromAnchor(anchor[0], atRead[0], f.view[0], &sm)) sMeasured = sm;
    if (atAnchor && sMeasured > 0.0f) s = sMeasured;
    float read[4], px[4] = {};
    const bool fromAnchor = atAnchor && plateworld::BoxAtAnchor(r, anchor, s, atRead, f.view[0], f.view[1], read);
    if (!fromAnchor) {
        if (!plateworld::BoxOf(r, s, sw, sh, px)) { box[0] = box[1] = box[2] = box[3] = 0.0f; return; }
        read[0] = px[0] / sw; read[1] = px[1] / sh; read[2] = px[2] / sw; read[3] = px[3] / sh;
    }
    static float widest = 0.0f;   // the widest plate read (uv): a new target's box that wide until its own rect is read
    if (plateworld::SanePlate(r)) widest = (std::max)(widest, read[2] - read[0]);
    plateworld::FollowBox(read, readUnit, atRead, atReadOk, f, widest, box);
    static int told = 0, toldScale = 0;
    static int32_t last[4] = {};
    if (told < 8 && memcmp(last, r, sizeof last) != 0) {
        ++told;
        memcpy(last, r, sizeof last);
        LogF("vrcam: the monster's name plate %d,%d %dx%d (layout), anchor %d,%d, x scale %.3f (the game's %.3f) -> uv %.3f,%.3f %.3fx%.3f on %ux%u (viewport %.0fx%.0f); "
             "set %s; following the hook's anchor: %s (%d answers, %+.0f,%+.0f px since the read)",
             r[0], r[1], r[2], r[3], anchor[0], anchor[1], s, sGame, read[0], read[1], read[2] - read[0], read[3] - read[1], sw, sh, f.view[0], f.view[1],
             fromAnchor ? "where the game hung it" : "from the rect alone", atReadOk && f.n > 0 ? "yes" : "no", f.n,
             f.n > 0 ? f.px[0][0] - atRead[0] : 0.0f, f.n > 0 ? f.px[0][1] - atRead[1] : 0.0f);
    }
    if (sm > 0.0f && std::fabs(sm - sGame) > 0.03f * sGame && toldScale < 4) {   // what would have put the box off the plate: in the log
        ++toldScale;
        LogF("vrcam: the monster's name plate - the game's interface scale %.3f, the plate's anchor says %.3f (anchor x %d units at %.0f px of a %.0f px viewport; "
             "screen %ux%u): the box is set by the anchor", sGame, sm, anchor[0], atRead[0], f.view[0], sw, sh);
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
    else if (plateworld::Ours()) off = 7;   // [hud] monster_names_own: ours instead, the game's plate not drawn at all
    static const char* const kOff[] = {"in force", "", "the mod is off", "no game yet (a menu or the title)", "not the F4 view (the game's own plate in every other view)",
                                       "a panel is open", "no native OpenXR eyes this frame", "our own plate is on (monster_names_own): the game's is not drawn"};
    const bool active = off == 0;
    if (wanted && toldState.Should(0, off, nowMs)) LogF("vrcam: monster_bar_room - %s%s", off ? "not in force now: " : "", kOff[off]);
    plateworld::SetActive(active);
    {   // the targeted monster's ground distance from the hero's feet (monster_far, monster_depth_body, the bar's size): from the hook's latest note
        plateworld::Seen ts;
        float hero[3], units = 0.0f, metres = 0.0f;
        if (plateworld::Tracking() || active) {
            if (plateworld::GetSeen(&ts, nowMs) && ts.footOk && LookAtNow(hero) &&
                plateworld::GroundDistance(ts.foot, hero, std::max(1.0f, ViewHeight()) / UserEyeHeightM(), &units, &metres)) {
                if (plateworld::Steadying()) {   // [hud] monster_plate_smooth: the distance held by the same spring as the head (the size, the depth, the eyes by turns)
                    static plateworld::Steady<1> held;
                    static ULONGLONG lastMs = 0;
                    const float dt = lastMs ? static_cast<float>(nowMs - lastMs) * 0.001f : -1.0f;
                    lastMs = nowMs;
                    const float m = plateworld::SteadyValue(&held, ts.unit, metres, dt);
                    if (metres > 1e-4f) units *= m / metres;
                    metres = m;
                }
                plateworld::SetTarget(metres, units, nowMs, ts.unit);
            }
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
    if (plateworld::LiftWanted() || plateworld::Steadying()) {   // [hud] monster_over_head, monster_plate_smooth: what the hook needs (the floor, the scale) and the game's own gap under the plate
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
// portraits itself, at the top left (its HireablesPanel, data\global\ui\Layouts\HireablesPanelHD.json) - and the
// next thing down that side is the chat (HUDMessagePanelHD.json: 40, 910 in the layout's units, off the interface
// area's top left). The panel: the party's players in a column from 46, 266 (204 units apart), the mercenary and
// the summons in a ROW ("summonsUseSecondSet": 206 units higher, $SecondSetIconOffset = 175 units apart, 225 with
// the large font), each portrait 120 units wide with its name under it. So the corner is from the screen's top left
// to kAlliesRowUnits right of the area's left - the row's eight portraits and their names - and 900 units under its
// top. It was 420 units wide (2026-11-06, docs/notes/ally_portraits.md): the row's third portrait (46 + 2 x 175 = 396)
// was cut at 420 and the rest of the row left behind where the game drew it. The area: the game's interface scale s
// (PlateBoxUv) is the screen's height over 2160 times the Safe Screen Percent, and the area stands in the middle of
// the screen. At most 0.6 of the screen across: the corner map at the top right stays where it is.
// [hud] allies_game_hide hides the corner instead (AlliesHidden): `rowUnits` is then the width of what the game draws now, no more.
constexpr float kAlliesRowUnits = 46.0f + 7.0f * 175.0f + 160.0f;
void AlliesBoxUv(uint32_t sw, uint32_t sh, float box[4], float rowUnits = kAlliesRowUnits) {
    float s = g_uiScaleSeen.load();
    if (!(s > 0.05f && s < 20.0f) && Matches(RVA_UI_SCALE, kSigUiScale, sizeof kSigUiScale)) s = ((UiScaleFn)d2rsig::Addr(RVA_UI_SCALE))();
    if (!(s > 0.05f && s < 20.0f)) s = sh / 2160.0f;
    const float safe = std::clamp(s * 2160.0f / sh, 0.3f, 1.0f);
    const float left = 0.5f * sw * (1.0f - safe), top = 0.5f * sh * (1.0f - safe);
    box[0] = 0.0f; box[1] = 0.0f;
    box[2] = std::clamp((left + rowUnits * s) / sw, 0.0f, 0.6f); box[3] = std::clamp((top + 900.0f * s) / sh, 0.0f, 0.75f);
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
template <class RT> void SetBool(RT* rt, const char* name, bool v);   // (below)
// [hud] labels_own: the names' atlas into the effect's OwnLabelTex (vr/own_labels_wire.h Resolve).
template <class RT> struct OwnLabelUp { RT* rt; reshade::api::effect_texture_variable tv; };
template <class RT> bool OwnLabelUpload(void* ctx, const uint32_t* px, int w, int h) {
    const OwnLabelUp<RT>* u = (const OwnLabelUp<RT>*)ctx;
    u->rt->update_texture(u->tv, (uint32_t)w, (uint32_t)h, px);
    return true;
}
bool g_namesFrameOn = false;   // our plate drawn this frame (OwnNamesUniforms): the game's plate is hidden (PlateAt w 2)
// Each eye's overlay camera as the effect last set it (the phantom block below, every frame): for the names' one-time per-eye dump (OwnNamesUniforms).
struct NamesCam { bool ok = false; float off[3] = {}, axes[9] = {}, proj[4] = {}; };
NamesCam g_namesCam[2];
// The atlas's picture the effect's OwnLabelTex holds: one for the item labels and the monsters' names (both from ownlabels::Resolve).
uint64_t g_ownUpVar = 0;
uint32_t g_ownUpRev = 0;
template <class RT> reshade::api::effect_texture_variable OwnLabelTexNow(RT* rt) {
    const reshade::api::effect_texture_variable tv = rt->find_texture_variable("D2R_DepthFog.fx", "OwnLabelTex");
    if (tv.handle && tv.handle != g_ownUpVar) { g_ownUpVar = tv.handle; g_ownUpRev = 0; }   // (a reloaded effect: its texture is empty again)
    return tv;
}
// The interface layer's size: ReShade's view, or (no ReShade) the stand-in's (vr/native_rsapi.h).
template <class RT> void LayerSizeOf(RT* rt, uint64_t layerSrv, float* W, float* H) {
    *W = *H = 0.0f;
    if (!layerSrv) return;
    if constexpr (std::is_same_v<RT, reshade::api::effect_runtime>) {
        reshade::api::device* dev = rt->get_device();
        const reshade::api::resource_desc d = dev->get_resource_desc(dev->get_resource_from_view(reshade::api::resource_view{layerSrv}));
        *W = (float)d.texture.width;
        *H = (float)d.texture.height;
    } else {
        uint32_t fmt = 0;
        if (ID3D12Resource* r = nrs::ResourceOfView(layerSrv, &fmt)) {
            const D3D12_RESOURCE_DESC d = r->GetDesc();
            *W = (float)d.Width;
            *H = (float)d.Height;
        }
    }
}
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
        LayerSizeOf(rt, layerSrv, &W, &H);
        const float m0 = g_projSx.load(), half = 0.5f * g_stereoIpd.load(), conv = g_stereoConv.load();
        // [hud] labels_own: ours stand at their items' depth - exactly (labels_depth_body 100) unless that is set, and labels_near_body does not move them
        const bool own = OwnLabelsNow();
        const float eyeH = std::max(1.0f, ViewHeight()), k = own && g_set.labelsDepthBody.load() <= 0.005f ? 1.0f : g_set.labelsDepthBody.load();
        if (own) base = 0.0f;
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
    // [hud] labels_own (vr/own_labels.h): our cell in each item label's place, from the names' atlas; a name not painted yet keeps the game's pixels
    float cells[kLabelDepthMax * 4] = {}, rects[kLabelDepthMax * 4] = {};
    bool ownShown = false;
    if (n > 0 && OwnLabelsNow()) {
        const reshade::api::effect_texture_variable tv = OwnLabelTexNow(rt);
        if (!tv.handle) {
            static bool told = false;
            if (!told) { told = true; Log("vrcam: [hud] labels_own - D2R_DepthFog.fx has no OwnLabelTex: an older one, copy the new one in (the DLL and the effect together); the game's labels meanwhile"); }
        } else {
            uint32_t bw = 0, bh = 0;
            rt->get_screenshot_width_and_height(&bw, &bh);
            ownlabels::Want wants[kLabelDepthMax];
            float cellK[kLabelDepthMax] = {};   // the cell's size against the game's box: ownK, larger to labels_own_min_px
            int idx[kLabelDepthMax], m = 0;
            for (int i = 0; i < n; ++i) {
                if (!shown[i].own) continue;
                wants[m] = shown[i].want;
                // (labels_own_min_px: never smaller than that on the eye; the cell then stands larger than the game's box)
                cellK[i] = shown[i].ownK * ownlabels::MinScale((boxes[i * 4 + 3] - boxes[i * 4 + 1]) * shown[i].ownK * (float)bh, g_set.ownMinPx.load());
                wants[m].heightPx = ownlabels::HeightFor((boxes[i * 4 + 3] - boxes[i * 4 + 1]) * cellK[i], (float)bh);
                idx[m++] = i;
            }
            ownlabels::Cell got[kLabelDepthMax];
            OwnLabelUp<RT> up{rt, tv};
            if (m && bw && bh && ownlabels::Resolve(wants, m, got, &g_ownUpRev, &OwnLabelUpload<RT>, &up) > 0)
                for (int j = 0; j < m; ++j) {
                    const int i = idx[j];
                    if (!got[j].w || !ownlabels::ShownRect(boxes + i * 4, got[j].w, got[j].h, (float)bw, (float)bh, rects + i * 4, cellK[i])) continue;
                    ownlabels::CellUv(got[j], ownlabels::kAtlasW, ownlabels::kAtlasH, cells + i * 4);
                    ownShown = true;
                }
            static bool toldOn = false;
            if (ownShown && !toldOn) {
                toldOn = true;
                LogF("vrcam: [hud] labels_own - our item labels shown in the game's places (%d of %d labels this frame, eye %ux%u; the first: %.1f units "
                     "off, size x%.2f by our curve %.0f%% at %.1f m to %.0f%% at %.1f m (box %s), %d px tall, backdrop alpha %.2f)",
                     m, n, bw, bh, shown[idx[0]].dist, shown[idx[0]].sizeK, g_set.ownSizeNear.load(), g_set.ownNearM.load(), g_set.ownSizeFar.load(), g_set.ownFarM.load(),
                     shown[idx[0]].ownK == 1.0f ? "made that size by the game" : "left, our cell sized", wants[0].heightPx, shown[idx[0]].want.bg[3]);
            }
        }
    }
    SetInt(rt, "LabelCount", n);
    if (n) {
        SetFloats(rt, "LabelBoxes", boxes, (size_t)n * 4);
        SetFloats(rt, "LabelShifts", shifts, (size_t)n);
        SetFloats(rt, "LabelTurnH", turnH, (size_t)n);
    }
    SetBool(rt, "OwnLabelsOn", ownShown);
    if (ownShown) {
        SetFloats(rt, "LabelCells", cells, (size_t)n * 4);
        SetFloats(rt, "LabelOwnRects", rects, (size_t)n * 4);
    }
}
template <class RT> void SetBool(RT* rt, const char* name, bool v) {
    const reshade::api::effect_uniform_variable u = rt->find_uniform_variable("D2R_DepthFog.fx", name);
    if (u.handle) rt->set_uniform_value_bool(u, &v, 1);
}

// [hud] monster_names_own / npc_names_own (vr/own_names.h): our cells for the texts of the plate over the target and the hover names - D2R_DepthFog.fx
// draws them over the layer where the game drew each copy, each at its own depth (the plate's texts at their monster's), with our names' own size, size by
// distance and backdrop (names_own_*; none of the game plate's sliders). box: the plate's box (uv; empty: no plate), for the log. The texts come from the text hook (vr/hud/labels.cpp NoteNameText); the game skips only those marked here.
std::string NameBytes(const char* raw) {   // a text as the log can carry it: bytes past ASCII as \xNN (how the game marks colours)
    std::string s;
    char b[8];
    for (const unsigned char* p = (const unsigned char*)raw; *p && s.size() < 160; ++p) {
        if (*p >= 0x20 && *p < 0x7F) s += (char)*p;
        else { snprintf(b, sizeof b, "\\x%02X", *p); s += b; }
    }
    return s;
}
const float kNoBox[4] = {0.0f, 0.0f, 0.0f, 0.0f};
// target: the plate's monster at its own depth - its shift per eye and its half-disparity eyes by turns (have: its distance known); base: the layer's own shift
// (the item labels' plane), for a name with no depth of its own (a hover name); plateAlpha: monster_alpha, for the plate's texts.
struct NameDepth {
    bool have = false; float shift = 0.0f, turnH = 0.0f, base = 0.0f, groundM = -1.0f; bool head = false; float at[3] = {}; float plateShift = 0.0f;
    // for the one-time dump: the target's place on the grid (game units), its floor and the point over its head (render metres), the hero's eyes (render metres)
    int why = -1; float foot[2] = {}, floorM = 0.0f, headM[3] = {}, eyesM[3] = {}, upm = 0.0f; uint8_t row[3] = {};
};
template <class RT> void OwnNamesUniforms(RT* rt, uint64_t layerSrv, const float box[4], float plateScale, const NameDepth& depth) {
    const bool on = MonsterNamesOwnNow(), hover = NpcNamesOwnNow();
    NamesSetHover(hover);
    float W = 0.0f, H = 0.0f;
    if (on || hover) LayerSizeOf(rt, layerSrv, &W, &H);
    const bool plate = on && box[2] > box[0] && box[3] > box[1] && W > 0.0f && H > 0.0f;
    const float px[4] = {box[0] * W, box[1] * H, box[2] * W, box[3] * H};
    NamesSetPlate(on, plate ? px : nullptr);
    float cells[ownnames::kMax * 4] = {}, rects[ownnames::kMax * 4] = {}, nShift[ownnames::kMax] = {}, nTurn[ownnames::kMax] = {}, nAlpha[ownnames::kMax] = {};
    float nAt[ownnames::kMax * 4] = {}, nLift[ownnames::kMax] = {};   // the plate's lines over their monster's head in the world (w: height, px of the eye; 0 none)
    int k = 0;
    static ULONGLONG plateSince = 0;
    static bool toldNone = false, toldText = false;
    static int toldK = 0;   // the most texts ours stood in for, as last told (a line each time it grows: the second line coming in too)
    const ULONGLONG nowMs = GetTickCount64();
    if (!plate) plateSince = 0;
    else if (!plateSince) plateSince = nowMs;
    if ((on || hover) && W > 0.0f && H > 0.0f) {   // (every copy of the plate's texts, in the plate's box or not: 0.205.8, the box and the copies took turns in two places)
        const reshade::api::effect_texture_variable tv = OwnLabelTexNow(rt);
        ownnames::Text t[ownnames::kMax];
        int n = on ? NamesTake(t) : 0;
        if (depth.head) n = ownnames::OneCopyOfEach(t, n, plate ? px : nullptr);   // over the head: one copy of the plate (its lines as the game stands them)
        const int nPlate = n;
        if (hover) n += HoverTake(t + n, ownnames::kMax - n);   // [hud] npc_names_own: the hover names, drawn where the game drew them
        if (nPlate && !toldText) {
            toldText = true;
            LogF("vrcam: [hud] monster_names_own - the plate's texts as the game draws them: %d, the first \"%s\" in %d,%d %dx%d (plate %.0f,%.0f-%.0f,%.0f px)", n,
                 NameBytes(t[0].raw).c_str(), t[0].box[0], t[0].box[1], t[0].box[2], t[0].box[3], px[0], px[1], px[2], px[3]);
        }
        if (on && !nPlate && !toldNone && plateSince && nowMs - plateSince > 3000) {
            toldNone = true;
            Log("vrcam: [hud] monster_names_own - a plate shown for 3 s and no text of it drawn through the game's TextDraw inside its box: the game's names stay");
        }
        if (!tv.handle) {
            static bool told = false;
            if (!told) { told = true; Log("vrcam: [hud] monster_names_own - D2R_DepthFog.fx has no OwnLabelTex: an older one (the DLL and the effect together); the game's names meanwhile"); }
        } else if (n) {
            uint32_t bw = 0, bh = 0;
            rt->get_screenshot_width_and_height(&bw, &bh);
            uint8_t special = 0;
            const bool known = NamesTargetKind(&special);
            ownlabels::Want wants[ownnames::kMax + 2];   // (+ our plate's frame and its life bar)
            int idx[ownnames::kMax], m = 0;
            static int toldOut = 0;
            const bool hidden = NamesTargetHidden();   // [hud] ordinary_mob_bars on and an ordinary monster: no plate of ours either, as the game's
            bool in[ownnames::kMax] = {};
            float grow[ownnames::kMax] = {};   // labels_own_min_px: how much larger than the game's text ours stands (in the plate: on top of its size)
            for (int i = 0; i < n; ++i) {
                ownlabels::Want& w = wants[m];
                w = ownlabels::Want{};
                const char code = ownlabels::StripCodes(t[i].raw, sizeof t[i].raw, w.text, sizeof w.text);
                if (!w.text[0]) continue;
                const bool hoverName = t[i].which == ownnames::kWhichHover;
                if (hoverName) ownlabels::IconsToKeys(w.text, &w.keys);   // (the NPC's (A): our 'A' in the game's green)
                if (!hoverName && hidden) continue;
                in[m] = !hoverName && plate && ownnames::InPlate(t[i].box, px);   // in the plate's box
                // Not over the head (no target to stand on this frame): only the copy in the plate's box is ours - a copy elsewhere (the game's top place, 793,219:
                // the game does not show it) drawn by us showed the name high up and to the side (0.205.13, Serg's screenshot "Zombie / Undead" up-left)
                if (!hoverName && !depth.head && !in[m]) {
                    if (toldOut < 3 && ++toldOut)
                        LogF("vrcam: [hud] monster_names_own - the plate's text \"%s\" drawn at %d,%d %dx%d, outside the plate's box %.0f,%.0f-%.0f,%.0f px and not over the head: left the game's",
                             w.text, t[i].box[0], t[i].box[1], t[i].box[2], t[i].box[3], px[0], px[1], px[2], px[3]);
                    continue;
                }
                if (!ownlabels::CodeColour(code, w.rgb)) {   // a code in the text, else the colour the plate was given, else the target's kind
                    if (t[i].rgbKnown) std::copy_n(t[i].rgb, 3, w.rgb);
                    else if (hoverName) w.rgb[0] = w.rgb[1] = w.rgb[2] = 1.0f;   // (a hover name: white, as the game's, unless it carries a code)
                    else ownnames::NameColour(0, special, known && t[i].which == 0, w.rgb);
                }
                // our names' own three sliders (none of the game plate's): the backdrop's opacity, the size, the size by distance
                ownnames::NameLook look;
                look.sizePct = g_set.namesSize.load();
                look.farPct = g_set.namesFar.load();
                look.alpha = g_set.namesAlpha.load();
                ownnames::NameBackdrop(look.alpha, w.bg);
                w.bold = true;     // larger letters (bold only in our own face)
                w.font = t[i].font;   // the game's own face for it (its style's font)
                if (t[i].box[3] > 0) {   // ... and its letter spacing: as wide as the game measured this line (its padding spaces counted)
                    w.gameWPerH = (float)t[i].box[2] / (float)t[i].box[3];
                    int pad = 0;
                    for (const char* c = t[i].raw; *c == ' '; ++c) ++pad;
                    const size_t len = strlen(t[i].raw);
                    for (size_t e = len; e > 0 && t[i].raw[e - 1] == ' '; --e) ++pad;
                    w.padSpaces = (uint8_t)std::min(pad, 16);
                }
                grow[m] = ownnames::NameK(look, depth.groundM, !hoverName && depth.groundM >= 0.0f, g_set.ownNearM.load(), g_set.ownFarM.load());
                w.heightPx = ownlabels::StepPx((float)t[i].box[3] / H * (float)bh * grow[m]);
                idx[m++] = i;
            }
            ownlabels::Cell got[ownnames::kMax + 2];
            OwnLabelUp<RT> up{rt, tv};
            float lineH[3] = {}, lineLift[3] = {};   // the plate's lines over the head: the name in our plate, the others under it
            float nameW = 0.0f;
            if (depth.head)
                for (int j = 0; j < m; ++j)
                    if (t[idx[j]].which >= 0 && t[idx[j]].which < 3) {
                        const ownnames::Text& tx = t[idx[j]];
                        lineH[tx.which] = (float)tx.box[3] / H * (float)bh * grow[j];
                        if (tx.which == 0 && tx.box[3] > 0)   // (the name's width from the game's own measure of it, at our height, and the cell's room)
                            nameW = lineH[0] * (float)tx.box[2] / (float)tx.box[3] + 2.0f * ownlabels::PadX((int)lineH[0]);
                    }
            // Our plate (Serg, 0.205.13: "draw our own life bar, in the game's style"): the game's frame and red bar at our point, the name on the bar, the
            // other lines under the plate - over the head, with the life the plate was told; the game's plate is hidden while ours is there.
            float life = -1.0f;
            bool uniqueFrame = false;
            const bool plateOurs = depth.head && lineH[0] > 0.0f && NamesTargetPlate(&life, &uniqueFrame);
            ownnames::PlateLayout lay;
            int framePiece = -1, lifePiece = -1;
            {
                const float under[3] = {0.0f, lineH[1], lineH[2]};   // (the lines under the plate: stacked from the point up)
                ownnames::StackLines(under, 2.0f, lineLift);
                const float lowH = (lineH[1] > 0.0f ? lineH[1] + 2.0f : 0.0f) + (lineH[2] > 0.0f ? lineH[2] + 2.0f : 0.0f);
                if (plateOurs) {
                    // the game's own proportions: its bar (the plate's rect times the interface scale), its name's height, its lines' offsets from the name
                    ownnames::GamePlate gp;
                    int32_t pr[4];
                    float s = g_uiScaleSeen.load();
                    if (!(s > 0.05f && s < 20.0f)) s = GameUiScaleNow();
                    int nameAt = -1;
                    for (int j = 0; j < m; ++j)
                        if (t[idx[j]].which == 0) nameAt = idx[j];
                    // the game's bar read for THIS target only: the rect is read on the interface's thread a little after the target changes, and
                    // the last target's bar (shorter name, another frame) made ours cut off or too large for a moment (sdfsdfsdfdscvx, 2026-11-10)
                    uintptr_t readUnit = 0, unitNow = 0;
                    float readAt[2];
                    plateworld::Follow fNow;
                    if (plateworld::GetFollow(&fNow, GetTickCount64())) unitNow = fNow.unit;
                    const bool rectOfTarget = !unitNow || (gamestate::PlateReadAt(&readUnit, readAt) && readUnit == unitNow);
                    if (nameAt >= 0 && s > 0.05f && rectOfTarget && gamestate::PlateRect(pr) && plateworld::SanePlate(pr)) {
                        gp.nameBoxH = (float)t[nameAt].box[3];
                        gp.barW = pr[2] * s;
                        gp.barH = pr[3] * s;
                        for (int j = 0; j < m; ++j) {
                            const ownnames::Text& tx = t[idx[j]];
                            if (tx.which < 1 || tx.which > 2) continue;
                            gp.line[tx.which] = true;
                            gp.lineDy[tx.which] = (tx.box[1] + 0.5f * tx.box[3]) - (t[nameAt].box[1] + 0.5f * t[nameAt].box[3]);
                        }
                    }
                    gp.nameW = nameW;   // (the frame never narrower than our name on it)
                    ownnames::GameLayout gl;
                    if (ownnames::LayOutGamePlate(lineH[0], gp, lineH, &gl)) {
                        lay = gl.plate;
                        for (int i = 0; i < 3; ++i) lineLift[i] = gl.lineLift[i];
                    } else {   // (no rect or scale this frame: our own stack - the lines under the plate)
                        lay = ownnames::LayOutPlate(lineH[0], nameW, lowH > 0.0f ? lowH - 2.0f : 0.0f, 2.0f);
                        lineLift[0] = lay.nameLift;
                    }
                    ownlabels::Want& fw = wants[m];
                    fw = ownlabels::Want{};
                    fw.piece = uniqueFrame ? ownlabels::kPieceFrameUnique : ownlabels::kPieceFrameNormal;
                    fw.heightPx = ownlabels::StepPx(lay.frameH);
                    fw.widthPx = std::max(fw.heightPx, (int)std::lround(lay.frameW * fw.heightPx / std::max(lay.frameH, 1.0f) / 8.0f) * 8);
                    framePiece = m++;
                    ownlabels::Want& lw = wants[m];
                    lw = ownlabels::Want{};
                    lw.piece = ownlabels::kPieceLife;
                    lw.heightPx = ownlabels::StepPx(lay.fill[3] - lay.fill[1]);
                    lw.widthPx = 64;
                    lifePiece = m++;
                } else {
                    float all[3] = {lineH[0], lineH[1], lineH[2]};
                    ownnames::StackLines(all, 2.0f, lineLift);   // (no plate of ours: the lines stacked over the head, the name on top)
                }
            }
            const int mText = plateOurs ? m - 2 : m;
            float frameAt[4] = {}, frameRect[4] = {}, frameCell[4] = {}, fillRect[4] = {}, fillCell[4] = {};
            bool frameOn = false;
            if (m && bw && bh && ownlabels::Resolve(wants, m, got, &g_ownUpRev, &OwnLabelUpload<RT>, &up) > 0) {
                if (plateOurs && got[framePiece].w && got[lifePiece].w) {
                    frameOn = true;
                    for (int c = 0; c < 3; ++c) frameAt[c] = depth.at[c];
                    frameAt[3] = 1.0f;
                    std::copy_n(lay.frame, 4, frameRect);
                    std::copy_n(lay.fill, 4, fillRect);
                    fillRect[2] = fillRect[0] + ownnames::FillWidth(lay, 1.0f);
                    ownlabels::CellUv(got[framePiece], ownlabels::kAtlasW, ownlabels::kAtlasH, frameCell);
                    ownlabels::CellUv(got[lifePiece], ownlabels::kAtlasW, ownlabels::kAtlasH, fillCell);
                }
                for (int j = 0; j < mText; ++j) {
                    const ownnames::Text& tx = t[idx[j]];
                    const float tb[4] = {tx.box[0] / W, tx.box[1] / H, (tx.box[0] + tx.box[2]) / W, (tx.box[1] + tx.box[3]) / H};
                    if (!got[j].w || !ownlabels::CentredRect(tb, got[j].w, got[j].h, (float)bw, (float)bh, rects + k * 4, grow[j])) continue;
                    ownlabels::CellUv(got[j], ownlabels::kAtlasW, ownlabels::kAtlasH, cells + k * 4);
                    const bool plateText = tx.which != ownnames::kWhichHover;
                    nShift[k] = plateText && depth.have ? depth.shift : depth.base;   // the plate's: at its monster's depth; a hover name: where the game's stood
                    nTurn[k] = plateText && depth.have ? depth.turnH : 0.0f;
                    nAlpha[k] = 1.0f;   // (monster_alpha is the game plate's: our names' backdrop has its own, the letters stay whole)
                    if (plateText && depth.head && tx.which >= 0 && tx.which < 3) {   // over its monster's head, each eye projecting the one point
                        for (int c = 0; c < 3; ++c) nAt[k * 4 + c] = depth.at[c];
                        nAt[k * 4 + 3] = lineH[tx.which];
                        nLift[k] = lineLift[tx.which];
                    }
                    NamesMark(ownnames::KeyOf(tx.raw));
                    ++k;
                }
            }
            SetFloats(rt, "PlateFrameAt", frameAt, 4);
            if (frameOn) {
                SetFloats(rt, "PlateFrameRect", frameRect, 4);
                SetFloats(rt, "PlateFrameCell", frameCell, 4);
                SetFloats(rt, "PlateFillRect", fillRect, 4);
                SetFloats(rt, "PlateFillCell", fillCell, 4);
                SetFloat(rt, "PlateLife", life);
            }
            g_namesFrameOn = frameOn;   // (the game's plate hidden while ours is there: the caller's PlateAt)
            static bool toldPlate = false;
            if (frameOn && !toldPlate) {
                toldPlate = true;
                LogF("vrcam: [hud] monster_names_own - our plate over the head: %s frame %.0fx%.0f px, life %.0f %%, the name %.0f px tall; the game's plate hidden",
                     uniqueFrame ? "the unique" : "the normal", lay.frameW, lay.frameH, life * 100.0f, lineH[0]);
            }
            {   // [debug] own_labels_dump: once our names are shown, the atlas to d2rloader\logs\d2r_vr_own_labels.bmp (beside the log), to hold against the game's text
                static int dumpTries = 0;   // (a write that fails says why and is tried again, three times)
                static bool dumped = false;
                if (g_set.ownLabelsDump.load() && !dumped && dumpTries < 3) {
                    ++dumpTries;
                    wchar_t path[MAX_PATH] = L"";
                    HMODULE self = nullptr;
                    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&OwnLabelTexNow<RT>), &self);
                    GetModuleFileNameW(self, path, MAX_PATH);
                    if (wchar_t* slash = wcsrchr(path, L'\\')) slash[1] = 0;
                    wcscat_s(path, L"..\\logs\\d2r_vr_own_labels.bmp");
                    wchar_t full[MAX_PATH] = L"";
                    GetFullPathNameW(path, MAX_PATH, full, nullptr);
                    int err = 0;
                    const bool ok = ownlabels::DumpAtlas(full[0] ? full : path, &err);
                    dumped = ok;
                    LogF("vrcam: [debug] own_labels_dump - our labels' atlas %s %ls%s%s", ok ? "written to" : "NOT written to", full[0] ? full : path,
                         ok ? "" : " - ", ok ? "" : (err == -1 ? "the atlas is empty yet" : strerror(err)));
                }
            }
            static bool toldEyes = false;   // once: each eye's x of our name and of the game's plate - the evidence for "left in one eye, right in the other"
            if (k && !toldEyes && g_namesCam[0].ok && g_namesCam[1].ok) {
                toldEyes = true;
                float x[2] = {-1.0f, -1.0f}, y[2] = {-1.0f, -1.0f}, ahead[2] = {};
                for (int e = 0; e < 2 && depth.head; ++e) {
                    const NamesCam& c = g_namesCam[e];
                    const float v[3] = {depth.at[0] - c.off[0], depth.at[1] - c.off[1], depth.at[2] - c.off[2]};
                    float p[2];
                    if (ownnames::EyePixel(v, c.axes, c.axes + 3, c.axes + 6, c.proj, (float)bw, (float)bh, p, &ahead[e])) { x[e] = p[0]; y[e] = p[1]; }
                }
                const float upmNow = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
                const float cx = 0.5f * (box[0] + box[2]) * (float)bw;   // the game's plate: its box's middle, moved by its shift in each eye (+ the left)
                const float plateL = cx + depth.plateShift * (float)bw, plateR = cx - depth.plateShift * (float)bw;
                const float nameL2d = 0.5f * (rects[0] + rects[2]) * (float)bw + nShift[0] * (float)bw, nameR2d = 0.5f * (rects[0] + rects[2]) * (float)bw - nShift[0] * (float)bw;
                LogF("vrcam: [hud] monster_names_own - per eye (px of a %ux%u eye): our name %s: left x %.1f y %.1f, right x %.1f y %.1f (apart %.1f px; the point %.2f / %.2f m "
                     "ahead of each eye, the target %.1f m off); in 2D it would be left %.1f right %.1f (apart %.1f); the game's plate: left %.1f, right %.1f (apart %.1f px, "
                     "its shift %.5f uv, the monster's %.5f)",
                     bw, bh, depth.head ? "over the head (projected by each eye)" : "NOT over the head", x[0], y[0], x[1], y[1], x[0] - x[1], ahead[0] / upmNow,
                     ahead[1] / upmNow, depth.groundM, nameL2d, nameR2d, nameL2d - nameR2d, plateL, plateR, plateL - plateR, depth.plateShift, depth.shift);
                // ... and the world it came from: the target's place, its body, the point over its head, the eyes; and where the game's own head point (the plate's
                // anchor, as its hook last answered it) stands - each eye's camera here is the one the shader projects with (the wrist medallions', the debug capsules')
                plateworld::Follow fw;
                const bool fOk = plateworld::GetFollow(&fw, GetTickCount64());
                stroke::Body body{};
                const bool bodyOk = meleetouch::BodyFor(meleetouch::MonsterRow{depth.row[0], depth.row[1], depth.row[2]}, depth.foot[0], depth.foot[1], depth.floorM, upmNow, body);
                LogF("vrcam: [hud] monster_names_own - the world: target on the grid %.1f,%.1f (game units; why %d), floor %.2f m, row %u,%u,%u -> body %s from %.2f,%.2f,%.2f to "
                     "%.2f,%.2f,%.2f m r %.2f; the name's point %.2f,%.2f,%.2f m (render); the eyes at %.2f,%.2f,%.2f m, %.2f m off; eye cameras: left off %.2f,%.2f,%.2f back "
                     "%.2f,%.2f,%.2f proj %.3f,%.3f,%.3f,%.3f, right off %.2f,%.2f,%.2f back %.2f,%.2f,%.2f; the game's head point (plate anchor) %s %.0f,%.0f px of a %.0fx%.0f "
                     "viewport (= %.0f,%.0f of the eye)",
                     depth.foot[0], depth.foot[1], depth.why, depth.floorM, depth.row[0], depth.row[1], depth.row[2], bodyOk ? "" : "(NOT made)", body.bottom.x, body.bottom.y,
                     body.bottom.z, body.top.x, body.top.y + body.radius, body.top.z, body.radius, depth.headM[0], depth.headM[1], depth.headM[2], depth.eyesM[0], depth.eyesM[1],
                     depth.eyesM[2],
                     std::sqrt((depth.headM[0] - depth.eyesM[0]) * (depth.headM[0] - depth.eyesM[0]) + (depth.headM[1] - depth.eyesM[1]) * (depth.headM[1] - depth.eyesM[1]) +
                               (depth.headM[2] - depth.eyesM[2]) * (depth.headM[2] - depth.eyesM[2])),
                     g_namesCam[0].off[0] / upmNow, g_namesCam[0].off[1] / upmNow, g_namesCam[0].off[2] / upmNow, g_namesCam[0].axes[6], g_namesCam[0].axes[7], g_namesCam[0].axes[8],
                     g_namesCam[0].proj[0], g_namesCam[0].proj[1], g_namesCam[0].proj[2], g_namesCam[0].proj[3], g_namesCam[1].off[0] / upmNow, g_namesCam[1].off[1] / upmNow,
                     g_namesCam[1].off[2] / upmNow, g_namesCam[1].axes[6], g_namesCam[1].axes[7], g_namesCam[1].axes[8], fOk ? "at" : "NOT known,", fOk ? fw.px[0][0] : 0.0f,
                     fOk ? fw.px[0][1] : 0.0f, fOk ? fw.view[0] : 0.0f, fOk ? fw.view[1] : 0.0f, fOk && fw.view[0] > 0.0f ? fw.px[0][0] / fw.view[0] * (float)bw : 0.0f,
                     fOk && fw.view[1] > 0.0f ? fw.px[0][1] / fw.view[1] * (float)bh : 0.0f);
            }
            if (k > toldK) {
                toldK = k;
                LogF("vrcam: [hud] monster_names_own - our names shown (%d of %d texts; the target's kind %s 0x%02X; %.1f m off: size x%.2f by names_own_size_pct %.0f / "
                     "names_own_far_pct %.0f, backdrop %.2f; eye %ux%u)", k, n, known ? "read" : "not read", special, depth.groundM, grow[0], g_set.namesSize.load(),
                     g_set.namesFar.load(), g_set.namesAlpha.load(), bw, bh);
            }
        }
    }
    SetBool(rt, "PlateNamesOn", k > 0);
    SetInt(rt, "PlateNameCount", k);
    if (k) {
        SetFloats(rt, "PlateNameCells", cells, (size_t)k * 4);
        SetFloats(rt, "PlateNameRects", rects, (size_t)k * 4);
        SetFloats(rt, "PlateNameShift", nShift, (size_t)k);
        SetFloats(rt, "PlateNameTurnH", nTurn, (size_t)k);
        SetFloats(rt, "PlateNameAlpha", nAlpha, (size_t)k);
        SetFloats(rt, "PlateNameAt", nAt, (size_t)k * 4);
        SetFloats(rt, "PlateNameLift", nLift, (size_t)k);
    }
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

// [hud] low_mp_vignette (vr/low_mp.h): the low mana's blue at the bottom, the same way (an older effect without LowMpAlpha: told once, nothing).
template <class RT> void LowMpUniforms(RT* rt) {
    static bool sent = false;
    lowmp::Uniforms u;
    const bool on = lowhp::CurrentMana(&u);
    if (!on && !sent) return;
    if (on && rt->find_uniform_variable("D2R_DepthFog.fx", "LowMpAlpha").handle == 0) {
        static bool told = false;
        if (!told) { told = true; Log("vrcam: low mana vignette - D2R_DepthFog.fx has no LowMpAlpha: it is an older one, copy the new one in (the vignette needs the DLL and the effect together)"); }
        return;
    }
    sent = on;
    SetFloat(rt, "LowMpAlpha", on ? u.alpha : 0.0f);
    if (on) {
        SetFloats(rt, "LowMpShape", u.shape, 4);
        SetFloats(rt, "LowMpColor", u.color, 3);
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
// The Gestures' glyph trigger held still with a skill armed that flies to where it is aimed (the icon cache's aims, from the game's skills.txt): its aim line is drawn
// as [debug] spell_aim_ray draws one - from that hand (AimHand follows the glyph trigger while it aims) - so the player points where it goes; a point skill shows
// point_cast's arc and circle instead (its slot held), a plain one nothing.
bool GlyphAimRayWanted() {
    int hand = -1;
    int32_t skill = -1;
    return trigroleswire::Aiming(&hand, &skill) && skill >= 0 && wheelicons::Aims(skill) == 1 && !(pointwire::On() && pointwire::IsPoint(skill)) && viewgate::F4();
}
bool SpellRayWanted() { return (g_set.spellAimRay.load() || GlyphAimRayWanted()) && g_enabled.load() && g_inWorld.load() && FullBody() && !ThirdPerson() && !BowInHand() && !ThrownInHand(); }
// [debug] point_pickup_ray: the ray [input] point_pickup picks along, VR first person (F4)
bool PointRayWanted() { return g_set.pointPickupRay.load() && g_set.pointPickup.load() && g_enabled.load() && g_inWorld.load() && FullBody() && !ThirdPerson(); }
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
// [debug] grip_draw: the grips as capsules in the headset, VR first person (F4) - both weapon attach bones' axes (x red, y green, z blue; a shrunk one hangs nothing: not drawn), each hand's
// palm (a grey ball) and its wrist's axes (thinner), the shaft (a grey line), the second hand's take zone round it (green glass: the pommel to the hilt's end, as wide as a fist takes it)
// and where each holding hand sits on it (right amber, left blue). Points in the head's turn-only frame from the eye (skel::GripDrawNow, weight::ZoneDraw), turned onto the view as the
// red line's: metres * upm along camR / camF.
bool GripDrawWanted() { return weight::GripDraw() && g_enabled.load() && g_inWorld.load() && FullBody() && !ThirdPerson(); }
int GripDrawMarks(const StickFrame& f, float upm, float (*a)[4], float (*b)[4], int max) {
    int n = 0;
    auto turn = [&](const float* v, float* w) {   // the head frame -> the view, from the eye, metres (as the phantom block's world())
        for (int i = 0; i < 3; ++i) w[i] = v[0] * f.camR[i] - v[2] * f.camF[i];
        w[1] += v[1];
    };
    auto put = [&](const float* p, const float* q, float radiusM, int colour, int style) {
        if (n >= max) return;
        float wp[3], wq[3];
        turn(p, wp);
        turn(q, wq);
        for (int i = 0; i < 3; ++i) { a[n][i] = wp[i] * upm; b[n][i] = wq[i] * upm; }
        a[n][3] = radiusM * upm;
        b[n][3] = (float)meleeboxes::TouchKind(colour, style);
        ++n;
    };
    auto axes = [&](const float o[3], const float ax[3][3], float len, float r) {
        static const int colour[3] = {meleeboxes::kCRed, meleeboxes::kCGreen, meleeboxes::kCBlue};
        for (int k = 0; k < 3; ++k) {
            const float e[3] = {o[0] + ax[k][0] * len, o[1] + ax[k][1] * len, o[2] + ax[k][2] * len};
            put(o, e, r, colour[k], meleeboxes::kTube);
        }
    };
    skel::GripDraw g;
    if (skel::GripDrawNow(&g)) {
        for (int sd = 0; sd < 2; ++sd) {
            if (g.attachShown[sd]) axes(g.attach[sd], g.attachAxes[sd], sd == g.drawnBone ? 0.09f : 0.06f, sd == g.drawnBone ? 0.004f : 0.0025f);
            put(g.hand[sd], g.hand[sd], 0.012f, meleeboxes::kCGrey, meleeboxes::kBall);
            axes(g.hand[sd], g.handAxes[sd], 0.04f, 0.0015f);
        }
    }
    weight::GripZoneDraw z;
    if (weight::ZoneDraw(&z)) {
        put(z.butt, z.tip, 0.004f, meleeboxes::kCGrey, meleeboxes::kTube);
        const bool second = z.held && (z.holds[0] != z.holds[1]);   // one hand on it: the zone is the other hand's
        if (second || !z.held) put(z.butt, z.zoneEnd, std::min(0.06f, z.reach * 0.5f), meleeboxes::kCGreen, meleeboxes::kGlass);
        for (int sd = 0; sd < 2; ++sd)
            if (z.holds[sd]) put(z.hand[sd], z.hand[sd], 0.02f, sd == 0 ? meleeboxes::kCAmber : meleeboxes::kCBlue, meleeboxes::kBall);
    }
    return n;
}
#else
bool PhantomWanted() { return false; }
bool BoneAxesWanted() { return false; }
bool XbowBarrelWanted() { return false; }
bool SpellRayWanted() { return false; }
bool PointRayWanted() { return false; }
bool BowVectorsWanted() { return false; }
bool BowAimRayWanted() { return false; }
bool BowQuiverWanted() { return false; }
bool BowOrbsWanted() { return false; }
bool BowArrowFxWanted() { return false; }
bool MeleeBoxesWanted() { return false; }
bool SkillTriggersWanted() { return false; }
bool HandOrbsWanted() { return false; }
bool MercTintWanted() { return false; }
bool GripDrawWanted() { return false; }
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
bool LowHpWanted() { return (lowhp::Enabled() || lowhp::ManaEnabled()) && g_enabled.load() && FullBody() && !ThirdPerson() && xr::Running() && g_inWorld.load(); }

// Who draws the sky and the fog (OurFogWanted, FogCannot, FogWanted, OurSkyWanted): vr/sky/sky_choice.cpp
// [sky] bright_<period>: the sky's multiplier for this moment of the game's day (EffectsBegin, a present behind at most) - the open-air
// fog darkens with it as the sky and the dome do (Serg 2026-10-31 on 0.192.19, 114.webp: "ночью туман не тёмный")
std::atomic<float> g_periodMul{1.0f};
// The fog's colour as the post pass lays it: the act's own (its day colour, darker at night with the sky), a cave's own underground;
// false without one (the post pass then takes the sky's horizon; `rgb` a dark grey - what [fog] game_fog gives the game then)
// The baked sky's colour at the horizon now, in the game's scene units (vr/sky/three_skies.h HorizonScene; EffectsBegin, a present behind at
// most): the fog's colour while [fog] fog_colour_measured is off - Serg: "цвет тумана не надо замерять он берется из ... неба", "и равен цвету
// неба". Set when the period, the picture or a slider changes; held 1 s (no effect drawing: none). With it the day sky's, for [fog]
// fog_keeps_colour: at night the night picture's horizon is all but black (act 1: 0.0041 0.0046 0.0069) - the far land went into it and
// the fog was gone (Serg on 0.192.34, Rogue camp at night: "с отпущенной вообще туман пропадает", his box ticked)
SRWLOCK g_skyFogLock = SRWLOCK_INIT;
float g_skyFogScene[3] = {};
float g_skyFogDay[3] = {};
ULONGLONG g_skyFogAt = 0;
void SetSkyFog(bool ok, const float scene[3], const float day[3] = nullptr) {
    AcquireSRWLockExclusive(&g_skyFogLock);
    g_skyFogAt = ok ? GetTickCount64() : 0;
    if (ok) {
        memcpy(g_skyFogScene, scene, sizeof g_skyFogScene);
        memcpy(g_skyFogDay, day, sizeof g_skyFogDay);
    }
    ReleaseSRWLockExclusive(&g_skyFogLock);
}
bool SkyFogNow(float scene[3], bool day) {
    AcquireSRWLockShared(&g_skyFogLock);
    const bool ok = g_skyFogAt && GetTickCount64() - g_skyFogAt < 1000;
    if (ok) memcpy(scene, day ? g_skyFogDay : g_skyFogScene, sizeof g_skyFogScene);
    ReleaseSRWLockShared(&g_skyFogLock);
    return ok;
}
bool FogColourNow(float rgb[3]) {
    static const float kDark[3] = {0.02f, 0.02f, 0.025f};
    // ([fog] fog_keeps_colour while the game draws the fog: the day sky's at every hour - a haze, never black - as the act's colour below)
    const bool keeps = g_set.fogKeepsColour.load() && gamefogvr::Active();
    if (float scene[3]; !g_set.fogMeasured.load() && !g_underground.load() && SkyFogNow(scene, keeps)) {   // the baked sky's own at the horizon
        // (as a display colour: every fog of ours takes it back to the scene's units - SceneFromDisplay - exactly, below the tone map's knee)
        const fogframe::V3 d = fogframe::DisplayFromScene({scene[0], scene[1], scene[2]});
        rgb[0] = d.r; rgb[1] = d.g; rgb[2] = d.b;
        return true;
    }
    SkyAct fa{};
    CaveFog cave{};
    AcquireSRWLockShared(&g_skyCfgLock);
    if (const int a = g_fogAct.load(); a > 0) fa = g_skyAct[a];
    cave = g_caveFog;
    if (ArcaneOwnFog() && g_arcaneFogColour.set) cave = g_arcaneFogColour;   // [fog] arcane_color (unset: the caves', as before)
    ReleaseSRWLockShared(&g_skyCfgLock);
    if (g_underground.load() && cave.set) { fa.fogSet = true; memcpy(fa.fog, cave.rgb, sizeof fa.fog); }   // caves and dungeons: their own
    else if (fa.fogSet && !(g_set.fogKeepsColour.load() && gamefogvr::Active())) {   // in the open the act's fog (its day colour) darkens at night with the sky
        // ([fog] fog_keeps_colour: not while the game draws the fog - Serg on 0.188.4, "прозрачнее, а не чернее": the far fades into a light
        // haze of his color_<act>, never toward black; the game's light read 32 - "end of dawn, day" - put act 2's fog at x0.25. Our own
        // effect's fog as it was: Serg, "the old one was good". Since 2026-10-31 he wants it dark at night again - "ночью туман не тёмный",
        // a grey-white band under the black night sky: the shipped ini has it 0, and his bright_<period> sliders go on it as on the sky)
        const float m = DayNightMul() * g_periodMul.load(std::memory_order_relaxed);
        for (float& f : fa.fog) f *= m;
    }
    memcpy(rgb, fa.fogSet ? fa.fog : kDark, 3 * sizeof(float));
    return fa.fogSet;
}
// The table (F5): the void black, the game lifted off black - D2R_DepthFog.fx TableKey.
// Not with [table] full_width (TableFull): no edge, no keyed void, nothing lifted off black - the picture as the game draws it, its dome round it.
bool TableKeyWanted() { return TableView() && !TableFull() && g_inWorld.load() && !gamestate::MenuOpen(); }
// ... and the cave ceiling (CeilWanted, GameCeilWanted): vr/sky/sky_choice.cpp

// The pictures of each act as the shader's preprocessor definitions; only what
// differs is set, since every change recompiles the effect. True if it did.
// Done here, after the effect pass, like the technique switch below.
// The colour measures compiled into the effect at all (D2R_DOME_MEASURE, D2R_FOG_MEASURE): the dome's only with the parked tinting ([sky]
// dome_tint and dome_true_colour); the fog's never since [fog] game_fog_match went (2026-11-08) - off none of their
// textures, passes or readbacks is made. A change builds the effect again (as a picture's does). True if it did.
template <class RT> bool ApplyMeasures(RT* rt) {
    bool set = false;
    const bool want[2] = {g_set.domeTint.load() && g_set.domeTrueColour.load(), false};
    static const char* const kNames[2] = {"D2R_DOME_MEASURE", "D2R_FOG_MEASURE"};
    for (int k = 0; k < 2; ++k) {
        const char* v = want[k] ? "1" : "0";
        char have[16] = {};
        size_t size = sizeof have;
        const bool known = rt->get_preprocessor_definition_for_effect("D2R_DepthFog.fx", kNames[k], have, &size);
        if (known ? strcmp(have, v) == 0 : !want[k]) continue;   // (never set and not wanted: the effect's own 0)
        rt->set_preprocessor_definition_for_effect("D2R_DepthFog.fx", kNames[k], v);
        LogF("vrcam: %s = %s - the %s colour measure %s", kNames[k], v, k ? "fog's" : "sky's", want[k] ? "compiled in" : "left out of the effect");
        set = true;
    }
    return set;
}
template <class RT> bool ApplySkyPictures(RT* rt) {
    static uint32_t applied = ~0u;
    const uint32_t gen = g_skyCfgGen.load();
    const bool measures = ApplyMeasures(rt);   // (a change builds the effect again: the pictures' definitions with it, if they changed too)
    if (gen == applied) {
        if (measures) Forget(nullptr);
        return measures;
    }
    applied = gen;
    bool set = measures;
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
    const bool want = GripDrawWanted() || FogWanted() || OurSkyWanted() || g_arcaneSky.load() || CeilWanted() || HudWanted() || StampWanted() || hud::PictureWanted() || TableKeyWanted() || PhantomWanted() || BoneAxesWanted() || BowVectorsWanted() || BowQuiverWanted() || MercTintWanted() || BowAimRayWanted() || XbowBarrelWanted() || SpellRayWanted() || PointRayWanted() || MeleeBoxesWanted() || SkillTriggersWanted() || HandOrbsWanted() || LowHpWanted() || BowOrbsWanted() || BowArrowFxWanted() || DistanceBlurWanted();
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

// [render] flatvr_feed (vr/flatvr_feed.h): without ReShade FlatVR gets this present's picture - and the scene's depth copied for it
// (scenedepth::BindNative) - from the plugin, through the blocks FlatVR's ReShade add-on would publish them on. VR, FlatVR running
// (its head sample moves), native OpenXR not holding the headset; otherwise the blocks say "no picture".
void FlatVrFeed(ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12Resource* bb) {
    const bool want = g_set.flatvrFeed.load() && g_set.platform.load() == 1 && dev && q && bb && !(xr::On() || xr::Running() || xr::StopPending()) &&
                      xr::FlatVrRunning();
    static int was = -1;
    if ((int)want != was) {
        was = want;
        LogF("vrcam: flatvr feed - %s", want ? "on: FlatVR's picture and depth from the plugin itself (no ReShade, [render] flatvr_feed)"
                                            : "standing by (native OpenXR, FlatVR not running, flat mode or [render] flatvr_feed=0)");
    }
    if (!want) { flatvrfeed::Idle(); return; }
    if (replay::SkipWhileStuck("the FlatVR feed")) return;
    const bool ours = replay::t_ours;
    replay::t_ours = true;   // (our submission kept out of vrcam's own queue hooks, as monitor_eye's)
    flatvrfeed::Present(dev, q, bb, g_afrBlock, scenedepth::g_nCopy, (unsigned)nativedepth::kReadable, scenedepth::g_copiedNow);
    replay::t_ours = ours;
}

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
        // ... and the game's dome ([sky] game_sky, dome_past_far: it stands past the linear depth's far end, so the pass took it for the void and
        // laid its mist over it whole - the dome, coloured once by our bake (dome_in_game_colour), tinted again by the mist; Serg on 0.192.23:
        // "почему купол досих пор красится дважды". The mist there also went into DomeSeen's measure of it. The dome is the sky: nothing of
        // the game's fog pass on it, as the step before the effects leaves it, Params::skyDepth)
        const bool voidAlone = (g_set.fogCavesBlack.load() && g_underground.load()) || engitem::DomeShown();
        // [fog] game_fog_before_fx: the same fog laid on the scene before the game's transparent resolve (smoke and fire then over it, unfogged);
        // while it is drawn the game's pass leaves the fog's colour out (its mist still fogged)
        // [fog] game_fog_our_curve: our effect's curve (D2R_DepthFog.fx FogAt) - [fog] start / end / curve, underground the caves' start / end
        // / strength - in the game's pass and the step before the effects
        fogheight::Curve curve;
        curve.on = g_set.fogOurCurve.load();
        if (curve.on) {
            const FogNumbers n = GameFogNumbersNow();   // (the game's fog's: ours sets its own below)
            curve.start = n.start;
            curve.end = n.end;
            curve.curve = n.curve;
            curve.strength = n.strength;
            curve.ease = g_set.fogEaseIn.load();   // [fog] ease_in (D2R VR Unleashed's)
        }
        fogfx::Params fp;
        // ([fog] game_fog_planes: the planes are drawn by the same step - with the game's fog only)
        const bool planesOn = g_set.gameFogPlanes.load() && g_set.gameFog.load();
        const bool beforeFx = (g_set.fogBeforeFx.load() || planesOn) && GameFogDrawnView() && !TableFull() && fogfx::Hooked();   // (VR F3 full width: no fog tweak)   // (its hooks: no ReShade, the key on at the start)
        {   // the switch, live (its hooks stay in from the start; off: the step draws nothing, the game's fog pass keeps its colour) - one line
            static int toldKey = -1;
            const int key = g_set.fogBeforeFx.load() ? 1 : 0;
            if (fogfx::Hooked() && key != toldKey) {
                if (toldKey >= 0)
                    Log(key ? "vrcam: fog before the effects ON again: the step draws, the game's fog pass leaves its colour out"
                            : "vrcam: fog before the effects OFF: nothing of the step drawn, the game's fog pass as before");
                toldKey = key;
            }
        }
        fp.on = beforeFx && gamefogvr::Active();
        fp.fxDepth = g_set.fogFxDepth.load();   // [fog] game_fog_fx_depth: the effects fogged by their own distance (either fog)
        fp.ourCurve = curve.on;
        fp.start = curve.start; fp.end = curve.end; fp.curve = curve.curve; fp.strength = curve.strength; fp.ease = curve.ease;
        // ... or our effect's fog ([fog] game_fog=0): its curve by the view's depth (FogAmount), its colour as the post pass lays it in the
        // scene's units; the post pass then leaves a drawn pixel's fog to the step (FogInFrame). Only with a colour of its own (the act's, a
        // cave's, or no sky: the dark grey) - without one the post pass takes the sky's horizon by the view's direction - and not in the
        // Arcane Sanctuary (its fog is its sky behind the point, ArcaneHaze)
        float ourRgb[3];
        if (!fp.on && beforeFx && FogWanted() && (FogColourNow(ourRgb) || !OurSkyWanted()) && !ArcaneHazeFog()) {
            const FogNumbers n = FogNumbersNow();
            fp.on = fp.ours = fp.ourCurve = true;
            fp.start = n.start;
            fp.end = n.end;
            fp.curve = n.curve;
            fp.strength = n.strength;
            fp.ease = g_set.fogEaseIn.load();
            const fogframe::V3 s = fogframe::SceneFromDisplay({ourRgb[0], ourRgb[1], ourRgb[2]});
            fp.colour[0] = s.r; fp.colour[1] = s.g; fp.colour[2] = s.b;
            fp.nearPlane = g_lastNear.load();
            fp.sx = g_projSx.load();
            fp.sy = g_projSy.load();
        } else if (fp.on) {
            // exactly the five floats the game's fog pass got this frame (its colour by the game's time with [fog] game_fog_colour=0, the
            // game's own with 1) - until 0.209.10 the step took our colour as set (Current), a lighter grey than the pass's: "только легкий
            // серый оттенок" (Serg 2026-11-09). None written yet: ours, the colour as the pass will get it
            gamefogvr::Fog gf = gamefogvr::Current();
            gamefogvr::Fog wrote{};
            const bool written = gamefogvr::Written(&wrote);
            if (written) gf = wrote;
            else {
                float rgb[3];
                gamefogvr::ColourNow(rgb);
                gf.r = rgb[0]; gf.g = rgb[1]; gf.b = rgb[2];
            }
            fp.plane = gf.plane;
            fp.density = gf.density;
            fp.colour[0] = gf.r; fp.colour[1] = gf.g; fp.colour[2] = gf.b;
            // the proof, once a change: the game's pass's numbers and the step's (the same floats), the share both reach at Full fog
            static gamefogvr::Fog told{-1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
            static float toldNear = -1.0f;
            const float nearNow = g_lastNear.load();
            if (std::fabs(gf.plane - told.plane) > 0.05f || std::fabs(gf.density - told.density) > 1e-4f || std::fabs(gf.r - told.r) > 1e-4f ||
                std::fabs(gf.g - told.g) > 1e-4f || std::fabs(gf.b - told.b) > 1e-4f || std::fabs(nearNow - toldNear) > 1e-4f) {
                told = gf;
                toldNear = nearNow;
                const FogNumbers n = GameFogNumbersNow();
                LogF("vrcam: fog before the effects - the same fog as the game's pass: the game's pass got plane %.1f, density %.4f, colour %.4f %.4f "
                     "%.4f (%s); the step draws plane %.1f, density %.4f, colour %.4f %.4f %.4f; both reach %.0f%% at Full fog %.0f (from %.0f) - "
                     "the step's distance from the depth with near %.3f (the depth range's far end %.0f)",
                     wrote.plane, wrote.density, wrote.r, wrote.g, wrote.b, written ? "as written into its frame settings" : "none written yet",
                     fp.plane, fp.density, fp.colour[0], fp.colour[1], fp.colour[2],
                     100.0f * gamefogvr::GameFogAmount(gf, n.end), n.end, n.start, nearNow, depthsrc::LastFar());
            }
            fp.nearPlane = g_lastNear.load();
            fp.sx = g_projSx.load();
            fp.sy = g_projSy.load();
            fp.height = height;
            fp.top = FogTopNow();
            fp.soft = g_set.fogSoft.load();
            fp.up[0] = g_viewRightY.load(); fp.up[1] = g_viewUpY.load(); fp.up[2] = g_viewFwdY.load();
        }
        // ([sky] game_sky: the game's dome left as the void is - its depth band, as the post pass's SkyDomeDepth)
        fp.skyDepth = engitem::DomeShown() ? skydome::DomeDepth(engitem::DomeRadius(), g_lastNear.load()) : 0.0f;
        if (planesOn && !fp.ours) {   // the planes from the game's fog's start to its Full fog ([fog] game_fog_start / _end; underground the caves')
            const FogNumbers n = GameFogNumbersNow();
            fp.planes = g_set.gameFogPlanesCount.load();
            fp.planesStart = n.start;
            fp.planesEnd = n.end;
            fp.planesStrength = n.strength;
            fp.planesSoft = g_set.gameFogPlanesSoft.load();
        }
        if (!fp.ours) fp.cover = g_set.gameFogCover.load();   // [fog] game_fog_cover: the fog's colour whole from this distance on (0 off)
        fogfx::Configure(fp);
        fogfx::Frame();
        {   // the submission order: the scene's state followed through the game's direct queue (the tap beside the replay's observer), once
            static bool tapped = false;
            if (!tapped && fogfx::Hooked())
                if (ID3D12CommandQueue* q = natived3d::GameQueue()) {
                    tapped = eclhook::InstallTap(q, &fogfx::OnExecute);
                    Log(tapped ? "vrcam: fog before the effects - the game's submissions followed (the scene's state in the order the lists run)"
                               : "vrcam: fog before the effects - the game's submissions could NOT be followed: drawn only from states the resolve's list sets");
                }
        }
        const bool noFog = fogfx::Drawn() && !fogfx::OursDrawn();   // (our effect's fog in the step: the game's pass has none of ours to leave out)
        fogheight::Frame((height || mist || voidAlone || noFog || curve.on) && gamefogvr::Active() && GameFogDrawnView() && !TableFull(), height ? FogTopNow() : 1.0e9f,
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
    foveation::NoteScreen(dev, sw, sh);   // [render] foveated: the eyes' picture's size, and VRS tier 2 looked for once (nothing when not hooked)
    lensmask::NoteScreen(sw, sh, renderscale::g_last);   // [render] lens_mask: the scene depth's size, and the share an upscaler draws into
    refleye::Tell();                   // [render] reflections_per_eye: a line when its state changes, every 10 s while bound
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
    if (!rt) FlatVrFeed(dev, q, bb);   // [render] flatvr_feed: no ReShade - FlatVR's picture from us, before monitor_eye covers it for the window
    replay::ShotPresent(dev, q, bb);   // [headset_shot] on FlatVR: the picture passed on, before monitor_eye covers it for the window
    replay::MonitorPresent(rt, wnd, q, bb);   // [render] monitor_eye: the window one eye, after the headset took this one
    if (rt && !replay::g_capturing.load() && markLater) scenedepth::MarkFx(rt);   // after the eye made's overlays (XrPresent)
    const double t = pairtime::UsNow();
    pairtime::g_presentAt.store(t);
    pairtime::g_presents.fetch_add(1, std::memory_order_relaxed);
    drawprof::Tick();
    logasync::SetHotThread(GetCurrentThreadId());   // [debug] log_async_auto: this thread's (the game's draw thread) lines go to the log's writer thread
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

// No ReShade (docs/plan_no_reshade.md, step 3; the only path since 0.203): the plugin's own hooks (vr/native_d3d12.h) give what
// its events gave native OpenXR eyes by turns - the game's present (PresentCommon, as reshade_present), the scene's depth for the eye made
// (scenedepth: the binds and barriers counted, the copy before the present, as the effects' begin did) and the share of the screen the scene is
// drawn at (renderscale, DLSS). Nothing else: no effect, so no fog, sky, ceiling or overlays; no replay (one pass, two pictures), no light
// bounds, no menu panel (hud's layer). FlatVR's picture and depth: [render] flatvr_feed (FlatVrFeed, vr/flatvr_feed.h).
template <class RT> void EffectsBegin(RT* rt, reshade::api::command_list* cl);   // (below, with the effect's uniforms)
template <class RT> void EffectWorldOff(RT* rt);   // ([render] effect_world, below)
// [sky] dome_in_game_colour: the numbers the effect has for the game's dome now (set with its uniforms: DomeNightMix, DomeDayMul, SkyCapOn,
// SkyZenith; the act's picture slot SkyTex) - the bake of the game's own picture takes the same (noreshade::DomeAtlas)
struct DomeBake { float night = 0.0f, mul = 1.0f; bool capOn = true; int slot = 0; float zenith[3] = {0.10f, 0.13f, 0.18f}; };
DomeBake g_domeBake;
namespace noreshade {
// [render] native_effects (on): D2R_DepthFog.fx run by the plugin itself (vr/fx_native.h) - the sky, the fog, the cave ceiling and the rest of
// its main technique, built in the background at the start and at each new size; the same uniform code as with ReShade (EffectsBegin /
// EffectsFinish). Off: no effect at all, as no_reshade was first (the lightest).
fxnative::Runtime* g_fx = nullptr;
double g_cpuUs = 0.0, g_effUs = 0.0;   // the present hook's own time on the CPU, and the effects' part of it (the 10 s line)
std::atomic<uint32_t> g_rectWhy[phantomrect::kWhyCount] = {};   // [render] phantom_rects: the full-screen vignette passes by carrier (the 10 s line)
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
    static bool wasFlat = false;
    if (!toldMode || byTurns != wasByTurns || m.vr == wasFlat) {
        toldMode = true;
        wasByTurns = byTurns;
        wasFlat = !m.vr;
        if (!m.vr) LogF("vrcam: no ReShade - the flat game: one picture on the monitor, drawn by the plugin's own hooks; without ReShade: %s",
                        nors::kOffFlat);
        else if (byTurns) Log("vrcam: no ReShade - native OpenXR eyes by turns: one eye drawn a frame, the other made on the GPU with the game's depth");
        else if (replay::NativeOn() && m.pairPerTick)
            Log("vrcam: no ReShade - both eyes from one game frame: the right eye drawn again from the left pass's lists ([stereo] native_replay=1, "
                "F4 on native OpenXR, once an area has settled); otherwise two passes a pair");
        else Log("vrcam: no ReShade - NOT native OpenXR eyes by turns with the eye made ([openxr] on, [stereo] afr=1 pair_per_tick=0 turn_warp=1): "
                 "the plugin's own hooks are made for that mode - one pass two pictures went with ReShade (the pair is drawn in two passes); FlatVR's "
                 "picture and depth come from the plugin itself with [render] flatvr_feed");
    }
    natived3d::WatchIndexCount(g_set.gameSky.load() ? engitem::DomeIndexCount() : 0);
    natived3d::WatchDomeAtlas(g_set.gameSky.load());   // (the game's view of the domes' picture: told to the log - read only)   // [sky] game_sky: the game's draws of the dome's mesh
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
    // the presents held for the headset's rate ([stereo] pace, FlatVR) are waiting inside the present, not its work: said apart
    const double held = std::min(pace::g_heldUs, g_cpuUs);
    LogF("vrcam: no ReShade, 10 s: on the CPU our present work %.2f ms a present (the effects %.2f), and %.2f ms a present held for the headset's "
         "rate ([stereo] pace: waiting, not work); our lists %u, %u of them waited for the graphics card (%.2f ms in all); %u presents of the "
         "game's swap chain; the game bound a depth-stencil %u times (%u more with a view made before the hook: not known); %u depth-stencil views "
         "known; the scene's depth %s",
         g_timed ? (g_cpuUs - held) / 1000.0 / g_timed : 0.0, g_timed ? g_effUs / 1000.0 / g_timed : 0.0, g_timed ? held / 1000.0 / g_timed : 0.0,
         st.lists, st.listWaits, st.listWaitMs, st.presents, st.depthBinds, st.unknownViews, st.views,
         scenedepth::g_cache.Counting() ? "not picked yet" : "picked");
    if (natived3d::CensusOn()) {   // [debug] call_census: the game's calls through our hooks, a present, the draw thread apart (view only)
        const natived3d::Census c = natived3d::TakeCensus(GetCurrentThreadId());
        const double k = st.presents ? 1.0 / st.presents : 0.0;
        char d[700];
        int at = 0;
        for (int side = 0; side < 2; ++side) {
            const uint32_t* n = side ? c.other : c.draw;
            at += snprintf(d + at, sizeof d - at, "%s%s: draws %.0f (indexed %.0f), dispatches %.0f, indirect %.0f, pipelines %.0f, target binds %.0f, "
                           "barriers %.0f (%.0f calls), viewports %.0f, scissors %.0f, clears %.0f + %.0f depth, list resets %.1f",
                           side ? " | " : "", side ? "the other threads" : "the draw thread", (n[natived3d::kCensusDrawIndexed] + n[natived3d::kCensusDraw]) * k,
                           n[natived3d::kCensusDrawIndexed] * k, n[natived3d::kCensusDispatch] * k, n[natived3d::kCensusIndirect] * k,
                           n[natived3d::kCensusPso] * k, n[natived3d::kCensusTargets] * k, n[natived3d::kCensusBarriers] * k,
                           n[natived3d::kCensusBarrierCalls] * k, n[natived3d::kCensusViewports] * k, n[natived3d::kCensusScissors] * k,
                           n[natived3d::kCensusClearRtv] * k, n[natived3d::kCensusClearDsv] * k, n[natived3d::kCensusReset] * k);
            if (at >= (int)sizeof d) break;
        }
        LogF("vrcam: call census, 10 s ([debug] call_census): the game's calls a present (%u presents, %s now) - %s; %d threads recorded%s", st.presents,
             viewgate::F4() ? "VR F4" : "not VR F4", d, c.threads, c.lost ? " (more than 64 threads: some not counted)" : "");
    }
    g_cpuUs = g_effUs = 0.0;
    pace::g_heldUs = 0.0;
    g_timed = 0;
    if (const flatvrfeed::Stats fs = flatvrfeed::TakeStats(); fs.presents)
        LogF("vrcam: flatvr feed, 10 s: %u presents - %u stereo pairs and %u single pictures handed to FlatVR, its depth %u times%s; %u half pairs "
             "lost (the same eye twice)", fs.presents, fs.pairs, fs.singles, fs.depths, fs.depthOk ? "" : " (no depth texture)", fs.halfLost);
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
        char blur[48] = "off";   // [render] distance_blur: its two passes' time (ms a present), whether or not among the dearest
        if (on("DistBlurOn")) snprintf(blur, sizeof blur, "on, %.2f ms", fs.MsOf("PS_DistBlur"));
        autotunerun::NoteGpu(fs.MsOf("PS_DepthFog"), fs.MsOf("PS_DistBlur"), fogfx::Hooked());   // ([autotune]: the order its probes run in)
        LogF("vrcam: native effects, 10 s: %s, sky %s (picture %d), fog %s, ceiling %s, distance blur %s; on the graphics card %.2f ms a present (the overlays "
             "apart not timed); %u passes in %u presents and %u overlay draws, %u left out with nothing to draw (D2R_DepthFog.fx, %ux%u) - dearest "
             "passes (ms a present, and when run): %s",
             fs.ready ? (g_set.effectWorld.load() ? "drawn" : "drawn, its sky and fog LEFT OUT ([render] effect_world=0)") : fs.compiling ? "building"
                      : fs.failed ? "NOT built (above)" : "waiting", on("SkyOn") ? "on" : "off",
             (int)(skyTex + 0.5f), !on("FogOn") ? "off" : on("FogLight") ? "light" : "full", on("CeilOn") && !on("FogLight") ? "on" : "off", blur, fs.gpuMs,
             fs.passes, fs.renders, fs.techniques, fs.skipped, fs.width, fs.height, fs.dearest.empty() ? "-" : fs.dearest.c_str());
        if (fs.rectPasses) {   // [render] phantom_rects: the vignette pass's pixels and time (vr/phantom_rect.h)
            const double share = fs.rectPixelsFull ? 100.0 * (double)fs.rectPixels / (double)fs.rectPixelsFull : 100.0;
            char why[400] = "";   // which carrier sent the full-screen ones (phantomrect::Why), most first
            {
                uint32_t nWhy[phantomrect::kWhyCount];
                for (int i = 0; i < phantomrect::kWhyCount; ++i) nWhy[i] = g_rectWhy[i].exchange(0, std::memory_order_relaxed);
                int at = 0;
                for (int pick = 0; pick < phantomrect::kWhyCount && at >= 0 && at < (int)sizeof why - 60; ++pick) {
                    int best = -1;
                    for (int i = 0; i < phantomrect::kWhyCount; ++i) if (nWhy[i] && (best < 0 || nWhy[i] > nWhy[best])) best = i;
                    if (best < 0) break;
                    at += snprintf(why + at, sizeof why - at, "%s%s %u", at ? ", " : "", phantomrect::WhyName(best), nWhy[best]);
                    nWhy[best] = 0;
                }
                if (!at) snprintf(why, sizeof why, "none");
            }
            LogF("vrcam: native effects, 10 s: the vignette pass (wrist orbs, quiver orbs, arrow glow, merc tint, point arc) drew %.1f%% of the pixels a full-screen "
                 "pass would (%.2f M of %.2f M a pass, %u passes, %u of them full screen - by: %s) - %.2f ms a present "
                 "([render] phantom_rects %s)", share, fs.rectPasses ? (double)fs.rectPixels / fs.rectPasses / 1e6 : 0.0,
                 fs.rectPasses ? (double)fs.rectPixelsFull / fs.rectPasses / 1e6 : 0.0, fs.rectPasses, fs.rectFull, why, fs.MsOf("PS_Phantom") + fs.MsOf("PS_OvPhantom") + fs.MsOf("PS_OvMadePhantom"),
                 g_set.phantomRects.load() ? "1: in rectangles" : "0: full screen, as before");
        }
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
// [sky] dome_gain_once (Serg 2026-10-31: "а сразу запеч все виды яркости" - every brightness baked, nothing measured while playing): the
// gain dome_true_colour measures (DomeSeen: what the game shows of the dome through its tone map and the area's grading against our
// painted sky) taken once for each area (the biome) and period of the game's day, and kept in d2r_vr_dome_gain.txt beside the ini - a
// line "<biome> <period> r g b" each. Known here: the bake takes it as numbers (BakeIn::gainFixed), the effect measures nothing (DomeGainOn
// off), and the dome is baked again only when its numbers change (the light's step, the period, a slider: domeatlas Differs) - no bake
// every 120 presents for the gain. Not known yet: measured as before until three bakes' gains in a row agree within 2 % (or the mean of eight, the camera moving it), then kept.
// The brightness and the night are no measure: the period sliders and the day's share, numbers already (g_domeBake).
namespace domegain {
struct Entry { std::string key; float rgb[3]; };
std::vector<Entry> g_table;
bool g_loaded = false;
std::atomic<bool> g_fixedNow{false};   // the dome's gain from the table this present (the effect's DomeGainOn off)
std::wstring File() {
    std::wstring p = g_iniPath;
    const size_t slash = p.find_last_of(L"\\/");
    return (slash == std::wstring::npos ? std::wstring() : p.substr(0, slash + 1)) + L"d2r_vr_dome_gain.txt";
}
void Load() {
    g_loaded = true;
    g_table.clear();
    FILE* f = _wfopen(File().c_str(), L"r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char biome[96] = {};
        int period = -1;
        Entry e{};
        if (line[0] == ';' || sscanf_s(line, "%95s %d %f %f %f", biome, (unsigned)sizeof biome, &period, &e.rgb[0], &e.rgb[1], &e.rgb[2]) != 5) continue;
        e.key = std::string(biome) + " " + std::to_string(period);
        g_table.push_back(e);
    }
    fclose(f);
    LogF("vrcam: game sky - the dome's measured colours: %zu area and period(s) known (%ls, [sky] dome_gain_once)", g_table.size(), File().c_str());
}
const Entry* Find(const std::string& key) {
    for (const Entry& e : g_table)
        if (e.key == key) return &e;
    return nullptr;
}
void Keep(const std::string& key, const float rgb[3]) {
    g_table.push_back({key, {rgb[0], rgb[1], rgb[2]}});
    const bool fresh = _waccess(File().c_str(), 0) != 0;
    if (FILE* f = _wfopen(File().c_str(), L"a")) {
        if (fresh) fprintf(f, "; [sky] dome_gain_once: the game's dome's colour gain, measured once per area (biome) and period of the day (0 - 5);"
                              " delete a line (or the file) to measure it again\n");
        fprintf(f, "%s %.5f %.5f %.5f\n", key.c_str(), rgb[0], rgb[1], rgb[2]);
        fclose(f);
    }
    LogF("vrcam: game sky - the dome's colours measured for %s: gain %.3f %.3f %.3f - kept (%ls), never measured there again",
         key.c_str(), rgb[0], rgb[1], rgb[2], File().c_str());
}
// This present's area and period ("" while the period is not known: measured as before)
std::string KeyNow() {
    int light = 0, period = -1;
    if (DayNightKRaw(&light, &period) < 0.0f || period < 0) return std::string();
    char biome[96];
    AcquireSRWLockShared(&g_biomeLock);
    memcpy(biome, g_biome, sizeof biome);
    ReleaseSRWLockShared(&g_biomeLock);
    biome[sizeof biome - 1] = 0;
    return biome[0] ? std::string(biome) + " " + std::to_string(period) : std::string();
}
}  // namespace domegain
// [sky] dome_fog_baked: the game's fog drawn under an act's sky (not underground, not the Arcane Sanctuary's own sky) - the dome's low part
// baked into its colour; the post pass's DomeHorizon stands aside meanwhile (the sky's low part is the fog's already)
bool DomeFogBaked() {
    const int band = engitem::DomeBand();
    return g_set.domeFogBaked.load() && g_set.domeFogBakedStrength.load() > 0.0f && gamefogvr::Active() && band >= 0 && band < 5 &&
           !g_underground.load() && !g_arcaneSky.load();
}
// [sky] dome_in_game_colour (vr/sky/dome_atlas.h): the game's own picture of the dome baked by the numbers the effect has for it now
// (g_domeBake, set with its uniforms), its night pictures and measured gain as the effect has them - on `l` after the effect's passes
// (ready: its build drew this present), or on a list of its own while the effect is built (the picture as last baked).
void DomeAtlas(ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12GraphicsCommandList* l, bool ready) {
    domeatlas::BakeIn in;
    // (the baked skies, the default: the act's pictures baked into the game's own one whatever dome_in_game_colour says, no gain; [sky]
    // dome_tint - parked - as before)
    const bool three = !g_set.domeTint.load();
    in.on = g_set.gameSky.load() && (g_set.domeInGameColour.load() || three);
    in.band = engitem::DomeBand();
    in.capLayout = engitem::DomeAtlasHasCap();   // (the pictures' layout: the zenith's square or not)
    in.night = g_domeBake.night;
    in.mul = g_domeBake.mul;
    in.capOn = g_domeBake.capOn;
    for (int i = 0; i < 3; ++i) in.zenith[i] = g_domeBake.zenith[i];
    // [sky] dome_fog_baked: the low part baked into the colour the game's fog draws now (scene units, as the game lays it on the land) - the
    // baked skies too (Serg 2026-11-09: "надо красить и запекать", "прямо в купол", "в текстуру": the dome's low band in the fog's colour,
    // in the texture the game samples, not a tint each frame; the game's own fog over it where it reaches). The colour follows the game's
    // time (the Fog tab's colours); a new bake only once it has moved 2 % - not each step of a dusk
    if (DomeFogBaked()) {
        static float baked[3] = {-1.0f, -1.0f, -1.0f};
        float now[3];
        gamefogvr::ColourNow(now);
        bool moved = false;
        for (int c = 0; c < 3; ++c) moved = moved || std::fabs(now[c] - baked[c]) > 0.02f * std::max(std::fabs(baked[c]), 0.01f);
        if (moved) memcpy(baked, now, sizeof baked);
        in.fog[0] = baked[0]; in.fog[1] = baked[1]; in.fog[2] = baked[2];
        in.fogBand = g_set.domeFogBakedStrength.load();
        in.fogBandTop = std::sin(g_set.domeFogBakedTopDeg.load() * 3.14159265f / 180.0f);   // ([sky] dome_fog_baked_top_deg)
    }
    if (ready && g_domeBake.slot > 0) {
        char n[24];
        snprintf(n, sizeof n, "SkyNBand%dTex", g_domeBake.slot);
        in.nightBand = g_fx->TextureResource(n);
        snprintf(n, sizeof n, "SkyNCap%dTex", g_domeBake.slot);
        in.nightCap = g_fx->TextureResource(n);
    }
    if (ready && g_set.domeTrueColour.load() && !three) in.gain = g_fx->TextureResource("DomeSeenTex");
    if (!ready) in.night = 0.0f, in.gain = nullptr;   // (no build: the effect's pictures are not there)
    // [sky] dome_gain_once: the area's and period's gain from the table when known - as numbers, nothing measured
    bool fixed = false;
    std::string key;
    if (g_set.domeGainOnce.load() && g_set.domeTrueColour.load() && in.on && !three) {   // (the baked skies: no file read or written)
        if (!domegain::g_loaded) domegain::Load();
        key = domegain::KeyNow();
        if (const domegain::Entry* e = key.empty() ? nullptr : domegain::Find(key)) {
            fixed = true;
            in.gain = nullptr;
            in.gainFixed = true;
            for (int c = 0; c < 3; ++c) in.gainRgb[c] = e->rgb[c];
        }
    } else if (!g_set.domeGainOnce.load()) {
        domegain::g_loaded = false;   // (ticked again: the file read again)
    }
    domegain::g_fixedNow.store(fixed, std::memory_order_relaxed);
    domeatlas::Present(dev, q, l, in);
    if (!key.empty() && !fixed && in.gain) {   // measuring: kept once three bakes' gains in a row agree within 2 %, or after a dozen
        static std::string measuring;
        static float last[3] = {};
        static uint32_t seenReads = 0;
        static int agree = 0;
        float rgb[3];
        uint32_t reads = 0;
        static int readsHere = 0;
        static float sum[3] = {};
        if (domeatlas::MeasuredGain(rgb, &reads) && reads != seenReads) {
            seenReads = reads;
            bool same = key == measuring;
            if (!same) { readsHere = 0; sum[0] = sum[1] = sum[2] = 0.0f; }
            for (int c = 0; c < 3 && same; ++c) same = std::fabs(rgb[c] - last[c]) <= 0.02f * std::max(std::fabs(last[c]), 0.05f);
            agree = same ? agree + 1 : 0;
            measuring = key;
            memcpy(last, rgb, sizeof last);
            ++readsHere;
            if (readsHere > 4) for (int c = 0; c < 3; ++c) sum[c] += rgb[c];   // (the first few still settling)
            // three in a row within 2 %; or, the camera moving the measure about, the mean of the reads after the first four once there are
            // eight of them (about 16 s)
            if (agree >= 2 || readsHere >= 12) {
                float keep[3];
                for (int c = 0; c < 3; ++c) keep[c] = agree >= 2 ? rgb[c] : sum[c] / (float)(readsHere - 4);
                domegain::Keep(key, keep);
                agree = 0;
                readsHere = 0;
                sum[0] = sum[1] = sum[2] = 0.0f;
            }
        }
    }
}
void Effects(ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12Resource* bb) {
    if (!g_fx || g_benchBare.load(std::memory_order_relaxed)) return;   // ([render] bench_bare: none of our effect at all)
    static bool timed = [&] {   // its passes timed on the graphics card (the 10 s line)
        UINT64 hz = 0;
        if (SUCCEEDED(q->GetTimestampFrequency(&hz))) g_fx->SetTimestampFrequency(hz);
        return true;
    }();
    (void)timed;
    ApplySkyPictures(g_fx);   // the pictures' definitions before the first build too (a change builds it again)
    if (!g_fx->Begin(dev, bb)) {
        if (!g_fxWaitSince.load(std::memory_order_relaxed)) g_fxWaitSince.store(GetTickCount64());
        ID3D12GraphicsCommandList* l = domeatlas::Ours() ? natived3d::OpenList(dev) : nullptr;
        DomeAtlas(dev, q, l, false);
        if (l) natived3d::Submit(q);
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
            SetBool(g_fx, "DomeLook", false);         // (the game's dome and fog as the game drew them: no depth to find them by)
            SetBool(g_fx, "DomeFogHorizon", false);
            SetBool(g_fx, "GameFogMatch", false);
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
    EffectWorldOff(g_fx);   // [render] effect_world unticked: the sky and fog passes left out, the overlays as before
    if (ID3D12Resource* g = domeatlas::BakedGain()) g_fx->BindSemantic("D2RVR_DOME_GAIN", g, DXGI_FORMAT_R16G16B16A16_FLOAT);
    if (ID3D12GraphicsCommandList* l = natived3d::OpenList(dev)) {
        g_fx->Render(l, bb);
        DomeAtlas(dev, q, l, true);   // (after the effect's passes: their gain measured this present)
        natived3d::Submit(q);
    }
    EffectsFinish(g_fx);
}
void Present(IDXGISwapChain3* sc, ID3D12CommandQueue* q) {
    pipehunt::OnPresent();   // [debug] patch_hunt 8 / Ctrl+F10: a pipe trace starts or ends at the game's present
    simplersun::OnPresent(); // [render] sun_shadow_*: a game frame ends (after pipe_hunt picked the frame's sun map)
    litflat::Frame();        // [render] bench_lit_flat: its copies made for the light, a few a present
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
    // [openxr] hands_over_panels: the same depth, with the camera's near plane and the world's units per metre, for the hands over the skill panel
    if (xr::NearOverlayWanted())
        xr::SetNearDepth(scenedepth::g_copiedNow ? (void*)scenedepth::g_nCopy : nullptr, (uint32_t)scenedepth::g_nSrv, s[0], s[1], g_lastNear.load(),
                         std::max(1.0f, ViewHeight()) / UserEyeHeightM());
    const double t1 = UsNow();
    Effects(dev, q, bb);   // [render] native_effects: the sky and the fog into the back buffer, before it goes to the headset
    g_effUs += UsNow() - t1;
    PresentCommon(nullptr, wnd, dev, q, bb, w, h);
    capture::OnPresent(dev, q, bb);   // the capture key: the picture as it goes out, and the presents' times
    g_cpuUs += UsNow() - t0;
    ++g_timed;
    Tell();
    bb->Release();
    dev->Release();
}
// [stereo] native_replay: the replay's hold of the left pass's present (vr/render/stereo/replay.cpp HoldLeft) - the first half of Present for the
// left eye: the scene's depth copied (the pass just bound it), the effects drawn into the back buffer. Nothing of PresentCommon: the pair's one
// present comes after the replay, with the right eye (its depth copied again there - the depth pick's frame ends only at a present, so the
// pick still counts as seen), and the per-present work runs once a pair, as the ReShade path's one present a pair did.
void HoldEye(ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12Resource* bb) {
    if (!dev || !q || !bb) return;
    const double t0 = UsNow();
    const D3D12_RESOURCE_DESC bd = bb->GetDesc();
    const uint32_t w = (uint32_t)bd.Width, h = bd.Height;
    g_fxEye = g_eye.load() & 1;
    float s[2];
    renderscale::FrameOf(w, h, s);
    scenedepth::BindNative(dev, q, w, h);
    scenedepth::g_sinceCopy = nativefxdepth::NextSince(std::min(scenedepth::g_sinceCopy, 1000000u), scenedepth::g_copiedNow);
    if (xr::NearOverlayWanted())
        xr::SetNearDepth(scenedepth::g_copiedNow ? (void*)scenedepth::g_nCopy : nullptr, (uint32_t)scenedepth::g_nSrv, s[0], s[1], g_lastNear.load(),
                         std::max(1.0f, ViewHeight()) / UserEyeHeightM());
    const double t1 = UsNow();
    Effects(dev, q, bb);
    g_effUs += UsNow() - t1;
    g_cpuUs += UsNow() - t0;
    ++g_timed;   // (a picture of its own: "ms a present" stays per eye's picture, as with two passes)
}
// (also into the GPU fault report's flight recorder: crash report 292 lost its device ~4 ms after the game's HDR Calibration "SaveAndClose",
// with a page fault where a "Swap Chain" was freed - this line says next time whether the game resized or made its swap chain anew just before)
void SwapChainChanged() {
    scenedepth::g_cache.SwapChainChanged();
    gpufault::Note("game: its swap chain resized or made anew (its old buffers freed)");
}
// [render] resize_drain: the game's swap chain is about to be resized (its old buffers still alive) - vr/resize_drain.h
void BeforeResize() { resizedrain::Event(resizedrain::Why::kResize); }
void BindDepth(ID3D12GraphicsCommandList* cl, ID3D12Resource* ds, unsigned colour) {
    scenedepth::OnBindNative(ds, colour);
    renderscale::OnBindNative(cl, ds);
}
void Barrier(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_RESOURCE_BARRIER* b) {
    scenedepth::OnBarrierNative(n, b);
    simplersun::OnBarrier(cl, n, b);   // [render] sun_shadow_every: the sun map copied out / back in where its drawing ends (vr/simpler_sun.h)
}
void Viewport(ID3D12GraphicsCommandList* cl, float w, float h) { renderscale::OnViewportNative(cl, w, h); }
// [stereo] turns_keep_own 3 / 4 without ReShade: the game's effects layer found from these hooks (effectslayer::OnBindNative) - beside
// [fog] game_fog_before_fx's own sinks for the same calls, when that is on
void (*g_fogTargets)(ID3D12GraphicsCommandList*, unsigned, const D3D12_CPU_DESCRIPTOR_HANDLE*, bool, ID3D12Resource*,
                     const D3D12_CPU_DESCRIPTOR_HANDLE*) = nullptr;
void (*g_fogBarrier)(ID3D12GraphicsCommandList*, unsigned, const D3D12_RESOURCE_BARRIER*) = nullptr;
void FxTargets(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_CPU_DESCRIPTOR_HANDLE* rts, bool single, ID3D12Resource* ds,
               const D3D12_CPU_DESCRIPTOR_HANDLE* dsv) {
    if (g_fogTargets) g_fogTargets(cl, n, rts, single, ds, dsv);
    effectslayer::OnBindNative(n, rts, single, ds);
}
void FxBarrier(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_RESOURCE_BARRIER* b) {
    if (g_fogBarrier) g_fogBarrier(cl, n, b);
    effectslayer::OnBarrierNative(n, b);
}
// the game's depth clears: [render] lens_mask's sink first (its hidden area), then [render] sun_shadows 0's (the sun's map to lit)
void (*g_lensClear)(ID3D12GraphicsCommandList*, uint64_t, ID3D12Resource*, unsigned, float, uint8_t, unsigned, const tagRECT*) = nullptr;
void ClearDsvChain(ID3D12GraphicsCommandList* cl, uint64_t dsv, ID3D12Resource* ds, unsigned flags, float depth, uint8_t stencil, unsigned n,
                   const tagRECT* rects) {
    if (g_lensClear) g_lensClear(cl, dsv, ds, flags, depth, stencil, n, rects);
    simplersun::OnClearDsv(cl, dsv, ds, flags, depth, stencil, n, rects);
}
// the game's pipelines as made: [fog] game_fog_height's fog pass and [fog] game_fog_before_fx's transparent resolve
void GraphicsPso(ID3D12Device* dev, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d, ID3D12PipelineState* pso) {
    fogheight::OnGraphicsPso(dev, d, pso);
    fogfx::OnGraphicsPso(dev, d, pso);
    refleye::OnGraphicsPso(dev, d, pso);   // [render] reflections_per_eye: the reflections' resolve
    pipehunt::OnGraphicsPso(d, pso);       // [debug] skip_pipeline / patch_hunt 8, Ctrl+F10's pipe trace: its pixel shader's hash
    litflat::OnGraphicsPso(dev, d, pso);   // [render] bench_lit_flat: the game's lit pipelines kept (vr/lit_flat.h)
}
void ComputePso(ID3D12Device* dev, const D3D12_COMPUTE_PIPELINE_STATE_DESC* d, ID3D12PipelineState* pso) {
    lightbounds::OnComputePso(dev, d, pso);
    benchlights::OnComputePso(dev, d, pso);   // [render] bench_skip_point_lights: the light bins and volumes copied with no local light
    refleye::OnComputePso(dev, d, pso);   // [render] reflections_per_eye: the reflections' projection pass
    pipehunt::OnComputePso(d, pso);
}
void StreamPso(ID3D12Device* dev, const D3D12_PIPELINE_STATE_STREAM_DESC* d, ID3D12PipelineState* pso) {
    fogheight::OnStreamPso(dev, d, pso);
    fogfx::OnStreamPso(dev, d, pso);
    lightbounds::OnStreamPso(dev, d, pso);   // [render] light_bounds_fix without ReShade: the light bins' compute pipeline, if made from a stream
    benchlights::OnStreamPso(dev, d, pso);   // [render] bench_skip_point_lights
    refleye::OnStreamPso(dev, d, pso);       // [render] reflections_per_eye: either reflection pass, if made from a stream
    pipehunt::OnStreamPso(d, pso);
    litflat::OnStreamPso(dev, d, pso);   // [render] bench_lit_flat
}
// a pipeline the game binds: [render] light_bounds_fix's light bins (compute), then [fog] game_fog_height's fog pass (graphics)
// and [render] reflections_per_eye's two reflection passes
ID3D12PipelineState* PsoSwap(ID3D12PipelineState* pso) { return refleye::Swap(fogheight::Swap(lightbounds::Swap(benchlights::Swap(litflat::Swap(pso))))); }
// At the plugin's load, MinHook initialised, before the game makes its device and swap chain.
void Install() {
    natived3d::Sinks k;
    k.present = &Present;
    k.swapChainChanged = &SwapChainChanged;
    k.beforeResize = &BeforeResize;
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
    benchlights::SetLog(&Log);   // [render] bench_skip_point_lights (vr/bench_lights.h)
    litflat::SetLog(&Log);       // [render] bench_lit_flat (vr/lit_flat.h)
    litflat::SetAreaColour(&FogColourNow);   // [render] potato_light_colour 3: the area's own (the sky's horizon / the act's fog colour)
    litflat::SetUnderground([] { return g_underground.load(std::memory_order_relaxed); });   // [render] potato_light_brightness_caves: the fog's split
    refleye::SetLog(&Log);   // [render] reflections_per_eye: the reflections' two passes, copies bound in their place (vr/reflections_eye.h)
    k.computePso = &ComputePso;
    // [debug] skip_pipeline / patch_hunt 8, [render] skip_shaders, Ctrl+F10's pipe trace: a game pass left out by its pipeline (vr/pipe_hunt.h)
    pipehunt::SetLog(&Log);
    simplersun::SetLog(&Log);   // [render] sun_shadow_radius_m / sun_shadow_every (vr/simpler_sun.h)
    k.huntBound = &pipehunt::OnBound;
    k.huntSkip = &pipehunt::Skip;
    k.huntTargets = &pipehunt::OnTargets;
    k.huntViewport = &pipehunt::OnViewport;
    k.captureBarrier = &capture::OnBarrier;   // the capture's intermediate targets (vrcapture targets)
    // [fog] game_fog_before_fx: the lists' bindings followed and the step recorded before the transparent resolve - its hooks only when the key
    // is on at the start (a list's state is followed from its first Reset); switched on later, from the next start
    fogfx::SetLog(&Log);
    if (IniB(L"fog", L"game_fog_before_fx", true) || IniB(L"fog", L"game_fog_planes", false)) {   // (on by default since 0.209.10: Serg 2026-11-09 "туман до эффектов"; the planes are drawn by it too)
        fogfx::FillSinks(k);
        Log("vrcam: fog before the effects - [fog] game_fog_before_fx on: the game's lists followed for the step before its transparent resolve");
    }
    g_fogTargets = k.fxTargets;
    g_fogBarrier = k.fxBarrier;
    k.fxTargets = &FxTargets;
    k.fxBarrier = &FxBarrier;
    effectslayer::SetLogNative(&Log);
    hud::SetLogger(&Log);
    // [render] foveated (vr/foveation.h, from D2R VR Unleashed): the game's pipelines, binds and draws followed for the rate image - its hooks
    // only when the key is on at the start; then switched on and off live
    // On a card without variable rate shading tier 2 (asked of D3D12 here, shared/vrs_probe.h - Serg 2026-11-08 on a GTX 1050 Ti: "фоверация
    // забрала fps, а не дала": the hooks followed every draw for nothing) none of it goes in: no hooks, no rate images, nothing drawn
    foveation::SetLog(&Log);
    if (IniB(L"render", L"foveated", false) || IniF(L"render", L"grass_lighting_coarse", 0.0f) > 0.0f) {
        const int vrs = vrsprobe::Ask();
        if (vrs == 0) {
            foveation::NoteNoVrs();
            Log("vrcam: foveated - this graphics card has no variable rate shading (tier 2): [render] foveated / grass_lighting_coarse OFF - no hooks, "
                "no rate images, nothing drawn");
        } else {
            foveation::FillSinks(k, &Log);
            LogF("vrcam: foveated - [render] foveated / grass_lighting_coarse on at the start: the game's pipelines, binds and draws followed (variable "
                 "rate shading, F4)%s", vrs < 0 ? " - its support not asked yet: the first present decides" : "");
        }
    }
    // [render] lens_mask (vr/lens_mask.h): the game's depth-stencil clears seen (always: off, the clear is only passed on) - switched live
    lensmask::SetLog(&Log);
    lensmask::FillSinks(k);
    // ... and [render] sun_shadows 0 (vr/simpler_sun.h) behind it: the sun's map cleared again to lit right after the game's clear
    g_lensClear = k.clearDsv;
    k.clearDsv = &ClearDsvChain;
    // ... the replay's state when a scene depth clear is recorded: the left pass captured for a replay carries both eyes' clears under the
    // replay's per-eye predicate (the DLSS twin's: written on the direct queue before the left lists and before the replay; only with the
    // camera slots' redirect, mode 1 - the native path's); captured any other way: no mask (the right eye would get the left one's)
    lensmask::SetReplay([](ID3D12Resource** pred) -> int {
        if (replay::g_capturing.load() != 1) return 0;
        ID3D12Resource* p = replay::g_predBuf;
        if (replay::g_want.load() != 1 || !camfix::g_redirect.load() || !p) return 2;
        *pred = p;
        return 1;
    });
    // [stereo] native_replay: one pass, two pictures on these hooks - the game's upload buffers followed when the key is on at the start
    replay::NativeSinks(k);
    // [render] shadows_once_a_pair (vr/shadows_once.h): what the game's lists write followed, so that the replay leaves out the lists that only
    // draw the shadow / grass maps - its hooks only with the replay's in and the key on at the start; then switched on and off live
    if (replay::NativeOn() && IniB(L"render", L"shadows_once_a_pair", false)) {
        shadowsonce::FillSinks(k, &Log);
        Log("vrcam: shadows once a pair - [render] shadows_once_a_pair on at the start: what the game's lists write is followed; the lists that only "
            "draw the shadow maps and the grass map are not replayed for the right eye (F4, one pass)");
    }
    natived3d::Install(k, &Log);
    domeatlas::SetLog(&Log);
    natived3d::SetAtlasStandIn(&domeatlas::StandIn);   // [sky] dome_in_game_colour: the game's view of the domes' picture made on ours
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
        // [render] phantom_rects (vr/phantom_rect.h, Serg 2026-11-08 "давай пофиксим отрисовку амулетов"): the vignette pass - the wrist orbs, the
        // quiver's, the arrow's glow, the merc tint - drawn only in the rectangles they can reach; full screen while a vignette, the point arc
        // or a debug draw is on. The picture is the same pixel for pixel (tools/phantom_rect_gpu_test.cpp)
        o.passRects = [](const char* entry, uint32_t w, uint32_t h, D3D12_RECT* rects, int max) -> int {
            const int eye = phantomrect::EyeOfEntry(entry);
            if (eye < 0) return -2;
            if (!g_set.phantomRects.load(std::memory_order_relaxed)) return -1;
            phantomrect::Inputs in;
            phantomrect::Eye eyes[2];
            phantomrect::Read(
                [](const char* n, float* v, size_t count) {
                    const reshade::api::effect_uniform_variable u = g_fx->find_uniform_variable("D2R_DepthFog.fx", n);
                    if (u.handle) g_fx->get_uniform_value_float(u, v, count);
                    return u.handle != 0;
                },
                [](const char* n, bool* v) {
                    const reshade::api::effect_uniform_variable u = g_fx->find_uniform_variable("D2R_DepthFog.fx", n);
                    if (u.handle) g_fx->get_uniform_value_bool(u, v, 1);
                    return u.handle != 0;
                },
                &in, eyes);
            const phantomrect::Result r = phantomrect::Compute(in, eyes[eye], (int)w, (int)h);
            if (r.full) {
                g_rectWhy[r.why >= 0 && r.why < phantomrect::kWhyCount ? r.why : 0].fetch_add(1, std::memory_order_relaxed);
                return -1;
            }
            int n = 0;
            for (int i = 0; i < r.n && n < max; ++i) rects[n++] = D3D12_RECT{r.r[i].x0, r.r[i].y0, r.r[i].x1, r.r[i].y1};
            return n;
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
    // ([fog] game_fog_before_fx without ReShade: our effect's fog laid by that step before the game's transparent resolve - the same rule)
    if (!fogframe::Wanted()) { SetBool(rt, "FogInFrame", fogfx::OursDrawn()); return; }
    fogframe::Params p;
    p.paint = g_fifPaint.load();
    const bool sky = FxBool(rt, "SkyOn");
    // (not in the Arcane Sanctuary: [sky] arcane_fog takes the fog's colour from its sky by the direction, as the sky's horizon does)
    p.on = FogWanted() && viewgate::F4() && FxBool(rt, "FogOn") && (FxBool(rt, "FogFixed") || !sky) && !ArcaneHazeFog() &&
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
    SetBool(rt, "FogInFrame", fogframe::Drawn() || fogfx::OursDrawn());
}
// Before each present's effects: everything the effect reads that changes (uniforms, bindings). RT: ReShade's runtime (its
// reshade_begin_effects, cl its list) or [render] no_reshade's own (vr/fx_native.h; cl nullptr - the depth was copied before, BindNative).
// [render] effect_world (Serg: a live "effect on / off" for an A/B, "там ведь рисуется не только небо и туман а ещё и медальоны"): unticked,
// every switch of the effect's sky and fog off for this present - our sky, fog and ceiling, the game fog's and the dome's fixes and
// measures, the distance blur, the fog in the frame: vr/fx_pass_keep.h then leaves out PS_DepthFog(R) and everything only it reads. The
// overlays and the interface passes (medallions, labels, the toolbar, the phantoms, the interface's copy) draw as before. The game's own
// sky and fog are the game's; nothing of ours on them.
template <class RT> void EffectWorldOff(RT* rt) {
    static int told = -1;
    const bool on = g_set.effectWorld.load();
    if ((int)on != told) {
        told = on;
        LogF("vrcam: effect - its sky and fog passes %s ([render] effect_world=%d); the overlays and the interface as before - the cost in the "
             "\"native effects, 10 s\" line", on ? "drawn" : "LEFT OUT", on ? 1 : 0);
    }
    if (on) return;
    for (const char* n : {"SkyOn", "ArcaneSkyOn", "FogOn", "SkyFogHorizon", "CeilOn", "GameFogMatch", "DomeFogHorizon", "DomeLook", "DomeGainOn",
                          "DomeSeenNow", "FogSeenNow", "DistBlurOn", "FogInFrame", "SpareOn"})
        SetBool(rt, n, false);
}
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
    LowMpUniforms(rt);   // [hud] low_mp_vignette
    const FogNumbers fogNow = FogNumbersNow();   // (the open air's, the caves', or the Arcane Sanctuary's own: [fog] arcane_own)
    SetFloat(rt, "FogStart", fogNow.start);
    SetFloat(rt, "FogEnd", fogNow.end);
    SetFloat(rt, "FogStrength", fogNow.strength);
    SetFloat(rt, "FogEaseIn", g_set.fogEaseIn.load() ? 1.0f : 0.0f);   // [fog] ease_in (D2R_FogEase_Unleashed.fxh)
    SetFloat(rt, "FogCurve", fogNow.curve);
    SetFloat(rt, "FogBlur", g_set.fogBlur.load());
    SetBool(rt, "FogOn", FogWanted());
    SetBool(rt, "FogLight", g_set.fogLight.load());   // [fog] light: the short way (the sky still drawn; no ceiling, no fog over glows)
    SetBool(rt, "DistBlurOn", DistanceBlurWanted());   // [render] distance_blur (world units: 4 a metre)
    SetFloat(rt, "DistBlurStart", g_set.distanceBlurStart.load());
    SetFloat(rt, "DistBlurFull", g_set.distanceBlurFull.load());
    SetFloat(rt, "DistBlurStrength", g_set.distanceBlurStrength.load());
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
        SetBool(rt, "DomeFogHorizon", g_set.domeFogHorizon.load() && engitem::DomeShown() && (FogWanted() || gamefogvr::Active()) && !noreshade::DomeFogBaked());
        // ([fog] game_fog_match removed 2026-11-08 - Serg: "она плохо работает и ломает туман". It measured the game's fog through the area's
        // grading and moved the fogged pixels toward the act's colour in our world pass, kept running for it every present. The effect's
        // GameFogTrue / FogSeen stay switched off: never a measure, never a pixel moved)
        SetBool(rt, "GameFogMatch", false);
        SetInt(rt, "FogSeenStep", -1);
        SetBool(rt, "FogSeenNow", false);
        SetBool(rt, "GameFogByDepth", false);
        // [fog] fog_into_sky: in the open air under an act's sky (ours or the game's dome - the same painted pictures), the fog fades the far
        // into the sky straight behind it, as the Arcane Sanctuary's ([sky] arcane_fog). Underground nothing but rock is behind: as before
        SetBool(rt, "FogIntoSky", g_set.fogIntoSky.load() && gamefogvr::Active() && g_skyPalette.load() >= 0 && !g_arcaneSky.load() && !g_underground.load() &&
                                 !TableFull());   // (VR F3 full width: the game's fog as it draws it)
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
    // (VR F3 with [table] full_width: the game's dome or nothing - no sky of ours, not even in its place)
    bool sky = skypaths::OurSky(pal >= 0, skypaths::GameHolds(g_set.gameSky.load(), cannot), arcane, engitem::DomeShown()) && !TableFull();
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
    SetBool(rt, "ArcaneFogIntoSky", !ArcaneOwnFog() || g_set.arcaneFogIntoSky.load());   // [fog] arcane_into_sky (off: its own colour)
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
            // [ceiling] min_headroom_m (Serg 2026-11-10): its lowest - the top less the rock's hang, CeilRelief - kept that many metres over
            // the eyes; what that needs held for the area (only ever up, in half-unit steps), as the game's slab (engitem::CeilingFrame)
            const float roomTop = height;
            static gameceiling::HeadroomHold headroom;
            const float held = gameceiling::HeadroomHeld(&headroom, g_biomeGen.load(), CeilingHeadroomNeed(sv[0].hero[1] - floorY, std::max(0.0f, relief)));
            const float top = gameceiling::Guarded(roomTop, held);
            {
                static float toldHeld = -1.0f;
                if (top > roomTop && held != toldHeld) {
                    toldHeld = held;
                    LogF("vrcam: cave ceiling - raised for headroom: its top %.1f over the area's floor, not the room's %.1f - its lowest (%.1f of "
                         "rock hanging) kept %.2f m over the eyes ([ceiling] min_headroom_m; the hero %.1f over the floor it stands on)", top, roomTop,
                         relief, g_set.ceilHeadroomM.load(), sv[0].hero[1] - floorY);
                }
            }
            SetFloat(rt, "CeilHeight", top);
            SetFloat(rt, "CeilScale", scale);
            SetFloat(rt, "CeilBrightness", bright);
            SetFloat(rt, "CeilRelief", relief);
            SetFloat(rt, "CeilTexSlot", (float)slot);
            SetFloat(rt, "CeilLightRadius", light);
            // [ceiling] fire_from_picture (2026-10-11, "any fire gives light, as at the start"): the
            // shader's own search of the picture for fire - torches, braziers, spells, burning
            // monsters. (The game's own torches found in its memory lit it too until 0.198: gone,
            // the game draws the cave ceiling and lights it itself.)
            SetBool(rt, "CeilTorches", g_set.ceilPictureFire.load());
            SetFloat(rt, "CeilTorchBright", g_set.ceilTorchBright.load());
            SetFloat(rt, "CeilTorchRadius", g_set.ceilTorchRadius.load());
            SetFloat(rt, "CeilFireNearer", g_set.ceilFireNearer.load());
            SetFloat(rt, "CeilHalo", g_set.ceilHalo.load());
            SetFloat(rt, "CeilWallDist", g_set.ceilWall.load());
            SetFloat(rt, "CeilFloorDepth", g_set.ceilFloor.load());
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
            LabelDepthUniforms(rt, srv, (tilted || replay::OverlaysWanted() || LayerOwnEye() || OwnLabelsNow()) && body, tilted ? g_set.labelsNearBody.load() * 0.005f : lu.base);
            SetFloat(rt, "LabelAlpha", lu.alpha);
            float box[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            int32_t r[4];
            const uint32_t sw = renderscale::g_screenW.load(), sh = renderscale::g_screenH.load();
            float hideUnits = 0.0f;
            const bool alliesHidden = AlliesHidden(&hideUnits);   // [hud] allies_game_hide: the game's own portraits hidden (VR F4)
            uintptr_t plateUnit = 0;
            if ((PlateWanted() || AlliesMoved() || alliesHidden || PlateTurnsWanted() || MonsterNamesOwnNow()) && sw && sh && gamestate::PlateRect(r) &&
                PlateUpNow(&plateUnit))
                PlateBoxUv(r, sw, sh, box);
            SetFloats(rt, "PlateBox", box, 4);
            platelook::PlateIn pin;   // the plate's own settings and the monster it hangs on: none of the item labels'
            pin.firstPerson = PlateOwnDepth();
            pin.depthPercent = g_set.plateNearBody.load();
            pin.alpha = g_set.plateAlpha.load();
            pin.size = g_set.plateSize.load();
            pin.farFraction = g_set.plateFar.load();
            pin.atMonster = g_set.plateDepthBody.load();
            float tm = 0.0f, tu = 0.0f;
            // (a frame with no fresh note of the distance: the same monster's last one, not "none" - none sized our name at its near size)
            if (plateworld::GetTargetOrHeld(&tm, &tu, plateUnit, GetTickCount64())) { pin.groundM = tm; pin.groundUnits = tu; }
            {   // [hud] monster_stereo_by_distance: the depth by the monster's distance from the eyes, eased from frame to frame (vr/plate_look.h DepthNow)
                static platelook::DepthEase ease;
                static LARGE_INTEGER last = {};
                LARGE_INTEGER now, freq;
                QueryPerformanceCounter(&now);
                QueryPerformanceFrequency(&freq);
                const float dt = last.QuadPart && freq.QuadPart ? static_cast<float>(static_cast<double>(now.QuadPart - last.QuadPart) / static_cast<double>(freq.QuadPart)) : 0.0f;
                last = now;
                const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();   // game units a metre
                const platelook::DepthByDistance d = platelook::Bounded(PlateDepthByDistance(), g_set.modelRadius.load() / upm);   // (no farther than the drawn world)
                pin.depthPercent = platelook::DepthNow(&ease, g_set.plateStereoByDistance.load(), g_set.plateNearBody.load(), d, pin.groundM >= 0.0f,
                                                       platelook::EyeDistanceM(pin.groundM, UserEyeHeightM()), dt);
            }
            pin.m0 = g_projSx.load(); pin.halfGap = 0.5f * g_stereoIpd.load(); pin.conv = g_stereoConv.load(); pin.eyeHeightUnits = std::max(1.0f, ViewHeight());
            pin.stereo = AfrOn() && g_enabled.load() && FullBody() && pin.m0 > 0.0f && pin.halfGap > 0.0f;   // as LabelDepthWanted
            // The target's body and the point over its head (render metres): our plate stands there ([hud] monster_names_own), and in F2 / F3 the game's
            // plate is put at its depth ([hud_third] / [hud_floor] monster_depth)
            const bool otherStereo = !pin.firstPerson && PlateOtherStereo();
            const float otherK = otherStereo ? PlateDepthOtherView() : 0.0f;
            float foot[2] = {}, head[3] = {};
            uint8_t row[3] = {};
            const float upmNow = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
            const int why = MonsterNamesOwnNow() || otherK > 0.005f ? NamesTargetBody(foot, row) : -1;
            const bool anchored = (why == ownnames::kBodyOk || why == ownnames::kBodyRowOff) &&
                                  ownnames::HeadAnchor(meleetouch::MonsterRow{row[0], row[1], row[2]}, foot[0], foot[1], g_heroPosNow[1] / upmNow, upmNow, head);
            platelook::PlateUniforms pu = platelook::Plate(pin);   // (the game's plate keeps its own sliders; our names have theirs: OwnNamesUniforms)
            // eyes by turns: the plate's half-disparity at its target's depth - each eye then shows it the same every frame (0: no target, not by turns)
            float plateTurnH = PlateTurnsWanted() && pin.groundM >= 0.0f ? platelook::TurnsHalf(pin.groundUnits, pin.eyeHeightUnits, pin.m0, pin.halfGap, pin.conv) : 0.0f;
            // VR F1 / F2 / F3 (vr/plate_look.h OtherView): the plate apart from the labels' plane while our plate is wanted there or the view's monster_depth
            // moves the game's - by the depth of the point over its head ahead of the eyes, along the view
            bool otherDepth = false;
            if (otherStereo) {
                platelook::OtherViewIn ov;
                ov.stereo = true;
                ov.ours = MonsterNamesOwnNow();
                // (ours: our name in the plate's box at its monster's depth, whatever the game plate's monster_depth says - the game's plate is not
                // drawn, its depth key is for it alone and only with ours off)
                ov.atMonster = ov.ours ? 1.0f : otherK;
                if (anchored) {
                    const float p[3] = {head[0] * upmNow, head[1] * upmNow, head[2] * upmNow};
                    const float eyes[3] = {g_headCentre[0].load(), g_headCentre[1].load(), g_headCentre[2].load()};
                    const float fwd[3] = {g_gazeFwd[0].load(), g_gazeFwd[1].load(), g_gazeFwd[2].load()};
                    ov.zUnits = platelook::AheadOf(p, eyes, fwd);
                }
                ov.m0 = pin.m0;
                ov.halfGap = pin.halfGap;
                ov.conv = pin.conv;
                ov.turns = replay::OverlaysWanted() || LayerOwnEye();
                const platelook::OtherViewOut oo = platelook::OtherView(ov);
                pu.own = oo.own;
                pu.shift = oo.shift;
                plateTurnH = oo.turnH;
                otherDepth = otherK > 0.005f && ov.zUnits > 0.0f;
                static int toldOther = -1;   // (each change: whether the game's plate stands at its monster's depth in this view)
                const int tellNow = otherK > 0.005f ? (otherDepth ? 2 : 1) : 0;
                if (tellNow != toldOther && (tellNow == 2 || toldOther == 2)) {
                    toldOther = tellNow;
                    LogF("vrcam: monster plate in F%d - %s", g_set.vrView.load(), tellNow == 2 ? "the game's plate at its monster's depth (monster_depth)" : "the game's plate where the game leaves it");
                }
            }
            SetFloat(rt, "PlateTurnH", plateTurnH);
            // [hud] monster_names_own / npc_names_own: our names, each at its own depth - the plate's at its monster's (as each item label at its item's)
            NameDepth nd;
            {
                nd.base = lu.base;
                nd.groundM = pin.groundM;
                nd.why = MonsterNamesOwnNow() ? why : -1;
                nd.foot[0] = foot[0]; nd.foot[1] = foot[1];
                nd.floorM = g_heroPosNow[1] / upmNow;
                nd.upm = upmNow;
                for (int c = 0; c < 3; ++c) { nd.row[c] = row[c]; nd.eyesM[c] = g_eyeWorld[c].load() / upmNow; }
                if (anchored && MonsterNamesOwnNow()) {
                    nd.head = true;   // (render metres -> world units from where the hands hang: the frame the effect's cameras project, as the capsules')
                    for (int c = 0; c < 3; ++c) { nd.at[c] = head[c] * upmNow - g_eyeWorld[c].load(); nd.headM[c] = head[c]; }
                }
                static int toldWhy = -2, toldWhyN = 0;   // (each change of the reason, a few lines: why the name is or is not over the head)
                int32_t plateNow[4];
                if (nd.why >= 0 && gamestate::PlateRect(plateNow) && (why != toldWhy) && toldWhyN < 6) {
                    toldWhy = why;
                    ++toldWhyN;
                    LogF("vrcam: [hud] monster_names_own - over the head: %s; the target's body %s (row %u,%u,%u, place %.0f,%.0f); %s", anchored ? "yes" : "NO",
                         ownnames::BodyWhyText(why), row[0], row[1], row[2], foot[0], foot[1],
                         anchored ? "the name stands at a point over it, each eye projecting it" : "the name drawn where the game drew it");
                }
                if (pin.stereo && pin.groundM >= 0.0f) {
                    nd.have = true;
                    nd.shift = platelook::DepthShift(pin.groundUnits, pin.eyeHeightUnits, pin.m0, pin.halfGap, pin.conv, 1.0f);
                    nd.turnH = (replay::OverlaysWanted() || LayerOwnEye()) ? platelook::TurnsHalf(pin.groundUnits, pin.eyeHeightUnits, pin.m0, pin.halfGap, pin.conv) : 0.0f;
                } else if (otherDepth) {   // F2 / F3: a name left in the plate's box stands where the game's plate is put
                    nd.have = true;
                    nd.shift = pu.shift;
                    nd.turnH = plateTurnH;
                }
            }
            // Our name over its monster's head stands at the monster's depth: the game's plate (its bar) there too, or the two slid against each other from
            // one eye to the other (Serg, 0.205.11: "in one eye the text is against the plate's left edge, in the other against its right") - its own stereo
            // keys (monster_near_body, monster_stereo_*) give way while ours is there; its size, opacity and place stay its own.
            // (ours on: our own depth only - none of the game plate's depth keys, monster_near_body / monster_depth_body / monster_stereo_*, which are
            // the old plate's and apply only with ours off; Serg 2026-11-09)
            const bool oursOn = MonsterNamesOwnNow();
            const float plateShift = nd.have && (oursOn || (nd.head && pu.own)) ? nd.shift : pu.shift;
            SetBool(rt, "PlateOwn", pu.own);   // the plate at its own depth, none of the item labels' (F4; F1 - F3 with our plate or monster_depth there)
            SetFloat(rt, "PlateShift", plateShift);
            SetFloat(rt, "PlateAlpha", pu.alpha);
            SetFloat(rt, "PlateScale", pu.scale);
            nd.plateShift = plateShift;   // (the per-eye dump: where the game's plate stands in each eye)
            NamesSet3d(nd.head);   // (the text hook: over the head, every copy is ours to stand in for; else only the one in the plate's box)
            {   // the game's plate hidden while our own plate stands over the head (D2R_DepthFog.fx PlateAt w 2; last frame's: ours is made below)
                // (every frame while our names are on - not only the frames ours is drawn: a new target's first frame, a frame the hook missed showed theirs)
                const float at[4] = {nd.at[0], nd.at[1], nd.at[2], ownnames::GamePlateHidden(MonsterNamesOwnNow(), pu.own) ? 2.0f : 0.0f};
                SetFloats(rt, "PlateAt", at, 4);
                static bool toldFx = false;
                if (nd.head && !toldFx) {
                    toldFx = true;
                    const bool hasAt = rt->find_uniform_variable("D2R_DepthFog.fx", "PlateAt").handle != 0;
                    LogF("vrcam: [hud] monster_names_own - the effect %s PlateNameAt and %s PlateAt (our name over the head, the game's plate under it)%s",
                         rt->find_uniform_variable("D2R_DepthFog.fx", "PlateNameAt").handle ? "has" : "has NO", hasAt ? "has" : "has NO",
                         hasAt ? "" : " - an older D2R_DepthFog.fx: copy the new one in with the DLL");
                }
            }
            // (our plate gets the game plate's box whatever that plate's own depth keys say: the box is where the game laid its plate out, the
            // texts ours stands in for are in it - the game's plate itself is not drawn)
            OwnNamesUniforms(rt, srv, pu.own || MonsterNamesOwnNow() ? box : kNoBox, pu.scale, nd);
            if (!MonsterNamesOwnNow()) g_namesFrameOn = false;
            float allies[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            if (alliesHidden && sw && sh) AlliesBoxUv(sw, sh, allies, hideUnits);   // (moved off the picture below: its place left empty)
            else if (AlliesMoved() && sw && sh) AlliesBoxUv(sw, sh, allies);
            SetFloats(rt, "AlliesBox", allies, 4);
            const float alliesOff[2] = {alliesHidden ? 4.0f : g_set.alliesX.load(), alliesHidden ? 0.0f : g_set.alliesY.load()};
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
        if (i == 1) {   // [hud] map_rotate_flat: the map round about the hero, turned (vr/map_turn.h RoundOnScreen); an older effect has no such uniform
            float circle[4], hero[2], m[4];
            bool whole = false;
            const bool turn = on && hud::PictureMapTurn(circle, hero, m, &whole);
            SetBool(rt, "GameMapTurnOn", turn);
            SetBool(rt, "GameMapTurnWhole", turn && whole);   // [hud] flat_map_zoom: out of the circle (an older effect: none, the circle stays)
            // [hud] map_compass_flat: the compass strip over it - its picture (xr.cpp's, painted once) uploaded into GameCompassTex once per effect load
            float cbox[4], crop = 0.0f;
            bool compass = turn && hud::PictureCompass(cbox, &crop);
            if (compass) {
                static uint64_t upVar = 0;
                static bool failed = false;
                const reshade::api::effect_texture_variable tv = rt->find_texture_variable("D2R_DepthFog.fx", "GameCompassTex");
                if (!tv.handle) {
                    static bool told = false;
                    if (!told) { told = true; Log("vrcam: flat map compass - D2R_DepthFog.fx has no GameCompassTex: an older one, copy the new one in (the DLL and the effect together)"); }
                    compass = false;
                } else if (tv.handle != upVar && !failed) {
                    const std::vector<uint32_t> px = xr::CompassPixels();
                    if (px.size() == (size_t)mapturn::kAtlasW * mapturn::kAtlasH) {
                        rt->update_texture(tv, mapturn::kAtlasW, mapturn::kAtlasH, px.data());
                        upVar = tv.handle;
                        Log("vrcam: flat map compass - its strip uploaded into D2R_DepthFog.fx (the VR compass's picture; the window moves with the way you look)");
                    } else {
                        failed = true;
                        Log("vrcam: flat map compass - its picture could not be painted: no compass");
                    }
                }
                compass = compass && tv.handle == upVar;
            }
            SetBool(rt, "GameCompassOn", compass);
            if (compass) {
                SetFloats(rt, "GameCompassBox", cbox, 4);
                SetFloat(rt, "GameCompassCrop", crop);
            }
            if (turn) {
                SetFloats(rt, "GameMapTurnCircle", circle, 4);
                SetFloats(rt, "GameMapTurnHero", hero, 2);
                SetFloats(rt, "GameMapTurnM", m, 4);
            }
        }
    }
    {   // [hud] swing_timer (vr/swing_timer_wire.h): the attack's ring in the middle (or its bar at the top) and its outcome, the monitor's F2 / F3 -
        // its picture, painted when it changes, uploaded into SwingTex; an older effect has no SwingTex and shows nothing
        static uint64_t upVar = 0;
        static uint32_t upRev = 0;
        static std::vector<uint32_t> swingPx;
        float sbox[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        uint32_t need = 0;
        const int view = g_set.flatView.load();
        const uint32_t sw = renderscale::g_screenW.load(), sh = renderscale::g_screenH.load();
        bool swing = g_set.platform.load() == 0 && (view == 2 || view == 3) && sw && sh && !gamestate::MenuOpen() &&
                     swingtimer::Monitor(GetTickCount64(), (float)sw / (float)sh, sbox, &need);
        if (swing) {
            const reshade::api::effect_texture_variable tv = rt->find_texture_variable("D2R_DepthFog.fx", "SwingTex");
            if (!tv.handle) {
                static bool told = false;
                if (!told) { told = true; Log("vrcam: swing timer - D2R_DepthFog.fx has no SwingTex: an older one, copy the new one in (the DLL and the effect together)"); }
                swing = false;
            } else {
                if (tv.handle != upVar) { upVar = tv.handle; upRev = 0; }   // (a reloaded effect: its texture is empty again)
                uint32_t rev = 0;
                if (swingtimer::Picture(&swingPx, &rev, upRev) && swingPx.size() == (size_t)swingtimer::kPicW * swingtimer::kPicH) {
                    rt->update_texture(tv, swingtimer::kPicW, swingtimer::kPicH, swingPx.data());
                    upRev = rev;
                }
                swing = upRev != 0;   // (the newest picture uploaded; one painted a frame late shows the frame after)
            }
        }
        SetBool(rt, "SwingOn", swing);
        if (swing) SetFloats(rt, "SwingBox", sbox, 4);
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
            SkyView built[2];
            for (int e = 0; e < 2; ++e) {
                const bool ownView = sv[e].axes_ok && sv[e].proj_ok;
                const SkyView& other = sv[e ^ 1];
                // an eye's own view not there: the other eye's moved across the gap, its projection mirrored (turnwarp::EyeFromOther) - the
                // other's as it is put every overlay of this eye where the other eye sees it: shown twice (0.193.2, "the texts are doubled")
                const bool fromOther = !ownView && other.axes_ok && other.proj_ok;
                if (!ownView) {
                    ++replay::g_ovTell.viewMissing[e];
                    if (!sv[e].axes_ok) ++replay::g_ovTell.axesMissing[e];
                    if (!sv[e].proj_ok) ++replay::g_ovTell.projMissing[e];
                }
                if (fromOther) {
                    built[e] = other;
                    const turnwarp::EyeView v = turnwarp::EyeFromOther({other.eyeRel[0], other.eyeRel[1], other.eyeRel[2]},
                                                                       {other.axes[0], other.axes[1], other.axes[2]}, other.proj, g_eyeHalf.load(), e ^ 1);
                    built[e].eyeRel[0] = v.at.x; built[e].eyeRel[1] = v.at.y; built[e].eyeRel[2] = v.at.z;
                    for (int i = 0; i < 4; ++i) built[e].proj[i] = v.proj[i];
                }
                const SkyView& s = ownView ? sv[e] : fromOther ? built[e] : sv[0];
                float off[3];
                for (int i = 0; i < 3; ++i) off[i] = s.eyeRel[i] - g_eyeFromLook[i].load();
                if (tv.ok) {
                    const turnwarp::Camera picture{{tv.at[e][0], tv.at[e][1], tv.at[e][2]}, {tv.turn[0], tv.turn[1], tv.turn[2], tv.turn[3]}, {}, true};
                    const turnwarp::Vec at{g_eyeWorld[0].load() + off[0], g_eyeWorld[1].load() + off[1], g_eyeWorld[2].load() + off[2]};   // (the effect: from where the hands hang)
                    const turnwarp::CameraGap g = turnwarp::GapOf(picture, at, {s.axes[0], s.axes[1], s.axes[2]}, {s.axes[3], s.axes[4], s.axes[5]},
                                                                  {s.axes[6], s.axes[7], s.axes[8]});
                    replay::g_ovTell.gapAt[e] = std::max(replay::g_ovTell.gapAt[e], g.at);
                    replay::g_ovTell.gapTurn[e] = std::max(replay::g_ovTell.gapTurn[e], g.turn);
                    if (!ownView && !fromOther) ++replay::g_ovTell.camFallback;
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
                g_namesCam[e].ok = true;   // (the names' per-eye dump: the same camera the shader projects with)
                for (int i = 0; i < 3; ++i) g_namesCam[e].off[i] = off[i];
                for (int i = 0; i < 9; ++i) g_namesCam[e].axes[i] = axes[i];
                for (int i = 0; i < 4; ++i) g_namesCam[e].proj[i] = s.proj[i];
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
            if (worldcontact::DrawWanted())   // [debug] world_contact_draw: the walls near the hands, the controllers against the hands and the weapon as drawn
                nh += worldcontact::Marks(boxesNow, eyeAt, std::max(1.0f, ViewHeight()) / UserEyeHeightM(), ha + nh, hb + nh, meleeboxes::kShaderCapsules - nh);
            if (!g_paFxHas) nh += PointCastMarks(eyeAt, ha + nh, hb + nh, meleeboxes::kShaderCapsules - nh);   // [gameplay] teleport_circle: an older effect - the arc and circle as capsules
            if (GripDrawWanted() && f.valid)   // [debug] grip_draw: the attach bones, the hands, the shaft and the second hand's take zone
                nh += GripDrawMarks(f, std::max(1.0f, ViewHeight()) / UserEyeHeightM(), ha + nh, hb + nh, meleeboxes::kShaderCapsules - nh);
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
                for (int k = 0; k < 2; ++k)   // [hud] hand_vitals_pulse_eq: the vignettes' beats, for the balls to beat in step
                    if (!lowhp::BeatNow(k == 1, &in.follow[k])) in.follow[k] = -1.0f;
                {   // [shapeshift] a druid's werewolf / werebear as your body (VR F4): its own depth offset on the wrists, and with hand_vitals_orb_form_auto how much farther its
                    // forearm's skin stands out than the druid's - each from the forearm bone's mesh box as drawn (the druid's kept from her last measure, per wrist)
                    in.form = viewgate::F4() ? skel::GraftedForm() : 0;
                    static float personOutM[2] = {};
                    static int toldForm = -1;
                    static float toldAuto[2] = {-1.0f, -1.0f};
                    static float toldZ = 0.0f;
                    static bool toldOn = false;
                    bool measured[2] = {};
                    for (int side = 0; side < 2; ++side) {
                        float out = 0.0f;
                        measured[side] = handorbs::SkinOutM(in.arm[side], &out);
                        if (in.form == 0 && measured[side]) personOutM[side] = out;
                        else if (in.form == 2 && in.cfg.orbFormAuto && measured[side]) in.formAutoCm[side] = handorbs::FormLiftCm(out, personOutM[side]);   // (the werebear only)
                    }
                    const bool autoMoved = in.form == 2 && (fabsf(in.formAutoCm[0] - toldAuto[0]) > 0.5f || fabsf(in.formAutoCm[1] - toldAuto[1]) > 0.5f ||
                                                            fabsf(handvitals::FormZCm(in.cfg, 2) - toldZ) > 0.05f || in.cfg.orbFormAuto != toldOn);   // (the slider, the tick moved live)
                    if (in.form != toldForm || autoMoved) {   // once per change: the form, its offset, the auto lift and what it was measured from
                        toldForm = in.form;
                        toldAuto[0] = in.formAutoCm[0]; toldAuto[1] = in.formAutoCm[1];
                        toldZ = handvitals::FormZCm(in.cfg, 2);
                        toldOn = in.cfg.orbFormAuto;
                        if (in.form == 1) Log("vrcam: hand vitals - the werewolf is your body: the wrist medallions at the druid's place");
                        else if (in.form == 2)
                            LogF("vrcam: hand vitals - the %s is your body: the wrist medallions' depth offset for it %+.1f cm ([hud] hand_vitals_orb_z_cm_%s), auto lift %s right %.1f cm, "
                                 "left %.1f cm (the forearm's skin out of the back of the wrist: druid %.1f / %.1f cm, %s; %s)",
                                 handvitals::FormKey(in.form), handvitals::FormZCm(in.cfg, in.form), handvitals::FormKey(in.form), in.cfg.orbFormAuto ? "on" : "off ([hud] hand_vitals_orb_form_auto=0):",
                                 in.formAutoCm[0], in.formAutoCm[1], personOutM[0] * 100.0f, personOutM[1] * 100.0f,
                                 measured[0] || measured[1] ? "the form's measured" : "the form's forearm box not read",
                                 in.cfg.orbOnBone ? "on the bone the ball lies on the form's own skin, the offset on top" : "on the spot's numbers");
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
                // the Gestures' Select mode: a skill a glyph armed on the right trigger ([gestures] select_icon) - after the bow's, before the melee armed one
                const int glyphSkill = skilllinkwire::SelectIcon() ? skilllinkwire::ArmedSkill(1) : -1;
                const armedicon::Source src = bowSkill < 0 && glyphSkill >= 0 ? armedicon::Source{glyphSkill, true} : armedicon::Pick(meleeSkill, meleeReady, bowSkill, bowReady);
                ii.on = ii.armedOn = (meleearm::IconOn() && meleearm::On()) || bowSkill >= 0 || glyphSkill >= 0;
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
                {   // ... and the LIFE medallion: the skill a glyph armed on the left trigger, the same way (OrbIconL / OrbIconLTex)
                    static armedicon::Fader fadeL;
                    static bool wasL = false;
                    static int upSkillL = -1, tryL = -1;
                    static uint32_t upRevL = 0;
                    static uint64_t upVarL = 0;
                    armedicon::In il = ii;
                    const int leftSkill = skilllinkwire::SelectIcon() ? skilllinkwire::ArmedSkill(0) : -1;
                    il.on = il.armedOn = leftSkill >= 0;
                    il.armed = leftSkill;
                    il.enough = true;
                    armedicon::Out ol = fadeL.Step(il, dt);
                    if (ol.show) {
                        const reshade::api::effect_texture_variable tl = rt->find_texture_variable("D2R_DepthFog.fx", "OrbIconLTex");
                        const uint32_t rev = wheelicons::Revision();
                        if (!tl.handle) {
                            static bool told = false;
                            if (!told) { told = true; Log("vrcam: gestures - D2R_DepthFog.fx has no OrbIconLTex: an older effect (the life medallion shows no armed icon)"); }
                            ol.show = false;
                        } else if ((ol.skill != upSkillL || tl.handle != upVarL) && (ol.skill != tryL || rev != upRevL || tl.handle != upVarL)) {
                            upSkillL = -1;
                            tryL = ol.skill;
                            upRevL = rev;
                            upVarL = tl.handle;
                            const std::shared_ptr<const wheelicons::Image> img = wheelicons::Get(ol.skill);
                            static std::vector<uint8_t> bakedL;
                            if (img && armedicon::Bake(img->rgba.data(), img->width, img->height, bakedL)) {
                                rt->update_texture(tl, armedicon::kTex, armedicon::kTex, bakedL.data());
                                upSkillL = ol.skill;
                                LogF("vrcam: skill icon - skill %d (armed on the left trigger by a glyph) is in the life medallion", ol.skill);
                            }
                        }
                        if (ol.skill != upSkillL) ol.show = false;
                    }
                    float ul[4];
                    armedicon::Uniform(ol, ul, place);
                    if (ol.show || wasL) SetFloats(rt, "OrbIconL", ul, 4);
                    wasL = ol.show;
                }
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
            // [debug] point_pickup_ray: each hand's pointing ray as the pointed pick-up really uses it (vr/input/flat_click.cpp PointPickupAnswer) - from
            // its own origin (a small white ball: the drawn wrist, or the controller's aim pose with no arm posed) along its own direction to the hero's
            // floor (a full ball there) or 8 m out; the right hand green, the left cyan, the hand whose pick is lit bright, the other dim, a hand whose
            // arm is not held out (point_pickup_arm_cm) dimmer still; a yellow ring round the lit item.
            PointRayDebug pr;
            if (PointRayWanted() && f.valid && g_eyeOk.load() && PointRayDebugNow(&pr)) {
                const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
                auto head = [&](const float* w, float* h) {   // a world point -> the head's turn-only frame, from the eye, metres
                    float v[3];
                    for (int k = 0; k < 3; ++k) v[k] = (w[k] - g_eyeWorld[k].load()) / upm;
                    h[0] = v[0] * f.camR[0] + v[2] * f.camR[2];
                    h[1] = v[1];
                    h[2] = -(v[0] * f.camF[0] + v[2] * f.camF[2]);
                };
                static const float kHandRgb[2][3] = {{0.2f, 1.0f, 0.3f}, {0.2f, 0.85f, 1.0f}};   // right green, left cyan
                for (int h = 0; h < 2; ++h) {
                    const PointRayDebug::Hand& ph = pr.hand[h];
                    if (!ph.valid || vs.nseg >= bowtouch::kMaxSegs || vs.nball + 2 > bowtouch::kMaxBalls) continue;
                    float end[3];
                    bool floor = false;
                    float t = 8.0f * upm;
                    if (ph.d[1] < -1e-4f && ph.o[1] > pr.floorY) { const float tf = (pr.floorY - ph.o[1]) / ph.d[1]; if (tf < t) { t = tf; floor = true; } }
                    for (int k = 0; k < 3; ++k) end[k] = ph.o[k] + ph.d[k] * t;
                    float A[3], B[3];
                    head(ph.o, A); head(end, B);
                    const float k = pr.owner == h ? 1.0f : ph.pointing ? 0.45f : 0.2f;
                    bowtouch::Seg& ray = vs.seg[vs.nseg++];
                    ray.a = {A[0], A[1], A[2]}; ray.b = {B[0], B[1], B[2]};
                    for (int c = 0; c < 3; ++c) ray.rgb[c] = kHandRgb[h][c] * k;
                    bowtouch::Ball& from = vs.ball[vs.nball++];
                    from.c = ray.a; from.r = 0.015f; from.rgb[0] = from.rgb[1] = from.rgb[2] = pr.owner == h ? 1.0f : 0.5f; from.fill = 1.0f;
                    bowtouch::Ball& hit = vs.ball[vs.nball++];
                    hit.c = ray.b; hit.r = pr.owner == h ? 0.05f : 0.035f; hit.fill = floor ? 1.0f : 0.0f;
                    for (int c = 0; c < 3; ++c) hit.rgb[c] = kHandRgb[h][c] * k;
                    on = true;
                }
                if (pr.item && vs.nball < bowtouch::kMaxBalls) {
                    float I[3];
                    head(pr.itemAt, I);
                    bowtouch::Ball& ring = vs.ball[vs.nball++];
                    ring.c = {I[0], I[1], I[2]}; ring.r = 0.15f; ring.fill = 0.0f;
                    ring.rgb[0] = 1.0f; ring.rgb[1] = 0.9f; ring.rgb[2] = 0.1f;
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
    // [sky] dome_true_colour / day_night on the game's dome (D2R_DepthFog.fx DomeLookOf): its colours as our sky's, by day and night as ours
    // ... the tinting only: [sky] dome_tint, parked ("мы вернемся позже к покраске когда научимся красить без контуров вокруг деревьев" - open
    // until the dome can be tinted with no outline round the trees). The default: the dome is the act's baked pictures (vr/sky/three_skies.h);
    // the effect neither measures nor recolours it
    const bool tint = g_set.domeTint.load();
    const bool three = !tint && engitem::DomeShown() && !arcane && pal >= 0;
    if (!three) SetSkyFog(false, nullptr);
    const bool domeLook = tint && engitem::DomeShown() && !arcane && pal >= 0 && (g_set.domeTrueColour.load() || g_set.skyDayNight.load());
    SetBool(rt, "DomeLook", domeLook);
    if (three) {
        SetBool(rt, "DomeGainOn", false);
        SetBool(rt, "DomeSeenNow", false);   // (the measure's passes not drawn: vr/fx_pass_keep.h)
        SetInt(rt, "DomeSeenStep", -1);
    }
    // [sky] dome_in_game_colour: the game draws the dome from our baked picture - nothing recoloured here (vr/sky/dome_atlas.h)
    SetBool(rt, "DomeInGame", domeLook && domeatlas::Live());
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
    if (!sky && !domeLook && !three && !(g_set.fogIntoSky.load() && gamefogvr::Active())) return;
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
    // [sky] bright_<period> (Serg's own sliders): the sky's brightness each period of the game's day, eased by its light - our old sky and
    // the game's dome alike; 1 while the time of day is not known or not followed
    float periodMul = 1.0f;
    {
        int light = 0, period = 0;
        static periodbright::State periodState;
        if (DayNightKRaw(&light, &period) >= 0.0f) {
            float b[periodbright::kPeriods];
            for (int i = 0; i < periodbright::kPeriods; ++i) b[i] = g_set.periodBright[i].load();
            periodMul = periodbright::Mul(periodState, period, (float)light, b);
        }
    }
    if (three) {   // the baked skies: the picture by the game's time of day (whatever [sky] day_night says), each at its own brightness
        // ... its LIGHTING's time of day (cavelight::VisTime, vr/sky/three_skies.h VisShares), what the HD world is drawn with - not the
        // world clock (env::Now), which only the classic picture and the rules follow: by the clock the night picture stood over a world
        // the game lit as day (Serg on 0.208.2, act 2 town, "end of dusk, night" at the light 128: "в игре день а небо темное")
        int light = 0, period = -1;   // (the clock: for the log)
        env::Now(&light, &period);
        int visPeriod = -1;
        float visFrac = 0.0f;
        int visEvent = -1;
        const bool visKnown = cavelight::VisTime(&visPeriod, &visFrac, &visEvent);
        const bool hell = p.act == threeskies::kHellAct;   // (act 4: one sky, "там ночи нет" - the time of day does nothing)
        // Nothing coloured (Serg 2026-11-08: "у нас же должно быть два купола дневной и ночной мы не должны его красить", "мы же
        // договорились не красить ничего"): the dome is ONE picture as it is - the day's, or the night's where the act has one - never
        // two blended ([sky] sky_crossfade not on the dome), never times a brightness ([sky] three_bright_* not on the dome). No dawn /
        // dusk picture exists: the day's stands for it as it is. An act with no night picture: the day's as it is.
        threeskies::Shares sh;   // (act 4, or the lighting's time not read: the day's - the game starts its lighting at day)
        if (!hell) sh = threeskies::VisShares(visKnown ? visPeriod : -1, visFrac, false);
        // the eclipse (the Tainted Sun: the lighting's event 2 - Act 2's open air under its event file, the sun gone, whatever the time): the
        // night picture as it is (Serg on 0.209.2, act 2 town at the light 32: "у меня тут ночь а не день")
        const bool eclipse = visKnown && visEvent == 2 && !hell;
        if (eclipse) sh = threeskies::Whole(threeskies::kNight);
        float b[threeskies::kPics];
        for (int i = 0; i < threeskies::kPics; ++i) b[i] = g_set.threeBright[hell ? 3 : i].load();
        const float asIs[threeskies::kPics] = {1.0f, 1.0f, 1.0f};
        const threeskies::Bake bake = threeskies::BakeOf(sh, asIs, !hell && act.nightOk && act.bandOk);   // (night 0 or 1, x1)
        periodMul = threeskies::Brightness(sh, b);   // (our own sky and the effect's fog as before - not the dome: frozen)
        g_domeBake.night = bake.night;
        g_domeBake.mul = bake.mul;
        g_domeBake.capOn = act.capOk;
        g_domeBake.slot = act.bandOk ? p.act : 0;
        for (int i = 0; i < 3; ++i) g_domeBake.zenith[i] = p.zenith[i];
        // the fog's colour: the dome's own at the horizon, by the same numbers ([fog] fog_colour_measured off: FogColourNow)
        float horizon[3], dayHorizon[3];
        threeskies::HorizonScene(bake, act.dayHorizon, act.nightHorizon, horizon);
        // ([fog] fog_keeps_colour: the day picture's at its own slider - act 4's slider there - at every hour)
        threeskies::HorizonScene(threeskies::BakeOf(threeskies::Whole(threeskies::kDay), b, false), act.dayHorizon, act.nightHorizon, dayHorizon);
        SetSkyFog(act.bandOk && act.dayHorizonOk && (bake.night <= 0.0f || act.nightHorizonOk), horizon, dayHorizon);
        static int toldPeriod = -2, toldVis = -3, toldAct = -1;
        static float toldMul = -1.0f, toldNight = -1.0f;
        const int visSeen = visKnown ? (eclipse ? 100 : visPeriod) : -1;
        if (period != toldPeriod || visSeen != toldVis || p.act != toldAct || std::fabs(bake.mul - toldMul) > 0.002f ||
            std::fabs(bake.night - toldNight) > 0.002f) {
            toldPeriod = period;
            toldVis = visSeen;
            toldAct = p.act;
            toldMul = bake.mul;
            toldNight = bake.night;
            static const char* const kNames[6] = {"end of night, dawn", "end of dawn, day", "day", "end of day, dusk", "end of dusk, night", "night"};
            static const char* const kVis[6] = {"dawn1", "dawn2", "day", "dusk1", "dusk2", "night"};
            char lit[128];
            if (eclipse) snprintf(lit, sizeof lit, "the game's lighting under the eclipse (its event 2 - the Tainted Sun): the night picture");
            else if (visKnown) snprintf(lit, sizeof lit, "the game's lighting at %s, %.2f into it", kVis[visPeriod], visFrac);
            else snprintf(lit, sizeof lit, "the game's lighting's time of day not read - the day's");
            LogF("vrcam: game sky - the baked skies: act %d, %s (the world clock: %s, the game's light %d - the classic picture's, not what "
                     "the HD world is lit by) - day %.2f, night %.2f, dawn / dusk %.2f (the day's "
                     "picture until the act has one of its own)%s; baked as night picture %.2f x%.2f; the fog %s %.4f %.4f %.4f (scene), the day "
                     "sky's %.4f %.4f %.4f%s; nothing measured or recoloured", p.act,
                     hell ? "one sky (no night in act 4)" : lit, period >= 0 && period < 6 ? kNames[period] : "not known",
                     light, sh.pic[threeskies::kDay], sh.pic[threeskies::kNight], sh.pic[threeskies::kDawnDusk],
                     ", one picture as it is - nothing coloured", bake.night, bake.mul,
                     g_set.fogMeasured.load() ? "measured ([fog] fog_colour_measured) - the sky's would be" : "the sky's at the horizon", horizon[0],
                     horizon[1], horizon[2], dayHorizon[0], dayHorizon[1], dayHorizon[2],
                     g_set.fogKeepsColour.load() ? " (taken while the game draws the fog: [fog] fog_keeps_colour)" : "");
        }
    }
    g_periodMul.store(periodMul, std::memory_order_relaxed);   // (the open-air fog by the same: FogColourNow)
    SetFloat(rt, "SkyBrightness", (act.nightOk && act.bandOk ? act.bright : act.bright * DayNightMul()) * periodMul);
    if (domeLook) {   // the dome by our sky's numbers: its colours ([sky] dome_true_colour), its night as ours ([sky] day_night - off, or not known: day)
        int light = 0, period = 0;
        const float k = DayNightKRaw(&light, &period);
        const bool night = g_set.skyDayNight.load() && k >= 0.0f;
        SetBool(rt, "DomeGainOn", g_set.domeTrueColour.load() && !noreshade::domegain::g_fixedNow.load(std::memory_order_relaxed));   // ([sky] dome_gain_once: known - no measure)
        {   // the measure: every 10 s, a row of 10 points a present over 5 presents (D2R_DepthFog.fx DomeSeenStep; Serg: "мерить раз в 10
            // секунд ... по 10 точек брать с кадра" - one pixel walking the points each present held a small card's effect, PC2 2026-10-31)
            // ... on a new area or a new period of the game's day, then 2 s and 5 s on (the dome may not be in view at first: a measure
            // with too little of it keeps the last), else once in 10 minutes (Serg: "сутки в игре меняются не раз в 10 секунд", "а раз в 10
            // минут" - the grading is the area's and the period's; the light's steps the bake follows by numbers). The measures after a new
            // area or period take the new gain whole (DomeSeenFresh: the last one was another place's), the 10-minute ones ease half way
            static ULONGLONG startedAt = 0, freshAt = 0;
            static int step = -1, freshLeft = 0;
            static uint32_t areaSeen = ~0u;
            static int periodSeen = -2;
            const ULONGLONG now = GetTickCount64();
            int dayLight = 0, period = -1;
            DayNightKRaw(&dayLight, &period);
            const uint32_t area = g_biomeGen.load(std::memory_order_relaxed);
            if (area != areaSeen || period != periodSeen) {
                areaSeen = area;
                periodSeen = period;
                freshLeft = 3;
                freshAt = now;
            }
            static bool fresh = false;
            if (step < 0) {
                static const ULONGLONG kAfter[3] = {0, 2000, 5000};
                if (freshLeft > 0 && now - freshAt >= kAfter[3 - freshLeft]) {
                    step = 0;
                    fresh = true;
                    --freshLeft;
                    startedAt = now;
                } else if (freshLeft == 0 && (!startedAt || now - startedAt >= 600000)) {
                    step = 0;
                    fresh = false;
                    startedAt = now;
                }
            }
            SetInt(rt, "DomeSeenStep", step);
            SetBool(rt, "DomeSeenFresh", fresh);
            SetBool(rt, "DomeSeenNow", step >= 0);   // (else the measure's passes are not drawn at all: vr/fx_pass_keep.h)
            if (step >= 0) step = step + 1 < 5 ? step + 1 : -1;
        }
        SetFloat(rt, "DomeNightMix", night && act.nightOk && act.bandOk ? 1.0f - k : 0.0f);
        SetFloat(rt, "DomeDayMul", (night && !(act.nightOk && act.bandOk) ? DayNightMul() : 1.0f) * periodMul);
        SetFloat(rt, "DomeBright", act.bright);
        // (the same for the bake of the game's own picture: noreshade::DomeAtlas)
        g_domeBake.night = night && act.nightOk && act.bandOk ? 1.0f - k : 0.0f;
        g_domeBake.mul = (night && !(act.nightOk && act.bandOk) ? DayNightMul() : 1.0f) * periodMul;
        g_domeBake.capOn = act.capOk;
        g_domeBake.slot = act.bandOk ? p.act : 0;
        for (int i = 0; i < 3; ++i) g_domeBake.zenith[i] = p.zenith[i];
        // (each period of the game's day once: its light and the share of night it gives the dome and our old sky alike - Serg on 0.192.8,
        // 93 - 96.webp: "его цвет не соответствует времени суток")
        static int toldPeriod = -1;
        if (night && period != toldPeriod) {
            toldPeriod = period;
            static const char* const kNames[6] = {"end of night, dawn", "end of dawn, day", "day", "end of day, dusk", "end of dusk, night", "night"};
            LogF("vrcam: game sky - %s: the game's light %d -> day share %.2f ([sky] light_night %.0f .. light_day %.0f); the dome: night picture "
                 "%.2f, dimmed x%.2f; our old sky: night picture %.2f, x%.2f; both x%.2f by [sky] bright_<period> (this one's slider %.2f)",
                 period >= 0 && period < 6 ? kNames[period] : "?", light, k, g_set.lightNight.load(), g_set.lightDay.load(),
                 act.nightOk && act.bandOk ? 1.0f - k : 0.0f, act.nightOk && act.bandOk ? 1.0f : DayNightMul(),
                 act.nightOk && act.bandOk ? 1.0f - DayNightK() : 0.0f, act.nightOk && act.bandOk ? 1.0f : DayNightMul(), periodMul,
                 period >= 0 && period < 6 ? g_set.periodBright[period].load() : 1.0f);
        }
    }
}

void OnBeginEffects(reshade::api::effect_runtime* rt, reshade::api::command_list* cl, reshade::api::resource_view, reshade::api::resource_view) {
    if (replay::t_overlay) return;   // our own overlay render: not a present's effects
    EffectsBegin(rt, cl);
    EffectWorldOff(rt);   // [render] effect_world (ReShade's path: the same switches)
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
    if (refleye::Wanted()) refleye::Register(&Log);           // (the same)
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
