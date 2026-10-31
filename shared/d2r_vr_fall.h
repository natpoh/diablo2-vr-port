/* d2r_vr_fall.h - the C interface between the D2R VR mod (d2rl-vrcam.dll) and an optional knock-down library (the D2rf* exports of d2r_vr_weight.dll).
 *
 * The mod owns this header (shared/d2r_vr_fall.h in the d2r_vr repository); a library that implements it keeps a byte-identical copy (tools/check_abi.ps1 compares them).
 * Nothing in it comes from any third-party code: it only says what the mod knows about a struck body and what it wants back.
 *
 * What the library is: a humanoid knocked off its feet, staggered, or lifted and thrown by the hands, as drawn. The mod hands it a body as the game draws it now - the places of its joints - and the blow that
 * knocked it down; the library makes the fall (a ragdoll on the ground), the lying, and the getting up, and says each moment where every joint is drawn and how much of
 * the drawn body is its own (the rest is the game's animation, which the mod blends in at the end). A body knocked or thrown out of its place walks
 * back to where the game has it (D2rfHome) before the game's animation has it again: the drawn body always comes home. The mod keeps everything else: when a blow knocks a body down, which
 * bones are which joints, the drawing, the settings. Nothing of the game's is changed by any of it: the unit stands, walks and acts as the game has it; only its drawing
 * lies down and gets up.
 *
 * The library keeps its state in a world the mod creates and destroys (D2rfCreate / D2rfDestroy). A library that is not the version the mod expects is not used
 * (D2rfAbi). All functions are called from one thread at a time for one world; they must not throw and must not call back into the mod.
 *
 * Units: metres, kilograms, seconds, newton-seconds. The render world: y up; all positions of one call are in it.
 */
#ifndef D2R_VR_FALL_H
#define D2R_VR_FALL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define D2RF_ABI_VERSION 2   /* 2: a stagger, the walk home (D2rfHome), the hands (D2rfGrab / D2rfHand / D2rfRelease / D2rfHeld), D2rfPose::off */

/* The library defines D2RF_BUILD_DLL before including this to export the functions; the mod never does. */
#if defined(D2RF_BUILD_DLL) && defined(_WIN32)
#define D2RF_API __declspec(dllexport)
#else
#define D2RF_API
#endif

/* A humanoid's joints: where each bone turns (the origin of the bone that starts there). TOE: the toes' bone (or the foot's far end). */
enum {
    D2RF_PELVIS = 0, D2RF_CHEST, D2RF_NECK, D2RF_HEAD,
    D2RF_L_SHOULDER, D2RF_L_ELBOW, D2RF_L_WRIST,
    D2RF_R_SHOULDER, D2RF_R_ELBOW, D2RF_R_WRIST,
    D2RF_L_HIP, D2RF_L_KNEE, D2RF_L_ANKLE, D2RF_L_TOE,
    D2RF_R_HIP, D2RF_R_KNEE, D2RF_R_ANKLE, D2RF_R_TOE,
    D2RF_JOINTS
};

#define D2RF_MAX_DOWN 8   /* bodies down at once */

/* How it was struck: knocked down by a blow, a sweep, a shove; or staggered - a step or two back, its balance caught, no fall. */
enum { D2RF_BLOW = 0, D2RF_SWEEP = 1, D2RF_SHOVE = 2, D2RF_STAGGER = 3 };

/* Where it is. D2RF_BACK: home and standing - the drawing goes back to the game's animation by `weight`. */
enum { D2RF_NONE = 0, D2RF_FALLING = 1, D2RF_LYING = 2, D2RF_GETTING_UP = 3, D2RF_BACK = 4, D2RF_HELD = 5, D2RF_STAGGERING = 6, D2RF_WALKING = 7 };

typedef struct D2rfKnock {
    uint32_t size;             /* sizeof(D2rfKnock) as the caller has it */
    uint32_t id;               /* the game's unit id */
    float joint[D2RF_JOINTS][3];   /* m, render world: the body as the game draws it now */
    uint32_t have;             /* bit j: joint j is the model's own (the others are the mod's guess from its neighbours) */
    float vel[3];              /* m/s: how the body moves now (the game walks it) */
    float floorY;              /* m: the ground under it */
    float massKg;              /* how heavy it is */
    float impulse[3];          /* N s: the blow, the way the body is pushed */
    float point[3];            /* m: where the blow met it */
    int32_t kind;              /* D2RF_BLOW, D2RF_SWEEP, D2RF_SHOVE, D2RF_STAGGER */
    float lieS;                /* s: how long it lies still before it gets up */
} D2rfKnock;

