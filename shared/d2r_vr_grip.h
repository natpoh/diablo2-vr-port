/* d2r_vr_grip.h - the C interface between the D2R VR mod (d2rl-vrcam.dll) and an optional grip library (d2r_vr_grip.dll).
 *
 * The mod owns this header (shared/d2r_vr_grip.h in the d2r_vr repository); a library that implements it keeps a byte-identical copy (tools/check_abi.ps1 compares them).
 * Nothing in it comes from any third-party code: it only says what the mod knows about a hand on a weapon's shaft and what it wants back.
 *
 * What the library is: how a hand takes hold of a weapon's shaft and looks while it does - where the hand sits on the wood, how its fingers close round it, whether the grip is
 * tight or loose and how the hand slides and turns on the shaft when it is loose, and the pulses the hand feels. The mod keeps everything else: which hand holds the weapon,
 * where the weapon is, the weight, the aim, the hit by touch. The library keeps NOTHING between calls: the state each hand needs is a plain struct the mod owns and hands back
 * (zero it to start afresh), so the library has no handle to create or free and may be replaced at any time.
 *
 * Units: metres, seconds, radians. Right-handed frames, y up; the "world" of a call is whatever frame the mod works the hand in (all positions of one call are in one frame).
 * Quaternions are (x, y, z, w), unit. The hand's own frame ("hand frame") is a PROPER (right-handed) frame whose origin is the middle of the palm: x toward the fingers, y across the
 * knuckles, z out of the palm - the way the palm faces, toward what it holds. A left hand is a mirror image of a right one, but its frame is proper too (y runs the other way
 * round); the library never needs to know which hand it has. A library that is not the version the mod expects is not used (D2rgAbi). All functions are called from one thread
 * at a time for one state; they must not throw and must not call back into the mod.
 */
#ifndef D2R_VR_GRIP_H
#define D2R_VR_GRIP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Version 2 grows D2rgHandIn at its end (axisInHand, below). A version-2 library takes a version-1 mod's smaller struct (D2rgHandIn::size) and reads the new fields as zero; a version-1
 * library given a version-2 struct reads what it knew - the mod sees the version a library reports (D2rgAbi) and gives it nothing else. The mod uses a library of version 1 or 2. */
#define D2RG_ABI_VERSION 2
#define D2RG_ABI_OLDEST 1

/* The library defines D2RG_BUILD_DLL before including this to export the functions; the mod never does. */
#if defined(D2RG_BUILD_DLL) && defined(_WIN32)
#define D2RG_API __declspec(dllexport)
#else
#define D2RG_API
#endif

#define D2RG_MAX_FINGERS 5
#define D2RG_MAX_BONES 24   /* every finger bone of one hand, the thumb included (five fingers of four bones and a few to spare) */
#define D2RG_MAX_PIECES 3

typedef struct D2rgV3 { double x, y, z; } D2rgV3;
typedef struct D2rgQ { double x, y, z, w; } D2rgQ;

/* ---- the hand's rig: the same for every call while the hero, the hand and the pose it closes to stay the same ---- */

/* One finger bone. Bones are listed finger by finger, each finger root first, parents before children. A bone's frame is proper (a mirrored rig's bones are un-mirrored by the
 * mod); `offset` is where its origin is in its parent's frame - the parent of a finger's first bone is the hand frame - and `open` / `closed` are its rotation against the parent's
 * frame in the open pose and in the closed pose (the rotation whose columns are the bone's axes, written in the parent's frame). */
typedef struct D2rgBone {
    int32_t parent;      /* index of the parent bone in D2rgRig::bones, or -1: the hand frame */
    int32_t finger;      /* 0 .. nFingers-1 */
    D2rgV3 offset;       /* m */
    D2rgQ open, closed;
} D2rgBone;

typedef struct D2rgRig {
    uint32_t size;       /* sizeof(D2rgRig) as the caller has it */
    int32_t nFingers;    /* 0 .. D2RG_MAX_FINGERS */
    int32_t nBones;      /* 0 .. D2RG_MAX_BONES */
    D2rgBone bones[D2RG_MAX_BONES];
    D2rgV3 tip[D2RG_MAX_FINGERS];   /* a finger's tip beyond its last bone's origin, in that bone's frame (m) */
} D2rgRig;

/* ---- the shaft ---- */

