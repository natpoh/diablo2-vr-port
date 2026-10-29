// Offline checks of vr/leg_chain.h: a hero rig's legs by its hierarchy and bind pose, whatever its bones are named - a rig like the named ones, a biped-style
// one with twist bones and its thighs off the root, one with no legs, toes and heels left out. Run: leg_chain_test.exe (exit 0 = all passed).
#include <cstdio>
#include <vector>

#include "leg_chain.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

struct Rig {
    std::vector<int> parent;
    std::vector<float> x, y;
    std::vector<char> upper;
    int Add(int p, float px, float py, bool up = false) {
        parent.push_back(p); x.push_back(px); y.push_back(py); upper.push_back(up ? 1 : 0);
        return (int)parent.size() - 1;
    }
};

int main() {
    using legchain::Leg;
    // a rig whose thighs hang off the pelvis (the "_bind_jnt" heroes): pelvis 9.5, knees 5, ankles 0.9, toes 0.3
    {
        Rig r;
        const int root = r.Add(-1, 0, 0);
        const int pelvis = r.Add(root, 0, 9.5f);
        const int spine = r.Add(pelvis, 0, 10.5f, true);
        r.Add(spine, 0, 15.0f, true);
        int hip[2], knee[2], foot[2];
        for (int s = 0; s < 2; ++s) {
            const float sx = s ? 1.0f : -1.0f;
            hip[s] = r.Add(pelvis, sx, 9.0f);
            knee[s] = r.Add(hip[s], sx * 1.1f, 5.0f);
            foot[s] = r.Add(knee[s], sx * 1.1f, 0.9f);
            r.Add(foot[s], sx * 1.1f, 0.3f);        // the toes
            r.Add(foot[s], sx * 1.1f, 0.6f);        // a heel under the ankle
        }
        Leg legs[2];
        CHECK(legchain::Find((int)r.parent.size(), r.parent.data(), r.x.data(), r.y.data(), r.upper.data(), pelvis, 9.5f, legs) == 2);
        for (int s = 0; s < 2; ++s) CHECK(legs[s].hip == hip[s] && legs[s].knee == knee[s] && legs[s].foot == foot[s]);
    }

    // a biped-style rig (the barbarian's kind of names, not read here): thighs off the ROOT beside the pelvis, a twist bone in the thigh and in the calf
    {
        Rig r;
        const int root = r.Add(-1, 0, 0);
        const int pelvis = r.Add(root, 0, 9.0f);
        r.Add(pelvis, 0, 10.0f, true);   // the spine
        int thigh[2], calf[2], ankle[2];
        for (int s = 0; s < 2; ++s) {
            const float sx = s ? 1.2f : -1.2f;
            thigh[s] = r.Add(root, sx, 8.8f);
            const int twist = r.Add(thigh[s], sx, 7.0f);
            calf[s] = r.Add(twist, sx, 4.7f);
            const int calfTwist = r.Add(calf[s], sx, 2.5f);
            ankle[s] = r.Add(calfTwist, sx, 0.85f);
            r.Add(ankle[s], sx, 0.2f);
        }
        Leg legs[2];
        CHECK(legchain::Find((int)r.parent.size(), r.parent.data(), r.x.data(), r.y.data(), r.upper.data(), pelvis, 9.0f, legs) == 2);
        for (int s = 0; s < 2; ++s) CHECK(legs[s].hip == thigh[s] && legs[s].knee == calf[s] && legs[s].foot == ankle[s]);
    }

    // no legs (a skirt to the floor of one bone, an arm down by the side in the upper body): none
    {
        Rig r;
        const int root = r.Add(-1, 0, 0);
        const int pelvis = r.Add(root, 0, 9.0f);
        const int spine = r.Add(pelvis, 0, 10.0f, true);
        const int arm = r.Add(spine, -2.0f, 13.0f, true);
        r.Add(arm, -2.0f, 1.0f, true);   // (upper: never a leg)
        r.Add(pelvis, 0.5f, 1.0f);       // a skirt's end: no knee between
        Leg legs[2];
        CHECK(legchain::Find((int)r.parent.size(), r.parent.data(), r.x.data(), r.y.data(), r.upper.data(), pelvis, 9.0f, legs) == 0);
        CHECK(legs[0].foot < 0 && legs[1].foot < 0);
    }

    // nothing to read: nothing found
    {
        Leg legs[2];
        CHECK(legchain::Find(0, nullptr, nullptr, nullptr, nullptr, 0, 9.0f, legs) == 0);
        const int p[1] = {-1};
        const float v[1] = {0.0f};
        const char u[1] = {0};
        CHECK(legchain::Find(1, p, v, v, u, 0, 0.0f, legs) == 0);
    }

    std::printf("leg_chain_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
