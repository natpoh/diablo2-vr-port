/* d2r_vr_props.h - the C interface between the D2R VR mod (d2rl-vrcam.dll) and the optional props world of the weight library (the D2rp* exports of d2r_vr_weight.dll).
 *
 * The mod owns this header (shared/d2r_vr_props.h in the d2r_vr repository); a library that implements it keeps a byte-identical copy (tools/check_abi.ps1 compares them).
 * Nothing in it comes from any third-party code: it only says what the mod knows about the level's small props and the hands near them, and what it wants back.
 *
 * What the library is: a small rigid-body world for the level's own props (cups, plates, vases, books, stools...) that the held weapon and the bare fists knock about. A prop
 * stands frozen where the game placed it until something reaches it; then it falls, bounces and rolls, and freezes again where it comes to rest. The weapon in it is not a
 * kinematic stick: it is a body of the weapon's own mass, pulled to the weapon as drawn by a drive with the hands' spring, damper and force ceilings - a heavy maul throws a cup
 * harder than a dagger, and a barrel holds a light sword back. The mod keeps everything else: which props (the game's own models, its catalogue), what they stand on, the floor,
 * the drawing (the game's model is moved where the prop lies), the haptics, the settings. Nothing of the game's state is changed by any of it: it is only drawn.
 *
 * Unlike the weight library's other parts this one keeps state: a world made by D2rpCreate and freed by D2rpDestroy. All functions of one world are called from ONE thread (the
 * mod's physics thread), never two at a time; they must not throw and must not call back into the mod. A library that is not the version the mod expects is not used (D2rpAbi).
 *
 * Units: metres, kilograms, seconds, newton-seconds; y up. Positions are in the mod's physics frame: the render world in metres, less an origin the mod picks near the hero
 * (so floats stay exact). Quaternions are (x, y, z, w), unit.
 */
#ifndef D2R_VR_PROPS_H
#define D2R_VR_PROPS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define D2RP_ABI_VERSION 1

/* The library defines D2RP_BUILD_DLL before including this to export the functions; the mod never does. */
#if defined(D2RP_BUILD_DLL) && defined(_WIN32)
#define D2RP_API __declspec(dllexport)
#else
#define D2RP_API
#endif

#define D2RP_MAX_PROPS 192     /* props in the world at once, frozen or moving */
#define D2RP_MAX_STATICS 128   /* what they stand on: the surfaces under them, walls */
#define D2RP_MAX_MOVERS 4      /* 0 the right hand's weapon or fist, 1 the left's, 2 the shield, 3 spare */

/* A prop's shape: an upright cylinder in its bounds (cups, vases, barrels), a box (books, stools), a ball (apples). */
#define D2RP_SHAPE_ROUND 0
#define D2RP_SHAPE_BOX 1
#define D2RP_SHAPE_BALL 2
/* What it is made of: how heavy, how it slides, bounces and rolls. */
#define D2RP_MAT_CERAMIC 0
#define D2RP_MAT_GLASS 1
#define D2RP_MAT_METAL 2
#define D2RP_MAT_WOOD 3
#define D2RP_MAT_PAPER 4
#define D2RP_MAT_BONE 5
#define D2RP_MAT_WICKER 6
#define D2RP_MAT_FOOD 7
#define D2RP_MAT_CLOTH 8
#define D2RP_MAT_FURNITURE 9
#define D2RP_MAT_STONE 10
#define D2RP_MAT_SACK 11
#define D2RP_MATERIALS 12

typedef struct D2rpParams {
    uint32_t size;          /* sizeof(D2rpParams) as the caller has it */
    float gravity;          /* m/s2 down (-y); 9.81 */
    int32_t substeps;       /* the solver's substeps inside one D2rpStep (4) */
    int32_t maxMoving;      /* props moving at once at most (12); a weapon reaching one more freezes the one that has moved longest */
    float restSpeed;        /* m/s: a prop slower than this ... */
    float restTime;         /* ... for this long (s) is frozen where it lies */
    float moveLimit;        /* s: a prop never moves longer than this */
    float strength;         /* the hands' drive x this (1 = the weight library's own numbers) */
    float propMass;         /* the props as heavy as their stuff x this (1) */
} D2rpParams;

/* A prop as the mod sees it: id (the game's entity), its body's place, and its bounds in its body's own frame (centre and half extents). */
typedef struct D2rpProp {
    uint32_t id;
    uint8_t shape;          /* D2RP_SHAPE_* */
    uint8_t material;       /* D2RP_MAT_* */
    uint16_t reserved;
    float p[3];
    float q[4];
    float centre[3], half[3];
} D2rpProp;

