// vr/combat/combat_ticks.cpp - the hero's animation mode and client unit, the flat town portal's frame, and the update thread's ticks of
// the attacks: [debug] bow_latency, the hit by touch's animation notes and [debug] melee_press_trace, [bow] quick_release / [hands]
// quick_throw (anim cut), [debug] throw_trace, MeleeTouchTick (cut from vr/vrcam.cpp, docs/plan_modules.md - the map's combat group).
// What it gives: vr/combat/combat.h. It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// The hero's animation mode as the game's client unit has it (Diablo II's unit modes: 1 neutral, 7 and 8 the attacks, 11 the throw), -1 when the unit
// is not known: it tells [weight] when the game's own throw animation is over (weight::SetHeroModeSource). The same unit the shield's smite uses.
int HeroAnimMode() {
    uintptr_t hero = (uintptr_t)g_heroUnit.load();
    if (!hero || !IsHeroUnit((void*)hero)) hero = (uintptr_t)g_heroSeen.load();
    if (!hero || !IsHeroUnit((void*)hero)) {
        const uintptr_t p = (uintptr_t)gamestate::LocalPlayer();   // the SDK's handle of the local player, when it is the unit itself
        if (p > 0x10000 && IsHeroUnit((void*)p)) hero = p;
        else return -1;
    }
    uint32_t mode = 0;
    return SafeRead(&mode, (const void*)(hero + 0xC), sizeof mode) && mode < 64 ? (int)mode : -1;
}

// [weight] clash_sound: the hero's client unit the game's block sound sounds at (the same one HeroAnimMode reads), nullptr when not known. The game's thread.
void* HeroUnitForSound() {
    void* hero = g_heroUnit.load();
    if (hero && IsHeroUnit(hero)) return hero;
    hero = g_heroSeen.load();
    if (hero && IsHeroUnit(hero)) return hero;
    const uintptr_t p = (uintptr_t)gamestate::LocalPlayer();
    return p > 0x10000 && IsHeroUnit((void*)p) ? (void*)p : nullptr;
}
void* PortalHeroUnit() { return HeroUnitForSound(); }

// [input] portal_auto's click (vr/portal_auto.h): the left button through the window, as a player's - the mouse look keeps the pointer in the
// middle and every mouse message carries the middle (GameWndProc), so the game's own click code takes the portal its hover has. The press only
// with the game's window in front; the release whatever the focus is, so a button never stays down.
bool PortalClick(bool down) {
    if (down && !GameFocused()) return false;
    SendLeftButton(down);
    return true;
}

