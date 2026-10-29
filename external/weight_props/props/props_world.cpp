// SPDX-License-Identifier: MIT
// props_world.cpp - see props_world.h.
//
// Copyright (c) 2026 natpoh
// Copyright (c) 2026 bearstonem (the knockable props' rules - how frozen props are woken, held on what they stand on and frozen again - and the materials' numbers:
//                                bearstonem/d2r-pcvr's knockable props of 2026-10-22, steamvr/dropped_bottles.cpp and steamvr/props.h, MIT)
// Box3D: Copyright (c) 2026 Erin Catto (MIT), v0.1.0, used unmodified.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "props/props_world.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include <box3d/box3d.h>

namespace props {

using namespace math;

namespace {

// ---- collision groups ----
constexpr uint64_t kGround = 1, kStatic = 2, kProp = 4, kFrozen = 8, kMover = 16;
b3Filter Filter(uint64_t category, uint64_t mask) {
    b3Filter f = b3DefaultFilter();
    f.categoryBits = category;
    f.maskBits = mask;
    f.groupIndex = 0;
    return f;
}
b3Filter GroundFilter() { return Filter(kGround, kProp); }
b3Filter StaticFilter() { return Filter(kStatic, kProp); }
b3Filter MovingFilter() { return Filter(kProp, kGround | kStatic | kProp | kFrozen | kMover); }
b3Filter FrozenFilter() { return Filter(kFrozen, kProp); }
b3Filter MoverFilter() { return Filter(kMover, kProp); }

// ---- numbers ----
constexpr float kGroundHalf = 40.0f;          // m: the floor box round the origin
constexpr int kRoundSides = 12;
constexpr float kInset = 0.002f, kInsetShare = 0.05f;   // a prop's shape this far inside its bounds: props packed together do not start overlapping
constexpr float kWakeMargin = 0.01f;          // m: a mover passing this near a frozen prop sets it going
constexpr int kWakeSamples = 6;               // points along a mover's sweep in the wake test
constexpr float kWakeMoverSpeed = 0.3f;       // m/s: a moving prop slower than this does not wake its neighbours (a settling row)
constexpr float kRestTurn = 0.3f;             // rad/s: and turning slower than this counts as still
constexpr float kDrift = 0.015f, kDriftTurn = 0.07f, kDriftTime = 0.5f;   // moved less than this over this long: settled in place
constexpr float kMoverJump = 0.75f;           // m: a mover that jumps further is put there (a warp, a recentre), no sweep
constexpr float kMoverMinRadius = 0.02f, kMoverMaxRadius = 0.08f;
constexpr float kMoverFriction = 0.4f;
constexpr float kLost = 0.6f;                 // m: a mover's body this far from its target is put back on it
constexpr float kContactSpeed = 1.0f;         // m/s: overlaps resolve this gently instead of popping apart
constexpr float kFloorFriction = 0.7f, kFloorBounce = 0.2f;

// The materials (bearstonem/d2r-pcvr, MIT: steamvr/props.h's Properties). Densities are for the solid hull, so hollow vessels and chairs (mostly air) have less than their stuff.
constexpr std::array<Material, D2RP_MATERIALS> kMaterials{ {
    { 600, 0.6f, 0.15f, 0.04f },   // ceramic
    { 500, 0.5f, 0.30f, 0.015f },  // glass
    { 900, 0.4f, 0.25f, 0.03f },   // metal
    { 500, 0.6f, 0.20f, 0.03f },   // wood
    { 400, 0.7f, 0.05f, 0.05f },   // paper
    { 600, 0.6f, 0.20f, 0.04f },   // bone
    { 150, 0.7f, 0.10f, 0.04f },   // wicker
    { 800, 0.6f, 0.10f, 0.04f },   // food
    { 100, 0.9f, 0.0f, 0.10f },    // cloth
    { 80, 0.6f, 0.10f, 0.10f },    // furniture
    { 1500, 0.6f, 0.10f, 0.05f },  // stone
    { 350, 0.9f, 0.0f, 0.20f },    // a filled sack
} };

// Does a capsule (a-b, radius r) reach a box (centre c, half extents h) of a body at (p, q)? Its rounded corners count as the box inflated by r: a near miss at a corner wakes a
// prop that then rests where it is.
bool CapsuleTouchesBox(V a, V b, float r, V p, Q q, V c, V h) {
    const Q inv = Conj(q);
    const V s = Rotate(inv, a - p) - c, d = Rotate(inv, b - p) - c - s;
    const float* so = &s.x;
    const float* dd = &d.x;
    const float* hh = &h.x;
    float t0 = 0, t1 = 1;
    for (int i = 0; i < 3; ++i) {
        const float lo = -hh[i] - r, hi = hh[i] + r;
        if (std::fabs(dd[i]) < 1e-7f) {
            if (so[i] < lo || so[i] > hi) return false;
            continue;
        }
        float ta = (lo - so[i]) / dd[i], tb = (hi - so[i]) / dd[i];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    return true;
}

bool Overlap(const b3AABB& a, const b3AABB& b) {
    return a.lowerBound.x <= b.upperBound.x && b.lowerBound.x <= a.upperBound.x && a.lowerBound.y <= b.upperBound.y && b.lowerBound.y <= a.upperBound.y &&
           a.lowerBound.z <= b.upperBound.z && b.lowerBound.z <= a.upperBound.z;
}

void SetFilter(b3BodyId body, b3Filter f) {
    b3ShapeId shapes[4];
    const int n = b3Body_GetShapes(body, shapes, 4);
    for (int i = 0; i < n; ++i) b3Shape_SetFilter(shapes[i], f, true);
}

}  // namespace

D2rpParams Defaults() {
    D2rpParams p{};
    p.size = sizeof(D2rpParams);
    p.gravity = 9.81f;
    p.substeps = 4;
    p.maxMoving = 12;
    p.restSpeed = 0.03f;
    p.restTime = 0.6f;
    p.moveLimit = 12.0f;
    p.strength = 1.0f;
    p.propMass = 1.0f;
    return p;
}

D2rpParams Sane(const D2rpParams* in) {
    D2rpParams p = Defaults();
    if (in && in->size >= sizeof(uint32_t)) {
        const size_t n = std::min<size_t>(in->size, sizeof(D2rpParams));
        std::memcpy(&p, in, n);
        p.size = sizeof(D2rpParams);
        const D2rpParams d = Defaults();
        // fields a shorter struct did not carry keep the defaults
        auto keep = [&](size_t offset, size_t bytes, const void* def) {
            if (n < offset + bytes) std::memcpy(reinterpret_cast<char*>(&p) + offset, def, bytes);
        };
        keep(offsetof(D2rpParams, gravity), sizeof(float), &d.gravity);
        keep(offsetof(D2rpParams, substeps), sizeof(int32_t), &d.substeps);
        keep(offsetof(D2rpParams, maxMoving), sizeof(int32_t), &d.maxMoving);
        keep(offsetof(D2rpParams, restSpeed), sizeof(float), &d.restSpeed);
        keep(offsetof(D2rpParams, restTime), sizeof(float), &d.restTime);
        keep(offsetof(D2rpParams, moveLimit), sizeof(float), &d.moveLimit);
        keep(offsetof(D2rpParams, strength), sizeof(float), &d.strength);
        keep(offsetof(D2rpParams, propMass), sizeof(float), &d.propMass);
    }
    auto clampf = [](float v, float lo, float hi, float def) { return std::isfinite(v) ? std::clamp(v, lo, hi) : def; };
    const D2rpParams d = Defaults();
    p.gravity = clampf(p.gravity, 0.0f, 30.0f, d.gravity);
    p.substeps = std::clamp(p.substeps, 1, 16);
    p.maxMoving = std::clamp(p.maxMoving, 1, 32);
    p.restSpeed = clampf(p.restSpeed, 0.001f, 1.0f, d.restSpeed);
    p.restTime = clampf(p.restTime, 0.05f, 10.0f, d.restTime);
    p.moveLimit = clampf(p.moveLimit, 0.5f, 120.0f, d.moveLimit);
    p.strength = clampf(p.strength, 0.05f, 10.0f, d.strength);
    p.propMass = clampf(p.propMass, 0.05f, 20.0f, d.propMass);
    return p;
}

const Material& MaterialOf(int material) { return kMaterials[(size_t)std::clamp(material, 0, D2RP_MATERIALS - 1)]; }

HandDrive DriveOf(int hands, float strength) {
    // per hand, a hand's pull on its weapon (kHandForce, kHandTorque), times the strength; the stiffness is high because the target already is the mod's weighted weapon (its lag
    // and sag are in it): this body only has to stay on it until a prop pushes back
    const float n = (float)std::clamp(hands, 1, 2), st = std::max(0.05f, strength);
    return { kHandForce * n * st, kHandTorque * n * st, 30.0f, 30.0f, 1.0f };
}

struct World::Impl {
    D2rpParams p{};
    b3WorldId world = b3_nullWorldId;
    b3BodyId ground = b3_nullBodyId, statics = b3_nullBodyId;
    float floorY = 0;
    std::vector<D2rpBox> staticBoxes;
    Holder* holder = nullptr;

