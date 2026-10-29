// The bow's own rig drawn by the pull (vr/bow_layouts.h, vr/bow_rig.h) on synthetic bows, without the game.
//
//   bow_rig_test
//
// Every bow a hero holds is a skeleton of its own; kHeroBows lists the bones (names and hierarchy only) of all 15 as the game
// has them, and a bind pose is made up for each by what the bones are FOR. Checks: each of the 15 is matched to a layout and
// learned, the crossbows and monsters' bows are refused with a reason; no pull is the bind pose again and a pull is a pose
// again each time (it snaps back); the nock goes where the hand is, the limbs keep their length, the string halves keep
// theirs, whatever hangs on a limb turns with it, whatever hangs on the string goes with it, the arrow lies on the string
// along the aim, a bone that is none of these is left alone; a hand out of reach is pulled toward rest and never explodes the
// rig; a bad input leaves the game's own matrices alone; the hunter's bow, as the first version of this posed it (a copy of
// that code is kept here), comes out the same; the release; a cancelled pull going back to rest; the frame chain; the hold.

#include "bow_rig.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace bowrig;

namespace {

int g_fails = 0;
int g_checks = 0;

#define EXPECT(c, ...)                                         \
    do {                                                       \
        ++g_checks;                                            \
        if (!(c)) {                                            \
            ++g_fails;                                         \
            std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
            std::printf(__VA_ARGS__);                          \
            std::printf("\n");                                 \
        }                                                      \
    } while (0)

bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
bool Near(V3 a, V3 b, float eps = 1e-4f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps); }
bool NearMat(const Mat& a, const Mat& b, float eps = 1e-4f) {
    for (int i = 0; i < 16; ++i) if (!Near(a.m[i], b.m[i], eps)) return false;
    return true;
}
float Dist(V3 a, V3 b) { return Length(a - b); }
float Rows(const Mat& a) { return Length({a.m[0], a.m[1], a.m[2]}); }

Mat Translation(V3 p) { Mat r = Identity(); SetPosition(r, p); return r; }
// a turn about the vertical by deg, row-vector form
Mat TurnY(float deg) {
    const float t = deg * 0.0174532925f, c = std::cos(t), s = std::sin(t);
    Mat r = Identity();
    r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
    return r;
}
Mat TurnX(float deg) {
    const float t = deg * 0.0174532925f, c = std::cos(t), s = std::sin(t);
    Mat r = Identity();
    r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
    return r;
}
Mat TurnZ(float deg) {
    const float t = deg * 0.0174532925f, c = std::cos(t), s = std::sin(t);
    Mat r = Identity();
    r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
    return r;
}

// The bones (name>parent) of every bow in the game, as its skeleton lists them - names and hierarchy only.
struct Spec { const char* model; const char* bones; };
const Spec kHeroBows[] = {
    {"hunters_bow",
     "root>-1 translate>0 anim>1 cog>2 upperBend>3 lowerBend>3 lowerString>5 upperString>4 bowString>3 joint2>6 joint1>7 arrow>3 arrow_helper>11"},
    {"short_bow",
     "root>-1 translate>0 anim>1 cog>2 topBowBend>3 botBowBend>3 top_stringHelper>4 bot_stringHelper>5 arrow>3 arrow_helper>8 bowString>3 bowString_upper_helper>10 bowString_lower_helper>10"},
    {"long_bow",
     "root>-1 translate>0 anim>1 cog>2 upperBend>3 lowerBend>3 bowString>3 alt_cog>3 upperString_shortBattleBow_offset>4 upperString>8 lowerString_shortBattleBow_offset>5 lowerString>10 upperString_end>9 lowerString_end>11 arrow>3 arrowHelper>14"},
    {"composite_bow",
     "root>-1 translate>0 anim>1 cog>2 upperBend>3 lowerBend>3 bowString>3 upperString_compositeBow_offset>4 upperString>7 lowerString_compositeBow_offset>5 lowerString>9 arrow>3 arrow_helper>11"},
    {"short_battle_bow",
     "root>-1 translate>0 anim>1 cog>2 upperBend>3 lowerBend>3 alt_cog>3 upperString_shortBattleBow_offset>4 upperString>7 lowerString_shortBattleBow_offset>5 lowerString>9 upperString_end>8 lowerString_end>10 arrow>3 arrowHelper>13 bowString>3 bowString_upper_helper>15 bowString_lower_helper>15"},
    {"long_battle_bow",
     "root>-1 translate>0 anim>1 cog>2 upperBend>3 lowerBend>3 alt_cog>3 bowString>3 upperString>4 lowerString>5 lowerString_altPivot>5 upperString_altPivot>4 upperString_end>8 lowerString_end>9 lowerString_altPivot_end>10 upperString_altPivot_end>11 arrow>3 arrow_helper>16"},
    {"short_war_bow",
     "root>-1 translate>0 anim>1 cog>2 botBowBend>3 topBowBend>3 bot_stringHelper>4 bowString>3 top_stringHelper>5 lowerString_altPivot>4 upperString_altPivot>5 lowerString_altPivot_end>9 upperString_altPivot_end>10 arrow>3 alt_arrow>13"},
    {"long_war_bow",
     "root>-1 translate>0 anim>1 cog>2 topBowBend>3 botBowBend>3 bowString>3 top_stringHelper>4 bot_stringHelper>5 lowerString_altPivot>5 upperString_altPivot>4 lowerString_altPivot_end>9 upperString_altPivot_end>10 arrow>3 arrow_helper>13"},
    {"stag_bow",
     "root>-1 translate>0 anim>1 cog>2 lowerBend>3 upperBend>3 alt_cog>3 bowString>3 lowerString_shortBattleBow_offset>4 upperString_shortBattleBow_offset>5 upperString>9 lowerString>4 upperString_end>10 lowerString_end>11 arrow>3 arrowHelper>14"},
    {"reflex_bow",
     "root>-1 translate>0 anim>1 cog>2 lowerBend>3 upperBend>3 bowString>3 upperString_compositeBow_offset>5 upperString>7 lowerString_compositeBow_offset>4 lowerString>9 arrow2>3 arrow_helper>11"},
    {"hellclap",
     "root>-1 translate>0 anim>1 cog>2 topBowBend>3 botBowBend>3 arrow_helper>3 arrow>3 bowString>3 top_stringHelper>4 bot_stringHelper>5 upperString_altPivot>4 lowerString_altPivot>5 upperString_altPivot_end>11 lowerString_altPivot_end>12"},
    {"kuko_shakaku",
     "root>-1 translate>0 anim>1 cog>2 upperBend>3 lowerBend>3 bowString>3 alt_cog>3 upperString_shortBattleBow_offset>4 upperString>8 feathers_01>3 lowerString_shortBattleBow_offset>5 lowerString>11 upperString_end>9 feathers_02>10 secondaryFeather>10 lowerString_end>12 arrow2>3 arrowHelper>17"},
    {"piercerib",
     "root>-1 translate>0 anim>1 cog>2 lowerBend>3 upperBend>3 lowerString>4 bowString>3 upperString>5 lowerString_altPivot>4 upperString_altPivot>5 joint4>9 joint2>10 arrow>3 arrow_helper>13"},
    {"pullspite",
     "root>-1 translate>0 anim>1 cog>2 topBowBend>3 botBowBend>3 top_stringHelper>4 bowString>3 topFront_leaves>3 topBack_leaves>3 botFront_leaves>3 botBack_leaves>3 bot_stringHelper>5 arrow>3 arrow_helper>13"},
    {"whichwild_string",
     "root>-1 translate>0 anim>1 cog>2 lowerBend>3 upperBend>3 lowerString>4 upperString>5 bowString>3 bot_stringHelper1>6 top_stringHelper1>7 lowerString_altPivot>4 upperString_altPivot>5 lowerString_altPivot_end>11 upperString_altPivot_end>12 arrow>3 arrow_helper>15"},
};
const Spec kOtherBows[] = {   // monsters' bows, and the crossbows (their own parts: a trigger, a lever, a magazine)
    {"skeleton_bow",
     "root>-1 translate>0 anim>1 cog>2 topBowBend>3 botBowBend>3 top_stringHelper>4 bot_stringHelper>5 bowString>3 bowString_upper_helper>8 bowString_lower_helper>8 arrow>3 arrow_helper>11"},
    {"corrupt_rogue_bow",
     "root>-1 translate>0 anim>1 cog>2 topBowBend>3 botBowBend>3 bot_stringHelper>5 top_stringHelper>4 bot_stringHelper1>6 top_stringHelper1>7 arrow>3 arrow_helper>10 bowString>3 bowString_upper_helper>12 bowString_lower_helper>12"},
    {"crossbow",
     "root>-1 translate>0 anim>1 cog>2 R_bowBend>3 L_bowBend>3 trigger>3 spool>3 clip>3 bowString>3 L_bowString_helper_jnt>5 R_bowString_helper_jnt>4 L_bowString_helper_end_jnt>10 R_bowString_helper_end_jnt>11 bolt>3 bolt_helper>14"},
    {"light_crossbow",
     "root>-1 translate>0 anim>1 cog>2 L_bowBend>3 R_bowBend>3 bowString>3 trigger>3 spool>3 L_bowString_helper_jnt>4 R_bowString_helper_jnt>5 L_bowString_helper_end_jnt>9 R_bowString_helper_end_jnt>10 bolt>3 bolt_helper>13"},
    {"heavy_crossbow",
     "root>-1 translate>0 anim>1 cog>2 R_bowBend>3 L_bowBend>3 trigger>3 plate>3 hammer>3 bowString>3 L_bowString_help_jnt>5 R_bowString_help_jnt>4 L_bowString_help_end_jnt>10 R_bowString_help_end_jnt>11 bolt>3 bolt_helper>14"},
    {"repeating_crossbow",
     "root>-1 translate>0 anim>1 cog>2 lever_driven_jnt>3 magazine_driven_jnt>4 R_bowBend>3 L_bowBend>3 lever>3 new_string>3 magazin_driven_end_jnt>5 L_bowString_helper_jnt>7 R_bowString_helper_jnt>6 L_bowString_helper_end_jnt>11 R_bowString_helper_end_jnt>12 bolt>3 bolt_helper>15"},
    {"hellcast",
     "root>-1 translate>0 anim>1 cog>2 L_bowBend>3 R_bowBend>3 trigger>3 plate>3 bowString>3 L_bowString_help_jnt>4 R_bowString_help_jnt>5 L_bowString_help_end_jnt>9 R_bowString_help_end_jnt>10 bolt>3 bolt_helper>13"},
    {"ichorsting",
     "root>-1 translate>0 anim>1 cog>2 L_bowBend>3 R_bowBend>3 trigger>3 plate>3 bowString>3 L_bowString_help_jnt>4 R_bowString_help_jnt>5 L_bowString_help_end_jnt>9 R_bowString_help_end_jnt>10 bolt>3 bolt_helper>13"},
    {"langer_briser",
     "root>-1 translate>0 anim>1 cog>2 R_bowBend>3 L_bowBend>3 trigger>3 plate>3 hammer>3 bowString>3 L_bowString_help_jnt>5 R_bowString_help_jnt>4 L_bowString_help_end_jnt>10 R_bowString_help_end_jnt>11 bolt>3 bolt_helper>14"},
    {"leadcrow",
     "root>-1 translate>0 anim>1 cog>2 L_bowBend>3 R_bowBend>3 trigger>3 spool>3 bowString>3 L_bowString_helper_jnt>4 R_bowString_helper_jnt>5 L_bowString_helper_end_jnt>9 R_bowString_helper_end_jnt>10 bolt>3 bolt_helper>13"},
    {"pus_spiter",
     "root>-1 translate>0 anim>1 cog>2 L_bowBend>3 R_bowBend>3 trigger>3 spool>3 clip>3 bowString>3 cog_helper>3 L_bowString_helper_jnt>4 R_bowString_helper_jnt>5 L_bowString_helper_end_jnt>11 R_bowString_helper_end_jnt>12 bolt>3 bolt_helper>15"},
    {"doomspittle",
     "root>-1 translate>0 anim>1 cog>2 lever>3 magazine_driven_jnt>4 R_bowBend>3 L_bowBend>3 bowString>3 lever_driven_jnt>3 magazin_driven_end_jnt>5 L_bowString_helper_jnt>7 R_bowString_helper_jnt>6 L_bowString_helper_end_jnt>11 R_bowString_helper_end_jnt>12 bolt>3 bolt_helper>15"},
};

