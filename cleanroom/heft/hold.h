#pragma once
// Hold: a weapon in D2R VR's first-person hands (docs/plan_weapon_weight.md, step 1b) - Heft's body
// with hands that take it and let it go.
//
// - A hand takes the weapon where its grip closes on it, anywhere along the shaft, and holds it
//   there: the fist slides onto the shaft and lies along it.
// - The weight is felt through where it is held: a spear held at its end sinks and is slow to
//   swing, held near its middle it is not; two hands apart on a shaft hold it better than one.
// - A hand holds it while its grip is held ("full control over the grip", the user 2026-10-13): let a
//   grip go and that hand lets go at once; the other hand, if it holds, keeps it - from hand to hand.
// - Both let go, it falls: it is heavy. It drops with the motion it had, hits the floor with an end,
//   tips over about it and lies there. Any hand takes it up where it closes on it; the grip of the hand
//   the game holds it in, pressed away from it, calls it back - it flies into that fist.
// - Placed (a new weapon, a swap) it sits in the game's hand without the grip, until that grip is let go
//   for the first time - else it fell at every swap.
// - Stowed (Settings::stowAfter >= 0): a weapon that has landed and lain for that long goes to the hero's
//   back - the mod draws it slung there (the body's pose is told with SetStowPose). A hand behind a shoulder
//   (HandIn::behind) pressing its grip takes it into that hand (it flies there, to where the game holds it
//   for the game's hand, to where the fist closes on the shaft's grip point for the other); the game's
//   hand pressing away from the back calls it back as from the floor.
// - Drawn (Settings::drawMode, [hands] shoulder_draw): a weapon let go lies where it fell - in the WORLD (Transport: the hero walking or turning does not carry it
//   along). While it is in the air or lies there the grip of EITHER hand, pressed anywhere (Settings::recallFloorAny), calls it back: it flies into that fist. A hand behind
//   a shoulder (HandIn::draw) pressing its grip DRAWS it instead: it leaves the floor in that instant and flies from the shoulder (HandIn::drawFrom) into that hand - the game's
//   hand as the game holds it, the other as a fist on the shaft. After Settings::stowAfter seconds on the floor it goes to the back (Stowed), and from then on only a hand
//   behind a shoulder (or at the weapon where it hangs, backTake) takes it - no grip anywhere else calls it from the back.
//
// Room space: OpenXR stage axes, metres, y up. The body's frame is the fist that held it first,
// where the game's own grip puts the weapon; its shaft runs through that frame along `Line`.

#include <cstdint>

#include "heft.h"

