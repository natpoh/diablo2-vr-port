// Hold: see hold.h.
//
// Taking and letting go. A grip pressed with the fist near the shaft (within grabDistance, between its
// butt and its tip) takes the weapon there: the fist slides onto the shaft at its nearest point and
// turns the least it must to lie along it (its roll about the shaft stays the hand's). A hand holds
// while its grip is held and lets go the moment it is let go (the hand it was placed in excepted, until
// its grip is first let go); the other hand, if it holds, has it then. Both let go, the weapon falls
// (Fall): with the motion it had, to the floor, onto an end, over about it, flat. The game's hand
// pressed away from it calls it back: it flies into that fist in Settings::fly seconds.

#include "hold.h"
#include "slide.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace heft::hold {

namespace {

constexpr double kPi = 3.14159265358979323846;

Piece P(double a, double b, double r, double m) { return { { 0, a, 0 }, { 0, b, 0 }, r, m }; }

// The turn taking unit a onto unit b, the shortest way.
Q Between(V3 a, V3 b) {
	const double d = Dot(a, b);
	if (d < -0.999999) {   // opposite: half a turn about any axis square to a
		V3 ax = Cross({ 1, 0, 0 }, a);
		if (Len(ax) < 1e-6) ax = Cross({ 0, 0, 1 }, a);
		return QFromAxisAngle(ax, kPi);
	}
	const V3 c = Cross(a, b);
	return QNorm({ c.x, c.y, c.z, 1 + d });
}

// The largest moment of inertia of the body about a point of it (grip frame): how hard it is to
// turn about a hand there.
double MomentAbout(const Body& b, V3 p) {
	const V3 d = Sub(p, b.com);
	const double dd = Dot(d, d), v[3] = { d.x, d.y, d.z };
	M3 i = b.inertia;
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 3; ++c) i.m[r][c] += b.mass * ((r == c ? dd : 0) - v[r] * v[c]);
	V3 diag;
	Q  rot;
	Principal(i, &diag, &rot);
	return std::max({ diag.x, diag.y, diag.z, 1e-4 });
}

// The springs as they are, the dampers worked out from the load (a share of the mass and of the
// moment about the hand) so that every weapon settles alike: the slerp drive's damping is c/4
// against k/4 at small angles, hence 4 zeta sqrt(k I).
Muscle MuscleFor(const Strength& f, double mass, double moment, double strength, double damping) {
	Muscle m;
	const double pull = std::pow(std::max(strength, 1e-3), f.springWithStrength);   // 1 for the stiff profiles
	m.spring       = f.spring * pull;
	m.damper       = 2.0 * f.damping * damping * std::sqrt(m.spring * std::max(mass, 0.05));
	m.maxForce     = f.maxForce * strength;
	m.torqueSpring = f.torqueSpring * pull;
	m.torqueDamper = 4.0 * f.torqueDamping * damping * std::sqrt(m.torqueSpring * std::max(moment, 1e-4));
	m.torqueSoft   = f.torqueSoft;
	m.maxTorque    = f.maxTorque * strength;
	return m;
}

double Smooth(double u) {
	u = std::clamp(u, 0.0, 1.0);
	return u * u * (3 - 2 * u);
}

}   // namespace

namespace {
const Engine* g_engine = nullptr;   // the weight-physics library, when one is loaded (vr/weight_lib.cpp)
}   // namespace
void SetEngine(const Engine* engine) { g_engine = engine; }
const Engine* GetEngine() { return g_engine; }

const GripEngine* g_gripEngine = nullptr;   // the grip library, when one is loaded (vr/grip_lib.cpp)
void SetGripEngine(const GripEngine* engine) { g_gripEngine = engine; }
const GripEngine* GetGripEngine() { return g_gripEngine; }

const char* KindName(Kind k) {
	static const char* const names[] = { "dagger", "sword",  "axe",     "mace",     "scepter", "wand",
	                                     "staff",  "polearm", "spear", "javelin", "throwing", "other" };
	const int i = (int)k;
	return i >= 0 && i < (int)Kind::Count ? names[i] : "?";
}

double BodyFriction(Kind k) {
	switch (k) {
	case Kind::Mace: case Kind::Scepter: case Kind::Staff: case Kind::Wand: case Kind::Other: return 1.5;   // (cleanroom/tests/monster_bodies_test: a shallow cut - a sword slides 58 cm along the body, a mace 4)
	default: return 0.25;   // an edge or a point: a dagger, a sword, an axe, a polearm, a spear, a javelin, a throwing weapon
	}
}

void Hold::SetBodies(const contact::Capsule* bodies, int n) {
	nBodies_ = 0;
	for (int i = 0; bodies && i < n && nBodies_ < kBodies; ++i) bodies_[nBodies_++] = bodies[i];
}

double Shape::Mass() const {
	double m = 0;
	for (int i = 0; i < nPieces; ++i) m += pieces[i].mass;
	return m;
}

// Lengths from where the game's fist holds the weapon, masses as the real ones weigh (a dagger
// 0.4 kg, a sword 1.2, a two-handed sword 2.5, a spear 1, a polearm and a maul 3.5): the shaft or
// grip and the blade or head apart, so an axe's, a mace's and a polearm's weight sits at its end.
Shape ShapeOf(Kind k, bool twoHanded) {
	Shape s;
	auto set = [&](double butt, double tip, std::initializer_list<Piece> ps) {
		s.butt    = butt;
		s.tip     = tip;
		s.nPieces = 0;
		for (const Piece& p : ps)
			if (s.nPieces < 3) s.pieces[s.nPieces++] = p;
	};
	switch (k) {
	case Kind::Dagger: set(-0.09, 0.30, { P(-0.09, 0.03, 0.015, 0.12), P(0.03, 0.30, 0.012, 0.25) }); break;
	case Kind::Throwing: set(-0.08, 0.22, { P(-0.08, 0.02, 0.012, 0.08), P(0.02, 0.22, 0.012, 0.20) }); break;
	case Kind::Wand: set(-0.06, 0.32, { P(-0.06, 0.32, 0.010, 0.25) }); break;
	case Kind::Scepter: set(-0.10, 0.56, { P(-0.10, 0.44, 0.016, 0.35), P(0.44, 0.56, 0.040, 0.55) }); break;
	case Kind::Axe:
		if (twoHanded) set(-0.40, 0.88, { P(-0.40, 0.70, 0.020, 1.10), P(0.68, 0.88, 0.080, 2.00) });
		else set(-0.10, 0.58, { P(-0.10, 0.45, 0.018, 0.45), P(0.42, 0.58, 0.060, 0.90) });
		break;
	case Kind::Mace:
		if (twoHanded) set(-0.40, 0.86, { P(-0.40, 0.68, 0.022, 0.90), P(0.66, 0.86, 0.080, 2.60) });
		else set(-0.10, 0.58, { P(-0.10, 0.44, 0.018, 0.45), P(0.42, 0.58, 0.050, 1.20) });
		break;
	case Kind::Staff: set(-0.80, 0.95, { P(-0.80, 0.82, 0.020, 1.60), P(0.80, 0.95, 0.040, 0.30) }); break;
	case Kind::Polearm: set(-0.50, 1.60, { P(-0.50, 1.40, 0.022, 1.90), P(1.25, 1.60, 0.060, 1.60) }); break;
	case Kind::Spear: set(-0.60, 1.32, { P(-0.60, 1.15, 0.020, 0.82), P(1.10, 1.32, 0.030, 0.18) }); break;
	case Kind::Javelin: set(-0.45, 0.80, { P(-0.45, 0.66, 0.015, 0.50), P(0.64, 0.80, 0.020, 0.12) }); break;
	case Kind::Sword:
	case Kind::Other:
	default:
		if (twoHanded) set(-0.30, 1.20, { P(-0.30, 0.06, 0.018, 0.50), P(0.06, 1.20, 0.025, 2.00) });
		else set(-0.14, 0.90, { P(-0.14, 0.06, 0.016, 0.30), P(0.06, 0.90, 0.020, 0.90) });
		break;
	}
	return s;
}