struct Skeleton {
    std::string model;
    std::vector<std::string> names;
    std::vector<int> parents;
    int count() const { return (int)names.size(); }
};

Skeleton Parse(const Spec& s) {
    Skeleton k;
    k.model = s.model;
    std::string all = s.bones, tok;
    for (size_t i = 0; i <= all.size(); ++i) {
        if (i == all.size() || all[i] == ' ') {
            if (!tok.empty()) {
                const size_t gt = tok.rfind('>');
                k.names.push_back(tok.substr(0, gt));
                k.parents.push_back(std::atoi(tok.c_str() + gt + 1));
                tok.clear();
            }
        } else {
            tok += all[i];
        }
    }
    return k;
}

bool Resolve(const Skeleton& k, Resolved* r, const char** why = nullptr) {
    std::vector<std::string_view> views;
    for (const std::string& n : k.names) views.push_back(n);
    return bowrig::Resolve(views.data(), k.parents.data(), k.count(), r, why);
}

// A bow lying like a bow, in its own units (scaled): y up, the archer behind it (-z), the cog at the grip, the limb hinges 0.4 either
// side, the string tied 2.2 either side and 0.6 behind the grip, resting there; the geometry follows from what each bone is for.
// Bones carry turns of their own (the real ones do), so that "turns with" is not the same as "is unchanged".
struct Synthetic {
    Skeleton skel;
    Resolved r;
    Mat bind[kMaxBones];
    Mat cogWorld;
    V3 local[kMaxBones];
};

Mat Orientation(int i) {
    static const float kDeg[][3] = {{0, 0, 0}, {90, 0, 90}, {0, 90, 0}, {90, 90, 0}, {0, 0, 180}, {35, 20, 10}};
    const float* d = kDeg[(i * 5 + 1) % 6];
    return Mul(TurnX(d[0]), Mul(TurnY(d[1]), TurnZ(d[2])));
}

bool MakeBow(const Spec& spec, Synthetic* out, float scale = 1.0f, Mat cogWorld = Mul(TurnY(25.0f), Translation({0.3f, 1.1f, -0.4f}))) {
    Synthetic& s = *out;
    s.skel = Parse(spec);
    if (!Resolve(s.skel, &s.r)) return false;
    s.cogWorld = cogWorld;
    const Resolved& r = s.r;
    const V3 nock = {0, 0, -0.60f};
    int seq = 0;
    for (int i = 0; i < r.count; ++i) {
        const int sd = r.side[i] >= 0 ? r.side[i] : 0;
        const float up = sd == 0 ? 1.0f : -1.0f;
        V3 p = {0, 0, 0};
        switch (r.role[i]) {
            case kBend:       p = {0, 0.40f * up, 0.05f}; break;
            case kLimb:       p = {0, (0.8f + 0.35f * (float)(seq++ % 4)) * up, -0.15f}; break;
            case kTip:        p = {0, 2.2f * up, -0.60f}; break;
            case kTipFollow:  p = {0, (2.2f - 0.4f * (float)(1 + seq++ % 3)) * up, -0.60f}; break;
            case kNock:       p = nock; break;
            case kNockHelper: p = nock; break;
            case kNockFollow: p = nock; break;
            case kArrow:      p = {0, 0, -0.58f}; break;
            default:          p = i == r.cog ? V3{0, 0, 0} : V3{0.05f * (float)(i % 3), -0.7f + 0.01f * (float)i, 0.08f}; break;   // the cog; feathers, a second cog...
        }
        s.local[i] = p * scale;
        // root, translate and anim sit on the identity; the cog is the cog; the rest carry a turn of their own
        const Mat turn = i < r.cog ? Identity() : i == r.cog ? Identity() : Orientation(i);
        Mat m = Mul(turn, Translation(s.local[i]));
        s.bind[i] = i < r.cog ? Identity() : Mul(m, cogWorld);
    }
    return true;
}