// Once a frame (VrFrame, the frame's thread): [input] portal_look_horizon and [input] portal_auto, flat only (from sajmonsoft's flat fork).
void FlatPortalFrame() {
    const bool flat = g_set.platform.load() == 0;
    const bool lookWanted = flat && g_set.portalLookHorizon.load();
    const bool autoOn = flat && g_set.portalAuto.load();
    static portalauto::Arrival arrival;
    static portalauto::Machine machine;
    static bool idle = true;
    if (!lookWanted && !autoOn && idle) return;
    float look[3];
    const bool have = LookAtNow(look) && g_inWorld.load();
    // the look-at is the hero as drawn: his place on the grid is it less its height's push (flat_ground.h)
    const float gx = have ? look[0] - look[1] * flatground::kShear : 0.0f, gz = have ? look[2] - look[1] * flatground::kShear : 0.0f;
    const uint32_t gen = g_areaSetGen.load();
    if (arrival.Step(GetTickCount64(), gen, have, gx, gz) && lookWanted && InsideFree()) {
        const float to = std::clamp(portalauto::HorizonMousePitch(g_gameDownDeg.load(), g_set.pitch.load()), -90.0f, 90.0f);
        if (g_mousePitch.load() != to) {
            g_mousePitch.store(to);
            g_gen.fetch_add(1);
            Log("vrcam: flat: through a portal or a waypoint - the look level at the horizon ([input] portal_look_horizon)");
        }
    }
    portalauto::In in;
    in.now = GetTickCount64();
    const int view = ViewNow();
    in.wanted = autoOn && g_enabled.load() && (view == 2 || view == 3) && have && !gamestate::MenuOpen() && ShooterActive() && KeyMoveMode();
    auto held = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
    in.moving = in.wanted && GameFocused() && !ChatOpen() && (held('W') || held('A') || held('S') || held('D'));
    in.areaGen = gen;
    in.heroOk = have;
    in.heroX = gx;
    in.heroZ = gz;
    in.radius = g_set.portalAutoRadius.load();
    in.delayMs = g_set.portalAutoDelay.load();
    if (in.wanted) {   // the nearest portal on the grid (the grid has no height): an object of kind 4, its static path's tile
        const uintptr_t table = d2rsig::Addr(RVA_FLAT_UNITS);
        auto read = [](void* d, uintptr_t src, size_t n) { return src >= 0x10000 && SafeRead(d, (const void*)src, n); };
        float best = 1e30f;
        unitscan::EachUnit(read, table, 2, [&](uintptr_t, const unitscan::Unit& u) {
            if (!PortalClass(unitscan::Field32(u, unitscan::kOffClass))) return true;
            uint32_t tile[2] = {};
            if (!read(tile, unitscan::Field64(u, unitscan::kOffPath) + 0x10, sizeof tile) || tile[0] > 200000u || tile[1] > 200000u) return true;
            const float px = 2.0f * tile[0] + 1.0f, pz = 2.0f * tile[1] + 1.0f, d2 = (px - gx) * (px - gx) + (pz - gz) * (pz - gz);
            if (d2 < best) { best = d2; in.portalX = px; in.portalZ = pz; }
            return true;
        });
        in.portal = best < 1e29f;
        in.dist = in.portal ? sqrtf(best) : 0.0f;
    }
    in.hoverIsPortal = g_portalHover.load(std::memory_order_acquire);
    const portalauto::Out o = machine.Step(in);
    if (o.force) {
        g_portalForceX.store(o.forceX);
        g_portalForceZ.store(o.forceZ);
        if (!g_portalForce.exchange(true, std::memory_order_release)) g_portalHover.store(false);   // a new aim: the hover found from now on
    } else {
        g_portalForce.store(false, std::memory_order_release);
        g_portalHover.store(false);
    }
    if (o.up) PortalClick(false);
    if (o.down) {
        PortalClick(true);
        static int told = 0;
        if (told++ < 5) LogF("vrcam: flat: walked into a portal (%.1f away) - entered by the game's own click ([input] portal_auto)", in.dist);
    }
    idle = !o.force && machine.stage() == portalauto::Machine::Stage::Idle;
}

