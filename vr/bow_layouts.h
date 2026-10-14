// bow_layouts.h - which bones of a bow's own skeleton are its limbs, its string and its arrow. Pure: names in, indices out.
//
// Every bow in the game is a skeleton of its own: root > translate > anim > cog, and under the cog two limb bones, the
// string's middle (bowString) and the arrow. What differs from bow to bow is the names and the odd extra bone (a pivot at
// a limb's tip, an offset bone, a joint on the string, two helpers on the nock). A layout says WHICH bones play the parts
// the solver (bow_rig.h) needs; the hierarchy says the rest:
//
//   bend[2]        the limb bones, upper (top) first. They turn about their own origin when the string is drawn.
//   tip[2]         the string's end on each limb, somewhere below its bend. Where the string is tied: the tip stays a limb's
//                  length from its hinge and a string half's length from the nock. What hangs below a tip follows the string.
//   nock           the string's middle. It goes to the string hand.
//   nockHelper[2]  optional: bones on the nock that lean toward each string half (upper, lower).
//   arrow[]        the arrow's bones, optional ones among them: the arrow itself and the release helper below it (the game's
//                  own arrow starts from it), whichever of these names the bow has.
//   forward, up    the bow's axes in the cog's own frame: forward = away from the archer along the bow (the way an arrow flies,
//                  the nock lies at -forward of the grip), up = along the limbs, lower tip to upper. Every hero bow in the game has
//                  them as +z and +y (read from the 15 skeletons); a bind pose that lies another way is refused, and the bow's
//                  turn onto the aim line (bow_aim.h) takes its axes from here and from the rig's learned geometry.
//
// Every other bone below a bend turns rigidly with it; every other bone below the nock or an arrow bone goes with it; a
// bone elsewhere (feathers, leaves, a second cog) is left to the game's own animation. A bow whose bones are not one of these
// layouts is refused, and keeps the game's animation.
//
// A new layout is an entry of kLayouts. Read from the game's data (data/hd/items/weapon/bow/*): the 15 bows a hero holds fall
// into the two entries below (hunters_bow, composite_bow, long_bow, short_battle_bow, long_battle_bow, stag_bow, reflex_bow,
// kuko_shakaku, piercerib, whichwild_string: first; short_bow, short_war_bow, long_war_bow, hellclap, pullspite: second).
// The crossbows (crossbow, light/heavy/repeating_crossbow, hellcast, ichorsting, langer_briser, leadcrow, pus_spiter,
// doomspittle) have limbs, a string and a bolt, but a trigger, a lever or a magazine beside them: not covered.
#pragma once

#include <cstdint>
#include <string_view>