// ---- the hunter's bow as the first version of this file posed it: one rig, 13 bones, fixed. Kept to show that the general
// ---- solver does the same on it.
namespace ref13 {
enum { Root, Translate, Anim, Cog, UpperBend, LowerBend, LowerString, UpperString, BowString, Joint2, Joint1, Arrow, ArrowHelper, Count };
struct Rig {
    Mat rest[Count];
    V3 restNock, back;
    float span = 0;
    bool Learn(const Mat* bind) {
        Mat inverseCog;
        if (!Inverse(bind[Cog], &inverseCog)) return false;
        for (int i = 0; i < Count; ++i) rest[i] = Mul(bind[i], inverseCog);
        restNock = Position(rest[BowString]);
        const V3 upper = Position(rest[UpperString]), lower = Position(rest[LowerString]), up = Unit(upper - lower);
        span = Length(upper - lower);
        const V3 grip = (Position(rest[UpperBend]) + Position(rest[LowerBend])) * 0.5f, offset = restNock - grip;
        back = Unit(offset - up * Dot(offset, up));
        return true;
    }
    bool DrawAt(const Mat& cog, V3 nock, V3 direction, float arrowScale, Mat* out) const {
        V3 tips[2];
        bool reachable = false;
        for (int attempt = 0; attempt < 16 && !reachable; ++attempt) {
            reachable = LimbTip(Position(rest[UpperBend]), Position(rest[UpperString]), restNock, nock, &tips[0]) &&
                        LimbTip(Position(rest[LowerBend]), Position(rest[LowerString]), restNock, nock, &tips[1]);
            if (!reachable) nock = restNock + (nock - restNock) * 0.75f;
        }
        if (!reachable) return false;
        Mat pose[Count];
        for (int i = 0; i < Count; ++i) pose[i] = rest[i];
        static const int bend[2] = {UpperBend, LowerBend}, tip[2] = {UpperString, LowerString}, joint[2] = {Joint1, Joint2};
        for (int side = 0; side < 2; ++side) {
            const V3 hinge = Position(rest[bend[side]]), oldTip = Position(rest[tip[side]]), newTip = tips[side];
            const Mat bendTurn = Rotation(oldTip - hinge, newTip - hinge);
            pose[bend[side]] = Turn(rest[bend[side]], bendTurn, hinge);
            const Mat stringTurn = Rotation(restNock - oldTip, nock - newTip);
            pose[tip[side]] = Turn(rest[tip[side]], stringTurn, newTip);
            pose[joint[side]] = Turn(rest[joint[side]], stringTurn, newTip + Direction(stringTurn, Position(rest[joint[side]]) - oldTip));
        }
        const Mat arrowTurn = Rotation(back * -1.0f, direction);
        pose[BowString] = Turn(rest[BowString], arrowTurn, nock);
        for (int i = Arrow; i < Count; ++i) {
            pose[i] = Turn(rest[i], arrowTurn, nock + Direction(arrowTurn, Position(rest[i]) - restNock));
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 3; ++col) pose[i].m[row * 4 + col] *= arrowScale;
        }
        for (int i = 0; i < Count; ++i) { pose[i] = Mul(pose[i], cog); out[i] = pose[i]; }
        return true;
    }
};
}  // namespace ref13

void TestMath() {
    std::printf("math\n");
    // Rotation takes one direction onto the other, keeps lengths, and is a pure turn
    const V3 dirs[][2] = {{{0, 0, 1}, {1, 0, 0}}, {{1, 2, 3}, {-3, 1, 2}}, {{0, 0, 1}, {0, 0, -1}}, {{0, 1, 0}, {0, 1, 0}}, {{1, 0, 0}, {-1, 0, 0}}};
    for (const auto& d : dirs) {
        const Mat r = Rotation(d[0], d[1]);
        EXPECT(Near(Direction(r, Unit(d[0])), Unit(d[1])), "rotation takes %.1f %.1f %.1f onto %.1f %.1f %.1f", d[0].x, d[0].y, d[0].z, d[1].x, d[1].y, d[1].z);
        const V3 any = {0.3f, -0.8f, 0.5f};
        EXPECT(Near(Length(Direction(r, any)), Length(any)), "length kept");
        Mat inv;
        EXPECT(Inverse(r, &inv) && NearMat(Mul(r, inv), Identity()), "a turn has an inverse");
        EXPECT(Near(r.m[0] * (r.m[5] * r.m[10] - r.m[6] * r.m[9]) + r.m[1] * (r.m[6] * r.m[8] - r.m[4] * r.m[10]) + r.m[2] * (r.m[4] * r.m[9] - r.m[5] * r.m[8]), 1.0f), "a turn, not a mirror");
    }
    // inverse of an affine with scale and a place
    Mat a = Mul(TurnX(33.0f), Mul(TurnY(-71.0f), Translation({1, 2, 3})));
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) a.m[i * 4 + j] *= 1.7f;
    Mat inv;
    EXPECT(Inverse(a, &inv) && NearMat(Mul(a, inv), Identity()) && NearMat(Mul(inv, a), Identity()), "inverse of a scaled, turned, moved matrix");
    Mat bad = Identity(); bad.m[3] = 0.5f;
    EXPECT(!Inverse(bad, &inv) && !Affine(bad), "a projective matrix is not taken");
    bad = Identity(); bad.m[5] = NAN;
    EXPECT(!Affine(bad), "a NaN matrix is not affine");
    Mat flat = Identity(); flat.m[5] = 0.0f;
    EXPECT(!Inverse(flat, &inv), "a flat matrix has no inverse");
    Mat small = Identity();
    for (int i = 0; i < 3; ++i) small.m[i * 5] = 0.01f;
    EXPECT(Inverse(small, &inv) && Near(inv.m[0], 100.0f, 1e-2f), "a matrix in small units has one");
    Mat skew = Identity(); skew.m[5] = 1.0f; skew.m[4] = 1.0f; skew.m[0] = 1.0f; skew.m[1] = 1.0f;   // two rows the same way
    EXPECT(!Inverse(skew, &inv), "rows pointing the same way have none");
}

// ---- layouts
int RoleCount(const Resolved& r, Role role) { int n = 0; for (int i = 0; i < r.count; ++i) n += r.role[i] == role; return n; }

