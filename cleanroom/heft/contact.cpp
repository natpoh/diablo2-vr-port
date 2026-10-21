// Contact: see contact.h.
//
// One body after the other, a few rounds: the blade's closest point to the body's axis, how deep the blade is in it, the push that puts it back on
// the surface - shared between moving the weapon and turning it as its mass and its inertia at that point say (the push at a point r from the centre
// of mass along n moves a body of mass m and inertia I by n/m and turns it by I^-1 (r x n), per unit of push) - then the same for its velocity there:
// the part into the body taken away (and a bounce, if any), the part along the surface held back by friction up to that push times the friction.

#include "contact.h"

#include <algorithm>
#include <cmath>

namespace heft::contact {

namespace {

// The body's inverse inertia applied to a world vector: into the grip frame, through the inverse of the body's inertia there, back out.
struct InvInertia {
	double m[3][3] = {};
	bool   ok = false;
	explicit InvInertia(const M3& a) {
		const double (&x)[3][3] = a.m;
		const double det = x[0][0] * (x[1][1] * x[2][2] - x[1][2] * x[2][1]) - x[0][1] * (x[1][0] * x[2][2] - x[1][2] * x[2][0]) + x[0][2] * (x[1][0] * x[2][1] - x[1][1] * x[2][0]);
		if (!(std::fabs(det) > 1e-18)) return;
		const double k = 1.0 / det;
		m[0][0] = (x[1][1] * x[2][2] - x[1][2] * x[2][1]) * k;
		m[0][1] = (x[0][2] * x[2][1] - x[0][1] * x[2][2]) * k;
		m[0][2] = (x[0][1] * x[1][2] - x[0][2] * x[1][1]) * k;
		m[1][0] = (x[1][2] * x[2][0] - x[1][0] * x[2][2]) * k;
		m[1][1] = (x[0][0] * x[2][2] - x[0][2] * x[2][0]) * k;
		m[1][2] = (x[0][2] * x[1][0] - x[0][0] * x[1][2]) * k;
		m[2][0] = (x[1][0] * x[2][1] - x[1][1] * x[2][0]) * k;
		m[2][1] = (x[0][1] * x[2][0] - x[0][0] * x[2][1]) * k;
		m[2][2] = (x[0][0] * x[1][1] - x[0][1] * x[1][0]) * k;
		ok = true;
	}
	V3 Apply(const Q& rot, V3 w) const {
		if (!ok) return {};
		const V3 l = Rotate(QConj(rot), w);
		const V3 r { m[0][0] * l.x + m[0][1] * l.y + m[0][2] * l.z, m[1][0] * l.x + m[1][1] * l.y + m[1][2] * l.z, m[2][0] * l.x + m[2][1] * l.y + m[2][2] * l.z };
		return Rotate(rot, r);
	}
};

// The weapon's give at r (from the centre of mass) along unit d: how much a unit push there moves that point along d.
double Give(const State& st, const InvInertia& ii, double invMass, V3 r, V3 d) {
	const V3 rd = Cross(r, d);
	return invMass + Dot(rd, ii.Apply(st.rot, rd));
}

// A push of `amount` along d at r: the body moved and turned by it (positions), or its velocities changed by it (impulses).
void Shift(State& st, const InvInertia& ii, double invMass, V3 r, V3 d, double amount) {
	st.com = Add(st.com, Mul(d, amount * invMass));
	const V3 turn = ii.Apply(st.rot, Mul(Cross(r, d), amount));
	const double a = Len(turn);
	if (a > 1e-12) st.rot = QNorm(QMul(QFromAxisAngle(turn, a), st.rot));
}
void Kick(State& st, const InvInertia& ii, double invMass, V3 r, V3 j) {
	st.vel    = Add(st.vel, Mul(j, invMass));
	st.angVel = Add(st.angVel, ii.Apply(st.rot, Cross(r, j)));
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
	double best = -1.0;
	{   // the bodies far from the whole weapon are left out at once: a ball round its pieces against each body's axis (the pushes below move it by maxPush a round at most)
		V3 lo = PointAt(st, b, blades[0].butt), hi = lo;
		V3 pts[8];
		int np = 0;
		double thick = 0;
		for (int j = 0; j < nBlades && np < 8; ++j) {
			pts[np++] = PointAt(st, b, blades[j].butt);
			if (np < 8) pts[np++] = PointAt(st, b, blades[j].tip);
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
	for (int round = 0; round < std::max(1, p.iterations); ++round) {
		bool any = false;
		for (int k = 0; k < nBlades * n; ++k) {
			const int i = k % n;
			const Blade& blade = blades[k / n];
			if (i < 64 && (skip >> i & 1u)) continue;
			const Capsule& c = caps[i];
			const V3 a = PointAt(st, b, blade.butt), z = PointAt(st, b, blade.tip);
			double s, t;
			ClosestOnSegments(a, z, c.a, c.b, &s, &t);
			const V3 onBlade = Add(a, Mul(Sub(z, a), s));
			const V3 onBody  = Add(c.a, Mul(Sub(c.b, c.a), t));
			V3 off = Sub(onBlade, onBody);
			double dist = Len(off);
			const double reach = blade.radius + c.radius, depth = reach - dist;
			if (depth <= 0) continue;
			if (depth > p.deepShare * c.radius + blade.radius) continue;   // deep inside already: left there (contact.h Params::deepShare)
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
			const double push = std::min(depth, p.maxPush);
			const double give = Give(st, ii, invMass, r, nrm);
			if (give > 1e-12) Shift(st, ii, invMass, r, nrm, push / give);
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
			double jn = 0, slide = 0;
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
					slide = std::max(0.0, lt - jt * gt);
				} else {
					slide = lt;
				}
			}
			any = true;
			if (i < 64) touching |= 1ull << i;
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
	if (out) *out = rep;
	return rep.bodies > 0;
}

}   // namespace heft::contact
