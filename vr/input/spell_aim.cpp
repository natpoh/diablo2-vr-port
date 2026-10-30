// vr/input/spell_aim.cpp - cut from vr/vrcam.cpp (docs/plan_modules.md M10); what it gives: vr/input/input.h. It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// spell_aim (above HookAttackPoint): the resolver, its two target lookups, and the skill the
// controller casts - 0x1440D0(handler, client index) reads it as the resolver itself does.
constexpr uint64_t RVA_SPELL_TARGET_RET = 0x19057F, RVA_SPELL_HOVER_RET = 0x190588, RVA_SPELL_POINT_RET = 0x1906E7;
// A press casts by the map click (0x14B1A9 and 0x14C21C: the resolver, then MapClick(unit, 7..10, x, y, flags) with
// its point) - and the click asks the controller's target once more, 0x18DD60(handler, index) (at 0xFE4C4), which
// jumps on to the same two lookups: they return into the click at 0xFE4C9. A monster there and the cast went to it
// (2026-10-13: near monsters the bolts flew at them).
constexpr uint64_t RVA_CLICK_TARGET = 0x18DD60, RVA_CLICK_TARGET_RET = 0xFE4C9;
// ... and so does the function that makes the cast's flags, 0x1454A0(handler, index, word*), called by the controller's press (0x1450BF) and its hold (0x14C3CA)
// just before the cast: in controller mode it sets the flags' Stand Still byte to 1 for an ordinary skill, then asks the controller's target the same way
// (0x18DD60, returning at 0x145534) and, with one that the skill's table row and the reach test (0x190D40) accept, clears the byte - a cast AT that target, which
// the click may walk the hero up to. docs/shot_run_recon.md.
constexpr uint64_t RVA_DISPATCH_TARGET_RET = 0x145534;
// mov [rsp+8], rbx; mov [rsp+10h], rsi; push rdi; sub rsp, 20h
const uint8_t kSigClickTarget[15] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20};
// mov rax, rsp; mov [rax+18h], r8b; mov [rax+10h], rdx; push rbp/rbx/rsi/rdi
const uint8_t kSigSpellResolve[15] = {0x48,0x8B,0xC4,0x44,0x88,0x40,0x18,0x48,0x89,0x50,0x10,0x55,0x53,0x56,0x57};
// mov rax, rsp; mov [rax+10h], edx; push rbp/rbx/rsi/rdi; lea rbp, [rax-48h]
const uint8_t kSigSpellTarget[14] = {0x48,0x8B,0xC4,0x89,0x50,0x10,0x55,0x53,0x56,0x57,0x48,0x8D,0x68,0xB8};
// push rbx; sub rsp, 20h; mov rax, gs:[58h] (a static's guard)
const uint8_t kSigCtrlHandler[15] = {0x40,0x53,0x48,0x83,0xEC,0x20,0x65,0x48,0x8B,0x04,0x25,0x58,0x00,0x00,0x00};
// push rbx; sub rsp, 20h; mov eax, edx; mov rbx, rcx; mov [rsp+30h], rax
const uint8_t kSigCtrlSkill[16] = {0x40,0x53,0x48,0x83,0xEC,0x20,0x8B,0xC2,0x48,0x8B,0xD9,0x48,0x89,0x44,0x24,0x30};
using SpellResolveFn = void (*)(void* mgr, void* unit, bool assist, float* x, float* y);
using SpellTargetFn = void* (*)(void* mgr, int skill);
using CtrlHandlerFn = void* (*)();
using CtrlSkillFn = int (*)(void* handler, int index);
SpellResolveFn OrigSpellResolve = nullptr;
SpellTargetFn OrigSpellTarget = nullptr;
HoverUnitFn OrigSpellHover = nullptr;
uintptr_t g_spellTargetRet = 0, g_spellHoverRet = 0, g_clickTargetRet = 0, g_dispatchTargetRet = 0;
// A directed resolution's cast, waiting for its click's one ask of the target (this thread, 100 ms at most).
struct SpellClick { bool directed = false; ULONGLONG at = 0; };
thread_local SpellClick g_spellClick;
std::atomic<int> g_spellAimHook{0};   // 0 not in yet, 1 in, 2 not possible

// The skill the controller casts for this hero, the game's own answer; -1 none.
int SpellSkill(void* unit) {
    const int idx = ((ClientIndexFn)d2rsig::Addr(RVA_CLIENT_INDEX))(unit);
    if (idx < 0 || idx >= 8) return -1;
    void* handler = ((CtrlHandlerFn)d2rsig::Addr(RVA_CTRL_HANDLER))();
    return handler ? ((CtrlSkillFn)d2rsig::Addr(RVA_CTRL_SKILL))(handler, idx) : -1;
}

// [hands] bow_aim: with a bow or a crossbow in hand, its basic attack (Attack 0) and its arrow skills go along the
// hand the same way - the Skills table does not call a basic attack one that flies (the arrow is the weapon's), so
// spell_aim left the bow's shot to the game's target and aim assist (the user, 2026-10-13). [hands] throw_aim: the
// same for javelins and throwing weapons (Throw 2), a tick box of its own - in VR a throw is hard to put on a target,
// the game's auto-target may be wanted there (the user); with one of those in hand it alone decides, spell_aim too.
// The fork (d2r-pcvr) rewrites the target in the server's Shoot (0x4333F0) instead; here the game's own attack keeps
// its animation, rate and ammo - the game's rules are not to be broken (the user) - only its target lookups are off.
bool BowInHand() { const uint32_t w = gamestate::WeaponClass(); return w == D2RVR_WEAPON_BOW || w == D2RVR_WEAPON_CROSSBOW; }
bool ThrownInHand() { const uint32_t t = gamestate::WeaponType(); return t == D2RVR_TYPE_JAVELIN || t == D2RVR_TYPE_THROWING; }
// The boxes that make a cast of the pad's skills go along the hand at all (the resolver's and the cast flags' look at the target both ask).
bool SpellAimWanted(bool drawn) {
    return g_set.spellAim.load() || g_set.bowAim.load() || g_set.throwAim.load() || g_set.throwGesture.load() || drawn;
}
// A cast of `skill` by the hero: does it go along the hand (directed: the game's target and aim assist off), and is its point ours too (point)?
SpellCast SpellCastFor(int skill, bool drawn) {
    SpellCast cast;
    const bool flies = gamestate::SkillAimed(skill);
    // a released draw's own shot - the attack or a bow skill - goes along the draw line, the game's target and aim assist off
    // ([autotarget] does not keep them: the line is the player's aim); any other skill pressed then is as the other boxes say
    const bool shot = drawn && bowdraw::DrawsWith(skill);
    bool along;
    if (ThrownInHand()) along = cast.point = (g_set.throwAim.load() || GestureAimLive()) && (skill == 0 || skill == 2 || skill == 140 || flies);   // (140: Double Throw, from the quiver)
    else if (BowInHand()) along = cast.point = shot || (g_set.bowAim.load() && (skill == 0 || flies)) || (g_set.spellAim.load() && flies);
    else along = g_set.spellAim.load() && flies;   // a spell: the old aim, only no monster
    if (along && !shot && skill < 1024 && ((g_set.autoKeep[skill >> 6].load() >> (skill & 63)) & 1)) along = false;   // [autotarget]: the game's
    if (along && HandGridDir(&cast.gx, &cast.gy)) cast.directed = true;
    return cast;
}