// [debug] bow_latency (vr/bow_latency.h): for every shot the draw gesture releases and every throw the throw gesture presses, one log line - the wire's press, the pad's button, the hero's
// mode, the body's turn, the animation row the action runs on, the server's missile create (throw_born.cpp's read-only look) and the first missile the client shows - as ms after
// the release. Read only; the camera thread and the input hook leave their stamps (BowLatencyPad, g_latYaw), this runs on the update thread.
void BowLatencyTick() {
    static bowlatency::Tracker tr;
    static bool wasOn = false;
    static unsigned shots = 0, throws = 0;
    static double yawStamp = 0.0, lastCreate = 0.0;
    static uint32_t known[96];
    static size_t nKnown = 0;
    static ULONGLONG scanAt = 0;
    const bool on = g_set.bowLatency.load();
    if (on != wasOn) {
        wasOn = on;
        throwborn::Observe(on);
        tr.Cancel();
        shots = bowdrawwire::ShotCount();
        throws = throwwire::ShotCount();
        if (on) Log("bow latency: on ([debug] bow_latency) - one line for every bow shot of the draw gesture and every throw of the throw gesture: ms after the release to the wire's press, the pad's button, the hero's attack or throw mode, the body's turn, the animation row, the server's missile create and the client's missile");
    }
    if (!on) return;
    throwborn::Install();   // (the hook goes in for the read-only look too, once the game's code is there)
    const double now = throwborn::NowS();
    if (const double s = g_latYawS.load(); s != yawStamp) { yawStamp = s; tr.Yaw(s, g_latYaw.load()); }
    tr.HeroMode(now, HeroAnimMode());
    auto tell = [&](const bowlatency::Result& r) {
        char line[1200];
        bowlatency::Line(r, throwborn::Observing(), line, sizeof line);
        Log(line);
    };
    auto begin = [&](const char* kind, double rel, double press) {   // a release: the clock starts at it
        if (tr.Active()) tell(tr.Finish());
        tr.Begin(rel, kind);
        tr.WirePress(press);
        lastCreate = rel - 0.001;
        meleetouch::MissileView mv[96];   // the missiles the client has now: a new one is the arrow's / the thrown thing's
        const size_t n = meleetouch::MissileScan(mv, 96);
        nKnown = std::min<size_t>(n, 96);
        for (size_t i = 0; i < nKnown; ++i) known[i] = mv[i].id;
        scanAt = 0;
    };
    if (const unsigned count = bowdrawwire::ShotCount(); count != shots) {
        shots = count;
        double rel = 0.0, press = 0.0;
        bowdrawwire::ShotTimes(&rel, &press);
        begin("bow", rel, press);
    }
    if (const unsigned count = throwwire::ShotCount(); count != throws) {   // a throw's game button pressed by the throw gesture (a potion, a javelin, a knife, an axe)
        throws = count;
        double rel = 0.0, press = 0.0;
        throwwire::ShotTimes(&rel, &press);
        begin("throw", rel, press);
    }
    if (!tr.Active()) return;
    const double t0 = tr.Started();
    if (const double d = g_latPadDownS.load(); d >= t0) {
        tr.PadA(d, true);
        if (const double u = g_latPadUpS.load(); u >= d) tr.PadA(u, false);
    }
    {
        throwborn::Made made[8];
        const size_t n = throwborn::CreatesSince(lastCreate + 1e-6, made, 8);
        for (size_t i = 0; i < n; ++i) {
            bowlatency::AnimInfo anim;
            if (made[i].animRead) bowlatency::ParseAnimRecord(made[i].anim, made[i].animPtr, &anim);
            tr.Created(made[i].at, made[i].missileClass, made[i].flags, anim);
            lastCreate = std::max(lastCreate, made[i].at);
        }
    }
    if (tr.NeedClientAnim() && bowlatency::IsActionMode(HeroAnimMode())) {   // the hero's unit has gone into its attack: the animation record it points at (+0x70)
        uintptr_t hero = (uintptr_t)g_heroUnit.load();
        if (!hero || !IsHeroUnit((void*)hero)) hero = (uintptr_t)g_heroSeen.load();
        uintptr_t rec = 0;
        uint8_t bytes[bowlatency::kAnimRecordSize];
        bowlatency::AnimInfo anim;
        if (hero && SafeRead(&rec, (const void*)(hero + 0x70), sizeof rec) && rec > 0x10000 && SafeRead(bytes, (const void*)rec, sizeof bytes))
            bowlatency::ParseAnimRecord(bytes, rec, &anim);
        tr.ClientAnim(anim);
    }
    if (GetTickCount64() - scanAt >= 30) {
        scanAt = GetTickCount64();
        meleetouch::MissileView mv[96];
        const size_t n = meleetouch::MissileScan(mv, 96);
        for (size_t i = 0; i < n; ++i) {
            bool old = false;
            for (size_t k = 0; k < nKnown && !old; ++k) old = known[k] == mv[i].id;
            if (!old) { tr.ClientMissile(now, (int)mv[i].cls); break; }
        }
    }
    if (tr.Over(now)) tell(tr.Finish());
}

// [debug] bow_latency, for the melee press->hit line (vr/melee_touch.cpp): the animation row the hero's unit points at - which row his attack really runs on and where its action frame is
// (with [hands] quick_strike on it says 'the flag 1 at frame 1'). Read only; empty when bow_latency is off.
size_t MeleeAnimNote(char* out, size_t n) {
    if (!g_set.bowLatency.load()) return 0;
    uintptr_t hero = (uintptr_t)g_heroUnit.load();
    if (!hero || !IsHeroUnit((void*)hero)) hero = (uintptr_t)g_heroSeen.load();
    uintptr_t rec = 0;
    uint8_t bytes[bowlatency::kAnimRecordSize];
    bowlatency::AnimInfo anim;
    if (!hero || !SafeRead(&rec, (const void*)(hero + 0x70), sizeof rec) || rec <= 0x10000 || !SafeRead(bytes, (const void*)rec, sizeof bytes)) return 0;
    bowlatency::ParseAnimRecord(bytes, rec, &anim);
    return bowlatency::AnimText(anim, out, n);
}

