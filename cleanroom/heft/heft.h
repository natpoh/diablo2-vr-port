#pragma once
// Heft: a held weapon that has weight. Our own model, written for D2R VR
// (docs/WEAPON_WEIGHT.md); HurricaneVR serves only as the reference it is
// measured against (tools/heft_bench_unity), none of its code is here.
//
// The hand is not a copy of the controller. The weapon and the hand(s) on it
// are one rigid body; each controller pulls its hand toward itself through a
// muscle - a spring and a damper with a ceiling on force and on torque - and
// gravity pulls the body down. A dagger is light enough that the ceilings are
// never reached and it sits in the fist; a polearm is not: it lags, it is slow
// to start and to stop, and held out level in one hand its point sinks.
//
// Units: metres, kilograms, seconds, radians; any right-handed frame with
// `World::gravity` pointing down.

#include <cstdint>

namespace heft {

struct V3 {
	double x = 0, y = 0, z = 0;
};
struct Q {   // unit quaternion, (x, y, z) vector part
	double x = 0, y = 0, z = 0, w = 1;
};
struct M3 {   // row-major 3x3
	double m[3][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
};

struct Pose {
	V3 pos;
	Q  rot;
};

// The held body in its own frame - the grip frame: origin where the main hand
// holds, axes as the weapon's. Mass, centre of mass and inertia are of the
// weapon and the hands together.
struct Body {
	double mass = 1;
	V3     com;                     // grip frame
	M3     inertia;                 // about the centre of mass, grip-frame axes, kg m^2
	double maxAngularSpeed = 30;    // rad/s
	// What gravity pulls: the steel, at its own centre. A hand has mass (it is slow to
	// turn) but no weight - the arm carries itself.
	double weight = 1;              // kg
	V3     weightAt;                // grip frame
};

// A cylinder of the weapon, for building a Body: from `a` to `b` in the grip frame.
struct Piece {
	V3     a, b;
	double radius = 0.02;
	double mass   = 0;
};
// A weapon of pieces plus hands (`handMass` each at `hands[i]`, weightless).
Body Build(const Piece* pieces, int n, const V3* hands, int nHands, double handMass);

// How hard a hand pulls the body to its controller. The ceilings are the weight:
// below them the body is where the hand is, at them it falls behind.
struct Muscle {
	double spring = 3000, damper = 300, maxForce = 300;             // N/m, N s/m, N (per axis)
	double torqueSpring = 500, torqueDamper = 50, maxTorque = 75;   // N m/rad, N m s/rad, N m
};

// One hand on the body.
struct Grip {
	V3     at;        // the palm, grip frame
	Q      rot;       // the hand in the grip frame: hand = body * rot
	Muscle muscle;
	bool   on = true;
};

// A controller as the hand wants to be: its pose and its velocities (the
// muscle damps toward the controller's motion, not toward rest).
struct Controller {
	Pose pose;
	V3   vel, angVel;   // world; leave zero when not known
};

struct State {
	V3   com;          // world
	Q    rot;          // the grip frame in world
	V3   vel, angVel;  // of the centre of mass, world
	bool placed = false;
};

struct World {
	V3     gravity { 0, -9.81, 0 };
	double substep  = 1.0 / 360.0;   // s; a frame is cut into steps no longer than this
	double snapDistance = 1.2;       // m between a hand and its controller: lost, put back in the fist
	// The controllers move smoothly across the frame (true), or stand at the frame's
	// end for all of it, as a physics engine sees a teleported target (false).
	bool   glide = true;
	int    iterations = 8;          // rounds the muscles share each step in (two hands: their joint answer)
};

// Puts the body in the hand at rest, the hand exactly on its controller.
void Place(State& s, const Body& b, const Grip& g, const Pose& controller);
// The grip frame / a hand / a point of the body (grip frame) in world.
Pose GripPose(const State& s, const Body& b);
Pose HandPose(const State& s, const Body& b, const Grip& g);
V3   PointAt(const State& s, const Body& b, const V3& local);
V3   PointVelocity(const State& s, const Body& b, const V3& local);

// Advances `dt` seconds. The controllers move linearly across the frame from
// `from` to `to` (pass the same array twice when there is no earlier pose).
// Grips that are off pull nothing. Returns true when the body was put back in
// the hand (snap distance passed).
bool Step(State& s, const Body& b, const Grip* grips, const Controller* from, const Controller* to, int n, double dt,
          const World& w);

// Small helpers the bench and the mod share.
V3     Add(V3 a, V3 b);
V3     Sub(V3 a, V3 b);
V3     Mul(V3 a, double k);
double Dot(V3 a, V3 b);
V3     Cross(V3 a, V3 b);
double Len(V3 a);
Q      QMul(Q a, Q b);
Q      QConj(Q q);
Q      QNorm(Q q);
V3     Rotate(Q q, V3 v);
Q      QFromAxisAngle(V3 axis, double angle);
V3     RotVec(Q q);                      // the rotation as axis * angle, angle in [0, pi]
Q      QSlerp(Q a, Q b, double t);
// The principal axes of a symmetric inertia: moments in `diag`, axes as `rot` (I = R diag R^T).
void   Principal(const M3& inertia, V3* diag, Q* rot);

}   // namespace heft