// ---- [gameplay] leap_by_gaze (vr/leap_gaze.h) -------------------------------------------------------------------------------------------------
// The Barbarian's Leap and Leap Attack go where the head looks, VR F4. The cast is the game's own - its key or slot, its range, cooldown, mana and arc;
// ours is only the answer to the game's own questions about it: its target lookups (the cast flags' look, the resolver's two, the map click's) and the
// point its resolver asks of AttackPoint. One decision a cast, made at its first question and kept for the others (300 ms at most, until the click has
// asked): the gaze from the eyes - its heading on the grid, how far out it meets the hero's ground - and, for Leap Attack, the hostile monster whose body
// the gaze goes through (the monsters the throw's auto-aim reads, with the server's own hostility and wall verdicts). A monster: the lookups answer it,
// and the game's Leap Attack leaps AT it as a click on it would. None, or Leap: the lookups answer none, the aim assist is off, and the resolver's point
// is the gaze's - held to the skill's range (the distance the game's own pad cast asks of its point); a gaze that meets no ground within it is the full
// range along its heading. No server call. Unticked, F1, F2, F3, the flat game, a skill pressed by a touch or the shield: the game's aim, as before.
// The same answers could serve the other movement skills (Charge, Whirlwind): GazeSkill says which skills.
// [gameplay] teleport_circle (vr/point_cast_wire.h) shares them for the skills cast onto a point (Teleport, Fire Wall, Meteor ... the ini's list): their cast is
// the same game's own cast with the same answers, the point the gaze's (a tap, or a cast made any other way) or the arc's circle (a hold let go a moment
// ago: pointwire::CastPoint); with [gameplay] leap_arc, Leap and Leap Attack too (Leap Attack: the monster under the circle).
struct LeapAim {
    bool ok = false, done = false;
    bool fixed = false;               // [gameplay] teleport_circle: the point is `cells` out along the heading (the arc's circle), not the gaze's ground
    float cells = 0.0f;
    int skill = -1;
    ULONGLONG at = 0;
    float gx = 0.0f, gy = 0.0f;       // the gaze's heading on the grid
    float eyeH = 0.0f, down = 0.0f;   // the eyes over the hero's ground (cells), the gaze's drop a cell across (+ down)
    uint32_t monster = 0;             // Leap Attack: the monster the gaze is on (0: none)
    float monsterCells = 0.0f;
    size_t seen = 0;                  // ... the hostile monsters read round the hero
    bool polled = false;
};
SRWLOCK g_leapLock = SRWLOCK_INIT;
LeapAim g_leapAim;   // the cast's decision (the game's thread asks; the lock for the log's reads)
std::atomic<float> g_leapRange[3] = {};   // the skill's range as the game's last pad cast of it asked its point (cells; 0: not seen yet) - Leap, Leap Attack, Teleport
int RangeIndex(int skill) { return skill == leapgaze::kLeap ? 0 : skill == leapgaze::kLeapAttack ? 1 : 2; }

bool LeapGazeOn() {
    return (g_set.leapByGaze.load() || g_set.teleportCircle.load()) && viewgate::F4() && g_enabled.load() && !gamestate::MenuOpen() && !shieldsmite::Casting() &&
           !meleetouch::TargetPending();
}
// A skill whose cast goes where the head looks: Leap and Leap Attack ([gameplay] leap_by_gaze), the point skills ([gameplay] teleport_circle's list).
bool GazeSkill(int skill) {
    return (leapgaze::IsLeap(skill) && g_set.leapByGaze.load()) || pointwire::IsPoint(skill);
}
// How far the point of a skill may go from the hero (cells): Leap's and Leap Attack's own range (the distance the game's last pad cast of it asked its point at, else
// 40 cells - the game's leap holds it); every other point skill [gameplay] point_max_cells (Skills.txt gives them no cast distance of their own; at most 48).
float PointCapCells(int skill) {
    if (leapgaze::IsLeap(skill)) {
        const float seen = g_leapRange[RangeIndex(skill)].load();
        return seen > 0.0f ? leapgaze::RangeCells(seen) : leapgaze::kMaxCells;
    }
    return pointcast::CapCells(g_set.pointMaxCells.load());
}

LeapAim LeapAimNow(int skill) {
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockShared(&g_leapLock);
    const LeapAim last = g_leapAim;
    ReleaseSRWLockShared(&g_leapLock);
    if (last.ok && !last.done && last.skill == skill && now - last.at < 300) return last;   // this cast's, asked again
    LeapAim a;
    a.skill = skill;
    a.at = now;
    const float fx = g_gazeFwd[0].load(), fy = g_gazeFwd[1].load(), fz = g_gazeFwd[2].load();
    float down = 0.0f;
    pointcast::Offset circle;
    if (pointwire::CastPoint(skill, now, &circle)) {   // [gameplay] teleport_circle: a hold let go - the arc's circle
        AcquireSRWLockShared(&g_calLock); const Calib c = g_cal; ReleaseSRWLockShared(&g_calLock);
        aimpoint::GridDir(atan2f(circle.z, circle.x), xr::Running(), c.ok, c.mirror != 0, c.angle, &a.gx, &a.gy);
        a.fixed = true;
        a.cells = pointcast::Length(circle) / leapgaze::kUnitsPerCell;
        a.ok = std::isfinite(a.cells) && a.cells > 0.0f;
    } else if (now - g_gazeAt.load() < 500 && leapgaze::DownPerAcross(fx, fy, fz, &down)) {   // (a gaze straight down has no heading: the game's aim)
        AcquireSRWLockShared(&g_calLock); const Calib c = g_cal; ReleaseSRWLockShared(&g_calLock);
        aimpoint::GridDir(atan2f(fz, fx), xr::Running(), c.ok, c.mirror != 0, c.angle, &a.gx, &a.gy);
        a.eyeH = g_gazeEyeH.load() / leapgaze::kUnitsPerCell;
        a.down = down;
        a.ok = std::isfinite(a.eyeH) && a.eyeH > 0.0f;
    }
    if (a.ok && skill == leapgaze::kLeapAttack) {
        meleetouch::ThrowMonster mons[48];
        leapgaze::Monster lm[48];
        a.seen = meleetouch::ThrowTargets(mons, 48, &a.polled);
        float hx, hz;
        if (a.seen && meleetouch::UnitPlace((uintptr_t)g_heroUnit.load(), &hx, &hz)) {
            for (size_t i = 0; i < a.seen; ++i) lm[i] = {mons[i].id, (mons[i].x - hx) / leapgaze::kUnitsPerCell, (mons[i].z - hz) / leapgaze::kUnitsPerCell};
            const float cellsPerM = std::max(1.0f, ViewHeight()) / UserEyeHeightM() / leapgaze::kUnitsPerCell;
            const leapgaze::Body body{0.6f * cellsPerM, 1.9f * cellsPerM};   // a man-sized body, a little wide: the head's aim is rough
            if (a.fixed) {   // [gameplay] leap_arc: the hostile monster nearest the circle, within a body and a half of it
                const leapgaze::Pick p = leapgaze::PickNear(a.gx * a.cells, a.gy * a.cells, 1.5f * body.radiusCells + 0.5f, lm, (int)a.seen);
                if (p.found) { a.monster = p.id; a.monsterCells = p.along; }
            } else {
                const leapgaze::Pick p = leapgaze::PickMonster(a.gx, a.gy, a.eyeH, a.down, PointCapCells(skill), lm, (int)a.seen, body);
                if (p.found) { a.monster = p.id; a.monsterCells = p.along; }
            }
        }
    }
    AcquireSRWLockExclusive(&g_leapLock);
    g_leapAim = a;
    ReleaseSRWLockExclusive(&g_leapLock);
    if (a.ok && skill == leapgaze::kLeapAttack) {
        if (a.monster) LogF("leap by gaze: Leap Attack at monster %u, %.1f cells out %s (%u hostile read) - the game's lookups answer it", a.monster, a.monsterCells,
                            a.fixed ? "by the arc's circle" : "under the gaze", (unsigned)a.seen);
        else if (!a.polled) Log("leap by gaze: Leap Attack - no monster poll lately (not a Barbarian in F4?): the gaze's point on the ground");
    }
    return a;
}