    struct Prop {
        D2rpProp in{};
        b3BodyId body = b3_nullBodyId;
        V centre{}, half{};
        b3AABB reach{};            // frozen: its shape's bounds, for being bumped
        bool moving = false, rested = false;
        double since = 0, still = 0, anchoredAt = 0;
        V anchor{};
        Q turned{};
        int knock = -1;            // index into knocks while its impulse is still to be read
        b3MassData own{};          // carried: its own mass and inertia, put back by Uncarry
        bool carried = false;
    };
    std::vector<Prop> props;

    struct Mover {
        D2rpMover in{};
        bool have = false;
        b3BodyId anchor = b3_nullBodyId, body = b3_nullBodyId;
        b3JointId joint = b3_nullJointId;
        b3ShapeId shape = b3_nullShapeId;
        float length = 0, radius = 0, mass = 0;
        int hands = 0, kind = -1;
        float impulse = 0;
    };
    std::array<Mover, D2RP_MAX_MOVERS> movers{};

    std::vector<D2rpKnock> knocks;
    std::vector<D2rpPose> poses;

    explicit Impl(const D2rpParams& params) : p(params) {
        b3WorldDef def = b3DefaultWorldDef();
        def.gravity = { 0, -p.gravity, 0 };
        def.workerCount = 1;   // serial: no threads of Box3D's own in the game's process
        def.contactSpeed = kContactSpeed;
        world = b3CreateWorld(&def);
    }
    ~Impl() {
        if (b3World_IsValid(world)) b3DestroyWorld(world);
    }