// [debug] melee_press_trace (vr/press_trace.h): one line for every attack press of hit by touch - what the plugin held at the press, the game's pad polls while it was live,
// its two reads of the slot's skill, its target lookups and what they were given, the resolver's verdict, the hero's mode and animation row, and the hit counter's
// verdict - so the step at which a press that never strikes stops is in the log (the Amazon's sword, the author 2026-10-20). The first 12 presses of a run are told
// whatever the key says, every one with it. Read only: nothing a press does is changed. The notes come from the pad's poll, the game's UI thread and the timer: one lock.
SRWLOCK g_ptLock = SRWLOCK_INIT;
presstrace::Recorder g_ptRec;
std::atomic<unsigned> g_ptTold{0};
constexpr unsigned kPressTraceAlways = 12;

void PressTraceTell(const presstrace::Trace& t) {
    char b[1600];
    presstrace::Format(t, b, sizeof b);
    Log(b);
    if (g_ptTold.fetch_add(1) + 1 == kPressTraceAlways && !g_pressTraceKey.load())
        Log("melee press: the first 12 presses told - [debug] melee_press_trace=1 tells every one");
}
// Due: told outside the lock.
void PressTraceDue(ULONGLONG now) {
    presstrace::Trace t;
    AcquireSRWLockExclusive(&g_ptLock);
    const bool due = g_ptRec.Due(now, &t);
    g_ptActive.store(g_ptRec.Active());
    ReleaseSRWLockExclusive(&g_ptLock);
    if (due) PressTraceTell(t);
}
bool HeroAnimRow(bowlatency::AnimInfo* anim) {
    uintptr_t hero = (uintptr_t)g_heroUnit.load();
    if (!hero || !IsHeroUnit((void*)hero)) hero = (uintptr_t)g_heroSeen.load();
    uintptr_t rec = 0;
    uint8_t bytes[bowlatency::kAnimRecordSize];
    if (!hero || !SafeRead(&rec, (const void*)(hero + 0x70), sizeof rec) || rec <= 0x10000 || !SafeRead(bytes, (const void*)rec, sizeof bytes)) return false;
    return bowlatency::ParseAnimRecord(bytes, rec, anim);
}