// A hand alone: a sword follows a hard cut (140 degrees in 0.3 s) some 20 cm behind at its tip, a
// two-handed sword or a polearm cannot - it swings round like a flail, it wants the other hand
// (heft_test hands: the table). Softer wrists showed the sag better, but then no one-handed weapon
// could be swung: a metre behind (2026-10-13).
Strength OneHand() { return { 3000, 400, 1500, 120, 0.9, 0.8 }; }
// Each of two: weaker wrists - two hands apart turn the weapon by the lever between them.
Strength EachOfTwo() { return { 3000, 400, 750, 60, 0.9, 0.8 }; }
// The gravity feel. One hand: the wrist 200 (k/4 = 50 N m/rad: a sword's 4 N m of weight sags it 4 degrees, a polearm's 31 N m about 11), firmer
// the further it is turned off the controller's line (doubled at 8 degrees), so a hard cut is followed nearly as before; ceilings high enough that a
// sword never meets them and a two-hander does. The strength setting stiffens the springs too. Each of two: the same hands as before - the lever
// between them holds a weight best.
Strength OneHandSoft() { return { 3000, 400, 200, 200, 0.9, 0.9, 0.14, 1.0 }; }
Strength EachOfTwoSoft() { return { 3000, 400, 750, 60, 0.9, 0.8, 0.0, 1.0 }; }

V3 Hold::FistOffset(int hand, const Settings& s) const { return hand == 1 ? V3 { -s.fist.x, s.fist.y, s.fist.z } : s.fist; }

Pose Hold::BodyPose() const { return GripPose(st_, body_); }
Pose Hold::OnWeapon(int hand) const { return HandPose(st_, body_, grips_[hand & 1]); }
int Hold::RoomPieces(contact::Capsule* out, int cap) const {
	if (!placed_ || !out) return 0;
	int n = 0;
	for (int i = 0; i < nPieces_ && n < cap; ++i) {
		contact::Capsule& c = out[n++];
		c = contact::Capsule{};
		c.a = PointAt(st_, body_, pieces_[i].a);
		c.b = PointAt(st_, body_, pieces_[i].b);
		c.radius = std::max(0.01, HeadKnown(i) ? std::min(pieces_[i].radius, pieces_[0].radius) : pieces_[i].radius);   // (the head's own volume stands for the kind's head)
	}
	for (int i = 0; i < nHead_ && n < cap; ++i) {   // the head's own volume
		contact::Capsule& c = out[n++];
		c = contact::Capsule{};
		c.a = PointAt(st_, body_, head_[i].butt);
		c.b = PointAt(st_, body_, head_[i].tip);
		c.radius = head_[i].radius;
	}
	return n;
}

void Hold::SetHeads(const V3* a, const V3* b, const double* radius, int n, uint32_t only) {
	nHead_ = 0;
	if (!placed_ || !a || !b || !radius) return;
	for (int i = 0; i < n && i < kHeadParts; ++i) {
		const V3 p = a[i], q = b[i];
		const double r = radius[i];
		if (!std::isfinite(p.x + p.y + p.z + q.x + q.y + q.z + r) || !(r > 0.0) || Len(p) > 3.0 || Len(q) > 3.0) {
			nHead_ = 0;   // (one part not a measurement: none of it)
			return;
		}
		head_[nHead_++] = { p, q, std::max(0.005, r), only };
	}
	headOnly_ = only;
}

void Hold::Ends(V3* butt, V3* tip) const {
	if (butt) *butt = PointAt(st_, body_, Add(line_.at, Mul(line_.dir, shape_.butt)));
	if (tip) *tip = PointAt(st_, body_, Add(line_.at, Mul(line_.dir, shape_.tip)));
}

void Hold::Build(const Settings& s) {
	Piece pieces[3];
	int   n = 0;
	const Q q = Between({ 0, 1, 0 }, line_.dir);
	for (int i = 0; i < shape_.nPieces && n < 3; ++i) {
		const Piece& p = shape_.pieces[i];
		pieces[n++]    = { Add(line_.at, Rotate(q, p.a)), Add(line_.at, Rotate(q, p.b)), p.radius, p.mass * std::max(0.0, s.weight) };
	}
	for (int i = 0; i < n; ++i) pieces_[i] = pieces[i];   // (a library that moves the body wants the steel as it is)
	nPieces_ = n;
	V3  hands[2];
	int nh = 0;
	for (int h = 0; h < 2; ++h)
		if (holds_[h]) hands[nh++] = grips_[h].at;
	Body b = heft::Build(pieces, n, hands, nh, std::max(0.05, s.handMass));
	b.maxAngularSpeed = 25;
	if (st_.placed) {   // the same motion, about the new centre of mass
		const V3 com = Add(st_.com, Rotate(st_.rot, Sub(b.com, body_.com)));
		st_.vel      = Add(st_.vel, Cross(st_.angVel, Sub(com, st_.com)));
		st_.com      = com;
	}
	body_ = b;
	builtWeight_ = s.weight;
}

void Hold::Place(int hand, Kind kind, bool twoHanded, const Line& line, const HandIn in[2], const Settings& s) {
	hand   = hand & 1;
	twoHanded_ = twoHanded;
	kind_  = kind;
	stuck_    = false;   // (a new weapon is in no body)
	stuckId_  = 0;
	nCleft_   = 0;       // ([weight] cleave: a new weapon has cut through nothing)
	nMetWas_  = 0;
	strokeCleaves_ = 0;
	strokeLeft_ = 0;
	tearFor_  = 0;
	stabBlob_ = {};
	shape_ = ShapeOf(kind, twoHanded);
	nHead_ = 0;   // (a new weapon: its head, if it has one, is told again)
	line_  = line;
	const double l = Len(line_.dir);
	line_.dir = l > 1e-6 ? Mul(line_.dir, 1.0 / l) : V3 { 0, 1, 0 };
	first_ = main_ = hand;
	for (int h = 0; h < 2; ++h) {
		holds_[h]   = h == hand;
		sticky_[h]  = h == hand;
		grabs_[h]   = 0;
		on_[h]      = h == hand ? 1 : 0;
		grips_[h]   = Grip {};
		pressed_[h] = in[h].grip > 0.6f;   // a grip already held is not a new press
		line_state_[h] = LineState {};
		lineQuiet_[h]  = h == hand;
	}
	st_       = State {};
	haveLast_ = false;
	free_     = Free::Held;
	lyingFor_ = 0;
	thrown_   = false;
	thrownFor_ = 0;
	stowHold_ = false;
	Build(s);
	heft::Place(st_, body_, grips_[hand], in[hand].pose);
	placed_ = true;
}

void Hold::Transport(const Q& yaw, const V3& shift) {
	if (!placed_) return;
	if (Away()) {
		st_.com    = Add(Rotate(yaw, st_.com), shift);
		st_.rot    = QNorm(QMul(yaw, st_.rot));
		st_.vel    = Rotate(yaw, st_.vel);
		st_.angVel = Rotate(yaw, st_.angVel);
	} else if (free_ == Free::Flying) {
		flyFrom_ = { Add(Rotate(yaw, flyFrom_.pos), shift), QNorm(QMul(yaw, flyFrom_.rot)) };
	}
}

