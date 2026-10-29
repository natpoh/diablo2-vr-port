/* d2r_vr_gait.h - the C interface between the D2R VR mod (d2rl-vrcam.dll) and an optional gait library (d2r_vr_gait.dll).
 *
 * The mod owns this header (shared/d2r_vr_gait.h in the d2r_vr repository); a library that implements it keeps a byte-identical copy (tools/check_abi.ps1 compares them).
 * It only says what the mod knows about a hero's legs and his travel, and what it wants back.
 *
 * What the library is: where a walking body puts its feet - steps along the way the hero travels, whatever way his body faces (forward, backward, sideways), the feet planted
 * while they bear the body and swung with a lift between, a standing stance when he stands. The mod keeps everything else: which bones are the legs, how the knee bends to put
 * the foot there, which way the body faces. The library keeps NOTHING between calls: the state is a plain struct the mod owns and hands back (zero it to start afresh), so the
 * library has no handle to create or free and may be replaced at any time.
 *
 * Units: the hero model's own units for every position and velocity (unitsPerM says how many make a metre), seconds, radians. Model space: y up; the body's forward is given.
 * A library that is not the version the mod expects is not used (D2rfAbi). All functions are called from one thread at a time for one state; they must not throw and must not
 * call back into the mod.
 */
#ifndef D2R_VR_GAIT_H
#define D2R_VR_GAIT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define D2RF_ABI_VERSION 1

/* The library defines D2RF_BUILD_DLL before including this to export the functions; the mod never does. */
#if defined(D2RF_BUILD_DLL) && defined(_WIN32)
#define D2RF_API __declspec(dllexport)
#else
#define D2RF_API
#endif

typedef struct D2rfV3 { float x, y, z; } D2rfV3;

/* One step of one hero's legs ([0] right, [1] left). */
typedef struct D2rfLegsIn {
    uint32_t size;          /* sizeof(D2rfLegsIn) as the caller has it */
    float dt;               /* seconds since the last step of this state (the library clamps it) */
    float unitsPerM;        /* the model's units in a metre */
    D2rfV3 forward;         /* the body's forward on the ground (unit, y 0): where the pelvis faces */
    D2rfV3 velocity;        /* the hero's travel over the ground, model units a second (y ignored) */
    D2rfV3 hip[2];          /* the hip joints as the body is posed now */
    D2rfV3 rest[2];         /* where each foot bone stands when he stands still (its y: the foot bone's height over the floor) */
    float legLength[2];     /* hip joint -> foot bone, thigh and shin together */
} D2rfLegsIn;

/* The library's own memory of one hero's legs, kept by the mod between steps; all zero = a fresh start. */
typedef struct D2rfState {
    uint32_t magic;         /* set by the library; another value: starts afresh */
    float phase;            /* radians of stride, from the distance walked */
    float dirX, dirZ;       /* the step's way on the ground (unit), turned toward the travel at a bounded rate */
    float move;             /* 0 standing .. 1 walking */
    float speed;            /* m/s, smoothed */
    float reserved[6];
} D2rfState;

typedef struct D2rfLegsOut {
    uint32_t size;          /* sizeof(D2rfLegsOut) as the caller has it */
    D2rfV3 foot[2];         /* where each foot bone goes */
    float lift[2];          /* 0 planted .. 1 the top of its swing */
    float drop;             /* how far the hips go down (model units, >= 0) so the legs reach their feet */
    float move;             /* 0 standing .. 1 walking */
    float strideM;          /* the stride's length used, metres (a foot's travel while it is planted) */
    float speedM;           /* the smoothed speed, m/s */
} D2rfLegsOut;

/* D2RF_ABI_VERSION of the library. */
D2RF_API int32_t D2rfAbi(void);
/* A short name and version for the log. */
D2RF_API const char* D2rfName(void);
/* One step: 1 done (out written), 0 refused (bad arguments: out untouched). */
D2RF_API int32_t D2rfLegsStep(const D2rfLegsIn* in, D2rfState* state, D2rfLegsOut* out);

typedef int32_t (*D2rfAbiFn)(void);
typedef const char* (*D2rfNameFn)(void);
typedef int32_t (*D2rfLegsStepFn)(const D2rfLegsIn* in, D2rfState* state, D2rfLegsOut* out);

#ifdef __cplusplus
}
#endif

#endif /* D2R_VR_GAIT_H */