    bool Held(uint32_t id) const { return holder && holder->Holds(id); }
    bool HoldsWith(int slot) const { return holder && holder->HoldsWith(slot); }

    int MovingCount() const {
        int n = 0;
        for (const Prop& pr : props) n += pr.moving ? 1 : 0;
        return n;
    }
    // ... of them the ones not held: what maxMoving counts (a held prop is its holder's, not one more the world keeps going)
    int MovingFree() const {
        int n = 0;
        for (const Prop& pr : props) n += pr.moving && !Held(pr.in.id) ? 1 : 0;
        return n;
    }

    void DropMover(Mover& m) {
        if (!B3_IS_NULL(m.joint) && b3Joint_IsValid(m.joint)) b3DestroyJoint(m.joint, false);
        m.joint = b3_nullJointId;
        Destroy(m.body);
        Destroy(m.anchor);
        m.shape = b3_nullShapeId;
        m.length = m.radius = m.mass = 0;
        m.hands = 0;
        m.kind = -1;
    }

    void Clear(float y) {
        if (holder) holder->Clearing();
        for (Mover& m : movers) {
            DropMover(m);
            m.have = false;
        }
        for (Prop& pr : props) Destroy(pr.body);
        props.clear();
        Destroy(statics);
        staticBoxes.clear();
        Destroy(ground);
        knocks.clear();
        poses.clear();
        floorY = y;
        b3BodyDef def = b3DefaultBodyDef();
        def.position = { 0, y - 0.5f, 0 };
        ground = b3CreateBody(world, &def);
        b3BoxHull box = b3MakeBoxHull(kGroundHalf, 0.5f, kGroundHalf);
        b3ShapeDef shape = b3DefaultShapeDef();
        shape.baseMaterial.friction = kFloorFriction;
        shape.baseMaterial.restitution = kFloorBounce;
        shape.filter = GroundFilter();
        b3CreateHullShape(ground, &shape, &box.base);
    }

    Prop* PropById(uint32_t id) {
        for (Prop& pr : props)
            if (pr.in.id == id) return &pr;
        return nullptr;
    }

    // A carried prop weighs kg: its mass and inertia while carried, its own put back by Uncarry.
    void Carry(Prop& pr, float kg) {
        if (pr.carried || !pr.moving) return;
        pr.own = b3Body_GetMassData(pr.body);
        if (!(pr.own.mass > 0.0f) || kg >= pr.own.mass) return;
        b3MassData md = pr.own;
        const float share = kg / pr.own.mass;
        md.mass = kg;
        md.inertia = { b3MulSV(share, md.inertia.cx), b3MulSV(share, md.inertia.cy), b3MulSV(share, md.inertia.cz) };
        b3Body_SetMassData(pr.body, md);
        pr.carried = true;
    }
    void Uncarry(Prop& pr) {
        if (!pr.carried) return;
        pr.carried = false;
        if (pr.moving) b3Body_SetMassData(pr.body, pr.own);
    }

    // The prop is set going without a knock (nothing struck it), as many moving as allowed - the one moving longest that is not held is frozen for it.
    void Lift(Prop& pr, double now) {
        if (!pr.moving) {
            if (MovingFree() >= p.maxMoving) {
                Prop* oldest = nullptr;
                for (Prop& o : props)
                    if (o.moving && !Held(o.in.id) && (!oldest || o.since < oldest->since)) oldest = &o;
                if (oldest) Freeze(*oldest);
            }
            b3Body_SetType(pr.body, b3_dynamicBody);
            SetFilter(pr.body, MovingFilter());
            pr.moving = true;
            pr.rested = false;
        }
        b3Body_SetAwake(pr.body, true);
        pr.since = now;
        pr.still = 0;
        const b3WorldTransform t = b3Body_GetTransform(pr.body);
        pr.anchor = F(t.p);
        pr.turned = F(t.q);
        pr.anchoredAt = now;
    }

    // Its time to rest starts now, from where it is.
    void Restart(Prop& pr, double now) {
        pr.since = now;
        pr.still = 0;
        pr.anchoredAt = now;
        pr.anchor = F(b3Body_GetTransform(pr.body).p);
        pr.turned = F(b3Body_GetTransform(pr.body).q);
    }

    // ---- props ----
    b3AABB Reach(const Prop& pr) const { return b3Body_ComputeAABB(pr.body); }

