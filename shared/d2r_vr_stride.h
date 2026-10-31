/* d2r_vr_stride.h - the C interface between the D2R VR mod (d2rl-vrcam.dll) and an optional stepping walk for the hero's legs (the D2rs* exports of
 * d2r_vr_weight.dll).
 *
 * The mod owns this header (shared/d2r_vr_stride.h in the d2r_vr repository); a library that implements it keeps a byte-identical copy (tools/check_abi.ps1 compares them).
 * Nothing in it comes from any third-party code: it only says where the hero is, which way his trunk faces and how he goes, and wants his feet back.
 *
 * What the library is: the knock-down's walk (the feet stepping after the body: one foot in the air at a time, carried to its place under the body ahead along
 * the way, lifted between), made for a hero who goes at the game's speeds and any way against his trunk: the stride by his speed within the leg's reach,
 * running a flight between the steps, and the feet never crossed across the trunk - going sideways the leading foot steps out and the trailing one steps up
 * to it, never past it. The mod keeps everything else: which bones are the legs, how the knee bends to put the foot there, the trunk (the game's), when the
 * walk is used at all. The library keeps NOTHING between calls: the state is a plain struct the mod owns and hands back (zero it to start afresh), so it has no
 * handle to create or free.
 *
 * Units: metres, seconds, radians. The ground: the world's x, z (any origin; the same between calls) with a facing yaw's ahead (sin yaw, cos yaw) and its left
 * (cos yaw, -sin yaw). A library that is not the version the mod expects is not used (D2rsAbi). All functions are called from one thread at a time for one
 * state; they must not throw and must not call back into the mod.
 */
#ifndef D2R_VR_STRIDE_H
#define D2R_VR_STRIDE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define D2RS_ABI_VERSION 2   /* 2: D2rsGait - the pelvis and both legs from the ragdoll package's own walking cycles (the gait core) */

/* The library defines D2RS_BUILD_DLL before including this to export the functions; the mod never does. */
#if defined(D2RS_BUILD_DLL) && defined(_WIN32)
#define D2RS_API __declspec(dllexport)
#else
#define D2RS_API
#endif

/* One step of the hero's feet ([0] right, [1] left). */
typedef struct D2rsLegs {
    uint32_t size;          /* sizeof(D2rsLegs) as the caller has it */
    float dt;               /* s since the last step of this state (the library clamps it) */
    float body[2];          /* m: the pelvis's place on the ground */
    float yaw;              /* rad: the trunk's facing on the ground */
    float vel[2];           /* m/s: his travel over the ground */
    float stance[2][2];     /* m: each foot's place standing, against the body: [0] to the body's left (the right foot's is < 0), [1] ahead */
    float reach;            /* m: how far along the ground a planted foot may be from under its hip (the leg at the hips' height) */
    float legLength;        /* m: hip to ankle, thigh and shin together */
    float feet[2][2];       /* m: where the feet are drawn now on the ground - a fresh state starts with them planted there */
    /* stride 2.1 (read when `size` covers them; an older caller's struct ends above): */
    float stanceMax;        /* m: the body goes at most this far over a planted foot (0: 1.6 reaches) - narrow steps; faster ones instead */
    float cycleMinS;        /* s: a foot's cycle no quicker than this (0: the library's own, 1 / 2.6) - how fast the steps may come for stanceMax */
    /* stride 2.2 (read when `size` covers it): */
    float stagger;          /* m: > 0 with stanceMax: the narrow steps cross - the feet take turns, one always down (no bound), passing each other this far
                               apart along the trunk's forward (the left one ahead); the stance grows past stanceMax rather than a swing shorter than half
                               cycleMinS. 0: stride 2.1's (the trailing foot never passes the leading one: a bound sideways) */
} D2rsLegs;

/* The library's own memory of the feet, kept by the mod between steps; all zero = a fresh start. */
typedef struct D2rsState {
    uint32_t magic;         /* set by the library; another value: starts afresh */
    float at[2][2];         /* each foot on the ground (flying: where it is under its swing) */
    float yaw[2];           /* its toes' way */
    float from[2][2];       /* flying: where it left, against the body then */
    float fromYaw[2];
    float u[2];             /* of its swing done (0..1); < 0: planted */
    float dur[2];           /* its swing's time, s */
    int32_t lastSide;       /* the foot that stepped last */
    int32_t started;        /* steps taken since he began to go (0, 1, 2: more) */
    float last[2];          /* the body's place at the last call */
    float reserved[8];
} D2rsState;

