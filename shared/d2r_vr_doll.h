/* d2r_vr_doll.h - the C interface between the D2R VR mod (d2rl-vrcam.dll) and an optional doll library (d2r_vr_doll.dll).
 *
 * The mod owns this header (shared/d2r_vr_doll.h in the d2r_vr repository); a library that implements it keeps a byte-identical copy (tools/check_abi.ps1 compares them).
 * Nothing in it comes from any third-party code: it only says what the mod knows about the monsters' bodies and the weapon's pushes, and what it wants back.
 *
 * What the library is: the monsters' weight. The mod hands it the bodies the held weapon meets - a capsule on each limb of a monster, in the render world - and each push the
 * weapon gives one of them; the library says how far each body is out of the place the game has it (a limb gives way, the whole monster less, its muscles bring both back) and
 * how heavy each monster is. The mod keeps everything else: which monsters, their limbs from the game's models, which of them are humanoids (only a body marked `doll` may move),
 * the weapon, the drawing, the settings. Nothing of the game's is changed by any of it: the bodies only shape how the drawn weapon moves and what the debug view draws.
 *
 * The library keeps NOTHING between calls: the state is a plain struct the mod owns and hands back (zero it to start afresh), so the library has no handle to create or free and
 * may be replaced at any time. A library that is not the version the mod expects is not used (D2rdAbi). All functions are called from one thread at a time for one state; they must
 * not throw and must not call back into the mod.
 *
 * Units: metres, kilograms, seconds, newton-seconds. The render world: y up; all positions of one call are in it.
 */
#ifndef D2R_VR_DOLL_H
#define D2R_VR_DOLL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define D2RD_ABI_VERSION 1

/* The library defines D2RD_BUILD_DLL before including this to export the functions; the mod never does. */
#if defined(D2RD_BUILD_DLL) && defined(_WIN32)
#define D2RD_API __declspec(dllexport)
#else
#define D2RD_API
#endif

#define D2RD_MAX_LIMBS 64      /* limbs swaying at once */
#define D2RD_MAX_MONSTERS 16   /* monsters swaying at once */

/* A body: a capsule from a to b, `radius` round it - a limb of a monster's skeleton (rig 1, bone its bone) or the game's capsule of it (rig 0). doll 1: it may move (a humanoid's
 * limb, as the mod judges it); doll 0: it stands, and its monster is not pushed through it. */
typedef struct D2rdBody {
    uint32_t id;          /* the game's unit id of its monster */
    int32_t bone;         /* rig: the skeleton's bone, else -1 */
    float a[3], b[3];     /* m, render world */
    float radius;         /* m: as thick as the weapon meets it */
    int32_t rig;
    int32_t doll;
} D2rdBody;

typedef struct D2rdParams {
    uint32_t size;        /* sizeof(D2rdParams) as the caller has it */
    float limbHz;         /* how fast a limb's muscles bring it back (its spring's own frequency) */
    float bodyHz;         /* the whole monster's, slower: its feet hold it, it staggers */
    float damping;        /* share of the critical damping (1: back without overshoot) */
    float maxLimbM;       /* the most a limb's far end is out of the game's place */
    float maxBodyM;       /* the most the whole monster is */
    float massScale;      /* the monsters as heavy as their flesh times this */
    float pushScale;      /* the weapon's push times this */
} D2rdParams;

/* The state: what sways now. The mod owns it; zero it to start afresh (a new area, the feature switched off). */
typedef struct D2rdSpring {
    float off[3], vel[3];
} D2rdSpring;
typedef struct D2rdLimb {
    uint32_t id;
    int32_t bone;
    D2rdSpring s;         /* its far end's sway */
} D2rdLimb;
typedef struct D2rdMonster {
    uint32_t id;
    int32_t reserved;
    D2rdSpring s;         /* the whole body's sway */
} D2rdMonster;
typedef struct D2rdState {
    uint32_t size;        /* sizeof(D2rdState) as the caller has it (0 in a zeroed state: the library takes it as its own) */
    int32_t nLimbs, nMonsters;
    D2rdLimb limbs[D2RD_MAX_LIMBS];
    D2rdMonster monsters[D2RD_MAX_MONSTERS];
} D2rdState;

typedef int32_t (*D2rdAbiFn)(void);
typedef const char* (*D2rdNameFn)(void);
/* The weapon pushed body i of bodies[0..n): impulse (N s, the way the body is pushed - into it) at point. 1 pushed, 0 not (not a doll, nothing to push), < 0 an error. */
typedef int32_t (*D2rdPushFn)(D2rdState* state, const D2rdParams* params, const D2rdBody* bodies, int32_t n, int32_t i, const float impulse[3], const float point[3]);
/* Time goes on by dt seconds: every sway back toward the game's place; quiet ones let go. 0, or < 0 an error. */
typedef int32_t (*D2rdStepFn)(D2rdState* state, const D2rdParams* params, float dt);
/* The bodies as they stand swayed (each moved in place). How many moved, < 0 an error. */
typedef int32_t (*D2rdApplyFn)(const D2rdState* state, const D2rdParams* params, D2rdBody* bodies, int32_t n);
/* How heavy monster id is (kg, massScale in it), from its bodies; 0 when none. */
typedef float (*D2rdMassFn)(const D2rdParams* params, const D2rdBody* bodies, int32_t n, uint32_t id);
/* How far out of balance monster id is: 0 (standing as the game has it) .. 1 (at the edge); off (may be null): its body's sway, m. */
typedef float (*D2rdBalanceFn)(const D2rdState* state, const D2rdParams* params, uint32_t id, float off[3]);

D2RD_API int32_t D2rdAbi(void);
D2RD_API const char* D2rdName(void);
D2RD_API int32_t D2rdPush(D2rdState* state, const D2rdParams* params, const D2rdBody* bodies, int32_t n, int32_t i, const float impulse[3], const float point[3]);
D2RD_API int32_t D2rdStep(D2rdState* state, const D2rdParams* params, float dt);
D2RD_API int32_t D2rdApply(const D2rdState* state, const D2rdParams* params, D2rdBody* bodies, int32_t n);
D2RD_API float D2rdMass(const D2rdParams* params, const D2rdBody* bodies, int32_t n, uint32_t id);
D2RD_API float D2rdBalance(const D2rdState* state, const D2rdParams* params, uint32_t id, float off[3]);

#ifdef __cplusplus
}
#endif

#endif /* D2R_VR_DOLL_H */
