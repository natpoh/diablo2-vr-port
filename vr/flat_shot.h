// flat_shot.h - flat 3D views (our camera, mouse look): a bow's, a crossbow's or a throwing weapon's shot goes where the crosshair is (docs/notes/shot_at_release.md,
// item 6). Pure: no game, no Windows; the game's side is vr/input/flat_click.cpp, the test cleanroom/tests/flat_shot_test.cpp.
//
// [input] flat_shot_at_crosshair - no auto-aim. A click on a monster the game hovers is the game's attack AT that unit: the server aims the missile at the
// unit, the hero walks to it when it is out of reach, and a held button keeps shooting it wherever the crosshair has gone. The game hovers by its old
// isometric box round the mouse's point, so a monster near the crosshair is "under" it - that is the auto-aim. With a ranged weapon in hand and a HOSTILE
// monster hovered (a wild one: no owner, not dying or dead, not in a town), the click goes as on bare ground with Stand Still: the game's own lookup of the
// unit under the pointer answers nothing for that one call, and the shot goes to the mouse's point - the cell the crosshair's ray met ([input]
// flat_aim_ground, flat_body_pick). An item, an NPC, a hireling or summon, an object, a portal, a door: the game's own click, as ever.
// The arrow drawn into the crosshair is vr/flat_arrow.h.
#pragma once

#include <cstdint>

namespace flatshot {

// -- the click ---------------------------------------------------------------------------------------------------------------------------------------------

constexpr uint32_t kTypeMonster = 1;
constexpr uint32_t kModeDying = 0, kModeDead = 12;
constexpr uint32_t kNoOwner = 0xFFFFFFFFu;   // a monster's owner id (clientverdict::kNoOwner): a wild monster's; a hireling's or a summon's is its player's

// Rogue Encampment, Lut Gholein, Kurast Docks, Pandemonium Fortress, Harrogath (Levels.txt ids; as clientverdict::IsTown).
inline bool IsTown(int levelId) { return levelId == 1 || levelId == 40 || levelId == 75 || levelId == 103 || levelId == 109; }

struct ClickIn {
    bool on = true;          // [input] flat_shot_at_crosshair
    bool flat = false;       // [mode] platform 0
    bool shooter = false;    // mouse look and W A S D (KeyMoveMode && ShooterActive)
    bool portal = false;     // [input] portal_auto is taking this click to a portal
    bool menu = false;       // a panel of the game's is open
    bool mouseType = false;  // a mouse button's click (types 0 / 1 pressed / held left, 3 / 4 right)
    bool ranged = false;     // a bow, a crossbow, a javelin or a throwing weapon in hand
    bool hover = false;      // the game hovers a unit
    bool hoverRead = false;  // ... and its type, mode and owner were read
    uint32_t type = 0, mode = 0, owner = kNoOwner;
    int heroLevel = 0;       // the hero's area (0: not known)
};

enum class Click : uint8_t { Game, Bare };
struct ClickOut {
    Click click = Click::Game;
    const char* why = "";
};

inline ClickOut DecideClick(const ClickIn& in) {
    ClickOut o;
    if (!in.on) { o.why = "[input] flat_shot_at_crosshair is off"; return o; }
    if (!in.flat || !in.shooter || in.menu) { o.why = "not flat mouse look"; return o; }
    if (in.portal) { o.why = "the click goes to a portal"; return o; }
    if (!in.mouseType) { o.why = "not a mouse button's click"; return o; }
    if (!in.ranged) { o.why = "no bow, crossbow or throwing weapon in hand"; return o; }
    if (!in.hover) { o.why = "nothing hovered: already a shot at the crosshair"; return o; }
    if (!in.hoverRead) { o.why = "the hovered unit could not be read"; return o; }
    if (in.type != kTypeMonster) { o.why = "the hovered unit is no monster (an item, an object, a player)"; return o; }
    if (in.mode == kModeDying || in.mode == kModeDead) { o.why = "the hovered monster is dying or dead"; return o; }
    if (in.owner != kNoOwner) { o.why = "the hovered monster has an owner (a hireling, a summon)"; return o; }
    if (in.heroLevel <= 0) { o.why = "the hero's area is not known"; return o; }
    if (IsTown(in.heroLevel)) { o.why = "in a town (an NPC)"; return o; }
    o.click = Click::Bare;
    o.why = "a hostile monster hovered: the shot goes to the crosshair, not to the unit";
    return o;
}

}  // namespace flatshot
