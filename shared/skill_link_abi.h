// skill_link_abi.h - Game Link's Skills and Casts blocks (BodyWalk's sdk/include/bodywalk/game_link.h, from its branch
// claude/skill-gestures): the game's skills to BodyWalk, the skill to cast back.
//
// BodyWalk's header is the source of truth. An older one (the SDK most build dirs are configured with,
// C:/wsl/vjoy/.claude/worktrees/d2rvr-engine-link/sdk/include) has neither block: then this file defines them, byte for byte the
// same, so the mod builds against either. cleanroom/tests/skill_link_test.cpp checks the layout against the numbers below, so a copy
// that drifted from BodyWalk's fails there.
#pragma once

#include <bodywalk/game_link.h>

#ifndef BW_LINK_SKILLS_VERSION

#define BW_LINK_SKILLS_NAME L"Local\\BodyWalk.Link.Skills"   // game -> BodyWalk
#define BW_LINK_CASTS_NAME L"Local\\BodyWalk.Link.Casts"     // BodyWalk -> game

#define BW_LINK_SKILLS_VERSION 1u
#define BW_LINK_SKILLS_MAX 192u
#define BW_LINK_SKILL_CHARS 48u
#define BW_LINK_SKILL_GROUPS 4u
#define BW_LINK_SKILL_SLOTS 16u

enum {
    BW_LINK_SKILL_LEARNED = 1u << 0,
    BW_LINK_SKILL_ITEM = 1u << 1,
    BW_LINK_SKILL_ON_SLOT = 1u << 2,
};

#pragma pack(push, 8)
typedef struct BW_LinkSkill {
    int32_t id;
    uint32_t flags;
    int16_t level;
    int8_t group;
    int8_t slot;
    char name[BW_LINK_SKILL_CHARS];
} BW_LinkSkill;

typedef struct BW_LinkSkills {
    BW_LinkHeader header;
    char gameId[32];
    uint32_t revision;
    uint32_t count;
    char setKey[32];
    char setName[64];
    char heroName[32];
    char groupName[BW_LINK_SKILL_GROUPS][BW_LINK_SKILL_CHARS];
    char slotName[BW_LINK_SKILL_SLOTS][16];
    uint32_t recordSequence;
    int32_t recordSkill;
    BW_LinkSkill skill[BW_LINK_SKILLS_MAX];
} BW_LinkSkills;
#pragma pack(pop)

#define BW_LINK_CASTS_VERSION 1u
#define BW_LINK_CASTS_RING 16u
#define BW_LINK_CASTS_HELD 8u

enum {
    BW_LINK_CAST_GESTURE = 1,
    BW_LINK_CAST_BUTTON = 2,
};
enum {
    BW_LINK_BOUND_BUTTON = 1u << 0,
    BW_LINK_BOUND_GESTURE = 1u << 1,
};
enum {
    BW_LINK_REC_NONE = 0,
    BW_LINK_REC_READY = 1,
    BW_LINK_REC_DRAWING = 2,
    BW_LINK_REC_SAVED = 3,
    BW_LINK_REC_FAILED = 4,
    BW_LINK_REC_CANCELLED = 5,
};

#pragma pack(push, 8)
typedef struct BW_LinkCast {
    uint32_t sequence;
    int32_t skill;
    uint32_t source;
    uint32_t stamp;
} BW_LinkCast;

typedef struct BW_LinkBound {
    int32_t skill;
    uint32_t kinds;
} BW_LinkBound;

typedef struct BW_LinkCasts {
    BW_LinkHeader header;
    uint32_t stamp;
    uint32_t skillsRevision;
    char setKey[32];
    uint32_t lastSequence;
    uint32_t heldCount;
    int32_t held[BW_LINK_CASTS_HELD];
    BW_LinkCast ring[BW_LINK_CASTS_RING];
    uint32_t recordSequence;
    int32_t recordSkill;
    uint32_t recordState;
    uint32_t boundCount;
    BW_LinkBound bound[BW_LINK_SKILLS_MAX];
} BW_LinkCasts;
#pragma pack(pop)

#endif  // BW_LINK_SKILLS_VERSION

// Gestures (BodyWalk -> game) and GestureCmds (game -> BodyWalk): the player's gestures managed from the game's own overlay - the list
// with a picture of each, how a recording stands, the last stroke's match; the player's commands back. BodyWalk records, recognises and
// keeps the gestures; the game only draws and asks. The contract is BodyWalk's header (claude/skill-gestures-184); a copy here for
// older SDKs, the same bytes.
#ifndef BW_LINK_GESTURES_VERSION

#define BW_LINK_GESTURES_NAME L"Local\\BodyWalk.Link.Gestures"         // BodyWalk -> game
#define BW_LINK_GESTURECMDS_NAME L"Local\\BodyWalk.Link.GestureCmds"   // game -> BodyWalk
#define BW_LINK_GESTURES_VERSION 1u
#define BW_LINK_GESTURES_MAX 64u
#define BW_LINK_GESTURE_POINTS 48u
#define BW_LINK_GESTURE_JOINTS 5u
#define BW_LINK_GESTURECMDS_VERSION 1u
#define BW_LINK_GESTURECMDS_RING 8u

