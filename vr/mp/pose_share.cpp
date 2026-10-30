// vr/mp/pose_share.cpp - [multiplayer] pose_share: our head, controllers, body and held things to the other players, and theirs drawn and
// met (cut from vr/vrcam.cpp, docs/plan_modules.md M11; the network side is vr/pose_share_wire.cpp). What it gives: vr/mp/pose_share.h.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// [multiplayer] pose_share (vr/pose_share_wire.h): on at the start only (the channel is registered then, D2RLoaderLoadPlugin); the rest live. The echo,
// rates, delay, burst, draw and our own hero's legs stay in [debug]. The four moved (2026-10-26, the author: "this must not be debug, but multiplayer")
// are on when missing; [debug] is no longer read for them (an old ini's 0 there was the debug tab's default, never a choice).
// [multiplayer] pose_share_bodies: our body and weapons go out with the pose, and the others' are drawn and met (live); pose_share_echo_ahead_cm: the echo
// of our own pose stood that far ahead, facing us, as a second player to meet on one PC (0: on our own hero, drawn only)
std::atomic<bool> g_poseBodies{false};
std::atomic<float> g_poseEchoAheadM{0.0f};
// [multiplayer] pose_share_hero: the other players' hero models posed from their poses (vr/remote_pose.h) - the head, the arms on the controllers, the weapon in hand; any view
std::atomic<bool> g_poseHero{false};
// [multiplayer] pose_share_legs: with pose_share_hero, the other heroes' legs step along their travel (the gait library d2r_vr_gait.dll), their whole body to their look
std::atomic<bool> g_poseLegs{false};
// [debug] pose_share_draw: the other players drawn as debug glass (the markers, the bodies, the weapons) - off by default since the 2026-10-26 test ("the grey
// debug doll has to go"); what our weapon meets ([multiplayer] pose_share_bodies) is the same whether it is drawn or not
std::atomic<bool> g_poseDraw{false};
std::atomic<bool> g_poseLegsSelf{false};   // [debug] pose_share_legs_self: the gait on our own hero (a one-PC test of the legs)
// [multiplayer] pose_share_held: what our hands have (a potion, an item taken off the ground, a prop of the level) goes out with the pose - in the hand, flying, at rest -
// and the other players' is drawn: copies of the items in their hands and in flight, their props put where they have them (visual only; live)
std::atomic<bool> g_poseHeld{false};
void PoseShareConfigure() {
    {
        const bool self = IniB(L"debug", L"pose_share_legs_self", false);
        if (g_poseLegsSelf.exchange(self) != self)
            LogF("vrcam: pose share - our own hero's legs %s ([debug] pose_share_legs_self)", self ? "by the gait (a test: see him in F2 / F3)" : "as before");
    }
    {
        const bool draw = IniB(L"debug", L"pose_share_draw", false);
        if (g_poseDraw.exchange(draw) != draw)
            LogF("vrcam: pose share - the other players' debug glass (markers, bodies, weapons) %s ([debug] pose_share_draw)", draw ? "drawn" : "not drawn");
    }
    {
        const bool legs = IniB(L"multiplayer", L"pose_share_legs", true);
        if (g_poseLegs.exchange(legs) != legs)
            LogF("vrcam: pose share - the other heroes' legs %s ([multiplayer] pose_share_legs)", legs ? "step along their travel, their pelvis to their look (the gait library)" : "as the game animates them");
    }
    {
        const bool held = IniB(L"multiplayer", L"pose_share_held", true);
        if (g_poseHeld.exchange(held) != held)
            LogF("vrcam: pose share - what the hands have (potions, items, props; in the hand and thrown) %s ([multiplayer] pose_share_held)",
                 held ? "goes out with the pose, and the others' is drawn" : "is not shared");
    }
    {
        const bool hero = IniB(L"multiplayer", L"pose_share_hero", true);
        if (g_poseHero.exchange(hero) != hero)
            LogF("vrcam: pose share - the other players' heroes %s ([multiplayer] pose_share_hero: their heads, their arms on their controllers, their weapons in hand)",
                 hero ? "posed from their poses" : "as the game animates them");
    }
    {
        const bool bodies = IniB(L"multiplayer", L"pose_share_bodies", true);
        meleetouch::SetPlayerBodies(bodies);   // the other players among hit by touch's bodies, as NPCs (their own skeletons' limbs), whether they send a pose or not
        const float ahead = std::clamp(IniF(L"debug", L"pose_share_echo_ahead_cm", 0.0f), 0.0f, 500.0f) * 0.01f;
        const bool wasBodies = g_poseBodies.exchange(bodies);
        const float wasAhead = g_poseEchoAheadM.exchange(ahead);
        if (wasBodies != bodies || wasAhead != ahead)
            LogF("vrcam: pose share - bodies and weapons %s ([multiplayer] pose_share_bodies; drawn, and met with [weight] monster_bodies on), the echo %s", bodies ? "on" : "off",
                 ahead > 0.0f ? "stood ahead of us" : "on our own hero");
    }
    posewire::Config pc;
    pc.on = IniB(L"multiplayer", L"pose_share", true);
    pc.hz = (int)std::clamp(IniF(L"debug", L"pose_share_hz", (float)poseshare::kSendHzDefault), 5.0f, 90.0f);
    pc.echo = IniB(L"debug", L"pose_share_echo", false);
    pc.burst = IniB(L"debug", L"pose_share_burst", false);
    pc.delayMs = (uint32_t)std::clamp(IniF(L"debug", L"pose_share_delay_ms", (float)poseshare::kDelayMsDefault), 0.0f, 500.0f);
    posewire::Configure(pc);
}