    bool Add(const D2rpProp& in) {
        Prop pr;
        pr.in = in;
        pr.centre = F3(in.centre);
        auto inset = [](float v) { return std::max(kMinHalf, v - std::min(kInset, v * kInsetShare)); };
        pr.half = { inset(std::fabs(in.half[0])), inset(std::fabs(in.half[1])), inset(std::fabs(in.half[2])) };
        b3BodyDef def = b3DefaultBodyDef();
        def.type = b3_staticBody;
        def.position = B(F3(in.p));
        def.rotation = B(F4(in.q));
        def.linearDamping = 0.05f;
        def.angularDamping = 0.1f;
        pr.body = b3CreateBody(world, &def);
        if (B3_IS_NULL(pr.body)) return false;
        const Material& m = MaterialOf(in.material);
        b3ShapeDef shape = b3DefaultShapeDef();
        shape.density = m.density * p.propMass;
        shape.baseMaterial.friction = m.friction;
        shape.baseMaterial.restitution = m.bounce;
        shape.baseMaterial.rollingResistance = m.rolling;
        shape.filter = FrozenFilter();
        const V c = pr.centre, e = pr.half;
        b3ShapeId made = b3_nullShapeId;
        if (in.shape == D2RP_SHAPE_BOX) {
            b3BoxHull box = b3MakeOffsetBoxHull(e.x, e.y, e.z, B(c));
            made = b3CreateHullShape(pr.body, &shape, &box.base);
        } else if (in.shape == D2RP_SHAPE_BALL) {
            const b3Sphere sphere{ B(c), std::max(kMinHalf, (e.x + e.y + e.z) / 3.0f) };
            made = b3CreateSphereShape(pr.body, &shape, &sphere);
        } else {
            // an upright cylinder in the bounds, elliptical where they are (a jug's handle side)
            std::array<b3Vec3, kRoundSides * 2> pts{};
            for (int i = 0; i < kRoundSides; ++i) {
                const float a = 2.0f * 3.14159265f * (float)i / kRoundSides, x = c.x + e.x * std::cos(a), z = c.z + e.z * std::sin(a);
                pts[(size_t)(2 * i)] = { x, c.y - e.y, z };
                pts[(size_t)(2 * i + 1)] = { x, c.y + e.y, z };
            }
            if (b3HullData* hull = b3CreateHull(pts.data(), (int)pts.size(), (int)pts.size())) {
                made = b3CreateHullShape(pr.body, &shape, hull);   // (the world keeps a copy of its own)
                b3DestroyHull(hull);
            }
        }
        if (B3_IS_NULL(made)) {
            Destroy(pr.body);
            return false;
        }
        pr.reach = Reach(pr);
        props.push_back(pr);
        return true;
    }

    void Knocked(Prop& pr, int slot, float speed, double now) {
        b3Body_SetType(pr.body, b3_dynamicBody);
        SetFilter(pr.body, MovingFilter());
        b3Body_SetAwake(pr.body, true);
        pr.moving = true;
        pr.rested = false;
        pr.since = now;
        pr.still = 0;
        const b3WorldTransform t = b3Body_GetTransform(pr.body);
        pr.anchor = F(t.p);
        pr.turned = F(t.q);
        pr.anchoredAt = now;
        pr.knock = (int)knocks.size();
        knocks.push_back({ pr.in.id, slot, speed, 0.0f });
    }

    void Freeze(Prop& pr) {
        b3Body_SetLinearVelocity(pr.body, { 0, 0, 0 });
        b3Body_SetAngularVelocity(pr.body, { 0, 0, 0 });
        b3Body_SetType(pr.body, b3_staticBody);
        SetFilter(pr.body, FrozenFilter());
        pr.moving = false;
        pr.rested = true;
        pr.reach = Reach(pr);
    }

    // ---- movers ----
    // Where a mover is drawn at time t: its sample carried on by its velocity (not further than kAhead).
    void Target(const D2rpMover& in, double t, V* a, Q* q, float* length) const {
        const V a0 = F3(in.a), b0 = F3(in.b);
        const V d = b0 - a0;
        *length = Len(d);
        const Q q0 = *length > 1e-4f ? FromUp(d * (1.0f / *length)) : Q{};
        const float ahead = (float)std::clamp(t - in.time, 0.0, (double)kAhead);
        *a = a0 + F3(in.vel) * ahead;
        *q = Norm(Mul(Exp(F3(in.angVel) * ahead), q0));
    }