// meleetouch's press (the timer's thread): what the plugin holds now.
void PressTraceBegin(uint32_t monster, int hand) {
    if (!g_pressTraceKey.load() && g_ptTold.load() >= kPressTraceAlways) return;
    presstrace::Facts f;
    f.weaponSet = gamestate::WeaponSet();
    f.weaponClass = gamestate::WeaponClass();
    f.weaponType = gamestate::WeaponType();
    f.codeR = gamestate::HandCode(0);
    f.codeL = gamestate::HandCode(1);
    {
        static wheel::Snapshot snap;   // (12 KB: not on the stack; the timer's thread only)
        static uint32_t rev = 0;
        if (wheelgame::Latest(&snap, &rev) && snap.ready) {
            f.bank = snap.bank;
            if (snap.slot[bowquiver::kAttackSlot].known) f.slotA = snap.slot[bowquiver::kAttackSlot].id;
        }
    }
    f.armedMelee = meleearm::On() ? meleearm::PressSkill() : -1;
    f.bowShot = bowquiverwire::ShotSkill();
    int32_t chosen = -1;
    bool ranged = false;
    if (quiverwire::Chosen(&chosen, &ranged)) f.javChosen = chosen;
    f.bowInHand = BowInHand();
    f.bowOwned = bowdrawwire::Owned();
    f.bowAimLive = bowdrawwire::AimLive();
    f.aimByVector = AimByVector();
    f.throwLive = GestureAimLive();
    f.bowMode = g_set.bowMode.load();
    f.cutMs = animcut::CutMs(animcut::Kind::Melee);
    const int mode = HeroAnimMode();
    presstrace::Trace prev;
    AcquireSRWLockExclusive(&g_ptLock);
    const bool handed = g_ptRec.Begin(monster, hand, GetTickCount64(), f, mode, &prev);
    g_ptActive.store(true);
    ReleaseSRWLockExclusive(&g_ptLock);
    if (handed) PressTraceTell(prev);
}
void PressTraceOutcome(bool hit, double ms) {
    AcquireSRWLockExclusive(&g_ptLock);
    g_ptRec.Outcome(hit, ms);
    ReleaseSRWLockExclusive(&g_ptLock);
    PressTraceDue(GetTickCount64());
}
// The game's poll of pad 0 (its thread), the buttons as it gets them.
void PressTracePoll(uint16_t buttons) {
    const ULONGLONG now = GetTickCount64();
    const bool still = gamecmd::Pressed(0x24);   // the game's Stand Still
    const int mode = HeroAnimMode();
    AcquireSRWLockExclusive(&g_ptLock);
    g_ptRec.Poll(now, buttons, still, mode);
    const bool wantsAnim = g_ptRec.WantsAnim();
    ReleaseSRWLockExclusive(&g_ptLock);
    if (wantsAnim) {
        bowlatency::AnimInfo a;
        if (HeroAnimRow(&a)) {
            AcquireSRWLockExclusive(&g_ptLock);
            g_ptRec.Anim(a.name, (int)a.frames, a.flagIdx);
            ReleaseSRWLockExclusive(&g_ptLock);
        }
    }
    PressTraceDue(now);
}
void PressTraceSlot(int site, int slot, int bound, int answer) {
    AcquireSRWLockExclusive(&g_ptLock);
    g_ptRec.Slot(site, slot, bound, answer);
    ReleaseSRWLockExclusive(&g_ptLock);
}
void PressTraceLookup(int which, void* unit, bool ours) {
    uint32_t id = 0;
    if (unit && !SafeRead(&id, (const char*)unit + 8, sizeof id)) id = 0;   // the unit's id
    AcquireSRWLockExclusive(&g_ptLock);
    g_ptRec.Lookup(which, GetTickCount64(), id, ours);
    ReleaseSRWLockExclusive(&g_ptLock);
}
void PressTraceCast(int skill, bool directed) {
    AcquireSRWLockExclusive(&g_ptLock);
    g_ptRec.Cast(skill, directed);
    ReleaseSRWLockExclusive(&g_ptLock);
}

// [bow] quick_release and [hands] quick_throw (vr/anim_cut.cpp): ticked about 20 times a second from the update thread - the action-frame flag of the hero's bow attack rows is moved
// while he is in VR first person with a bow in hand, and that of his throw rows (the Throw skill's potions, javelins, knives, axes - whatever weapon he holds) while he is in VR first
// person; put back the moment any of that stops. The hero's unit mode tells whether he is mid-animation (a flag is not moved under one).
void AnimCutTick() {
    static ULONGLONG at = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - at < 50) return;
    at = now;
    const bool inF4 = g_enabled.load() && g_inWorld.load() && FullBody();
    const bool acting = bowlatency::IsActionMode(HeroAnimMode());
    animcut::Tick(animcut::Kind::Bow, inF4 && gamestate::WeaponType() == D2RVR_TYPE_BOW, skel::HeroClass(), acting, now);
    animcut::Tick(animcut::Kind::Melee, inF4, skel::HeroClass(), acting, now);   // [hands] quick_strike: the class's melee attack rows (never the bow's or the throw's)
    // a throwing potion keeps its own timing (the game's missile comes at its action frame and the explosion about when our potion lands): the rows are put back while one is in the hand
    const bool potionInHand = gamestate::HandIsThrowPotion((int)gamestate::WeaponHand());
    animcut::Tick(animcut::Kind::Throw, animcut::ThrowCutWanted(inF4, potionInHand, g_set.quickThrowPotions.load()), skel::HeroClass(), acting, now);
}

