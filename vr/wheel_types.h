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
constexpr int32_t kSkillInteract = 357;      // the pick-up / interact command, bindable to a slot
constexpr int32_t kSkillTownPortal = 359;    // one entry for the scroll (219) and the tome (220)
constexpr int32_t kSkillScroll = 219, kSkillTome = 220;

struct Skill {
    int32_t id = -1;       // the Skills row's id; -1 nothing
    int32_t owner = -1;    // -1 the character's own, else the item that grants it
    int32_t level = 0;
    bool known = false;    // read from the game
    bool available = false;   // the player has it now (not a mana or cooldown check: those stay the game's)
    char name[kNameLen] = {};
    bool operator==(const Skill& o) const {
        return id == o.id && owner == o.owner && level == o.level && known == o.known && available == o.available &&
               std::strncmp(name, o.name, kNameLen) == 0;
    }
};

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