// One of the game's target lookups (the cast flags' 0x145534, the resolver's two, the map click's): true when it is a leap's by the gaze - *unit its answer,
// the monster the gaze is on or none. skill: the controller's skill where the hook has it (-1: asked of the controller).
bool LeapLookup(uintptr_t ret, int skill, void** unit) {
    const int which = ret == g_dispatchTargetRet ? 0 : ret == g_spellTargetRet || ret == g_spellHoverRet ? 1 : ret == g_clickTargetRet ? 2 : -1;
    if (which < 0 || !LeapGazeOn() || !g_heroUnit.load()) return false;
    if (skill < 0) skill = SpellSkill(g_heroUnit.load());
    if (!GazeSkill(skill)) return false;
    const LeapAim a = LeapAimNow(skill);
    if (!a.ok) return false;
    *unit = a.monster ? (void*)meleetouch::MonsterUnit(a.monster) : nullptr;   // (a monster gone since: none - the gaze's point instead)
    if (which == 2) {   // the click has asked: the cast is on its way
        AcquireSRWLockExclusive(&g_leapLock);
        if (g_leapAim.at == a.at) g_leapAim.done = true;
        ReleaseSRWLockExclusive(&g_leapLock);
        g_spellClick = {};
        if (pointwire::IsPoint(skill)) pointwire::CastDone();   // [gameplay] teleport_circle: the release's point is spent
    }
    return true;
}

// "skill 54" for the log (a static buffer: the game's thread only)
const char* SkillWord(int skill) {
    static char b[24];
    snprintf(b, sizeof b, "skill %d", skill);
    return b;
}

// The resolver's AttackPoint for a leap by the gaze: `game` is the game's own point (from + its aim * the skill's distance) - its distance is the range the point
// is held to. False: the game's own point stays.
bool LeapPointFor(const float from[2], const float game[2], float* px, float* py) {
    AcquireSRWLockShared(&g_leapLock);
    const LeapAim a = g_leapAim;
    ReleaseSRWLockShared(&g_leapLock);
    if (!a.ok || !GazeSkill(a.skill)) return false;
    const float gameCells = std::hypot(game[0] - from[0], game[1] - from[1]);
    if (std::isfinite(gameCells) && gameCells > 0.0f) g_leapRange[RangeIndex(a.skill)].store(gameCells);
    const bool leap = leapgaze::IsLeap(a.skill);
    const float range = leap ? leapgaze::RangeCells(gameCells) : PointCapCells(a.skill);   // (a point skill: its cap - the game's own pad point is no range of its)
    leapgaze::Ground g = leapgaze::GroundCells(a.eyeH, a.down, range, leap ? leapgaze::kMaxCells : pointcast::kHardCapCells);
    if (a.fixed) g = {true, std::min(a.cells, range)};   // [gameplay] teleport_circle: the circle's distance, held to the range
    const bool ok = leapgaze::Point(from[0], from[1], a.gx, a.gy, g.cells, range, px, py, leap ? leapgaze::kMaxCells : pointcast::kHardCapCells);
    static ULONGLONG told = 0;
    if (told != a.at) {   // once a cast
        told = a.at;
        if (a.fixed)
            LogF("leap by gaze: %s - the arc's circle (teleport_circle) -> %.1f cells out along it (%.2f %.2f), the point (%.1f, %.1f)%s; the game's own point was %.1f cells out (%s)",
                 a.skill == leapgaze::kLeap ? "Leap" : a.skill == leapgaze::kLeapAttack ? (a.monster ? "Leap Attack at the monster by the circle" : "Leap Attack, no monster at the circle") : SkillWord(a.skill),
                 g.cells, a.gx, a.gy, ok ? *px : game[0], ok ? *py : game[1], ok ? "" : " - REFUSED (the map's edge): the game's own point stays", gameCells,
                 !leap ? "a point skill: held to [gameplay] point_max_cells" : range == gameCells ? "the range" : "not a leap's range: held to 40 cells, the game's leap holds it to its own");
        else LogF("leap by gaze: %s - %s -> %.1f cells out along it (%.2f %.2f), the point (%.1f, %.1f)%s; the game's own point was %.1f cells out (%s), eyes %.1f cells up, the gaze %.0f deg down",
             a.skill == leapgaze::kLeap ? "Leap" : a.skill == leapgaze::kLeapAttack ? "Leap Attack, no monster at the point" : SkillWord(a.skill),
             a.fixed ? "the arc's circle (teleport_circle)" : g.met ? "the gaze meets the ground" : "the gaze meets no ground within the range", g.cells, a.gx, a.gy, ok ? *px : game[0], ok ? *py : game[1],
             ok ? "" : " - REFUSED (the map's edge): the game's own point stays", gameCells,
             !leap ? "a point skill: held to [gameplay] point_max_cells" : range == gameCells ? "the range" : "not a leap's range: held to 40 cells, the game's leap holds it to its own",
             a.eyeH, atanf(a.down) * 57.2957795f);
    }
    return ok;
}

