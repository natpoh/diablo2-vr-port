// Contact: see contact.h.
//
// One body after the other, a few rounds: the blade's closest point to the body's axis, how deep the blade is in it, the push that puts it back on
// the surface - shared between moving the weapon and turning it as its mass and its inertia at that point say (the push at a point r from the centre
// of mass along n moves a body of mass m and inertia I by n/m and turns it by I^-1 (r x n), per unit of push) - then the same for its velocity there:
// the part into the body taken away (and a bounce, if any), the part along the surface held back by friction up to that push times the friction.

#include "contact.h"
#include "push.h"

#include <algorithm>
#include <cmath>

namespace heft::contact {

namespace {

using push::Give;   // (push.h: shared with the stab, stab.h)
using push::InvInertia;
using push::Kick;
using push::Shift;

// How deep the weapon's pieces are in a capsule (< 0: that far off it), the weapon at `st`.
double DepthIn(const State& st, const Body& b, const Blade* blades, int nBlades, const Capsule& c) {
	double deepest = -1e9;
	for (int j = 0; j < nBlades; ++j) {
		if (!Meets(blades[j], c)) continue;
		double s, t;
		const V3 a = PointAt(st, b, blades[j].butt), z = PointAt(st, b, blades[j].tip);
		ClosestOnSegments(a, z, c.a, c.b, &s, &t);
		const double dist = Len(Sub(Add(a, Mul(Sub(z, a), s)), Add(c.a, Mul(Sub(c.b, c.a), t))));
		deepest = std::max(deepest, blades[j].radius + c.radius - dist);
	}
	return deepest;
}

// The step swept (Resolve's `before` to `st`): a fast blade moves more in one step than a thin limb lets it in before it counts as "deep inside already"
// (Params::deepShare) - it would land past the limb, or deep in it and be left there, and pass through untouched (the headset 2026-10-23: «одноручное оружие проходит
// насквозь, даже не замедляясь»; cleanroom/tests/strike_measure_test: a sword at 8 m/s across a 7 cm leg, at 12 m/s across a 4 cm arm after a step on it). For each
// body the weapon was not deep in before the step (clear of it, or resting on its surface after the last step's contact) and is past or too deep in after it, the step
// is sampled; the weapon is put back at its last pose before it got too deep (or, grazing through the edge, at its deepest) - its velocity kept: the contact below
// then stops it there, or it glances along. A body it was deep in already is left as before.
void Sweep(State& st, const State& before, const Body& b, const Blade* blades, int nBlades, const Capsule* caps, int n, const Params& p, uint64_t skip) {
	double moved = 0;   // the most any end of the weapon moved in the step
	double thinnest = 1e9;
	for (int j = 0; j < nBlades; ++j) {
		moved = std::max(moved, Len(Sub(PointAt(st, b, blades[j].butt), PointAt(before, b, blades[j].butt))));
		moved = std::max(moved, Len(Sub(PointAt(st, b, blades[j].tip), PointAt(before, b, blades[j].tip))));
		thinnest = std::min(thinnest, blades[j].radius);
	}
	if (moved < 1e-4) return;
	const auto at = [&](double u) {
		State mid = st;
		mid.com = Add(before.com, Mul(Sub(st.com, before.com), u));
		mid.rot = QSlerp(before.rot, st.rot, u);
		return mid;
	};
	double back = 2.0;   // the earliest share of the step the weapon is put back to
	for (int i = 0; i < n; ++i) {
		if (i < 64 && (skip >> i & 1u)) continue;
		const Capsule& c = caps[i];
		const double limit = p.deepShare * c.radius + thinnest;
		if (DepthIn(before, b, blades, nBlades, c) > limit) continue;   // deep in it already (got there some other way): as before
		const double after = DepthIn(st, b, blades, nBlades, c);
		if (after > 0 && after <= limit) continue;   // in it a little: the contact puts it back as ever
		// past it or too deep: sampled finely enough that no sample steps over the deep core (0.8 of the radius across)
		const int k = std::clamp((int)std::ceil(moved / std::max(0.005, 0.3 * c.radius)), 2, 32);
		double deepestU = -1, deepest = 0;
		for (int q = 1; q <= k; ++q) {
			const double u = (double)q / k;
			if (u >= back) break;
			const double d = DepthIn(at(u), b, blades, nBlades, c);
			if (d > limit) {   // too deep from here on: the sample before
				back = std::min(back, (double)(q - 1) / k);
				deepestU = -1;
				break;
			}
			if (d > deepest) { deepest = d; deepestU = u; }
		}
		if (deepestU >= 0 && after <= 0) back = std::min(back, deepestU);   // grazed through its edge: where it was deepest
	}
	if (back > 1.0) return;
	const State to = at(back);
	st.com = to.com;
	st.rot = to.rot;
}

}   // namespace

void ClosestOnSegments(V3 p0, V3 p1, V3 q0, V3 q1, double* s, double* t) {
	const V3 d1 = Sub(p1, p0), d2 = Sub(q1, q0), r = Sub(p0, q0);
	const double a = Dot(d1, d1), e = Dot(d2, d2), f = Dot(d2, r);
	double ss = 0, tt = 0;
	if (a <= 1e-12 && e <= 1e-12) {
		ss = tt = 0;
	} else if (a <= 1e-12) {
		tt = std::clamp(f / e, 0.0, 1.0);
	} else {
		const double c = Dot(d1, r);
		if (e <= 1e-12) {
			ss = std::clamp(-c / a, 0.0, 1.0);
		} else {
			const double b = Dot(d1, d2), den = a * e - b * b;
			ss = den > 1e-12 ? std::clamp((b * f - c * e) / den, 0.0, 1.0) : 0.5;   // (parallel: the middle of the first)
			tt = (b * ss + f) / e;
			if (tt < 0) {
				tt = 0;
				ss = std::clamp(-c / a, 0.0, 1.0);
			} else if (tt > 1) {
				tt = 1;
				ss = std::clamp((b - c) / a, 0.0, 1.0);
			}
		}
	}
	*s = ss;
	*t = tt;
}

bool Inside(const Capsule& c, V3 p, double margin) {
	double s, t;
	ClosestOnSegments(p, p, c.a, c.b, &s, &t);
	const V3 on = Add(c.a, Mul(Sub(c.b, c.a), t));
	return Len(Sub(p, on)) < c.radius + margin;
}

bool Resolve(State& st, const State* before, const Body& b, const Blade* blades, int nBlades, const Capsule* caps, int n, const Params& p, uint64_t skip, Report* out) {
	Report rep;
	if (out) *out = rep;
	if (!caps || n <= 0 || !blades || nBlades <= 0 || !(b.mass > 0)) return false;
	const InvInertia ii(b.inertia);
	const double invMass = 1.0 / b.mass;
	uint64_t touching = 0;   // the bodies the blade was in, a bit each (the first 64)
	double sumJn[64] = {}, sumPush[64] = {}, sumJt[64] = {}, sumSlid[64] = {}, inSpeed[64] = {};   // what each body took this step (Report::step*, met)
	double best = -1.0;
	{   // the bodies far from the whole weapon are left out at once: a ball round its pieces against each body's axis (the pushes below move it by maxPush a round at most)
		V3 lo = PointAt(st, b, blades[0].butt), hi = lo;
		V3 pts[64];   // (a weapon's steel and its head's parts: hold.h kHeadParts)
		int np = 0;
		double thick = 0;
		for (int j = 0; j < nBlades && np < 64; ++j) {
			pts[np++] = PointAt(st, b, blades[j].butt);
			if (np < 64) pts[np++] = PointAt(st, b, blades[j].tip);
			thick = std::max(thick, blades[j].radius);
		}
		for (int j = 0; j < np; ++j) {
			lo = { std::min(lo.x, pts[j].x), std::min(lo.y, pts[j].y), std::min(lo.z, pts[j].z) };
			hi = { std::max(hi.x, pts[j].x), std::max(hi.y, pts[j].y), std::max(hi.z, pts[j].z) };
		}
		const V3 mid = Mul(Add(lo, hi), 0.5);
		const double ball = 0.5 * Len(Sub(hi, lo)) + thick + p.maxPush * std::max(1, p.iterations);
		for (int i = 0; i < n && i < 64; ++i) {
			double s, t;
			ClosestOnSegments(mid, mid, caps[i].a, caps[i].b, &s, &t);
			if (Len(Sub(mid, Add(caps[i].a, Mul(Sub(caps[i].b, caps[i].a), t)))) > ball + caps[i].radius) skip |= 1ull << i;
		}
	}
	if (before) Sweep(st, *before, b, blades, nBlades, caps, n, p, skip);   // (a fast blade across a thin limb: back where it first met it)
	for (int round = 0; round < std::max(1, p.iterations); ++round) {
		bool any = false;
		for (int k = 0; k < nBlades * n; ++k) {
			const int i = k % n;
			const Blade& blade = blades[k / n];
			if (i < 64 && (skip >> i & 1u)) continue;
			const Capsule& c = caps[i];
			if (!Meets(blade, c)) continue;   // (a piece for one body only, or for all but one: the others pass it)
			const V3 a = PointAt(st, b, blade.butt), z = PointAt(st, b, blade.tip);
			double s, t;
			ClosestOnSegments(a, z, c.a, c.b, &s, &t);
			const V3 onBlade = Add(a, Mul(Sub(z, a), s));
			const V3 onBody  = Add(c.a, Mul(Sub(c.b, c.a), t));
			V3 off = Sub(onBlade, onBody);
			double dist = Len(off);
			const double reach = blade.radius + c.radius, depth = reach - dist;
			if (depth <= 0) continue;
			const bool deep = depth > p.deepShare * c.radius + blade.radius;
			if (deep && !c.firm) continue;   // deep inside already: left there (contact.h Params::deepShare) - but never in the other weapon of a pair (Capsule::firm)
			if (dist < 1e-6) {   // on the axis: out the way the weapon's centre lies, across the axis
				const V3 ax = Sub(c.b, c.a);
				V3 side = Sub(st.com, onBody);
				const double la = Len(ax);
				if (la > 1e-9) side = Sub(side, Mul(ax, Dot(side, ax) / (la * la)));
				off = Len(side) > 1e-9 ? side : V3 { 1, 0, 0 };
				dist = Len(off);
			}
			const V3 nrm = Mul(off, 1.0 / dist);
			const V3 point = Sub(onBlade, Mul(nrm, blade.radius));   // where the blade's surface is in the body
			const V3 r = Sub(point, st.com);
			// the position: back on the surface
			const double push = std::min(depth, deep ? std::min(p.maxPush, kFirmEase) : p.maxPush);   // (deep in a firm one: eased out)
			const double give = Give(st, ii, invMass, r, nrm);
			if (give > 1e-12) Shift(st, ii, invMass, r, nrm, push / give);
			if (i < 64 && give > 1e-12) sumPush[i] += push / give;
			const V3 onLocal = Add(blade.butt, Mul(Sub(blade.tip, blade.butt), s));
			// the slide along the surface in this step - the way that point of the blade went since `before`, across the normal - held back by friction up to the
			// push times the friction (Coulomb's law on the step's displacements: the muscles set the velocity afresh every step, so a friction on the velocity alone
			// would not hold the weapon back at all)
			double slid = 0;
			if (before && p.friction > 0) {
				const V3 moved = Sub(PointAt(st, b, onLocal), PointAt(*before, b, onLocal));
				const V3 across = Sub(moved, Mul(nrm, Dot(moved, nrm)));
				const double la = Len(across);
				if (la > 1e-9) {
					const V3 dir = Mul(across, 1.0 / la);
					const double hold = std::min(la, p.friction * push);
					const double gt = Give(st, ii, invMass, Sub(PointAt(st, b, onLocal), st.com), dir);
					if (gt > 1e-12) Shift(st, ii, invMass, Sub(PointAt(st, b, onLocal), st.com), dir, -hold / gt);
					slid = la - hold;
				}
			}
			// the velocity there (the same point of the blade, where the push has put it), against the body's own
			const V3 r2 = Sub(Sub(PointAt(st, b, onLocal), Mul(nrm, blade.radius)), st.com);
			const V3 vrel = Sub(Add(st.vel, Cross(st.angVel, r2)), c.vel);
			const double vn = Dot(vrel, nrm);
			double jn = 0, slide = 0, jtTaken = 0;
			if (vn < 0 && give > 1e-12) {
				jn = -(1.0 + std::max(0.0, p.restitution)) * vn / give;
				Kick(st, ii, invMass, r2, Mul(nrm, jn));
			}
			{   // along the surface: friction up to the push it took (Coulomb), none when nothing was pushed
				const V3 v2 = Sub(Add(st.vel, Cross(st.angVel, r2)), c.vel);
				const V3 vt = Sub(v2, Mul(nrm, Dot(v2, nrm)));
				const double lt = Len(vt);
				if (lt > 1e-9 && jn > 0) {
					const V3 dir = Mul(vt, 1.0 / lt);
					const double gt = Give(st, ii, invMass, r2, dir);
					const double jt = gt > 1e-12 ? std::min(lt / gt, std::max(0.0, p.friction) * jn) : 0.0;
					Kick(st, ii, invMass, r2, Mul(dir, -jt));
					jtTaken = jt;
					slide = std::max(0.0, lt - jt * gt);
				} else {
					slide = lt;
				}
			}
			any = true;
			if (i < 64) {
				touching |= 1ull << i;
				sumJn[i] += jn;
				sumJt[i] += jtTaken;
				sumSlid[i] += slid;
				if (round == 0) inSpeed[i] = std::max(inSpeed[i], -vn);
			}
			// the strongest contact of the step: by what was taken out of the blade's motion, else by depth (the first round's numbers: what the blade did;
			// the later rounds only settle it)
			const double score = jn > 0 ? jn : depth * 1e-3;
			if (round == 0 && score > best) {
				best         = score;
				rep.id       = c.id;
				rep.body     = i;
				rep.depth    = depth;
				rep.approach = std::max(0.0, -vn);
				rep.slide    = slide;
				rep.slid     = slid;
				rep.impulse  = jn;
				rep.blade    = k / n;
				rep.along    = s;
				rep.normal   = nrm;
				rep.point    = point;
			}
		}
		if (!any) break;
	}
	for (uint64_t m = touching; m; m &= m - 1) ++rep.bodies;
	if (rep.bodies > 0)   // the step's totals against the monster met hardest - all its capsules
		for (int i = 0; i < n && i < 64; ++i)
			if ((touching >> i & 1u) && caps[i].id == rep.id) {
				rep.stepImpulse += sumJn[i];
				rep.stepPush += sumPush[i];
				rep.stepFriction += sumJt[i];
				rep.stepSlid += sumSlid[i];
			}
	for (uint64_t m = touching; m; m &= m - 1) {   // each monster's own totals (Report::met): its capsules summed, the hardest met first
		int i = 0;
		while (!(m >> i & 1u)) ++i;
		Report::Met* e = nullptr;
		for (int k = 0; k < rep.nMet; ++k)
			if (rep.met[k].id == caps[i].id) e = &rep.met[k];
		if (!e) {
			if (rep.nMet >= Report::kMet) continue;
			e = &rep.met[rep.nMet++];
			e->id = caps[i].id;
		}
		if (e->body < 0 || sumJn[i] > sumJn[e->body]) e->body = i;
		(caps[i].held ? e->onHeld : e->onBody) = true;
		e->impulse += sumJn[i];
		e->push += sumPush[i];
		e->friction += sumJt[i];
		e->slid += sumSlid[i];
		e->approach = std::max(e->approach, inSpeed[i]);
	}
	for (int k = 1; k < rep.nMet; ++k)   // (the hardest first)
		for (int j = k; j > 0 && rep.met[j].impulse > rep.met[j - 1].impulse; --j) std::swap(rep.met[j], rep.met[j - 1]);
	if (out) *out = rep;
	return rep.bodies > 0;
}

}   // namespace heft::contact