    void MakeMover(Mover& m, V a, Q q, float length) {
        DropMover(m);
        const float radius = std::clamp(m.in.radius, kMoverMinRadius, kMoverMaxRadius);
        const float mass = std::clamp(m.in.mass, 0.2f, 40.0f);
        b3BodyDef ad = b3DefaultBodyDef();
        ad.type = b3_kinematicBody;
        ad.position = B(a);
        ad.rotation = B(q);
        ad.enableSleep = false;
        m.anchor = b3CreateBody(world, &ad);
        b3BodyDef bd = b3DefaultBodyDef();
        bd.type = b3_dynamicBody;
        bd.position = B(a);
        bd.rotation = B(q);
        bd.gravityScale = 0.0f;   // the drawn weapon's own weight is in its target already
        bd.enableSleep = false;
        bd.isBullet = true;
        m.body = b3CreateBody(world, &bd);
        b3ShapeDef sd = b3DefaultShapeDef();
        sd.density = 1000.0f;
        sd.baseMaterial.friction = kMoverFriction;
        sd.filter = MoverFilter();
        sd.updateBodyMass = false;
        const float len = std::max(length, 0.001f);
        const b3Capsule cap{ { 0, 0, 0 }, { 0, len, 0 }, radius };
        m.shape = b3CreateCapsuleShape(m.body, &sd, &cap);
        // its own mass, about its middle: a rod of the weapon's length with the hands' mass on it
        b3MassData md{};
        md.mass = mass;
        md.center = { 0, len * 0.5f, 0 };
        const float across = mass * (len * len / 12.0f + radius * radius / 4.0f), along = 0.5f * mass * radius * radius;
        md.inertia = { { across, 0, 0 }, { 0, std::max(along, 1e-4f), 0 }, { 0, 0, across } };
        b3Body_SetMassData(m.body, md);
        const HandDrive dr = DriveOf(m.in.hands, p.strength);
        b3MotorJointDef jd = b3DefaultMotorJointDef();
        jd.base.bodyIdA = m.anchor;
        jd.base.bodyIdB = m.body;
        jd.base.localFrameA = b3Transform_identity;
        jd.base.localFrameB = b3Transform_identity;
        jd.base.collideConnected = false;
        jd.linearVelocity = { 0, 0, 0 };    // the velocity motor holds it to the anchor's velocity (the drawn weapon's), with the hands' ceiling
        jd.angularVelocity = { 0, 0, 0 };
        jd.maxVelocityForce = dr.maxForce;
        jd.maxVelocityTorque = dr.maxTorque;
        jd.linearHertz = dr.linearHz;
        jd.linearDampingRatio = dr.damping;
        jd.maxSpringForce = dr.maxForce;
        jd.angularHertz = dr.angularHz;
        jd.angularDampingRatio = dr.damping;
        jd.maxSpringTorque = dr.maxTorque;
        m.joint = b3CreateMotorJoint(world, &jd);
        m.length = length;
        m.radius = radius;
        m.mass = mass;
        m.hands = m.in.hands;
        m.kind = m.in.kind;
    }

    // Before the step: each mover's anchor heads for where it is drawn at the step's end; the body follows by the drive.
    void Drive(double now, float dt) {
        for (size_t slot = 0; slot < movers.size(); ++slot) {
            Mover& m = movers[slot];
            if (!m.have || !m.in.on || HoldsWith((int)slot)) {   // (a hand that holds a prop: its fist would push the prop out of it)
                if (!B3_IS_NULL(m.body)) DropMover(m);
                continue;
            }
            V a;
            Q q;
            float length = 0;
            Target(m.in, now + dt, &a, &q, &length);
            if (!Finite(a) || !Finite(q)) continue;
            const float radius = std::clamp(m.in.radius, kMoverMinRadius, kMoverMaxRadius);
            if (B3_IS_NULL(m.body) || std::fabs(length - m.length) > 0.01f || std::fabs(radius - m.radius) > 0.005f || m.hands != m.in.hands || m.kind != m.in.kind ||
                std::fabs(std::clamp(m.in.mass, 0.2f, 40.0f) - m.mass) > 0.05f) {
                MakeMover(m, a, q, length);
                continue;
            }
            const b3WorldTransform at = b3Body_GetTransform(m.anchor);
            const V was = F(at.p);
            const Q wasQ = F(at.q);
            const V tipWas = was + Rotate(wasQ, { 0, m.length, 0 }), tipNow = a + Rotate(q, { 0, length, 0 });
            const float moved = std::max(Len(a - was), Len(tipNow - tipWas));
            if (moved > kMoverJump || moved > kMoverMaxSpeed * dt) {   // a warp: there at once, both of them
                b3Body_SetTransform(m.anchor, B(a), B(q));
                b3Body_SetLinearVelocity(m.anchor, { 0, 0, 0 });
                b3Body_SetAngularVelocity(m.anchor, { 0, 0, 0 });
                b3Body_SetTransform(m.body, B(a), B(q));
                b3Body_SetLinearVelocity(m.body, { 0, 0, 0 });
                b3Body_SetAngularVelocity(m.body, { 0, 0, 0 });
                continue;
            }
            // the anchor moves there over the step by its own velocity (a kinematic body's target transform is skipped below the sleep speed)
            b3Body_SetLinearVelocity(m.anchor, B((a - was) * (1.0f / dt)));
            b3Body_SetAngularVelocity(m.anchor, B(Log(Mul(q, Conj(wasQ))) * (1.0f / dt)));
            // a body left far behind (held by something heavy, then let go) goes back onto it
            const b3WorldTransform bt = b3Body_GetTransform(m.body);
            if (Len(F(bt.p) - was) > kLost) {
                b3Body_SetTransform(m.body, at.p, at.q);
                b3Body_SetLinearVelocity(m.body, B((a - was) * (1.0f / dt)));
            }
        }
    }