void Hold::MarkThrown() {
	if (!placed_ || free_ == Free::Flying || free_ == Free::Stowed) return;
	thrown_    = true;
	thrownFor_ = 0;
}

void Hold::SetLine(const Line& line, const Settings& s) {
	if (!AsPlaced() || holds_[first_ ^ 1]) return;
	Line l = line;
	const double n = Len(l.dir);
	if (n < 1e-6) return;
	l.dir = Mul(l.dir, 1.0 / n);
	if (Len(Sub(l.at, line_.at)) < 1e-4 && Dot(l.dir, line_.dir) > 0.999999) return;
	line_ = l;
	Build(s);
}

void Hold::SetDrawnShaft(const Line& line, double butt, double tip, const Settings& s) {
	if (!placed_ || free_ == Free::Flying) return;
	Line l = line;
	const double n = Len(l.dir);
	if (!(n > 1e-6) || !std::isfinite(n + l.at.x + l.at.y + l.at.z + butt + tip) || !(tip - butt > 1e-3)) return;
	l.dir = Mul(l.dir, 1.0 / n);
	// hardly any hysteresis (0.006 degrees, 0.2 mm): the model's line is the drawn bone's axis to well under a millimetre - a looser one (it was 0.2 degrees, 3 mm) let the
	// red line and the grasp points stand off the drawn weapon by that, and by more along a long shaft
	if (std::acos(std::clamp(Dot(l.dir, line_.dir), -1.0, 1.0)) < 0.0001 && Len(Sub(l.at, line_.at)) < 0.0002 && std::fabs(butt - shape_.butt) < 0.0002 &&
	    std::fabs(tip - shape_.tip) < 0.0002)
		return;
	line_ = l;
	shape_.Fit(butt, tip);
	for (int h = 0; h < 2; ++h) {   // a hand that closed on the shaft where it was: its grasp put back on the shaft where it is
		if (!holds_[h] || (h == first_ && grabs_[h] == 0)) continue;
		const V3 off = FistOffset(h, s);
		const V3 on  = Add(grips_[h].at, Rotate(grips_[h].rot, off));
		const double t = std::clamp(Dot(Sub(on, line_.at), line_.dir), shape_.butt, shape_.tip);
		grips_[h].at   = Sub(Add(line_.at, Mul(line_.dir, t)), Rotate(grips_[h].rot, off));
	}
	Build(s);
}

double Hold::StowDistance(int hand, const HandIn& in, const Settings& s) const {
	if (!Stowed()) return 1e9;
	const V3 fist = Add(in.pose.pos, Rotate(in.pose.rot, FistOffset(hand, s)));
	V3 b, t;
	Ends(&b, &t);
	const V3 d = Sub(t, b);
	const double l2 = Dot(d, d);
	const double u = l2 > 1e-12 ? std::clamp(Dot(Sub(fist, b), d) / l2, 0.0, 1.0) : 0.0;
	return Len(Sub(fist, Add(b, Mul(d, u))));
}

// Called back (the game's hand pressed away from the weapon) or taken from the back: it flies into that hand.
void Hold::StartFly(int hand) {
	free_     = Free::Flying;
	flyT_     = 0;
	flyFrom_  = BodyPose();
	flyHand_  = hand & 1;
	lyingFor_ = 0;
}

// Drawn from a shoulder (drawMode): it flies into `hand` like a weapon called back, but from the shoulder point and already turned as it will sit in the
// hand - it slides out of the shoulder, it does not tumble from where it lay. The floor holds no copy from this instant.
void Hold::StartDraw(int hand, const HandIn& in, const Settings& s) {
	StartFly(hand);
	const Grip home    = FlyGrip(hand, in, s);
	const Q    goalRot = QNorm(QMul(in.pose.rot, QConj(home.rot)));
	flyFrom_           = { Sub(in.drawFrom, Rotate(goalRot, home.at)), goalRot };
}

// How the weapon sits in the hand it flies into. The game's hand: where the game holds it (the body's frame IS that fist). The other
// hand: the fist on the shaft at the game's grip point, turned the least that lays the fist along the shaft - as a grip that closed there.
Grip Hold::FlyGrip(int hand, const HandIn& in, const Settings& s) const {
	Grip g;
	if (hand == first_) return g;
	const V3 off = FistOffset(hand, s);
	V3 f = in.fistLine;
	const double fl = Len(f);
	f = fl > 1e-6 ? Mul(f, 1.0 / fl) : V3 { 0, 0, -1 };
	g.rot = Between(f, line_.dir);
	g.at  = Sub(line_.at, Rotate(g.rot, off));
	return g;
}

double Hold::HiltEnd(const Settings& s) const {
	if (!s.hiltOnly) return shape_.tip;
	switch (kind_) {
	case Kind::Dagger: case Kind::Sword: case Kind::Axe: case Kind::Mace: case Kind::Scepter: break;
	default: return shape_.tip;   // a staff, a spear, a polearm ...: no blade to keep off, the whole shaft
	}
	double end = shape_.butt + s.hiltLen;
	if (!(s.hiltLen > 0.0)) {   // the shape's own: the end of its first piece
		if (shape_.nPieces < 2) return shape_.tip;
		end = std::max(shape_.pieces[0].a.y, shape_.pieces[0].b.y);
	}
	return std::clamp(end, shape_.butt, shape_.tip);
}

bool Hold::PastHilt(int hand, double along, const Settings& s) const {
	return s.hiltOnly && free_ == Free::Held && holds_[hand ^ 1] && along > HiltEnd(s) + std::max(0.0, s.hiltMargin);
}

Hold::TakeAt Hold::TakeZone(int hand, const HandIn& in, const Settings& s, double* metres) const {
	if (metres) *metres = 1e9;
	if (!placed_ || hand < 0 || hand > 1 || !in.tracked || holds_[hand]) return TakeAt::Nowhere;
	if (thrown_ && !holds_[0] && !holds_[1]) return TakeAt::Nowhere;   // a thrown weapon in the air is nobody's to take
	if (free_ == Free::Flying) return TakeAt::Nowhere;
	if (free_ == Free::Stowed) {
		const double d = StowDistance(hand, in, s);
		if (metres) *metres = d;
		if ((!s.drawMode && in.behind) || (s.drawMode && in.draw && !in.busy) || (s.backTake > 0.0 && d <= s.backTake)) return TakeAt::Back;
		if (s.drawMode) return TakeAt::Nowhere;   // from the shoulder (or at the weapon on the back) from then on: no grip anywhere else calls it
		return (hand == first_ || s.recallAny) && !in.claimed ? TakeAt::Recall : TakeAt::Nowhere;
	}
	const Pose gp  = BodyPose();
	const V3   off = FistOffset(hand, s);
	const V3 fist = Rotate(QConj(gp.rot), Sub(Add(in.pose.pos, Rotate(in.pose.rot, off)), gp.pos));
	const double raw = Dot(Sub(fist, line_.at), line_.dir);
	const double t = std::clamp(raw, shape_.butt, shape_.tip);
	const double d = Len(Sub(fist, Add(line_.at, Mul(line_.dir, t))));
	if (metres) *metres = d;
	if (d <= s.grabDistance && !PastHilt(hand, raw, s)) return TakeAt::Shaft;   // (past the hilt of a sword the second hand takes nothing: that grip is free for something else)
	if (s.drawMode) {   // the shoulder draws it; else (recallFloorAny) the grip of any free hand pressed anywhere calls it back from where it lies
		if (in.draw && !in.busy && Away()) return TakeAt::Back;
		return s.recallFloorAny && Away() && !in.busy && !in.claimed && in.callable ? TakeAt::Recall : TakeAt::Nowhere;
	}
	return free_ != Free::Held && hand == first_ && !in.claimed ? TakeAt::Recall : TakeAt::Nowhere;   // a weapon off the hands is called back into the game's hand from afar
}