typedef struct D2rfPose {
    uint32_t id;
    int32_t phase;             /* D2RF_FALLING .. D2RF_WALKING */
    float weight;              /* 0 .. 1: how much of the drawn body is this pose (1 - weight: the game's own) */
    float t;                   /* s in the phase */
    int32_t fromBack;          /* getting up: 1 from lying on its back, 0 from lying face down */
    float off;                 /* m: how far its pelvis is drawn from home across the ground */
    float joint[D2RF_JOINTS][3];   /* m, render world */
} D2rfPose;

typedef int32_t (*D2rfAbiFn)(void);
typedef const char* (*D2rfNameFn)(void);
/* A world for the bodies down; null when it cannot be made. */
typedef void* (*D2rfCreateFn)(void);
typedef void (*D2rfDestroyFn)(void* world);
/* Knocks a body down: 1 it falls, 0 not (it is down already, the world is full, the body does not make a humanoid), < 0 an error. */
typedef int32_t (*D2rfKnockDownFn)(void* world, const D2rfKnock* knock);
/* Time goes on by dt seconds. 0, or < 0 an error. */
typedef int32_t (*D2rfStepFn)(void* world, float dt);
/* The bodies down now, up to max into out. How many. */
typedef int32_t (*D2rfPosesFn)(void* world, D2rfPose* out, int32_t max);
/* The game has the body do something it cannot do lying (it attacks, it walks off): how 1 - it gets up now, quicker; how 0 - it is let go of at once (it died, it is
 * gone): no pose for it from now on. 1 done, 0 not down. */
typedef int32_t (*D2rfLetGoFn)(void* world, uint32_t id, int32_t how);
/* The body as the game has it now (joint: render world) - its real place, which the drawn body walks back to. 1 taken, 0 not down. */
typedef int32_t (*D2rfHomeFn)(void* world, uint32_t id, const float joint[D2RF_JOINTS][3]);
/* Hand `hand` (0 right, 1 left) takes the body at `point`. body: its joints as drawn now, its floor, its mass (for one not down yet; one down takes the hold as it
 * is). It hangs from the hand: standing while its feet bear it, off its feet (a ragdoll) once lifted or pulled out of its place. 1 held, 0 not, < 0 an error. */
typedef int32_t (*D2rfGrabFn)(void* world, const D2rfKnock* body, int32_t hand, const float point[3]);
/* D2rfGrab with the part the point goes with: joint (D2RF_*) - the hand holds the body by `point` fixed to that joint's limb, wherever the point is (a spear
 * held by its shaft: the hand that holds it, `point` on the shaft). joint < 0: the limb nearest the point, as D2rfGrab. Optional (a library without it: D2rfGrab). */
typedef int32_t (*D2rfGrabPartFn)(void* world, const D2rfKnock* body, int32_t hand, const float point[3], int32_t joint);
/* Where the holding hand is now (its palm, render world). 1 done, 0 not held. */
typedef int32_t (*D2rfHandFn)(void* world, uint32_t id, int32_t hand, const float pos[3]);
/* The hand lets go: off its feet, the body flies on as the hand left it (a throw), falls, lies, gets up and walks home; still on its feet, it walks home. */
typedef int32_t (*D2rfReleaseFn)(void* world, uint32_t id, int32_t hand);
/* What the hand holds as it is drawn now: a capsule a..b, radius *r (render world) - the mod lays the hand on it. 1 filled, 0 not held. */
typedef int32_t (*D2rfHeldFn)(void* world, uint32_t id, int32_t hand, float a[3], float b[3], float* r);

D2RF_API int32_t D2rfAbi(void);
D2RF_API const char* D2rfName(void);
D2RF_API void* D2rfCreate(void);
D2RF_API void D2rfDestroy(void* world);
D2RF_API int32_t D2rfKnockDown(void* world, const D2rfKnock* knock);
D2RF_API int32_t D2rfStep(void* world, float dt);
D2RF_API int32_t D2rfPoses(void* world, D2rfPose* out, int32_t max);
D2RF_API int32_t D2rfLetGo(void* world, uint32_t id, int32_t how);
D2RF_API int32_t D2rfHome(void* world, uint32_t id, const float joint[D2RF_JOINTS][3]);
D2RF_API int32_t D2rfGrab(void* world, const D2rfKnock* body, int32_t hand, const float point[3]);
D2RF_API int32_t D2rfGrabPart(void* world, const D2rfKnock* body, int32_t hand, const float point[3], int32_t joint);
D2RF_API int32_t D2rfHand(void* world, uint32_t id, int32_t hand, const float pos[3]);
D2RF_API int32_t D2rfRelease(void* world, uint32_t id, int32_t hand);
D2RF_API int32_t D2rfHeld(void* world, uint32_t id, int32_t hand, float a[3], float b[3], float* r);

#ifdef __cplusplus
}
#endif

#endif /* D2R_VR_FALL_H */
