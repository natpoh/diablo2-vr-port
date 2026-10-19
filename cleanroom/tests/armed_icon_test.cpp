// armed_icon_test - [gameplay] melee_skill_armed_icon (vr/armed_icon.h): when the armed skill's icon lies in the mana medallion and how strong, the circle it is cut to, and the bake of the
// game's icon into the effect's texture. No game, no windows.h.
#include "../../vr/armed_icon.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {
int g_fail = 0;
void Check(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        std::printf("FAIL: %s\n", what);
    }
}
bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) < e; }

using namespace armedicon;

In Live(int32_t armed, bool enough) {
    In in;
    in.on = in.armedOn = in.f4 = in.medallion = true;
    in.armed = armed;
    in.enough = enough;
    return in;
}

void Mask() {
    Check(Near(CircleCover(0.0f), 1.0f) && Near(CircleCover(0.5f), 1.0f) && Near(CircleCover(1.0f - kEdge), 1.0f), "inside the circle: the whole icon");
    Check(CircleCover(1.0f) == 0.0f && CircleCover(1.2f) == 0.0f && CircleCover(10.0f) == 0.0f, "from the circle out: nothing");
    Check(CircleCover(NAN) == 0.0f && CircleCover(INFINITY) == 0.0f, "not a number: nothing");
    Check(Near(CircleCover(1.0f - 0.5f * kEdge), 0.5f), "the soft edge: half way at its middle");
    float last = 2.0f;
    bool down = true;
    for (float r = 0.0f; r <= 1.05f; r += 0.01f) {
        const float c = CircleCover(r);
        if (c > last + 1e-6f) down = false;
        last = c;
    }
    Check(down, "the cover never grows outward");
    Check(kIconR + 1e-6f < 0.80f, "the circle is inside the dome's foot (handorbs::kDomeR)");
}

void Ready() {
    // Bash costs 2; Vengeance 4 at level 1: the hit's own arithmetic decides
    Check(armedicon::Ready(126, 5, 10.0f), "Bash with 10 mana: ready");
    Check(!armedicon::Ready(126, 5, 1.5f), "Bash with 1.5 mana: the next hit is the plain attack");
    Check(armedicon::Ready(126, 5, 2.0f), "Bash with exactly its cost: ready");
    Check(armedicon::Ready(111, 1, 4.5f) && !armedicon::Ready(111, 16, 4.5f), "Vengeance: 4 at level 1, 7.75 at 16");
    Check(armedicon::Ready(126, -1, -1.0f), "the mana not read: the game decides - full strength");
    Check(!armedicon::Ready(126, 0, 100.0f), "no level in the skill: the plain attack");
    Check(!armedicon::Ready(-1, 5, 100.0f), "nothing armed: not ready");
    Check(armedicon::Ready(96, 1, 0.0f), "Sacrifice costs life, not mana: always ready");
}

void Fading() {
    Fader f;
    Out o = f.Step(Live(-1, false), 0.016f);
    Check(!o.show && o.skill == -1, "nothing armed: no icon");

    for (int i = 0; i < 60; ++i) o = f.Step(Live(126, true), 0.016f);
    Check(o.show && o.skill == 126 && Near(o.alpha, 1.0f), "armed and paid for: full strength");
    o = f.Step(Live(126, false), 0.016f);
    Check(o.show && o.alpha < 1.0f && o.alpha > kInactive, "the mana runs short: it eases down, not a jump");
    for (int i = 0; i < 60; ++i) o = f.Step(Live(126, false), 0.016f);
    Check(o.show && Near(o.alpha, kInactive), "too little mana: half see-through");
    for (int i = 0; i < 60; ++i) o = f.Step(Live(126, true), 0.016f);
    Check(Near(o.alpha, 1.0f), "the mana is back: it brightens again");

    o = f.Step(Live(147, true), 0.016f);
    Check(o.skill == 147 && o.alpha <= kInactive + 0.1f, "another skill armed: its icon at once, coming in from faint");
    for (int i = 0; i < 60; ++i) o = f.Step(Live(147, true), 0.016f);
    Check(Near(o.alpha, 1.0f), "... then full");

    o = f.Step(Live(-1, false), 0.016f);
    Check(o.show && o.skill == 147, "disarmed: the last icon fades out ...");
    for (int i = 0; i < 60; ++i) o = f.Step(Live(-1, false), 0.016f);
    Check(!o.show && o.skill == -1 && o.alpha == 0.0f, "... and is gone");

    for (int i = 0; i < 60; ++i) o = f.Step(Live(126, true), 0.016f);
    In off = Live(126, true);
    off.on = false;
    o = f.Step(off, 0.016f);
    Check(!o.show && o.alpha == 0.0f, "unticked: no icon at once - the medallion as before");
    In noArm = Live(126, true);
    noArm.armedOn = false;
    Check(!f.Step(noArm, 0.016f).show, "melee_skill_armed off: no icon");
    In flat = Live(126, true);
    flat.f4 = false;
    Check(!f.Step(flat, 0.016f).show, "not F4: no icon");
    In ball = Live(126, true);
    ball.medallion = false;
    Check(!f.Step(ball, 0.016f).show, "the ball, the panel or the game's globes: no bottom to lie on, no icon");
    Check(!f.Step(Live(49, true), 1.0f).show, "a skill that is not a melee one (Lightning) is never shown");

    Fader g;
    o = g.Step(Live(126, true), 10.0f);
    Check(o.show && o.alpha <= 0.25f / kFadeS + 1e-4f, "a stall's long step is capped: it still eases in");
    o = g.Step(Live(126, true), NAN);
    Check(std::isfinite(o.alpha), "a step that is not a number changes nothing");

    float u[4];
    Uniform(o, u);
    Check(u[0] == 1.0f && Near(u[1], o.alpha) && Near(u[2], kIconR) && Near(u[3], kEdge), "the effect's numbers");
    Uniform(Out{}, u);
    Check(u[0] == 0.0f && u[1] == 0.0f, "nothing to draw: OrbIcon.x 0");
}

