#pragma once
// Walls: the held weapon and a bare hand meet the level's solid things - its walls, pillars, big props - each a box ([weight] world_contact, F4 only).
// Our own model, written for D2R VR.
//
// A wall is a box in the room (its centre, its own axes, half its size). The weapon is Heft's body (heft.h) as in contact.h: after a step its pieces
// (capsules) found in a wall are put back on the wall's surface and their motion into it is taken away at the point where they meet, through the weapon's
// mass and inertia - a blow near the tip turns the weapon about the hand, a little of it comes back as a bounce, the slide along the face is held back by
// friction. The controllers go on into the wall; the muscles pull the weapon after them as in any lag, and it follows again once they come out.
//
// Which face: a box has six and a fast swing goes deeper in one step than a thin wall is thick (a palisade, a door). The weapon is put back out of the face
// it was outside of BEFORE the step (the faces the weapon was beyond, or resting on, wherever it was over them) - the one of those it is least deep past now. So a
// blade that went through a thin wall in one step comes back out on the side it came from, never out of the far side, and a blade that slid round a
// corner leaves by the face it was beside. With no such face (the weapon was inside before: put back only part of the way, or there when the wall was
// first given) the face the wall last let it out of stands; with none known the wall is left alone until the weapon is clear of it (a wall that came
// up round the weapon - a new scan, a teleport - is not a reason to throw it across the room), and so is a wall the weapon is found deeper in than it
// is thick (Params::through: it got there some other way).
//
// A bare hand (Block) is a capsule moved, not turned: the hand drawn at the surface while the controller is past it, sliding along; and when the
// controller is farther past it than Params::returnGap the hand goes back to the controller and that wall is left alone until the hand is clear of it
// (a hand stuck behind a wall forever is worse than one through it).
//
// Nothing of the game's is touched: only where the weapon and the hands are drawn.
//
// Units: metres, kilograms, seconds; any one frame for all (the weight model's room).

#include <cstdint>

#include "contact.h"
#include "heft.h"

namespace heft::walls {

struct Wall {
	V3       c;                                              // its centre
	V3       axis[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };   // its own axes (unit, at right angles)
	V3       half;                                           // half its size along each, m
	uint32_t id = 0;                                         // the box (a piece's part: the memory below is kept by it)
	uint32_t body = 0;                                       // the one object it is part of (touching pieces of a wall in a row are one): its contact, its
	                                                         // face held, its strike are the body's; 0: the box is its own
	uint8_t  open = 0x3F;                                    // the faces it may put the weapon (the hand) out of, a bit each (face numbering below): [weight]
	                                                         // wall_collide's cells close a side another wall cell stands against - the cells of one wall are one
	                                                         // face, no seam catches a blade sliding along (vr/unleashed_wall_cells.h); all six: any face, as before
};

constexpr int kMaxWalls = 256;   // (the level pieces' parts near the hands: shared/world_solids.h)
// A face: axis f / 2, outward along +axis (f even) or -axis (f odd).
constexpr int kFaces = 6;

// What is kept of each wall between steps: the face the weapon (or the hand) was last let out of, and whether the wall is left alone until it is clear.
struct Side {
	uint32_t id = 0;
	int      face = -1;
	bool     ignore = false;
	uint32_t seen = 0;
};
// What is kept of each body: the way it last put the weapon (the hand) out - while the weapon rests on it, slides along it, rides over a step of it (Params::holdSteps
// after the last contact), its boxes put it out that way only: a stone wall of many boxes is one face, not a jitter between the boxes' faces.
struct BodySide {
	uint32_t body = 0;
	V3       normal;
	uint32_t seen = 0;
};
struct Memory {
	static constexpr int kSides = 2 * kMaxWalls;
	static constexpr int kBodies = 16;
	Side     s[kSides];
	int      n = 0;
	BodySide b[kBodies];
	int      nb = 0;
	uint32_t tick = 0;
	void      Clear() { n = 0; nb = 0; }
	Side*     Of(uint32_t id);   // made when not there (the oldest given up when full)
	BodySide* Body(uint32_t body, bool make);
	void      Forget();          // the walls not given for a few steps go
};

struct Params {
	double friction    = 0.3;    // Coulomb's, as contact.h: a share of the push it takes out of the motion into the wall
	double restitution = 0.15;   // how much of that motion comes back as a bounce
	double maxPush     = 0.30;   // m: the most the weapon is put back in one round
	double through     = 0.25;   // m past the far side (beyond the wall's thickness) a blade is still put back; deeper: left alone (it got there some other way)
	double rest        = 0.02;   // m: a weapon this little in a face still counts as outside it (it rests on it after the last step's push)
	double returnGap   = 0.45;   // m: a bare hand whose controller is this far past where it is drawn goes back to the controller (Block)
	int    holdSteps   = 8;      // steps a body's face is held after the last contact with it
	int    iterations  = 3;
};

// The strongest contact of one step.
struct Report {
	int      walls = 0;      // how many walls the weapon (the hand) touched
	uint32_t id = 0;         // the body met hardest (its box's id when it has no body)
	int      wall = -1;      // ... its index
	int      face = -1;
	double   depth = 0;      // m: how deep it was before it was put back
	double   approach = 0;   // m/s: its speed into it (what was taken away)
	double   slide = 0;      // m/s: along the face after the friction
	double   impulse = 0;    // N s
	int      blade = 0;
	double   along = 0;      // where on that piece: 0 its butt, 1 its tip
	V3       normal, point;  // the face's outward normal, the point of contact
	int      ignored = 0;    // walls left alone this step (the weapon inside them, no face to put it out of)
	int      returned = 0;   // Block: walls the hand went back through this step (the controller past Params::returnGap)
	double   gap = 0;        // Block: m, the controller from the hand drawn
};

// The face (a bit each in `faces`: those it may leave by) a capsule from la to lb (the wall's own frame), radius r, is put back out of, and how deep it is
// past it (*depth; *s where along the capsule it is deepest): in the box widened by r, the least deep of those faces - none when it only touches the box;
// clear of the box, a face whose part of the capsule over it is past the far side (it went through in one step). -1: none.
int Pick(V3 la, V3 lb, double r, V3 half, int faces, double* depth, double* s);
// A point in the wall's own frame.
V3 Local(const Wall& w, V3 p);
// How far the part of the capsule over face f (within its sides widened by r) is beyond its plane (>= 0: clear of it - the capsule's surface, so one resting
// on the face is 0; a very large number: no part of it over the face).
double Beyond(V3 la, V3 lb, double r, V3 half, int face);

// After a step of the weapon: put back out of the walls, its motion into them taken away (st changes; b is the body st is of). before: the state before the
// step (which face it was outside of, and the friction on the slide); null: only the faces remembered. Pieces with Blade::only set (a head's volume for the
// other weapon of a pair) are left out. Returns true when the weapon touched a wall.
bool Resolve(State& st, const State* before, const Body& b, const contact::Blade* blades, int nBlades, const Wall* walls, int n, Memory& mem, const Params& p, Report* out);

// A bare hand: the capsule from a to b (where the controller has it now), radius r; wasA / wasB where it was DRAWN at the last step (null: not known). Returns
// how far to move it (add it to a and b) so it stands outside the walls.
V3 Block(V3 a, V3 b, double r, const V3* wasA, const V3* wasB, const Wall* walls, int n, Memory& mem, const Params& p, Report* out);

}   // namespace heft::walls
