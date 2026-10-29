#pragma once
// Contact: the held weapon meets a monster's body (docs/notes/monster_bodies.md, step 1). Our own model, written for D2R VR.
//
// A monster is a capsule (or a few) in the room, posed each step from what the game gives. The weapon is Heft's body (heft.h):
// one rigid body of the steel and the hands on it, pulled to the controllers by the muscles. When the blade - the weapon's
// pieces of steel, each a capsule as thick as it is - is found inside a body after a step, the weapon is put back
// on the body's surface and its motion into the body is taken away, at the point where the two meet, through the weapon's own
// mass and inertia: a hit near the tip turns the weapon about the hand, a hit at the hilt moves it all. What is left of the
// motion along the surface is held back by friction - a little for an edge (a slash at a shallow angle glances off and slides
// along the body), much for a blunt weapon (it stops where it struck). Nothing bounces.
//
// The hand stays the player's: the controllers go on into the monster, only the weapon (and the hands as they are drawn on it)
// is held at the surface - the muscles pull it after the controller as they do in any lag, and it follows again once the
// controller comes out. Nothing of the game's is touched: whether a touch is an attack stays the hit by touch's (vr/melee_touch.h).
//
// Units: metres, kilograms, seconds; the room frame of heft.h.

#include <cstdint>

#include "heft.h"

namespace heft::contact {

// A body in the room: a capsule from `a` to `b` (a sphere when they are one point), `radius` round it. `vel`: how it moves (m/s, room),
// zero when not known. `id`: the game's unit id, for the report.
struct Capsule {
	V3       a, b;
	double   radius = 0.3;
	V3       vel;
	uint32_t id = 0;
	int      part = -1;   // which of the monster's limbs (its skeleton's bone; -1: the monster's one capsule) - the stab follows the limb it is in (stab.h)
	int      held = 0;    // [weight] monster_shields: 0 its body; 1 a weapon it holds, 2 its shield - the weapon stops on it as on a limb, never stabs into it
	// [weight] dual_collide: the other weapon of a pair - never left inside (Params::deepShare is for a body that walked onto the blade): a blade deep in it is
	// eased out, kFirmEase a round, so two weapons put into each other (a fresh javelin after a throw, a line measured anew) come apart and then rest on each other
	bool     firm = false;
};
constexpr double kFirmEase = 0.03;   // m

// A piece of the weapon as a capsule in the body's GRIP frame (heft.h Body): a blade, a haft, a mace's head - `radius` round it. The weapon is
// up to a few of them (its steel's pieces, hold.h Shape).
struct Blade {
	V3     butt, tip;
	double radius = 0.02;
	uint32_t only = 0;   // 0: it meets every capsule; else only the capsules of this id (a head's own volume against the other weapon of a pair, hold.h SetHead)
	uint32_t except = 0; // ... and never the capsules of this id (0: none) - the kind's own head piece, where the head's own volume stands for it (hold.h SetHeads)
};
inline bool Meets(const Blade& b, const Capsule& c) { return (!b.only || b.only == c.id) && (!b.except || b.except != c.id); }

struct Params {
	double friction    = 0.25;   // Coulomb's: the most the surface holds back of the slide, as a share of the push it takes out of the blade's motion into the body
	double restitution = 0.0;    // how much of that motion comes back as a bounce (0: none)
	double maxPush     = 0.10;   // m: the most the weapon is pushed out of one body in one step (a body that jumped onto the blade comes off it over a few steps)
	double deepShare   = 0.6;    // a blade deeper in a body than this share of the body's radius is left in it (it got there some other way - a scan that jumped, the body
	                             // walking onto it): pushing it out would throw the weapon across the room
	int    iterations  = 3;      // rounds over the bodies (one blade against two bodies at once)
};

// What the contact did in one step: the strongest contact of it.
struct Report {
	int      bodies = 0;         // how many bodies the blade was touching after the step
	uint32_t id = 0;             // the one it met hardest
	int      body = -1;          // ... its index among the capsules given
	double   depth = 0;          // m: how deep the blade was in it before it was put back
	double   approach = 0;       // m/s: the blade's speed into it there (what was taken away)
	double   slide = 0;          // m/s: its speed along the surface after the friction (> 0: it glances along)
	double   slid = 0;           // m: how far it slid along the surface in this step, after the friction
	double   impulse = 0;        // N s: the push the surface gave the weapon
	int      blade = 0;          // which piece of the weapon met it
	double   along = 0;          // where on that piece: 0 its butt end, 1 its tip end
	V3       normal, point;      // the surface's outward normal there and the point of contact (room)
	// The step's totals against the monster met hardest (all its capsules, all rounds) - what the weapon delivered into its body this step ([debug] strike_measure,
	// docs/notes/strike_force.md): the bodies do not move, so all the contact takes out of the weapon's motion is the push it gave them.
	double   stepImpulse = 0;    // N s: the velocity taken out into the body (the normal kicks)
	double   stepPush = 0;       // kg m: the weapon put back out of the body (the normal shifts: a push of this over the step's dt is an impulse, N s)
	double   stepFriction = 0;   // N s: the slide held back by the surface (the tangential kicks)
	double   stepSlid = 0;       // m: how far the blade slid along the body this step (after the friction)
	// ... and the same for each monster touched this step (a blade caught between two: each its own share; the first kMet, the hardest met first)
	struct Met {
		uint32_t id = 0;
		int      body = -1;          // its capsule met hardest (the bone)
		double   impulse = 0, push = 0, friction = 0, slid = 0;
		double   approach = 0;       // m/s: the closing speed into it (the fastest point's)
		bool     onBody = false, onHeld = false;   // it touched a capsule of its body / of what it holds (Capsule::held) - on the held one alone: blocked
	};
	static constexpr int kMet = 4;
	Met      met[kMet];
	int      nMet = 0;
};

// The closest points of two segments p0-p1 and q0-q1: s and t along them (0..1).
void ClosestOnSegments(V3 p0, V3 p1, V3 q0, V3 q1, double* s, double* t);

// After a step of the body: the weapon put back out of the bodies and its motion into them taken away (st changes; b is the body st is of). before: the state before the
// step (the friction holds back how far the blade slid along a surface in it); null: no friction on the slide. `skip`: a bit per capsule that is
// left alone (bit i for caps[i], i < 64: a capsule the hand is inside of - there is nothing for it to push against). Returns true when the blade touched any body.
bool Resolve(State& st, const State* before, const Body& b, const Blade* blades, int nBlades, const Capsule* caps, int n, const Params& p, uint64_t skip, Report* out);

// Is a point inside a capsule (within its radius plus `margin`)?
bool Inside(const Capsule& c, V3 p, double margin = 0.0);

}   // namespace heft::contact