    // Before the step: frozen props a mover is about to reach, or a fast moving thing is about to bump, are set going. With as many moving as allowed, a mover still sets one
    // going: the one moving longest is frozen.
    void Wake(double now, float dt) {
        struct Sweep {
            V a0, b0, a1, b1;
            float r, speed;
            int slot;
        };
        std::array<Sweep, D2RP_MAX_MOVERS> sweeps{};
        int n = 0;
        for (int i = 0; i < D2RP_MAX_MOVERS; ++i) {
            const Mover& m = movers[(size_t)i];
            if (!m.have || !m.in.on || HoldsWith(i)) continue;
            V a1;
            Q q1;
            float len = 0;
            Target(m.in, now + dt, &a1, &q1, &len);
            if (!Finite(a1) || !Finite(q1)) continue;
            const V b1 = a1 + Rotate(q1, { 0, len, 0 });
            V a0 = a1, b0 = b1;
            if (!B3_IS_NULL(m.body) && b3Body_IsValid(m.body)) {
                const b3WorldTransform t = b3Body_GetTransform(m.body);
                a0 = F(t.p);
                b0 = a0 + Rotate(F(t.q), { 0, m.length, 0 });
            }
            const float moved = std::max(Len(a1 - a0), Len(b1 - b0));
            if (moved > kMoverJump || moved > kMoverMaxSpeed * dt) {
                a0 = a1;
                b0 = b1;
            }
            // its speed is the drawn weapon's (the faster of its two ends), not its body's, which may still be catching up
            const float speed = Len(F3(m.in.vel)) + Len(F3(m.in.angVel)) * len;
            sweeps[(size_t)n++] = { a0, b0, a1, b1, std::clamp(m.in.radius, kMoverMinRadius, kMoverMaxRadius) + kWakeMargin, speed, i };
        }
        std::vector<b3AABB> bumpers;
        for (const Prop& pr : props) {
            if (!pr.moving) continue;
            const V v = F(b3Body_GetLinearVelocity(pr.body));
            const float speed = Len(v);
            if (speed < kWakeMoverSpeed) continue;
            b3AABB box = b3Body_ComputeAABB(pr.body);
            const float grow = speed * dt + kWakeMargin;
            box.lowerBound = { box.lowerBound.x - grow, box.lowerBound.y - grow, box.lowerBound.z - grow };
            box.upperBound = { box.upperBound.x + grow, box.upperBound.y + grow, box.upperBound.z + grow };
            bumpers.push_back(box);
        }
        int moving = MovingFree();
        for (Prop& pr : props) {
            if (pr.moving) continue;
            const b3WorldTransform t = b3Body_GetTransform(pr.body);
            const V at = F(t.p);
            const Q q = F(t.q);
            int slot = -1;
            float speed = 0;
            for (int s = 0; s < n && slot < 0; ++s)
                for (int k = 0; k < kWakeSamples; ++k) {
                    const float f = (float)k / (kWakeSamples - 1);
                    const Sweep& w = sweeps[(size_t)s];
                    if (CapsuleTouchesBox(w.a0 + (w.a1 - w.a0) * f, w.b0 + (w.b1 - w.b0) * f, w.r, at, q, pr.centre, pr.half)) {
                        slot = w.slot;
                        speed = w.speed;
                        break;
                    }
                }
            const bool bumped = slot < 0 && moving < p.maxMoving && std::any_of(bumpers.begin(), bumpers.end(), [&](const b3AABB& b) { return Overlap(b, pr.reach); });
            if (slot < 0 && !bumped) continue;
            if (moving >= p.maxMoving) {
                Prop* oldest = nullptr;
                for (Prop& o : props)
                    if (o.moving && !Held(o.in.id) && (!oldest || o.since < oldest->since)) oldest = &o;
                if (!oldest) continue;
                Freeze(*oldest);
                --moving;
            }
            Knocked(pr, slot, speed, now);
            ++moving;
        }
    }

