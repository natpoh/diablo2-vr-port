#pragma once
// The few matrix operations the camera layer needs, in the game's convention:
// row-major float[16], row vectors (p' = p * M), right-handed, the camera looks
// down -Z in view space, projections are infinite-far reverse-Z.

#include <cmath>
#include <cstring>

namespace d2rcam::m4 {

struct V3 {
	float x, y, z;
};

inline V3 operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline V3 operator*(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
inline float Length(V3 a) { return std::sqrt(Dot(a, a)); }

inline V3 Normalize(V3 a) {
	const float l = Length(a);
	return l > 1e-20f ? a * (1.0f / l) : V3 { 0, 0, 0 };
}

inline void Identity(float m[16]) {
	std::memset(m, 0, 16 * sizeof(float));
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// r = a * b (apply a first, then b).
inline void Mul(const float a[16], const float b[16], float r[16]) {
	float t[16];
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			t[i * 4 + j] = a[i * 4 + 0] * b[0 * 4 + j] + a[i * 4 + 1] * b[1 * 4 + j] + a[i * 4 + 2] * b[2 * 4 + j] + a[i * 4 + 3] * b[3 * 4 + j];
		}
	}
	std::memcpy(r, t, sizeof(t));
}

// General inverse (cofactors); false when singular.
inline bool Inverse(const float m[16], float out[16]) {
	float inv[16];
	inv[0]  = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
	inv[4]  = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
	inv[8]  = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
	inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
	inv[1]  = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
	inv[5]  = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
	inv[9]  = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
	inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
	inv[2]  = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
	inv[6]  = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
	inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
	inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
	inv[3]  = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
	inv[7]  = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
	inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
	inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
	const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
	if (!(std::fabs(det) > 1e-30f)) {
		return false;
	}
	const float s = 1.0f / det;
	for (int i = 0; i < 16; ++i) {
		out[i] = inv[i] * s;
	}
	return true;
}

// Same construction as the game's LookTo (RVA 0x122F3A0): z = -fwd, x = up x z, y = z x x.
inline void LookTo(V3 pos, V3 fwd, V3 up, float m[16]) {
	const V3 z = Normalize(fwd * -1.0f);
	const V3 x = Normalize(Cross(up, z));
	const V3 y = Cross(z, x);
	const float r[16] {
		x.x, y.x, z.x, 0,
		x.y, y.y, z.y, 0,
		x.z, y.z, z.z, 0,
		-Dot(x, pos), -Dot(y, pos), -Dot(z, pos), 1,
	};
	std::memcpy(m, r, sizeof(r));
}

// v + 2 * cross(q, cross(q, v) + w * v), as the game's 0xD7AC40.
inline V3 Rotate(const float q[4], V3 v) {
	const V3 qv { q[0], q[1], q[2] };
	const V3 u = Cross(qv, v) + v * q[3];
	return v + Cross(qv, u) * 2.0f;
}

// Quaternion of the rotation whose columns are the images of the local axes.
inline void QuatFromAxes(V3 ax, V3 ay, V3 az, float q[4]) {
	// R[row][col]: column 0 = ax, 1 = ay, 2 = az.
	const float r00 = ax.x, r01 = ay.x, r02 = az.x;
	const float r10 = ax.y, r11 = ay.y, r12 = az.y;
	const float r20 = ax.z, r21 = ay.z, r22 = az.z;
	const float t = r00 + r11 + r22;
	if (t > 0.0f) {
		const float s = std::sqrt(t + 1.0f) * 2.0f;
		q[3] = 0.25f * s;
		q[0] = (r21 - r12) / s;
		q[1] = (r02 - r20) / s;
		q[2] = (r10 - r01) / s;
	} else if (r00 > r11 && r00 > r22) {
		const float s = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
		q[3] = (r21 - r12) / s;
		q[0] = 0.25f * s;
		q[1] = (r01 + r10) / s;
		q[2] = (r02 + r20) / s;
	} else if (r11 > r22) {
		const float s = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
		q[3] = (r02 - r20) / s;
		q[0] = (r01 + r10) / s;
		q[1] = 0.25f * s;
		q[2] = (r12 + r21) / s;
	} else {
		const float s = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
		q[3] = (r10 - r01) / s;
		q[0] = (r02 + r20) / s;
		q[1] = (r12 + r21) / s;
		q[2] = 0.25f * s;
	}
	const float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
	for (int i = 0; i < 4; ++i) {
		q[i] /= l;
	}
}

// The game's perspective (0x122EE60): infinite far, reverse-Z, right-handed.
inline void PerspectiveRevZ(float fovYRad, float aspect, float zNear, float m[16]) {
	const float f = 1.0f / std::tan(fovYRad * 0.5f);
	std::memset(m, 0, 16 * sizeof(float));
	m[0]  = f / aspect;
	m[5]  = f;
	m[11] = -1.0f;
	m[14] = zNear;
}

// The game's orthographic (0x122EF30), reverse-Z.
inline void OrthoRevZ(float l, float r, float b, float t, float zFar, float zNear, float m[16]) {
	std::memset(m, 0, 16 * sizeof(float));
	m[0]  = 2.0f / (r - l);
	m[5]  = 2.0f / (t - b);
	m[10] = 1.0f / (zFar - zNear);
	m[12] = -(r + l) / (r - l);
	m[13] = -(t + b) / (t - b);
	m[14] = zFar / (zFar - zNear);
	m[15] = 1.0f;
}

inline bool Finite(const float* m, int n) {
	for (int i = 0; i < n; ++i) {
		if (!std::isfinite(m[i])) {
			return false;
		}
	}
	return true;
}

// A world ray through the given matrices, origin at the eye for a perspective.
inline bool RayThrough(const float invView[16], const float proj[16], float vpW, float vpH, float sx, float sy, float origin[3], float dir[3]) {
	if (!(vpW > 0.0f) || !(vpH > 0.0f)) {
		return false;
	}
	float invProj[16];
	if (!m4::Inverse(proj, invProj)) {
		return false;
	}
	const float nx = 2.0f * sx / vpW - 1.0f;
	const float ny = 1.0f - 2.0f * sy / vpH;
	// Reverse-Z: ndc z = 1 is the near plane, z = 0.5 lies further along the ray.
	auto unproject = [&](float z, float out[3]) {
		const float c[4] { nx, ny, z, 1.0f };
		float r[4];
		for (int j = 0; j < 4; ++j) {
			r[j] = c[0] * invProj[0 * 4 + j] + c[1] * invProj[1 * 4 + j] + c[2] * invProj[2 * 4 + j] + c[3] * invProj[3 * 4 + j];
		}
		const float w = r[3] != 0.0f ? 1.0f / r[3] : 0.0f;
		out[0] = r[0] * w;
		out[1] = r[1] * w;
		out[2] = r[2] * w;
	};
	float pNear[3], pFar[3];
	unproject(1.0f, pNear);
	unproject(0.5f, pFar);
	const bool perspective = proj[11] != 0.0f;
	const V3 o = perspective ? V3 { 0, 0, 0 } : V3 { pNear[0], pNear[1], pNear[2] };
	const V3 d = m4::Normalize({ pFar[0] - pNear[0], pFar[1] - pNear[1], pFar[2] - pNear[2] });
	// view space -> world (row vectors: point uses row 3, direction does not)
	const float* m = invView;
	origin[0] = o.x * m[0] + o.y * m[4] + o.z * m[8] + m[12];
	origin[1] = o.x * m[1] + o.y * m[5] + o.z * m[9] + m[13];
	origin[2] = o.x * m[2] + o.y * m[6] + o.z * m[10] + m[14];
	const V3 w = m4::Normalize({ d.x * m[0] + d.y * m[4] + d.z * m[8], d.x * m[1] + d.y * m[5] + d.z * m[9], d.x * m[2] + d.y * m[6] + d.z * m[10] });
	dir[0] = w.x;
	dir[1] = w.y;
	dir[2] = w.z;
	return m4::Finite(origin, 3) && m4::Finite(dir, 3) && m4::Length(w) > 0.5f;
}

}   // namespace d2rcam::m4