void TestLayouts() {
    std::printf("layouts\n");
    int first = 0, second = 0;
    for (const Spec& spec : kHeroBows) {
        const Skeleton k = Parse(spec);
        Resolved r;
        const char* why = nullptr;
        EXPECT(Resolve(k, &r, &why), "%s is a layout (%s)", spec.model, why ? why : "");
        if (!r.layout) continue;
        (r.layout == &kLayouts[0] ? first : second)++;
        EXPECT(RoleCount(r, kBend) == 2 && RoleCount(r, kTip) == 2 && RoleCount(r, kNock) == 1, "%s: two limbs, two string ends, a nock", spec.model);
        EXPECT(RoleCount(r, kArrow) >= 2, "%s: the arrow and its release helper", spec.model);
        EXPECT(k.names[r.bend[0]].find("pper") != std::string::npos || k.names[r.bend[0]].find("top") != std::string::npos, "%s: the upper limb first (%s)", spec.model, k.names[r.bend[0]].c_str());
        EXPECT(k.parents[r.bend[0]] == r.cog && k.parents[r.bend[1]] == r.cog && k.parents[r.nock] == r.cog, "%s: limbs and nock hang off the cog", spec.model);
        EXPECT(r.Writes(r.bend[0]) && r.Writes(r.arrow) && !r.Writes(r.cog) && !r.Writes(0), "%s: the cog and what is above it are not written", spec.model);
        EXPECT(r.lowest > r.cog && r.highest < r.count && r.lowest <= r.highest, "%s: write range %d..%d", spec.model, r.lowest, r.highest);
        // every bone with a role is below the cog
        for (int i = 0; i < r.count; ++i)
            if (r.role[i] != kNone) EXPECT(i > r.cog, "%s: bone %d (%s) is after the cog", spec.model, i, k.names[i].c_str());
    }
    EXPECT(first == 10 && second == 5, "the layouts: %d + %d of 15", first, second);

    // what the odd bones are
    auto find = [](const Skeleton& k, const char* n) { for (int i = 0; i < k.count(); ++i) if (k.names[i] == n) return i; return -1; };
    {
        const Skeleton k = Parse(kHeroBows[0]);   // hunters_bow
        Resolved r;
        Resolve(k, &r);
        EXPECT(r.role[find(k, "joint1")] == kTipFollow && r.side[find(k, "joint1")] == 0 && r.role[find(k, "joint2")] == kTipFollow && r.side[find(k, "joint2")] == 1, "hunters_bow: the joints follow their string halves");
        EXPECT(r.role[find(k, "arrow_helper")] == kArrow && r.arrow == find(k, "arrow"), "hunters_bow: the release helper is an arrow bone");
    }
    for (const Spec& spec : kHeroBows) {
        const Skeleton k = Parse(spec);
        Resolved r;
        Resolve(k, &r);
        const std::string m = spec.model;
        if (m == "long_battle_bow") {
            EXPECT(r.role[find(k, "upperString_altPivot")] == kLimb && r.role[find(k, "upperString_altPivot_end")] == kLimb, "long_battle_bow: the pivot at the limb's end turns with the limb");
            EXPECT(r.role[find(k, "upperString_end")] == kTipFollow && r.role[find(k, "alt_cog")] == kNone, "long_battle_bow: the string's end follows the string; the second cog is left alone");
        }
        if (m == "short_bow") EXPECT(r.role[find(k, "bowString_upper_helper")] == kNockHelper && r.side[find(k, "bowString_lower_helper")] == 1, "short_bow: two helpers on the nock, upper and lower");
        if (m == "hellclap") EXPECT(r.role[find(k, "arrow_helper")] == kArrow && r.role[find(k, "arrow")] == kArrow, "hellclap: the arrow and its helper both hang off the cog");
        if (m == "reflex_bow" || m == "kuko_shakaku") EXPECT(k.names[r.arrow] == "arrow2", "%s: the arrow is arrow2", m.c_str());
        if (m == "kuko_shakaku") EXPECT(r.role[find(k, "feathers_01")] == kNone, "kuko_shakaku: the feathers are the game's");
        if (m == "pullspite") EXPECT(r.role[find(k, "topFront_leaves")] == kNone, "pullspite: the leaves are the game's");
        if (m == "short_war_bow") EXPECT(k.names[r.bend[0]] == "topBowBend" && k.names[r.bend[1]] == "botBowBend", "short_war_bow: listed bottom first, resolved top first");
        if (m == "stag_bow") EXPECT(r.role[find(k, "lowerString")] == kTip && k.parents[find(k, "lowerString")] == find(k, "lowerBend"), "stag_bow: a string end straight on its limb");
    }

    // not bows with a string to draw: refused, with a reason
    for (const Spec& spec : kOtherBows) {
        const Skeleton k = Parse(spec);
        Resolved r;
        const char* why = nullptr;
        const std::string m = spec.model;
        const bool resolved = Resolve(k, &r, &why);
        if (m == "skeleton_bow" || m == "corrupt_rogue_bow") continue;   // monsters' bows: whatever they are, they are never the hero's
        EXPECT(!resolved && why && *why, "%s is refused (%s)", spec.model, why ? why : "-");
    }
    // a made-up change to a real rig: a name, a parent, a missing arrow, no cog, too small, too big
    auto mutated = [&](const char* from, const char* to) {
        Skeleton k = Parse(kHeroBows[0]);
        for (std::string& n : k.names) if (n == from) n = to;
        Resolved r;
        return Resolve(k, &r);
    };
    EXPECT(!mutated("upperString", "upperRope"), "a renamed string end");
    EXPECT(!mutated("bowString", "sinew"), "a renamed nock");
    EXPECT(!mutated("arrow", "bolt"), "no arrow bone off the cog is no bow");
    EXPECT(mutated("arrow_helper", "bolt_helper"), "a helper under the arrow with another name is still the arrow's");
    EXPECT(!mutated("cog", "centre"), "no cog");
    {
        Skeleton k = Parse(kHeroBows[0]);
        k.parents[10] = 4;   // joint1 moved off its string end to the limb: still a bow (it turns with the limb)
        Resolved r;
        EXPECT(Resolve(k, &r) && r.role[10] == kLimb, "a joint moved to the limb is a limb bone");
        k = Parse(kHeroBows[0]);
        k.parents[7] = 4;   // fine
        k.parents[4] = 5;   // a limb under the other limb: not a bow
        EXPECT(!Resolve(k, &r), "a limb under a limb");
        k = Parse(kHeroBows[0]);
        k.parents[8] = 2;   // the nock above the cog
        EXPECT(!Resolve(k, &r), "the nock not under the cog");
        k = Parse(kHeroBows[0]);
        k.parents[6] = 9;   // a parent after its child
        EXPECT(!Resolve(k, &r), "bones not parents first");
        std::vector<std::string_view> views;
        for (const std::string& n : k.names) views.push_back(n);
        EXPECT(!bowrig::Resolve(nullptr, nullptr, 0, &r) && !bowrig::Resolve(views.data(), k.parents.data(), 3, &r) && !bowrig::Resolve(views.data(), k.parents.data(), kMaxBones + 1, &r), "no skeleton, too few bones, too many");
    }
}

// ---- learning
void TestLearn() {
    std::printf("learn\n");
    for (const Spec& spec : kHeroBows) {
        Synthetic s;
        EXPECT(MakeBow(spec, &s), "%s resolves", spec.model);
        Rig rig;
        const char* why = nullptr;
        EXPECT(rig.Learn(s.r, s.bind, s.r.count, &why) && rig.valid, "%s is learned (%s)", spec.model, why ? why : "");
        EXPECT(Near(rig.span, 4.4f, 1e-3f), "%s: span %.3f", spec.model, rig.span);
        EXPECT(Near(rig.back, V3{0, 0, -1}, 1e-3f), "%s: back %.3f %.3f %.3f is behind the grip", spec.model, rig.back.x, rig.back.y, rig.back.z);
        EXPECT(Near(rig.restNock, V3{0, 0, -0.60f}, 1e-3f), "%s: rest nock", spec.model);
        EXPECT(Near(rig.up, V3{0, 1, 0}, 1e-3f), "%s: up %.3f %.3f %.3f runs along the limbs, lower tip to upper", spec.model, rig.up.x, rig.up.y, rig.up.z);
        EXPECT(Near(rig.reach, 0.65f, 1e-3f), "%s: nock %.3f behind the grip", spec.model, rig.reach);
        {   // [bow] arrow_rest_model: the arrow's line crosses the riser on the nock's own line, in the grip's plane - not behind or ahead of the bow
            V3 rest;
            const V3 grip = (Position(rig.rest[rig.r.bend[0]]) + Position(rig.rest[rig.r.bend[1]])) * 0.5f;
            EXPECT(ArrowRestPoint(rig, &rest) && Near(Dot(rest - grip, rig.back), 0.0f, 1e-3f) && Near(rest - rig.restNock, rig.back * -rig.reach, 1e-3f),
                   "%s: the arrow rest %.3f %.3f %.3f is where the nock's line meets the riser", spec.model, rest.x, rest.y, rest.z);
        }
        // other units, the cog turned over
        for (const float scale : {0.01f, 3.0f, 40.0f}) {
            Synthetic t;
            Rig r;
            EXPECT(MakeBow(spec, &t, scale) && r.Learn(t.r, t.bind, t.r.count), "%s learned at scale %.2f", spec.model, scale);
            EXPECT(Near(r.back, V3{0, 0, -1}, 1e-3f), "%s: back at scale %.2f", spec.model, scale);
        }
        Synthetic t;
        Rig r;
        EXPECT(MakeBow(spec, &t, 1.0f, Mul(TurnX(80.0f), Mul(TurnY(140.0f), Translation({-5, 0, 9})))) && r.Learn(t.r, t.bind, t.r.count), "%s with the cog turned over", spec.model);
    }

    // the layouts know which way their bows lie: +z forward, +y along the limbs; a bind pose lying another way is refused
    for (const Layout& l : kLayouts) {
        EXPECT(Near(Length({l.forward[0], l.forward[1], l.forward[2]}), 1.0f) && Near(Length({l.up[0], l.up[1], l.up[2]}), 1.0f) &&
               Near(l.forward[0] * l.up[0] + l.forward[1] * l.up[1] + l.forward[2] * l.up[2], 0.0f), "layout '%s': unit axes across each other", l.name);
    }
    {
        Synthetic sideways;
        MakeBow(kHeroBows[0], &sideways);
        // the same bow with its geometry turned a quarter about the cog's x: the limbs along z, the string beside - not this layout's
        const Mat q = TurnX(90.0f);
        for (int i = sideways.r.cog + 1; i < sideways.r.count; ++i) {
            const V3 p = Direction(q, sideways.local[i]);
            sideways.bind[i] = Mul(Mul(Orientation(i), Translation(p)), sideways.cogWorld);
        }
        Rig r;
        const char* why = nullptr;
        EXPECT(!r.Learn(sideways.r, sideways.bind, sideways.r.count, &why) && why, "a bow lying another way: %s", why ? why : "");
    }
    // not a bow's bind pose
    Synthetic s;
    MakeBow(kHeroBows[0], &s);
    Rig r;
    const char* why = nullptr;
    Synthetic b = s;
    b.bind[b.r.bend[1]] = Mat{};
    EXPECT(!r.Learn(b.r, b.bind, b.r.count, &why) && !r.valid && why, "a limb bone without an inverse: %s", why ? why : "");
    b = s; b.bind[b.r.tip[0]].m[13] = NAN;
    EXPECT(!r.Learn(b.r, b.bind, b.r.count) && !r.valid, "a NaN bone");
    b = s; b.bind[b.r.cog] = Mat{};
    EXPECT(!r.Learn(b.r, b.bind, b.r.count), "no cog");
    b = s;
    for (int i = b.r.cog; i < b.r.count; ++i) b.bind[i] = b.cogWorld;   // every bone at one point
    EXPECT(!r.Learn(b.r, b.bind, b.r.count), "all bones at one point");
    b = s; b.bind[b.r.nock] = Mul(Translation({0, 0, 0.05f}), b.cogWorld);   // the nock in the grip: nothing behind it
    why = nullptr;
    EXPECT(!r.Learn(b.r, b.bind, b.r.count, &why) && why, "no string behind the grip (%s)", why ? why : "");
    b = s; b.bind[b.r.nock] = Mul(Translation({0, 0.30f, 0.05f}), b.cogWorld);   // the nock on the limbs' axis, off the grip
    EXPECT(!r.Learn(b.r, b.bind, b.r.count), "a nock on the limbs' axis has no side to be behind");
    b = s;   // limbs too short for the string: their circles never meet the draw
    b.bind[b.r.tip[0]] = Mul(Translation({0, 0.10f, -0.60f}), b.cogWorld);
    b.bind[b.r.tip[1]] = Mul(Translation({0, -0.10f, -0.60f}), b.cogWorld);
    EXPECT(!r.Learn(b.r, b.bind, b.r.count, &why), "limbs shorter than the draw can fold");
    // a bone that has no role may be as broken as it likes
    b = s;
    for (int i = 0; i < b.r.count; ++i) if (b.r.role[i] == kNone && i > b.r.cog) b.bind[i] = Mat{};
    EXPECT(r.Learn(b.r, b.bind, b.r.count), "a bone with no role does not matter");
    EXPECT(!r.Learn(b.r, b.bind, b.r.count - 1) && !r.Learn(Resolved{}, b.bind, b.r.count) && !r.Learn(b.r, nullptr, b.r.count), "a count that is not the layout's, no layout, no bones");
    // a failed learn clears the rig
    Rig used;
    used.Learn(s.r, s.bind, s.r.count);
    EXPECT(used.valid, "learned");
    used.Learn(b.r, b.bind, b.r.count - 1);
    EXPECT(!used.valid, "a failed learn clears the rig");
    Pose out;
    EXPECT(!used.Draw(s.cogWorld, 0.5f, 1.0f, &out) && !used.Released(s.cogWorld, &out), "an unlearned rig poses nothing");
}

