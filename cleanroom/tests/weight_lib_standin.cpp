// weight_lib_standin.cpp - a stand-in for the weight-physics library (shared/d2r_vr_weight.h): the interface implemented with this repository's own heft body and fixed muscles, so
// the loader (vr/weight_lib.cpp) and the engine adapter can be tested without the real library. Not a physics anyone plays with.
//
// Switches in D2rwHold::flags (a stand-in's own): bit 31 hands of 2 kg (so the body's centre of mass differs from the mod's: the state must go through the grip frame), bit 30 refuse
// the step (-1: the adapter must fall back to the mod's own). STANDIN_BAD_ABI builds one that speaks another interface version.

#define D2RW_BUILD_DLL 1
#include "d2r_vr_weight.h"

#include <algorithm>
#include <cmath>

#include "heft.h"

namespace {
heft::V3 V(const D2rwV3& v) { return { v.x, v.y, v.z }; }
D2rwV3 C(const heft::V3& v) { return { v.x, v.y, v.z }; }
heft::Q Q(const D2rwQ& q) { return heft::QNorm({ q.x, q.y, q.z, q.w }); }
}  // namespace

namespace {
double g_lastStrength = -1.0, g_lastSteelKg = -1.0;   // what the last step was given: the hands' strength and the steel's mass (StandinLast)
}  // namespace

extern "C" {

// Not part of the interface: what the last D2rwStep was given, for the tests (the hero's strength reaches a library as these two numbers).
__declspec(dllexport) void StandinLast(double* strength, double* steelKg) {
    if (strength) *strength = g_lastStrength;
    if (steelKg) *steelKg = g_lastSteelKg;
}

int32_t D2rwAbi(void) {
#ifdef STANDIN_BAD_ABI
    return 99;
#else
    return D2RW_ABI_VERSION;
#endif
}

const char* D2rwName(void) { return "standin 0.1"; }

int32_t D2rwStep(const D2rwHold* hold, const D2rwHand* hands, int32_t nHands, double dt, D2rwState* state) {
    if (!hold || hold->size < sizeof(D2rwHold) || !hands || nHands < 1 || nHands > 2 || !state) return -1;
    if (hold->flags & (1u << 30)) return -1;
    g_lastStrength = hold->strength;
    g_lastSteelKg  = 0.0;
    for (int i = 0; i < hold->nPieces && i < 3; ++i) g_lastSteelKg += hold->pieces[i].mass;
    heft::Piece pieces[3];
    for (int i = 0; i < hold->nPieces && i < 3; ++i) pieces[i] = { V(hold->pieces[i].a), V(hold->pieces[i].b), hold->pieces[i].radius, hold->pieces[i].mass };
    heft::V3 palms[2];
    for (int i = 0; i < nHands; ++i) palms[i] = V(hands[i].palm);
    heft::Body b = heft::Build(pieces, hold->nPieces, palms, nHands, (hold->flags & (1u << 31)) ? 2.0 : 0.5);
    b.maxAngularSpeed = 25;
    heft::State s;
    s.rot = Q(state->rot);
    const heft::V3 comAt = heft::Rotate(s.rot, b.com);
    s.com    = heft::Add(V(state->pos), comAt);
    s.angVel = V(state->angVel);
    s.vel    = heft::Add(V(state->vel), heft::Cross(s.angVel, comAt));
    s.placed = true;
    heft::Grip g[2];
    heft::Controller from[2], to[2];
    for (int i = 0; i < nHands; ++i) {
        g[i].at     = V(hands[i].palm);
        g[i].rot    = Q(hands[i].gripRot);
        g[i].on     = true;
        g[i].muscle = { 3000, 300, 300, 500, 50, 75, 0 };
        from[i]     = { { V(hands[i].fromPos), Q(hands[i].fromRot) }, V(hands[i].fromVel), V(hands[i].fromAngVel) };
        to[i]       = { { V(hands[i].toPos), Q(hands[i].toRot) }, V(hands[i].toVel), V(hands[i].toAngVel) };
    }
    heft::World w;
    w.gravity.y    = -9.81 * hold->gravity;
    w.snapDistance = hold->snap > 0 ? hold->snap : 0.8;
    const bool snapped = dt > 0 && heft::Step(s, b, g, from, to, nHands, std::min(dt, 0.1), w);
    const heft::V3 comOut = heft::Rotate(s.rot, b.com);
    state->pos    = C(heft::Sub(s.com, comOut));
    state->rot    = { s.rot.x, s.rot.y, s.rot.z, s.rot.w };
    state->angVel = C(s.angVel);
    state->vel    = C(heft::Sub(s.vel, heft::Cross(s.angVel, comOut)));
    return snapped ? 1 : 0;
}

int32_t D2rwThrowVelocity(const uint64_t* tMs, const D2rwV3* vel, int32_t n, uint64_t nowMs, D2rwV3* out) {
    if (!tMs || !vel || !out || n <= 0) return 0;
    D2rwV3 sum { 0, 0, 0 };
    int used = 0;
    for (int i = 0; i < n; ++i)
        if (tMs[i] <= nowMs && nowMs - tMs[i] <= 100) {
            sum.x += vel[i].x; sum.y += vel[i].y; sum.z += vel[i].z;
            ++used;
        }
    if (!used) return 0;
    *out = { sum.x / used, sum.y / used, sum.z / used };
    return 1;
}

}  // extern "C"