void HookSpellResolve(void* mgr, void* unit, bool assist, float* x, float* y) {
    SpellCast cast;
    int skill = -1;
    const bool drawn = bowdrawwire::AimLive() && BowInHand();   // [hands] bow_draw: the 0.4 s after a released draw
    // [hands] throw_gesture / throw_by_grip: the 0.5 s after a released throw. Its cast is the controller's own and the unit is the hero the
    // aim function last worked on (g_heroUnit) even where IsHeroUnit's look-at test says no - the camera's look-at lags a hero who runs.
    const bool thrownLive = GestureAimLive() && ThrownInHand();
    // F4 only: in F1, F2, F3 and the flat modes the game's own auto-target and aim assist stay as they are (the user, 2026-10-15)
    if (unit && x && y && LeapGazeOn() && (IsHeroUnit(unit) || unit == g_heroUnit.load()) && GazeSkill(skill = SpellSkill(unit))) {
        // [gameplay] leap_by_gaze, teleport_circle: the game's lookups answer the gaze's monster or none (LeapLookup), the aim assist is off, the point is the gaze's (or the circle's)
        const LeapAim a = LeapAimNow(skill);
        if (a.ok) { cast.directed = cast.leap = true; cast.gx = a.gx; cast.gy = a.gy; }
    } else if (viewgate::F4() && SpellAimWanted(drawn) && unit && x && y && AimByVector() &&
        !gamestate::MenuOpen() && (IsHeroUnit(unit) || (thrownLive && unit == g_heroUnit.load())) && (skill = SpellSkill(unit)) >= 0)
        cast = SpellCastFor(skill, drawn);
    if (g_ptActive.load(std::memory_order_relaxed) && unit && IsHeroUnit(unit)) PressTraceCast(skill >= 0 ? skill : SpellSkill(unit), cast.directed);   // [debug] melee_press_trace
    const SpellCast outer = g_spellCast;   // a resolution inside this one is its own; this one's comes back after
    g_spellCast = cast;
    OrigSpellResolve(mgr, unit, cast.directed ? false : assist, x, y);
    g_spellCast = outer;
    if (shieldsmite::Casting() && x && y && unit && IsHeroUnit(unit) && SpellSkill(unit) == shieldsmite::kSmiteSkill) shieldsmite::Aim(x, y);   // [gameplay] shield_smite: aimed at the monster
    g_spellClick = {cast.directed, GetTickCount64()};   // whichever caller: the ones that click do it right after
    if (x && y)
        shottrace::Note("cast resolver (from 0x%llX): skill %d, %s, the point (%.1f, %.1f)", (unsigned long long)((uintptr_t)_ReturnAddress() - g_base), skill,
                        cast.directed ? (cast.point ? "directed, the point far out along the hand" : "directed, the game's aim") : "NOT directed - the game's own aim", *x, *y);
    if (thrownLive && !cast.directed) {   // a throw gesture's press, and the cast was not aimed along it: say why
        const bool hero = unit && (IsHeroUnit(unit) || unit == g_heroUnit.load());
        const char* why = !unit || !x || !y ? "no unit or point" :
                          !AimByVector() ? "[bow] mode is not 4" :
                          gamestate::MenuOpen() ? "a menu is open" :
                          !hero ? "the unit is not recognised as the hero (the camera's look-at lags a walking hero)" :
                          skill < 0 ? "the controller has no skill for this cast" :
                          skill != 0 && skill != 2 && !gamestate::SkillAimed(skill) ? "the skill is not the throw (0 / 2) nor one that flies" :
                          skill < 1024 && ((g_set.autoKeep[skill >> 6].load() >> (skill & 63)) & 1) ? "[autotarget] keeps the game's auto-target for this skill" :
                          "no heading on the ground";
        LogF("throw gesture: the cast (skill %d) was NOT aimed along the throw - %s", skill, why);
    }
    if (cast.directed) {
        static ULONGLONG told = 0;
        if (thrownLive || GetTickCount64() - told > 2000) {
            told = GetTickCount64();
            LogF("spell aim: skill %d from 0x%llX, the hand %.2f %.2f -> %.1f %.1f (%s; the game's target and aim assist off for this cast)",
                 skill, (unsigned long long)((uintptr_t)_ReturnAddress() - g_base), cast.gx, cast.gy, *x, *y,
                 cast.point ? "the point far out along the hand" : "the game's aim turned to the hand");
        }
    }
}

// The tap's click asking for its target, just after a directed resolution: none, once.
bool ClickTargetOff(uintptr_t ret) {
    if (ret != g_clickTargetRet) return false;
    const ULONGLONG age = GetTickCount64() - g_spellClick.at;
    const bool off = g_spellClick.directed && age < 100;
    shottrace::Note("the click's second target lookup: %s (%llu ms after the resolver)", off ? "answered none (a directed cast)" : "the game's own answer", (unsigned long long)age);
    static ULONGLONG told = 0;
    if ((off || g_spellAimHook.load() == 1) && GetTickCount64() - told > 2000) {
        told = GetTickCount64();
        LogF("spell aim: the map click asks its target - %s", off ? "none (a directed cast)" :
             g_spellClick.directed ? "the game's (the directed cast too long ago)" : "the game's (not a directed cast)");
    }
    g_spellClick = {};
    return off;
}

// [gameplay] melee_touch: the press the touch made (melee_touch.cpp) asks the game's target lookups - the resolver's two
// and the map click's - for the monster the blade touched, for 350 ms; the game's own attack then goes to it (the resolver
// aims at the unit's place, the click attacks the unit). Looked up by its id then and there, on the game's UI thread.
void* TouchedTarget(uintptr_t ret) {
    if ((ret != g_spellTargetRet && ret != g_spellHoverRet && ret != g_clickTargetRet) || !meleetouch::TargetPending()) return nullptr;
    return (void*)meleetouch::TargetUnit(ret == g_clickTargetRet ? 2 : 1);
}

// [gameplay] aim_no_chase: the cast flags' own look at the controller's target (0x145534, inside 0x1454A0) for a cast that goes along the hand. The resolver's and the
// click's looks are none for it already; this one is the game's first, made before them, and with a unit found (a monster near the aim) it clears the flags' Stand Still
// byte - a cast AT that unit - so the click, which has no target left and no Stand Still, is a plain click on the ground: the hero RUNS to the point (the pad's cast of
// the log of 2026-10-14 18:05, 3 ms after A went down; and, with the monster in front of the aim, "he runs up to it instead of shooting"). None here, the flags keep
// their Stand Still and the cast is the same cast in place as one with no monster near. `skill`: the controller's skill where the hook has it (-1: asked of the controller).
bool DispatchTargetOff(int skill) {
    if (!g_set.aimNoChase.load() || g_spellAimHook.load() != 1 || !viewgate::F4()) return false;   // F4 only, like the guards of the shot
    if (shieldsmite::Casting() || meleetouch::TargetPending()) return false;   // casts made to go AT a target keep the game's own
    void* unit = g_heroUnit.load();
    const bool drawn = bowdrawwire::AimLive() && BowInHand();
    if (!unit || !SpellAimWanted(drawn) || !AimByVector() || gamestate::MenuOpen()) return false;
    if (skill < 0 && (skill = SpellSkill(unit)) < 0) return false;
    return SpellCastFor(skill, drawn).directed;
}
// What the cast flags' look gets back: the game's `answer`, or none when `off`. [debug] shot_trace tells which, and what the game found when it was let ask.
void* DispatchAnswer(void* answer, bool off, void* peeked = nullptr) {
    if (shottrace::Active()) {
        char u[220];
        shottrace::UnitText(off ? peeked : answer, g_heroUnit.load(), u, sizeof u);
        shottrace::Note("the cast flags' own look at the controller's target (0x1454A0 clears Stand Still when it finds one): %s%s",
                        off ? "answered none (a directed cast, [gameplay] aim_no_chase); the game would have answered: " : "the game's answer: ", u);
    }
    if (off) {
        static ULONGLONG told = 0;
        if (GetTickCount64() - told > 5000) {
            told = GetTickCount64();
            Log("spell aim: the cast flags' own look at the controller's target answered none (a directed cast) - the game keeps Stand Still for it, so a monster near the aim does not send the hero after it ([gameplay] aim_no_chase)");
        }
    }
    return off ? nullptr : answer;
}

