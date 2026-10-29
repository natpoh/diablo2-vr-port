// SPDX-License-Identifier: MIT
// props_world.h - the level's small props knocked about by the held weapon and the fists, for D2R VR (the D2rp* interface, include/d2r_vr_props.h).
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
//
// The rigid bodies, their contacts and their sleep are Box3D's (Erin Catto, MIT, v0.1.0, compiled in unmodified, serial: it starts no threads in the game). What is ours is the
// weapon's drive: each mover is a dynamic body of the weapon's own mass pulled to the weapon as the mod draws it (that is already the weapon of the mod's weight model, lagging
// and sagging by its muscles) by a motor joint whose force and torque ceilings are a hand's pull on its weapon (300 N, 75 N m per hand), times the strength setting - so a cup
// flies off a maul harder than off a dagger, and a barrel holds a light sword back. The idea of knocking the level's props about, and how frozen props are woken, held on what
// they stand on and frozen again, are bearstonem/d2r-pcvr's knockable props (MIT, 2026-10-22: steamvr/dropped_bottles.cpp); the numbers of the materials are his.
//
// Collision groups: the floor and the statics meet moving props only; a frozen prop is a static body that meets moving props only (a mover passes through it unless the wake
// test set it going first); a mover meets moving props only (never the floor or a wall: it is a copy of the drawn weapon, which goes where the hands go).
//
// What else may act in the world (hands that take props, say) comes in as a World::Holder: the world asks it what it holds - a held prop is kept moving and is not one of the
// props maxMoving counts - and steps it with its own step. With none the world is the props and the movers alone.
#pragma once

#include <cstdint>
#include <memory>

#include <box3d/box3d.h>

#include "d2r_vr_props.h"
#include "props/props_math.h"

namespace props {

D2rpParams Defaults();
D2rpParams Sane(const D2rpParams* p);   // the caller's; a shorter struct's missing fields from Defaults; every number held to a sane range

// The material table: density (kg/m3 of the solid hull: vessels are hollow, so less than their stuff), friction, bounce, rolling resistance.
struct Material {
    float density, friction, bounce, rolling;
};
const Material& MaterialOf(int material);

// The hands' drive on a mover, per hand: the force and torque ceilings (N, N m) and how stiff (Hz) and damped (share of critical) its spring is.
struct HandDrive {
    float maxForce, maxTorque, linearHz, angularHz, damping;
};
constexpr float kHandForce = 300.0f;   // N: a hand's pull on its weapon at strength 1
constexpr float kHandTorque = 75.0f;   // N m: and its turn
HandDrive DriveOf(int hands, float strength);

constexpr float kAhead = 0.05f;             // s: a mover's pose is carried on by its velocity at most this far past its sample
constexpr float kMoverMaxSpeed = 30.0f;     // m/s: a mover that moves faster is put there (a warp, a recentre), no sweep

// A prop as what acts in the world sees it (World::Find / World::View): valid until the props change.
struct PropView {
    const D2rpProp* in = nullptr;   // as the mod gave it
    b3BodyId body = b3_nullBodyId;
    math::V centre{}, half{};       // its shape's bounds in its body's frame (the shape a little inside the mod's bounds)
    bool moving = false;            // a dynamic body (else frozen: a static one)
};

class World {
public:
    // Something else that acts in the world (none by default). Called from the world's own calls only, on its thread.
    class Holder {
    public:
        virtual bool Holds(uint32_t id) const = 0;    // it holds this prop: kept moving (never frozen, never set back), not counted by maxMoving, never frozen for another
        virtual bool HoldsWith(int slot) const = 0;   // that mover slot's hand holds something: its mover is not driven and wakes nothing
        virtual void BeforeStep(double now, float dt) = 0;   // after the wake test, before the movers are driven
        virtual void AfterStep() = 0;                // after the props' rest and poses
        virtual void Clearing() = 0;                 // the world is cleared: let go of everything, drop its bodies (the props are still there)

    protected:
        ~Holder() = default;
    };

    explicit World(const D2rpParams& p);
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    bool Ok() const;
    void Clear(float floorY);
    int SetProps(const D2rpProp* props, int n);
    int SetStatics(const D2rpBox* boxes, int n);
    int SetMovers(const D2rpMover* movers, int n);
    int Step(double now, float dt);
    int Poses(D2rpPose* out, int max) const;
    int Knocks(D2rpKnock* out, int max);
    int Mover(int slot, D2rpMoverOut* out) const;

    // ---- for what acts in the world ----
    void SetHolder(Holder* holder);
    b3WorldId Box3D() const;
    const D2rpParams& Params() const;   // as Sane made them
    int PropCount() const;
    PropView View(int index) const;     // in the world's own order
    bool Find(uint32_t id, PropView* out) const;
    void Lift(uint32_t id, double now);      // set it going without a knock (nothing struck it): as many moving as allowed, the one moving longest that is not held frozen for it
    void Restart(uint32_t id, double now);   // its time to rest starts now, from where it is (let go)
    void Carry(uint32_t id, float kg);       // moving: it weighs kg (if less than its own mass; its inertia with it) until Uncarry puts its own back
    void Uncarry(uint32_t id);

    // for the tests
    int Moving() const;
    int Frozen() const;
    bool PropAt(uint32_t id, float p[3], float q[4]) const;
    float PropMass(uint32_t id) const;
    bool Held(uint32_t id) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace props
