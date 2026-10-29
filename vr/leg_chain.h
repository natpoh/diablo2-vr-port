// leg_chain.h - a hero rig's legs found by its own hierarchy and bind pose, not by the bones' names: the fallback of skeletons.cpp Learn's name rule (a bone
// named foot or ankle, its parent the knee, that one's the hip). The barbarian's rig has bones named otherwise (R_Wrist, L_ForeArm ...) and no leg was found
// on it ("crouch - this hero's rig has no leg chain found", the 2026-10-26 log): its crouch did not bend, and the other players saw his legs run as the game's
// while [multiplayer] pose_share_legs held his pelvis to his look. Pure (cleanroom/tests/leg_chain_test.cpp).
//
// A foot: a bone not in the body above the pelvis, low in the bind pose (an ankle: 5 - 30 % of the pelvis's standing height - not a toe on the floor, not a
// knee); its hip: the highest of its ancestors below the pelvis's height (and not above it by more than 15 %), none of them in the upper body; its knee: the
// ancestor between them nearest the middle height, each joint a tenth of a unit or more below the one above. Of each side's candidates the one whose shin
// and thigh are most nearly of a length (the ankle - not a twist bone up the calf, not the heel or a toe under it); the side by where the foot stands (model +x
// is the hero's left: [0] right, [1] left).
#pragma once

#include <cmath>

namespace legchain {

struct Leg { int hip = -1, knee = -1, foot = -1; };

// n bones; parent[i] (-1 the root); x[i], y[i]: bind positions (model space); upper[i]: in the body above the pelvis (spine_01 and under); pelvisY: its
// standing height. Fills legs[2] where found; returns how many sides were.
inline int Find(int n, const int* parent, const float* x, const float* y, const char* upper, int pelvis, float pelvisY, Leg legs[2]) {
    if (!parent || !x || !y || !upper || n <= 0 || !(pelvisY > 0.5f)) return 0;
    float best[2] = {1e30f, 1e30f};   // |log(shin / thigh)| of each side's foot so far
    for (int f = 0; f < n; ++f) {
        if (upper[f] || f == pelvis || !std::isfinite(x[f] + y[f])) continue;
        if (y[f] < 0.05f * pelvisY || y[f] > 0.30f * pelvisY) continue;   // an ankle: above the toes on the floor, well under the knee
        // its ancestors, none of them upper; the hip the highest of them at about the pelvis's height or under it
        int hip = -1, depth = 0;
        bool bad = false;
        for (int a = parent[f]; a >= 0 && depth < 64; a = parent[a], ++depth) {
            if (upper[a]) { bad = true; break; }
            if (a == pelvis) break;
            if (y[a] <= 1.15f * pelvisY && (hip < 0 || y[a] > y[hip])) hip = a;
        }
        if (bad || hip < 0 || y[hip] - y[f] < 0.5f * pelvisY) continue;   // (a leg spans most of the hips' height)
        // the knee: between them, nearest the middle height
        const float mid = 0.5f * (y[hip] + y[f]);
        int knee = -1;
        for (int a = parent[f]; a >= 0 && a != hip; a = parent[a])
            if (y[hip] - y[a] >= 0.1f && y[a] - y[f] >= 0.1f && (knee < 0 || std::fabs(y[a] - mid) < std::fabs(y[knee] - mid))) knee = a;
        if (knee < 0) continue;
        const int side = x[f] < 0.0f ? 0 : 1;
        const float score = std::fabs(std::log((y[knee] - y[f]) / (y[hip] - y[knee])));
        if (score < best[side]) { best[side] = score; legs[side] = {hip, knee, f}; }
    }
    return (legs[0].foot >= 0 ? 1 : 0) + (legs[1].foot >= 0 ? 1 : 0);
}

}  // namespace legchain