// ---- the Kurast sewer stairs (vr/sewer_interaction.h) ----------------------------------------------------------------------------
// The game's Interact query (the controller's target for skill 357, this same lookup) feeds both its "(A) Interact" prompt and the
// A press. When it finds nothing in Kurast Bazaar or Upper Kurast, VR F4, the sewer warp the head faces nearby is offered - if the
// game's own reach and line-of-sight test (0x348650) accepts it. Any target the game found keeps priority. From bearstonem/d2r-pcvr
// (MIT, v0.3.20: vr/vrcam.cpp NearbySewerInteraction), with its audit of the layout it reads.
constexpr int kInteractSkill = 357;

bool SewerLayoutReady() {
    static std::atomic<bool> ready{false};
    if (ready.load()) return true;
    static thread_local ULONGLONG tried = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - tried < 1000) return false;
    tried = now;
    // The room's first unit and the next one (+0x160), its neighbours, the dynamic path's room, a tile's static x/y, and the reach and
    // line test inside 0x348650 - untouched instructions inside each, as 3.3.93787 has them.
    static const uint8_t first[] = {0x48,0x8B,0x81,0xA8,0,0,0,0xC3};
    static const uint8_t next[] = {0x48,0x8B,0x81,0x60,0x01,0,0};
    static const uint8_t neighbors[] = {0x8B,0x41,0x40,0x41,0x89,0,0x48,0x8B,0x01,0x48,0x89,0x02,0xC3};
    static const uint8_t dynamic[] = {0x48,0x8B,0x41,0x20,0xC3};
    static const uint8_t sy[] = {0x48,0x8B,0x46,0x38,0x8B,0x58,0x14};
    static const uint8_t sx[] = {0x48,0x8B,0x46,0x38,0x8B,0x40,0x10};
    static const uint8_t range[] = {0x4D,0x8B,0xF1,0x45,0x8B,0xF8,0x48,0x8B,0xF2,0x48,0x8B,0xF9};
    static const uint8_t line[] = {0x41,0xB8,0x04,0x08,0,0,0x48,0x8B,0xD6,0x48,0x8B,0xCF,0xE8,0x5B,0x7B,0,0};
    const bool found = Matches(0x2EFD90, first, sizeof first) && Matches(0x34B4CE, next, sizeof next) && Matches(0x2EFDE0, neighbors, sizeof neighbors) &&
                       Matches(0x341C30, dynamic, sizeof dynamic) && Matches(0x18A88D, sy, sizeof sy) && Matches(0x18A8B8, sx, sizeof sx) &&
                       Matches(0x348668, range, sizeof range) && Matches(0x348734, line, sizeof line);
    static std::atomic<bool> told{false};
    if (found) { ready.store(true); Log("vrcam: Kurast sewer stairs - the game's room lists and its reach/line test verified: A takes the stairs standing"); }
    else if (!told.exchange(true)) Log("vrcam: Kurast sewer stairs - the game's layout is not 3.3.93787's (another build?): the game's own selection only");
    return found;
}