// [debug] throw_trace (vr/throw_trace.h): for 5 s after every released throw, every ~40 ms, the hero's mode and the client's missiles (read only) are diffed
// into log lines: the hero's throw animation, the potion's missile (whether the game made one at all, how far it flew, when it vanished), and whether any
// effect missile (the explosion) appeared where it ended.
void ThrowTraceTick() {
    static throwtrace::Tracker tracker;
    static uint64_t armedFor = 0;
    static ULONGLONG lastRun = 0;
    if (!g_set.throwTrace.load()) { tracker.Stop(); return; }
    const uint64_t rel = throwwire::LastThrowTickMs();
    if (rel && rel != armedFor && GetTickCount64() - rel < 2000) {
        armedFor = rel;
        tracker.Arm((double)rel / 1000.0);
        Log("throw trace: a throw was released - watching the hero and the missiles for 5 s ([debug] throw_trace)");
    }
    if (!tracker.Active()) return;
    const ULONGLONG now = GetTickCount64();
    if (now - lastRun < 40) return;
    lastRun = now;
    meleetouch::MissileView mv[96];
    throwtrace::Missile mm[96];
    const size_t n = meleetouch::MissileScan(mv, 96);
    for (size_t i = 0; i < n; ++i) {
        mm[i].id = mv[i].id; mm[i].cls = mv[i].cls; mm[i].mode = mv[i].mode; mm[i].flags = mv[i].flags;
        mm[i].x = mv[i].x; mm[i].z = mv[i].z; mm[i].life = mv[i].life;
    }
    throwtrace::Snapshot snap;
    snap.t = (double)now / 1000.0;
    uint32_t hm = 0;
    snap.heroModeKnown = GestureHeroMode(&hm);
    snap.heroMode = hm;
    snap.heroKnown = meleetouch::UnitPlace((uintptr_t)g_heroUnit.load(), &snap.hx, &snap.hz);
    snap.m = mm;
    snap.n = n;
    throwtrace::Line lines[24];
    const size_t k = tracker.Step(snap, lines, 24);
    for (size_t i = 0; i < k; ++i) Log(lines[i].text);
}

