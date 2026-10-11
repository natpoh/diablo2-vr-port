// SPDX-License-Identifier: MIT
// Copyright (c) 2026 BodyWalkVR
//
// bodywalk/game_link.h - BodyWalk Game Link: how a game mod and BodyWalk talk.
//
// One header for game mods, one plugin in BodyWalk (game_link.dll) that knows
// nothing about any game. The mod says what it offers (Manifest) and what it is
// doing (GameState); BodyWalk hands it the player's head and hands (Tracking)
// and the actions the player's gestures and buttons fire (Actions). A game that
// shows itself in the headset through OpenXR of its own (no FlatVR) hands its
// headset and controllers to BodyWalk (XrInput) and gets BodyWalk's gesture
// zones to draw (VrZones). Any process - the game, its settings program - may
// ask BodyWalk for FlatVR things (Requests); BodyWalk says what it can do
// (Host).
//
// Every block is named shared memory, Local\BodyWalk.Link.<Block>, always
// BW_LINK_BLOCK_BYTES long, made by whichever side comes first (BW_LINK_SDDL:
// a process of low integrity may write too) and starting with BW_LinkHeader.
//
//  - Structs only grow at the end; a reader takes min(header.size, its own
//    sizeof) and finds zeros past what an older writer filled. No magic per
//    field.
//  - A block with one writer is a seqlock: the writer makes header.sequence
//    odd, writes, makes it even again; a reader copies the block and copies
//    again if the sequence was odd or moved meanwhile (bw::link::Read). The
//    sequence moving is also the writer's pulse.
//  - Alive: the writer's process (header.writerPid) runs and its sequence
//    moves. The game writes GameState at least every 500 ms; BodyWalk takes a
//    game whose GameState stood still for 3 s for gone.
//  - One game at a time.
//
// Requests is the one block with many writers: each request is a
// {sequence, value} pair - put the value, then InterlockedIncrement the
// sequence (bw::link::Request); the last one wins. BodyWalk acts on requests
// made while it runs (it takes the sequences it finds at its start as done).
//
// Times are BW_LinkStampNow(): QueryPerformanceCounter in 0.1 ms, low 32 bits
// - FlatVR's clock, so a stamp means the same in every process.
//
// Spaces: metres; x right, y up, z back (OpenXR's axes); rotations x y z w.
// Yaw is positive to the left (about +y).
#ifndef BODYWALK_GAME_LINK_H
#define BODYWALK_GAME_LINK_H

#include <stdint.h>

#define BW_LINK_MAGIC 0x4B4E4C42u          // "BLNK"
#define BW_LINK_BLOCK_BYTES 65536u          // every block, whatever its struct
// Everyone may use it; a process of low integrity may write too.
#define BW_LINK_SDDL L"D:(A;;GA;;;WD)S:(ML;;NW;;;LW)"

#define BW_LINK_MANIFEST_NAME  L"Local\\BodyWalk.Link.Manifest"   // game -> BodyWalk
#define BW_LINK_GAMESTATE_NAME L"Local\\BodyWalk.Link.GameState"  // game -> BodyWalk
#define BW_LINK_TRACKING_NAME  L"Local\\BodyWalk.Link.Tracking"   // BodyWalk -> game
#define BW_LINK_ACTIONS_NAME   L"Local\\BodyWalk.Link.Actions"    // BodyWalk -> game
#define BW_LINK_REQUESTS_NAME  L"Local\\BodyWalk.Link.Requests"   // anyone -> BodyWalk
#define BW_LINK_HOST_NAME      L"Local\\BodyWalk.Link.Host"       // BodyWalk -> anyone
#define BW_LINK_XRINPUT_NAME   L"Local\\BodyWalk.Link.XrInput"    // game (its own OpenXR) -> BodyWalk
#define BW_LINK_VRZONES_NAME   L"Local\\BodyWalk.Link.VrZones"    // BodyWalk -> game (its own OpenXR)

#if defined(__cplusplus)
#define BW_LINK_STATIC_ASSERT(c, m) static_assert(c, m)
#else
#define BW_LINK_STATIC_ASSERT(c, m) _Static_assert(c, m)
#endif

