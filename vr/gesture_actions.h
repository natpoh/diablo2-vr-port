#pragma once
// gesture_actions.h - what a gesture may do besides casting a skill (Serg, 2026-11-08: "bind not only spells but other game actions to glyphs and
// gestures, the town portal on a glyph, for one"): the Gestures tab's Bind offers these beside the skills, the engine keeps the choice with the
// gesture (shared/gesture_engine_abi.h BWGE_BindAction, its id string - so BodyWalk shows the same binding) and hands it back when the gesture is
// recognised (BWGE_PollCasts2); vr/gesture_engine_wire.cpp then does it THE GAME'S OWN WAY, never past its rules:
//   Item     the hero's own Town Portal / Identify - the tome's skill, else the scroll's, cast as a skill is (skill_link_wire's caster: the game's
//            own skill use of the item, as a right click on it); neither in the inventory or belt: the HUD says so, nothing is done
//   Command  one of the game's key commands (shared/d2r_vr_shared.h kD2RVRCommands: the belt's potions, swap weapons, the panels, Alt, run /
//            walk ...) pressed by the game's own executor as its key would be (vr/gamecmd.cpp, directinput::Tap) - every gate of the game applies
//   Act      the mod's own "D2R:" actions (directinput::Action: the game's attack on the pad, the pick-up onto a target)
//   Pad      a controller button pulsed into the game's own XInput poll (directinput::PadPulse) - as BodyWalk's Mapping list has them
// (A keyboard key as such is not offered: every key the game has a use for is a key command above, pressed where the game reads its key.)
// Pure (no calls into the game or Windows; the shared header's command names only): cleanroom/tests/gesture_overlay_test.cpp.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "d2r_vr_shared.h"

namespace gact {

enum class Kind : uint8_t { Item, Command, Act, Pad };

struct Def {
    char id[32];      // kept with the gesture: "d2r.town_portal", "d2r.cmd.17", "d2r.act.2", "pad.A"
    char name[48];    // shown (English: the game's own command names, as BodyWalk's list shows them)
    Kind kind;
    uint32_t code;    // Item: kItemTownPortal / kItemIdentify; Command: the game's command; Act: D2RVR_ACT_* bits; Pad: XInput bits (LT / RT: kPadLT / kPadRT)
};

enum : uint32_t { kItemTownPortal = 1, kItemIdentify = 2 };
// The items' skills (the game's skills.txt): the tome's first, then the scroll's.
constexpr int32_t kTownPortalSkills[2] = {220, 219}, kIdentifySkills[2] = {218, 217};
constexpr uint32_t kPadLT = 0x10000u, kPadRT = 0x20000u;   // (the triggers: not XInput button bits)

constexpr int kMax = 128;

// The list, in the order the Bind screen shows it: the most wanted first (the town portal, the potions, the panels), then the pad's buttons, then
// every other key command of the game.
class List {
    Def d_[kMax];
    int n_ = 0;
    void Add(const char* id, const char* name, Kind k, uint32_t code) {
        if (n_ >= kMax) return;
        for (int i = 0; i < n_; ++i)
            if (std::strcmp(d_[i].id, id) == 0) return;   // (once)
        Def& d = d_[n_++];
        std::snprintf(d.id, sizeof d.id, "%s", id);
        std::snprintf(d.name, sizeof d.name, "%s", name);
        d.kind = k;
        d.code = code;
    }
    void Cmd(uint8_t cmd) {
        for (uint32_t i = 0; i < kD2RVRCommandCount; ++i)
            if (kD2RVRCommands[i].cmd == cmd) {
                const char* n = kD2RVRCommands[i].action;
                if (std::strncmp(n, "D2R key: ", 9) == 0) n += 9;
                char id[32];
                std::snprintf(id, sizeof id, "d2r.cmd.%02X", (unsigned)cmd);
                Add(id, n, Kind::Command, cmd);
                return;
            }
    }

public:
    List() {
        Add("d2r.town_portal", "Town Portal (tome or scroll)", Kind::Item, kItemTownPortal);
        Add("d2r.identify", "Identify (tome or scroll)", Kind::Item, kItemIdentify);
        for (uint8_t c : {0x17, 0x18, 0x19, 0x1A, 0x2C, 0x01, 0x00, 0x0C, 0x07, 0x04, 0x25, 0x23}) Cmd(c);   // belt 1-4, swap, inventory, character, skill tree, map, quests, Alt, run / walk
        Add("d2r.act.2", "Pick up / interact", Kind::Act, D2RVR_ACT_INTERACT);
        Add("d2r.act.1", "Attack", Kind::Act, D2RVR_ACT_ATTACK);
        for (uint8_t c : {0x3C, 0x36, 0x02, 0x26, 0x16, 0x27, 0x28, 0x2D}) Cmd(c);   // cube, mercenary, party, close all, show belt, next / previous skill, minimap
        struct P { const char* id; const char* name; uint32_t bits; };
        static const P pads[] = {{"pad.A", "Pad A", 0x1000}, {"pad.B", "Pad B", 0x2000}, {"pad.X", "Pad X", 0x4000}, {"pad.Y", "Pad Y", 0x8000},
                                 {"pad.LB", "Pad left bumper", 0x0100}, {"pad.RB", "Pad right bumper", 0x0200}, {"pad.LT", "Pad left trigger", kPadLT},
                                 {"pad.RT", "Pad right trigger", kPadRT}, {"pad.Back", "Pad Back", 0x0020}, {"pad.Start", "Pad Start", 0x0010},
                                 {"pad.LS", "Pad left stick click", 0x0040}, {"pad.RS", "Pad right stick click", 0x0080}, {"pad.Up", "Pad D-pad up", 0x0001},
                                 {"pad.Down", "Pad D-pad down", 0x0002}, {"pad.Left", "Pad D-pad left", 0x0004}, {"pad.Right", "Pad D-pad right", 0x0008}};
        for (const P& p : pads) Add(p.id, p.name, Kind::Pad, p.bits);
        for (uint32_t i = 0; i < kD2RVRCommandCount; ++i) Cmd(kD2RVRCommands[i].cmd);   // every other key command of the game
    }
    int Count() const { return n_; }
    const Def& At(int i) const { return d_[i < 0 ? 0 : i >= n_ ? n_ - 1 : i]; }
    const Def* Find(const char* id) const {
        if (!id || !id[0]) return nullptr;
        for (int i = 0; i < n_; ++i)
            if (std::strcmp(d_[i].id, id) == 0) return &d_[i];
        return nullptr;
    }
    int IndexOf(const char* id) const {
        const Def* d = Find(id);
        return d ? int(d - d_) : -1;
    }
};

inline const List& All() {
    static const List l;
    return l;
}

// The name to show for an action id (an id this build does not know: the id itself - BodyWalk or a newer mod bound it).
inline const char* NameOf(const char* id) {
    const Def* d = All().Find(id);
    return d ? d->name : id;
}

}  // namespace gact