// ---- the pose, on every bow
void CheckBow(const Spec& spec) {
    Synthetic s;
    MakeBow(spec, &s);
    const char* m = spec.model;
    const Resolved& R = s.r;
    Rig rig;
    rig.Learn(R, s.bind, R.count);
    Pose sentinel{};
    for (int i = 0; i < kMaxBones; ++i) for (int j = 0; j < 16; ++j) sentinel[i].m[j] = 7.0f + (float)j;   // what the game left there
    Pose rest = sentinel, full = sentinel, moved = sentinel;

    // no pull is the bind pose, at the bind cog and at a moved cog; bones with no role are not written
    EXPECT(rig.Draw(s.cogWorld, 0.0f, 1.0f, &rest), "%s: rest pose", m);
    for (int i = 0; i < R.count; ++i) {
        if (R.role[i] == kNone) { EXPECT(NearMat(rest[i], sentinel[i], 0.0f), "%s: bone %d (%s) has no role and is not written", m, i, s.skel.names[i].c_str()); continue; }
        EXPECT(NearMat(rest[i], s.bind[i], 2e-4f), "%s: bone %d (%s) at rest is its bind", m, i, s.skel.names[i].c_str());
    }
    const Mat cog2 = Mul(TurnX(-20.0f), Mul(TurnY(-80.0f), Translation({2, 0.5f, 7})));
    EXPECT(rig.Draw(cog2, 0.0f, 1.0f, &moved), "%s: rest pose, cog moved", m);
    for (int i = 0; i < R.count; ++i)
        if (R.role[i] != kNone) EXPECT(Near(Position(moved[i]), Point(cog2, s.local[i]), 2e-4f), "%s: bone %d follows the cog", m, i);

    // a full pull
    EXPECT(rig.Draw(s.cogWorld, 1.0f, 1.0f, &full), "%s: full draw", m);
    const V3 nockNow = Position(full[R.nock]);
    EXPECT(Near(nockNow, Point(s.cogWorld, rig.NockAt(1.0f)), 2e-4f), "%s: nock at the hand", m);
    EXPECT(Near(Dist(nockNow, Position(rest[R.nock])), 4.4f * kFullDraw, 2e-4f), "%s: pulled %.3f", m, Dist(nockNow, Position(rest[R.nock])));
    V3 hinge[2], oldTip[2];
    Mat turn[2], stringTurn[2];
    for (int sd = 0; sd < 2; ++sd) {
        hinge[sd] = Position(rest[R.bend[sd]]);
        oldTip[sd] = Position(rest[R.tip[sd]]);
        const float limb = Dist(oldTip[sd], hinge[sd]), str = Dist(oldTip[sd], Position(rest[R.nock]));
        const V3 newTip = Position(full[R.tip[sd]]);
        EXPECT(Near(Dist(newTip, hinge[sd]), limb, 3e-4f), "%s: limb %d keeps its length", m, sd);
        EXPECT(Near(Dist(newTip, nockNow), str, 3e-4f), "%s: string half %d keeps its length", m, sd);
        EXPECT(Near(Position(full[R.bend[sd]]), hinge[sd], 2e-4f), "%s: the hinge %d does not move", m, sd);
        EXPECT(Dist(newTip, oldTip[sd]) > 0.05f, "%s: tip %d moved", m, sd);
        const V3 inward = newTip - oldTip[sd];
        EXPECT(Dot(inward, Direction(s.cogWorld, V3{0, sd == 0 ? -1.0f : 1.0f, 0})) > 0.0f, "%s: tip %d folds inward", m, sd);
        EXPECT(Dot(inward, Direction(s.cogWorld, rig.back)) > 0.0f, "%s: tip %d comes toward the archer", m, sd);
        // the turn the limb made: a pure turn taking the old tip's way from the hinge to the new
        Mat inv;
        Inverse(rest[R.bend[sd]], &inv);
        turn[sd] = Mul(inv, full[R.bend[sd]]);
        SetPosition(turn[sd], V3{});
        EXPECT(Near(Direction(turn[sd], Unit(oldTip[sd] - hinge[sd])), Unit(newTip - hinge[sd]), 5e-4f), "%s: limb %d turned its tip to the new place", m, sd);
        float stay = 1.0f;   // the limb's own axes: at least one of them left where it was
        for (const V3 axis : {V3{1, 0, 0}, V3{0, 1, 0}, V3{0, 0, 1}}) stay = std::min(stay, Dot(Direction(full[R.bend[sd]], axis), Direction(rest[R.bend[sd]], axis)));
        EXPECT(stay < 0.9999f, "%s: limb %d turned", m, sd);
        Inverse(rest[R.tip[sd]], &inv);
        stringTurn[sd] = Mul(inv, full[R.tip[sd]]);
        SetPosition(stringTurn[sd], V3{});
    }
    for (int i = 0; i < R.count; ++i) {
        const int sd = R.side[i];
        Mat inv;
        Inverse(rest[i], &inv);
        Mat t = Mul(inv, full[i]);
        SetPosition(t, V3{});
        switch (R.role[i]) {
            case kLimb:
                EXPECT(Near(Dist(Position(full[i]), hinge[sd]), Dist(Position(rest[i]), hinge[sd]), 3e-4f), "%s: %s (turns with limb %d) keeps its distance from the hinge", m, s.skel.names[i].c_str(), sd);
                EXPECT(NearMat(t, turn[sd], 5e-4f), "%s: %s turns as the limb does", m, s.skel.names[i].c_str());
                break;
            case kTipFollow:
                EXPECT(Near(Dist(Position(full[i]), Position(full[R.tip[sd]])), Dist(Position(rest[i]), oldTip[sd]), 3e-4f), "%s: %s (on the string %d) keeps its place on it", m, s.skel.names[i].c_str(), sd);
                EXPECT(NearMat(t, stringTurn[sd], 5e-4f), "%s: %s turns as the string does", m, s.skel.names[i].c_str());
                break;
            case kNockFollow:
            case kNockHelper:
                EXPECT(Near(Position(full[i]), nockNow, 2e-4f), "%s: %s sits on the nock", m, s.skel.names[i].c_str());
                break;
            case kArrow:
                EXPECT(Near(Position(full[i]) - nockNow, Position(rest[i]) - Position(rest[R.nock]), 2e-4f), "%s: the arrow bone %s sits on the string as it did", m, s.skel.names[i].c_str());
                EXPECT(Near(Rows(full[i]), Rows(rest[i]), 2e-4f), "%s: %s is shown at its size", m, s.skel.names[i].c_str());
                break;
            default: break;
        }
    }
    EXPECT(Dist(Position(full[R.tip[0]]), Position(full[R.tip[1]])) < 4.4f - 0.05f, "%s: the tips come together", m);

    // every draw is a pose again, not a state: let go and it is at rest at once
    Pose again = sentinel;
    EXPECT(rig.Draw(s.cogWorld, 0.0f, 1.0f, &again), "%s: let go", m);
    for (int i = 0; i < R.count; ++i)
        if (R.role[i] != kNone) EXPECT(NearMat(again[i], rest[i], 1e-5f), "%s: bone %d snaps back", m, i);
    // a hand off the axis: the nock is exactly there, the arrow lies along the aim, the string halves keep their length
    Pose p = sentinel;
    const V3 hand = rig.restNock + rig.back * 0.22f + V3{0.04f, -0.03f, 0.0f};
    const V3 aim = Unit(rig.back * -1.0f + V3{-0.15f, 0.08f, 0.0f});
    EXPECT(rig.DrawAt(s.cogWorld, hand, aim, 1.0f, &p), "%s: a hand off the axis", m);
    EXPECT(Near(Position(p[R.nock]), Point(s.cogWorld, hand), 2e-4f), "%s: the nock is at the hand", m);
    for (int sd = 0; sd < 2; ++sd) {
        EXPECT(Near(Dist(Position(p[R.tip[sd]]), Position(p[R.bend[sd]])), Dist(Position(rest[R.tip[sd]]), Position(rest[R.bend[sd]])), 3e-4f), "%s: limb %d off axis", m, sd);
        EXPECT(Near(Dist(Position(p[R.tip[sd]]), Position(p[R.nock])), Dist(Position(rest[R.tip[sd]]), Position(rest[R.nock])), 3e-4f), "%s: string half %d off axis", m, sd);
    }
    // the arrow bones' positions relative to the nock are turned to the aim as one body
    for (int i = 0; i < R.count; ++i)
        if (R.role[i] == kArrow) EXPECT(Near(Dist(Position(p[i]), Position(p[R.nock])), Dist(Position(rest[i]), Position(rest[R.nock])), 3e-4f), "%s: %s keeps its distance from the nock", m, s.skel.names[i].c_str());

    // out of reach: pulled toward rest, a complete finite pose
    for (const V3 far : {V3{0, 0, -40.0f}, V3{30, 30, 30}, V3{0, 0.5f, -3.0f}, V3{0, 0, 0}, rig.restNock + V3{0, 0, 0.9f}}) {
        Pose q = sentinel;
        EXPECT(rig.DrawAt(s.cogWorld, far, rig.back * -1.0f, 1.0f, &q), "%s: out of reach %.1f %.1f %.1f", m, far.x, far.y, far.z);
        bool finite = true;
        for (int i = 0; i < R.count; ++i) if (R.role[i] != kNone) finite = finite && Affine(q[i]);
        EXPECT(finite, "%s: finite pose out of reach", m);
        for (int sd = 0; sd < 2; ++sd)
            EXPECT(Near(Dist(Position(q[R.tip[sd]]), Position(q[R.bend[sd]])), Dist(Position(rest[R.tip[sd]]), Position(rest[R.bend[sd]])), 1e-3f), "%s: limb %d keeps its length out of reach", m, sd);
    }

    // arrow hidden: all the arrow's bones lose their size (the release helper with them) and keep their places
    Pose hidden = sentinel;
    EXPECT(rig.Draw(s.cogWorld, 0.5f, kArrowHidden, &hidden) && rig.Draw(s.cogWorld, 0.5f, 1.0f, &p), "%s: arrow hidden", m);
    for (int i = 0; i < R.count; ++i) {
        if (R.role[i] == kNone) continue;
        const float k = Rows(hidden[i]) / Rows(p[i]);
        EXPECT(Near(k, R.role[i] == kArrow ? kArrowHidden : 1.0f, 1e-5f), "%s: bone %d size x%.4f", m, i, k);
        EXPECT(Near(Position(hidden[i]), Position(p[i]), 1e-5f), "%s: bone %d stays put hidden", m, i);
    }

    // the release: strings and limbs at rest, the arrow bones at the grip and hidden, nothing else written
    Pose rel = sentinel;
    EXPECT(rig.Released(s.cogWorld, &rel), "%s: released", m);
    for (int i = 0; i < R.count; ++i) {
        if (R.role[i] == kNone) { EXPECT(NearMat(rel[i], sentinel[i], 0.0f), "%s: bone %d not written on the release", m, i); continue; }
        if (R.role[i] == kArrow) {
            EXPECT(Near(Rows(rel[i]), Rows(rest[i]) * kArrowHidden, 1e-5f), "%s: %s hidden after the release", m, s.skel.names[i].c_str());
            const V3 inFrame = Position(rel[i]) - Position(rest[i]);   // moved from the nock to the grip: straight forward by the nock's reach
            EXPECT(Near(inFrame, Direction(s.cogWorld, rig.back * -rig.reach), 2e-4f), "%s: %s at the grip", m, s.skel.names[i].c_str());
        } else {
            EXPECT(NearMat(rel[i], rest[i], 1e-5f), "%s: bone %d at rest after the release", m, i);
        }
    }

    // refused inputs leave the output alone
    Pose keep = sentinel, out = sentinel;
    EXPECT(!rig.DrawAt(s.cogWorld, V3{NAN, 0, 0}, rig.back * -1.0f, 1.0f, &out) && !rig.DrawAt(s.cogWorld, V3{INFINITY, 0, 0}, rig.back * -1.0f, 1.0f, &out), "%s: NaN or infinite nock", m);
    EXPECT(!rig.DrawAt(s.cogWorld, hand, V3{0, 0, 0}, 1.0f, &out) && !rig.DrawAt(s.cogWorld, hand, V3{0, NAN, 1}, 1.0f, &out), "%s: no direction, NaN direction", m);
    EXPECT(!rig.Draw(s.cogWorld, NAN, 1.0f, &out) && !rig.DrawAt(s.cogWorld, hand, rig.back, NAN, &out) && !rig.DrawAt(s.cogWorld, hand, rig.back, -1.0f, &out), "%s: NaN amount, NaN or negative arrow size", m);
    Mat badCog = s.cogWorld; badCog.m[14] = NAN;
    EXPECT(!rig.DrawAt(badCog, hand, rig.back, 1.0f, &out) && !rig.Released(badCog, &out), "%s: NaN cog", m);
    badCog = s.cogWorld; badCog.m[15] = 2.0f;
    EXPECT(!rig.DrawAt(badCog, hand, rig.back, 1.0f, &out), "%s: projective cog", m);
    EXPECT(!rig.DrawAt(s.cogWorld, hand, rig.back, 1.0f, nullptr) && !rig.Released(s.cogWorld, nullptr), "%s: no output", m);
    for (int i = 0; i < kMaxBones; ++i) EXPECT(NearMat(out[i], keep[i], 0.0f), "%s: bone %d untouched by a refused pose", m, i);
    // an arrow turned round and straight up are still poses
    EXPECT(rig.DrawAt(s.cogWorld, hand, rig.back, 1.0f, &p) && rig.DrawAt(s.cogWorld, hand, V3{0, 1, 0}, 1.0f, &p), "%s: odd aims", m);

    // the pull grows with the amount, and is clamped
    float last = -1.0f;
    for (int k = 0; k <= 10; ++k) {
        EXPECT(rig.Draw(s.cogWorld, (float)k / 10.0f, 1.0f, &p), "%s: draw %d/10", m, k);
        const float d = Dist(Position(p[R.nock]), Position(rest[R.nock]));
        EXPECT(d >= last - 1e-5f, "%s: draw %d/10 is not shorter than the one before", m, k);
        last = d;
    }
    Pose over = sentinel, under = sentinel;
    EXPECT(rig.Draw(s.cogWorld, 7.0f, 1.0f, &over) && rig.Draw(s.cogWorld, -3.0f, 1.0f, &under), "%s: out-of-range amounts", m);
    for (int i = 0; i < R.count; ++i)
        if (R.role[i] != kNone) EXPECT(NearMat(over[i], full[i], 1e-5f) && NearMat(under[i], rest[i], 1e-5f), "%s: bone %d: amounts clamp to 0..1", m, i);

    // the cancelled pull: eased from where it was drawn back to rest, finite all the way
    const V3 drawnNock = rig.NockAt(0.7f), drawnDir = Unit(rig.back * -1.0f + V3{0.1f, 0.05f, 0});
    for (uint64_t t = 0; t <= kCancelMs + 20; t += 10) {
        Pose c = sentinel;
        EXPECT(rig.DrawAt(s.cogWorld, EasedNock(drawnNock, rig.restNock, t), EasedDirection(drawnDir, rig.back * -1.0f, t), 1.0f, &c), "%s: cancelled pull at %llu ms", m, (unsigned long long)t);
        if (t >= kCancelMs) EXPECT(Near(Position(c[R.nock]), Position(rest[R.nock]), 1e-4f), "%s: back at rest at the end", m);
    }
}

