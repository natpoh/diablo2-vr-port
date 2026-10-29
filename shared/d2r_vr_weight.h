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

/* The library defines D2RW_BUILD_DLL before including this to export its functions; the mod never does. */
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

/* ---- The stab (OPTIONAL: found by GetProcAddress; a library without it is still version 1, and the mod then has no stab) ----
 * A thrusting weapon driven point first into a monster's body goes in and is held there ([weight] stab_in). The mod keeps the bodies (a monster's limbs: capsules, refreshed
 * every step), its own contact (the weapon stops on a body or glances off it) and the gate (its hit by touch says when the weapon thrusts and the hand is thrusting now); the
 * library decides whether a strike goes in and holds the weapon in the wound. Its state between steps is the mod's blob (zero = in no body); the library keeps nothing.
 * The mod calls D2rwStab after the weapon's step: while in a wound BEFORE its own contact (with no strike; it leaves that monster's bodies out of its contact meanwhile), and when
 * not in one AFTER its contact, with the strike it met (if any). With no bodies this step (n 0) a wound counts the body as missing. */
#define D2RW_STAB_BLOB 256
typedef struct D2rwStabState { uint8_t blob[D2RW_STAB_BLOB]; } D2rwStabState;

/* A monster's capsule in the world of the call: from a to b, radius round it; vel its velocity (zero when not known); id the monster; part which of its limbs (-1: its one
 * capsule) - a wound follows its limb. */
typedef struct D2rwBody {
    D2rwV3 a, b;
    double radius;
    D2rwV3 vel;
    uint32_t id;
    int32_t part;
} D2rwBody;

/* The weapon as the stab sees it and the stab's settings (the mod's sliders). A setting <= 0 is the library's own number (except where 0 is said to mean something). */
typedef struct D2rwStabber {
    uint32_t size;          /* sizeof(D2rwStabber) as the caller has it */
    D2rwV3 tip, base;       /* the stab line in the grip frame: the point, and where the blade (the part that goes in) begins */
    D2rwV3 com;             /* the weapon with its hands: centre of mass in the grip frame ... */
    double mass;            /* ... mass, kg ... */
    double inertia[9];      /* ... inertia about the centre of mass, grip-frame axes, row-major, kg m^2 */
    double angleCos;        /* the stab line against the surface's inward normal at least this to go in */
    double requiredSpeed;   /* m/s: the point's speed against the body at least this */
    double tipZone;         /* m: the contact within this of the tip, along the line (the point struck, not the edge) */
    double sharpness;       /* 0..1: the dampers x (1 - sharpness) */
    double shellDamper;     /* N s/m until the point has first been shellThickness deep */
    double shellThickness;  /* m */
    double damper;          /* N s/m past the shell */
    double depthLimit;      /* m: the deepest; <= 0 the blade's length (tip - base) */
    double lockSpeed;       /* m/s: the weapon slower than this - the slide locks */
    double lockTime;        /* s: ... and unlocks when the joint's force has stayed over unlockForce this long */
    double unlockForce;     /* N; 0 = never locks */
    double unstabThreshold; /* m: the point behind the entry by more than this - out */
    double unstabDelay;     /* s after going in before it may come out */
    double perpendicular;   /* m: the entry this far off the stab line - out */
    double tearDistance;    /* m: a hand's controller this far across the line from its hand on the weapon - out */
    double missingTime;     /* s: the body missing from the bodies this long - out */
} D2rwStabber;

/* A contact the mod's own contact met this step, before it put the weapon back - and the weapon as it was before that contact (the speed it came in with). */
typedef struct D2rwStrike {
    int32_t body;           /* index into the bodies; < 0: none */
    D2rwV3 point, normal;   /* where it met (on the weapon's surface) and the body's outward normal there */
    int32_t thrusting;      /* the mod's gate: the weapon thrusts and the hand is thrusting now */
    D2rwState before;       /* the weapon before the contact pushed it back */
} D2rwStrike;

/* What the stab did. why: a short reason (UTF-8): why a strike did not go in, or why the weapon came out ("" otherwise). */
typedef struct D2rwStabOut {
    int32_t in;             /* in a wound after the call */
    int32_t events;         /* 1 went in, 2 came out, 4 locked, 8 unlocked, 16 a full stab, 32 a strike was judged */
    int32_t locked;
    uint32_t id;            /* the monster */
    int32_t part;
    double depth, limit, deepest;
    double speed, cosInto, fromTip;   /* the judged strike */
    double lateral, force, pull;      /* the entry off the line, the joint's force (N), the hand across the line (m) */
    D2rwV3 entry, dir;                /* the wound: its entry and the line's way in (world) */
    char why[64];
} D2rwStabOut;

/* One stab step. The weapon's `state` is the one the mod's own step left (D2rwStep's or ours); `stabber.com/mass/inertia` is the body that state moves. gaps: each holding hand's
 * controller minus its hand drawn on the weapon (world). Returns 1 in a wound after the call (state may have been changed: the weapon held on its line), 0 in none, < 0 an error
 * (nothing changed). */
D2RW_API int32_t D2rwStab(const D2rwStabber* stabber, const D2rwBody* bodies, int32_t n, const D2rwStrike* strike, const D2rwV3* gaps, int32_t nGaps, double dt,
                          D2rwStabState* st, D2rwState* state, D2rwStabOut* out);

/* The same as pointers, for a loader (GetProcAddress by the names above). */
typedef int32_t (*D2rwAbiFn)(void);
typedef const char* (*D2rwNameFn)(void);
typedef int32_t (*D2rwStepFn)(const D2rwHold*, const D2rwHand*, int32_t, double, D2rwState*);
typedef int32_t (*D2rwThrowVelocityFn)(const uint64_t*, const D2rwV3*, int32_t, uint64_t, D2rwV3*);
typedef int32_t (*D2rwStabFn)(const D2rwStabber*, const D2rwBody*, int32_t, const D2rwStrike*, const D2rwV3*, int32_t, double, D2rwStabState*, D2rwState*, D2rwStabOut*);

#ifdef __cplusplus
}
#endif

#endif /* D2R_VR_WEIGHT_H */