// [gameplay] melee_touch (melee_touch.cpp): what the module cannot know - a game area, first person on the controllers, no
// panel - and the weapon in hand; it does the rest from the skeleton hook's posed weapon and hand samples.
void MeleeTouchTick() {
#if D2RVR_FIRST_PERSON
    ThrowTraceTick();
    BowLatencyTick();   // [debug] bow_latency
    AnimCutTick();      // [bow] quick_release, [hands] quick_throw
    {   // [hands] throw_autoaim_deg: the monsters near the hero and the server's verdicts, while a thrown weapon is in hand (the touch stands down for it)
        const bool inWorld = g_enabled.load() && g_inWorld.load() && !gamestate::MenuOpen();
        // [bow] pitch_aim: the same poll for a bow or crossbow in hand (F4), and the scene its shot is decided in - the models near read while the
        // string is drawn, a shot is under way or the attack is aimed by the hand (a crossbow)
        const bool bowWant = bowpitchwire::On() && BowInHand() && inWorld && viewgate::F4();
        // [gameplay] leap_by_gaze: and for a Barbarian (F4), whose Leap Attack goes at the monster the gaze is on
        const bool leapWant = g_set.leapByGaze.load() && skel::HeroClass() == 4 && viewgate::F4();
        const bool want = ((meleetouch::ThrowWanted() && ThrownInHand()) || bowWant || leapWant) && inWorld;
        meleetouch::ThrowPoll(want ? (uintptr_t)g_heroUnit.load() : 0);
        const float heroRender[3] = {g_heroPosNow[0], g_heroPosNow[1], g_heroPosNow[2]};
        bowpitchwire::Poll(bowWant, bowdrawwire::Drawing() || bowdrawwire::AimLive() || g_aHeld.load() || g_skillHeld.load(), (uintptr_t)g_heroUnit.load(), heroRender,
                           std::max(1.0f, ViewHeight()) / UserEyeHeightM());
        arrowflightwire::Tick();   // [bow] arrow_flight: the arrows done with let go of
        // which pad button holds the game's Throw skill: the wheel's reads of the controller's slots, kept going for it when the wheel is off
        // (and for the quiver: the same reads, at the quick cadence while the string is drawn)
        if ((throwwire::WantsSlots() || quiverwire::WantsSkills() || bowquiverwire::WantsSlots() || pointwire::WantsSlots()) && !skillwheel::Enabled())
            wheelgame::Tick(true, bowquiverwire::Drawing());   // ([gameplay] teleport_circle: RT's and LT + RT's skill)   // (the quiver reads the learned skills from it too)
    }
    meleetouch::Frame f;
    f.ok = g_enabled.load() && g_inWorld.load() && !ThirdPerson() && !Overhead() && g_eyeOk.load() && !gamestate::MenuOpen() && ArmsMode() == 2;
    f.weaponType = gamestate::WeaponType();
    f.twoHandsOn = (gamestate::TwoHanded() & D2RVR_TWO_HANDS_ON) != 0;
    f.twoHanded = (gamestate::TwoHanded() & (D2RVR_TWO_HANDED | D2RVR_TWO_HANDED_KNOWN)) == (D2RVR_TWO_HANDED | D2RVR_TWO_HANDED_KNOWN);
    f.epoch = g_gen.load();
    f.session = gamestate::LocalPlayer();
    f.upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
    {   // the weapon as drawn: the view the skeleton hook's bones are hung in (the red line's frame), and what each hand's slot holds
        AcquireSRWLockShared(&g_stickLock); const StickFrame sf = g_stickFrame; ReleaseSRWLockShared(&g_stickLock);
        f.headOk = sf.valid;
        for (int i = 0; i < 3; ++i) { f.camR[i] = sf.camR[i]; f.camF[i] = sf.camF[i]; f.eyeWorld[i] = g_eyeWorld[i].load(); }
        f.kind[0] = gamestate::HandKind(0);
        f.kind[1] = gamestate::HandKind(1);
        f.code[0] = gamestate::HandCode(0);
        f.code[1] = gamestate::HandCode(1);
        f.thrustClass[0] = gamestate::HandThrust(0);   // [gameplay] melee_thrust: the item's class in the game's data (1ht, 2ht thrust)
        f.thrustClass[1] = gamestate::HandThrust(1);
    }
    // [weight] (vr/weight.cpp): the weapon is carried by the hand that holds it; let go (falling, lying, flying back) it strikes nothing
    f.weightPlaced = weight::Placed();
    f.weightCarrying = weight::CarryingHand();
    f.secondLetGo = weight::SecondLetGo();   // (a weapon in each hand, the second let go: its blade strikes nothing)
    f.secondHand = weight::SecondHand();     // (... and which hand swings it: after the hand-over, the first's)
    f.heroMode = g_set.strikeByForce.load() ? HeroAnimMode() : -1;   // [weight] strike_force_stuck 1: a repeat waits for the game's attack to end
    meleetouch::Tick(f);
    {   // [weight] grab_bodies: the bodies for the grab when the touch keeps none (a bow, bare hands, hit by touch off) - the hero where the bow's poll has him
        const float heroRender[3] = {g_heroPosNow[0], g_heroPosNow[1], g_heroPosNow[2]};
        meleetouch::GrabTick(f.ok, heroRender, f.upm);
        meleetouch::ItemsTick(f.ok, heroRender, f.upm);   // [weight] grab_items: the items lying near
        meleetouch::BarrelsStep();   // [gameplay] melee_touch_barrels: the press on a barrel the swing went through, held until it breaks
    }
    {   // [weight] knock_props (vr/knock_props.h): the level's props near the hero, the blades hit by touch just drew, the physics thread's knocks
        knockprops::Frame kf;
        skel::PosedWeaponPose pw;
        kf.ok = f.ok && knockprops::Wanted() && skel::PosedWeapon(pw) && pw.worldEpoch == f.epoch;
        if (kf.ok) for (int i = 0; i < 3; ++i) kf.hero[i] = pw.hero[i];
        kf.upm = f.upm;
        kf.session = f.session;
        kf.steelKg = weight::SteelKg();
        kf.hands = f.twoHandsOn ? 2 : 1;
        knockprops::Tick(kf);
    }
#endif
}

}  // namespace vrc
