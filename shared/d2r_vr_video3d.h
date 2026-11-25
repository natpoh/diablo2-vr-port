/* d2r_vr_video3d.h - the C interface between the D2R VR mod (d2rl-vrcam.dll) and an optional 3D video library (d2r_vr_video3d.dll).
 *
 * The mod owns this header (shared/d2r_vr_video3d.h in the d2r_vr repository); a library that implements it keeps a byte-identical copy.
 * Nothing in it comes from any third-party code: it only says what the mod has (the game's frame with a film in it, a render target for two eyes) and what it wants back.
 *
 * What the library is: the game's cinematics in 3D. The mod hands it, each flat frame while a cinematic plays, the game's back buffer and where the film is in it; the library
 * records, on the mod's own command list, a drawing of the film for the left eye into the left half of the given render target and for the right eye into the right half.
 * How the two eyes differ is the library's business (it guesses the film's depth). Until it has a depth for the film it draws the film flat into both halves, so the target is
 * always whole. The mod keeps everything else: when a cinematic plays, where the film is, the swapchain, the screen in the room, the settings.
 *
 * Threads: D2rv3Start / D2rv3Record / D2rv3Submitted / D2rv3Reset / D2rv3Stop are called from one thread at a time (the game's present). The library may run its own worker
 * thread; it must not throw, must not call back into the mod except through the log function, and must never wait on the GPU in D2rv3Record.
 */
#ifndef D2R_VR_VIDEO3D_H
#define D2R_VR_VIDEO3D_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define D2RV3_ABI_VERSION 1

/* The library defines D2RV3_BUILD_DLL before including this to export the functions; the mod never does. */
#if defined(D2RV3_BUILD_DLL) && defined(_WIN32)
#define D2RV3_API __declspec(dllexport)
#else
#define D2RV3_API
#endif

typedef void (*D2rv3LogFn)(const char* line);

/* What the source's view returns when it is sampled. */
#define D2RV3_SRC_LINEAR 0   /* linear light: an _SRGB view of an 8-bit buffer, or a float buffer */
#define D2RV3_SRC_GAMMA 1    /* gamma-encoded values: a plain UNORM view (a 10-bit buffer has no _SRGB twin) */

typedef struct D2rv3Frame {
    uint32_t size;            /* sizeof(D2rv3Frame) as the caller has it */
    void* src;                /* ID3D12Resource*: the game's back buffer this present */
    uint32_t srcState;        /* D3D12_RESOURCE_STATES it is in; the library leaves it there again */
    uint32_t srcViewFormat;   /* DXGI_FORMAT to view it with (not typeless) */
    uint32_t srcEncoding;     /* D2RV3_SRC_* of that view */
    uint32_t filmX, filmY, filmW, filmH;   /* the film's pixels in the source */
    uint64_t rtv;             /* D3D12_CPU_DESCRIPTOR_HANDLE.ptr of the output's render-target view (it is in RENDER_TARGET state and stays there) */
    uint32_t rtvFormat;       /* DXGI_FORMAT of that view; an _SRGB format takes linear light */
    uint32_t outW, outH;      /* the output: left eye in [0, outW/2), right eye in [outW/2, outW), outH rows */
    float strengthPct;        /* the largest separation between the eyes, % of the film's width */
    float convergence;        /* 0..1: the depth that lands on the screen (0 far, 1 near) */
} D2rv3Frame;

/* The interface version the library speaks; the mod uses it only when it equals D2RV3_ABI_VERSION. */
D2RV3_API int32_t D2rv3Abi(void);
/* A name and version for the log. */
D2RV3_API const char* D2rv3Name(void);
/* Ready the library for this device and queue (ID3D12Device*, ID3D12CommandQueue*): its pipeline, its depth network. 1 ready, 0 not (it said why through log).
 * Called again with another device after a Stop. */
D2RV3_API int32_t D2rv3Start(void* device, void* queue, D2rv3LogFn log);
/* Record this frame's drawing on the list (ID3D12GraphicsCommandList*, open). 1: the eyes differ (a depth was ready); 0: drawn flat; -1: nothing recorded. */
D2RV3_API int32_t D2rv3Record(void* list, const D2rv3Frame* frame);
/* The list D2rv3Record recorded on has been handed to the queue (ExecuteCommandLists): the library signals its own fence behind it. */
D2RV3_API void D2rv3Submitted(void);
/* A new film: forget what the last one looked like. */
D2RV3_API void D2rv3Reset(void);
/* Let go of the device: wait for the library's own work on it, free everything of it. */
D2RV3_API void D2rv3Stop(void);

typedef int32_t (*D2rv3AbiFn)(void);
typedef const char* (*D2rv3NameFn)(void);
typedef int32_t (*D2rv3StartFn)(void*, void*, D2rv3LogFn);
typedef int32_t (*D2rv3RecordFn)(void*, const D2rv3Frame*);
typedef void (*D2rv3VoidFn)(void);

#ifdef __cplusplus
}
#endif

#endif
