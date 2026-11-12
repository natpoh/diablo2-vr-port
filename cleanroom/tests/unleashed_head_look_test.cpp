// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// After D2R VR Unleashed commit 64e723b30c402a0d9b164b3038ef61a5f30847dd, its steamvr/look_at_test.cpp, for D2R VR Authentic's port
// (vr/unleashed_head_look.h, [world] town_heads_look): the town's people, the chance, the model's ahead from its face, the damped ease, the
// neck's limits. Stays MPL 2.0.
#include "unleashed_head_look.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace headlook;
static int g_fail = 0, g_checks = 0;
#define CHECK(x) do { ++g_checks; if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++g_fail; } } while (0)
static bool Near(float a, float b, float e = 0.01f) { return std::fabs(a - b) <= e; }

// A row-vector matrix: rotation about y by `yaw` (atan2(x, z) rule: +z turned toward +x), translation t.
static void YawMatrix(float yaw, V t, float m[16]) {
    std::memset(m, 0, 16 * sizeof(float));
    const float c = std::cos(yaw), s = std::sin(yaw);
    // row 0 = x axis turned, row 2 = z axis turned (v' = v . M)
    m[0] = c; m[2] = -s; m[5] = 1; m[8] = s; m[10] = c; m[15] = 1;
    m[12] = t.x; m[13] = t.y; m[14] = t.z;
}
static void Translation(V t, float m[16]) { YawMatrix(0, t, m); }
// The inverse bind of a bone standing at p unturned (model frame): translation -p.
static void InverseAt(V p, float m[16]) { Translation({-p.x, -p.y, -p.z}, m); }

// A little person: root, spine, neck, head, jaw, eye_l, eye_r (bind pose facing `face` in the model). Heights in model units (1 = 1 m here).
struct Person {
    std::vector<std::string> names{"root", "spine", "neck", "head", "jaw", "eye_l", "eye_r"};
    std::vector<int> parent{-1, 0, 1, 2, 3, 3, 3};
    std::vector<V> at;
    std::vector<std::array<float, 16>> inv;
    std::vector<Bone> bones;
    std::vector<int16_t> parents16;
    explicit Person(V face = {0, 0, 1}) {
        const V side{face.z, 0, -face.x};
        at = {{0, 0, 0}, {0, 1.0f, 0}, {0, 1.5f, 0}, {0, 1.6f, 0}, V{0, 1.55f, 0} + face * 0.08f, V{0, 1.68f, 0} + face * 0.09f + side * 0.03f,
              V{0, 1.68f, 0} + face * 0.09f - side * 0.03f};
        inv.resize(at.size());
        for (size_t i = 0; i < at.size(); ++i) InverseAt(at[i], inv[i].data());
        for (size_t i = 0; i < at.size(); ++i) bones.push_back({names[i], parent[i], inv[i].data()});
        for (int p : parent) parents16.push_back((int16_t)p);
    }
    // The pose as bound (each bone's world = its bind place, unturned), body turned by bodyYaw about the root.
    std::vector<float> Pose(float bodyYaw = 0.0f) const {
        std::vector<float> m(at.size() * 16);
        for (size_t i = 0; i < at.size(); ++i) {
            const V p = About({0, 1, 0}, bodyYaw, at[i]);
            YawMatrix(bodyYaw, p, &m[i * 16]);
        }
        return m;
    }
};

// The head's look in the world after a turn: its bone's ahead (rig.headAhead) through the posed matrix and the placement.
static V HeadLook(const Rig& r, const float* pose, const float* placement) { return Unit(Direction(placement, Direction(pose + r.head * 16, r.headAhead))); }