enum {
    BW_LINK_GT_NONE = 0,
    BW_LINK_GT_GLYPH = 1,     // a shape drawn in the air, the trigger held as the pen
    BW_LINK_GT_POSE = 2,      // a pose held (hands where they are, a grab): taken at the trigger's press
    BW_LINK_GT_MOTION = 3,    // a movement: the trigger starts it, the trigger again ends it
    BW_LINK_GT_BUTTON = 4,    // a controller button (chosen in BodyWalk; not recorded)
};
enum { BW_LINK_GH_LEFT = 0, BW_LINK_GH_RIGHT = 1, BW_LINK_GH_BOTH = 2 };
enum {
    BW_LINK_GF_RECORDED = 1u << 0,
    BW_LINK_GF_LIVE = 1u << 1,
    BW_LINK_GF_REVIEW = 1u << 2,
    BW_LINK_GF_RECORDING = 1u << 3,
};
enum { BW_LINK_J_HEAD = 0, BW_LINK_J_CHEST = 1, BW_LINK_J_PELVIS = 2, BW_LINK_J_LHAND = 3, BW_LINK_J_RHAND = 4 };

#pragma pack(push, 8)
typedef struct BW_LinkGesture {
    uint32_t id;                       // stable while it lives; never 0
    uint8_t type, hand, flags, pointCount;
    int32_t skill;                     // the Skills id it casts; -1 none
    uint32_t rgba;                     // R in the low byte
    float lastPercent;                 // -1 none yet
    float threshold;                   // percent
    char name[48];
    float pts[BW_LINK_GESTURE_POINTS][2];   // GLYPH: its icon; MOTION: its path, start first; in [-0.5, 0.5]^2, y up
    float joints[BW_LINK_GESTURE_JOINTS][3];   // POSE: metres from the pelvis; x body-right, y up, z body-back
    float grip[2];                     // POSE: left, right closed 0..1
    uint32_t jointValid;               // bit per BW_LINK_J_*
    char button[32];                   // BUTTON: its name
} BW_LinkGesture;

typedef struct BW_LinkGestures {
    BW_LinkHeader header;
    uint32_t stamp;
    uint32_t revision;
    char setKey[32];
    uint32_t count;
    uint32_t cmdDone;                  // the last GestureCmds sequence acted on
    uint32_t cmdResult;                // BW_LINK_GR_*
    uint32_t recordState;              // BW_LINK_REC_*
    uint32_t recordGesture;
    uint8_t recordType, recordHand, reserved8[2];
    int32_t recordSkill;
    uint32_t lastStamp;
    uint32_t lastGesture;
    float lastPercent;
    uint32_t lastRuns;
    uint32_t lastMatched;              // bit n = run n matched
    BW_LinkGesture g[BW_LINK_GESTURES_MAX];
} BW_LinkGestures;
#pragma pack(pop)

enum {
    BW_LINK_GC_RECORD = 1,       // gesture != 0: its new take; 0: a new gesture of type + hand for skill
    BW_LINK_GC_STOP = 2,
    BW_LINK_GC_KEEP = 3,
    BW_LINK_GC_DISCARD = 4,
    BW_LINK_GC_CLEAR = 5,
    BW_LINK_GC_BIND = 6,         // gesture casts skill (-1: nothing)
    BW_LINK_GC_COLOR = 7,
    BW_LINK_GC_THRESHOLD = 8,
};
enum { BW_LINK_GR_OK = 0, BW_LINK_GR_NO_GESTURE = 1, BW_LINK_GR_BAD = 2, BW_LINK_GR_BUSY = 3, BW_LINK_GR_NO_SET = 4 };

#pragma pack(push, 8)
typedef struct BW_LinkGestureCmd {
    uint32_t sequence;
    uint32_t op;
    uint32_t gesture;
    int32_t skill;
    uint8_t type, hand, reserved8[2];
    uint32_t rgba;
    float value;
} BW_LinkGestureCmd;

typedef struct BW_LinkGestureCmds {
    BW_LinkHeader header;
    uint32_t lastSequence;
    uint32_t reserved;
    BW_LinkGestureCmd ring[BW_LINK_GESTURECMDS_RING];
} BW_LinkGestureCmds;
#pragma pack(pop)

#endif  // BW_LINK_GESTURES_VERSION

// The wire, whichever header defined it.
static_assert(sizeof(BW_LinkSkill) == 60, "BW_LinkSkill is a wire format");
static_assert(sizeof(BW_LinkSkills) == 24 + 32 + 8 + 32 + 64 + 32 + 4 * 48 + 16 * 16 + 8 + 192 * 60, "BW_LinkSkills is a wire format");
static_assert(sizeof(BW_LinkCast) == 16 && sizeof(BW_LinkBound) == 8, "BW_LinkCast / BW_LinkBound are wire formats");
static_assert(sizeof(BW_LinkCasts) == 24 + 8 + 32 + 8 + 8 * 4 + 16 * 16 + 16 + 192 * 8, "BW_LinkCasts is a wire format");
static_assert(sizeof(BW_LinkSkills) <= BW_LINK_BLOCK_BYTES && sizeof(BW_LinkCasts) <= BW_LINK_BLOCK_BYTES, "a block holds it");
static_assert(sizeof(BW_LinkGesture) == 560, "BW_LinkGesture is a wire format");
static_assert(sizeof(BW_LinkGestures) == 112 + 64 * 560, "BW_LinkGestures is a wire format");
static_assert(sizeof(BW_LinkGestureCmd) == 28 && sizeof(BW_LinkGestureCmds) == 32 + 8 * 28, "BW_LinkGestureCmd(s) are wire formats");