void TestEveryBow() {
    for (const Spec& spec : kHeroBows) {
        std::printf("pose: %s\n", spec.model);
        CheckBow(spec);
    }
}

// ---- the hunter's bow, as the first version posed it
void TestHuntersAsBefore() {
    std::printf("the hunter's bow as before\n");
    Synthetic s;
    MakeBow(kHeroBows[0], &s);
    EXPECT(std::string(s.skel.model) == "hunters_bow" && s.r.count == 13, "the first of the table is the hunter's bow");
    Rig rig;
    rig.Learn(s.r, s.bind, 13);
    ref13::Rig old;
    old.Learn(s.bind);
    EXPECT(Near(old.restNock, rig.restNock) && Near(old.back, rig.back) && Near(old.span, rig.span), "learned the same: nock, back, span");
    // 400 draws: amounts, hands off the axis, out of reach, turned aims, a moved cog
    unsigned seed = 12345u;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 8) / (float)(1u << 24); };
    int compared = 0;
    for (int n = 0; n < 400; ++n) {
        const Mat cog = Mul(TurnX(rnd() * 90.0f - 45.0f), Mul(TurnY(rnd() * 360.0f), Translation({rnd() * 4 - 2, rnd() * 2, rnd() * 4 - 2})));
        const V3 nock = n % 5 == 4 ? V3{rnd() * 60 - 30, rnd() * 60 - 30, rnd() * 60 - 30}   // out of reach
                                   : old.restNock + old.back * (rnd() * 2.2f) + V3{rnd() * 0.5f - 0.25f, rnd() * 0.5f - 0.25f, rnd() * 0.3f - 0.15f};
        const V3 aim = Unit(V3{rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f} + old.back * -1.0f * (n % 3 ? 3.0f : 0.0f));
        if (Length(aim) < 0.5f) continue;
        Mat ref[ref13::Count];
        Pose now{};
        const bool a = old.DrawAt(cog, nock, aim, 1.0f, ref), b = rig.DrawAt(cog, nock, aim, 1.0f, &now);
        EXPECT(a == b, "draw %d: the same answer (%d, %d)", n, a, b);
        if (!a || !b) continue;
        ++compared;
        for (int i = ref13::UpperBend; i < ref13::Count; ++i) EXPECT(NearMat(ref[i], now[i], 2e-4f), "draw %d: bone %d is what it was", n, i);
    }
    EXPECT(compared > 350, "%d draws compared", compared);
}

