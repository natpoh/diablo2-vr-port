// Heft: see heft.h.
//
// Each muscle is solved implicitly (backward Euler) for the impulse it gives in
// one step, then that impulse is held under its ceiling: a stiff spring stays
// stable at a game's frame rate, and the ceiling - not the spring - decides how
// a heavy weapon moves. The linear muscle pulls at the palm, not at the centre
// of mass, so a hand holding the end of a long weapon turns it as well; with
// two hands on, the two pulls make a lever.

#include "heft.h"

#include <algorithm>
#include <cmath>

namespace heft {

V3 Add(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
V3 Sub(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
V3 Mul(V3 a, double k) { return { a.x * k, a.y * k, a.z * k }; }
double Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
double Len(V3 a) { return std::sqrt(Dot(a, a)); }

Q QMul(Q a, Q b) {
	return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
		     a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}
Q QConj(Q q) { return { -q.x, -q.y, -q.z, q.w }; }
Q QNorm(Q q) {
	const double n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	return n > 0 ? Q { q.x / n, q.y / n, q.z / n, q.w / n } : Q {};
}
V3 Rotate(Q q, V3 v) {
	const V3 u { q.x, q.y, q.z };
	const V3 t = Mul(Cross(u, v), 2.0);
	return Add(Add(v, Mul(t, q.w)), Cross(u, t));
}
Q QFromAxisAngle(V3 axis, double angle) {
	const double n = Len(axis);
	if (n < 1e-12) return {};
	const double s = std::sin(angle * 0.5) / n;
	return { axis.x * s, axis.y * s, axis.z * s, std::cos(angle * 0.5) };
}
V3 RotVec(Q q) {
	if (q.w < 0) q = { -q.x, -q.y, -q.z, -q.w };
	const V3 v { q.x, q.y, q.z };
	const double s = Len(v);
	if (s < 1e-12) return Mul(v, 2.0);
	return Mul(v, 2.0 * std::atan2(s, q.w) / s);
}
Q QSlerp(Q a, Q b, double t) {
	double d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
	if (d < 0) {
		b = { -b.x, -b.y, -b.z, -b.w };
		d = -d;
	}
	if (d > 0.9995) {
		return QNorm({ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t });
	}
	const double th = std::acos(d), s = std::sin(th);
	const double ka = std::sin((1 - t) * th) / s, kb = std::sin(t * th) / s;
	return { a.x * ka + b.x * kb, a.y * ka + b.y * kb, a.z * ka + b.z * kb, a.w * ka + b.w * kb };
}

namespace {

M3 Identity(double k = 1) {
	M3 r;
	r.m[0][0] = r.m[1][1] = r.m[2][2] = k;
	return r;
}
M3 MAdd(const M3& a, const M3& b) {
	M3 r;
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j) r.m[i][j] = a.m[i][j] + b.m[i][j];
	return r;
}
M3 MScale(const M3& a, double k) {
	M3 r;
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j) r.m[i][j] = a.m[i][j] * k;
	return r;
}
M3 MMul(const M3& a, const M3& b) {
	M3 r;
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			for (int k = 0; k < 3; ++k) r.m[i][j] += a.m[i][k] * b.m[k][j];
	return r;
}
M3 MT(const M3& a) {
	M3 r;
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j) r.m[i][j] = a.m[j][i];
	return r;
}
V3 MV(const M3& a, V3 v) {
	return { a.m[0][0] * v.x + a.m[0][1] * v.y + a.m[0][2] * v.z, a.m[1][0] * v.x + a.m[1][1] * v.y + a.m[1][2] * v.z,
		     a.m[2][0] * v.x + a.m[2][1] * v.y + a.m[2][2] * v.z };
}
M3 MInv(const M3& a) {
	const auto& m = a.m;
	const double c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1], c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2],
	             c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
	const double det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;
	M3 r;
	if (std::fabs(det) < 1e-300) return r;
	const double k = 1.0 / det;
	r.m[0][0] = c00 * k;
	r.m[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * k;
	r.m[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * k;
	r.m[1][0] = c01 * k;
	r.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * k;
	r.m[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * k;
	r.m[2][0] = c02 * k;
	r.m[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * k;
	r.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * k;
	return r;
}
M3 FromQ(Q q) {
	M3 r;
	const double xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z,
	             wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
	r.m[0][0] = 1 - 2 * (yy + zz);
	r.m[0][1] = 2 * (xy - wz);
	r.m[0][2] = 2 * (xz + wy);
	r.m[1][0] = 2 * (xy + wz);
	r.m[1][1] = 1 - 2 * (xx + zz);
	r.m[1][2] = 2 * (yz - wx);
	r.m[2][0] = 2 * (xz - wy);
	r.m[2][1] = 2 * (yz + wx);
	r.m[2][2] = 1 - 2 * (xx + yy);
	return r;
}
Q ToQ(const M3& r) {
	const auto& m = r.m;
	const double tr = m[0][0] + m[1][1] + m[2][2];
	Q q;
	if (tr > 0) {
		const double s = std::sqrt(tr + 1.0) * 2;
		q = { (m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25 * s };
	} else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
		const double s = std::sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2;
		q = { 0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s };
	} else if (m[1][1] > m[2][2]) {
		const double s = std::sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2;
		q = { (m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s };
	} else {
		const double s = std::sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2;
		q = { (m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s, (m[1][0] - m[0][1]) / s };
	}
	return QNorm(q);
}
// [r]: the matrix of r x .
M3 Skew(V3 r) {
	M3 s;
	s.m[0][1] = -r.z;
	s.m[0][2] = r.y;
	s.m[1][0] = r.z;
	s.m[1][2] = -r.x;
	s.m[2][0] = -r.y;
	s.m[2][1] = r.x;
	return s;
}
// m (|d|^2 E - d d^T): what a mass at offset d adds to an inertia.
M3 Offset(double m, V3 d) {
	M3 r = Identity(m * Dot(d, d));
	const double v[3] = { d.x, d.y, d.z };
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j) r.m[i][j] -= m * v[i] * v[j];
	return r;
}

V3 Lerp(V3 a, V3 b, double t) { return Add(a, Mul(Sub(b, a), t)); }
V3 ClampEach(V3 v, double m) { return { std::clamp(v.x, -m, m), std::clamp(v.y, -m, m), std::clamp(v.z, -m, m) }; }

}   // namespace

Body Build(const Piece* pieces, int n, const V3* hands, int nHands, double handMass) {
	struct Part {
		double m;
		V3     c;
		M3     i;   // about its own centre
	};
	Part parts[64];
	int  np = 0;
	for (int k = 0; k < n && np < 64; ++k) {
		const Piece& p = pieces[k];
		const V3     ax = Sub(p.b, p.a);
		const double len = Len(ax);
		const V3     u = len > 0 ? Mul(ax, 1.0 / len) : V3 { 1, 0, 0 };
		const double r2 = p.radius * p.radius;
		const double iAx = p.mass * r2 * 0.5, iPerp = p.mass * (3 * r2 + len * len) / 12.0;
		M3 i = Identity(iPerp);
		const double v[3] = { u.x, u.y, u.z };
		for (int a = 0; a < 3; ++a)
			for (int b = 0; b < 3; ++b) i.m[a][b] += (iAx - iPerp) * v[a] * v[b];
		parts[np++] = { p.mass, Mul(Add(p.a, p.b), 0.5), i };
	}
	for (int k = 0; k < nHands && np < 64; ++k) {   // a hand: a 5 cm ball
		parts[np++] = { handMass, hands[k], Identity(0.4 * handMass * 0.05 * 0.05) };
	}
	Body b;
	double m = 0, steel = 0;
	V3 c, cs;
	for (int k = 0; k < n && k < 64; ++k) {
		steel += parts[k].m;
		cs = Add(cs, Mul(parts[k].c, parts[k].m));
	}
	b.weight   = steel;
	b.weightAt = steel > 0 ? Mul(cs, 1.0 / steel) : V3 {};
	for (int k = 0; k < np; ++k) {
		m += parts[k].m;
		c = Add(c, Mul(parts[k].c, parts[k].m));
	}
	b.mass = m > 0 ? m : 1;
	b.com  = Mul(c, 1.0 / b.mass);
	M3 i;
	for (int k = 0; k < np; ++k) i = MAdd(i, MAdd(parts[k].i, Offset(parts[k].m, Sub(parts[k].c, b.com))));
	b.inertia = i;
	return b;
}

void Principal(const M3& inertia, V3* diag, Q* rot) {
	// Jacobi: a few sweeps are plenty for 3x3.
	M3 a = inertia, v = Identity();
	for (int sweep = 0; sweep < 32; ++sweep) {
		const double off = a.m[0][1] * a.m[0][1] + a.m[0][2] * a.m[0][2] + a.m[1][2] * a.m[1][2];
		if (off < 1e-24) break;
		for (int p = 0; p < 2; ++p)
			for (int q = p + 1; q < 3; ++q) {
				if (std::fabs(a.m[p][q]) < 1e-30) continue;
				const double th = 0.5 * std::atan2(2 * a.m[p][q], a.m[q][q] - a.m[p][p]);
				const double c = std::cos(th), s = std::sin(th);
				M3 j = Identity();
				j.m[p][p] = c;
				j.m[q][q] = c;
				j.m[p][q] = s;
				j.m[q][p] = -s;
				a = MMul(MT(j), MMul(a, j));
				v = MMul(v, j);
			}
	}
	// a right-handed frame
	const V3 c0 { v.m[0][0], v.m[1][0], v.m[2][0] }, c1 { v.m[0][1], v.m[1][1], v.m[2][1] }, c2 { v.m[0][2], v.m[1][2], v.m[2][2] };
	if (Dot(Cross(c0, c1), c2) < 0) {
		for (int r = 0; r < 3; ++r) v.m[r][2] = -v.m[r][2];
	}
	*diag = { a.m[0][0], a.m[1][1], a.m[2][2] };
	*rot  = ToQ(v);
}

Pose GripPose(const State& s, const Body& b) { return { Sub(s.com, Rotate(s.rot, b.com)), s.rot }; }
V3 PointAt(const State& s, const Body& b, const V3& local) { return Add(s.com, Rotate(s.rot, Sub(local, b.com))); }
V3 PointVelocity(const State& s, const Body& b, const V3& local) {
	return Add(s.vel, Cross(s.angVel, Rotate(s.rot, Sub(local, b.com))));
}
Pose HandPose(const State& s, const Body& b, const Grip& g) { return { PointAt(s, b, g.at), QMul(s.rot, g.rot) }; }

void Place(State& s, const Body& b, const Grip& g, const Pose& controller) {
	s.rot    = QNorm(QMul(controller.rot, QConj(g.rot)));
	s.com    = Add(controller.pos, Rotate(s.rot, Sub(b.com, g.at)));
	s.vel    = {};
	s.angVel = {};
	s.placed = true;
}

bool Step(State& s, const Body& b, const Grip* grips, const Controller* from, const Controller* to, int n, double dt,
          const World& w) {
	int main = -1;
	for (int i = 0; i < n; ++i) {
		if (grips[i].on) {
			main = i;
			break;
		}
	}
	if (!s.placed) {
		if (main < 0) return false;
		Place(s, b, grips[main], to[main].pose);
		return true;
	}
	if (dt <= 0) return false;
	const int    steps = std::max(1, (int)std::ceil(dt / std::max(1e-5, w.substep) - 1e-9));
	const double h     = dt / steps;
	const double invM  = 1.0 / b.mass;
	for (int k = 1; k <= steps; ++k) {
		const double t = w.glide ? (double)k / steps : 1.0;
		const M3 r    = FromQ(s.rot);
		const M3 iw   = MMul(r, MMul(b.inertia, MT(r)));
		const M3 iInv = MInv(iw);
		{   // gravity, on the steel at its own centre
			const V3 jg = Mul(w.gravity, h * b.weight);
			s.vel    = Add(s.vel, Mul(jg, invM));
			s.angVel = Add(s.angVel, MV(iInv, Cross(Rotate(s.rot, Sub(b.weightAt, b.com)), jg)));
		}
		// Every muscle at once: each is a soft constraint - an implicit spring and damper -
		// solved for the impulse it gives, the impulses summed over a few rounds with each
		// muscle's total held under its ceiling (on each axis of its controller), so two
		// hands find their shared answer instead of the second fighting the first.
		struct Row {
			M3 rt, rtT;            // the controller's rotation and its inverse
			V3 rr;                 // centre of mass to palm, world
			M3 aLin;               // controller frame
			V3 bLin;
			double gLin, maxLin;
			V3 jAcc;               // controller frame
			M3 jac, jacT, aAng;    // controller frame
			V3 bAng;
			double gAng, maxAng;
			V3 pAcc;
			bool on;
		} rows[8];
		const int nr = std::min(n, 8);
		for (int i = 0; i < nr; ++i) {
			Row& ro = rows[i];
			const Grip& g = grips[i];
			ro.on = g.on;
			if (!g.on) continue;
			const Muscle& mu = g.muscle;
			const V3 tPos = Lerp(from[i].pose.pos, to[i].pose.pos, t);
			const Q  tRot = QSlerp(from[i].pose.rot, to[i].pose.rot, t);
			const V3 tVel = Lerp(from[i].vel, to[i].vel, t), tAng = Lerp(from[i].angVel, to[i].angVel, t);
			ro.rt  = FromQ(tRot);
			ro.rtT = MT(ro.rt);

			// Linear: the palm toward the controller.
			ro.rr = Rotate(s.rot, Sub(g.at, b.com));
			const M3 sk   = Skew(ro.rr);
			const M3 kMat = MAdd(Identity(invM), MScale(MMul(sk, MMul(iInv, sk)), -1.0));
			ro.gLin   = h * h * mu.spring + h * mu.damper;
			ro.aLin   = MInv(MAdd(Identity(), MScale(MMul(ro.rtT, MMul(kMat, ro.rt)), ro.gLin)));
			ro.bLin   = MV(ro.rtT, Add(Mul(Sub(tPos, Add(s.com, ro.rr)), h * mu.spring), Mul(tVel, h * mu.damper)));
			ro.maxLin = mu.maxForce * h;
			ro.jAcc   = {};

			// Angular: the hand's turn toward the controller's. The spring and the damper
			// work on the vector part of the turn still to make (sin of half the angle),
			// not on the angle - the shape measured off the reference (tools/heft_bench_unity,
			// HeftBench.Identify): for small errors k/4 and c/4 per radian; the torque
			// grows as (k/4) sin(angle), so it weakens past 90 degrees and is nothing at
			// 180; the ceiling holds the pull on that vector, which turns into at most
			// max/2 cos(half the angle) of torque. A weak wrist barely brings back a
			// weapon turned far round - part of what makes a heavy one feel heavy.
			Q rel = QMul(QConj(tRot), QMul(s.rot, g.rot));   // the hand in the controller's frame
			if (rel.w < 0) rel = { -rel.x, -rel.y, -rel.z, -rel.w };
			const V3 sv { rel.x, rel.y, rel.z };
			// d(vector part)/dt = 1/2 (w E - [s]) omega, omega in the controller's frame
			ro.jac  = MScale(MAdd(Identity(rel.w), MScale(Skew(sv), -1.0)), 0.5);
			ro.jacT = MT(ro.jac);
			const M3 iInvL = MMul(ro.rtT, MMul(iInv, ro.rt));
			ro.gAng   = h * h * mu.torqueSpring + h * mu.torqueDamper;
			ro.aAng   = MInv(MAdd(Identity(), MScale(MMul(ro.jac, MMul(iInvL, ro.jacT)), ro.gAng)));
			ro.bAng   = Add(Mul(sv, -h * mu.torqueSpring), Mul(MV(ro.jac, MV(ro.rtT, tAng)), h * mu.torqueDamper));
			ro.maxAng = mu.maxTorque * h;
			ro.pAcc   = {};
		}
		for (int it = 0; it < std::max(1, w.iterations); ++it) {
			for (int i = 0; i < nr; ++i) {
				Row& ro = rows[i];
				if (!ro.on) continue;
				const V3 va = MV(ro.rtT, Add(s.vel, Cross(s.angVel, ro.rr)));
				const V3 jNew = ClampEach(Add(ro.jAcc, MV(ro.aLin, Sub(Sub(ro.bLin, Mul(va, ro.gLin)), ro.jAcc))), ro.maxLin);
				const V3 dj = MV(ro.rt, Sub(jNew, ro.jAcc));
				ro.jAcc = jNew;
				s.vel    = Add(s.vel, Mul(dj, invM));
				s.angVel = Add(s.angVel, MV(iInv, Cross(ro.rr, dj)));

				const V3 vs = MV(ro.jac, MV(ro.rtT, s.angVel));
				const V3 pNew = ClampEach(Add(ro.pAcc, MV(ro.aAng, Sub(Sub(ro.bAng, Mul(vs, ro.gAng)), ro.pAcc))), ro.maxAng);
				const V3 dp = Sub(pNew, ro.pAcc);
				ro.pAcc = pNew;
				s.angVel = Add(s.angVel, MV(iInv, MV(ro.rt, MV(ro.jacT, dp))));
			}
		}
		const double om = Len(s.angVel);
		if (om > b.maxAngularSpeed) s.angVel = Mul(s.angVel, b.maxAngularSpeed / om);
		s.com = Add(s.com, Mul(s.vel, h));
		const double a = Len(s.angVel) * h;
		if (a > 0) s.rot = QNorm(QMul(QFromAxisAngle(s.angVel, a), s.rot));
	}
	if (main >= 0 && Len(Sub(PointAt(s, b, grips[main].at), to[main].pose.pos)) > w.snapDistance) {
		Place(s, b, grips[main], to[main].pose);
		return true;
	}
	return false;
}

}   // namespace heft