// The client's local hero (the game's LocalIndex and ClientPlayer) and its area, by D2RLoader's own accessor. 0 when not there.
uintptr_t SewerHero() noexcept {
    const uintptr_t indexFn = d2rsig::Addr(RVA_SEWER_LOCAL_INDEX), playerFn = d2rsig::Addr(RVA_SEWER_CLIENT_PLAYER);
    if (!indexFn || !playerFn) return 0;
    __try {
        const int index = reinterpret_cast<int (*)()>(indexFn)();
        if (index < 0 || index >= 8) return 0;
        const uintptr_t unit = reinterpret_cast<uintptr_t (*)(int)>(playerFn)(index);
        if (unit < 0x10000 || *reinterpret_cast<const volatile uint32_t*>(unit) != 0) return 0;
        return unit;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
D2RL::Reimplementations::GetLevelIdFromUnitFn SewerGetLevel() noexcept {   // apart from SewerLevel: a static's guard and __try don't mix (C2712)
    static const auto getLevel = []() -> D2RL::Reimplementations::GetLevelIdFromUnitFn {
        const HMODULE core = GetModuleHandleW(L"D2RCore.dll");
        return core ? reinterpret_cast<D2RL::Reimplementations::GetLevelIdFromUnitFn>(
                          GetProcAddress(core, MAKEINTRESOURCEA(D2RL::Reimplementations::GetLevelIdFromUnitInfo.ordinal)))
                    : nullptr;
    }();
    return getLevel;
}
int SewerLevel(uintptr_t hero) noexcept {
    const auto getLevel = SewerGetLevel();
    if (!getLevel || !hero) return 0;
    __try { return getLevel(reinterpret_cast<const void*>(hero)); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

uintptr_t NearbySewerWarp() {
    if (!g_enabled.load() || !FullBody() || gamestate::MenuOpen()) return 0;
    const uintptr_t hero = SewerHero();
    const int level = SewerLevel(hero);
    if (!sewerinteraction::Area(level) || !SewerLayoutReady()) return 0;
    float bx = 0.0f, bz = 0.0f;   // the camera's back axis: the head looks along its negative
    bool axes = false;
    AcquireSRWLockShared(&g_skyLock);
    const SkyView& view = g_skyView[g_eye.load() & 1];
    if (view.axes_ok) { bx = view.axes[6]; bz = view.axes[8]; axes = true; }
    ReleaseSRWLockShared(&g_skyLock);
    if (!axes || !std::isfinite(bx) || !std::isfinite(bz) || bx * bx + bz * bz < 1e-6f) return 0;
    float gx = 0.0f, gy = 0.0f;
    AcquireSRWLockShared(&g_calLock); const Calib c = g_cal; ReleaseSRWLockShared(&g_calLock);
    aimpoint::GridDir(atan2f(-bz, -bx), xr::Running(), c.ok, c.mirror != 0, c.angle, &gx, &gy);
    const uintptr_t reach = d2rsig::Addr(RVA_SEWER_REACH);
    const sewerinteraction::Result r = sewerinteraction::Find(hero, level, gx, gy,
        [](uintptr_t at, auto& value) { return at >= 0x10000 && at <= UINTPTR_MAX - sizeof value && SafeRead(&value, reinterpret_cast<const void*>(at), sizeof value); },
        [reach](uintptr_t local, uintptr_t tile) {
            // the game's own reach AND line test: never through a wall, never farther than the game reaches
            if (!reach) return false;
            __try { return reinterpret_cast<int (*)(uintptr_t, uintptr_t, int, void*)>(reach)(local, tile, 0, nullptr) != 0; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        });
    static thread_local ULONGLONG reported = 0;
    if (const ULONGLONG now = GetTickCount64(); (r.warps || r.unit) && now - reported >= 1000) {
        reported = now;
        LogF("vrcam: Kurast sewer stairs - area %d: %u near, %u faced, %s (id %u, %.2f cells; %u units, %u tiles looked at)", level, r.warps, r.facing,
             r.unit ? "offered to A" : "none in reach", r.id, r.distance, r.visited, r.tiles);
    }
    return r.unit;
}

thread_local bool t_touchAnswer = false;   // [debug] melee_press_trace: the lookup was answered with the touched monster
int LookupOf(uintptr_t ret) {
    return ret == g_dispatchTargetRet ? presstrace::kDispatch : ret == g_spellTargetRet ? presstrace::kResolver : ret == g_spellHoverRet ? presstrace::kHover
         : ret == g_clickTargetRet ? presstrace::kClick : -1;
}

void* SpellTargetFor(void* mgr, int skill, uintptr_t ret) {
    if (void* leap = nullptr; LeapLookup(ret, skill, &leap)) return leap;   // [gameplay] leap_by_gaze: the gaze's monster, or none
    if (ret == g_dispatchTargetRet) {
        const bool off = DispatchTargetOff(skill);
        void* t = off && !shottrace::Active() ? nullptr : OrigSpellTarget(mgr, skill);   // asked anyway while a shot is traced: to say what the game found
        return DispatchAnswer(off ? nullptr : t, off, t);
    }
    if (shieldsmite::Casting()) {   // [gameplay] shield_smite: the monster the shield touched is the cast's target
        if (const int where = ret == g_spellTargetRet ? 1 : ret == g_clickTargetRet ? 2 : 0)
            if (void* t = shieldsmite::Target(where, skill)) return t;
    }
    if (void* t = TouchedTarget(ret)) { t_touchAnswer = true; return t; }
    if ((g_spellCast.directed && ret == g_spellTargetRet) || ClickTargetOff(ret)) return nullptr;
    void* t = OrigSpellTarget(mgr, skill);
    if (!t && skill == kInteractSkill) t = reinterpret_cast<void*>(NearbySewerWarp());   // the Kurast sewer stairs, standing
    return t;
}
void* HookSpellTarget(void* mgr, int skill) {
    const uintptr_t ret = (uintptr_t)_ReturnAddress();
    t_touchAnswer = false;
    void* t = SpellTargetFor(mgr, skill, ret);
    if (g_ptActive.load(std::memory_order_relaxed))
        if (const int which = LookupOf(ret); which >= 0) PressTraceLookup(which, t, t_touchAnswer);   // [debug] melee_press_trace
    return t;
}

void* SpellHoverFor(int index, uintptr_t ret) {
    if (void* leap = nullptr; LeapLookup(ret, -1, &leap)) return leap;   // [gameplay] leap_by_gaze, as SpellTargetFor
    if (ret == g_dispatchTargetRet) {
        const bool off = DispatchTargetOff(-1);
        void* t = off && !shottrace::Active() ? nullptr : OrigSpellHover(index);
        return DispatchAnswer(off ? nullptr : t, off, t);
    }
    if (shieldsmite::Casting()) {   // [gameplay] shield_smite, as HookSpellTarget
        if (const int where = ret == g_spellHoverRet ? 1 : ret == g_clickTargetRet ? 2 : 0)
            if (void* t = shieldsmite::Target(where, -1)) return t;
    }
    if (void* t = TouchedTarget(ret)) { t_touchAnswer = true; return t; }
    if ((g_spellCast.directed && ret == g_spellHoverRet) || ClickTargetOff(ret)) return nullptr;
    return OrigSpellHover(index);
}
void* HookSpellHover(int index) {
    const uintptr_t ret = (uintptr_t)_ReturnAddress();
    t_touchAnswer = false;
    void* t = SpellHoverFor(index, ret);
    if (g_ptActive.load(std::memory_order_relaxed))
        if (const int which = LookupOf(ret); which >= 0) PressTraceLookup(which, t, t_touchAnswer);   // [debug] melee_press_trace
    return t;
}

// `fn` ends in a jump to `to` (an E9 rel32 in its first 0x80 bytes): a tail call.
bool TailJumpsTo(uint64_t fn, uint64_t to) {
    const uintptr_t f = d2rsig::Addr(fn), t = d2rsig::Addr(to);
    uint8_t b[0x80];
    if (!f || !t || !SafeRead(b, (const void*)f, sizeof b)) return false;
    for (size_t i = 0; i + 5 <= sizeof b; ++i) {
        int32_t rel;
        memcpy(&rel, b + i + 1, 4);
        if (b[i] == 0xE9 && f + i + 5 + rel == t) return true;
    }
    return false;
}

// The call that returns at `ret` goes to `fn` (an E8 rel32 just before it).
bool CallsTo(uint64_t ret, uint64_t fn) {
    const uintptr_t r = d2rsig::Addr(ret), f = d2rsig::Addr(fn);
    uint8_t op = 0;
    int32_t rel = 0;
    return r && f && SafeRead(&op, (const void*)(r - 5), 1) && op == 0xE8 && SafeRead(&rel, (const void*)(r - 4), 4) && r + rel == f;
}

// From the timer once spell_aim is on (and stays in after: off, the hooks pass everything on).
// The resolver's code is decrypted with the first cast: tried again until then.
// [hands] throw_auto_skill: a throw by gesture presses the pad's A, which the controller table binds to Attack (skill 0) - the skill skills.txt refuses for a
// throwing potion (etypea1 tpot) and gives a knife, axe or javelin only for an enemy at the point (TargetableOnly, SearchEnemyXY). With a throwing weapon in
// hand the game's own mouse path casts Throw (skill 2, a cast at a location) for the plain attack; the pad's path never does. The pad's press runs
// SelectSlot (0x14A5A0: the cast's skill = the slot's binding, read by SlotSkill 0x117D20 at 0x14A69E) and the game's hand-skill setter (0x118D50: the unit's
// left skill = the slot's binding, SlotSkill at 0x118DDD). While a throw is live these two reads answer Throw when the binding is Attack: the cast, the
// controller's state and the unit's left skill are then exactly those of a slot that holds Throw; the game's own checks (the skill is the hero's, the weapon
// is a throwing one, mana, range, the animation, the stack) all run as for a click. Any other caller of SlotSkill (the interface drawing the slots) and any
// other binding are answered as they are.
constexpr uint64_t RVA_SLOT_SKILL = 0x117D20, RVA_SLOT_SKILL_SELECT_RET = 0x14A6A3, RVA_SLOT_SKILL_SET_RET = 0x118DE2;
// push rbx (REX-prefixed); sub rsp, 20h; movsxd rbx, ecx; cmp ebx, 0Fh; jbe +14h; lea rcx, [rsp+30h]
const uint8_t kSigSlotSkill[19] = {0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x63,0xD9,0x83,0xFB,0x0F,0x76,0x14,0x48,0x8D,0x4C,0x24,0x30};
using SlotSkillFn = int (*)(int slot);
SlotSkillFn OrigSlotSkill = nullptr;
uintptr_t g_slotSelectRet = 0, g_slotSetRet = 0;
std::atomic<int> g_slotSkillHook{0};   // 0 not in yet, 1 in, 2 not possible

int SlotSkillFor(int slot, uintptr_t ret, int* boundOut) {
    const int bound = OrigSlotSkill(slot);
    *boundOut = bound;
    if (bound != 0) {   // (the common case first: any other binding is the game's)
        // a bow skill on the attack's slot with no bow or crossbow in hand: the old bow quiver (to 0.174) loaded its arrow skills there and the game
        // keeps that slot in the save - every attack, the touch's included, went to the game as that bow skill, which it refuses for a melee weapon
        // (the author, 2026-10-20: "the monster's capsule turns red but the strike is not pressed"). The quiver now only arms its skill (ShotSkill,
        // below) and gives that slot back as the plain attack (bowquiver::Cleanup); until it has, this reads it as the plain attack (or the armed
        // melee skill, when the weapon can use it).
        // [bow] draw_shoots_arrow: a released draw's press (the bow draw's aim is live: 0.4 s) reads the A slot. The draw's release is "the game's attack - a bow's
        // own arrow" (bow_draw_button 0), but the game casts what the A slot holds: a player's (0.178, a Warlock) held a spell there - the string drew, no arrow
        // ever flew, with every version and a reset ini alike (the slot is the save's). With the box: a skill there that is not a bow skill is read as the plain
        // Attack (the game's own, every hero has it - the bow's arrow); a bow skill there stays; an armed quiver skill comes first, as with Attack on the slot.
        if (bound > 0 && slot == 0 && g_set.bowDrawArrow.load() && BowInHand() && (ret == g_slotSelectRet || ret == g_slotSetRet) && bowdrawwire::AimLive()) {
            const int shot = bowquiverwire::ShotSkill();
            const int skill = bowquiver::DrawSlotAnswer(bound, shot);
            if (skill != bound) {
                static ULONGLONG told = 0;
                if (GetTickCount64() - told > 2000) {
                    told = GetTickCount64();
                    LogF("bow draw: the release's A slot holds skill %d (not a bow skill) - read as %s (%d), the bow's %s ([bow] draw_shoots_arrow)", bound,
                         shot > 0 && bowquiver::FindArrow(shot) ? bowquiver::FindArrow(shot)->name : "the plain Attack", skill, shot > 0 ? "armed skill" : "own arrow");
                }
            }
            return skill;
        }
        if (bound > 0 && bowquiver::FindArrow(bound) && !BowInHand()) {
            const uintptr_t at = ret;
            if (at == g_slotSelectRet || at == g_slotSetRet) {
                const uint32_t t = gamestate::WeaponType();
                const int armed = ThrownInHand() ? 0 : meleearm::PressSkill();
                const int skill = armed > 0 && meleearm::Fits(armed, t == D2RVR_TYPE_SPEAR || t == D2RVR_TYPE_JAVELIN, t == D2RVR_TYPE_DAGGER) ? armed : 0;
                if (skill > 0) meleearm::Used(skill);   // [gameplay] melee_skill_arm_once: the blow is the skill's - spent
                static ULONGLONG told = 0;
                if (GetTickCount64() - told > 2000) {
                    told = GetTickCount64();
                    LogF("bow quiver: the slot's bow skill (%d) was read as %s (%d) by the %s - no bow in hand", bound, meleearm::Name(skill), skill,
                         at == g_slotSelectRet ? "cast's slot selection" : "unit's hand-skill setter");
                }
                return skill;
            }
        }
        return bound;
    }
    const bool site = ret == g_slotSelectRet || ret == g_slotSetRet;
    int skill = throwcast::SwapSkill(bound, site, g_set.throwAutoSkill.load(), site && ThrownInHand(), site && GestureAimLive());
    int32_t chosen = quiver::kDefault;
    bool chosenRanged = true;
    const bool quivered = site && quiverwire::Chosen(&chosen, &chosenRanged);   // [hands] javelin_quiver: the plain attack is the skill chosen from the quiver
    if (quivered) skill = quiver::Answer(skill, bound, site, true, ThrownInHand(), chosen, chosenRanged, GestureAimLive());
    if (skill != bound) {
        static ULONGLONG told = 0;
        if (GetTickCount64() - told > 300) {
            told = GetTickCount64();
            LogF("throw gesture: the slot's skill (Attack, 0) was read as %s (%d) by the %s", quivered && skill == chosen ? "the quiver's skill" : "Throw", skill,
                 ret == g_slotSelectRet ? "cast's slot selection" : "unit's hand-skill setter");
        }
    } else if (const int shot = site && BowInHand() ? bowquiverwire::ShotSkill() : 0; shot > 0) {
        // [bow] quiver: a release of the string with a bow skill armed - read as that skill, decided at the release (vr/bow_quiver_wire.cpp OnRelease: the plain
        // arrow when the hero cannot pay it). With no bow or crossbow in hand the armed bow skill is never answered.
        skill = shot;
        static ULONGLONG told = 0;
        if (GetTickCount64() - told > 300) {
            told = GetTickCount64();
            LogF("bow quiver: the slot's skill (Attack, 0) was read as the armed %s (%d) by the %s", bowquiver::FindArrow(shot) ? bowquiver::FindArrow(shot)->name : "skill", shot,
                 ret == g_slotSelectRet ? "cast's slot selection" : "unit's hand-skill setter");
        }
    } else if (site && !ThrownInHand()) {   // [gameplay] melee_skill_armed: the touch's attack press is read as the armed melee skill - decided at the press (vr/melee_arm_game.cpp: the plain attack when the mana is short)
        const uint32_t type = gamestate::WeaponType();
        const int armed = meleearm::PressSkill();
        if (armed > 0 && !meleearm::Fits(armed, type == D2RVR_TYPE_SPEAR || type == D2RVR_TYPE_JAVELIN, type == D2RVR_TYPE_DAGGER)) {
            static ULONGLONG toldFit = 0;   // armed for another weapon (Jab, then a sword in hand): the plain attack, never a cast the game refuses
            if (GetTickCount64() - toldFit > 2000) {
                toldFit = GetTickCount64();
                LogF("melee armed: %s (%d) needs a %s - the weapon in hand (type %u) cannot: the plain attack", meleearm::Name(armed), armed,
                     meleearm::NeedOf(armed) == meleearm::Need::Spear ? "spear or javelin" : "dagger", type);
            }
        } else if (armed > 0) {
            skill = armed;
            meleearm::Used(armed);   // [gameplay] melee_skill_arm_once: the blow is the skill's - spent
            static ULONGLONG told = 0;
            if (GetTickCount64() - told > 300) {
                told = GetTickCount64();
                LogF("melee armed: the slot's skill (Attack, 0) was read as %s (%d) by the %s", meleearm::Name(armed), armed, ret == g_slotSelectRet ? "cast's slot selection" : "unit's hand-skill setter");
            }
        }
    }
    return skill;
}
int HookSlotSkill(int slot) {
    const uintptr_t ret = (uintptr_t)_ReturnAddress();
    int bound = -1;
    const int skill = SlotSkillFor(slot, ret, &bound);
    if (g_ptActive.load(std::memory_order_relaxed) && (ret == g_slotSelectRet || ret == g_slotSetRet))   // [debug] melee_press_trace: the game's reads of the pressed slot
        PressTraceSlot(ret == g_slotSelectRet ? presstrace::kSelect : presstrace::kSetter, slot, bound, skill);
    return skill;
}

bool ThrowAutoSkillReady() { return g_set.throwAutoSkill.load() && g_slotSkillHook.load() == 1; }

void InstallSlotSkillHook() {
    if (!g_ctx || g_slotSkillHook.load() != 0 || !((g_set.throwGesture.load() && g_set.throwAutoSkill.load()) || quiverwire::On() || bowquiverwire::WantsHook() || meleearm::On() ||
                                                    g_set.bowDrawArrow.load())) return;
    if (!Matches(RVA_SLOT_SKILL, kSigSlotSkill, sizeof kSigSlotSkill)) return;   // the code is decrypted as it runs: tried again
    if (!CallsTo(RVA_SLOT_SKILL_SELECT_RET, RVA_SLOT_SKILL) || !CallsTo(RVA_SLOT_SKILL_SET_RET, RVA_SLOT_SKILL)) {
        g_slotSkillHook.store(2);
        meleearm::SetHookState(2);   // [gameplay] melee_skill_armed needs the hook
        Log("vrcam: throw_auto_skill NOT possible - the controller's slot-skill reads are not where they were (another build?); a throw presses the pad button of the slot that holds Throw");
        return;
    }
    g_slotSelectRet = d2rsig::Addr(RVA_SLOT_SKILL_SELECT_RET);
    g_slotSetRet = d2rsig::Addr(RVA_SLOT_SKILL_SET_RET);
    const bool in = d2rsig::Hook(RVA_SLOT_SKILL, kSigSlotSkill, sizeof kSigSlotSkill, (void*)&HookSlotSkill, (void**)&OrigSlotSkill);
    g_slotSkillHook.store(in ? 1 : 2);
    bowquiverwire::SetHookReady(in);        // [bow] quiver: the armed bow skill's shot needs the hook
    meleearm::SetHookState(in ? 1 : 2);     // [gameplay] melee_skill_armed too
    Log(in ? "vrcam: throw_auto_skill in - with a throwing weapon in hand a throw's A press is read by the game as its Throw skill (no skill slot needed), or as the skill chosen from the quiver"
           : "vrcam: throw_auto_skill hook FAILED - a throw presses the pad button of the slot that holds Throw");
}

void InstallSpellAimHooks() {
    if (!g_ctx || g_spellAimHook.load() != 0 || !(g_set.spellAim.load() || g_set.bowAim.load() || g_set.throwAim.load() || g_set.throwGesture.load() || bowdrawwire::On() || shieldsmite::On() || g_set.meleeTouch.load()) || !OrigAttackPoint) return;   // the point's hook in first
    if (!Matches(RVA_SPELL_RESOLVE, kSigSpellResolve, sizeof kSigSpellResolve) || !Matches(RVA_SPELL_TARGET, kSigSpellTarget, sizeof kSigSpellTarget) ||
        !Matches(RVA_HOVER_UNIT, kSigHoverUnit, sizeof kSigHoverUnit) || !Matches(RVA_CLIENT_INDEX, kSigClientIndex, sizeof kSigClientIndex) ||
        !Matches(RVA_CTRL_HANDLER, kSigCtrlHandler, sizeof kSigCtrlHandler) || !Matches(RVA_CTRL_SKILL, kSigCtrlSkill, sizeof kSigCtrlSkill) ||
        !Matches(RVA_CLICK_TARGET, kSigClickTarget, sizeof kSigClickTarget))
        return;
    if (!CallsTo(RVA_SPELL_TARGET_RET, RVA_SPELL_TARGET) || !CallsTo(RVA_SPELL_HOVER_RET, RVA_HOVER_UNIT) ||
        !CallsTo(RVA_SPELL_POINT_RET, RVA_ATTACK_POINT) ||
        !CallsTo(RVA_CLICK_TARGET_RET, RVA_CLICK_TARGET) || !TailJumpsTo(RVA_CLICK_TARGET, RVA_SPELL_TARGET) ||
        !TailJumpsTo(RVA_CLICK_TARGET, RVA_HOVER_UNIT)) {
        g_spellAimHook.store(2);
        Log("vrcam: spell aim NOT possible - the cast resolver's calls are not where they were (another build?); spells keep the game's targeting");
        return;
    }
    g_spellTargetRet = d2rsig::Addr(RVA_SPELL_TARGET_RET);
    g_spellHoverRet = d2rsig::Addr(RVA_SPELL_HOVER_RET);
    g_spellPointRet = d2rsig::Addr(RVA_SPELL_POINT_RET);
    g_clickTargetRet = d2rsig::Addr(RVA_CLICK_TARGET_RET);
    if (CallsTo(RVA_DISPATCH_TARGET_RET, RVA_CLICK_TARGET)) g_dispatchTargetRet = d2rsig::Addr(RVA_DISPATCH_TARGET_RET);
    else Log("vrcam: spell aim - the cast flags' look at the controller's target (0x145534) is not where it was (another build?): [gameplay] aim_no_chase does nothing");
    // the lookups first, the resolver last: nothing is aimed until all three are in
    bool in = d2rsig::Hook(RVA_SPELL_TARGET, kSigSpellTarget, sizeof kSigSpellTarget, (void*)&HookSpellTarget, (void**)&OrigSpellTarget);
    if (in) g_hoverHooked.store(in = d2rsig::Hook(RVA_HOVER_UNIT, kSigHoverUnit, sizeof kSigHoverUnit, (void*)&HookSpellHover, (void**)&OrigSpellHover));
    if (in) in = d2rsig::Hook(RVA_SPELL_RESOLVE, kSigSpellResolve, sizeof kSigSpellResolve, (void*)&HookSpellResolve, (void**)&OrigSpellResolve);
    g_spellAimHook.store(in ? 1 : 2);
    Log(in ? "vrcam: spell aim in - a spell that flies goes along the hand, the game's target and aim assist off for that cast"
           : "vrcam: spell aim hooks FAILED - spells keep the game's targeting");
}

}  // namespace vrc