bool Hold::TryGrab(int hand, const HandIn& in, const Settings& s) {
	const Pose gp  = BodyPose();
	const V3   off = FistOffset(hand, s);
	// the fist's middle in the body's frame, and the nearest point of the shaft to it
	const V3 fist = Rotate(QConj(gp.rot), Sub(Add(in.pose.pos, Rotate(in.pose.rot, off)), gp.pos));
	const double raw = Dot(Sub(fist, line_.at), line_.dir);
	const double t = std::clamp(raw, shape_.butt, shape_.tip);
	const V3 on = Add(line_.at, Mul(line_.dir, t));
	if (Len(Sub(fist, on)) > s.grabDistance || PastHilt(hand, raw, s)) return false;
	// the hand in the body's frame, turned the least that lays the line through its fist along the shaft
	Q qh = QMul(QConj(gp.rot), in.pose.rot);
	V3 f = Rotate(qh, in.fistLine);
	const double fl = Len(f);
	if (fl > 1e-6) {
		f = Mul(f, 1.0 / fl);
		qh = QNorm(QMul(Between(f, Dot(f, line_.dir) < 0 ? Mul(line_.dir, -1) : line_.dir), qh));
	}
	grips_[hand].rot = qh;
	grips_[hand].at  = Sub(on, Rotate(qh, off));
	holds_[hand]     = true;
	++grabs_[hand];
	Build(s);
	return true;
}

// The weapon on its own. Falling: gravity and the motion it was let go with, until an end reaches the
// floor. Tipping: over about that end, as a stick falls - angular acceleration m g d cos(angle) / I about
// the end - until it lies flat. No bounce, no slide: it stops where it lands.
void Hold::Fall(double dt, const Settings& s) {
	if (dt <= 0) return;
	const double kLie = shape_.Lie();   // the shaft's height over the floor, lying: its own radius (the thickest piece), not a fixed 3 cm
	V3 ends[2];
	if (free_ == Free::Falling) {
		World w;
		heft::Step(st_, body_, nullptr, nullptr, nullptr, 0, std::min(dt, 0.1), w);   // gravity only
		Ends(&ends[0], &ends[1]);
		const int low = ends[0].y <= ends[1].y ? 0 : 1;
		if (ends[low].y > s.floor + kLie) return;
		st_.com.y += s.floor + kLie - ends[low].y;   // on the floor, not in it
		st_.vel = st_.angVel = {};
		Ends(&ends[0], &ends[1]);
		const V3 u = Sub(ends[low ^ 1], ends[low]);
		const double l = Len(u);
		if (l < 1e-6 || std::asin(std::clamp(u.y / l, -1.0, 1.0)) < 0.05) {   // came down flat
			free_ = Free::Lying;
			return;
		}
		free_     = Free::Tipping;
		pivot_    = low;
		tipSpeed_ = 0.3;   // a nudge: balanced on its end it would stand
		return;
	}
	if (free_ != Free::Tipping) return;
	Ends(&ends[0], &ends[1]);
	const V3 P = ends[pivot_];
	V3 u = Sub(ends[pivot_ ^ 1], P);
	const double l = Len(u);
	if (l < 1e-6) {
		free_ = Free::Lying;
		return;
	}
	u = Mul(u, 1.0 / l);
	const double angle = std::asin(std::clamp(u.y, -1.0, 1.0));   // the free end above the floor
	if (angle <= 0) {
		free_ = Free::Lying;
		return;
	}
	const V3 pivotLocal = Add(line_.at, Mul(line_.dir, pivot_ == 0 ? shape_.butt : shape_.tip));
	const double d = std::fabs(Dot(Sub(body_.com, pivotLocal), line_.dir));
	const double moment = MomentAbout(body_, pivotLocal);
	tipSpeed_ += std::max(body_.weight, 0.05) * 9.81 * d * std::cos(angle) / std::max(moment, 1e-4) * dt;
	const double turn = std::min(angle, tipSpeed_ * dt);
	V3 axis = Cross({ 0, 1, 0 }, u);   // turning about it brings the free end down
	if (Len(axis) < 1e-6) axis = { 1, 0, 0 };
	const Q R = QFromAxisAngle(axis, turn);
	st_.com = Add(P, Rotate(R, Sub(st_.com, P)));
	st_.rot = QNorm(QMul(R, st_.rot));
	st_.vel = st_.angVel = {};
	if (turn >= angle) free_ = Free::Lying;
}

// [weight] stab_in: the strike of the POINT - a body the weapon's point is in (the deepest), where the stab line (base -> tip) crosses into it, and its outward normal there.
// A real thrust moves the point some centimetres a step, so it is found already in: the contact's own closest points then lie back on the shaft (past the body's axis)
// and its normal turns across - "not the point", "not straight in" for a straight thrust (the headset, 2026-10-22). False: the point is in no body.
bool Hold::CanCleave(const Settings& s) const {
	if (!s.cleave || !s.bodies || !(s.cleaveImpulse > 0) || stabbing_ || stuck_ || !Held()) return false;
	// only a blade cuts through: a staff, a maul, a mace or a club stops on the body, however heavy or two-handed (the author, 2026-10-25: "a blunt weapon must
	// not go through - staves, mauls")
	return kind_ == Kind::Dagger || kind_ == Kind::Sword || kind_ == Kind::Axe || kind_ == Kind::Polearm;
}

bool Hold::Cleft_(uint32_t id) const {
	for (int i = 0; i < nCleft_; ++i)
		if (cleft_[i].id == id) return true;
	return false;
}

void Hold::ForgetCleft(double dt) {
	int k = 0;
	for (int i = 0; i < nCleft_; ++i) {
		bool on = false;   // a piece of the steel still within that monster's body (its radius and the piece's, and 2 cm)
		for (int b = 0; b < nBodies_ && !on; ++b) {
			const contact::Capsule& c = bodies_[b];
			if (c.id != cleft_[i].id || c.held) continue;
			for (int p = 0; p < nPieces_ && !on; ++p) {
				const V3 a = PointAt(st_, body_, pieces_[p].a), e = PointAt(st_, body_, pieces_[p].b);
				double u = 0, v = 0;
				contact::ClosestOnSegments(a, e, c.a, c.b, &u, &v);
				const V3 pa = Add(a, Mul(Sub(e, a), u)), pc = Add(c.a, Mul(Sub(c.b, c.a), v));
				on = Len(Sub(pa, pc)) <= c.radius + std::max(0.01, pieces_[p].radius) + 0.02;
			}
		}
		cleft_[i].off = on ? 0.0 : cleft_[i].off + std::max(0.0, dt);
		if (cleft_[i].off < kCleaveForgetS) cleft_[k++] = cleft_[i];
	}
	nCleft_ = k;
	if (!nCleft_) { strokeCleaves_ = 0; strokeLeft_ = 0; }
}