// ---- the frame chain
void TestFrames() {
    std::printf("frames\n");
    Synthetic s;
    MakeBow(kHeroBows[0], &s);
    // The hero's model space: the bow's cog now, its entity's offset from the hand's attach bone, the bone itself
    const Mat cog = Mul(TurnY(-15.0f), Translation({0.0f, 1.2f, 0.1f}));
    const Mat itemLocal = Mul(TurnX(12.0f), Translation({0.02f, -0.01f, 0.05f}));
    Mat attach = Mul(TurnY(70.0f), Mul(TurnX(-30.0f), Translation({0.6f, 1.0f, 0.4f})));
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) attach.m[i * 4 + j] *= 1.8f;   // model units are not metres
    const Mat toHero = Mul(cog, Mul(itemLocal, attach));

    // a nock and an aim chosen in the cog's frame, the hands they come from in the hero's
    const V3 nockCog = {0.02f, -0.01f, -0.45f}, bowHandCog = {0.0f, 0.0f, 0.15f};
    const V3 stringHand = Point(toHero, nockCog), bowHand = Point(toHero, bowHandCog);
    V3 nock, direction;
    EXPECT(ToRigFrame(cog, itemLocal, attach, stringHand, bowHand, &nock, &direction), "frame chain");
    EXPECT(Near(nock, nockCog), "nock back in the cog's frame %.4f %.4f %.4f", nock.x, nock.y, nock.z);
    EXPECT(Near(Unit(direction), Unit(bowHandCog - nockCog)), "direction from the string hand to the bow hand");

    Mat flat = attach;
    for (int j = 0; j < 3; ++j) flat.m[4 + j] = 0.0f;   // a row of the turn gone
    EXPECT(!ToRigFrame(cog, itemLocal, flat, stringHand, bowHand, &nock, &direction), "an attach bone with no inverse");
    EXPECT(!ToRigFrame(cog, itemLocal, attach, V3{NAN, 0, 0}, bowHand, &nock, &direction), "a hand with NaN");
    EXPECT(!ToRigFrame(cog, itemLocal, attach, stringHand, bowHand, nullptr, &direction), "no output");

    // end to end: the string hand in the hero's space is where the bow's string ends up there
    for (const Spec& spec : kHeroBows) {
        Synthetic t;
        MakeBow(spec, &t);
        Rig rig;
        rig.Learn(t.r, t.bind, t.r.count);
        V3 n, d;
        ToRigFrame(cog, itemLocal, attach, stringHand, bowHand, &n, &d);
        Pose p{};
        EXPECT(rig.DrawAt(cog, n, d, 1.0f, &p), "%s: pose from the hero's hands", spec.model);
        const V3 stringNow = Point(Mul(itemLocal, attach), Position(p[t.r.nock]));
        EXPECT(Near(stringNow, stringHand, 1e-3f), "%s: the string is at the string hand in the hero's space: %.4f %.4f %.4f, hand %.4f %.4f %.4f", spec.model,
               stringNow.x, stringNow.y, stringNow.z, stringHand.x, stringHand.y, stringHand.z);
    }
}

void TestHold() {
    std::printf("hold\n");
    EXPECT(!Holding(0, 1000), "never drawn: the game's animation");
    EXPECT(Holding(1100, 1100), "drawing");
    EXPECT(Holding(1100, 1150), "just let go: still held at rest");
    EXPECT(Holding(1100, 1100 + kHoldMs - 1), "held for the whole hold");
    EXPECT(!Holding(1100, 1100 + kHoldMs), "then the game's animation again");
    EXPECT(!Holding(1100, 5000), "and stays so");
    EXPECT(!Holding(6000, 100), "a clock gone back does not hold it");
    // the cancel's ease: 0 at the start, 1 at the end, never backwards
    EXPECT(Near(EaseOut(0), 0.0f) && Near(EaseOut(kCancelMs), 1.0f) && Near(EaseOut(kCancelMs * 3), 1.0f), "the ease runs 0 to 1 in kCancelMs");
    float prev = -1.0f;
    for (uint64_t t = 0; t <= kCancelMs; ++t) { const float e = EaseOut(t); EXPECT(e >= prev, "the ease is monotonic at %llu", (unsigned long long)t); prev = e; }
    EXPECT(Near(EaseOut(kCancelMs / 2), 0.5f, 0.02f) && EaseOut(kCancelMs / 4) > 0.05f && EaseOut(kCancelMs / 4) < 0.3f && EaseOut(kCancelMs * 3 / 4) > 0.7f, "the ease is smooth: not a step, not a line");
    EXPECT(kCancelMs >= 100 && kCancelMs <= 150, "the cancel takes 100 to 150 ms");
    EXPECT(ShortPull(0.0f) && ShortPull(0.1f) && ShortPull(0.249f) && !ShortPull(0.25f) && !ShortPull(0.27f) && !ShortPull(1.0f) && !ShortPull(NAN), "a pull under 0.25 is too short to shoot");
    EXPECT(Near(EasedNock({0, 0, -2}, {0, 0, -0.6f}, 0), V3{0, 0, -2}) && Near(EasedNock({0, 0, -2}, {0, 0, -0.6f}, kCancelMs), V3{0, 0, -0.6f}), "the eased nock goes from the drawn place to rest");
    EXPECT(Near(EasedDirection({1, 0, 0}, {-1, 0, 0}, kCancelMs / 2), EasedDirection({1, 0, 0}, {-1, 0, 0}, kCancelMs / 2)) && Length(EasedDirection({1, 0, 0}, {-1, 0, 0}, kCancelMs / 2)) > 0.9f, "opposite directions still give a direction");
}

