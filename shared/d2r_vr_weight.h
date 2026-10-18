/* d2r_vr_weight.h - the C interface between the D2R VR mod (d2rl-vrcam.dll) and an optional weight-physics library (d2r_vr_weight.dll).
 *
 * The mod owns this header (shared/d2r_vr_weight.h in the d2r_vr repository); a library that implements it keeps a byte-identical copy (tools/check_abi.ps1 compares them).
 * Nothing in it comes from any third-party code: it only says what the mod knows about a weapon in the hands and what it wants back.
 *
 * What the library is: the held weapon and the hands on it, as one body pulled to the controllers. The mod keeps everything else - which hand holds it, taking, passing, letting go,
 * falling, the back, the aim, the hit by touch. Per step the mod gives the library the weapon's pieces, the hands on it (their palms and the controllers' poses across the frame)
 * and the body's state; the library moves the state forward by dt and gives it back. It keeps nothing between calls (the mod may reset the state at any moment), so a library has no
 * handle to create or free.
 *
 * Units: metres, kilograms, seconds, radians. Right-handed frames, y up; the mod's "room" frame is the world of the call. The grip frame is the weapon's own: its origin is where the
 * first hand holds it, its axes the weapon's. Quaternions are (x, y, z, w), unit. A library that is not the version the mod expects is not used (D2rwAbi). All functions are
 * called from one thread at a time, under the mod's lock. They must not throw and must not call back into the mod.
 */
#ifndef D2R_VR_WEIGHT_H
#define D2R_VR_WEIGHT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define D2RW_ABI_VERSION 1

/* The library defines D2RW_BUILD_DLL before including this to export the four functions; the mod never does. */
#if defined(D2RW_BUILD_DLL) && defined(_WIN32)
#define D2RW_API __declspec(dllexport)
#else
#define D2RW_API
#endif

typedef struct D2rwV3 { double x, y, z; } D2rwV3;
typedef struct D2rwQ { double x, y, z, w; } D2rwQ;

/* A cylinder of the weapon's steel, from a to b in the grip frame; mass in kg (the mod has applied its "weight" setting). */
typedef struct D2rwPiece {
    D2rwV3 a, b;
    double radius, mass;
} D2rwPiece;

/* One hand on the weapon. The controller moves from its `from` pose to its `to` pose across the frame; the velocities are the controller's own, in the world. */
typedef struct D2rwHand {
    D2rwV3 palm;        /* the palm, in the grip frame */
    D2rwQ gripRot;      /* the hand's rotation in the grip frame: hand = grip * gripRot */
    D2rwV3 fromPos;
    D2rwQ fromRot;
    D2rwV3 fromVel, fromAngVel;
    D2rwV3 toPos;
    D2rwQ toRot;
    D2rwV3 toVel, toAngVel;
} D2rwHand;

/* The weapon and the settings of one step. */
typedef struct D2rwHold {
    uint32_t size;          /* sizeof(D2rwHold) as the caller has it */
    D2rwPiece pieces[3];
    int32_t nPieces;        /* 1..3 */
    int32_t twoHandedKind;  /* the game's "2handed" kind (a two-handed sword, a great axe, a maul...) */
    double gravity;         /* g on the steel: 1 = the real 9.81 m/s2 down (-y), 0 = none */
    double damping;         /* the hands' dampers x this (1 = as the library has them) */
    double strength;        /* the hands' strength x this (1 = as the library has it) */
    double snap;            /* m: the weapon this far from the first hand's palm is put back in it */
    uint32_t flags;         /* bit 0: the weapon's weight in the palm (a comfort setting: the library may ignore it) */
    double substep;         /* s: the largest step inside the frame; 0 = the library's own */
} D2rwHold;

/* The weapon's state: its grip frame in the world. vel is the velocity of the grip frame's origin, angVel the body's. */
typedef struct D2rwState {
    D2rwV3 pos;
    D2rwQ rot;
    D2rwV3 vel, angVel;
} D2rwState;

/* The version of this header the library implements (D2RW_ABI_VERSION). Anything else: the mod does not use the library. */
D2RW_API int32_t D2rwAbi(void);
/* A short name and version for the log and the settings page ("name 1.0"). UTF-8, static. */
D2RW_API const char* D2rwName(void);
/* Moves `state` forward by dt seconds with the hands pulling the weapon. Returns 1 when the weapon was put back in the first hand (it was farther than `snap`: state is then the
 * weapon at rest in that hand), 0 for an ordinary step, negative for an error (state untouched). nHands is 1 or 2; the first hand is the main one. */
D2RW_API int32_t D2rwStep(const D2rwHold* hold, const D2rwHand* hands, int32_t nHands, double dt, D2rwState* state);
/* A throw's launch velocity (m/s, world) from the weapon's recent velocities: n samples, oldest first, at tMs milliseconds, up to nowMs. 1 and *out filled, or 0: the mod's own. */
D2RW_API int32_t D2rwThrowVelocity(const uint64_t* tMs, const D2rwV3* vel, int32_t n, uint64_t nowMs, D2rwV3* out);

/* The same as pointers, for a loader (GetProcAddress by the names above). */
typedef int32_t (*D2rwAbiFn)(void);
typedef const char* (*D2rwNameFn)(void);
typedef int32_t (*D2rwStepFn)(const D2rwHold*, const D2rwHand*, int32_t, double, D2rwState*);
typedef int32_t (*D2rwThrowVelocityFn)(const uint64_t*, const D2rwV3*, int32_t, uint64_t, D2rwV3*);

#ifdef __cplusplus
}
#endif

#endif /* D2R_VR_WEIGHT_H */