static bool PointStrike(V3 base, V3 tip, const contact::Capsule* caps, int n, uint64_t skip, int* body, V3* point, V3* normal) {
	double best = 0;
	*body = -1;
	for (int i = 0; i < n; ++i) {
		if (i < 64 && (skip >> i & 1u)) continue;
		const contact::Capsule& c = caps[i];
		const auto axisDist = [&](V3 p, V3* on) {
			double s, t;
			contact::ClosestOnSegments(p, p, c.a, c.b, &s, &t);
			*on = Add(c.a, Mul(Sub(c.b, c.a), t));
			return Len(Sub(p, *on));
		};
		V3 on;
		const double in = c.radius - axisDist(tip, &on);
		if (in <= best) continue;
		const V3 u = Sub(tip, base);
		const double len = Len(u);
		if (len < 1e-6) continue;
		const V3 dir = Mul(u, 1.0 / len);
		double lo = 0, hi = len;   // back from the tip along the line: lo inside, hi outside (or the base)
		for (int k = 0; k < 40; ++k) {
			const double mid = 0.5 * (lo + hi);
			if (axisDist(Sub(tip, Mul(dir, mid)), &on) < c.radius) lo = mid;
			else hi = mid;
		}
		const V3 entry = Sub(tip, Mul(dir, hi));
		V3 onEntry;
		axisDist(entry, &onEntry);
		const V3 out = Sub(entry, onEntry);
		const double lo2 = Len(out);
		if (lo2 < 1e-9) continue;
		best = in;
		*body = i;
		*point = entry;
		*normal = Mul(out, 1.0 / lo2);
	}
	return *body >= 0 && !caps[*body].held;   // ([weight] monster_shields: the point deepest in a shield or a weapon the monster holds goes into nothing)
}

// [weight] stab_in: one step of the library's stab (stab.h Engine); its report is the step's, and while it says the weapon is in a wound, that monster is the wound's.
void Hold::StabStep(const stab::Engine& e, V3 tipL, V3 baseL, const contact::Capsule* bodies, int n, const stab::Strike* strike, const V3* gaps, int nGaps, double dt,
                    const Settings& s) {
	stab::Report rep;
	const int r = e.Step(st_, body_, tipL, baseL, bodies, n, strike, gaps, nGaps, std::max(0.0, std::min(dt, 0.1)), s.stabParams, &stabBlob_, &rep);
	if (r < 0) {   // refused: nothing changed
		if (stuck_) Unstick("the stab library refused a step");
		return;
	}
	stabRep_ = rep;
	stuck_   = r == 1;
	if (!stuck_) tearFor_ = 0;
	stuckId_ = stuck_ ? rep.id : 0;
	if (stuck_) lastIn_ = rep;
	else stabBlob_ = {};
}

// [weight] stab_in: out of the wound for a reason of the mod's (the weapon left the hands, the tick went off...).
void Hold::Unstick(const char* why) {
	stabRep_         = {};
	stabRep_.left    = true;
	stabRep_.id      = stuckId_;
	stabRep_.depth   = lastIn_.depth;
	stabRep_.deepest = lastIn_.deepest;
	stabRep_.entry   = lastIn_.entry;
	stabRep_.axis    = lastIn_.axis;
	stabRep_.limit   = lastIn_.limit;
	std::snprintf(stabRep_.why, sizeof stabRep_.why, "%s", why);
	stuck_    = false;
	stuckId_  = 0;
	tearFor_  = 0;
	stabBlob_ = {};
}