#pragma pack(push, 8)

typedef struct BW_LinkHeader {
    uint32_t magic;      // BW_LINK_MAGIC once written
    uint32_t version;    // the block's version (BW_LINK_*_VERSION)
    uint32_t size;       // bytes the writer filled: sizeof its struct
    uint32_t sequence;   // seqlock: odd while being written; moves = the writer is alive
    uint32_t writerPid;  // the writer's process
    uint32_t reserved;
} BW_LinkHeader;
BW_LINK_STATIC_ASSERT(sizeof(BW_LinkHeader) == 24, "BW_LinkHeader is a wire format");

// ---------------------------------------------------------------------------
// Manifest (game -> BodyWalk): who the game is and what it offers, written
// when the game starts. BodyWalk registers its actions and gesture tabs and
// keeps a copy (BW_LINK_CACHE_DIR below), so they can be mapped before the
// game runs.
//
// Actions are named with the game's prefix ("D2R: Attack") and must be unique.
// BodyWalk's Mapping refers to an action by its place in BodyWalk's list: add
// new actions at the end, never in between, or the player's mappings move.
#define BW_LINK_MANIFEST_VERSION 1u
#define BW_LINK_MAX_ACTIONS 256u
#define BW_LINK_MAX_TABS 64u
#define BW_LINK_ACTION_CHARS 64u

typedef struct BW_LinkTab {
    char name[64];           // the Mapping tab, e.g. "D2R Bow"; "main" is BodyWalk's own
    char description[128];
} BW_LinkTab;

typedef struct BW_LinkManifest {
    BW_LinkHeader header;
    char gameId[32];         // stable, lower case: "d2r", "cp2077"
    char displayName[64];    // "Diablo II: Resurrected"
    char modName[64];        // "D2R VR"
    char modVersion[32];     // "0.157"
    uint32_t actionCount;
    uint32_t tabCount;
    char actions[BW_LINK_MAX_ACTIONS][BW_LINK_ACTION_CHARS];
    BW_LinkTab tabs[BW_LINK_MAX_TABS];
} BW_LinkManifest;
BW_LINK_STATIC_ASSERT(sizeof(BW_LinkManifest) == 24 + 192 + 8 + 256 * 64 + 64 * 192, "BW_LinkManifest is a wire format");

// ---------------------------------------------------------------------------
// GameState (game -> BodyWalk): what the game wants now. Written whenever it
// changes and at least every 500 ms.
#define BW_LINK_GAMESTATE_VERSION 1u

typedef struct BW_LinkGameState {
    BW_LinkHeader header;
    char gameId[32];          // the Manifest's
    char categories[256];     // the Mapping tabs in force, "main, D2R Bow"; "" = "main"
    int32_t headLock;         // FlatVR's Head Lock: -1 no wish, 0 free in the room, 1 on the head
    float screenDistanceM;    // FlatVR's screen this far for now; 0 = the player's own distance
} BW_LinkGameState;
BW_LINK_STATIC_ASSERT(sizeof(BW_LinkGameState) == 24 + 32 + 256 + 8, "BW_LinkGameState is a wire format");

// ---------------------------------------------------------------------------
// Tracking (BodyWalk -> game): the head and hands, each time BodyWalk has
// them (Universal tracking output on).
#define BW_LINK_TRACKING_VERSION 1u

enum {
    BW_LINK_TRACK_HEAD = 1u << 0,          // head.valid: the headset tracks
    BW_LINK_TRACK_PITCH = 1u << 1,         // head.pitchDeg is there
    BW_LINK_TRACK_ROLL = 1u << 2,          // head.rollDeg is there
    BW_LINK_TRACK_ROOM = 1u << 3,          // head.yawRoomDeg and the hands' *Ahead are there
    BW_LINK_TRACK_FLATVR_STAMP = 1u << 4,  // stamp is FlatVR's own moment for this head, else when it came
};