/* A cylinder of the weapon's wood along its axis, from t0 to t1 (m along `dir` from `at`), with its radius. */
typedef struct D2rgPiece { double t0, t1, radius; } D2rgPiece;

typedef struct D2rgShaft {
    D2rgV3 at;           /* a point on the axis (world) */
    D2rgV3 dir;          /* unit, toward the tip */
    D2rgV3 up;           /* unit, square to dir: the weapon's own reference for how a hand lies round it */
    double butt, tip;    /* the wood's ends, m along dir from `at` (butt < tip): where a hand may take it */
    int32_t nPieces;     /* 0 .. D2RG_MAX_PIECES */
    D2rgPiece pieces[D2RG_MAX_PIECES];
} D2rgShaft;

/* ---- the line of the grip: which buttons hold the hand, and how it slides and turns (one call per hand per step) ---- */

#define D2RG_MODE_NONE 0     /* not holding */
#define D2RG_MODE_TIGHT 1    /* the point and the turn on the shaft are fixed */
#define D2RG_MODE_LOOSE 2    /* the hand slides along the shaft with a damper and turns freely about it */

#define D2RG_EV_HOVER 1u     /* a free hand has come within reach of the shaft */
#define D2RG_EV_GRAB 2u      /* the hand has closed on the shaft */
#define D2RG_EV_RELEASE 4u   /* the hand has let go */

typedef struct D2rgLineIn {
    uint32_t size;       /* sizeof(D2rgLineIn) */
    double dt;           /* s */
    double grip;         /* 0..1; < 0 not known */
    double trigger;      /* 0..1; < 0 not known */
    int32_t holds;       /* the mod has this hand on the shaft */
    int32_t second;      /* ... and it is the second hand on a long shaft: the only hand the line modes act on (the others are tight, held by the grip alone) */
    int32_t inReach;     /* the hand is free and within reach of the shaft */
    double t;            /* where the hand holds the shaft now, m along it (the mod's measure) */
    double raw;          /* where the controller's fist projects on the axis, the same measure, not clamped */
    double rollRaw;      /* the controller's turn about the shaft against the grip the hand took it in (rad) */
    double butt, tip;    /* the wood's ends, the same measure */
} D2rgLineIn;

typedef struct D2rgLineState {
    uint32_t size;       /* 0 in a fresh (zeroed) state; the library sets it to sizeof(D2rgLineState) and refuses a state of another size */
    int32_t gripOn, trigOn;     /* the two buttons as the library has seen them */
    int32_t wasHolds, wasNear;
    int32_t mode;
    double roll;         /* the hand's turn about the shaft against its first grip (rad) */
    double spare[4];
} D2rgLineState;

typedef struct D2rgLineOut {
    int32_t hold;        /* the hand keeps its hold */
    int32_t mode;        /* D2RG_MODE_* */
    double t;            /* where the hand holds the shaft now (m) */
    double roll;         /* the hand's turn about the shaft against its first grip (rad) */
    uint32_t events;     /* D2RG_EV_* since the last call */
    double hapticAmp;    /* 0..1 and ms of the strongest event of this call; 0 when there is none */
    int32_t hapticMs;
} D2rgLineOut;

/* ---- the hand on the shaft: where it sits, how its fingers close (one call per hand per step) ---- */

#define D2RG_HAND_FACES_AXIS 1u   /* flags: turn the hand about the shaft when it is placed on the wood so that the palm faces the axis - with axisInHand: so that the shaft lies where axisInHand says it
                                   * does, on the side of the palm the shaft is on now (when the palm is more than 2 cm off the axis; nearer, the turn stays); else it keeps the turn the knuckle
                                   * line leaves it */