typedef struct D2rsFeet {
    uint32_t size;          /* sizeof(D2rsFeet) as the caller has it */
    float at[2][2];         /* m: each foot on the ground */
    float yaw[2];           /* rad: its toes' way */
    float lift[2];          /* m: above the ground */
    float dip;              /* m: how far the hips go down under a swing (>= 0; the mod may leave the trunk the game's) */
    int32_t flying;         /* bit side: that foot in the air */
    int32_t steps;          /* steps begun in this call */
    float strideM;          /* m: how far the body goes over a planted foot at this speed */
    float swingS;           /* s: a swing's time at this speed */
} D2rsFeet;

/* The gait (version 2): the pelvis and both legs of the hero going, wholly from the ragdoll package's own walk / run / sprint cycles - a real cycle at the
 * cadence and stride of his speed (the phase by the distance he goes), the hips bobbing and swaying with it, the feet's path turned the way he goes, the
 * feet never across the trunk's middle, each leg laid to his own thigh and shin. The author (2026-10-31): "the legs up to the hips wholly to the ragdoll,
 * so the walk is right". Frame: the body's own - x its RIGHT, y up from its floor, z its forward (the trunk's), the middle of its hips over the origin; metres.
 * Joints: 0 the pelvis (the hips' middle), then the left hip, knee, ankle, toe, then the right hip, knee, ankle, toe. */
enum { D2RS_PELVIS = 0, D2RS_L_HIP, D2RS_L_KNEE, D2RS_L_ANKLE, D2RS_L_TOE, D2RS_R_HIP, D2RS_R_KNEE, D2RS_R_ANKLE, D2RS_R_TOE, D2RS_GAIT_JOINTS };

typedef struct D2rsGaitIn {
    uint32_t size;          /* sizeof(D2rsGaitIn) as the caller has it */
    float dt;               /* s since the last call for this state (the library clamps it) */
    float speed;            /* m/s over the ground */
    float angle;            /* rad: his travel off the trunk's forward, + to its right (pi: straight back) */
    float turnRate;         /* rad/s: the trunk's facing turning (stepping in place when he stands and turns) */
    float rest[D2RS_GAIT_JOINTS][3];   /* his legs as he stands, in the body's frame */
} D2rsGaitIn;

typedef struct D2rsGaitState {
    uint32_t magic;         /* set by the library; another value: starts afresh */
    float phase;            /* 0..1 of the cycle */
    float going;            /* 0 standing .. 1 going */
    float inPlace;          /* 0 .. 1 stepping in place */
    float reserved[4];
} D2rsGaitState;

typedef struct D2rsGaitOut {
    uint32_t size;          /* sizeof(D2rsGaitOut) as the caller has it */
    float joint[D2RS_GAIT_JOINTS][3];  /* the pelvis and the legs now, in the body's frame */
    float stepM;            /* m: one step at this speed (half a cycle's stride) */
    float cycleS;           /* s: one cycle at this speed (a foot's) */
} D2rsGaitOut;

/* D2RS_ABI_VERSION of the library. */
D2RS_API int32_t D2rsAbi(void);
/* A short name and version for the log. */
D2RS_API const char* D2rsName(void);
/* One step: 1 done (out written), 0 refused (bad arguments: out untouched). */
D2RS_API int32_t D2rsStep(const D2rsLegs* in, D2rsState* state, D2rsFeet* out);
/* The gait: 1 done (out written), 0 refused (bad arguments: out untouched). */
D2RS_API int32_t D2rsGait(const D2rsGaitIn* in, D2rsGaitState* state, D2rsGaitOut* out);

typedef int32_t (*D2rsAbiFn)(void);
typedef const char* (*D2rsNameFn)(void);
typedef int32_t (*D2rsStepFn)(const D2rsLegs* in, D2rsState* state, D2rsFeet* out);
typedef int32_t (*D2rsGaitFn)(const D2rsGaitIn* in, D2rsGaitState* state, D2rsGaitOut* out);

#ifdef __cplusplus
}
#endif

#endif /* D2R_VR_STRIDE_H */