// CanReach: whether the limbs can still meet the string with the nock at a place (a nock lifted off the arm must not be asked past that edge)
void TestReach() {
    std::printf("reach\n");
    for (const Spec& spec : kHeroBows) {
        Synthetic t;
        MakeBow(spec, &t);
        Rig rig;
        EXPECT(rig.Learn(t.r, t.bind, t.r.count), "%s: learned", spec.model);
        EXPECT(rig.CanReach(rig.restNock) && rig.CanReach(rig.NockAt(1.0f)), "%s: rest and a full draw are within reach", spec.model);
        EXPECT(!rig.CanReach(rig.NockAt(1.0f) + rig.back * 100.0f) && !rig.CanReach(V3{NAN, 0, 0}), "%s: far off and NaN are not", spec.model);
        // what DrawAt does with a nock that CanReach says is within reach: it is not pulled back
        const V3 nock = rig.NockAt(0.8f) + rig.up * 0.1f;
        if (rig.CanReach(nock)) {
            const Mat cog = Mul(TurnY(-15.0f), Translation({0.0f, 1.2f, 0.1f}));
            Pose p{};
            EXPECT(rig.DrawAt(cog, nock, rig.back * -1.0f, 1.0f, &p), "%s: a nock within reach is drawn", spec.model);
            const Mat expect = Mul(Mul(Rotation(rig.back * -1.0f, rig.back * -1.0f), Translation(nock)), cog);
            EXPECT(Near(Position(p[t.r.nock]), Position(expect), 1e-3f), "%s: and drawn where it was asked", spec.model);
        }
    }
    EXPECT(!Rig{}.CanReach({0, 0, 0}), "an unlearned rig reaches nothing");
}

}  // namespace

// [bow] arrow_fx: the arrow's ends from the bow's own bones - the nock at the string's rest place; the head the farthest arrow bone along the arrow, or the riser
// when the arrow's bones do not reach past it
void TestArrowEnds() {
    std::printf("arrow ends\n");
    for (const Spec& spec : kHeroBows) {
        Synthetic s;
        Rig rig;
        EXPECT(MakeBow(spec, &s) && rig.Learn(s.r, s.bind, s.r.count), "%s learned", spec.model);
        ArrowEnds e;
        EXPECT(ArrowEndsOf(rig, &e), "%s: its arrow's ends", spec.model);
        EXPECT(Near(e.nock, rig.restNock, 1e-5f), "%s: the nock is the string's rest place", spec.model);
        // the synthetic arrow bones sit 2 cm ahead of the nock, short of the riser: the head is the riser (the nock + reach, forward)
        EXPECT(!e.fromBones && Near(e.head, rig.restNock + rig.back * -rig.reach, 1e-4f), "%s: bones short of the riser - the head at the riser (%.3f %.3f %.3f)",
               spec.model, e.head.x, e.head.y, e.head.z);
        // an arrow bone far ahead (the model's head bone): the head is that bone's place along the arrow
        int arrowBone = -1;
        for (int i = 0; i < s.r.count; ++i) if (s.r.role[i] == kArrow) { arrowBone = i; break; }
        EXPECT(arrowBone >= 0, "%s: an arrow bone", spec.model);
        if (arrowBone < 0) continue;
        Synthetic t = s;
        t.bind[arrowBone] = Mul(Mul(Orientation(arrowBone), Translation({0.03f, -0.02f, 0.95f})), t.cogWorld);   // 1.55 ahead of the nock, a little off the line
        Rig far;
        EXPECT(far.Learn(t.r, t.bind, t.r.count), "%s: learned with a long arrow", spec.model);
        ArrowEnds f;
        EXPECT(ArrowEndsOf(far, &f) && f.fromBones && f.bone == arrowBone, "%s: the head is the far arrow bone's (bone %d)", spec.model, f.bone);
        EXPECT(Near(f.head, far.restNock + far.back * -1.55f, 1e-3f), "%s: on the arrow's own line, 1.55 ahead (%.3f %.3f %.3f)", spec.model, f.head.x, f.head.y, f.head.z);
    }
    // the arrow mesh's box (the installed bows: z -0.581 .. 2.309 in the bind frame, the nock at -0.60): the head is its far end along the arrow, 2.909 ahead
    for (const Spec& spec : kHeroBows) {
        for (const bool turned : {false, true}) {
            Synthetic s;
            Rig rig;
            const Mat cogWorld = turned ? Mul(TurnY(25.0f), Translation({0.3f, 1.1f, -0.4f})) : Identity();
            EXPECT(MakeBow(spec, &s, 1.0f, cogWorld) && rig.Learn(s.r, s.bind, s.r.count), "%s learned (%s)", spec.model, turned ? "the cog turned and moved" : "the cog at the origin");
            // the box in the bind (model) frame: an axis-aligned box around the arrow in the cog's frame, carried by the cog's bind
            V3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
            for (int c = 0; c < 8; ++c) {
                const V3 p = Point(s.bind[s.r.cog], V3{(c & 1) ? 0.055f : -0.057f, (c & 2) ? 0.079f : -0.079f, (c & 4) ? 2.309f : -0.581f});
                lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
                hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
            }
            const float mn[3] = {lo.x, lo.y, lo.z}, mx[3] = {hi.x, hi.y, hi.z};
            const float ahead = MeshAhead(rig, s.bind[s.r.cog], mn, mx);
            if (!turned) EXPECT(Near(ahead, 2.909f, 1e-3f), "%s: the arrow mesh reaches %.3f ahead of the nock (2.909)", spec.model, ahead);
            else EXPECT(ahead >= 2.909f - 1e-3f && ahead < 3.3f, "%s, turned: at least the arrow's length ahead (%.3f; the box is the turned one's, a little bigger)", spec.model, ahead);
            ArrowEnds e;
            EXPECT(ArrowEndsOf(rig, &e, ahead) && e.fromMesh && !e.fromBones, "%s: the head from the mesh", spec.model);
            EXPECT(Near(e.head, rig.restNock + rig.back * -ahead, 1e-4f), "%s: the head on the arrow's own line, at the mesh's far end", spec.model);
            EXPECT(Length(e.head - e.nock) > rig.reach * 4.0f, "%s: far past the riser (%.2f against %.2f)", spec.model, Length(e.head - e.nock), rig.reach);
        }
        Synthetic s;
        Rig rig;
        EXPECT(MakeBow(spec, &s) && rig.Learn(s.r, s.bind, s.r.count), "%s learned", spec.model);
        ArrowEnds e;
        EXPECT(ArrowEndsOf(rig, &e, 0.1f) && !e.fromMesh, "%s: a mesh that does not reach past the riser is not the head", spec.model);
        EXPECT(ArrowEndsOf(rig, &e, std::nanf("")) && !e.fromMesh, "%s: a NaN mesh reach is none", spec.model);
    }
    Rig none;
    ArrowEnds e;
    EXPECT(!ArrowEndsOf(none, &e) && !ArrowEndsOf(none, nullptr), "no rig, no output: no ends");
    const float z3[3] = {0, 0, 0};
    EXPECT(MeshAhead(none, Identity(), z3, z3) == 0.0f, "no rig: no mesh reach");
}

int main() {
    TestArrowEnds();
    TestMath();
    TestLayouts();
    TestLearn();
    TestEveryBow();
    TestHuntersAsBefore();
    TestFrames();
    TestHold();
    TestReach();
    std::printf("%d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