typedef struct D2rgHandIn {
    uint32_t size;       /* sizeof(D2rgHandIn) */
    double dt;           /* s */
    int32_t holds;       /* the hand is on the shaft (0: it is free, the state is cleared) */
    int32_t mode;        /* D2rgLineOut::mode */
    D2rgV3 handPos;      /* the hand frame now, as the mod has the hand on the weapon (world): the middle of the palm ... */
    D2rgQ handRot;       /* ... and its turn */
    D2rgShaft shaft;     /* the shaft now */
    double roll;         /* D2rgLineOut::roll: the hand's turn about the shaft against the grip it took it in */
    double palmDepth;    /* m: from the middle of the palm out to its surface; <= 0: the library's own */
    D2rgV3 lineInHand;   /* the shaft's direction as the hand lies on it, in the hand frame; zero: the line of the knuckles (y) */
    uint32_t flags;      /* D2RG_HAND_* */
    int32_t hasFingersFrom;                   /* fingersFrom is filled */
    D2rgQ fingersFrom[D2RG_MAX_BONES];        /* each bone's rotation against its parent's frame as the hand has the fingers now: the fingers close from there, not from the open pose */
    /* ---- version 2 ---- */
    int32_t hasAxisInHand;   /* axisInHand is filled (0, or a struct that ends before this field: the version-1 way - the palm's surface on the wood's, palmDepth) */
    D2rgV3 axisInHand;       /* where the shaft lies against the hand when it holds it: the vector from the middle of the palm to the nearest point of the shaft's axis, in the hand frame (m), with the
                              * hand turned so that lineInHand runs along the shaft - the way the game's own grip has its hand on the weapon. Zero: the middle of the palm ON the axis. The library
                              * lays the palm's middle there (the foot of the perpendicular from the palm onto the axis stays the point it holds), whatever the radius of the wood. */
} D2rgHandIn;

typedef struct D2rgHandState {
    uint32_t size;       /* 0 in a fresh (zeroed) state; the library sets it to sizeof(D2rgHandState) and refuses a state of another size */
    int32_t phase;       /* 0 free, 1 moving onto the shaft, 2 on it */
    int32_t solved;      /* the closed fingers are worked out for this grab */
    double elapsed, duration;
    D2rgV3 startPos;     /* the hand when it took hold (world) */
    D2rgQ startRot;
    D2rgQ rel;           /* the hand's turn on the shaft, in the shaft's own frame, when it took hold */
    D2rgV3 off;          /* the palm's place against the axis point it holds, in the shaft's own frame */
    D2rgQ cur[D2RG_MAX_BONES];       /* each bone's rotation now */
    D2rgQ target[D2RG_MAX_BONES];    /* ... and where the grab closed it to */
    double percent[D2RG_MAX_BONES];  /* how far each bone went from open to closed (0..1) */
    int32_t stopped[D2RG_MAX_BONES]; /* the bone met the wood and stopped */
    double spare[4];
} D2rgHandState;

typedef struct D2rgHandOut {
    int32_t onShaft;     /* the hand frame below is the hand on the wood (moving onto it or on it) */
    int32_t fingersOn;   /* the fingers below are the grip's (they begin once the hand is on the wood) */
    D2rgV3 handPos;      /* the hand to draw (world) */
    D2rgQ handRot;
    D2rgQ finger[D2RG_MAX_BONES];    /* each bone's rotation against its parent's frame now */
    double t;            /* where on the shaft the hand holds it (m along dir from `at`) */
    double palmGap;      /* m between the palm's surface and the wood's (0: touching) */
    double tipGap;       /* m: the fingertip nearest the wood, from the wood's surface (negative: in it) */
    int32_t stopped;     /* the number of bones that met the wood */
} D2rgHandOut;

/* ---- the calls ---- */

/* The version of this header the library implements (D2RG_ABI_VERSION). Anything else: the mod does not use the library. */
D2RG_API int32_t D2rgAbi(void);
/* A short name and version for the log and the settings page ("name 1.0"). UTF-8, static. */
D2RG_API const char* D2rgName(void);
/* Which buttons hold the hand, how a loose hand slides and turns, the pulses. 0 for a step, negative for an error (state and out untouched). */
D2RG_API int32_t D2rgLineStep(const D2rgLineIn* in, D2rgLineState* state, D2rgLineOut* out);
/* Where the hand sits on the shaft and how its fingers close. 0 for a step, negative for an error (state and out untouched). */
D2RG_API int32_t D2rgHandStep(const D2rgHandIn* in, const D2rgRig* rig, D2rgHandState* state, D2rgHandOut* out);

/* The same as pointers, for a loader (GetProcAddress by the names above). */
typedef int32_t (*D2rgAbiFn)(void);
typedef const char* (*D2rgNameFn)(void);
typedef int32_t (*D2rgLineStepFn)(const D2rgLineIn*, D2rgLineState*, D2rgLineOut*);
typedef int32_t (*D2rgHandStepFn)(const D2rgHandIn*, const D2rgRig*, D2rgHandState*, D2rgHandOut*);

#ifdef __cplusplus
}
#endif

#endif /* D2R_VR_GRIP_H */