typedef struct BW_LinkHead {
    uint32_t valid;          // 1 while the headset tracks
    float yawDeg;            // the turn since BodyWalk's recenter, + left
    float pitchDeg;          // up (+) / down (-) from the horizon
    float rollDeg;           // tilted toward the right shoulder (+) / left (-)
    float yawRoomDeg;        // the turn in BodyWalk's room, not recentered, + left
    float pos[3];            // where it is in BodyWalk's room (y: its height above the floor)
} BW_LinkHead;
BW_LINK_STATIC_ASSERT(sizeof(BW_LinkHead) == 32, "BW_LinkHead is a wire format");

typedef struct BW_LinkHand {
    uint32_t valid;          // 1 while the controller tracks
    float trigger;           // 0..1
    float grip;              // 0..1
    float stickX, stickY;    // -1..1, + right, + up
    uint32_t reserved;
    uint64_t buttons;        // BodyWalk's bits (OpenVR's numbers): 0 menu, 1 B / Y, 2 grip, 7 A / X, 32 stick click, 33 trigger click, 34 stick touch
    float pos[3];            // from the head, in the room's axes, as BodyWalk sends it
    float rot[4];            // as BodyWalk sends it
    float posAhead[3];       // the same turned by the head's room yaw: x to my right, z behind me (BW_LINK_TRACK_ROOM)
    float rotAhead[4];       // the rotation in that frame
} BW_LinkHand;
BW_LINK_STATIC_ASSERT(sizeof(BW_LinkHand) == 88, "BW_LinkHand is a wire format");

typedef struct BW_LinkTracking {
    BW_LinkHeader header;
    uint32_t flags;          // BW_LINK_TRACK_*
    uint32_t stamp;          // when this head was sampled (BW_LinkStampNow's clock)
    uint32_t userHeightMm;   // the player's height as set in BodyWalk, 0 not known
    uint32_t hostVersion;    // the BW_TrackingData version BodyWalk sent
    BW_LinkHead head;
    BW_LinkHand left;
    BW_LinkHand right;
} BW_LinkTracking;
BW_LINK_STATIC_ASSERT(sizeof(BW_LinkTracking) == 24 + 16 + 32 + 2 * 88, "BW_LinkTracking is a wire format");

// ---------------------------------------------------------------------------
// Actions (BodyWalk -> game): the Manifest's actions, held or not. Bit n of
// held = action n of the manifest whose sequence is manifestSequence;
// presses[n] counts its presses, so a press shorter than the game's polling
// still counts.
#define BW_LINK_ACTIONS_VERSION 1u

typedef struct BW_LinkActions {
    BW_LinkHeader header;
    uint32_t manifestSequence;   // the Manifest's header.sequence these indices are of; 0 none yet
    uint32_t count;              // actions in that manifest
    uint64_t held[4];            // bit n % 64 of held[n / 64]: action n held now
    uint32_t presses[BW_LINK_MAX_ACTIONS];
} BW_LinkActions;
BW_LINK_STATIC_ASSERT(sizeof(BW_LinkActions) == 24 + 8 + 32 + 256 * 4, "BW_LinkActions is a wire format");

// ---------------------------------------------------------------------------
// Requests (anyone -> BodyWalk): one-shot wishes. BodyWalk does each on its
// next frame, only what it can (BW_LinkHost::caps).
#define BW_LINK_REQUESTS_VERSION 1u

typedef struct BW_LinkRequest {
    uint32_t sequence;       // moved = a new wish; the value is put first
    int32_t value;
} BW_LinkRequest;

enum {
    BW_LINK_STEREO_NONE = 0,     // FlatVR shows a flat screen
    BW_LINK_STEREO_DEPTH = 1,    // 3D from the game's depth (ReShade)
    BW_LINK_STEREO_PAIR = 2,     // the stereo pair the game draws
};
enum {
    BW_LINK_PRESENTER_FLATVR = 0,   // FlatVR shows the game: its headset drives BodyWalk's Mapping
    BW_LINK_PRESENTER_GAME = 1,     // the game holds the headset (OpenXR of its own): its XrInput drives the Mapping
};