void Baking() {
    std::vector<uint8_t> out;
    Check(!Bake(nullptr, 0, 0, out) && out.size() == (size_t)kTex * kTex * 4, "no picture: an empty texture");

    // a 64 x 64 icon: opaque red, with a white frame of 3 pixels (the tile's own border)
    const unsigned w = 64, h = 64;
    std::vector<uint8_t> src(w * h * 4);
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            uint8_t* p = &src[(y * w + x) * 4];
            const bool frame = x < 3 || y < 3 || x >= w - 3 || y >= h - 3;
            p[0] = 255;
            p[1] = p[2] = frame ? 255 : 0;
            p[3] = 255;
        }
    Check(Bake(src.data(), w, h, out), "baked");
    auto at = [&](int x, int y) { return &out[((size_t)y * kTex + x) * 4]; };
    const uint8_t* mid = at(kTex / 2, kTex / 2);
    Check(mid[3] == 255 && mid[0] == 255 && mid[1] == 0, "the middle: the icon, opaque");
    bool outside = true, premul = true, frame = false;
    for (int y = 0; y < kTex; ++y)
        for (int x = 0; x < kTex; ++x) {
            const float u = (x + 0.5f) / kTex * 2.0f - 1.0f, v = (y + 0.5f) / kTex * 2.0f - 1.0f;
            const uint8_t* p = at(x, y);
            if (std::sqrt(u * u + v * v) >= 1.0f && (p[0] | p[1] | p[2] | p[3])) outside = false;
            if (p[0] > p[3] || p[1] > p[3] || p[2] > p[3]) premul = false;
            if (p[1] > 128) frame = true;
        }
    Check(outside, "nothing from the circle out: the corners are empty");
    Check(premul, "premultiplied: no channel over the alpha");
    Check(!frame, "the tile's frame is left out (the circle is taken from the middle)");
    const uint8_t* edge = at(kTex / 2, 1);
    Check(edge[3] < 128, "the circle's edge is soft");
    Check(at(0, 0)[3] == 0 && at(kTex - 1, kTex - 1)[3] == 0, "the texture's corners: clear");

    // a straight-alpha source: half see-through white stays white at half, premultiplied
    std::vector<uint8_t> half(4 * 4 * 4);
    for (size_t i = 0; i < half.size(); i += 4) {
        half[i] = half[i + 1] = half[i + 2] = 255;
        half[i + 3] = 128;
    }
    Bake(half.data(), 4, 4, out);
    const uint8_t* m = at(kTex / 2, kTex / 2);
    Check(m[3] == 128 && m[0] == 128, "straight alpha in, premultiplied out");
}

}  // namespace

int main() {
    Mask();
    Ready();
    Fading();
    Baking();
    if (g_fail) {
        std::printf("armed_icon_test: %d FAILED\n", g_fail);
        return 1;
    }
    std::printf("armed_icon_test: all passed\n");
    return 0;
}