int main() {
    {   // the town's people: the game's NPC folder only
        CHECK(TownNpcPath("data\\hd\\character\\npc\\akara\\skeleton\\akara.skeleton"));
        CHECK(TownNpcPath("data/hd/character/npc/act2guard1/skeleton/act2guard1.skeleton"));
        CHECK(TownNpcPath("DATA\\HD\\CHARACTER\\NPC\\WARRIV1\\SKELETON\\WARRIV1.SKELETON"));
        CHECK(!TownNpcPath("data\\hd\\character\\monster\\fallen1\\skeleton\\fallen1.skeleton"));
        CHECK(!TownNpcPath("data\\hd\\character\\player\\amazon\\skeleton\\amazon.skeleton"));
        CHECK(!TownNpcPath("data\\hd\\character\\enemy\\wolf\\skeleton\\wolf.skeleton"));
        CHECK(!TownNpcPath(""));
    }
    {   // the chance: stable, about the share asked, 0 none, 100 all, another visit another draw
        int yes = 0, same = 0, changed = 0;
        for (uint32_t e = 1; e <= 10000; ++e) {
            const bool a = Chooses(e, 1234u, 50);
            yes += a ? 1 : 0;
            same += a == Chooses(e, 1234u, 50) ? 1 : 0;
            changed += a != Chooses(e, 98765u, 50) ? 1 : 0;
        }
        CHECK(yes > 4700 && yes < 5300);
        CHECK(same == 10000);
        CHECK(changed > 4000 && changed < 6000);   // a new visit: about half change their mind
        int quarter = 0;
        for (uint32_t e = 1; e <= 10000; ++e) quarter += Chooses(e, 7u, 25) ? 1 : 0;
        CHECK(quarter > 2200 && quarter < 2800);
        bool none = false, all = true;
        for (uint32_t e = 1; e <= 1000; ++e) { none = none || Chooses(e, 7u, 0); all = all && Chooses(e, 7u, 100); }
        CHECK(!none && all);
        int neighbours = 0;   // neighbouring ids are not all alike
        for (uint32_t e = 100; e < 110; ++e) neighbours += Chooses(e, 3u, 50) ? 1 : 0;
        CHECK(neighbours > 0 && neighbours < 10);
    }
    {   // names
        CHECK(HeadName("head") && HeadName("Bip01 Head") && HeadName("head_bind_jnt") && !HeadName("head_attach") && !HeadName("jaw") && !HeadName("headnub"));
        CHECK(NeckName("neck") && NeckName("Bip01 Neck1") && !NeckName("necklace") && !NeckName("neckhead"));
        CHECK(FaceName("eye_l") && FaceName("Jaw") && !FaceName("eye_attach") && !FaceName("spine"));
    }
    {   // learn: head, neck, body; the model's ahead from the face (+z here)
        Person p;
        Rig r;
        CHECK(Learn(p.bones.data(), (int)p.bones.size(), r) == Refusal::None);
        CHECK(r.head == 3 && r.root == 2 && r.body == 1);
        CHECK(r.faceBones == 3 && Near(r.modelAhead.z, 1.0f) && Near(r.modelAhead.x, 0.0f));
    }
    {   // a model whose face looks along -z: found from its bones, not assumed
        Person p(V{0, 0, -1});
        Rig r;
        CHECK(Learn(p.bones.data(), (int)p.bones.size(), r) == Refusal::None);
        CHECK(Near(r.modelAhead.z, -1.0f));
        // facing the eyes straight ahead (along -z): nothing to turn
        const std::vector<float> pose = p.Pose();
        float place[16];
        Translation({0, 0, 0}, place);
        Aim a;
        CHECK(Want(r, pose.data(), (int)p.at.size(), place, V{0, 1.6f, -3}, 1.0f, Limits{}, a));
        CHECK(a.weight > 0.99f && Near(a.yaw, 0.0f) && Near(a.tilt, 0.0f, 0.02f));
        // the eyes straight behind it (+z): let go of (the +z model would have looked straight at them)
        CHECK(Want(r, pose.data(), (int)p.at.size(), place, V{0, 1.6f, 3}, 1.0f, Limits{}, a));
        CHECK(a.weight == 0.0f);
    }
    {   // no face bones: +z, as the heroes'
        Person p;
        std::vector<Bone> four(p.bones.begin(), p.bones.begin() + 4);
        Rig r;
        CHECK(Learn(four.data(), 4, r) == Refusal::None);
        CHECK(r.faceBones == 0 && Near(r.modelAhead.z, 1.0f));
    }
    {   // no head / bad parents: refused
        std::vector<Bone> b{{"root", -1, nullptr}, {"spine", 0, nullptr}};
        Rig r;
        CHECK(Learn(b.data(), 2, r) == Refusal::NoHead);
        std::vector<Bone> bad{{"root", 1, nullptr}, {"head", 0, nullptr}};
        CHECK(Learn(bad.data(), 2, r) == Refusal::Bones);
    }
    Person p;
    Rig rig;
    Learn(p.bones.data(), (int)p.bones.size(), rig);
    const int n = (int)p.at.size();
    {   // the limits: 40 degrees to the side is followed; 90 held at 70; up and down held at 30 / 40
        const std::vector<float> pose = p.Pose();
        float place[16];
        Translation({10, 0, 5}, place);
        const Limits l = FromIni(70, 30, 40, 7);
        Aim a;
        const float d = 3.0f;
        CHECK(Want(rig, pose.data(), n, place, V{10 + d * std::sin(40 * kRad), 1.6f, 5 + d * std::cos(40 * kRad)}, 1.0f, l, a));
        CHECK(Near(a.yaw, 40 * kRad) && a.weight > 0.99f);
        CHECK(Want(rig, pose.data(), n, place, V{10 + d, 1.6f, 5}, 1.0f, l, a));   // 90 to the side
        CHECK(Near(a.yaw, 70 * kRad));
        CHECK(Want(rig, pose.data(), n, place, V{10 - d, 1.6f, 5}, 1.0f, l, a));   // the other side
        CHECK(Near(a.yaw, -70 * kRad));
        CHECK(Want(rig, pose.data(), n, place, V{10, 1.6f + 4, 5 + 1}, 1.0f, l, a));   // high above: up held at 30
        CHECK(Near(a.tilt, 30 * kRad));
        CHECK(Want(rig, pose.data(), n, place, V{10, 1.6f - 4, 5 + 1}, 1.0f, l, a));   // far below: down held at 40
        CHECK(Near(a.tilt, -40 * kRad));
        CHECK(Want(rig, pose.data(), n, place, V{10, 1.6f, 5 - 3}, 1.0f, l, a));   // behind: let go of
        CHECK(a.weight == 0.0f);
        CHECK(Want(rig, pose.data(), n, place, V{10, 1.6f, 5 + 9}, 1.0f, l, a));   // out of range
        CHECK(a.weight == 0.0f);
        CHECK(Want(rig, pose.data(), n, place, V{10, 1.6f, 5 + 6}, 1.0f, l, a));   // fading (5.5 - 7 m)
        CHECK(a.weight > 0.0f && a.weight < 1.0f);
        // units: 4 a metre - the same place 4x as far is the same look
        float place4[16];
        Translation({40, 0, 20}, place4);
        std::vector<float> pose4 = pose;
        for (int b = 0; b < n; ++b) for (int k = 12; k < 15; ++k) pose4[b * 16 + k] *= 4.0f;
        CHECK(Want(rig, pose4.data(), n, place4, V{40 + 4 * d * std::sin(40 * kRad), 6.4f, 20 + 4 * d * std::cos(40 * kRad)}, 4.0f, l, a));
        CHECK(Near(a.yaw, 40 * kRad));
        // the ini's degrees kept to a neck's
        const Limits wild = FromIni(400, -5, 1000, 0);
        CHECK(Near(wild.yaw, 90 * kRad) && Near(wild.up, 0) && Near(wild.down, 60 * kRad) && Near(wild.range, 2));
    }
    {   // the animation's head already turned 10 deg the other way: 80 is added, the head as drawn stays at the 70 limit (test 0.213.6's "widest 80")
        std::vector<float> pose = p.Pose();
        YawMatrix(-10 * kRad, p.at[3], &pose[3 * 16]);
        float place[16];
        Translation({0, 0, 0}, place);
        Aim a;
        CHECK(Want(rig, pose.data(), n, place, V{3, 1.6f, 0}, 1.0f, FromIni(70, 30, 40, 7), a));
        CHECK(Near(a.animYaw, -10 * kRad) && Near(a.yaw, 80 * kRad) && Near(a.animYaw + a.yaw, 70 * kRad));
        Eased e;
        e.yaw = a.yaw;
        CHECK(Apply(rig, p.parents16.data(), pose.data(), n, place, e, Limits{}));
        CHECK(Near(YawOf(HeadLook(rig, pose.data(), place)), 70 * kRad, 0.02f));
    }
    {   // a body turned in the world: the limit is from where the BODY faces
        const float body = 50 * kRad;
        const std::vector<float> pose = p.Pose();
        float place[16];
        YawMatrix(body, {0, 0, 0}, place);
        Aim a;
        const V eye{3 * std::sin(body + 30 * kRad), 1.6f, 3 * std::cos(body + 30 * kRad)};
        CHECK(Want(rig, pose.data(), n, place, eye, 1.0f, Limits{}, a));
        CHECK(Near(a.yaw, 30 * kRad));
    }
    {   // apply: the head then looks where it was turned; the body below the neck stays; the jaw and eyes go with the head
        std::vector<float> pose = p.Pose();
        const std::vector<float> before = pose;
        float place[16];
        Translation({0, 0, 0}, place);
        Eased e;
        e.yaw = 40 * kRad;
        e.tilt = 10 * kRad;
        CHECK(Apply(rig, p.parents16.data(), pose.data(), n, place, e, Limits{}));
        const V look = HeadLook(rig, pose.data(), place);
        CHECK(Near(YawOf(look), 40 * kRad, 0.02f) && Near(TiltOf(look), 10 * kRad, 0.02f));
        CHECK(std::memcmp(&pose[0], &before[0], 2 * 16 * sizeof(float)) == 0);   // root and spine untouched
        const V eye{pose[5 * 16 + 12], pose[5 * 16 + 13], pose[5 * 16 + 14]}, head{pose[3 * 16 + 12], pose[3 * 16 + 13], pose[3 * 16 + 14]};
        {   // the eye sits where it sat in the head's own frame (it went with the head)
            const float* h = &pose[3 * 16];
            const V off = eye - head, local{Dot(off, V{h[0], h[1], h[2]}), Dot(off, V{h[4], h[5], h[6]}), Dot(off, V{h[8], h[9], h[10]})};
            const V was = p.at[5] - p.at[3];
            CHECK(Near(local.x, was.x, 1e-4f) && Near(local.y, was.y, 1e-4f) && Near(local.z, was.z, 1e-4f));
        }
        CHECK(Near(std::sqrt(Dot(eye - head, eye - head)), std::sqrt(Dot(p.at[5] - p.at[3], p.at[5] - p.at[3])), 1e-4f));   // rigid
        // the neck took its share: the head's place moved round the neck's root
        CHECK(!Near(head.x, p.at[3].x, 1e-5f) || !Near(head.z, p.at[3].z, 1e-5f));
        Eased bad;
        bad.yaw = NAN;
        CHECK(!Apply(rig, p.parents16.data(), pose.data(), n, place, bad, Limits{}));
    }
    {   // the ease: starts slowly, never jumps, never overshoots, gets there; back again when let go of
        Limits l;
        Eased e;
        Aim want;
        want.yaw = 60 * kRad;
        Step(e, want, 1.0f / 90, l);
        const float first = e.yaw;
        CHECK(first > 0 && first < 2 * kRad);   // a slow start (no jump to a fraction of the way)
        float last = first, most = 0;
        bool over = false, monotone = true;
        for (int i = 0; i < 270; ++i) {   // 3 s at 90 Hz
            Step(e, want, 1.0f / 90, l);
            over = over || e.yaw > want.yaw + 1e-4f;
            monotone = monotone && e.yaw >= last - 1e-6f;
            most = std::max(most, (e.yaw - last) * 90);
            last = e.yaw;
        }
        CHECK(!over && monotone);
        CHECK(Near(e.yaw, want.yaw, 0.5f * kRad));
        CHECK(most <= l.fastest + 1e-3f);
        want.yaw = 0;
        for (int i = 0; i < 270; ++i) Step(e, want, 1.0f / 90, l);
        CHECK(Still(e));
        // a long frame or none: no step past the cap, nothing for zero
        Eased z;
        Step(z, Aim{1.0f, 0.0f, 1.0f, 1.0f}, 0.0f, l);
        CHECK(z.yaw == 0.0f);
        Step(z, Aim{1.0f, 0.0f, 1.0f, 1.0f}, 5.0f, l);
        CHECK(z.yaw <= l.fastest * 0.1f + 1e-4f);
    }
    std::printf("unleashed_head_look_test: %d/%d passed\n", g_checks - g_fail, g_checks);
    return g_fail ? 1 : 0;
}
