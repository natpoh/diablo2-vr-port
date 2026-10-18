// grip_rig.h - a hand's rig and the grip library's rotations, in and out of the hook's matrices. Pure maths: no game, no skeleton, no library.
//
// The grip library (shared/d2r_vr_grip.h) knows a hand as a rig of finger bones in the hand's own PROPER frame (x toward the fingers, y across, z out of the palm; metres) and answers with
// each bone's rotation against its parent's frame. The hook has the hand's bones as 4x4 matrices of the rig it was given - any axes, a left hand a mirror of the right (determinant -1),
// a scale. This turns the one into the other and back:
//   - every bone's frame is made proper (a mirror bone's third axis turned over) before any rotation is taken between two frames, and turned back after: so a mirror bone and a proper one
//     meet in a proper rotation, which a quaternion can carry;
//   - the hand's frame is the hook's (grip_pose.h HandFrame: a right hand's frame is a mirror of a left one's) with its second axis turned over on the right, so it is proper too;
//   - a bone's offset from its parent is written in the parent's frame (an offset the pose never changes: bind pose), the rotations in the same.
// Row-vector convention, 3x3 as 9 floats, rows = the bone's axes (grip_pose.h M3).
#pragma once

#include <cmath>
#include <cstring>
#include <vector>

#include "d2r_vr_grip.h"
#include "grip_pose.h"