typedef struct BW_LinkRequests {
    BW_LinkHeader header;          // made by whoever came first; its sequence is not used
    BW_LinkRequest flatvrRunning;  // 1 start FlatVR, 0 stop it
    BW_LinkRequest stereoSource;   // BW_LINK_STEREO_*
    BW_LinkRequest presenter;      // BW_LINK_PRESENTER_*: the Mapping's Input Source follows
} BW_LinkRequests;
BW_LINK_STATIC_ASSERT(sizeof(BW_LinkRequests) == 24 + 3 * 8, "BW_LinkRequests is a wire format");

// ---------------------------------------------------------------------------
// Host (BodyWalk -> anyone): game_link is there, and what this BodyWalk can
// do. The sequence moves a few times a second while BodyWalk runs.
#define BW_LINK_HOST_VERSION 1u

enum {
    BW_LINK_CAP_HEAD_LOCK = 1u << 0,        // GameState.headLock is followed
    BW_LINK_CAP_SCREEN_DISTANCE = 1u << 1,  // GameState.screenDistanceM is followed
    BW_LINK_CAP_FLATVR_RUNNING = 1u << 2,   // Requests.flatvrRunning
    BW_LINK_CAP_STEREO_SOURCE = 1u << 3,    // Requests.stereoSource
    BW_LINK_CAP_XR_INPUT = 1u << 4,         // XrInput is taken, Requests.presenter followed
    BW_LINK_CAP_VR_ZONES = 1u << 5,         // VrZones is written
};

typedef struct BW_LinkHost {
    BW_LinkHeader header;
    uint32_t hostApiVersion;   // BodyWalk's plugin host API
    uint32_t caps;             // BW_LINK_CAP_*
    char linkVersion[16];      // game_link's own version
    char gameId[32];           // the game connected now, "" none
    uint32_t trackingLive;     // 1 while Tracking is being written (head tracking reaches game_link)
    uint32_t reserved;
} BW_LinkHost;
BW_LINK_STATIC_ASSERT(sizeof(BW_LinkHost) == 24 + 8 + 16 + 32 + 8, "BW_LinkHost is a wire format");

// ---------------------------------------------------------------------------
// XrInput (game -> BodyWalk): a game that holds the headset itself (an OpenXR
// app of its own) hands BodyWalk its headset and controllers, once a headset
// frame, for the moment that frame will be shown. BodyWalk runs its gestures,
// Mapping and virtual pad on them as on FlatVR's ("Game Link" in the Mapping's
// Input Source). Poses as the runtime locates them: the STAGE space (the floor
// at y = 0; LOCAL without one), not recentered.
#define BW_LINK_XRINPUT_VERSION 1u

enum {
    BW_LINK_XRB_PRIMARY = 1u << 0,     // A on the right controller, X on the left
    BW_LINK_XRB_SECONDARY = 1u << 1,   // B / Y
    BW_LINK_XRB_STICK = 1u << 2,       // the thumbstick pressed in
    BW_LINK_XRB_MENU = 1u << 3,        // the menu button
    BW_LINK_XRB_TRIGGER = 1u << 4,     // the trigger past its click
    BW_LINK_XRB_SQUEEZE = 1u << 5,     // the grip past its click
};
enum {
    BW_LINK_XR_FOCUSED = 1u << 0,      // the session has the input focus
    BW_LINK_XR_ENDED = 1u << 1,        // the session is over: the game's last word, the Mapping goes back to FlatVR
};

typedef struct BW_LinkXrPose {
    float pos[3];
    float rot[4];
    uint32_t valid;          // 1 position and orientation, 2 orientation only, 0 none
} BW_LinkXrPose;
BW_LINK_STATIC_ASSERT(sizeof(BW_LinkXrPose) == 32, "BW_LinkXrPose is a wire format");

typedef struct BW_LinkXrHand {
    BW_LinkXrPose grip;      // the controller as held
    BW_LinkXrPose aim;       // where it points
    float trigger, squeeze;  // 0..1
    float stickX, stickY;    // -1..1, + right, + up
    uint32_t buttons;        // BW_LINK_XRB_*
    uint32_t active;         // 1 while a controller is there
} BW_LinkXrHand;
BW_LINK_STATIC_ASSERT(sizeof(BW_LinkXrHand) == 88, "BW_LinkXrHand is a wire format");

