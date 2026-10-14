// bow_hook.h - the string hand's fingers as the game's own bow-draw animation holds them. Pure: no game, no skeleton; skeletons.cpp lays the
// pose on the hero's finger bones and cleanroom/tests/bow_hook_test.cpp checks the maths and the table.
//
// What was wrong: the hand that takes the string closed with Fist() - the free-hand fist's joint table (80/95/65 degrees, knuckle, middle,
// tip) at 85 % - so the hand looked like a fist. The game's own Amazon, drawing a bow, hooks her fingers on the string: the knuckles stay
// open, the first joints hardly bend, the last two joints of each finger curl, the thumb is turned in. That pose is in the game's own data
// (data/hd/character/player/amazon/animation, the animations bow_attack1 and bow_attack2: the right hand's finger tracks hold one
// pose from about 0.1 s to the release at about 0.23 s). It was read once, with the machine's own tools, off the hold at 0.18 s of
// bow_attack1 against the skeleton's bind pose; what is kept here is nineteen small rotations - the measured angle of each finger bone,
// bind pose to hold, in the bone's own frame - not the game's file.
//
// The thumb's base was first read wrongly (145 degrees from the bind pose: the thumb stuck out of the hand, twisted). Its track is the only one
// of the hand whose first quarter second is stored with the other quaternion layout (the component left out is x, not w - which of the four
// is left out follows from two bits of each sample), and the reading assumed the one layout for all. Read with the right layout the track is
// one smooth curve that ends exactly on the neutral pose, and the hold is 57 degrees from the bind pose. The fingers' tracks all use the one
// layout for the whole animation and were right.
//
// A rotation is a unit quaternion (x y z w) q with  animation = bind * q  (the bone's local rotation); as a matrix in the row-vector
// convention the game's bone matrices use (translation in the last row) the local matrix of a bone in the hook is  Rrow(q) * bindLocal,
// where Rrow(q) is the transpose of the usual column-vector rotation matrix. (That convention is the one the skeleton's own locals compose
// to the world bind pose with: checked against the Amazon's skeleton for every finger bone.)
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <string_view>

namespace bowhook {

enum Kind : int { kIndex = 0, kMiddle = 1, kRing = 2, kPinky = 3, kThumb = 4, kNone = -1 };
constexpr int kKinds = 5;
constexpr int kMaxJoints = 4;

struct Q { float x, y, z, w; };

// The finger a chain belongs to, from the name of the chain's first bone ("R_ind_finger_01_bind_jnt"): the Amazon's names. Anything else -1.
inline int KindOf(std::string_view name) {
    std::string lo(name);
    for (char& c : lo) c = (char)std::tolower((unsigned char)c);
    auto has = [&](const char* s) { return lo.find(s) != std::string::npos; };
    if (!has("finger")) return kNone;
    if (has("_ind_")) return kIndex;
    if (has("_mid_")) return kMiddle;
    if (has("_rng_")) return kRing;
    if (has("_pnk_")) return kPinky;
    if (has("_tmb_")) return kThumb;
    return kNone;
}

// How many joints the finger has in the table (a thumb three, the others four), 0 for an unknown kind.
inline int Joints(int kind) { return kind == kThumb ? 3 : kind >= kIndex && kind < kThumb ? 4 : 0; }

// The measured rotations, bind pose -> the hold of the draw, per finger and joint (the chain's bones in order: 01 the knuckle, 02, 03, 04 the tip).
inline const Q& Rel(int kind, int joint) {
    static const Q kTable[kKinds][kMaxJoints] = {
        /* index  */ {{0.000280f, 0.000013f, 0.000554f, 1.000000f}, {-0.026324f, -0.064857f, -0.020340f, 0.997340f}, {0.000000f, -0.000000f, -0.416328f, 0.909214f}, {0.000000f, -0.000003f, -0.433645f, 0.901084f}},
        /* middle */ {{0.000240f, 0.000342f, 0.000143f, 1.000000f}, {-0.145600f, -0.081442f, -0.073055f, 0.983275f}, {-0.040356f, 0.036868f, -0.476257f, 0.877605f}, {-0.000003f, -0.000001f, -0.325654f, 0.945489f}},
        /* ring   */ {{0.000597f, -0.000440f, -0.000117f, 1.000000f}, {-0.131210f, -0.191897f, -0.123248f, 0.964764f}, {0.001897f, -0.006055f, 0.037258f, 0.999285f}, {0.000003f, 0.000000f, -0.489977f, 0.871735f}},
        /* pinky  */ {{0.000001f, -0.053209f, -0.000001f, 0.998583f}, {-0.033499f, -0.112623f, -0.246887f, 0.961894f}, {0.000000f, -0.000002f, -0.220038f, 0.975491f}, {0.000003f, 0.000000f, -0.225281f, 0.974294f}},
        /* thumb  */ {{0.460482f, -0.118037f, -0.044029f, 0.878684f}, {-0.000001f, -0.005733f, -0.154930f, 0.987909f}, {0.000002f, -0.006511f, -0.154901f, 0.987909f}, {0.0f, 0.0f, 0.0f, 1.0f}},
    };
    static const Q kIdentity = {0.0f, 0.0f, 0.0f, 1.0f};
    if (kind < 0 || kind >= kKinds || joint < 0 || joint >= kMaxJoints) return kIdentity;
    return kTable[kind][joint];
}

inline Q Normalised(Q q) {
    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(l > 1e-9f) || !std::isfinite(l)) return {0.0f, 0.0f, 0.0f, 1.0f};
    return {q.x / l, q.y / l, q.z / l, q.w / l};
}

