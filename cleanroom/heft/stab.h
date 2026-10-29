#pragma once
// Stab: the seam of [weight] stab_in - a thrusting weapon driven point first into a monster's body goes IN instead of stopping at the surface (docs/notes/monster_bodies.md,
// "Sticking in"). The stab itself - whether a strike goes in, the joint that holds the weapon in the wound, its resistance, its lock and when it comes out - is the optional
// weight-physics library's (D2rwStab in shared/d2r_vr_weight.h; vr/weight_lib.cpp puts it behind Engine). Nothing of it is in this repository: here are only what the mod
// hands it (the settings from its sliders, the strike its own contact met, the hands' gaps), what it hands back (Report) and the interface. Without the library there is no
// stab: every weapon stops at the surface or glances off (contact.h), as before.
//
// The author (2026-10-22): «капсулы монстров отображаются теперь давай сделаем чтобы колющее оружие в них втыкалось», then that the stab be taken as it is from the toolkit
// the weight library comes from and be an external library.
//
// What the mod keeps (hold.cpp): the bodies (a monster's limbs, refreshed every step, each with its monster's id and its limb's part), its own contact, the gate (hit by
// touch's thrust rule and path: Hold::SetStabbing), the hands' gaps (each holding hand's controller from its hand drawn on the weapon), and while the weapon is in a wound
// it leaves that monster's capsules out of its contact. Only the drawn weapon and how it moves: whether a touch is an attack, and what it does, stay hit by touch's.
// Units: metres, kilograms, seconds, newtons; the room frame of heft.h.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "contact.h"
#include "heft.h"

namespace heft::stab {

// The settings the mod hands the library (D2rwStabber's): its sliders, and the rest at the numbers the library takes for its own (the toolkit's; the tear, the wait for a
// missing body and the zone of the point are the mod's).
struct Params {
	double minSpeed     = 0.5;      // m/s: the point's speed against the body at the contact (the toolkit's own: 1)
	double angleCos     = 0.5;      // the stab line against the inward normal at least this (60 degrees)
	double tipM         = 0.10;     // m: the contact within this of the tip along the line - the point struck, not the edge or the shaft
	double sharpness    = 0.3;      // 0..1: the dampers times (1 - sharpness)
	double shellDamper  = 5000.0;   // N s/m until the point has first been shellM deep
	double shellM       = 0.025;    // m
	double fleshDamper  = 1000.0;   // N s/m past it
	double depthLimit   = 0.0;      // m: the deepest; <= 0 the blade's (the point's piece) length
	double lockSpeed    = 0.1;      // m/s: the weapon slower than this against the body - the slide locks
	double lockTime     = 0.25;     // s: ... and unlocks when the joint's force stays over unlockForce this long
	double unlockForce  = 150.0;    // N (our hands pull 300 N at most: the toolkit's 300 would never unlock); 0 = never locks
	double unstabM      = 0.01;     // m: the point behind the entry by more than this - drawn out
	double unstabDelay  = 0.25;     // s after going in before it may come out
	double perpM        = 0.20;     // m: the entry this far off the stab line - out
	double breakM       = 0.45;     // m: a controller this far ACROSS the line from its hand drawn on the weapon - torn free
	double missingS     = 0.3;      // s: the body missing from the bodies this long - gone
};

// The mod's sliders ([weight] stab_min_speed, stab_max_angle, stab_resist_pct, stab_depth_cm, stab_tear_cm, stab_unlock_n) as the numbers above: the least
// speed (m/s), the widest angle against straight in (deg), the resistance (% of the dampers - both the shell's and the flesh's), the depth limit (cm; 0 the blade's), the
// tear across the line (cm), the pull that unlocks (N). Out of range: clamped; not a number: the default.
struct Sliders {
	double minSpeed = 0.5, maxAngleDeg = 60, resistPct = 100, depthCm = 0, tearCm = 45, unlockN = 150;
};
inline Params FromSliders(const Sliders& s, Params p = Params {}) {
	const auto fin = [](double v, double lo, double hi, double def) { return std::isfinite(v) ? std::clamp(v, lo, hi) : def; };
	const Params d;
	p.minSpeed    = fin(s.minSpeed, 0.05, 5.0, d.minSpeed);
	p.angleCos    = std::cos(fin(s.maxAngleDeg, 5.0, 89.0, 60.0) * 0.017453292519943295);
	const double k = fin(s.resistPct, 0.0, 300.0, 100.0) * 0.01;
	p.shellDamper = d.shellDamper * k;
	p.fleshDamper = d.fleshDamper * k;
	p.depthLimit  = fin(s.depthCm, 0.0, 100.0, 0.0) * 0.01;
	p.breakM      = fin(s.tearCm, 5.0, 300.0, 45.0) * 0.01;
	p.unlockForce = fin(s.unlockN, 0.0, 2000.0, d.unlockForce);
	return p;
}

// The library's state of the wound between steps: the mod keeps it, zero = in none.
struct Blob {
	alignas(8) unsigned char bytes[256] = {};
};

// A contact the mod's own contact met this step, before it put the weapon back; `before` the weapon then (the speed it came in with).
struct Strike {
	int   body = -1;      // among the step's bodies
	V3    point, normal;  // where it met (on the weapon's surface), the body's outward normal there
	bool  thrusting = false;
	State before;
};

// What the stab did in one step (Hold::LastStab).
struct Report {
	bool        entered = false;   // went in this step
	bool        in = false;        // is in after the step
	bool        left = false;      // came out this step (why says)
	bool        locked = false;    // the slide locked
	char        why[64] = {};      // why a strike did not go in, or why it came out
	uint32_t    id = 0;
	double      depth = 0;         // m past the entry (after the step)
	double      deepest = 0;       // m the most of this wound
	double      speed = 0;         // m/s the point's speed at the strike
	double      lateral = 0;       // m the entry off the stab line
	double      pull = 0;          // m the controller from its hand on the weapon, across the line
	double      force = 0;         // N the joint's force
	double      limit = 0;         // m the deepest it may go
	V3          entry, axis;       // room: the wound's entry and its line's way in
	// a strike that was judged, going in or not: what it was judged on (the debug's readout)
	bool        judged = false;
	bool        thrusting = false;
	double      cosInto = 0, fromTip = 0;
	V3          point;             // room: where the contact met the body
};

// The library behind the seam (vr/weight_lib.cpp). One step: `st` the weapon (OUR body's state, about b's centre of mass); tip / base the stab line in the grip frame;
// bodies the step's (n 0: none came); strike the contact met (null: none - or the weapon is in a wound); gaps each holding hand's controller from its hand on the weapon.
// Returns 1 in a wound after it (st may have been changed), 0 in none, < 0 the library refused (nothing changed).
class Engine {
public:
	virtual ~Engine() = default;
	virtual int Step(State& st, const Body& b, V3 tipLocal, V3 baseLocal, const contact::Capsule* bodies, int n, const Strike* strike, const V3* gaps, int nGaps, double dt,
	                 const Params& p, Blob* blob, Report* out) const = 0;
};
// The library's stab, when one is loaded (null: no stab). Set once by the loader; read by Hold every step.
void SetEngine(const Engine* e);
const Engine* GetEngine();

}   // namespace heft::stab