typedef struct BW_LinkXrInput {
    BW_LinkHeader header;
    uint32_t stamp;          // BW_LinkStampNow() when written
    uint32_t state;          // BW_LINK_XR_*
    int64_t displayTime;     // the XrTime the poses are for
    BW_LinkXrPose head;      // between the eyes
    BW_LinkXrHand hand[2];   // 0 left, 1 right
    char profile[2][64];     // each hand's interaction profile, "/interaction_profiles/oculus/touch_controller"
} BW_LinkXrInput;
BW_LINK_STATIC_ASSERT(sizeof(BW_LinkXrInput) == 24 + 16 + 32 + 2 * 88 + 128, "BW_LinkXrInput is a wire format");

// ---------------------------------------------------------------------------
// VrZones (BodyWalk -> game): the Mapping's gesture zones ticked "VR" (a potion
// on the belt, a holster), for a game holding the headset to draw as FlatVR
// does: see-through balls. Placed from the head in XrInput, so in the same
// space. Written every BodyWalk frame while there are zones to show; let them
// go when the stamp is older than half a second.
#define BW_LINK_VRZONES_VERSION 1u
#define BW_LINK_VRZONES_MAX 32u

typedef struct BW_LinkVrZone {
    float centre[3];
    float radius;            // metres
    uint32_t rgba;           // its colour now (at rest, a hand inside, firing), R in the low byte, alpha = opacity
    float glow;              // halo and rim, 0..1
} BW_LinkVrZone;
BW_LINK_STATIC_ASSERT(sizeof(BW_LinkVrZone) == 24, "BW_LinkVrZone is a wire format");

typedef struct BW_LinkVrZones {
    BW_LinkHeader header;
    uint32_t stamp;          // BW_LinkStampNow() when written
    uint32_t count;          // used entries in zone[]
    BW_LinkVrZone zone[BW_LINK_VRZONES_MAX];
} BW_LinkVrZones;
BW_LINK_STATIC_ASSERT(sizeof(BW_LinkVrZones) == 24 + 8 + 32 * 24, "BW_LinkVrZones is a wire format");

#pragma pack(pop)

// ---------------------------------------------------------------------------
// The manifest copy BodyWalk keeps, one file per game, so the actions and tabs
// are there before the game runs: %LOCALAPPDATA%\BodyWalkVR\game_link\<gameId>.txt.
// A mod's installer may put its own there (BodyWalk not running), so they are
// there from BodyWalk's first start. UTF-8 text, a line each:
//
//   game d2r
//   name Diablo II: Resurrected
//   mod D2R VR 0.157
//   action D2R: Attack (never picks up)
//   tab D2R Bow|D2R: Bow in the hands (F4)
//
// Actions in the manifest's order. BodyWalk registers actions only when it
// starts: a game with actions its copy did not have gets them mapped after a
// BodyWalk restart.
#define BW_LINK_CACHE_DIR L"BodyWalkVR\\game_link"   // under %LOCALAPPDATA%

// ---------------------------------------------------------------------------
// C++ helpers (Windows).
#if defined(__cplusplus) && defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#include <string.h>
#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#endif

inline uint32_t BW_LinkStampNow() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint32_t)((unsigned long long)c.QuadPart / ((unsigned long long)f.QuadPart / 10000ull));
}

namespace bw {
namespace link {

// A block, made if no one has yet: the whole BW_LINK_BLOCK_BYTES mapped, read
// and write. Null when it cannot be had. Keep `map` open while you use it.
inline void* Open(const wchar_t* name, HANDLE* map) {
    *map = nullptr;
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, FALSE};
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(BW_LINK_SDDL, SDDL_REVISION_1, &sd, nullptr)) sa.lpSecurityDescriptor = sd;
    HANDLE m = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, BW_LINK_BLOCK_BYTES, name);
    if (sd) LocalFree(sd);
    if (!m) return nullptr;
    void* v = MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, BW_LINK_BLOCK_BYTES);
    if (!v) { CloseHandle(m); return nullptr; }
    *map = m;
    return v;
}

inline void Close(void* view, HANDLE map) {
    if (view) UnmapViewOfFile(view);
    if (map) CloseHandle(map);
}

