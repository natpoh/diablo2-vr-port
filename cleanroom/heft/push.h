#pragma once
// Push: a push on the held body at one of its points - what the contact (contact.h) and the stab (stab.h) put the weapon back with. A push at a point r from the
// centre of mass along n moves a body of mass m and inertia I by n/m and turns it by I^-1 (r x n), per unit of push. Our own, written for D2R VR.

#include <cmath>

#include "heft.h"

namespace heft::push {

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
inline double Give(const State& st, const InvInertia& ii, double invMass, V3 r, V3 d) {
	const V3 rd = Cross(r, d);
	return invMass + Dot(rd, ii.Apply(st.rot, rd));
}

// A push of `amount` along d at r: the body moved and turned by it (positions), or its velocities changed by it (impulses).
inline void Shift(State& st, const InvInertia& ii, double invMass, V3 r, V3 d, double amount) {
	st.com = Add(st.com, Mul(d, amount * invMass));
	const V3 turn = ii.Apply(st.rot, Mul(Cross(r, d), amount));
	const double a = Len(turn);
	if (a > 1e-12) st.rot = QNorm(QMul(QFromAxisAngle(turn, a), st.rot));
}
inline void Kick(State& st, const InvInertia& ii, double invMass, V3 r, V3 j) {
	st.vel    = Add(st.vel, Mul(j, invMass));
	st.angVel = Add(st.angVel, ii.Apply(st.rot, Cross(r, j)));
}

// Moves the body's point at r by `by` (room): as far as one push along it can (the point's give), for a body that is not a point mass the push moves it all the way only
// approximately - the callers run it a few rounds.
inline void MovePoint(State& st, const InvInertia& ii, double invMass, V3 r, V3 by) {
	const double l = Len(by);
	if (l < 1e-12) return;
	const V3 d = Mul(by, 1.0 / l);
	const double g = Give(st, ii, invMass, r, d);
	if (g > 1e-12) Shift(st, ii, invMass, r, d, l / g);
}
// Takes the body's velocity at r along unit d down to `keep` times what it was (0: none left) - an impulse there.
inline void DampAlong(State& st, const InvInertia& ii, double invMass, V3 r, V3 d, double keep) {
	const V3 v = Add(st.vel, Cross(st.angVel, r));
	const double vn = Dot(v, d);
	const double g = Give(st, ii, invMass, r, d);
	if (g > 1e-12) Kick(st, ii, invMass, r, Mul(d, -(1.0 - keep) * vn / g));
}

}   // namespace heft::push
