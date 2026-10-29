// props_lib.h - [weight] knock_props: the props world of the optional weight library (the D2rp* exports of d2r_vr_weight.dll 1.3 on, in d2rloader\d2r_vr_libs or beside
// the plugin). Its C interface is shared/d2r_vr_props.h. The library is not part of this repository and may be absent: then the props stand where the game placed them.
// This file only finds the DLL, checks its version and calls it; vr/props_world.cpp runs it on a thread of its own.
#pragma once

#include <cstdint>

#include "d2r_vr_props.h"

namespace propslib {

using LogFn = void (*)(const char*);
void SetLog(LogFn fn);
void SetPath(const wchar_t* path);   // tests: the DLL here instead; null: d2r_vr_libs or beside the plugin again

enum class Status { NotLooked, Missing, BadAbi, BrokenExports, Loaded };
Status GetStatus();
// Loads it when it is there and not loaded yet - at most every 2 s while it is missing or not usable. True when the props world can be used.
bool Ensure();
bool Loaded();

// The D2rp* calls (shared/d2r_vr_props.h); with no library: nothing done, a world is null.
void* Create(const D2rpParams* params);
void Destroy(void* world);
int32_t Clear(void* world, float floorY);
int32_t SetProps(void* world, const D2rpProp* props, int32_t n);
int32_t SetStatics(void* world, const D2rpBox* boxes, int32_t n);
int32_t SetMovers(void* world, const D2rpMover* movers, int32_t n);
int32_t Step(void* world, double now, float dt);
int32_t Poses(void* world, D2rpPose* out, int32_t max);
int32_t Knocks(void* world, D2rpKnock* out, int32_t max);
int32_t MoverAt(void* world, int32_t slot, D2rpMoverOut* out);
// [weight] grab_props: the hands in the props world - the optional D2rpSetHands / D2rpHandAt (a library without them: HasHands false, nothing taken).
bool HasHands();
int32_t SetHands(void* world, const D2rpHand* hands, int32_t n);
int32_t HandAt(void* world, int32_t hand, D2rpHandOut* out);

}  // namespace propslib