namespace heft::hold {

// The weapons by shape (D2R VR's kinds, shared/d2r_vr_state.h, are mapped onto these by the mod).
enum class Kind { Dagger, Sword, Axe, Mace, Scepter, Wand, Staff, Polearm, Spear, Javelin, Throwing, Other, Count };
const char* KindName(Kind k);

// A weapon along its shaft, in metres from where the game's fist holds it: from `butt` (behind the
// fist, negative) to `tip`, and its steel as pieces along +Y.
struct Shape {
	double butt = -0.1, tip = 0.9;
	Piece  pieces[3];
	int    nPieces = 0;
	double Mass() const;
	// The same weapon on other ends: its pieces moved along the shaft by the map that takes the old [butt, tip] onto the new one (their mass and radius kept).
	void Fit(double newButt, double newTip) {
		const double len = tip - butt, nl = newTip - newButt;
		if (!(len > 1e-6) || !(nl > 1e-3)) return;
		const double k = nl / len;
		for (int i = 0; i < nPieces; ++i) {
			pieces[i].a.y = newButt + (pieces[i].a.y - butt) * k;
			pieces[i].b.y = newButt + (pieces[i].b.y - butt) * k;
		}
		butt = newButt;
		tip  = newTip;
	}
	// How high the shaft's axis lies over the floor: the thickest of its pieces' radius (a staff's knob, a polearm's head), at least a centimetre.
	double Lie() const {
		double r = 0.01;
		for (int i = 0; i < nPieces; ++i) r = pieces[i].radius > r ? pieces[i].radius : r;
		return r;
	}
};
// twoHanded: the game's "2handed" (a two-handed sword, a great axe, a maul are longer and heavier).
Shape ShapeOf(Kind k, bool twoHanded);

// The muscles: how hard a controller pulls its hand. The dampers are worked out from what the hand
// carries (see Step), so a dagger turns crisply and a two-handed sword does not wobble.
struct Strength {
	double spring = 3000, maxForce = 350;          // N/m, N per axis
	double torqueSpring = 300, maxTorque = 70;     // N m/rad (the slerp law: k/4 at small angles), N m
	double damping = 0.9, torqueDamping = 0.8;     // of critical, on the load it carries
	double torqueSoft = 0;                         // rad: the wrist's stiffness doubles for a turn this far off the controller's line (heft::Muscle); 0 = a plain spring
	double springWithStrength = 0;                 // how far the strength setting also stiffens the springs: 0 only the ceilings (as ever), 1 the springs in step too
};
Strength OneHand();    // a hand holding the weapon alone
Strength EachOfTwo();  // each of two hands on it: the lever between them does most of the turning
// The same hands for the gravity feel (docs/WEAPON_WEIGHT.md): a wrist soft near the controller's line, so a weight can sag it, and firm
// for a turn far off it, so a hard cut is still followed; stronger ceilings, whose stiffness the strength setting scales too.
Strength OneHandSoft();
Strength EachOfTwoSoft();

struct Settings {
	Strength one = OneHand(), two = EachOfTwo();
	double weight       = 1.0;    // the steel's mass x this ("feel the weight", 0 = none)
	double strength     = 1.0;    // the muscles' ceilings x this (with the soft wrists the springs too)
	double gravity      = 1.0;    // g x this ([weight] gravity_pct; 0 = no gravity, only inertia). Held weapons only: one let go falls at the real g, or it would never land
	double damping      = 1.0;    // the muscles' dampers x this ([weight] damping_pct)
	double handMass     = 0.5;    // kg each hand on it adds (it has no weight: the arm carries itself)
	double grabDistance = 0.12;   // m between the fist and the shaft for a grip to take it
	double letGo        = 0.25;   // m the two hands are pulled apart (or together) off their places for one to let go
	double snap         = 0.80;   // m: the weapon this far from the last hand is put back in it; hands this far apart let go
	double ease         = 0.12;   // s a hand takes to go onto the weapon or off it
	double floor        = 0.0;    // the floor's height in the room, m: a weapon let go falls to it
	double stowAfter    = -1;     // s a landed weapon lies before it goes to the back; < 0 never (it lies on the floor)
	bool   slide        = false;  // the second hand slides along a long shaft while its grip stays squeezed (heft/slide.h); else its grasp stays where it closed
	double slideTau     = 0.05;   // s: the smoothing of that slide
	bool   hiltOnly     = false;  // [weight] grip_hilt_only: the SECOND hand (the other holds the weapon) takes a sword, a dagger, a scepter, an axe or a mace only on its hilt / haft - up to HiltEnd() and hiltMargin past it; any other kind, and the first hand, as ever
	double hiltLen      = 0.0;    // m from the butt: where that hilt ends; <= 0 the shape's own (the end of its first piece: the handle, the haft)
	double hiltMargin   = 0.03;   // m: a grip this far past the guard still takes it (the guard has a width, a hand a size)
	double backTake     = 0.25;   // m: a hand whose fist is this near the weapon stowed on the back (its drawn shaft) takes it with its grip - anywhere along it, not only behind a shoulder; 0 = the shoulder alone
	bool   drawMode     = false;  // [hands] shoulder_draw: a hand behind a shoulder draws the weapon (HandIn::draw); on the back only that (or a hand at the weapon) takes it, not a grip pressed elsewhere
	bool   recallFloorAny = false; // drawMode: a weapon that is away (in the air, lying) is called back by the grip of ANY free hand pressed anywhere (else: only a hand at the shaft, or behind a shoulder)
	bool   recallAny    = false;  // a weapon stowed on the back is called by the grip of ANY free hand pressed anywhere (else: the game's hand, or a hand behind a shoulder)
	double fly          = 0.15;   // s the weapon called back takes to fly into the fist
	V3     fist;                  // the closed fist's middle from the controller, in the RIGHT controller's frame (x mirrored for the left)
	int    model        = 0;      // the physics of the held body: 0 our own (below); 1 a weight-physics library's, when one is loaded (SetEngine) - else ours
	unsigned modelFlags = 0;      // the library's own switches (bit 0: the weapon's weight in the palm), handed to it as they are
	int    gripModel    = 0;      // how a hand holds the shaft: 0 our own (the grip alone holds it, the second hand slides while it is squeezed); 1 a grip library's, when one is loaded (SetGripEngine) - else ours
};
// The hands for the gravity feel ([weight] gravity_feel): the soft wrist profiles in place of the stiff ones.
inline void UseGravityFeel(Settings& s) {
	s.one = OneHandSoft();
	s.two = EachOfTwoSoft();
}

// A weight-physics library that moves the held body instead of ours (vr/weight_lib.h loads it: shared/d2r_vr_weight.h is its C interface). It is stateless: the body's state goes in and
// comes out, the rest of the hands (taking, passing, falling, the back) stays this file's. None loaded: Settings::model 1 is ours.
class Engine {
public:
	virtual ~Engine() = default;
	// One step of the held body. `st` is the state in OUR body's terms (`ours`: centre of mass and all); the library's own body has its own mass and centre, so the state goes through the
	// grip frame. pieces: the steel in the grip frame (its mass already x the weight setting); grips: the hands on it (palm, rotation in the grip frame); from / to: their controllers.
	// True when the weapon was put back in the first hand.
	virtual bool Step(State& st, const Body& ours, const Piece* pieces, int nPieces, bool twoHandedKind, const Grip* grips, const Controller* from, const Controller* to, int n,
	                  double dt, const Settings& s) const = 0;
	// A throw's launch velocity from the weapon's last steps (t in ms, oldest first). False: ours.
	virtual bool ThrowVelocity(const uint64_t* t, const V3* vel, int n, uint64_t now, V3* out) const = 0;
};
void SetEngine(const Engine* engine);   // null: none (a library that cannot be loaded, or one that is gone)
const Engine* GetEngine();

// A grip library (vr/grip_lib.h; shared/d2r_vr_grip.h is its C interface) decides which buttons hold a hand on the shaft and, for the second hand of a long shaft, whether it is tight
// or loose: a loose one slides along the shaft with a lag and turns freely about it. It also gives the pulses a hand feels. The state it keeps for a hand is a plain blob that belongs
// to the hand (zero = afresh). None loaded: Settings::gripModel 1 is ours.
struct LineIn {
	double grip = -1, trigger = -1;   // 0..1; < 0 not known
	bool   holds = false;             // the hand is on the shaft
	bool   second = false;            // ... and it is the second hand of a long shaft (the only one the modes act on)
	bool   inReach = false;           // a free hand within reach of the shaft
	double t = 0, raw = 0;            // where the hand holds the shaft (m along the line), where the controller's fist projects on it
	double rollRaw = 0;               // the controller's turn about the shaft against the grip the hand took it in (rad)
	double butt = 0, tip = 0;
};
struct LineOut {
	bool     hold = false;
	int      mode = 0;                // 0 none, 1 tight, 2 loose
	double   t = 0, roll = 0;
	uint32_t events = 0;              // 1 hover, 2 grab, 4 release
	double   hapticAmp = 0;
	int      hapticMs = 0;
};
struct LineState {
	alignas(8) unsigned char blob[128] = {};
};
class GripEngine {
public:
	virtual ~GripEngine() = default;
	// false: the library cannot take the step (the caller keeps our own rule for it)
	virtual bool Line(LineState& st, const LineIn& in, double dt, LineOut* out) const = 0;
};
void SetGripEngine(const GripEngine* engine);
const GripEngine* GetGripEngine();

struct HandIn {
	bool  tracked = false;
	Pose  pose;                    // the controller
	bool  velKnown = false;        // vel and angVel given; else taken from the pose's change since the last step
	V3    vel, angVel;             // world
	float grip   = -1.0f;          // 0..1; < 0 not known (nothing is taken or let go)
	float trigger = -1.0f;         // 0..1; < 0 not known: only a grip library's loose hold reads it
	bool  busy   = false;          // holds something else (a belt potion), or is about to take it: takes nothing
	bool  claimed = false;         // the grip is another gesture's (the weapon swap behind a shoulder): takes nothing off the floor, calls nothing back
	bool  behind = false;          // the hand is behind a shoulder (the swap gesture's volume): its grip takes a weapon stowed on the back
	bool  callable = true;         // drawMode: this hand may call the weapon back from afar (false: a javelin or a throwing weapon for the hand that is not the game's - the throw gesture reads only that controller)
	bool  draw = false;            // drawMode: the hand is behind the shoulder of this weapon's side - its grip press draws the weapon from there, wherever it lies or falls
	V3    drawFrom;                // ... from here: the shoulder point, in the room
	V3    fistLine { 0, 0, -1 };   // the line through the closed fist (little finger to index), controller frame
};

struct HandOut {
	bool     holds = false;   // its grip is on the weapon
	double   on    = 0;       // 0 drawn at its controller, 1 at its place on the weapon (easing between)
	Pose     pose;            // where the hand is drawn
	uint32_t grabs = 0;       // bumped each time this hand takes the weapon (0: the first fist, as placed)
	// A grip library's word (Settings::gripModel 1 and one loaded; else all zero): the grip's mode (0 none, 1 tight, 2 loose), the hand's turn about the shaft against the grip it took
	// it in (rad), the pulses this step (bits: 1 a free hand came within reach, 2 it closed, 4 it let go) and the strongest one's amplitude and length.
	int      mode = 0;
	double   roll = 0;
	uint32_t events = 0;
	double   hapticAmp = 0;
	int      hapticMs = 0;
};

// The shaft in the body's frame.
struct Line {
	V3 at;              // where it passes the first fist
	V3 dir { 0, 1, 0 }; // toward the tip
};

class Hold {
	enum class Free { Held, Falling, Tipping, Lying, Flying, Stowed };

public:
	// The weapon put in hand `hand`'s fist as the game holds it: a new weapon, a swap, the arms
	// back on the controllers. `line`: the shaft in that controller's frame.
	void Place(int hand, Kind kind, bool twoHanded, const Line& line, const HandIn in[2], const Settings& s);
	void Reset() { placed_ = false; }
	bool Placed() const { return placed_; }
	// The shaft measured again (Weapon Adjust moved the weapon in the hand): taken while the first
	// fist still holds it as placed and alone, ignored otherwise.
	void SetLine(const Line& line, const Settings& s);
	// The shaft as the weapon is DRAWN, measured by the mod each pose against the body's frame: the line (where it passes the body's origin, toward the tip) and
	// where the wood ends, metres along it from that point (butt <= tip). The grips, the ends and the lever follow it - the line is where the weapon IS, whatever
	// the first guess was - held back by a little hysteresis; a second hand's grasp is put back on the new line. Not while it flies back or is out of the hands.
	void SetDrawnShaft(const Line& line, double butt, double tip, const Settings& s);
	// Where a grip pressed NOW by `hand` would take the weapon, by the very rules Step uses - nothing is changed, nothing taken: the fist within grabDistance of the shaft of a weapon the other
	// hand holds or that lies / falls / tips (Shaft), or a weapon on the back with the hand behind a shoulder or the fist within backTake of its drawn shaft (Back). Nowhere: a grip there
	// takes nothing - except that a weapon that is AWAY (on the back, or lying / falling) is called back from afar by the hand the game holds it in, and on the back by any hand with
	// recall_any_hand: Recall (a grip there takes the weapon too, but the hand is not AT the weapon). *metres: the fist's distance to the shaft (Shaft, a floor weapon's Recall) or to
	// the stowed shaft (Back, Recall); 1e9 when there is none. The belt potions ask this (vr/grab_priority.h): a hand in a weapon's zone takes the weapon, not a potion.
	enum class TakeAt : uint8_t { Nowhere, Shaft, Back, Recall };
	TakeAt TakeZone(int hand, const HandIn& in, const Settings& s, double* metres) const;
	// Where along the shaft (metres, the line's measure) the hilt of a sword, a dagger, a scepter, an axe or a mace ends - the furthest a SECOND hand's grip takes it, less hiltMargin - when
	// Settings::hiltOnly is on; the tip for every other kind and with hiltOnly off.
	double HiltEnd(const Settings& s) const;
	// The distance (m) from a hand's fist to the weapon stowed on the back - the nearest point of its drawn shaft, between butt and tip. Large when it is not stowed.
	double StowDistance(int hand, const HandIn& in, const Settings& s) const;
	// One step of `dt` seconds: grips taken and let go, the body moved. `out` is where to draw the hands.
	void Step(const HandIn in[2], double dt, const Settings& s, HandOut out[2]);

