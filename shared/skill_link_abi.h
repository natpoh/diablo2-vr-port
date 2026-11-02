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

// The wire, whichever header defined it.
static_assert(sizeof(BW_LinkSkill) == 60, "BW_LinkSkill is a wire format");
static_assert(sizeof(BW_LinkSkills) == 24 + 32 + 8 + 32 + 64 + 32 + 4 * 48 + 16 * 16 + 8 + 192 * 60, "BW_LinkSkills is a wire format");
static_assert(sizeof(BW_LinkCast) == 16 && sizeof(BW_LinkBound) == 8, "BW_LinkCast / BW_LinkBound are wire formats");
static_assert(sizeof(BW_LinkCasts) == 24 + 8 + 32 + 8 + 8 * 4 + 16 * 16 + 16 + 192 * 8, "BW_LinkCasts is a wire format");
static_assert(sizeof(BW_LinkSkills) <= BW_LINK_BLOCK_BYTES && sizeof(BW_LinkCasts) <= BW_LINK_BLOCK_BYTES, "a block holds it");
