// Walls: see walls.h.
//
// Each round, each piece of the weapon against each wall near it: the face (of those it may leave by) it is least deep past, the push that puts it back on
// that face - shared between moving the weapon and turning it as its mass and inertia at that point say (push.h, as contact.cpp) - then its velocity
// there: the part into the wall taken away (and a little bounce), the part along the face held back by friction up to that push times the friction.

#include "walls.h"
#include "push.h"

#include <algorithm>
#include <cmath>

namespace heft::walls {

namespace {

using push::Give;
using push::InvInertia;
using push::Kick;
using push::Shift;

constexpr double kNone = -1e18;
constexpr double kBeside = 1e18;   // Beyond: no part of the capsule over that face

double Comp(V3 v, int k) { return k == 0 ? v.x : k == 1 ? v.y : v.z; }
double Sign(int face) { return (face & 1) ? -1.0 : 1.0; }

// The part of the capsule's axis (s in [0, 1]) within |p_i| <= half_i + r on every axis i in `axes` (a bit each). False: none of it.
bool Clip(V3 la, V3 lb, double r, V3 half, int axes, double* s0, double* s1) {
	double lo = 0.0, hi = 1.0;
	for (int i = 0; i < 3; ++i) {
		if (!(axes >> i & 1)) continue;
		const double a = Comp(la, i), d = Comp(lb, i) - a, lim = Comp(half, i) + r;
		if (std::fabs(d) < 1e-12) {
			if (std::fabs(a) > lim) return false;
			continue;
		}
		double t1 = (-lim - a) / d, t2 = (lim - a) / d;
		if (t1 > t2) std::swap(t1, t2);
		lo = std::max(lo, t1);
		hi = std::min(hi, t2);
		if (lo > hi) return false;
	}
	*s0 = lo;
	*s1 = hi;
	return true;
}

bool Overlaps(V3 la, V3 lb, double r, V3 half) {
	double s0, s1;
	return Clip(la, lb, r, half, 7, &s0, &s1);
}

// The weapon's pieces that meet walls (not a head's volume meant for the other weapon of a pair only).
bool Uses(const contact::Blade& b) { return b.only == 0; }

// The faces the capsules are beyond (or resting on, within `rest`) - a bit each; and the face they are farthest beyond (*best, its margin *margin).
int FacesBeyond(const V3* la, const V3* lb, const double* r, int n, V3 half, double rest, int* best, double* margin) {
	int bits = 0;
	*best = -1;
	*margin = kNone;
	for (int f = 0; f < kFaces; ++f) {
		double m = 1e18;
		for (int j = 0; j < n; ++j) m = std::min(m, Beyond(la[j], lb[j], r[j], half, f));
		if (n > 0 && m >= -rest) bits |= 1 << f;
		if (n > 0 && m < kBeside && m > *margin) { *margin = m; *best = f; }   // (the face it is farthest beyond of those it is over)
	}
	return bits;
}

struct Near {
	int   wall = -1;
	Side* side = nullptr;
	int   faces = 0;      // the faces it may be put back out of
	int   prefer = -1;    // its face its body holds (the body's last way out), -1 none
	bool  off = false;    // left alone this step
	bool  touched = false;
};

uint32_t BodyOf(const Wall& w) { return w.body ? w.body : w.id; }

// The face of a box its body holds: the one whose outward normal is the body's last way out (within ~25 degrees), -1 none.
int HeldFace(const Wall& w, const BodySide* bs) {
	if (!bs) return -1;
	int best = -1;
	double bestDot = 0.9;
	for (int f = 0; f < kFaces; ++f) {
		const double d = Dot(Mul(w.axis[f / 2], Sign(f)), bs->normal);
		if (d > bestDot) { bestDot = d; best = f; }
	}
	return best;
}

// Pick with the body's held face: while a body holds a way out, its boxes put the weapon out that way only - their other faces are seams inside the one object (a
// stepped stone wall pushed the blade back along itself through them, the test's 37 cm); a box the weapon went right through is crossed back that way too.
int PickHeld(const Near& e, V3 a, V3 z, double r, V3 half, const Params& p, double* depth, double* s) {
	(void)p;
	return Pick(a, z, r, half, e.prefer >= 0 ? 1 << e.prefer : e.faces, depth, s);
}

}   // namespace

V3 Local(const Wall& w, V3 p) {
	const V3 d = Sub(p, w.c);
	return { Dot(d, w.axis[0]), Dot(d, w.axis[1]), Dot(d, w.axis[2]) };
}

double Beyond(V3 la, V3 lb, double r, V3 half, int face) {
	const int k = face / 2;
	double s0, s1;
	if (!Clip(la, lb, r, half, 7 & ~(1 << k), &s0, &s1)) return kBeside;   // (no part of it over the face)
	const double sg = Sign(face);
	const double a = Comp(la, k), d = Comp(lb, k) - a;
	return std::min(sg * (a + d * s0), sg * (a + d * s1)) - Comp(half, k) - r;
}

int Pick(V3 la, V3 lb, double r, V3 half, int faces, double* depth, double* s) {
	double s0, s1;
	int face = -1;
	if (Clip(la, lb, r, half, 7, &s0, &s1)) {   // in the box (widened by r): out by the face it may leave by that it is least deep past - unless it only touches one
		double least = 1e18;
		for (int f = 0; f < kFaces; ++f) {
			const int k = f / 2;
			const double sg = Sign(f), a = Comp(la, k), d = Comp(lb, k) - a;
			const double v0 = sg * (a + d * s0), v1 = sg * (a + d * s1);
			const double df = Comp(half, k) + r - std::min(v0, v1);
			least = std::min(least, df);
			if ((faces >> f & 1) && (face < 0 || df < *depth)) {
				face = f;
				*depth = df;
				*s = v0 <= v1 ? s0 : s1;
			}
		}
		return least > 1e-7 ? face : -1;
	}
	for (int f = 0; f < kFaces; ++f) {   // clear of it now: through it in one step? (the part over a face it came in by is past the far side)
		if (!(faces >> f & 1)) continue;
		const int k = f / 2;
		if (!Clip(la, lb, r, half, 7 & ~(1 << k), &s0, &s1)) continue;
		const double sg = Sign(f), a = Comp(la, k), d = Comp(lb, k) - a;
		const double v0 = sg * (a + d * s0), v1 = sg * (a + d * s1);
		if (std::max(v0, v1) >= -Comp(half, k) - r) continue;
		const double df = Comp(half, k) + r - std::min(v0, v1);
		if (face < 0 || df < *depth) {
			face = f;
			*depth = df;
			*s = v0 <= v1 ? s0 : s1;
		}
	}
	return face;
}

Side* Memory::Of(uint32_t id) {
	for (int i = 0; i < n; ++i)
		if (s[i].id == id) return &s[i];
	if (n >= kSides) {   // full: the one seen longest ago goes
		int old = 0;
		for (int i = 1; i < n; ++i)
			if (s[i].seen < s[old].seen) old = i;
		s[old] = Side {};
		s[old].id = id;
		s[old].seen = tick;
		return &s[old];
	}
	s[n] = Side {};
	s[n].id = id;
	s[n].seen = tick;
	return &s[n++];
}

BodySide* Memory::Body(uint32_t body, bool make) {
	for (int i = 0; i < nb; ++i)
		if (b[i].body == body) return &b[i];
	if (!make) return nullptr;
	int at = nb < kBodies ? nb++ : 0;
	for (int i = 1; nb == kBodies && i < nb; ++i)
		if (b[i].seen < b[at].seen) at = i;
	b[at] = BodySide {};
	b[at].body = body;
	b[at].seen = tick;
	return &b[at];
}

void Memory::Forget() {
	int k = 0;
	for (int i = 0; i < n; ++i)
		if (tick - s[i].seen <= 30) s[k++] = s[i];   // (a third of a second of steps: a wall the scan lost for a moment keeps its side)
	n = k;
	k = 0;
	for (int i = 0; i < nb; ++i)
		if (tick - b[i].seen <= 30) b[k++] = b[i];
	nb = k;
}

bool Resolve(State& st, const State* before, const Body& b, const contact::Blade* blades, int nBlades, const Wall* walls, int n, Memory& mem, const Params& p, Report* out) {
	Report rep;
	if (out) *out = rep;
	if (!walls || n <= 0 || !blades || nBlades <= 0 || !(b.mass > 0)) return false;
	n = std::min(n, kMaxWalls);
	++mem.tick;
	const InvInertia ii(b.inertia);
	const double invMass = 1.0 / b.mass;
	constexpr int kPieces = 24;
	int used[kPieces];
	int nUsed = 0;
	for (int j = 0; j < nBlades && nUsed < kPieces; ++j)
		if (Uses(blades[j])) used[nUsed++] = j;
	if (nUsed == 0) return false;
	// the walls near the whole weapon: a ball round its pieces against each wall's (the pushes move it by maxPush a round at most)
	V3 lo = PointAt(st, b, blades[used[0]].butt), hi = lo;
	double thick = 0;
	for (int q = 0; q < nUsed; ++q) {
		const contact::Blade& bl = blades[used[q]];
		for (const V3 e : { PointAt(st, b, bl.butt), PointAt(st, b, bl.tip) }) {
			lo = { std::min(lo.x, e.x), std::min(lo.y, e.y), std::min(lo.z, e.z) };
			hi = { std::max(hi.x, e.x), std::max(hi.y, e.y), std::max(hi.z, e.z) };
		}
		thick = std::max(thick, bl.radius);
	}
	const V3 mid = Mul(Add(lo, hi), 0.5);
	const double ball = 0.5 * Len(Sub(hi, lo)) + thick + p.maxPush * std::max(1, p.iterations);
	Near nearW[kMaxWalls];
	int nNear = 0;
	V3 la[kPieces], lb[kPieces];
	double rr[kPieces];
	for (int i = 0; i < n; ++i) {
		const Wall& w = walls[i];
		if (Len(Sub(mid, w.c)) > ball + Len(w.half)) continue;
		Near& e = nearW[nNear++];
		e.wall = i;
		e.side = mem.Of(w.id);
		e.side->seen = mem.tick;
		for (int q = 0; q < nUsed; ++q) {
			la[q] = Local(w, PointAt(st, b, blades[used[q]].butt));
			lb[q] = Local(w, PointAt(st, b, blades[used[q]].tip));
			rr[q] = blades[used[q]].radius;
		}
		bool in = false;
		for (int q = 0; q < nUsed && !in; ++q) in = Overlaps(la[q], lb[q], rr[q], w.half);
		if (e.side->ignore) {   // left alone until the weapon is clear of it
			if (!in) {
				e.side->ignore = false;
				e.side->face = -1;
			} else {
				e.off = true;
				++rep.ignored;
				continue;
			}
		}
		if (before) {
			V3 ba[kPieces], bb[kPieces];
			for (int q = 0; q < nUsed; ++q) {
				ba[q] = Local(w, PointAt(*before, b, blades[used[q]].butt));
				bb[q] = Local(w, PointAt(*before, b, blades[used[q]].tip));
			}
			int bestWas;
			double marginWas;
			e.faces = FacesBeyond(ba, bb, rr, nUsed, w.half, p.rest, &bestWas, &marginWas);
		}
		if (!e.faces && e.side->face >= 0) e.faces = 1 << e.side->face;
		if (const BodySide* bs = mem.Body(BodyOf(w), false); bs && mem.tick - bs->seen <= (uint32_t)std::max(0, p.holdSteps)) {   // its body's face held
			e.prefer = HeldFace(w, bs);
			if (e.prefer >= 0) e.faces |= 1 << e.prefer;
		}
		if (w.open != 0x3F) {   // [weight] wall_contact_mode 2 (Bear's): only the sides no other cell of the wall stands against
			const int was = e.faces;
			e.faces &= w.open;
			if (e.prefer >= 0 && !(w.open >> e.prefer & 1)) e.prefer = -1;
			if (was && !e.faces) {   // it came at this cell through a neighbour of the same wall: that one holds it out
				e.off = true;
				continue;
			}
		}
		if (!e.faces) {   // no face it came in by: inside since the wall was first given - alone until clear
			if (in) {
				e.side->ignore = true;
				e.off = true;
				++rep.ignored;
			}
		}
	}
	double best = -1.0;
	for (int round = 0; round < std::max(1, p.iterations); ++round) {
		bool any = false;
		for (int k = 0; k < nNear; ++k) {
			Near& e = nearW[k];
			if (e.off || !e.faces) continue;
			const Wall& w = walls[e.wall];
			for (int q = 0; q < nUsed; ++q) {
				const contact::Blade& blade = blades[used[q]];
				const V3 a = Local(w, PointAt(st, b, blade.butt)), z = Local(w, PointAt(st, b, blade.tip));
				double depth = 0, s = 0;
				const int face = PickHeld(e, a, z, blade.radius, w.half, p, &depth, &s);
				if (face < 0) continue;
				if (depth > 2.0 * Comp(w.half, face / 2) + 2.0 * blade.radius + p.through) {   // deeper than the wall is thick and then some: it got there some other way
					e.side->ignore = true;
					e.off = true;
					++rep.ignored;
					break;
				}
				const V3 nrm = Mul(w.axis[face / 2], Sign(face));
				const V3 onLocal = Add(blade.butt, Mul(Sub(blade.tip, blade.butt), s));
				const V3 point = Sub(PointAt(st, b, onLocal), Mul(nrm, blade.radius));
				const V3 r = Sub(point, st.com);
				// the position: back on the face
				const double push = std::min(depth, p.maxPush);
				const double give = Give(st, ii, invMass, r, nrm);
				if (give > 1e-12) Shift(st, ii, invMass, r, nrm, push / give);
				// the slide along the face since `before`, held back by friction up to the push times the friction (contact.cpp: on the step's displacements)
				double slid = 0;
				if (before && p.friction > 0) {
					const V3 moved = Sub(PointAt(st, b, onLocal), PointAt(*before, b, onLocal));
					const V3 across = Sub(moved, Mul(nrm, Dot(moved, nrm)));
					const double l = Len(across);
					if (l > 1e-9) {
						const V3 dir = Mul(across, 1.0 / l);
						const double hold = std::min(l, p.friction * push);
						const V3 r1 = Sub(PointAt(st, b, onLocal), st.com);
						const double gt = Give(st, ii, invMass, r1, dir);
						if (gt > 1e-12) Shift(st, ii, invMass, r1, dir, -hold / gt);
						slid = l - hold;
					}
				}
				// the velocity there: into the wall taken away, a little back as a bounce; along it friction up to that
				const V3 r2 = Sub(Sub(PointAt(st, b, onLocal), Mul(nrm, blade.radius)), st.com);
				const V3 v = Add(st.vel, Cross(st.angVel, r2));
				const double vn = Dot(v, nrm);
				double jn = 0, slide = 0;
				if (vn < 0 && give > 1e-12) {
					jn = -(1.0 + std::max(0.0, p.restitution)) * vn / give;
					Kick(st, ii, invMass, r2, Mul(nrm, jn));
				}
				{
					const V3 v2 = Add(st.vel, Cross(st.angVel, r2));
					const V3 vt = Sub(v2, Mul(nrm, Dot(v2, nrm)));
					const double lt = Len(vt);
					if (lt > 1e-9 && jn > 0) {
						const V3 dir = Mul(vt, 1.0 / lt);
						const double gt = Give(st, ii, invMass, r2, dir);
						const double jt = gt > 1e-12 ? std::min(lt / gt, std::max(0.0, p.friction) * jn) : 0.0;
						Kick(st, ii, invMass, r2, Mul(dir, -jt));
						slide = std::max(0.0, lt - jt * gt);
					} else {
						slide = lt;
					}
				}
				(void)slid;
				any = true;
				e.touched = true;
				if (BodySide* bs = mem.Body(BodyOf(w), true)) { bs->normal = nrm; bs->seen = mem.tick; }
				const double score = jn > 0 ? jn : depth * 1e-3;
				if (round == 0 && score > best) {
					best         = score;
					rep.id       = BodyOf(w);
					rep.wall     = e.wall;
					rep.face     = face;
					rep.depth    = depth;
					rep.approach = std::max(0.0, -vn);
					rep.slide    = slide;
					rep.impulse  = jn;
					rep.blade    = used[q];
					rep.along    = s;
					rep.normal   = nrm;
					rep.point    = point;
				}
			}
		}
		if (!any) break;
	}
	for (int k = 0; k < nNear; ++k) {   // the face each wall now lets the weapon out of (the one it is farthest beyond), for a step that starts inside
		Near& e = nearW[k];
		if (e.touched) ++rep.walls;
		if (e.side->ignore) continue;
		const Wall& w = walls[e.wall];
		for (int q = 0; q < nUsed; ++q) {
			la[q] = Local(w, PointAt(st, b, blades[used[q]].butt));
			lb[q] = Local(w, PointAt(st, b, blades[used[q]].tip));
		}
		int bestNow;
		double marginNow;
		FacesBeyond(la, lb, rr, nUsed, w.half, p.rest, &bestNow, &marginNow);
		if (bestNow >= 0 && marginNow >= -p.rest) e.side->face = bestNow;
	}
	mem.Forget();
	if (out) *out = rep;
	return rep.walls > 0;
}

V3 Block(V3 a, V3 b, double r, const V3* wasA, const V3* wasB, const Wall* walls, int n, Memory& mem, const Params& p, Report* out) {
	Report rep;
	V3 off {};
	if (out) *out = rep;
	if (!walls || n <= 0) return off;
	n = std::min(n, kMaxWalls);
	++mem.tick;
	const V3 mid = Mul(Add(a, b), 0.5);
	const double ball = 0.5 * Len(Sub(b, a)) + r + p.returnGap + 0.1;
	Near nearW[kMaxWalls];
	int nNear = 0;
	for (int i = 0; i < n; ++i) {
		const Wall& w = walls[i];
		if (Len(Sub(mid, w.c)) > ball + Len(w.half)) continue;
		Near& e = nearW[nNear++];
		e.wall = i;
		e.side = mem.Of(w.id);
		e.side->seen = mem.tick;
		const V3 la = Local(w, a), lb = Local(w, b);
		if (e.side->ignore) {
			if (!Overlaps(la, lb, r, w.half)) {
				e.side->ignore = false;
				e.side->face = -1;
			} else {
				e.off = true;
				++rep.ignored;
				continue;
			}
		}
		if (wasA && wasB) {
			const V3 wa = Local(w, *wasA), wb = Local(w, *wasB);
			int bestWas;
			double marginWas;
			e.faces = FacesBeyond(&wa, &wb, &r, 1, w.half, p.rest, &bestWas, &marginWas);
		}
		if (!e.faces && e.side->face >= 0) e.faces = 1 << e.side->face;
		if (const BodySide* bs = mem.Body(BodyOf(w), false); bs && mem.tick - bs->seen <= (uint32_t)std::max(0, p.holdSteps)) {   // its body's face held
			e.prefer = HeldFace(w, bs);
			if (e.prefer >= 0) e.faces |= 1 << e.prefer;
		}
		if (w.open != 0x3F) {   // [weight] wall_contact_mode 2 (Bear's)'s cells: only the open sides (as Resolve)
			const int was = e.faces;
			e.faces &= w.open;
			if (e.prefer >= 0 && !(w.open >> e.prefer & 1)) e.prefer = -1;
			if (was && !e.faces) {
				e.off = true;
				continue;
			}
		}
		if (!e.faces && Overlaps(la, lb, r, w.half)) {
			e.side->ignore = true;
			e.off = true;
			++rep.ignored;
		}
	}
	double best = -1.0;
	for (int round = 0; round < std::max(1, p.iterations); ++round) {
		bool any = false;
		for (int k = 0; k < nNear; ++k) {
			Near& e = nearW[k];
			if (e.off || !e.faces) continue;
			const Wall& w = walls[e.wall];
			const V3 la = Local(w, Add(a, off)), lb = Local(w, Add(b, off));
			double depth = 0, s = 0;
			const int face = PickHeld(e, la, lb, r, w.half, p, &depth, &s);
			if (face < 0) continue;
			if (depth > 2.0 * Comp(w.half, face / 2) + 2.0 * r + p.through) {
				e.side->ignore = true;
				e.off = true;
				++rep.ignored;
				continue;
			}
			const V3 nrm = Mul(w.axis[face / 2], Sign(face));
			off = Add(off, Mul(nrm, depth));
			any = true;
			e.touched = true;
			if (BodySide* bs = mem.Body(BodyOf(w), true)) { bs->normal = nrm; bs->seen = mem.tick; }
			if (round == 0 && depth > best) {
				best       = depth;
				rep.id     = BodyOf(w);
				rep.wall   = e.wall;
				rep.face   = face;
				rep.depth  = depth;
				rep.normal = nrm;
				rep.along  = s;
				rep.point  = Sub(Add(Add(a, Mul(Sub(b, a), s)), off), Mul(nrm, r));
			}
		}
		if (!any) break;
	}
	rep.gap = Len(off);
	if (rep.gap > p.returnGap) {   // the controller far past the drawn hand: back to the controller, through the walls it is in, until it is clear of them
		for (int k = 0; k < nNear; ++k)
			if (nearW[k].touched) {
				nearW[k].side->ignore = true;
				++rep.returned;
			}
		off = {};
	}
	for (int k = 0; k < nNear; ++k) {
		Near& e = nearW[k];
		if (e.touched && !e.side->ignore) ++rep.walls;
		if (e.side->ignore) continue;
		const Wall& w = walls[e.wall];
		const V3 la = Local(w, Add(a, off)), lb = Local(w, Add(b, off));
		int bestNow;
		double marginNow;
		FacesBeyond(&la, &lb, &r, 1, w.half, p.rest, &bestNow, &marginNow);
		if (bestNow >= 0 && marginNow >= -p.rest) e.side->face = bestNow;
	}
	mem.Forget();
	if (out) *out = rep;
	return off;
}

}   // namespace heft::walls