// A block with one writer: WriteBegin, change it in place, WriteEnd.
inline void WriteBegin(BW_LinkHeader* h) {
    volatile uint32_t* s = &h->sequence;
    *s = *s | 1u;   // odd (a writer that died mid-write left it odd already)
    MemoryBarrier();
}
inline void WriteEnd(BW_LinkHeader* h, uint32_t version, uint32_t size) {
    h->version = version;
    h->size = size;
    h->writerPid = GetCurrentProcessId();
    h->magic = BW_LINK_MAGIC;
    MemoryBarrier();
    volatile uint32_t* s = &h->sequence;
    *s = *s + 1u;   // even again
}
// The whole struct at once: everything after the header taken from `v`.
template <class T> void Write(T* block, const T& v, uint32_t version) {
    WriteBegin(&block->header);
    memcpy((char*)block + sizeof(BW_LinkHeader), (const char*)&v + sizeof(BW_LinkHeader), sizeof(T) - sizeof(BW_LinkHeader));
    WriteEnd(&block->header, version, (uint32_t)sizeof(T));
}

// A consistent copy of a block. False: never written, or written all through
// the tries. What an older writer did not fill reads as zeros.
template <class T> bool Read(const T* block, T* out) {
    const volatile uint32_t* s = &block->header.sequence;
    for (int i = 0; i < 64; ++i) {
        const uint32_t before = *s;
        if (before & 1u) { YieldProcessor(); continue; }
        MemoryBarrier();
        memcpy(out, (const void*)block, sizeof(T));
        MemoryBarrier();
        if (*s != before) continue;
        if (out->header.magic != BW_LINK_MAGIC) return false;
        if (out->header.size < sizeof(T)) {
            const uint32_t have = out->header.size < sizeof(BW_LinkHeader) ? (uint32_t)sizeof(BW_LinkHeader) : out->header.size;
            memset((char*)out + have, 0, sizeof(T) - have);
        }
        return true;
    }
    return false;
}

// A wish into Requests: the value first, then the sequence moved.
inline void Request(BW_LinkRequests* block, BW_LinkRequest* r, int32_t value) {
    if (block->header.magic != BW_LINK_MAGIC) {   // the first one here says what the block is
        block->header.version = BW_LINK_REQUESTS_VERSION;
        block->header.size = (uint32_t)sizeof(BW_LinkRequests);
        block->header.magic = BW_LINK_MAGIC;
    }
    ((volatile BW_LinkRequest*)r)->value = value;
    MemoryBarrier();
    InterlockedIncrement((volatile LONG*)&r->sequence);
}

// The writer's process still there (0: none yet).
inline bool WriterAlive(uint32_t pid) {
    if (!pid) return false;
    if (pid == GetCurrentProcessId()) return true;
    HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!p) return false;
    const bool alive = WaitForSingleObject(p, 0) == WAIT_TIMEOUT;
    CloseHandle(p);
    return alive;
}

// The writer is alive: its process runs and its sequence moved within maxStillMs.
// Keep one Pulse per block you watch.
struct Pulse {
    uint32_t sequence = 0;
    ULONGLONG movedAt = 0;
    uint32_t pid = 0;          // the writer last asked about, and the answer (asked again every 500 ms)
    bool pidAlive = false;
    ULONGLONG pidAt = 0;
    bool Alive(const BW_LinkHeader* h, ULONGLONG maxStillMs) {
        const ULONGLONG now = GetTickCount64();
        const uint32_t s = ((const volatile BW_LinkHeader*)h)->sequence;
        if (s != sequence || !movedAt) { sequence = s; movedAt = now; }
        if (h->magic != BW_LINK_MAGIC || now - movedAt > maxStillMs) return false;
        if (h->writerPid != pid || now - pidAt > 500) { pid = h->writerPid; pidAlive = WriterAlive(pid); pidAt = now; }
        return pidAlive;
    }
};

// bit n of a 256-bit mask
inline bool Bit(const uint64_t* m, uint32_t n) { return n < 256 && ((m[n / 64] >> (n % 64)) & 1u) != 0; }

}  // namespace link
}  // namespace bw
#endif  // C++ and Windows

#endif  // BODYWALK_GAME_LINK_H