	int  Main() const { return main_; }      // the hand that has held it longest (-1 not placed)
	bool Held() const { return placed_ && (holds_[0] || holds_[1]); }   // in a hand (not falling, lying or flying back)
	bool Flying() const { return placed_ && free_ == Free::Flying; }
	int  FlyHand() const { return flyHand_; }   // the hand it flies into (while Flying)
	bool Stowed() const { return placed_ && free_ == Free::Stowed; }   // on the hero's back (drawn there by the mod)
	// Where the stowed weapon's body frame is in the room now, as the mod draws it on the back: told every step (a
	// frame late is fine); the weapon flies from there when it is taken or called back. Before the first one the
	// weapon goes to the back from where it lay.
	void SetStowPose(const Pose& p) { stowPose_ = p; }
	// A THROWN weapon (the mod knows the release was a throw - vr/weight_throw.h): once it is out of the hands it flies and falls like any weapon let go,
	// but it is not for taking, calling back or stowing - it only has to end (the mod places a fresh one in the hand when it does). Armed while a
	// hand still holds it (the throw may be known a frame before the grip is seen open); a no-op while it flies back into a hand.
	void MarkThrown();
	// The weapon's centre of mass and its velocity in the room (m, m/s): what a throw's release carries. The steel's mass in kg (the hands' share left out).
	V3 Com() const { return st_.com; }
	V3 ComVel() const { return st_.vel; }
	double SteelKg() const { return body_.weight; }
	bool Falling() const { return placed_ && free_ == Free::Falling; }
	// A thrown weapon in the air (Falling) is put on a flight the mod worked out: its centre and velocity. No-op in any other state.
	void SetFlight(const V3& com, const V3& vel) {
		if (!Falling()) return;
		st_.com = com;
		st_.vel = vel;
	}
	void ClearThrown() { thrown_ = false; thrownFor_ = 0; }
	bool Thrown() const { return placed_ && thrown_ && !Held(); }
	double ThrownFor() const { return thrownFor_; }    // s since it was out of the hands (while Thrown)
	bool Landed() const { return placed_ && (free_ == Free::Lying); }    // lies on the ground
	// The world moved under the room (the hero walked, the sticks turned him): a weapon that is away (falling, tipping over, lying) or flying back from the floor stays where it
	// is in the WORLD - its room pose is carried by the same move: turned about the vertical by `yaw`, then shifted by `shift` (metres; the height is left alone: the floor is
	// the room's). No-op for a weapon in the hands (they carry it) or on the back (the mod tells where that is every pose).
	void Transport(const Q& yaw, const V3& shift);
	// Away from the hands but still ours: falling, tipping over or lying (not flying into a hand, not on the back, not a thrown one on its way to ending). What a shoulder holds
	// when drawMode is on, and what a draw takes.
	bool Away() const { return placed_ && !thrown_ && (free_ == Free::Falling || free_ == Free::Tipping || free_ == Free::Lying); }
	// A release whose verdict (a throw or a drop) is not in yet: the landed weapon is not stowed meanwhile.
	void SetStowHold(bool hold) { stowHold_ = hold; }
	int  First() const { return first_; }    // the hand it was placed in
	bool AsPlaced() const { return placed_ && holds_[first_] && grabs_[first_] == 0; }   // the first fist where the game put it
	Pose BodyPose() const;                   // the body's frame (the first fist as placed) in the room
	const Shape& WeaponShape() const { return shape_; }
	const Line&  ShaftLine() const { return line_; }
	// The shaft toward the tip in a hand's OWN (its controller's) frame, as that hand grips it: fixed by the grip, not by where the simulated weapon is -
	// what a staff's aim is taken along (vrcam), so the weapon's lag and sag cannot move where the spells go.
	V3 ShaftInHand(int hand) const { return Rotate(QConj(grips_[hand & 1].rot), line_.dir); }
	// The shaft's two ends in the room: the butt and the tip.
	void Ends(V3* butt, V3* tip) const;
	// A hand's place on the weapon in the room (where it would be drawn fully on it).
	Pose OnWeapon(int hand) const;

private:
	void Build(const Settings& s);            // the body from the shape, the line and the hands on it
	bool TryGrab(int hand, const HandIn& in, const Settings& s);
	bool PastHilt(int hand, double along, const Settings& s) const;   // a grip of `hand` at `along` the shaft is past the zone a second hand may take (hiltOnly)
	V3   FistOffset(int hand, const Settings& s) const;
	void Fall(double dt, const Settings& s);  // Falling, Tipping: the weapon on its own, to the floor
	void StartFly(int hand);                  // called back / taken from the back into `hand`
	void StartDraw(int hand, const HandIn& in, const Settings& s);   // drawn from the shoulder (in.drawFrom) into `hand`
	Grip FlyGrip(int hand, const HandIn& in, const Settings& s) const;   // how the weapon sits in the hand it flies into