namespace fx {

// ---- [multiplayer] pose_share (vr/pose_share.h, vr/pose_share_wire.cpp): our head and controllers to the other players, theirs as three markers ----
// Our pose is sent only while we play VR F4. What we draw of the OTHER players (their markers, capsules, weapons) is drawn in every view our camera
// draws - VR or flat F2 / F3 (not the game's own F1): it is someone else's VR shown, no control of ours (the 2026-10-26 test: a flat PC2 saw none of
// it). Our weapon meets their bodies only in VR F4 (BodiesWithPlayers). Visual only.
bool PoseShareDrawWanted() { return posewire::Registered() && g_enabled.load() && g_inWorld.load(); }

// The players' places on the grid, read on the update thread (meleetouch::PlayersNow, read-only) for the draw.
struct PoseSpots {
    uint32_t own = 0;
    bool ownOk = false;
    float ownX = 0.0f, ownZ = 0.0f;
    std::array<meleetouch::PlayerSpot, 16> p{};
    size_t n = 0;
};
SRWLOCK g_poseSpotsLock = SRWLOCK_INIT;
PoseSpots g_poseSpots;
std::atomic<uint32_t> g_poseDrawn{0}, g_poseAway{0};   // remotes drawn / not in our area, since the last line

// Our hero's client unit id: the unit the aim works on, else the one seen at the look-at, else the SDK's local player when it is the
// unit itself (as HeroAnimMode). 0 = not known.
uint32_t OwnHeroUnitId() {
    uintptr_t hero = (uintptr_t)g_heroUnit.load();
    if (!hero || !IsHeroUnit((void*)hero)) hero = (uintptr_t)g_heroSeen.load();
    if (!hero || !IsHeroUnit((void*)hero)) {
        const uintptr_t p = (uintptr_t)gamestate::LocalPlayer();
        hero = p > 0x10000 && IsHeroUnit((void*)p) ? p : 0;
    }
    uint32_t id = 0;
    return hero && SafeRead(&id, (const void*)(hero + 8), sizeof id) ? id : 0;
}

// Our pose in the hero's frame (vr/pose_share.h): the hands as the shared block has them (from the head, in the frame of its turn - the
// frame's own axes, as HandRay turns them into the world), the head where the hands hang from (g_eyeWorld) over the hero's feet (the
// look-at), its pitch and roll from the block.
// [multiplayer] pose_share_held: hand `side`'s prop of the level for the pose (format 7) - the one it holds, or the one it let go of while that flies and for
// kHeldRestMs once it lies (the others keep it where it lay). False: none.
constexpr ULONGLONG kHeldRestMs = 2000;
bool PropForPose(int side, const float L[3], float heading, float upm, poseshare::Pose::Held* hd) {
    static uint32_t last[2] = {};
    static ULONGLONG restSince[2] = {};
    bool holding = false;
    const uint32_t id = grabwire::PropOf(side, &holding);
    knockprops::PropShot ps;
    if (!id || !knockprops::PropNow(id, &ps)) return false;
    if (id != last[side]) { last[side] = id; restSince[side] = 0; }
    const uint8_t state = holding ? poseshare::kInHand : ps.moving ? poseshare::kFlying : poseshare::kAtRest;
    const ULONGLONG now = GetTickCount64();
    if (state != poseshare::kAtRest) restSince[side] = 0;
    else if (!restSince[side]) restSince[side] = now;
    else if (now - restSince[side] > kHeldRestMs) return false;
    float scale = 1.0f;
    if (!poseshare::HeldFromWorld(ps.world, L, heading, upm, &hd->pose, &scale)) return false;
    hd->what = poseshare::kHeldProp;
    hd->state = state;
    hd->nameHash = poseshare::NameHash(ps.name);
    poseshare::ToHero(ps.home, L, heading, upm, hd->home);
    return true;
}

bool PoseShareOwn(poseshare::Pose* p) {
    if (!g_enabled.load() || !g_inWorld.load() || !FullBody() || ThirdPerson() || !g_eyeOk.load()) return false;
    const D2RVR_Shared* sh = Shared();
    if (!sh || sh->version != D2RVR_SHARED_VERSION || !sh->headValid) return false;
    AcquireSRWLockShared(&g_stickLock); const StickFrame f = g_stickFrame; ReleaseSRWLockShared(&g_stickLock);
    if (!f.valid) return false;
    float L[3];
    AcquireSRWLockShared(&g_lookLock); memcpy(L, g_lookAt, sizeof L); ReleaseSRWLockShared(&g_lookLock);
    const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
    const float hang[3] = {g_eyeWorld[0].load(), g_eyeWorld[1].load(), g_eyeWorld[2].load()};
    *p = poseshare::Pose{};
    p->heading = poseshare::HeadingOf(f.camF);
    p->eyeM = UserEyeHeightM();
    poseshare::ToHero(hang, L, p->heading, upm, p->head.p);
    poseshare::HeadQuat(sh->pitchValid ? sh->headPitchDeg : 0.0f, sh->rollValid ? sh->headRollDeg : 0.0f, p->head.q);
    p->flags = poseshare::kHead | poseshare::kF4;
    const float* hp[2] = {sh->rightHand, sh->leftHand};
    const float* hq[2] = {sh->rightRot, sh->leftRot};
    poseshare::Joint* j[2] = {&p->right, &p->left};
    for (int h = 0; h < 2; ++h) {
        if (!(sh->handsValid & (1u << h))) continue;
        for (int i = 0; i < 3; ++i) j[h]->p[i] = p->head.p[i] + hp[h][i];
        memcpy(j[h]->q, hq[h], sizeof j[h]->q);
        p->flags |= h == 0 ? poseshare::kRight : poseshare::kLeft;
    }
    {   // format 3: the camera's yaw in our hero's model space, as our own arms were posed by it - the others pose our hero by it ([multiplayer] pose_share_hero)
        float yaw = 0.0f;
        if (skel::HeroYawInModel(&yaw, 200)) { p->yawInModel = yaw; p->flags |= poseshare::kModelYaw; }
    }
    {   // format 5: each shown weapon slot's attach bone as drawn - the weapon's own place and turn against the head, in a hand, falling, lying, on the back - so
        // the others hang their copy of it exactly there (the author: "the weapon's absolute place against the headset"); which hands hold it, and how hard each grips
        float o[3], rows[3][3];
        bool shown[2] = {false, false};
        for (int sl = 0; sl < 2; ++sl) {
            if (!skel::DrawnAttach(sl, o, rows, 200)) continue;
            bool mirrored = false;
            if (!poseshare::QuatOfRows(rows[0], rows[1], rows[2], p->attach[sl].q, &mirrored)) continue;
            for (int i = 0; i < 3; ++i) p->attach[sl].p[i] = p->head.p[i] + o[i];
            p->flags2 |= (uint8_t)((sl ? poseshare::kAttach1 : poseshare::kAttach0) | (mirrored ? (sl ? poseshare::kMirror1 : poseshare::kMirror0) : 0));
            shown[sl] = true;
        }
        skel::DrawnHand dh;   // format 6: both hands and elbows as drawn, read with the attach bones - the others put his hands where ours are, not by their game's grip
        if (skel::DrawnHands(&dh, 200)) {
            bool fine = true;
            for (int side = 0; side < 2 && fine; ++side) {
                fine = poseshare::QuatOfRows(dh.rows[side][0], dh.rows[side][1], dh.rows[side][2], p->hand[side].q, &p->handMirror[side]);
                for (int i = 0; i < 3; ++i) { p->hand[side].p[i] = p->head.p[i] + dh.hand[side][i]; p->handElbow[side][i] = p->head.p[i] + dh.elbow[side][i]; }
            }
            for (int i = 0; i < 3; ++i) p->headBone[i] = p->head.p[i] + dh.headBone[i];
            p->modelUpm = dh.upm;
            if (fine) p->flags2 |= poseshare::kHands;
        }
        for (int side = 0; side < 2; ++side) {   // a hand on the weapon: the weight model's word where it holds the weapon, else the hand of the slot the weapon is on
            const bool holds = weight::Placed() ? weight::HoldsWeapon(side) : shown[side];
            if (holds && (shown[0] || shown[1])) p->flags2 |= side ? poseshare::kHoldL : poseshare::kHoldR;
        }
        float raw[2];   // the grips as the controllers have them (a grip a gesture takes reads 0 in the shared block: the raw ones first)
        bool grips = weightwire::RawGrips(raw);
        if (!grips && sh->gripMagic == D2RVR_GRIP_MAGIC) { raw[0] = sh->rightGrip; raw[1] = sh->leftGrip; grips = true; }
        if (grips) {
            p->grip[0] = raw[0];
            p->grip[1] = raw[1];
            p->flags2 |= poseshare::kGrips;
        }
    }
    if (g_poseHeld.load()) {   // format 7 ([multiplayer] pose_share_held): what each hand has as drawn - a potion or an item (a copy by its code), else a prop of the level
        for (int side = 0; side < 2; ++side) {
            poseshare::Pose::Held& hd = p->held[side];
            engitem::Shown shown;
            float scale = 1.0f;
            if (engitem::ShownIn(side, &shown) && poseshare::HeldFromWorld(shown.m, L, p->heading, upm, &hd.pose, &scale)) {
                hd.what = poseshare::kHeldItem;
                hd.state = shown.state;
                hd.code = shown.code;
                hd.scale = scale;
            } else if (!PropForPose(side, L, p->heading, upm, &hd)) {
                hd = poseshare::Pose::Held{};
                continue;
            }
            p->flags3 |= side ? poseshare::kHeldL : poseshare::kHeldR;
        }
        if (p->flags3) p->worldUpm = upm;
    }
    if (g_poseBodies.load()) {   // [multiplayer] pose_share_bodies: the shoulders and elbows as drawn (the hands' frame: from the head), the blades as hit by touch swept them
        bool body = true;
        for (int side = 0; side < 2 && body; ++side) {
            float shoulder[3], len = 0.0f, elbow[3], wrist[3], across[3];
            body = skel::ArmReach(side, shoulder, &len) && skel::HeroForearm(side, elbow, wrist, across);
            for (int i = 0; body && i < 3; ++i) { p->shoulder[side][i] = p->head.p[i] + shoulder[i]; p->elbow[side][i] = p->head.p[i] + elbow[i]; }
        }
        if (body) p->flags |= poseshare::kBody;
        meleetouch::OwnBlade bl[2];
        const size_t nb = meleetouch::OwnBlades(bl, 150);
        for (size_t k = 0; k < nb && k < 2; ++k) {
            float butt[3], tip[3];
            for (int i = 0; i < 3; ++i) { butt[i] = bl[k].butt[i] * upm; tip[i] = bl[k].tip[i] * upm; }   // (render metres -> world units)
            poseshare::Pose::Blade& o = p->weapon[k];
            poseshare::ToHero(butt, L, p->heading, upm, o.butt);
            poseshare::ToHero(tip, L, p->heading, upm, o.tip);
            o.radius = bl[k].radius;
            o.kind = (uint8_t)std::min<uint32_t>(bl[k].kind, 127u);
            o.hand = (uint8_t)(bl[k].hand & 1);
            p->flags |= k ? poseshare::kWeapon1 : poseshare::kWeapon0;
        }
    }
    return std::isfinite(p->heading) && std::isfinite(p->head.p[0] + p->head.p[1] + p->head.p[2]) && poseshare::Plausible(*p);
}

// [multiplayer] pose_share_bodies: the other players' capsules in the render world (vr/player_bodies.h), made on the update thread from the poses as
// they are played back - the weight model meets them (BodiesWithPlayers), the draw shows them, our blades knock on their weapons.
bool PoseShareBodiesWanted() { return g_poseBodies.load() && PoseShareDrawWanted(); }
SRWLOCK g_playerBodiesLock = SRWLOCK_INIT;
monsterbodies::Snapshot g_playerBodies;   // players only (n 0: none); render metres over upm
monsterbodies::Snapshot g_playerBodiesDrawn;   // the same plus our own echo when it stands on our own hero (drawn, never met)

// The weight model's bodies (weight::SetBodiesSource): the touch's monsters, and after them the other players' bodies and weapons while asked.
bool BodiesWithPlayers(monsterbodies::Snapshot* out) {
    const bool monsters = meleetouch::Bodies(out);
    if (!out || !PoseShareBodiesWanted() || !FullBody() || ThirdPerson()) return monsters;   // (met in VR F4 only, where our weapon is ours)
    AcquireSRWLockShared(&g_playerBodiesLock);
    const int n = g_playerBodies.n;
    const uint64_t at = g_playerBodies.tickMs;
    const float upm = g_playerBodies.upm;
    if (n > 0 && GetTickCount64() - at <= monsterbodies::kFreshMs) {
        if (!monsters) { out->n = 0; out->tickMs = at; out->upm = upm; }
        for (int i = 0; i < n && out->n < monsterbodies::kMax; ++i) out->b[out->n++] = g_playerBodies.b[i];
    }
    ReleaseSRWLockShared(&g_playerBodiesLock);
    return monsters || (out->n > 0 && GetTickCount64() - out->tickMs <= monsterbodies::kFreshMs);
}

// The capsules made (update thread, 90 a second while asked), and our blades' knocks on the other players' weapons: the game's block sound
// ([weight] clash_sound) and a pulse in the hand, as loud and as hard as they closed. Our own echo stood ahead is a player like the others.
void PoseShareBodiesTick(ULONGLONG now) {
    static ULONGLONG next = 0, toldAt = 0;
    static playerbodies::Clash clash;
    if (now < next) return;
    next = now + 11;
    monsterbodies::Snapshot met, drawn;
    met.tickMs = drawn.tickMs = now;
    const float upmOurs = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
    met.upm = drawn.upm = upmOurs;
    if (PoseShareBodiesWanted()) {
        posewire::Remote r[8];
        const size_t n = posewire::Remotes(r, 8);
        AcquireSRWLockShared(&g_poseSpotsLock); const PoseSpots spots = g_poseSpots; ReleaseSRWLockShared(&g_poseSpotsLock);
        float L[3];
        AcquireSRWLockShared(&g_lookLock); memcpy(L, g_lookAt, sizeof L); ReleaseSRWLockShared(&g_lookLock);
        const float eyeUnits = std::max(1.0f, ViewHeight());
        const float ahead = g_poseEchoAheadM.load();
        for (size_t i = 0; i < n && spots.ownOk; ++i) {
            float feet[3], heading = r[i].pose.heading;
            const bool own = r[i].unit == spots.own;
            if (own && ahead > 0.0f) playerbodies::EchoAhead(L, heading, ahead, upmOurs, feet, &heading);
            else if (own) memcpy(feet, L, sizeof feet);
            else {
                const meleetouch::PlayerSpot* at = nullptr;
                for (size_t j = 0; j < spots.n; ++j) if (spots.p[j].id == r[i].unit) at = &spots.p[j];
                if (!at) continue;
                poseshare::FeetOf(L, spots.ownX, spots.ownZ, at->x, at->z, feet);
            }
            // another player's body is hit by touch's own (meleetouch::SetPlayerBodies: his skeleton's limbs, as an NPC's); his pose gives his weapons. Only our own
            // echo stood ahead (no unit, no rig) has its body from the pose.
            playerbodies::Cap caps[playerbodies::kMaxCaps];
            const int nc = playerbodies::Capsules(r[i].pose, own, true, caps, playerbodies::kMaxCaps);
            const float upmTheirs = eyeUnits / r[i].pose.eyeM;
            if (!own || ahead > 0.0f) playerbodies::AddBodies(caps, nc, (int)i, feet, heading, upmTheirs, upmOurs, &met);
            playerbodies::AddBodies(caps, nc, (int)i, feet, heading, upmTheirs, upmOurs, &drawn);
        }
    }
    AcquireSRWLockExclusive(&g_playerBodiesLock);
    g_playerBodies = met;
    g_playerBodiesDrawn = drawn;
    ReleaseSRWLockExclusive(&g_playerBodiesLock);
    // our blades on their weapons
    meleetouch::OwnBlade bl[2];
    const size_t nb = met.n ? meleetouch::OwnBlades(bl, 100) : 0;
    playerbodies::Clash::Blade ours[2];
    for (size_t k = 0; k < nb; ++k) {
        ours[k].c = parry::Capsule{{bl[k].butt[0], bl[k].butt[1], bl[k].butt[2]}, {bl[k].tip[0], bl[k].tip[1], bl[k].tip[2]}, bl[k].radius};
        ours[k].hand = bl[k].hand;
    }
    playerbodies::Clash::Theirs theirs[16];
    int nt = 0;
    for (int i = 0; i < met.n && nt < 16; ++i)
        if (met.b[i].held == 1)
            theirs[nt++] = {parry::Capsule{{met.b[i].bottom[0], met.b[i].bottom[1], met.b[i].bottom[2]}, {met.b[i].top[0], met.b[i].top[1], met.b[i].top[2]}, met.b[i].radius}, met.b[i].id};
    playerbodies::Clash::Event ev[4];
    const int ne = clash.Step(now, ours, (int)nb, theirs, nt, ev, 4);
    for (int e = 0; e < ne; ++e) {
        const bool sounded = ev[e].loud.heard && clashsound::Play(ev[e].closing, ev[e].loud.gameVolume, "another player's weapon");
        if (ev[e].loud.heard) {
            const haptics::Request q = haptics::ForMotor(haptics::CurrentMotor(), ev[e].loud.pulse, (uint32_t)ev[e].loud.pulseMs);
            if (q.amplitude > 0.0f) haptics::Pulse(haptics::FromSkel(ev[e].hand), q.amplitude, (int)q.durationMs);
        }
        if (now - toldAt >= 1000) {
            toldAt = now;
            LogF("vrcam: pose share - the %s hand's weapon met another player's weapon (capsule %08X) at %.1f m/s: %s", ev[e].hand ? "left" : "right", ev[e].id, ev[e].closing,
                 !ev[e].loud.heard ? "too gentle to be heard or felt" : sounded ? "the game's block sound and a pulse" : "a pulse (the sound off: [weight] clash_sound)");
        }
    }
}

// [multiplayer] pose_share_hero: in a game, any view (the skeleton hook poses every rig, F4 or not)
bool PoseShareHeroWanted() { return posewire::Registered() && g_poseHero.load() && g_inWorld.load(); }

// The targets the skeleton hook poses the other players' rigs by (vr/remote_pose.h), from the poses as they are played back, 90 a second; none (once)
// when off. Our own unit's (the echo) never: our rig is ours.
void PoseShareHeroTick(ULONGLONG now) {
    static ULONGLONG next = 0;
    static bool had = false;
    if (now < next) return;
    next = now + 11;
    if (!PoseShareHeroWanted()) {
        if (had) { skel::SetRemoteHeroes(nullptr, 0); had = false; }
        return;
    }
    posewire::Remote r[8];
    const size_t n = posewire::Remotes(r, 8);
    AcquireSRWLockShared(&g_poseSpotsLock); const PoseSpots spots = g_poseSpots; ReleaseSRWLockShared(&g_poseSpotsLock);
    remotepose::Target t[remotepose::kMaxHeroes];
    int nt = 0;
    const bool legs = g_poseLegs.load() && gaitlib::Ensure();   // (the library loaded here, the plugin's thread, never in the pose hook)
    const float eyeUnits = std::max(1.0f, ViewHeight());
    for (size_t i = 0; i < n && nt < remotepose::kMaxHeroes; ++i) {
        if (r[i].unit == spots.own) continue;
        for (size_t j = 0; j < spots.n; ++j)
            if (spots.p[j].id == r[i].unit && remotepose::FromPose(r[i].pose, r[i].unit, spots.p[j].x, spots.p[j].z, &t[nt])) {
                t[nt].legs = legs;
                t[nt].worldUpm = eyeUnits / r[i].pose.eyeM;   // the world's units a metre of his body, as his capsules are scaled
                ++nt;
                break;
            }
    }
    skel::SetRemoteHeroes(t, nt);
    had = true;
}

// [multiplayer] pose_share_held: in a game, any view (what the others hold is someone else's VR shown, as their markers)
bool PoseShareHeldWanted() { return posewire::Registered() && g_poseHeld.load() && g_inWorld.load(); }
std::atomic<unsigned> g_heldCopies{0}, g_heldProps{0}, g_heldOnHand{0}, g_heldInWorld{0};   // since the last line: copies and props asked for, put on a hand / in the world

// What the other players' hands have, 90 a second: their potions and items as copies of the game's models (engitem::SetRemoteCopies), their props of the level put
// where they have them (knockprops::SetRemoteProps) - in a hand, on his hand as drawn here (skel::RemoteHandWorld; its place against his drawn hand, so it sits as in
// his), flying and at rest in the world by his feet, heading and units a metre. Drawn only.
void PoseShareHeldTick(ULONGLONG now) {
    static ULONGLONG next = 0;
    static bool had = false;
    if (now < next) return;
    next = now + 11;
    if (!PoseShareHeldWanted()) {
        if (had) { engitem::SetRemoteCopies(nullptr, 0); knockprops::SetRemoteProps(nullptr, 0); had = false; }
        return;
    }
    posewire::Remote r[8];
    const size_t n = posewire::Remotes(r, 8);
    AcquireSRWLockShared(&g_poseSpotsLock); const PoseSpots spots = g_poseSpots; ReleaseSRWLockShared(&g_poseSpotsLock);
    float L[3];
    AcquireSRWLockShared(&g_lookLock); memcpy(L, g_lookAt, sizeof L); ReleaseSRWLockShared(&g_lookLock);
    const float eyeUnits = std::max(1.0f, ViewHeight());
    engitem::RemoteCopy copies[16];
    knockprops::RemoteProp props[16];
    int nc = 0, np = 0;
    for (size_t i = 0; i < n && spots.ownOk; ++i) {
        if (r[i].unit == spots.own || !(r[i].pose.flags3 & poseshare::kAllFlags3)) continue;
        const meleetouch::PlayerSpot* at = nullptr;
        for (size_t j = 0; j < spots.n; ++j) if (spots.p[j].id == r[i].unit) at = &spots.p[j];
        remotepose::Target t;
        if (!at || !remotepose::FromPose(r[i].pose, r[i].unit, at->x, at->z, &t)) continue;
        float feet[3];
        poseshare::FeetOf(L, spots.ownX, spots.ownZ, at->x, at->z, feet);
        const float upm = r[i].pose.worldUpm > 0.5f ? r[i].pose.worldUpm : eyeUnits / r[i].pose.eyeM;   // his world units a metre (ours when not told)
        for (int side = 0; side < 2; ++side) {
            if (!t.hasHeld[side]) continue;
            const poseshare::Pose::Held& hd = t.held[side];
            const float scale = hd.what == poseshare::kHeldItem ? hd.scale : 1.0f;
            float m[16], relP[3], relR[3][3], hand[16];
            const bool onHand = hd.state == poseshare::kInHand && remotepose::HeldInHand(t, side, relP, relR) && skel::RemoteHandWorld(r[i].unit, side, hand, 200) &&
                                remotepose::OnHand(hand, relP, relR, upm, scale, m);
            if (!onHand) poseshare::HeldToWorld(hd.pose, feet, r[i].pose.heading, upm, scale, m);
            (onHand ? g_heldOnHand : g_heldInWorld).fetch_add(1, std::memory_order_relaxed);
            if (hd.what == poseshare::kHeldItem && nc < 16) {
                copies[nc].code = hd.code;
                memcpy(copies[nc].m, m, sizeof m);
                ++nc;
            } else if (hd.what == poseshare::kHeldProp && np < 16) {
                knockprops::RemoteProp& rp = props[np];
                float rows[3][3];
                bool fine = true;
                for (int k = 0; k < 3 && fine; ++k) {
                    const float l = sqrtf(m[k * 4] * m[k * 4] + m[k * 4 + 1] * m[k * 4 + 1] + m[k * 4 + 2] * m[k * 4 + 2]);
                    fine = l > 1e-6f;
                    for (int c = 0; fine && c < 3; ++c) rows[k][c] = m[k * 4 + c] / l;
                }
                bool mirrored = false;
                if (!fine || !poseshare::QuatOfRows(rows[0], rows[1], rows[2], rp.q, &mirrored) || mirrored) continue;
                rp.nameHash = hd.nameHash;
                poseshare::ToWorld(hd.home, feet, r[i].pose.heading, upm, rp.home);
                memcpy(rp.p, m + 12, sizeof rp.p);
                ++np;
            }
        }
    }
    engitem::SetRemoteCopies(copies, nc);
    knockprops::SetRemoteProps(props, np);
    g_heldCopies.fetch_add((unsigned)nc, std::memory_order_relaxed);
    g_heldProps.fetch_add((unsigned)np, std::memory_order_relaxed);
    had = true;
}

// The update thread, every round: our pose to the wire, the players' places for the draw (30 a second, while it may draw).
void PoseShareTick() {
    if (!posewire::Registered()) return;
    const ULONGLONG now = GetTickCount64();
    static ULONGLONG nextId = 0, nextSpots = 0, nextLine = 0;
    static uint32_t unit = 0;
    if (now >= nextId) { nextId = now + 100; unit = g_inWorld.load() ? OwnHeroUnitId() : 0; }
    poseshare::Pose own;
    const bool have = unit && PoseShareOwn(&own);
    posewire::Tick(g_inWorld.load(), have ? &own : nullptr, unit);
    PoseShareBodiesTick(now);   // [multiplayer] pose_share_bodies
    PoseShareHeroTick(now);     // [multiplayer] pose_share_hero
    PoseShareHeldTick(now);     // [multiplayer] pose_share_held
    // ... and every other hero, whether he sends a pose or not: his legs stepped (with pose_share_hero and _legs), his body from his own bones for our weapon
    skel::SetOtherHeroes(PoseShareHeroWanted() && g_poseLegs.load() && gaitlib::Loaded());
    skel::SetSelfLegs(g_poseLegsSelf.load() && g_inWorld.load() && gaitlib::Ensure());   // [debug] pose_share_legs_self (the library loaded here, never in the hook)
    if ((PoseShareDrawWanted() || PoseShareHeroWanted() || PoseShareHeldWanted()) && now >= nextSpots) {
        nextSpots = now + 33;
        PoseSpots s;
        meleetouch::PlayerSpot all[16];
        const size_t n = meleetouch::PlayersNow(all, 16);
        s.own = unit;
        for (size_t i = 0; i < n; ++i) {
            if (unit && all[i].id == unit) { s.ownOk = true; s.ownX = all[i].x; s.ownZ = all[i].z; }
            if (s.n < s.p.size()) s.p[s.n++] = all[i];
        }
        AcquireSRWLockExclusive(&g_poseSpotsLock); g_poseSpots = s; ReleaseSRWLockExclusive(&g_poseSpotsLock);
    }
    if (now >= nextLine) {
        const bool first = nextLine == 0;
        nextLine = now + 5000;
        const uint32_t drawn = g_poseDrawn.exchange(0), away = g_poseAway.exchange(0);
        {   // [multiplayer] pose_share_held
            const unsigned hc = g_heldCopies.exchange(0), hp = g_heldProps.exchange(0), ho = g_heldOnHand.exchange(0), hw = g_heldInWorld.exchange(0);
            const unsigned laid = engitem::TakeRemoteCopiesDrawn();
            unsigned told = 0;
            const unsigned found = knockprops::RemotePlaced(&told);
            if (!first && (hc || hp))
                LogF("vrcam: pose share - what their hands have (5 s): %u item copies asked, %u laid; %u props asked, %u of %u found here now; %u on a hand as drawn, %u in the world",
                     hc, laid, hp, found, told, ho, hw);
        }
        if (!first && (drawn || away))
            LogF("vrcam: pose share - markers: %u player-frames drawn, %u of a player not in our area (last 5 s); our unit %u", drawn, away, unit);
        const skel::RemoteHeroCount rh = skel::TakeRemoteHeroCount();
        if (!first && PoseShareHeroWanted() && (rh.posed || rh.unmatched || rh.unposable || rh.legsNoPose)) {   // (LogF cuts at 256: these are longer)
            char b[520];
            snprintf(b, sizeof b, "vrcam: pose share - other heroes (5 s): %u posed from a pose (%u turned by the world heading, %u weapon slots placed as drawn, %u hands placed as drawn - "
                     "out of the arm's reach up to %.1f cm, %u legs stepped), %u legs stepped with no pose; %u rigs with no pose near (within %.0f units), %u not posable (no bind pose / bones)",
                     rh.posed, rh.yawFromWorld, rh.weapon, rh.handsDrawn, rh.reachMissCm, rh.legs, rh.legsNoPose, rh.unmatched, remotepose::kMatchUnits, rh.unposable);
            Log(b);
        }
        if (const unsigned own = skel::TakeOwnLegs(); !first && own)
            LogF("vrcam: our hero's legs by the gait (5 s): %u poses ([body] gait_legs_third / _f4)", own);
        if (const std::string wb = skel::WalkBackNote(); !first && !wb.empty()) Log(("vrcam: " + wb).c_str());   // [body] walk_back
        if (const std::string df = skel::DollNote(); !first && !df.empty()) Log(("vrcam: " + df).c_str());       // [body] doll_feet
        {   // [debug] walk_back_dump: the legs of each walk back that ended, pose by pose, to d2rloader\logs\d2r_vr_walkback.txt (tools/walk_legs/walk_legs.py --dump)
            const bool want = IniB(L"debug", L"walk_back_dump", false);
            static bool told = false;
            if (want != told) {
                told = want;
                Log(want ? "vrcam: walk back dump on - each walk back's legs (the game's and as drawn) go to d2rloader\\logs\\d2r_vr_walkback.txt ([debug] walk_back_dump)"
                         : "vrcam: walk back dump off ([debug] walk_back_dump)");
            }
            skel::SetWalkBackDump(want);
            if (!want) headshadow::NoteHeroRig(0, 0, 0);
            else if (const std::string skin = headshadow::SkinNote(); !first && !skin.empty()) Log(("vrcam: " + skin).c_str());   // which pose the hero's meshes are skinned from
            if (const std::string dump = skel::TakeWalkBackDump(); !dump.empty()) {
                wchar_t path[MAX_PATH];
                wcscpy_s(path, g_iniPath);
                if (wchar_t* slash = wcsrchr(path, L'\\')) *slash = 0;
                wcscat_s(path, L"\\..\\logs\\d2r_vr_walkback.txt");
                static bool headed = false;
                if (FILE* f = _wfopen(path, headed ? L"ab" : L"wb")) {
                    if (!headed)
                        fputs("# [debug] walk_back_dump - per pose: t dt mode speed_m_s travel_deg share turn_taken_out_deg yaw_deg vx vz stepx stepz units_a_metre |"
                              " the game's (turned with the body): pelvis spine head, then right and left: hip knee foot foot_z_row | the same as drawn"
                              " (model units, the hero's model frame turned by yaw_deg: the look is (sin yaw, 0, cos yaw))\n", f);
                    headed = true;
                    fwrite(dump.data(), 1, dump.size(), f);
                    fclose(f);
                    LogF("vrcam: walk back dump - %zu bytes more in d2rloader\\logs\\d2r_vr_walkback.txt", dump.size());
                }
            }
        }
        const std::string legs = skel::LegsNote();
        if (!first && !legs.empty()) Log(("vrcam: pose share - " + legs).c_str());
    }
}

// The render thread: the markers of every player whose pose came (vr/pose_share.h Markers), relative to where the hands hang, as the
// capsules of melee_boxes. Their size is the sender's body scaled to our hero's eyes.
int PoseShareMarkers(const float eyeAt[3], float (*a)[4], float (*b)[4], int max) {
    if (max <= 0 || !PoseShareDrawWanted() || !g_poseDraw.load()) return 0;   // ([debug] pose_share_draw: the debug glass alone)
    posewire::Remote r[8];
    const size_t n = posewire::Remotes(r, 8);
    if (!n) return 0;
    AcquireSRWLockShared(&g_poseSpotsLock); const PoseSpots s = g_poseSpots; ReleaseSRWLockShared(&g_poseSpotsLock);
    if (!s.ownOk) return 0;
    float L[3];
    AcquireSRWLockShared(&g_lookLock); memcpy(L, g_lookAt, sizeof L); ReleaseSRWLockShared(&g_lookLock);
    const float eyeUnits = std::max(1.0f, ViewHeight());
    const poseshare::MarkerKinds k{meleeboxes::TouchKind(meleeboxes::kCBlue, meleeboxes::kBall), meleeboxes::TouchKind(meleeboxes::kCGreen, meleeboxes::kBall),
                                   meleeboxes::TouchKind(meleeboxes::kCGreen, meleeboxes::kTube)};
    int out = 0;
    for (size_t i = 0; i < n && out < max; ++i) {
        const meleetouch::PlayerSpot* at = nullptr;
        for (size_t j = 0; j < s.n; ++j) if (s.p[j].id == r[i].unit) at = &s.p[j];
        if (!at) { g_poseAway.fetch_add(1, std::memory_order_relaxed); continue; }
        float feet[3];
        poseshare::FeetOf(L, s.ownX, s.ownZ, at->x, at->z, feet);
        out += poseshare::Markers(r[i].pose, feet, eyeUnits / r[i].pose.eyeM, eyeAt, k, a + out, b + out, max - out);
        g_poseDrawn.fetch_add(1, std::memory_order_relaxed);
    }
    if (PoseShareBodiesWanted() && out < max) {   // [multiplayer] pose_share_bodies: their bodies grey glass, their weapons amber glass
        AcquireSRWLockShared(&g_playerBodiesLock);
        if (GetTickCount64() - g_playerBodiesDrawn.tickMs <= monsterbodies::kFreshMs)
            out += playerbodies::Pack(g_playerBodiesDrawn.b, g_playerBodiesDrawn.n, g_playerBodiesDrawn.upm, eyeAt, meleeboxes::TouchKind(meleeboxes::kCGrey, meleeboxes::kGlass),
                                      meleeboxes::TouchKind(meleeboxes::kCAmber, meleeboxes::kGlass), a + out, b + out, max - out);
        ReleaseSRWLockShared(&g_playerBodiesLock);
    }
    return out;
}

}  // namespace fx
}  // namespace vrc