// The rotation as a 4x4 in the row-vector convention (rotation block the transpose of the column-vector matrix, no translation), 16 floats,
// the bone matrices' own layout.
inline void RowMatrix(Q q, float m[16]) {
    q = Normalised(q);
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    const float c[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
                           {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
                           {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}};
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k) m[r * 4 + k] = c[k][r];   // transposed
    m[15] = 1.0f;
}

// A 4x4 times a 4x4, row-vector convention: out = a * b (out may not be a or b).
inline void Mul4(const float a[16], const float b[16], float out[16]) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) out[r * 4 + c] = a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c] + a[r * 4 + 2] * b[8 + c] + a[r * 4 + 3] * b[12 + c];
}

// The rotation of a bone matrix as a quaternion, its axes' lengths taken out first (a bone may carry a scale).
inline Q QuatOf(const float m[16]) {
    float r[3][3];
    for (int i = 0; i < 3; ++i) {
        const float l = std::sqrt(m[i * 4] * m[i * 4] + m[i * 4 + 1] * m[i * 4 + 1] + m[i * 4 + 2] * m[i * 4 + 2]);
        const float k = l > 1e-9f ? 1.0f / l : 0.0f;
        for (int j = 0; j < 3; ++j) r[i][j] = m[i * 4 + j] * k;
    }
    // the column-vector matrix is the transpose of the row layout: c[a][b] = r[b][a]
    const float c00 = r[0][0], c11 = r[1][1], c22 = r[2][2];
    Q q;
    const float tr = c00 + c11 + c22;
    if (tr > 0.0f) {
        const float s = std::sqrt(tr + 1.0f) * 2.0f;
        q = {(r[1][2] - r[2][1]) / s, (r[2][0] - r[0][2]) / s, (r[0][1] - r[1][0]) / s, 0.25f * s};
    } else if (c00 > c11 && c00 > c22) {
        const float s = std::sqrt(1.0f + c00 - c11 - c22) * 2.0f;
        q = {0.25f * s, (r[1][0] + r[0][1]) / s, (r[2][0] + r[0][2]) / s, (r[1][2] - r[2][1]) / s};
    } else if (c11 > c22) {
        const float s = std::sqrt(1.0f + c11 - c00 - c22) * 2.0f;
        q = {(r[1][0] + r[0][1]) / s, 0.25f * s, (r[2][1] + r[1][2]) / s, (r[2][0] - r[0][2]) / s};
    } else {
        const float s = std::sqrt(1.0f + c22 - c00 - c11) * 2.0f;
        q = {(r[2][0] + r[0][2]) / s, (r[2][1] + r[1][2]) / s, 0.25f * s, (r[0][1] - r[1][0]) / s};
    }
    return Normalised(q);
}

