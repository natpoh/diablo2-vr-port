/* d2r_vr_grab.h - the C interface between the D2R VR mod (d2rl-vrcam.dll) and the optional grab of the weight library (d2r_vr_weight.dll 1.4 or newer: the D2rgb* exports).
 *
 * The mod owns this header (shared/d2r_vr_grab.h in the d2r_vr repository); the library keeps a byte-identical copy (tools/check_abi.ps1 compares them).
 * Nothing in it comes from any third-party code: it only says what the mod knows about the bodies and the hands, and what it wants back.
 *
 * What the library is here: the hands that grab ([weight] grab_bodies). Each step the mod hands it the bodies near the hero (a capsule on each limb of a monster or an NPC,
 * as the game poses them, in the render world) and the two hands (where each palm is, its grip, whether it may take anything now); the library does the rest of the grab -
 * which body a hand is over, taking it, the joint between the hand and the body, how the hand's strength moves the body by its weight (a humanoid's limb and its monster give
 * way, anything else holds the hand), letting go and tearing loose - and says where each hand is to be drawn, the pulse to play in it, and how far each held body is out of
 * the game's pose (the mod draws its limbs and its lean from that, as it draws the doll's sway). Nothing of the game's moves: the game's units stay where the server has them.
 *
 * The library keeps its world between calls (create one, step it, destroy it). A library that is not the version the mod expects is not used (D2rgbAbi). One thread at a time
 * per world; nothing throws or calls back into the mod.
 *
 * Units: metres, kilograms, seconds. The render world: y up. Quaternions (x, y, z, w). Hands: [0] the right, [1] the left.
 */
#ifndef D2R_VR_GRAB_H
#define D2R_VR_GRAB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define D2RGB_ABI_VERSION 5   /* 2: D2rgbBody::parent and D2rgbParams::muscle (a held monster bends at its joints); 3: D2rgbHandOut::hold* (the capsule held, as it stands);
                                 * 4: a limb's sway is its line's absolute turn (below); 5: D2rgbLinks (every held limb as it stands), the body sways across the ground only */

/* The library defines D2RGB_BUILD_DLL before including this to export its functions; the mod never does. */
#if defined(D2RGB_BUILD_DLL) && defined(_WIN32)
#define D2RGB_API __declspec(dllexport)
#else
#define D2RGB_API
#endif

#define D2RGB_MAX_BODIES 64
#define D2RGB_MAX_SWAYS 32

/* The mod's settings. A number <= 0 is the library's own (except where 0 is said to mean something). */
typedef struct D2rgbParams {
    uint32_t size;          /* sizeof(D2rgbParams) as the caller has it */
    float limbHz;           /* a limb's muscles: how fast they bring it back to the game's pose (its spring's own frequency) */
    float bodyHz;           /* the whole monster's (its feet hold it) */
    float damping;          /* share of the critical damping of both */
    float maxLimbM;         /* the most a limb's far end is out of the game's pose */
    float maxBodyM;         /* the most the whole monster is */
    float massScale;        /* the monsters as heavy as their size gives, times this */
    float strength;         /* the hands' strength times this (1: the library's own hand) */
    float grabRadius;       /* m: the palm this near a body's surface is over it (can take it) */
    float breakDistance;    /* m: the controller this far from the point the hand holds tears the grip loose */
    float gripThreshold;    /* the grip squeezed from this ... */
    float gripRelease;      /* ... and let go below this (<= 0: below gripThreshold) */
    float muscle;           /* the held monster's muscles - how hard each joint holds its limb to the game's pose - times this (1: as its size gives; <= 0: 1) */
} D2rgbParams;

/* A body: a capsule from a to b, radius round it, render world, as the game poses it now (never the sway the library reported: the library adds that itself). id: the game's
 * unit id; bone: the limb's bone of its skeleton (-1: the game's capsule of the whole unit). doll 1: a humanoid's limb (the mod's judgement) - it may give way; 0: it stands. parent: the body of the
 * limb above it (index into the same bodies: the forearm's is the upper arm, the upper arm's the trunk), -1 none (a root) - a held limb bends at its joints up that chain. */
typedef struct D2rgbBody {
    uint32_t id;
    int32_t bone;
    float a[3], b[3];
    float radius;
    int32_t doll;
    int32_t parent;
} D2rgbBody;

/* A hand. pos: the palm - where the hand is drawn with nothing in it - and rot its turn, render world; grip 0..1 (< 0: not known - not squeezed). mayTake 0: it takes
 * nothing new now (it holds something of the mod's: a weapon, a bottle; its grip is another gesture's) - a hold already made goes on. */