namespace bowrig {

inline constexpr int kMaxBones = 32;

enum Role : uint8_t {
    kNone = 0,    // not touched: the game's own pose stays
    kBend,        // a limb bone: turns about its origin
    kLimb,        // below a bend, not the string: turns with it
    kTip,         // the string's end on a limb
    kTipFollow,   // below a tip: goes with the string
    kNock,        // the string's middle
    kNockHelper,  // on the nock, leaning toward a string half
    kNockFollow,  // below the nock: goes with it
    kArrow,       // an arrow bone, or below one
};

struct Layout {
    const char* name;
    const char* bend[2];
    const char* tip[2];
    const char* nock;
    const char* nockHelper[2];   // both or neither present
    const char* arrow[6];        // alternatives, nullptr ends the list; at least one is a child of the cog
    float forward[3];            // the bow's forward axis in the cog's frame, unit
    float up[3];                 // the bow's limb axis in the cog's frame, unit
};

inline constexpr Layout kLayouts[] = {
    {"upper/lower string", {"upperBend", "lowerBend"}, {"upperString", "lowerString"}, "bowString",
     {"bowString_upper_helper", "bowString_lower_helper"}, {"arrow", "arrow2", "arrow_helper", "arrowHelper", "alt_arrow", nullptr},
     {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
    {"top/bottom string helper", {"topBowBend", "botBowBend"}, {"top_stringHelper", "bot_stringHelper"}, "bowString",
     {"bowString_upper_helper", "bowString_lower_helper"}, {"arrow", "arrow2", "arrow_helper", "arrowHelper", "alt_arrow", nullptr},
     {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
};

// A skeleton matched to a layout: the bones' indices and what each is for.
struct Resolved {
    const Layout* layout = nullptr;
    int count = 0;
    int cog = -1, nock = -1;
    int bend[2] = {-1, -1}, tip[2] = {-1, -1}, nockHelper[2] = {-1, -1};
    int arrow = -1;                 // the first arrow bone that hangs off the cog
    uint8_t role[kMaxBones] = {};   // Role of each bone
    int8_t side[kMaxBones] = {};    // which limb (0 upper, 1 lower) a bone of that limb's kinds belongs to; -1 none
    int lowest = -1, highest = -1;  // the first and the last bone that has a role: what is written back
    bool Writes(int i) const { return i >= 0 && i < count && role[i] != kNone; }
};

namespace detail {
inline int Find(const std::string_view* names, int count, const char* name) {
    if (!name) return -1;
    for (int i = 0; i < count; ++i)
        if (names[i] == name) return i;
    return -1;
}
// i is j, or below it (parents come first)
inline bool Below(const int* parents, int i, int j) {
    for (int guard = 0; i >= 0 && guard <= kMaxBones; ++guard, i = parents[i])
        if (i == j) return true;
    return false;
}
}  // namespace detail

// Match a skeleton (bone names and parents as the game lists them) to a layout. False and *why set when it is not one.
inline bool Resolve(const std::string_view* names, const int* parents, int count, Resolved* out, const char** why = nullptr) {
    auto fail = [&](const char* r) { if (why) *why = r; return false; };
    if (!names || !parents || !out || count < 8 || count > kMaxBones) return fail("not a skeleton of a bow");
    for (int i = 0; i < count; ++i)
        if (parents[i] < -1 || parents[i] >= i) return fail("bones do not come parents first");
    const int cog = detail::Find(names, count, "cog");
    if (cog < 0) return fail("no cog bone");
    for (const Layout& l : kLayouts) {
        Resolved r;
        r.layout = &l;
        r.count = count;
        r.cog = cog;
        for (int i = 0; i < kMaxBones; ++i) r.side[i] = -1;
        bool ok = true;
        for (int s = 0; s < 2 && ok; ++s) {
            r.bend[s] = detail::Find(names, count, l.bend[s]);
            r.tip[s] = detail::Find(names, count, l.tip[s]);
            ok = r.bend[s] >= 0 && r.tip[s] >= 0 && parents[r.bend[s]] == cog && detail::Below(parents, r.tip[s], r.bend[s]) && r.tip[s] != r.bend[s];
        }
        r.nock = detail::Find(names, count, l.nock);
        ok = ok && r.nock >= 0 && parents[r.nock] == cog && r.bend[0] != r.bend[1] && r.tip[0] != r.tip[1];
        if (!ok) continue;
        const int h0 = detail::Find(names, count, l.nockHelper[0]), h1 = detail::Find(names, count, l.nockHelper[1]);
        if ((h0 < 0) != (h1 < 0)) continue;   // one of the two helpers: not this layout
        if (h0 >= 0 && (parents[h0] != r.nock || parents[h1] != r.nock)) continue;
        r.nockHelper[0] = h0;
        r.nockHelper[1] = h1;
        bool anyArrow = false, rootArrow = false;
        for (int k = 0; l.arrow[k]; ++k) {
            const int a = detail::Find(names, count, l.arrow[k]);
            if (a < 0) continue;
            anyArrow = true;
            if (parents[a] == cog) { rootArrow = true; if (r.arrow < 0) r.arrow = a; }
        }
        if (!anyArrow || !rootArrow) continue;
        // roles, in the order that gives a bone its first
        auto give = [&](int i, Role role, int side) {
            if (r.role[i] != kNone) return false;
            r.role[i] = role;
            r.side[i] = (int8_t)side;
            return true;
        };
        for (int s = 0; s < 2 && ok; ++s) ok = give(r.bend[s], kBend, s) && give(r.tip[s], kTip, s);
        ok = ok && give(r.nock, kNock, -1);
        if (ok && h0 >= 0) ok = give(h0, kNockHelper, 0) && give(h1, kNockHelper, 1);
        for (int k = 0; l.arrow[k] && ok; ++k) {
            const int a = detail::Find(names, count, l.arrow[k]);
            if (a >= 0) ok = give(a, kArrow, -1);
        }
        if (!ok) continue;
        // everything below those
        for (int i = 0; i < count && ok; ++i) {
            if (r.role[i] != kNone) continue;
            for (int s = 0; s < 2 && r.role[i] == kNone; ++s) {
                if (detail::Below(parents, i, r.tip[s])) ok = give(i, kTipFollow, s);
                else if (detail::Below(parents, i, r.bend[s])) ok = give(i, kLimb, s);
            }
            if (r.role[i] == kNone && detail::Below(parents, i, r.nock)) ok = give(i, kNockFollow, -1);
            for (int k = 0; r.role[i] == kNone && l.arrow[k]; ++k) {
                const int a = detail::Find(names, count, l.arrow[k]);
                if (a >= 0 && detail::Below(parents, i, a)) ok = give(i, kArrow, -1);
            }
        }
        if (!ok) continue;
        for (int i = 0; i < count; ++i)
            if (r.role[i] != kNone) { if (r.lowest < 0) r.lowest = i; r.highest = i; }
        *out = r;
        return true;
    }
    return fail("a bow whose bones are not a known layout");
}

}  // namespace bowrig