    // After the step: what each mover met (the props' contact impulses on it), the knocks' impulses, the props that stopped.
    void After(double now, float dt) {
        for (Mover& m : movers) {
            m.impulse = 0;
            if (B3_IS_NULL(m.body)) continue;
            b3ContactData cd[16];
            const int nc = b3Body_GetContactData(m.body, cd, 16);
            for (int i = 0; i < nc; ++i)
                for (int k = 0; k < cd[i].manifoldCount; ++k) {
                    const b3Manifold& mf = cd[i].manifolds[k];
                    for (int j = 0; j < mf.pointCount; ++j) m.impulse += mf.points[j].totalNormalImpulse;
                }
        }
        for (Prop& pr : props) {
            if (pr.knock >= 0 && pr.knock < (int)knocks.size()) knocks[(size_t)pr.knock].impulse = b3Body_GetMass(pr.body) * Len(F(b3Body_GetLinearVelocity(pr.body)));
            pr.knock = -1;
        }
        poses.clear();
        for (Prop& pr : props) {
            pr.rested = false;
            if (!pr.moving) continue;
            b3WorldTransform t = b3Body_GetTransform(pr.body);
            V at = F(t.p);
            const bool held = Held(pr.in.id);   // (a held prop moves as long as it is held)
            if (!held && (!Finite(at) || at.y < floorY - 1.0f)) {   // lost through everything: back on the floor where it was, upright
                at = { pr.anchor.x, floorY + pr.half.y - pr.centre.y, pr.anchor.z };
                b3Body_SetTransform(pr.body, B(at), B(F4(pr.in.q)));
                Freeze(pr);
                t = b3Body_GetTransform(pr.body);
            } else {
                const V v = F(b3Body_GetLinearVelocity(pr.body)), w = F(b3Body_GetAngularVelocity(pr.body));
                pr.still = Len(v) < p.restSpeed && Len(w) < kRestTurn ? pr.still + dt : 0.0;
                bool settled = false;
                if (now - pr.anchoredAt >= kDriftTime) {
                    settled = Len(at - pr.anchor) < kDrift && Angle(F(t.q), pr.turned) < kDriftTurn;
                    pr.anchor = at;
                    pr.turned = F(t.q);
                    pr.anchoredAt = now;
                }
                if (!held && (!b3Body_IsAwake(pr.body) || pr.still >= p.restTime || settled || now - pr.since > p.moveLimit)) Freeze(pr);
            }
            D2rpPose o{};
            o.id = pr.in.id;
            o.state = pr.moving ? 1 : 2;
            Put(F(t.p), o.p);
            Put(F(t.q), o.q);
            poses.push_back(o);
        }
    }
};

World::World(const D2rpParams& p) : impl_(std::make_unique<Impl>(Sane(&p))) { impl_->Clear(0.0f); }
World::~World() = default;
bool World::Ok() const { return b3World_IsValid(impl_->world); }
void World::Clear(float floorY) { impl_->Clear(std::isfinite(floorY) ? floorY : 0.0f); }

int World::SetProps(const D2rpProp* in, int n) {
    Impl& m = *impl_;
    if (n < 0 || (n > 0 && !in)) return -1;
    n = std::min(n, D2RP_MAX_PROPS);
    // a frozen prop not in the list goes; a moving one stays until it rests
    for (size_t k = m.props.size(); k-- > 0;) {
        Impl::Prop& pr = m.props[k];
        if (pr.moving) continue;
        bool wanted = false;
        for (int i = 0; i < n && !wanted; ++i) wanted = in[i].id == pr.in.id;
        if (!wanted) {
            Destroy(pr.body);
            m.props.erase(m.props.begin() + (ptrdiff_t)k);
        }
    }
    for (int i = 0; i < n; ++i) {
        const D2rpProp& p = in[i];
        if (!Finite(F3(p.p)) || !Finite(F4(p.q)) || !Finite(F3(p.centre)) || !Finite(F3(p.half)) || p.shape > D2RP_SHAPE_BALL || p.material >= D2RP_MATERIALS) continue;
        if (std::any_of(m.props.begin(), m.props.end(), [&](const Impl::Prop& o) { return o.in.id == p.id; })) continue;
        if ((int)m.props.size() >= D2RP_MAX_PROPS) break;
        m.Add(p);
    }
    return (int)m.props.size();
}

int World::SetStatics(const D2rpBox* boxes, int n) {
    Impl& m = *impl_;
    if (n < 0 || (n > 0 && !boxes)) return -1;
    n = std::min(n, D2RP_MAX_STATICS);
    const bool same = (int)m.staticBoxes.size() == n && std::equal(m.staticBoxes.begin(), m.staticBoxes.end(), boxes,
                                                                   [](const D2rpBox& a, const D2rpBox& b) { return std::memcmp(&a, &b, sizeof a) == 0; });
    if (same && (n == 0 || !B3_IS_NULL(m.statics))) return n;
    Destroy(m.statics);
    m.staticBoxes.assign(boxes, boxes + n);
    if (!n) return 0;
    b3BodyDef def = b3DefaultBodyDef();
    m.statics = b3CreateBody(m.world, &def);
    int made = 0;
    for (int i = 0; i < n; ++i) {
        const D2rpBox& b = boxes[i];
        const V lo = F3(b.lo), hi = F3(b.hi);
        if (!Finite(lo) || !Finite(hi) || hi.x - lo.x < 0.002f || hi.y - lo.y < 0.002f || hi.z - lo.z < 0.002f) continue;
        const V c = (lo + hi) * 0.5f, h = (hi - lo) * 0.5f;
        b3BoxHull box = b3MakeOffsetBoxHull(h.x, h.y, h.z, B(c));
        b3ShapeDef shape = b3DefaultShapeDef();
        shape.baseMaterial.friction = std::isfinite(b.friction) ? std::clamp(b.friction, 0.0f, 2.0f) : 0.6f;
        shape.baseMaterial.restitution = std::isfinite(b.bounce) ? std::clamp(b.bounce, 0.0f, 1.0f) : 0.1f;
        shape.filter = StaticFilter();
        b3CreateHullShape(m.statics, &shape, &box.base);
        ++made;
    }
    return made;
}

int World::SetMovers(const D2rpMover* in, int n) {
    Impl& m = *impl_;
    if (n < 0 || (n > 0 && !in)) return -1;
    for (int i = 0; i < D2RP_MAX_MOVERS; ++i) {
        Impl::Mover& mv = m.movers[(size_t)i];
        if (i < n && in[i].on && Finite(F3(in[i].a)) && Finite(F3(in[i].b)) && Finite(F3(in[i].vel)) && Finite(F3(in[i].angVel)) && std::isfinite(in[i].time)) {
            mv.in = in[i];
            mv.in.hands = std::clamp(mv.in.hands, 1, 2);
            mv.have = true;
        } else {
            mv.have = false;
            mv.in.on = 0;
        }
    }
    return 0;
}

int World::Step(double now, float dt) {
    Impl& m = *impl_;
    if (!Ok() || !std::isfinite(now) || !(dt > 1e-4f && dt <= 0.1f)) return -1;
    m.Wake(now, dt);
    if (m.holder) m.holder->BeforeStep(now, dt);
    m.Drive(now, dt);
    b3World_Step(m.world, dt, m.p.substeps);
    m.After(now + dt, dt);
    if (m.holder) m.holder->AfterStep();
    return m.MovingCount();
}

int World::Poses(D2rpPose* out, int max) const {
    const int n = std::min(max, (int)impl_->poses.size());
    for (int i = 0; i < n; ++i) out[i] = impl_->poses[(size_t)i];
    return std::max(0, n);
}

int World::Knocks(D2rpKnock* out, int max) {
    std::vector<D2rpKnock>& k = impl_->knocks;
    const int n = std::min(max, (int)k.size());
    for (int i = 0; i < n; ++i) out[i] = k[(size_t)i];
    k.clear();
    for (Impl::Prop& pr : impl_->props) pr.knock = -1;
    return std::max(0, n);
}

int World::Mover(int slot, D2rpMoverOut* out) const {
    if (!out || slot < 0 || slot >= D2RP_MAX_MOVERS) return 0;
    const Impl::Mover& mv = impl_->movers[(size_t)slot];
    *out = D2rpMoverOut{};
    if (B3_IS_NULL(mv.body)) return 0;
    const b3WorldTransform t = b3Body_GetTransform(mv.body);
    out->on = 1;
    Put(F(t.p), out->a);
    Put(F(t.p) + Rotate(F(t.q), { 0, mv.length, 0 }), out->b);
    out->impulse = mv.impulse;
    return 1;
}

void World::SetHolder(Holder* holder) { impl_->holder = holder; }
b3WorldId World::Box3D() const { return impl_->world; }
const D2rpParams& World::Params() const { return impl_->p; }
int World::PropCount() const { return (int)impl_->props.size(); }

PropView World::View(int index) const {
    PropView v;
    if (index < 0 || index >= PropCount()) return v;
    const Impl::Prop& pr = impl_->props[(size_t)index];
    v.in = &pr.in;
    v.body = pr.body;
    v.centre = pr.centre;
    v.half = pr.half;
    v.moving = pr.moving;
    return v;
}

bool World::Find(uint32_t id, PropView* out) const {
    for (int i = 0; i < PropCount(); ++i)
        if (impl_->props[(size_t)i].in.id == id) {
            if (out) *out = View(i);
            return true;
        }
    return false;
}

void World::Lift(uint32_t id, double now) {
    if (Impl::Prop* pr = impl_->PropById(id)) impl_->Lift(*pr, now);
}
void World::Restart(uint32_t id, double now) {
    if (Impl::Prop* pr = impl_->PropById(id)) impl_->Restart(*pr, now);
}
void World::Carry(uint32_t id, float kg) {
    if (Impl::Prop* pr = impl_->PropById(id)) impl_->Carry(*pr, kg);
}
void World::Uncarry(uint32_t id) {
    if (Impl::Prop* pr = impl_->PropById(id)) impl_->Uncarry(*pr);
}

bool World::Held(uint32_t id) const { return impl_->Held(id); }

int World::Moving() const { return impl_->MovingCount(); }
int World::Frozen() const { return (int)impl_->props.size() - impl_->MovingCount(); }

bool World::PropAt(uint32_t id, float p[3], float q[4]) const {
    for (const Impl::Prop& pr : impl_->props)
        if (pr.in.id == id) {
            const b3WorldTransform t = b3Body_GetTransform(pr.body);
            Put(F(t.p), p);
            Put(F(t.q), q);
            return true;
        }
    return false;
}

float World::PropMass(uint32_t id) const {
    for (const Impl::Prop& pr : impl_->props)
        if (pr.in.id == id) {
            // a frozen (static) body has no mass of its own in Box3D: the shape's
            b3ShapeId s;
            if (b3Body_GetShapes(pr.body, &s, 1) == 1) return b3Shape_ComputeMassData(s).mass;
        }
    return 0.0f;
}

}  // namespace props