typedef struct D2rgbHand {
    int32_t tracked;
    int32_t mayTake;
    float grip;
    float pos[3];
    float rot[4];
} D2rgbHand;

/* What a hand did this step. */
#define D2RGB_EMPTY 0
#define D2RGB_HOVER 1       /* over a body it would take */
#define D2RGB_TAKING 2      /* taken, the hand and the body coming together */
#define D2RGB_HOLDING 3     /* joined to it */
#define D2RGB_EV_HOVER 1    /* came over a body */
#define D2RGB_EV_GRAB 2     /* took it */
#define D2RGB_EV_JOINED 4   /* the joint is made */
#define D2RGB_EV_RELEASE 8  /* let go: the grip */
#define D2RGB_EV_TORN 16    /* tore loose: pulled past the break distance, or a body that stands left too far behind */
#define D2RGB_EV_LOST 32    /* the body is not among the bodies any more */
typedef struct D2rgbHandOut {
    int32_t state;
    int32_t events;
    uint32_t id;            /* the body hovered or held (state > 0) */
    int32_t bone;
    float pos[3];           /* where to draw the hand (state >= TAKING): the physics hand on the body; else the palm as given */
    float force;            /* N: the hand's pull on what it holds */
    float gap;              /* m: the controller off the hand */
    float massKg;           /* the monster held (0: none, or not known) */
    int32_t gives;          /* 1: what it holds gives way (a doll limb); 0: it stands */
    float hapticAmp, hapticSec, hapticHz;   /* a pulse to play in this hand now (amplitude 0: none) */
    int32_t holdOk;                     /* the capsule below is filled (state >= TAKING) */
    float holdA[3], holdB[3], holdR;    /* what the hand holds as it stands now: the limb as the library has moved it (or the body that stands), render world - the mod lays the hand on it */
} D2rgbHandOut;

/* How far a held body is out of the game's pose: bone -1 the whole monster (every body of it moved by off), else that limb's line - where its far end points now against
 * where the game has it, from its joint (the line's absolute turn: the bones above it are not taken off; the mod turns each bone from where its parents left it to
 * this). vel: its velocity. */
typedef struct D2rgbSway {
    uint32_t id;
    int32_t bone;
    float off[3], vel[3];
} D2rgbSway;

/* A held limb as it stands now: monster id's bone, from its joint end (joint) to its far end (tip), render world - the bodies the mod hands round (its debug view, the weapon's
 * contact) are put there while it is held. */
typedef struct D2rgbLink {
    uint32_t id;
    int32_t bone;
    float joint[3], tip[3];
} D2rgbLink;

typedef int32_t (*D2rgbAbiFn)(void);
typedef const char* (*D2rgbNameFn)(void);
typedef void* (*D2rgbCreateFn)(const D2rgbParams* params);
typedef void (*D2rgbDestroyFn)(void* world);
typedef int32_t (*D2rgbStepFn)(void* world, const D2rgbParams* params, const D2rgbBody* bodies, int32_t n, const D2rgbHand* hands, float dt, D2rgbHandOut* out);
typedef int32_t (*D2rgbSwaysFn)(void* world, D2rgbSway* out, int32_t max);
typedef int32_t (*D2rgbReleaseFn)(void* world);
typedef int32_t (*D2rgbLinksFn)(void* world, D2rgbLink* out, int32_t max);

D2RGB_API int32_t D2rgbAbi(void);
D2RGB_API const char* D2rgbName(void);
/* A world of its own (null: not made). */
D2RGB_API void* D2rgbCreate(const D2rgbParams* params);
D2RGB_API void D2rgbDestroy(void* world);
/* Time goes on by dt (s) with the bodies as they are now and both hands (hands[2], out[2]). How many hands hold something after it, < 0 an error. */
D2RGB_API int32_t D2rgbStep(void* world, const D2rgbParams* params, const D2rgbBody* bodies, int32_t n, const D2rgbHand* hands, float dt, D2rgbHandOut* out);
/* The held bodies' sways after the last step, up to max; how many. A body let go is no longer told (the mod springs it back from its last sway). */
D2RGB_API int32_t D2rgbSways(void* world, D2rgbSway* out, int32_t max);
/* The held limbs as they stand after the last step, up to max (D2RGB_MAX_SWAYS is enough); how many. */
D2RGB_API int32_t D2rgbLinks(void* world, D2rgbLink* out, int32_t max);
/* Every hand lets go at once (no pulse): the view changed, the feature was switched off. How many let go. */
D2RGB_API int32_t D2rgbReleaseAll(void* world);

#ifdef __cplusplus
}
#endif

#endif /* D2R_VR_GRAB_H */