namespace griprig {

using grippose::M3;
using grippose::V;

inline M3 Identity() { return M3{}; }

// The hand's frame made proper: grip_pose.h HandFrame's rows (along the fingers, along the knuckles, out of the palm) are a left-handed set on a right hand (side 0); its second row
// turned over makes it right-handed. The library's y runs across the knuckles from the index on a right hand and from the little finger on a left one - it never needs to know which.
inline M3 ProperFrame(const M3& e, int side) {
    M3 p = e;
    if (side == 0)
        for (int c = 0; c < 3; ++c) p.m[3 + c] = -p.m[3 + c];
    return p;
}

// A bone orientation's coordinates in the hand's frame -> the same in the hand's proper frame (the second COLUMN turned over on a right hand: C Ep^T = C E^T D).
inline M3 CoordsInProper(const M3& coords, int side) {
    M3 p = coords;
    if (side == 0)
        for (int r = 0; r < 3; ++r) p.m[r * 3 + 1] = -p.m[r * 3 + 1];
    return p;
}

// A bone's frame made proper: the third row turned over when it is a mirror. mirror: -1 for a mirror, +1 otherwise.
inline M3 Proper(const M3& rows, int* mirror) {
    M3 o = rows;
    const bool m = grippose::Det(rows) < 0.0f;
    if (mirror) *mirror = m ? -1 : 1;
    if (m)
        for (int c = 0; c < 3; ++c) o.m[6 + c] = -o.m[6 + c];
    return o;
}

// The quaternion of the rotation whose columns are the three rows of `l` (the child's axes written in the parent's frame).
inline D2rgQ QuatOfRows(const M3& l) {
    const double m00 = l.m[0], m01 = l.m[3], m02 = l.m[6], m10 = l.m[1], m11 = l.m[4], m12 = l.m[7], m20 = l.m[2], m21 = l.m[5], m22 = l.m[8];
    const double tr = m00 + m11 + m22;
    D2rgQ q;
    if (tr > 0.0) {
        const double s = std::sqrt(tr + 1.0) * 2.0;
        q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25 * s};
    } else if (m00 > m11 && m00 > m22) {
        const double s = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
        q = {0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        const double s = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
        q = {(m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        const double s = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
        q = {(m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s};
    }
    const double n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (n > 1e-12) { q.x /= n; q.y /= n; q.z /= n; q.w /= n; }
    return q;
}

// The rows (the child's axes in the parent's frame) of a quaternion's rotation: the transpose of its matrix.
inline M3 RowsOfQuat(const D2rgQ& q) {
    const double n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    const double x = n > 1e-12 ? q.x / n : 0, y = n > 1e-12 ? q.y / n : 0, z = n > 1e-12 ? q.z / n : 0, w = n > 1e-12 ? q.w / n : 1;
    M3 r;   // column j of the rotation matrix is row j here
    r.m[0] = (float)(1 - 2 * (y * y + z * z)); r.m[1] = (float)(2 * (x * y + z * w));     r.m[2] = (float)(2 * (x * z - y * w));
    r.m[3] = (float)(2 * (x * y - z * w));     r.m[4] = (float)(1 - 2 * (x * x + z * z)); r.m[5] = (float)(2 * (y * z + x * w));
    r.m[6] = (float)(2 * (x * z + y * w));     r.m[7] = (float)(2 * (y * z - x * w));     r.m[8] = (float)(1 - 2 * (x * x + y * y));
    return r;
}

// One finger bone of the hand, in the hand's proper frame (units of the hook's space).
struct Bone {
    int parent = -1;      // index in the list (the parent comes first), or -1: the hand
    int finger = 0;       // 0 .. nFingers-1
    M3 open, closed;      // the bone's orientation rows (coordinates in the hand's proper frame), in the open pose and in the closed one
    V pos;                // the bone's origin from the middle of the palm, in the open pose, in the hand's proper frame
};

// The rig for the library. unitsPerMetre: the hook's units per metre. mirror: each bone's handedness (+1 / -1), to give back to Compose. False when the list cannot be a rig.
inline bool BuildRig(const std::vector<Bone>& bones, int nFingers, double unitsPerMetre, D2rgRig* rig, std::vector<int>* mirror) {
    const int n = (int)bones.size();
    if (n < 1 || n > D2RG_MAX_BONES || nFingers < 1 || nFingers > D2RG_MAX_FINGERS || !(unitsPerMetre > 1e-6) || !rig || !mirror) return false;
    std::memset(rig, 0, sizeof *rig);
    rig->size = sizeof *rig;
    rig->nFingers = nFingers;
    rig->nBones = n;
    std::vector<M3> po((size_t)n), pc((size_t)n);
    mirror->assign((size_t)n, 1);
    for (int i = 0; i < n; ++i) {
        const Bone& b = bones[(size_t)i];
        if (b.parent < -1 || b.parent >= i || b.finger < 0 || b.finger >= nFingers) return false;
        int m1 = 1, m2 = 1;
        po[(size_t)i] = Proper(b.open, &m1);
        pc[(size_t)i] = Proper(b.closed, &m2);
        if (m1 != m2 || !grippose::Finite(b.open) || !grippose::Finite(b.closed)) return false;   // a bone is a mirror, or not, in both poses
        (*mirror)[(size_t)i] = m1;
    }
    for (int i = 0; i < n; ++i) {
        const Bone& b = bones[(size_t)i];
        const M3 parOpen = b.parent < 0 ? Identity() : po[(size_t)b.parent], parClosed = b.parent < 0 ? Identity() : pc[(size_t)b.parent];
        const V d = b.parent < 0 ? b.pos : grippose::Sub(b.pos, bones[(size_t)b.parent].pos);
        D2rgBone& o = rig->bones[i];
        o.parent = b.parent;
        o.finger = b.finger;
        o.offset = {grippose::Dot(d, parOpen.Row(0)) / unitsPerMetre, grippose::Dot(d, parOpen.Row(1)) / unitsPerMetre, grippose::Dot(d, parOpen.Row(2)) / unitsPerMetre};
        o.open = QuatOfRows(grippose::Mul(po[(size_t)i], grippose::Transpose(parOpen)));
        o.closed = QuatOfRows(grippose::Mul(pc[(size_t)i], grippose::Transpose(parClosed)));
    }
    for (int f = 0; f < nFingers; ++f) {
        int last = -1;
        for (int i = 0; i < n; ++i)
            if (bones[(size_t)i].finger == f) last = i;
        rig->tip[f] = {0.02, 0.0, 0.0};
        if (last >= 0 && bones[(size_t)last].parent >= 0 && bones[(size_t)bones[(size_t)last].parent].finger == f) {   // the last link, once more, in the last bone's frame
            const V d = grippose::Sub(bones[(size_t)last].pos, bones[(size_t)bones[(size_t)last].parent].pos);
            const M3& fr = po[(size_t)last];
            rig->tip[f] = {grippose::Dot(d, fr.Row(0)) / unitsPerMetre, grippose::Dot(d, fr.Row(1)) / unitsPerMetre, grippose::Dot(d, fr.Row(2)) / unitsPerMetre};
        }
    }
    return true;
}

// The rotations of the bones against their parents' frames for a pose the hand has now: coords[i] = bone i's orientation as coordinates in the hand's proper frame (CoordsInProper), in
// the rig's order. out[i] for the library (D2rgHandIn::fingersFrom). False for a pose that is no pose (a bone that is not a rotation).
inline bool LocalsFromCoords(const D2rgRig& rig, const std::vector<int>& mirror, const std::vector<M3>& coords, D2rgQ* out) {
    const int n = rig.nBones;
    if (n < 1 || (int)mirror.size() != n || (int)coords.size() != n || !out) return false;
    std::vector<M3> p((size_t)n);
    for (int i = 0; i < n; ++i) {
        p[(size_t)i] = coords[(size_t)i];
        if (mirror[(size_t)i] < 0)
            for (int c = 0; c < 3; ++c) p[(size_t)i].m[6 + c] = -p[(size_t)i].m[6 + c];
        const float det = grippose::Det(p[(size_t)i]);
        if (!(det > 0.5f && det < 1.5f)) return false;
    }
    for (int i = 0; i < n; ++i) {
        const int pa = rig.bones[i].parent;
        out[i] = QuatOfRows(grippose::Mul(p[(size_t)i], pa < 0 ? Identity() : grippose::Transpose(p[(size_t)pa])));
    }
    return true;
}

// The bones' world orientation rows for the rotations `local` the library gave (one per bone), given the hand's current proper frame `hand` (world rows). The mirror bones' third
// rows are turned back. out[i] = unit rows. False for a rotation that is not a number.
inline bool Compose(const D2rgRig& rig, const std::vector<int>& mirror, const M3& hand, const D2rgQ* local, std::vector<M3>* out) {
    const int n = rig.nBones;
    if (n < 1 || (int)mirror.size() != n || !local || !out) return false;
    std::vector<M3> proper((size_t)n);
    out->assign((size_t)n, M3{});
    for (int i = 0; i < n; ++i) {
        const D2rgQ& q = local[i];
        if (!std::isfinite(q.x + q.y + q.z + q.w)) return false;
        const int pa = rig.bones[i].parent;
        proper[(size_t)i] = grippose::Mul(RowsOfQuat(q), pa < 0 ? hand : proper[(size_t)pa]);
        M3 w = proper[(size_t)i];
        if (mirror[(size_t)i] < 0)
            for (int c = 0; c < 3; ++c) w.m[6 + c] = -w.m[6 + c];
        (*out)[(size_t)i] = w;
    }
    return true;
}

}  // namespace griprig