	bool     placed_ = false;
	bool     twoHanded_ = false;    // the game's "2handed" kind (Place)
	Kind     kind_ = Kind::Other;   // the kind it was placed as
	Piece    pieces_[3];            // the steel in the grip frame, as Build made the body of it
	int      nPieces_ = 0;
	int      first_ = -1, main_ = -1;
	Shape    shape_;
	Line     line_;
	Body     body_;
	State    st_;
	Grip     grips_[2];
	bool     holds_[2] = {};
	bool     pressed_[2] = {};
	bool     sticky_[2] = {};       // holds without its grip: the hand it was placed in, until that grip is first let go
	Free     free_ = Free::Held;
	int      pivot_ = 0;            // Tipping: the end on the floor (0 butt, 1 tip)
	double   tipSpeed_ = 0;         // rad/s
	double   flyT_ = 0;             // Flying: 0..1
	Pose     stowPose_;             // Stowed: the body's frame in the room
	double   lyingFor_ = 0;         // s Lying so far
	bool     thrown_ = false;       // MarkThrown
	double   thrownFor_ = 0;
	bool     stowHold_ = false;     // SetStowHold
	Pose     flyFrom_;              // the body when it was called back
	int      flyHand_ = 0;
	uint32_t grabs_[2] = {};
	LineState line_state_[2];       // a grip library's state for each hand
	double   builtWeight_ = -1;     // the steel's mass multiplier the body was built with (Build): another one rebuilds it
	bool     lineQuiet_[2] = {};    // the hand a weapon was placed in: its first "closed on it" is no pulse (a swap, an area change, the arms back on the controllers)
	double   on_[2] = {};
	Controller last_[2];
	bool     haveLast_ = false;
};

}   // namespace heft::hold