inline Q Slerp(Q a, Q b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0.0f) { b = {-b.x, -b.y, -b.z, -b.w}; d = -d; }
    if (d > 0.9995f) return Normalised({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
    const float th = std::acos(std::min(d, 1.0f)), s = std::sin(th);
    const float ka = std::sin((1.0f - t) * th) / s, kb = std::sin(t * th) / s;
    return Normalised({a.x * ka + b.x * kb, a.y * ka + b.y * kb, a.z * ka + b.z * kb, a.w * ka + b.w * kb});
}

// Between two poses of one bone's world matrix: the position and the axes' lengths in a straight line, the rotation by slerp. t 0 = a, 1 = b.
inline void BlendBone(const float a[16], const float b[16], float t, float out[16]) {
    t = std::isfinite(t) ? std::clamp(t, 0.0f, 1.0f) : 0.0f;
    const Q q = Slerp(QuatOf(a), QuatOf(b), t);
    float r[16];
    RowMatrix(q, r);
    for (int i = 0; i < 3; ++i) {
        const float la = std::sqrt(a[i * 4] * a[i * 4] + a[i * 4 + 1] * a[i * 4 + 1] + a[i * 4 + 2] * a[i * 4 + 2]);
        const float lb = std::sqrt(b[i * 4] * b[i * 4] + b[i * 4 + 1] * b[i * 4 + 1] + b[i * 4 + 2] * b[i * 4 + 2]);
        const float l = la + (lb - la) * t;
        for (int j = 0; j < 3; ++j) out[i * 4 + j] = r[i * 4 + j] * l;
        out[i * 4 + 3] = 0.0f;
    }
    for (int j = 0; j < 3; ++j) out[12 + j] = a[12 + j] + (b[12 + j] - a[12 + j]) * t;
    out[15] = 1.0f;
}

// ---- live tuning ---------------------------------------------------------------------------------------------------------------------------

// The pose as the sliders ask for it. Both are for looking: the measured pose is the default (curl 1, thumb 0).
struct Tune {
    float curl = 1.0f;       // [bow] hook_curl: how far the four fingers' joints bend against the measured pose (1 = as measured, 0 = the open hand)
    float thumbDeg = 0.0f;   // [bow] hook_thumb_deg: the thumb turned in toward the palm (+) or out (-) beyond the measured pose, degrees
};

constexpr float kMaxCurl = 1.5f;
constexpr float kMaxThumbDeg = 60.0f;

// a * b: the rotation b first, then a, as the bone matrices compose (x y z w, Hamilton).
inline Q QMul(Q a, Q b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

// The turn by `deg` about the axis (x y z) (any length; none = no turn).
inline Q AxisAngle(float x, float y, float z, float deg) {
    const float l = std::sqrt(x * x + y * y + z * z);
    if (!(l > 1e-9f) || !std::isfinite(l) || !std::isfinite(deg)) return {0.0f, 0.0f, 0.0f, 1.0f};
    const float h = deg * 0.0087266463f, s = std::sin(h) / l;   // half the angle, in radians
    return {x * s, y * s, z * s, std::cos(h)};
}

// The same turn through k times the angle (k 0 = none, 1 = q, 2 = twice as far; the short way round for q).
inline Q Scaled(Q q, float k) {
    q = Normalised(q);
    if (q.w < 0.0f) q = {-q.x, -q.y, -q.z, -q.w};
    const float s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
    if (!(s > 1e-9f) || !std::isfinite(k)) return {0.0f, 0.0f, 0.0f, 1.0f};
    const float deg = 2.0f * std::atan2(s, q.w) * 57.29577951f;
    return AxisAngle(q.x, q.y, q.z, deg * k);
}

// The rotation of one finger bone as tuned: the four fingers' bend scaled by `curl`; the thumb's base turned about its own y axis (the one that
// carries its tip toward the palm and the fingers - measured on the Amazon's thumb: 20 degrees about it moves the tip 6-7 hundredths of a hand
// length), - toward the palm for + degrees. The other joints of the thumb stay as measured.
inline Q Tuned(int kind, int joint, const Tune& t) {
    Q q = Rel(kind, joint);
    if (kind == kThumb) {
        if (joint == 0 && std::isfinite(t.thumbDeg) && t.thumbDeg != 0.0f)
            q = QMul(q, AxisAngle(0.0f, 1.0f, 0.0f, -std::clamp(t.thumbDeg, -kMaxThumbDeg, kMaxThumbDeg)));
        return q;
    }
    if (kind < kIndex || kind > kPinky) return q;
    if (!std::isfinite(t.curl) || t.curl == 1.0f) return q;
    return Scaled(q, std::clamp(t.curl, 0.0f, kMaxCurl));
}

// ---- where the string is held -----------------------------------------------------------------------------------------------------------------

// The string lies in the first joints of the hooked fingers: the bend of index, middle and ring, the third bone of each chain ("_03", the one
// the measured pose bends by 25-55 degrees). Without this the string was drawn from the controller's grip point - the middle of the palm,
// a hand's length (about 12 cm) behind them, so that it ran past the back of the hand and nothing hooked it.
constexpr int kAnchorJoint = 2;
inline bool InAnchor(int kind) { return kind == kIndex || kind == kMiddle || kind == kRing; }

struct Vec { float x = 0.0f, y = 0.0f, z = 0.0f; };
inline Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec operator*(Vec a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec Cross(Vec a, Vec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Length(Vec a) { return std::sqrt(Dot(a, a)); }
inline bool Finite(Vec a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

// A hand's own axes, from where its bones are - the same way for either hand, so that the sliders mean the same on both (the mirror rule):
// along = the way the fingers point (wrist -> middle knuckle), across = toward the thumb side (little-finger knuckle -> index knuckle, made square
// to `along`), palm = out of the palm, the way the fingers curl (the right hand's palm is -(along x across), the left's +; Fist() has the same).
struct Frame {
    Vec along, across, palm;
    bool ok = false;
};

inline Frame HandFrame(Vec wrist, Vec middle, Vec index, Vec pinky, int side) {
    Frame f;
    if (!Finite(wrist) || !Finite(middle) || !Finite(index) || !Finite(pinky)) return f;
    Vec a = middle - wrist;
    const Vec x0 = index - pinky;
    const float la = Length(a), lx = Length(x0);
    if (!(la > 1e-6f) || !(lx > 1e-6f)) return f;
    a = a * (1.0f / la);
    Vec c = x0 - a * Dot(x0, a);
    const float lc = Length(c);
    if (!(lc > 1e-4f * lx)) return f;   // the knuckles in a line with the wrist: no across
    c = c * (1.0f / lc);
    Vec p = Cross(a, c);
    if (side == 0) p = p * -1.0f;
    f.along = a;
    f.across = c;
    f.palm = p;   // (unit already: along and across are square unit vectors)
    f.ok = true;
    return f;
}

constexpr float kMaxAnchorCm = 15.0f;
inline float ClampCm(float v) { return std::isfinite(v) ? std::clamp(v, -kMaxAnchorCm, kMaxAnchorCm) : 0.0f; }

// The string's place: the middle of the chosen joints of the hooked pose, moved by the sliders' centimetres along the hand's own axes
// (`upm` model units to the metre). false when nothing can be said (no joints, a hand with no frame, a number that is not one).
inline bool AnchorPoint(const Vec* joints, int count, const Frame& f, float alongCm, float acrossCm, float palmCm, float upm, Vec* out) {
    if (!joints || count < 1 || !f.ok || !std::isfinite(upm) || !(upm > 0.0f)) return false;
    Vec sum;
    for (int i = 0; i < count; ++i) {
        if (!Finite(joints[i])) return false;
        sum = sum + joints[i];
    }
    const Vec mid = sum * (1.0f / (float)count);
    const Vec move = f.along * ClampCm(alongCm) + f.across * ClampCm(acrossCm) + f.palm * ClampCm(palmCm);
    *out = mid + move * (0.01f * upm);
    return Finite(*out);
}

// From `a` (0) to `b` (1).
inline Vec Lerp(Vec a, Vec b, float t) {
    t = std::isfinite(t) ? std::clamp(t, 0.0f, 1.0f) : 0.0f;
    return a + (b - a) * t;
}

}  // namespace bowhook