/* Something that does not move: an axis-aligned box (a surface a prop stands on, a wall). */
typedef struct D2rpBox {
    float lo[3], hi[3];
    float friction, bounce;
} D2rpBox;

/* A hand's weapon or fist: a capsule from a to b, `radius` round it, where the mod draws it at `time`, moving at vel / angVel then (vel: of point a). mass is the weapon with the
 * hands on it (kg); hands (1 or 2) pull it, each with the weight library's ceilings. on 0: none in that slot (its body goes). kind: 0 a weapon, 1 a bare fist, 2 a shield. */
typedef struct D2rpMover {
    int32_t on;
    int32_t kind;
    int32_t hands;
    int32_t reserved;
    float a[3], b[3];
    float radius;
    float mass;
    float vel[3], angVel[3];
    double time;            /* s, on the same clock as D2rpStep's `now` */
} D2rpMover;

/* A prop that moved in the last step: its body's place now. state 1: moving; 2: it came to rest in this step (frozen there: the mod's last write). */
typedef struct D2rpPose {
    uint32_t id;
    int32_t state;
    float p[3];
    float q[4];
} D2rpPose;

/* A prop set going: by mover slot (0..3), or -1 by another moving thing (a prop, a bottle). speed: the mover's speed then (m/s); impulse: the prop's momentum after its first
 * step (N s) - how hard it was struck. */
typedef struct D2rpKnock {
    uint32_t id;
    int32_t mover;
    float speed;
    float impulse;
} D2rpKnock;

/* A mover after the last step: where its body is (it may lag the drawn weapon where a heavy prop holds it back) and what the props gave it (N s, the sum of contact impulses). */
typedef struct D2rpMoverOut {
    int32_t on;
    float a[3], b[3];
    float impulse;
} D2rpMoverOut;

/* ---- The hands that take props ([weight] grab_props) - OPTIONAL exports: a library without D2rpSetHands / D2rpHandAt takes nothing (the ABI stays 1) ----
 * An empty hand's grip on a prop takes it: the hand is a body of its own in this world, driven to the controller with the hand's own strength; the grip pressed with the palm
 * near a prop's surface joins the hand to that prop at the point under the palm, the prop drawn there from then on - carried by the hand, its weight against the hand's
 * strength, knocking what it meets - and let go (the grip) it flies on as the hand threw it, and rests where it falls as a knocked prop does. Only drawn, like the rest.
 * Hands: [0] the right, [1] the left; a hand that holds a prop does not knock with its mover slot of the same number (the fist would push the prop out of it). */
#define D2RP_HANDS 2
#define D2RP_HAND_MAX_FORCE 300   /* N: a hand's pull at strength 1 (what it holds weighs at most half of it: a heavy prop is slower in the hand, never stuck) */

/* A hand, physics frame. pos: the palm (where the hand is drawn with nothing in it), rot its turn; vel / angVel the controller's at `time` (the movers' clock); grip 0..1 (< 0:
 * not known - not squeezed). mayTake 0: it takes nothing new now (it holds something else, or another grab is over a body) - a hold already made goes on. A number <= 0 is the
 * library's own. */
typedef struct D2rpHand {
    int32_t tracked;
    int32_t mayTake;
    float grip;
    float grabRadius;       /* m: the palm this near a prop's surface is over it (8 cm) */
    float breakDistance;    /* m: the controller this far from the hand that holds tears the grip loose (1 m) */
    float strength;         /* the hand's strength x this (1) */
    float pos[3];
    float rot[4];
    float vel[3], angVel[3];
    double time;
} D2rpHand;

#define D2RP_HAND_EMPTY 0
#define D2RP_HAND_HOVER 1     /* over a prop it would take */
#define D2RP_HAND_TAKING 2    /* taken: the prop coming into the hand */
#define D2RP_HAND_HOLDING 3   /* in the hand */
#define D2RP_HEV_HOVER 1      /* came over a prop */
#define D2RP_HEV_GRAB 2       /* took it */
#define D2RP_HEV_JOINED 4     /* it is in the hand */
#define D2RP_HEV_RELEASE 8    /* let go: the grip */
#define D2RP_HEV_TORN 16      /* tore loose: the controller past the break distance */
#define D2RP_HEV_LOST 32      /* the prop is gone (a new world, the hand lost its tracking) */

