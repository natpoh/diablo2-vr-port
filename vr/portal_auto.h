// portal_auto.h - two flat-mode helpers from sajmonsoft's flat fork (github.com/sotodeshi/d2r3dflat, CHANGES.md sections 12 and 13, 2026-10-28).
// Pure; the test is cleanroom/tests/portal_auto_test.cpp, the wiring vr/vrcam.cpp (FlatPortalFrame, HookMouseGround, PortalClick).
//
// Arrival - [input] portal_look_horizon (flat F3): the hero came through a portal or a waypoint, so the look can go back to the horizon (a way
// portal stands on the ground: you looked down at it and came out still looking down). His measure of an arrival: the game sets a biome again
// every 2 - 3 s while the hero only walks in town, and a new biome name is also what a walk across a border gives - neither is one. What is:
// the hero CARRIED, his place jumping over 30 units between two frames (a walk moves it under 1) within 20 s after the game set a biome (the
// set comes when the load starts, the jump when it ends, 6 - 7 s later). The last place before the load is kept through frames without one;
// a long jump with no biome set (the Sorceress's Teleport) is no arrival.
//
// Machine - [input] portal_auto (flat F2 / F3): walking into a portal enters it. Never a level change of ours: the game's own click does it.
// With a move key held and the hero within `radius` of a portal (kind 4 of flat_pick_obj.inc) for `delayMs`, the game's mouse point is put
// on the portal's place (Aim); once the game's own hover says the portal is under it, the left button goes down (a real click through the
// window: the game sends what it sends for a click on the portal) and 60 ms later up. Hover not there within 700 ms: no click. A button that
// went down always comes up. No bounce: after a new area or a jump of the hero's place nothing is entered until the area settled (1.5 s) and
// the hero walked radius + 1.5 away from every portal; after a click the same.
#pragma once

#include <cmath>
#include <cstdint>

namespace portalauto {

constexpr float kCarried = 30.0f;          // world units between two frames: carried, not walked
constexpr uint64_t kArrivalWindowMs = 20000;

struct Arrival {
    uint32_t seenGen = 0;
    bool started = false;
    uint64_t pendingUntil = 0;
    bool prevOk = false;
    float prevX = 0.0f, prevZ = 0.0f;
    // Each frame: areaGen bumps with every biome the game sets; have / x / z: the hero's place now (none while an area loads). True once per arrival.
    bool Step(uint64_t now, uint32_t areaGen, bool have, float x, float z) {
        if (!started) { started = true; seenGen = areaGen; }
        if (areaGen != seenGen) { seenGen = areaGen; pendingUntil = now + kArrivalWindowMs; }
        if (!have || !std::isfinite(x + z)) return false;   // the last place before the load stays the reference
        const float dx = x - prevX, dz = z - prevZ;
        const bool arrived = prevOk && now < pendingUntil && dx * dx + dz * dz > kCarried * kCarried;
        if (arrived) pendingUntil = 0;
        prevX = x; prevZ = z; prevOk = true;
        return arrived;
    }
};

// The look's pitch (degrees on top of the game camera's own, + = down) that puts the view at the horizon, given the game camera's own
// downward angle (degrees) and the [camera] pitch added to it: down0 + pitch + mouse = 0.
inline float HorizonMousePitch(float gameDownDeg, float settingPitchDeg) { return -(gameDownDeg + settingPitchDeg); }

struct In {
    uint64_t now = 0;
    bool wanted = false;        // flat F2 / F3, mouse look and W A S D, in the world, no menu, the tick on
    bool moving = false;        // a move key held, the game's window in front, no chat line
    uint32_t areaGen = 0;
    bool heroOk = false;
    float heroX = 0.0f, heroZ = 0.0f;   // the hero's place on the grid, world units
    bool portal = false;        // the nearest portal ...
    float portalX = 0.0f, portalZ = 0.0f, dist = 0.0f;
    bool hoverIsPortal = false; // the game's own hover reports a portal (asked on the mouse code's thread)
    float radius = 4.0f;
    int delayMs = 150;
};
struct Out {
    bool force = false;         // the game's mouse point goes to (forceX, forceZ), world units (the cell is half)
    float forceX = 0.0f, forceZ = 0.0f;
    bool down = false, up = false;   // press / release the left button now
};

class Machine {
public:
    enum class Stage { Idle, Aim, Down };
    Stage stage() const { return stage_; }
    bool armed() const { return armed_; }

    Out Step(const In& in) {
        Out o;
        const bool jumped = haveLast_ && in.heroOk && Dist2(in.heroX - lastX_, in.heroZ - lastZ_) > kCarried * kCarried;
        if (!started_ || in.areaGen != lastGen_ || jumped) {
            if (started_) { Cancel(&o); armed_ = false; coolUntil_ = in.now + 1000; settleUntil_ = in.now + 1500; }
            started_ = true;
            lastGen_ = in.areaGen;
        }
        haveLast_ = in.heroOk;
        if (in.heroOk) { lastX_ = in.heroX; lastZ_ = in.heroZ; }
        if (!in.wanted || !in.heroOk) { Cancel(&o); return o; }
        const bool close = in.portal && in.dist <= in.radius;
        if (!armed_) {   // after an arrival or a click: armed once the area settled and the hero is away from every portal
            if (in.now >= settleUntil_ && (!in.portal || in.dist > in.radius + 1.5f)) armed_ = true;
            return o;
        }
        switch (stage_) {
            case Stage::Idle:
                if (!close || !in.moving || in.now < coolUntil_) { if (!close || !in.moving) nearSince_ = 0; return o; }
                if (!nearSince_) nearSince_ = in.now ? in.now : 1;
                if (in.now - nearSince_ < (uint64_t)(in.delayMs > 0 ? in.delayMs : 0)) return o;
                stage_ = Stage::Aim;
                stageAt_ = in.now;
                Aim(in, &o);
                return o;
            case Stage::Aim:
                if (!in.portal || in.dist > in.radius + 2.0f) { Cancel(&o); return o; }
                Aim(in, &o);
                if (in.hoverIsPortal) {
                    o.down = true;
                    stage_ = Stage::Down;
                    stageAt_ = in.now;
                    return o;
                }
                if (in.now - stageAt_ > 700) { Cancel(&o); armed_ = false; coolUntil_ = in.now + 2000; }
                return o;
            case Stage::Down:
                Aim(in, &o);   // still on the portal while the button is down
                if (in.now - stageAt_ < 60) return o;
                Cancel(&o);
                armed_ = false;
                coolUntil_ = in.now + 1500;
                return o;
        }
        return o;
    }

private:
    static float Dist2(float dx, float dz) { return dx * dx + dz * dz; }
    void Aim(const In& in, Out* o) { o->force = true; o->forceX = in.portalX; o->forceZ = in.portalZ; }
    void Cancel(Out* o) {
        if (stage_ == Stage::Down) o->up = true;   // a button that went down always comes up
        o->force = false;
        stage_ = Stage::Idle;
        nearSince_ = 0;
    }
    Stage stage_ = Stage::Idle;
    bool started_ = false, armed_ = false, haveLast_ = false;
    uint32_t lastGen_ = 0;
    float lastX_ = 0.0f, lastZ_ = 0.0f;
    uint64_t stageAt_ = 0, nearSince_ = 0, coolUntil_ = 0, settleUntil_ = 0;
};

}  // namespace portalauto