void Hold::Step(const HandIn in[2], double dt, const Settings& s, HandOut out[2]) {
	for (int h = 0; h < 2; ++h) out[h] = { false, 0, in[h].pose, grabs_[h] };
	wedged_ = contact_.nMet >= 2;   // (the step before was caught between several monsters: Settings::wedgeFriction)
	contact_ = {};   // (the held step below fills it when the weapon meets a body)
	stabRep_ = {};   // (... and the stab its report)
	if (!placed_) return;
	// The steel's mass multiplier moved (the Weight slider, the hero's strength): the body is built again with it, about the same centre of mass and the same motion.
	if (free_ == Free::Held && std::fabs(s.weight - builtWeight_) > 1e-6) Build(s);

	// The controllers' velocities, where not given: their change since the last step (the muscles damp
	// toward the controller's motion - toward rest, a swing was dragged a metre behind).
	Controller now[2];
	for (int h = 0; h < 2; ++h) {
		now[h] = { in[h].pose, in[h].vel, in[h].angVel };
		if (!in[h].velKnown) {
			now[h].vel = now[h].angVel = {};
			if (haveLast_ && dt > 1e-4) {
				now[h].vel    = Mul(Sub(in[h].pose.pos, last_[h].pose.pos), 1.0 / dt);
				now[h].angVel = Mul(RotVec(QMul(in[h].pose.rot, QConj(last_[h].pose.rot))), 1.0 / dt);
			}
		}
	}

	// A grip library (Settings::gripModel 1), asked first, from the state it left on the last step: whether each hand keeps its hold (its grip - and, for the second hand of a long
	// shaft, its trigger too), and how that second hand sits on the shaft: tight (where it is) or loose (it slides along with a lag and turns about it). A hand the library cannot
	// take a step for is held by ours.
	const GripEngine* ge = s.gripModel == 1 ? g_gripEngine : nullptr;
	LineOut lo[2];
	bool    lineOk[2] = {}, lineSecond[2] = {};
	if (ge) {
		const bool longShaft = shape_.tip - shape_.butt >= slide::kMinShaft;
		const Pose gp        = BodyPose();
		for (int h = 0; h < 2; ++h) {
			if (!in[h].tracked) {
				line_state_[h] = LineState {};
				continue;
			}
			lineSecond[h] = s.slide && free_ == Free::Held && holds_[0] && holds_[1] && h == (main_ ^ 1) && longShaft && on_[h] >= 1.0 && !sticky_[h];
			const V3 off  = FistOffset(h, s);
			const V3 fist = Rotate(QConj(gp.rot), Sub(Add(in[h].pose.pos, Rotate(in[h].pose.rot, off)), gp.pos));   // the fist in the body's frame
			LineIn li;
			li.grip    = in[h].grip;
			li.trigger = in[h].trigger;
			li.holds   = holds_[h] && free_ == Free::Held;
			li.second  = lineSecond[h];
			li.raw     = Dot(Sub(fist, line_.at), line_.dir);
			li.butt    = shape_.butt;
			li.tip     = shape_.tip;
			if (holds_[h]) {
				const V3 on = Add(grips_[h].at, Rotate(grips_[h].rot, off));
				li.t        = Dot(Sub(on, line_.at), line_.dir);
				// the controller's turn about the shaft against the grip the hand took it in
				Q dq = QNorm(QMul(QMul(QConj(gp.rot), in[h].pose.rot), QConj(grips_[h].rot)));
				if (dq.w < 0) dq = { -dq.x, -dq.y, -dq.z, -dq.w };
				li.rollRaw = 2.0 * std::atan2(Dot(V3 { dq.x, dq.y, dq.z }, line_.dir), dq.w);
			} else {
				const double tt = std::clamp(li.raw, shape_.butt, shape_.tip);
				li.inReach      = !in[h].busy && (free_ == Free::Held || free_ == Free::Lying) && Len(Sub(fist, Add(line_.at, Mul(line_.dir, tt)))) <= s.grabDistance;
			}
			lineOk[h] = ge->Line(line_state_[h], li, dt, &lo[h]);
			if (!lineOk[h]) lineSecond[h] = false;
			if (lineOk[h] && lineQuiet_[h]) {   // the hand it was placed in: no pulse for the first closing
				lo[h].events &= ~2u;
				if (!lo[h].events) { lo[h].hapticAmp = 0; lo[h].hapticMs = 0; }
				lineQuiet_[h] = false;
			}
		}
	}

	// The grips (pressed past 0.6, let go under 0.4). A grip let go: that hand lets go - at once. A grip
	// pressed: a free hand near the shaft takes it there; the game's hand away from it calls it back.
	bool changed = false;
	for (int h = 0; h < 2; ++h) {
		const HandIn& c   = in[h];
		const bool    was = pressed_[h];
		bool          down = c.grip < 0 ? was : c.grip > 0.6f ? true : c.grip < 0.4f ? false : was;
		if (lineSecond[h]) down = lo[h].hold;   // the grip library: held by the grip or the trigger
		pressed_[h]        = down;
		if (!down && was) {
			sticky_[h] = false;
			if (free_ == Free::Flying && flyHand_ == h) free_ = Free::Falling;   // let go while it flew back: it falls
		}
		if (thrown_ && !holds_[0] && !holds_[1]) continue;   // a thrown weapon in the air is nobody's to take or call back
		if (free_ == Free::Stowed) {
			// On the back: a hand behind a shoulder takes it into that hand (its grip may be the swap's claim: this press is not a swap);
			// the game's hand pressed anywhere else calls it back, as from the floor.
			if (!c.tracked || !down || was || holds_[h] || c.busy) continue;
			// at the weapon where it hangs (anywhere along it, a generous radius) or behind a shoulder: that hand takes it
			if ((!s.drawMode && c.behind) || (s.drawMode && c.draw) || (s.backTake > 0.0 && StowDistance(h, c, s) <= s.backTake)) StartFly(h);
			else if (!s.drawMode && (h == first_ || s.recallAny) && !c.claimed) StartFly(h);   // (drawMode: from the shoulder from then on)
			continue;
		}
		// drawMode: a hand behind the shoulder of this weapon's side draws it - whether it falls, tips over or lies; the grip may be the zone's claim (that press is a draw)
		if (s.drawMode && c.draw && c.tracked && down && !was && !holds_[h] && !c.busy && Away()) {
			StartDraw(h, c, s);
			continue;
		}
		if (!c.tracked || !down || was || holds_[h] || c.busy || c.claimed || free_ == Free::Flying) continue;
		if (TryGrab(h, c, s)) {
			changed = true;
			if (free_ != Free::Held) {   // taken up off the floor (or out of the air)
				free_   = Free::Held;
				main_   = h;
				st_.vel = st_.angVel = {};
			}
		} else if (free_ != Free::Held && ((h == first_ && !s.drawMode) || (s.drawMode && s.recallFloorAny && c.callable))) {
			StartFly(h);   // called back into the game's hand (drawMode with recallFloorAny: into the hand whose grip it was, either)
		}
	}
	// The second hand slides along a long shaft the first also holds, while its grip stays squeezed (heft/slide.h): its grasp point follows the
	// controller's fist along the axis; the first hand's stays. Off an end of the shaft, or pulled away from its axis, it lets go. The weapon's turn
	// then follows the line between the hands, and the lever the muscles work with (the grips' places) is the current one.
	bool sliding = false;   // the second hand is sliding: the pull-apart rule below leaves the first hand alone (the slide has its own ends)
	// [weight] monster_bodies: a weapon a body holds back (on it now or kBodySnapHoldS ago, or in a wound) is not let go for being pulled off its axis or off an end - the
	// body stopped the steel while the arms went on, the hand did not leave it (a player, 0.179: slashing left to right "the other hand even lets go of the weapon
	// entirely"; cleave_test: a two-handed sword at 7 m/s, the second controller 23 cm off the axis). An open grip still lets go.
	const bool bodyHolds = s.bodies && (stuck_ || sinceBody_ < kBodySnapHoldS);
	if (s.slide && free_ == Free::Held && holds_[0] && holds_[1] && shape_.tip - shape_.butt >= slide::kMinShaft) {
		const int h = main_ ^ 1;
		if (in[h].tracked && pressed_[h] && !sticky_[h] && on_[h] >= 1.0) {
			sliding = true;
			const Pose gp   = BodyPose();
			const V3   off  = FistOffset(h, s);
			const V3   fist = Rotate(QConj(gp.rot), Sub(Add(in[h].pose.pos, Rotate(in[h].pose.rot, off)), gp.pos));   // the fist in the body's frame
			const V3   on   = Add(grips_[h].at, Rotate(grips_[h].rot, off));                                         // where the hand holds the shaft
			slide::In  si;
			si.t      = Dot(Sub(on, line_.at), line_.dir);
			si.raw    = Dot(Sub(fist, line_.at), line_.dir);
			si.perp   = Len(Sub(Sub(fist, line_.at), Mul(line_.dir, si.raw)));
			si.butt   = shape_.butt;
			si.tip    = shape_.tip;
			si.dt     = dt;
			si.tau    = s.slideTau;
			si.letGo  = s.letGo;
			if (lineOk[h] && lineSecond[h]) {
				// the grip library's modes: tight - the point stays where it is; loose - it goes where the library has it (the lag, the ends). Pulled away from the axis it lets go
				// in both, off an end in the loose one.
				const bool away = si.perp > si.letGo || (lo[h].mode == 2 && (si.raw < si.butt - si.margin || si.raw > si.tip + si.margin));
				if (away && !bodyHolds) {
					holds_[h]  = false;
					sticky_[h] = false;
					changed    = true;
				} else if (lo[h].mode == 2 && !away) {
					grips_[h].at = Sub(Add(line_.at, Mul(line_.dir, lo[h].t)), Rotate(grips_[h].rot, off));
				}
			} else {
				const slide::Out so = slide::Step(si);
				if (so.release && !bodyHolds) {
					holds_[h]  = false;
					sticky_[h] = false;
					changed    = true;
				} else if (!so.release) {
					grips_[h].at = Sub(Add(line_.at, Mul(line_.dir, so.t)), Rotate(grips_[h].rot, off));
				}
			}
		}
	}
	for (int h = 0; h < 2; ++h) {
		if (!holds_[h]) continue;
		const bool open = in[h].grip >= 0 && !pressed_[h] && !sticky_[h];
		bool go = open || (!in[h].tracked && holds_[h ^ 1] && in[h ^ 1].tracked);
		// the hand still placed in it (its grip never pressed) pulled away from the other one on it
		if (!go && !sliding && sticky_[h] && holds_[h ^ 1] && on_[h ^ 1] >= 1.0 &&
		    std::fabs(Len(Sub(in[0].pose.pos, in[1].pose.pos)) - Len(Sub(grips_[0].at, grips_[1].at))) > s.letGo)
			go = true;
		if (!go) continue;
		holds_[h]  = false;
		sticky_[h] = false;
		changed    = true;
	}
	if (changed) {
		if (holds_[0] || holds_[1]) {
			if (!holds_[main_]) main_ ^= 1;
		} else if (free_ == Free::Held) {
			free_ = Free::Falling;   // both let go: it falls, with the motion it had
		}
		Build(s);
	}

	if (free_ == Free::Flying) {
		// into the fist: the body slides to where the game's grip has it in that hand
		flyT_ += s.fly > 1e-4 ? std::max(dt, 0.0) / s.fly : 1.0;
		const double e = Smooth(flyT_);
		const Pose to = in[flyHand_].pose;   // the grip as placed: the body's frame is that fist
		const Grip home = FlyGrip(flyHand_, in[flyHand_], s);
		// the body's frame goes from where it was to where it stands when `home` sits in that hand (the game's hand: the frame IS the fist)
		const Q    goalRot = QNorm(QMul(to.rot, QConj(home.rot)));
		const Pose goal { Sub(to.pos, Rotate(goalRot, home.at)), goalRot };
		heft::Place(st_, body_, Grip {}, { Add(flyFrom_.pos, Mul(Sub(goal.pos, flyFrom_.pos), e)), QSlerp(flyFrom_.rot, goal.rot, e) });
		if (flyT_ >= 1.0) {
			free_            = Free::Held;
			grips_[flyHand_] = home;
			holds_[flyHand_] = true;
			if (flyHand_ == first_) grabs_[flyHand_] = 0;   // where the game holds it, as placed
			else ++grabs_[flyHand_];                         // a grip that closed on the shaft there
			on_[flyHand_]    = 1.0;
			main_            = flyHand_;
			Build(s);
		}
	} else if (free_ == Free::Stowed) {
		heft::Place(st_, body_, Grip {}, stowPose_);   // on the back, where the mod draws it
	} else if (free_ != Free::Held) {
		Fall(dt, s);
	} else {
		// The body: the main hand first (it is the one the weapon is put back in when lost).
		Grip       g[2];
		Controller from[2], to[2];
		int        k     = 0;
		const int  n     = (holds_[0] ? 1 : 0) + (holds_[1] ? 1 : 0);
		const Strength& f = n == 2 ? s.two : s.one;
		for (int i = 0; i < 2; ++i) {
			const int h = i == 0 ? main_ : main_ ^ 1;
			if (!holds_[h]) continue;
			g[k]        = grips_[h];
			g[k].on     = true;
			g[k].muscle = MuscleFor(f, body_.mass / n, MomentAbout(body_, grips_[h].at) / n, std::max(0.05, s.strength * (n == 2 && s.twoHandsDouble ? 2.0 : 1.0)), s.damping);
			to[k]       = now[h];
			from[k]     = haveLast_ ? last_[h] : to[k];
			++k;
		}
		State before = st_;   // (the weapon before the step: the bodies' friction holds back its slide in it)
		bool  stepped = false, putBack = false;
		// a jump of the main controller (a recenter, tracking back): the weapon put back in its hand
		if (haveLast_ && Len(Sub(in[main_].pose.pos, last_[main_].pose.pos)) > 0.5) {
			heft::Place(st_, body_, grips_[main_], in[main_].pose);
			putBack = true;
		} else if (dt > 0 && k > 0) {
			stepped = true;
			const Engine* engine = s.model == 1 ? g_engine : nullptr;   // a weight-physics library moves the body, when one is loaded
			// [weight] stab_in: a weapon in a wound is not snapped back into a far hand - the wound holds it, and the stab's own rules let it go (the controller goes on
			// into the monster; the snap threw the weapon out of the wound: "pulled off the line", the headset 2026-10-22)
			Settings held = s;
			if (stuck_) held.snap = std::max(s.snap, kStuckSnap);
			else if (s.bodies && sinceBody_ < kBodySnapHoldS) held.snap = std::max(s.snap, kBodySnap);   // (on a body: not snapped back through it)
			if (engine) {
				engine->Step(st_, body_, pieces_, nPieces_, twoHanded_, g, from, to, k, std::min(dt, 0.1), held);
			} else {
				World w;
				w.snapDistance = held.snap;
				w.gravity.y    = -9.81 * s.gravity;
				heft::Step(st_, body_, g, from, to, k, std::min(dt, 0.1), w);
			}
		}
		// [weight] monster_bodies: the weapon meets the bodies near it - put back on their surface, its motion into them taken away (contact.h). After the step, whichever
		// model moved it: the library is told nothing of the bodies.
		if (s.bodies && nBodies_ > 0 && Held() && stepped) {
			contact::Blade blades[6 + kHeadParts];
			int nb = 0;
			for (int i = 0; i < nPieces_ && i < 3; ++i) {
				blades[nb++] = { pieces_[i].a, pieces_[i].b, std::max(0.01, pieces_[i].radius) };
				if (HeadKnown(i)) {   // the kind's head piece: as it was against the monsters, as thin as the shaft against the other weapon (its head's own volume is the steel)
					blades[nb - 1].except = headOnly_;
					blades[nb++] = { pieces_[i].a, pieces_[i].b, std::max(0.01, std::min(pieces_[i].radius, pieces_[0].radius)), headOnly_ };
				}
			}
			for (int i = 0; i < nHead_; ++i) blades[nb++] = head_[i];   // (the head's own volume: the bodies of its id only - SetHeads)
			uint64_t skip = 0;   // a body a holding hand's controller is inside of: nothing to push against
			for (int i = 0; i < nBodies_; ++i)
				for (int h = 0; h < 2; ++h)
					if (holds_[h] && contact::Inside(bodies_[i], in[h].pose.pos)) skip |= 1ull << i;
			contact::Params cp;
			cp.friction = s.bodyFriction >= 0 ? s.bodyFriction : BodyFriction(kind_);
			if (wedged_ && std::isfinite(s.wedgeFriction) && s.wedgeFriction >= 0) cp.friction *= s.wedgeFriction;   // (caught between several: the wedge's own)
			// [weight] stab_in (stab.h): the library's stab - a weapon in a wound is held on it first, and the contact leaves that monster's bodies to it
			const stab::Engine* stabber = stab::GetEngine();
			const V3 tipL = Add(line_.at, Mul(line_.dir, shape_.tip));
			const V3 baseL = nPieces_ > 0 ? pieces_[nPieces_ - 1].a : Add(line_.at, Mul(line_.dir, shape_.butt));   // (the point's piece: the part that goes in)
			if (stuck_ && (!s.stab || !stabber)) {
				Unstick(!s.stab ? "stab_in turned off" : "the stab library went away");
			} else if (stuck_) {
				// the tear (the mod's own, not the toolkit's: never stuck for good) - a holding hand's controller far across the stab line for a moment. Across what: the
				// tear's distance plus kTearPerAlong of the hand's distance along the line from the entry - a long weapon held far back, or a hand driven on into the monster a little
				// off the line, is far across without pulling (the headset, 2026-10-22: "torn free" within 46 ms of going in).
				double across = 0, over = 0;
				const V3 a = lastIn_.axis, e = lastIn_.entry;
				if (Len(a) > 0.5)
					for (int h = 0; h < 2; ++h) {
						if (!holds_[h]) continue;
						const V3 rel = Sub(in[h].pose.pos, e);
						const double along = Dot(rel, a), acr = Len(Sub(rel, Mul(a, along)));
						across = std::max(across, acr);
						over = std::max(over, acr / std::max(1e-3, s.stabParams.breakM + kTearPerAlong * std::fabs(along)));
					}
				tearFor_ = over > 1.0 ? tearFor_ + std::max(0.0, dt) : 0.0;
				if (tearFor_ > kTearHold) {
					Unstick("torn free (the hand pulled away)");
				} else {
					StabStep(*stabber, tipL, baseL, bodies_, nBodies_, nullptr, nullptr, 0, dt, s);
					stabRep_.pull = across;
				}
			}
			if (stuck_)
				for (int i = 0; i < nBodies_ && i < 64; ++i)
					if (bodies_[i].id == stuckId_) skip |= 1ull << i;
			// [weight] cleave: the bodies the stroke went through are left alone (not what those monsters hold)
			ForgetCleft(dt);
			for (int i = 0; i < nBodies_ && i < 64; ++i)
				if (!bodies_[i].held && Cleft_(bodies_[i].id)) skip |= 1ull << i;
			const State unmet = st_;   // (the weapon before the contact: a stab undoes the contact's push and stop - it goes on at the speed it came in with)
			contact::Resolve(st_, &before, body_, blades, nb, bodies_, nBodies_, cp, skip, &contact_);
			if (CanCleave(s)) {
				// a new touch of a monster's body hard enough: cut through. The contact again without it, from the weapon as it was before it stopped there; its
				// arrival kept in the report (it is that monster's strike), the motion slowed by what it took.
				uint32_t through[contact::Report::kMet];
				int nThrough = 0;
				double keep = 1.0;
				for (int i = 0; i < contact_.nMet; ++i) {
					const contact::Report::Met& m = contact_.met[i];
					bool was = false;
					for (int j = 0; j < nMetWas_; ++j) was = was || metWas_[j] == m.id;
					if (!m.onBody || m.onHeld || was || Cleft_(m.id) || m.impulse < s.cleaveImpulse || m.approach < s.cleaveMinSpeed) continue;   // ("and swords only fast")
					const double cost = std::max(0.0, s.cleaveCost);
					if (strokeCleaves_ == 0) strokeLeft_ = m.impulse;   // (the stroke's force: its first body's arrival)
					if (strokeLeft_ < cost) continue;                   // spent: this one stops it
					strokeLeft_ -= cost;
					through[nThrough++] = m.id;
					const double k = std::clamp(1.0 - cost / m.impulse, 0.0, 1.0);
					keep *= k;
					++cleaves_;
					++strokeCleaves_;
					lastCleave_ = { m.id, m.impulse, s.cleaveImpulse, k, m.approach, strokeCleaves_, strokeLeft_ };
					if (nCleft_ < kCleft) cleft_[nCleft_++] = { m.id, 0.0 };
				}
				if (nThrough > 0) {
					const contact::Report first = contact_;
					for (int i = 0; i < nBodies_ && i < 64; ++i)
						if (!bodies_[i].held && Cleft_(bodies_[i].id)) skip |= 1ull << i;
					st_ = unmet;
					st_.vel    = Mul(st_.vel, keep);
					st_.angVel = Mul(st_.angVel, keep);
					contact::Resolve(st_, &before, body_, blades, nb, bodies_, nBodies_, cp, skip, &contact_);
					if (contact_.bodies == 0) {   // (nothing else met: the report is the first one's, of the bodies gone through alone)
						contact_ = first;
						contact_.nMet = 0;
					}
					for (int t = 0; t < nThrough; ++t)   // (the bodies gone through: their arrival is still theirs to be judged by)
						for (int i = 0; i < first.nMet; ++i) {
							if (first.met[i].id != through[t]) continue;
							if (contact_.nMet == 0) {
								contact_.id   = first.met[i].id;
								contact_.body = first.met[i].body;
							}
							if (contact_.nMet < contact::Report::kMet) contact_.met[contact_.nMet++] = first.met[i];
							else contact_.met[contact::Report::kMet - 1] = first.met[i];
						}
				}
			}
			nMetWas_ = 0;
			for (int i = 0; i < contact_.nMet; ++i) metWas_[nMetWas_++] = contact_.met[i].id;
			sinceBody_ = contact_.bodies > 0 || stuck_ ? 0.0 : sinceBody_ + std::max(0.0, dt);
			if (s.stab && stabber && !stuck_ && !stabRep_.left) {   // a strike: does it go in? The point's own when the point is in a body, else the contact's
				stab::Strike strike;
				strike.thrusting = stabbing_;
				strike.before    = unmet;
				if (PointStrike(PointAt(unmet, body_, baseL), PointAt(unmet, body_, tipL), bodies_, nBodies_, skip, &strike.body, &strike.point, &strike.normal)) {
					StabStep(*stabber, tipL, baseL, bodies_, nBodies_, &strike, nullptr, 0, dt, s);
				} else if (strike.body < 0 && contact_.bodies > 0 && contact_.body >= 0 && contact_.body < nBodies_ && !bodies_[contact_.body].held) {   // (the point in a shield: no stab at all)
					strike.body   = contact_.body;
					strike.point  = contact_.point;
					strike.normal = contact_.normal;
					StabStep(*stabber, tipL, baseL, bodies_, nBodies_, &strike, nullptr, 0, dt, s);
				}
			}
		} else {
			contact_ = {};
			nMetWas_ = 0;
			if (nCleft_) ForgetCleft(dt);
			sinceBody_ += std::max(0.0, dt);
			if (stuck_) {
				const stab::Engine* stabber = stab::GetEngine();
				if (!Held() || putBack || !s.stab || !stabber) {   // out of the hands, put back in the fist, the tick off: no wound
					Unstick(!Held() ? "the weapon left the hands" : putBack ? "the weapon was put back in the hand (the controller jumped)" : !s.stab ? "stab_in turned off" : "the stab library went away");
				} else {   // a step without bodies or without a step of the weapon: the library counts the body missing (it waits for it a little)
					const V3 tipL = Add(line_.at, Mul(line_.dir, shape_.tip));
					const V3 baseL = nPieces_ > 0 ? pieces_[nPieces_ - 1].a : Add(line_.at, Mul(line_.dir, shape_.butt));
					StabStep(*stabber, tipL, baseL, nullptr, 0, nullptr, nullptr, 0, dt, s);
				}
			}
		}
	}
	if (stuck_ && !Held()) Unstick("the weapon left the hands");   // [weight] stab_in: let go, thrown, called back - in no body
	// A weapon that has landed and lain long enough goes to the hero's back (the mod draws it there).
	// (not a thrown one - it only has to end - nor one whose release may still turn out to be a throw)
	if (free_ == Free::Lying) {
		if (s.stowAfter >= 0 && !thrown_ && !stowHold_) {
			lyingFor_ += std::max(dt, 0.0);
			if (lyingFor_ >= s.stowAfter) {
				free_     = Free::Stowed;
				stowPose_ = BodyPose();   // until the mod tells where the back is
			}
		}
	} else {
		lyingFor_ = 0;
	}
	if (thrown_ && !Held()) thrownFor_ += std::max(dt, 0.0);
	for (int h = 0; h < 2; ++h) last_[h] = now[h];
	haveLast_ = true;

	// Where to draw the hands: easing onto the weapon and off it.
	for (int h = 0; h < 2; ++h) {
		const double target = holds_[h] ? 1.0 : 0.0;
		const double rate   = s.ease > 1e-4 ? std::max(dt, 0.0) / s.ease : 1.0;
		on_[h] = on_[h] < target ? std::min(target, on_[h] + rate) : std::max(target, on_[h] - rate);
		out[h].holds = holds_[h];
		out[h].on    = on_[h];
		out[h].grabs = grabs_[h];
		if (lineOk[h]) {
			out[h].mode      = lo[h].mode;
			out[h].roll      = lo[h].roll;
			out[h].events    = lo[h].events;
			out[h].hapticAmp = lo[h].hapticAmp;
			out[h].hapticMs  = lo[h].hapticMs;
		}
		if (on_[h] > 0) {
			const Pose   p = OnWeapon(h);
			const double e = Smooth(on_[h]);
			out[h].pose    = { Add(in[h].pose.pos, Mul(Sub(p.pos, in[h].pose.pos), e)), QSlerp(in[h].pose.rot, p.rot, e) };
		}
	}
}

}   // namespace heft::hold