/* A hand after the steps since its last D2rpHandAt (the events and the pulse gathered over them; the rest as the last step left it). Physics frame. */
typedef struct D2rpHandOut {
    int32_t state;
    int32_t events;
    uint32_t id;            /* the prop hovered or held (state > 0) */
    uint8_t shape, material;
    uint16_t reserved;
    float pos[3];           /* where to draw the palm: the hand's body on the prop (state >= TAKING); else the palm as given */
    float rot[4];
    float p[3], q[4];       /* the prop as it stands now (state > 0) */
    float centre[3], half[3];   /* its bounds in its own frame (as the mod gave them) */
    float point[3];         /* the point of its surface the hand holds (state >= TAKING) or would take (hover) */
    float massKg;           /* its mass; held (state >= TAKING): what it weighs in the hand (a heavy prop less, D2RP_HAND_MAX_FORCE) */
    float gap;              /* m: the controller off the hand */
    float force;            /* N: the hand's pull on what it holds */
    float hapticAmp, hapticSec, hapticHz;   /* a pulse to play in this hand (amplitude 0: none) */
} D2rpHandOut;

typedef int32_t (*D2rpAbiFn)(void);
/* A world, or null. Frees with D2rpDestroy. */
typedef void* (*D2rpCreateFn)(const D2rpParams* params);
typedef void (*D2rpDestroyFn)(void* world);
/* Everything goes (a new area, a new origin); the floor's top is at y. */
typedef int32_t (*D2rpClearFn)(void* world, float floorY);
/* The props in reach now: a new id is added frozen where it is; a frozen prop not in the list goes; a moving one stays until it rests. How many there are, < 0 an error. */
typedef int32_t (*D2rpSetPropsFn)(void* world, const D2rpProp* props, int32_t n);
/* What the props stand on, all of it (replaced; unchanged boxes rebuild nothing). */
typedef int32_t (*D2rpSetStaticsFn)(void* world, const D2rpBox* boxes, int32_t n);
/* The movers' latest poses (n up to D2RP_MAX_MOVERS, slot = index). */
typedef int32_t (*D2rpSetMoversFn)(void* world, const D2rpMover* movers, int32_t n);
/* One fixed step of dt seconds at `now` (s, the movers' clock): frozen props a mover is about to reach are set going, the movers are driven to where they are drawn, the world
 * steps, still props freeze. How many props are moving, < 0 an error. */
typedef int32_t (*D2rpStepFn)(void* world, double now, float dt);
/* The props that moved in the last step (moving or just come to rest). How many written. */
typedef int32_t (*D2rpPosesFn)(void* world, D2rpPose* out, int32_t max);
/* The props set going since the last call. How many written. */
typedef int32_t (*D2rpKnocksFn)(void* world, D2rpKnock* out, int32_t max);
/* A mover after the last step. 1, or 0 when that slot has no body. */
typedef int32_t (*D2rpMoverAtFn)(void* world, int32_t slot, D2rpMoverOut* out);
/* (optional) The hands' latest (n up to D2RP_HANDS, index = hand); n 0: every hand lets go at once. 0, < 0 an error. */
typedef int32_t (*D2rpSetHandsFn)(void* world, const D2rpHand* hands, int32_t n);
/* (optional) A hand after the steps since the last call for it (its events and pulse are read out). 1, or 0 when that hand has no body (not tracked). */
typedef int32_t (*D2rpHandAtFn)(void* world, int32_t hand, D2rpHandOut* out);

D2RP_API int32_t D2rpAbi(void);
D2RP_API void* D2rpCreate(const D2rpParams* params);
D2RP_API void D2rpDestroy(void* world);
D2RP_API int32_t D2rpClear(void* world, float floorY);
D2RP_API int32_t D2rpSetProps(void* world, const D2rpProp* props, int32_t n);
D2RP_API int32_t D2rpSetStatics(void* world, const D2rpBox* boxes, int32_t n);
D2RP_API int32_t D2rpSetMovers(void* world, const D2rpMover* movers, int32_t n);
D2RP_API int32_t D2rpStep(void* world, double now, float dt);
D2RP_API int32_t D2rpPoses(void* world, D2rpPose* out, int32_t max);
D2RP_API int32_t D2rpKnocks(void* world, D2rpKnock* out, int32_t max);
D2RP_API int32_t D2rpMoverAt(void* world, int32_t slot, D2rpMoverOut* out);
D2RP_API int32_t D2rpSetHands(void* world, const D2rpHand* hands, int32_t n);
D2RP_API int32_t D2rpHandAt(void* world, int32_t hand, D2rpHandOut* out);

#ifdef __cplusplus
}
#endif

#endif /* D2R_VR_PROPS_H */
