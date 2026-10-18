#pragma once
// What the skill wheel's parts hand each other: the game's side (a snapshot the UI thread publishes, a request the draw
// thread makes, the answer) and the pad buttons of the twelve slots. Plain values: no pointers into the game, no Windows.

#include <cstdint>
#include <cstring>

namespace wheel {

// The game's controller has TWELVE skill slots: six buttons - A X B Y RB RT - and the same six with the left trigger held (its own
// rebind dialog shows them so). Slots 0..7 are the wheel's eight (B X Y RB, then the same four with LT) and keep their numbers; the
// wheel's ring shows only those. 8..11 are the rest, which only the tiles look shows: A, RT, LT + A, LT + RT.
constexpr int kSlotCount = 12;
constexpr int kWheelSlots = 8;
constexpr int kCatalogMax = 128;
constexpr int kNameLen = 48;
constexpr int kOptionsMax = 24;   // the most skills the editor shows on one page (the wheel's list shows 8, the tiles' grid 24)

// The editor's skill categories (idea after bearstonem/d2r-pcvr, MIT): All, the class's three skill trees, and General / Items (everything
// that is no tree's: the game's own commands, the town portal, the skills an item grants). Which tree a skill is on, and the trees' names,
// come from the player's own game (the icon cache: tools/skill_icons reads SkillPage from skilldesc.txt and the names' keys from
// charstats.txt), not from a table in the plugin; without the cache the editor has the one list, All.
constexpr int kCategories = 5;
enum Category : int { kCatAll = 0, kCatTree1 = 1, kCatTree2 = 2, kCatTree3 = 3, kCatGeneral = 4 };
// What a skill's `tree` says: -1 not known (no cache), 0 General / Items, 1..3 the class's tree.
constexpr int kTreeUnknown = -1;
// The game's own "Run / walk switch" key command (the 0x23 of its command table): the RUN / WALK control presses it - the game's own
// executor, as the key would.
constexpr int kRunWalkCommand = 0x23;
// What the game says of it now (Snapshot::run): the switch is "Always run".
enum RunState : uint8_t { kRunUnknown = 0, kWalking = 1, kRunning = 2 };

// The panel's look ([openxr] skill_panel_style): the same model, cast and game side behind both.
enum class Style : uint8_t { Wheel = 0, Tiles = 1 };

// The tiles' window has two tabs when [openxr] headset_settings is on: Skills (the tiles, as ever) and Settings (the headset's settings
// pages: the game view F1 - F4, the render size, the T-pose measurement; vr/hset_*.h). Without the key there is no strip and no tab.
enum class Tab : uint8_t { Skills = 0, Settings = 1 };

// A short caption under each slot (which button of the player's controller presses it); empty = none. Set by the controller.
constexpr int kHintLen = 20;   // "LT + Left Dpad Dn" (Steam Frame) and its NUL
struct Hints {
    char text[kSlotCount][kHintLen] = {};
    bool operator==(const Hints& o) const { return std::memcmp(text, o.text, sizeof text) == 0; }
};

// XInput's bits (the same as the Windows header's and directinput::pad's, kept here so this needs neither).
constexpr uint16_t kPadA = 0x1000, kPadB = 0x2000, kPadX = 0x4000, kPadY = 0x8000, kPadRb = 0x0200;

// What the trigger does once a slot is the picked one:
//   Pad    - the game's own pad button of the slot is pressed (B X Y RB; the left trigger first for the second row; LT + A too, as the
//            shield-smite press does);
//   Layout - nothing new: the trigger stays the layout's attack, which IS the game's A button (A is the game's attack slot);
//   None   - not castable from the panel: shown and editable only (RT: the layout has no control for it, and the game's own RT handling
//            in the world has not been seen - a slot to enable after a test in the headset).
enum class Cast : uint8_t { Pad, Layout, None };

// The slots are the game's controller skill slots (see kSlotCount).
struct SlotDef {
    uint16_t button;    // the XInput bit that names the slot to the game's own lookup (0: RT, which has no bit: found by elimination)
    bool alt;           // with the left trigger held
    const char* label;
    Cast cast;
};
constexpr SlotDef kSlotDefs[kSlotCount] = {
    {kPadB, false, "B", Cast::Pad},         {kPadX, false, "X", Cast::Pad},         {kPadY, false, "Y", Cast::Pad},         {kPadRb, false, "RB", Cast::Pad},
    {kPadB, true, "LT + B", Cast::Pad},     {kPadX, true, "LT + X", Cast::Pad},     {kPadY, true, "LT + Y", Cast::Pad},     {kPadRb, true, "LT + RB", Cast::Pad},
    {kPadA, false, "A", Cast::Layout},      {0, false, "RT", Cast::None},           {kPadA, true, "LT + A", Cast::Pad},     {0, true, "LT + RT", Cast::None},
};
// A slot's base (the same slot without the left trigger): 0..3 B X Y RB, 8 A, 9 RT.
constexpr int BaseSlot(int slot) { return slot >= 10 ? slot - 2 : slot >= 4 && slot < 8 ? slot - 4 : slot; }

// The game's special skill ids the wheel treats apart.
constexpr int32_t kSkillAttack = 0;          // the basic attack (the Skills row 0): a slot can hold it
constexpr int32_t kSkillInteract = 357;      // the pick-up / interact command, bindable to a slot
constexpr int32_t kSkillLoot = 358;          // the game's other two commands of the same kind (the same arrow on their tiles)
constexpr int32_t kSkillCubeLoot = 370;
constexpr int32_t kSkillTownPortal = 359;    // one entry for the scroll (219) and the tome (220)
constexpr int32_t kSkillScroll = 219, kSkillTome = 220;

// A slot that holds nothing, the basic attack or one of the game's pick-up commands has no skill on it: its button is the plain action (the arrow
// the tiles draw), which the layout's own controls already do. Anything else is a skill the slot casts.
constexpr bool IsPlainAction(int32_t id) { return id < 0 || id == kSkillAttack || id == kSkillInteract || id == kSkillLoot || id == kSkillCubeLoot; }

struct Skill {
    int32_t id = -1;       // the Skills row's id; -1 nothing
    int32_t owner = -1;    // -1 the character's own, else the item that grants it
    int32_t level = 0;
    int8_t tree = kTreeUnknown;   // the skill tree of the player's class it is on (1..3), 0 General / Items, -1 not known: see kCategories
    bool known = false;    // read from the game
    bool available = false;   // the player has it now (not a mana or cooldown check: those stay the game's)
    char name[kNameLen] = {};
    bool operator==(const Skill& o) const {
        return id == o.id && owner == o.owner && level == o.level && tree == o.tree && known == o.known && available == o.available &&
               std::strncmp(name, o.name, kNameLen) == 0;
    }
};

// A slot whose content is a skill its button casts: read from the game, placed, not the plain action, and the player still has it.
inline bool CastsSkill(const Skill& k) { return k.known && !IsPlainAction(k.id) && k.available; }

// What the UI thread read: the eight slots as the game's controller table has them, and the skills the player can assign.
struct Snapshot {
    uint64_t session = 0;       // + 1 for every session of the plugin (a new one forgets every request)
    uint64_t at = 0;            // GetTickCount64 when it was read
    uint32_t revision = 0;      // + 1 whenever anything below changed
    int32_t bank = -1;          // 0 or 16: the weapon set's half of the table; -1 unknown
    bool ready = false;         // every address was found, the player and the slots could be read
    bool editable = false;      // and the game is in its controller layout: its setter writes the controller's table
    Skill slot[kSlotCount];
    uint32_t count = 0;
    Skill catalog[kCatalogMax];
    // The skill trees (kCategories): the player's class, whether the cache told which skill is on which tree, and the trees' names in the
    // game's own text (the player's language). treesKnown false = the editor has no category strip.
    int32_t playerClass = -1;
    bool treesKnown = false;
    char treeName[3][kNameLen] = {};
    uint8_t run = kRunUnknown;   // RunState: the game's run / walk switch as it stands now
};

inline bool Fresh(const Snapshot& s, uint64_t now, uint64_t session, uint64_t maxAgeMs = 900) {
    return s.ready && session && s.session == session && now >= s.at && now - s.at <= maxAgeMs;
}

enum class Op : uint8_t { None, Assign, Clear };
enum class Result : uint8_t { None, Pending, Applied, Refused };

// An assignment the editor asks for. It names the session, the bank and what the slot held when the player chose, so a
// change in between (another bank, another skill in the slot) is refused instead of written over.
struct Request {
    Op op = Op::None;
    uint64_t sequence = 0;
    uint64_t session = 0;
    int slot = -1;                 // 0..7 (the wheel's), not the game's own index
    int32_t skill = -1, owner = -1;
    int32_t bank = -1;
    int32_t prevSkill = -1, prevOwner = -1;
};

struct Reply {
    uint64_t sequence = 0;
    Result result = Result::None;
};

}  // namespace wheel
