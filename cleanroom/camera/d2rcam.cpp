// Free perspective camera for D2R (see d2rcam.h; evidence in RECON.md).
//
// The world camera is a D2Render Camera object. Its view is rebuilt lazily from
// a position and a quaternion; its projection from fov/aspect/near. Several
// consumers read those fields directly instead of the matrices (mouse picking,
// shadow fitting, physics, D2Prism), so swapping only the matrices would leave
// them looking from the old isometric spot. Per frame, right after the game's
// own camera follow (CameraTranslation::RenderTick) we therefore:
//   1. recover the game's own pose (a field still holding the value we wrote
//      last frame was not touched by the game this frame),
//   2. hand the game's view/projection and look-at point to the callbacks,
//   3. write our eye position and orientation into the camera, so every
//      pos/quat reader agrees with the picture, and our exact matrices into
//      +0x10/+0x50/+0x90 (also after any later lazy rebuild, via two hooks),
//   4. leave fov/aspect/near/far alone: the 2D game layer derives its zoom
//      ratio from the fov field.
// ScreenToRay is answered from our matrices, except for the shadow fit, which
// keeps getting rays from the game's own camera so the sun's shadow map keeps
// covering the area around the hero it was tuned for.

#include "d2rcam.h"

#include "../drawdist/drawdist.h"
#include "game_layout.h"
#include "mat4.h"
#include "../sigscan/sigscan.h"

#include <D2RLPlugin/api.h>

#include <Windows.h>
#include <atomic>
#include <cstring>
#include <intrin.h>

namespace d2rcam {
namespace {

using namespace d2rcam::layout;
using m4::V3;

using GetRendererFn   = void*(__fastcall*)();
using GetCameraFn     = void*(__fastcall*)(void* renderer);
using CamRenderTickFn = void(__fastcall*)(void* layer, float dt);
using RebuildViewFn   = void(__fastcall*)(void* cam);
using GetProjectionFn = const float*(__fastcall*)(void* cam);
using ScreenToRayFn   = void(__fastcall*)(void* cam, const float* px, float* origin, float* dir);
using ReleaseCameraFn = void(__fastcall*)(void* cam);

const D2RL::PluginContext* g_ctx  = nullptr;
uintptr_t                  g_base = 0;

GetRendererFn g_getRenderer     = nullptr;
GetCameraFn   g_getGameCamera   = nullptr;
GetCameraFn   g_getActiveCamera = nullptr;

CamRenderTickFn o_camRenderTick = nullptr;
RebuildViewFn   o_rebuildView   = nullptr;
GetProjectionFn o_getProjection = nullptr;
ScreenToRayFn   o_screenToRay   = nullptr;
ReleaseCameraFn o_releaseCamera = nullptr;

std::atomic<bool>   g_cameraHooks { false };
std::atomic<bool>   g_enabled { false };
std::atomic<ViewFn> g_viewFn { nullptr };
std::atomic<ProjFn> g_projFn { nullptr };
std::atomic<FrameFn> g_frameFn { nullptr };
std::atomic<RayFn>  g_rayFn { nullptr };
std::atomic<ULONGLONG> g_lastSync { 0 };

// ---- Per-frame snapshot ----------------------------------------------------
// Written only by the frame sync (game main thread), read by the hooks and
// PickRay from any thread. A ring of slots: a reader holding a slot would have
// to stall for three frames before the writer reuses it.
struct Frame {
	void* cam;            // world camera this frame, null when not in world
	bool  ourView;
	bool  ourProj;
	float view[16];       // in use: ours or the game's
	float invView[16];
	float proj[16];
	float gamePos[3];
	float gameQuat[4];
};

Frame                 g_frames[4] {};
std::atomic<uint32_t> g_frameIndex { 0 };

const Frame& Current() {
	return g_frames[g_frameIndex.load(std::memory_order_acquire) & 3];
}

void Publish(const Frame& f) {
	const uint32_t next = g_frameIndex.load(std::memory_order_relaxed) + 1;
	g_frames[next & 3]  = f;
	g_frameIndex.store(next, std::memory_order_release);
}

// ---- Main-thread bookkeeping (frame sync and camera release only) ------------
struct Tracked {
	void* cam             = nullptr;
	bool  wrotePose       = false;   // our eye/orientation is in the fields
	float writtenPos[3]   = {};
	float writtenQuat[4]  = {};
	float gamePos[3]      = {};
	float gameQuat[4]     = {};
};
Tracked g_t;

// This frame's callback inputs, for Refresh (frame sync thread only).
WorldView g_lastIn {};
bool      g_lastInValid = false;

template <typename T>
T& At(void* base, size_t offset) {
	return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

float* Fp(void* cam, size_t offset) {
	return reinterpret_cast<float*>(static_cast<uint8_t*>(cam) + offset);
}

// The loader writes the trampoline pointer itself (raw overload below), but a
// game thread can enter a detour in the instant between the patch and that
// write. Wait for it; it is only ever a few instructions away.
template <typename Fn>
Fn Orig(Fn const volatile& p) {
	for (int i = 0; i < (1 << 20); ++i) {
		const Fn f = p;
		if (f != nullptr) {
			return f;
		}
		YieldProcessor();
	}
	return p;
}

bool Same(const float* a, const float* b, int n) {
	return std::memcmp(a, b, n * sizeof(float)) == 0;
}

void GameView(const float pos[3], const float quat[4], float out[16]) {
	const V3 fwd = m4::Rotate(quat, { 0, 0, 1 });
	const V3 up  = m4::Rotate(quat, { 0, 1, 0 });
	m4::LookTo({ pos[0], pos[1], pos[2] }, fwd, up, out);
}

// The projection the game itself would build from the camera's fields now.
void GameProjection(void* cam, float out[16]) {
	const float w = At<float>(cam, camf::AspectW);
	const float h = At<float>(cam, camf::AspectH);
	const float n = At<float>(cam, camf::Near);
	if (At<uint32_t>(cam, camf::ProjType) == 1) {
		m4::OrthoRevZ(-0.5f * w, 0.5f * w, -0.5f * h, 0.5f * h, At<float>(cam, camf::Far), n, out);
	} else {
		const float fov = At<float>(cam, camf::FovDeg) * 3.14159265358979f / 180.0f;
		m4::PerspectiveRevZ(fov, h != 0.0f ? w / h : 1.0f, n, out);
	}
}

// Puts the game's own pose back if our values are still in the fields.
void RestoreGamePose(void* cam) {
	if (!g_t.wrotePose) {
		return;
	}
	if (Same(Fp(cam, camf::Pos), g_t.writtenPos, 3)) {
		std::memcpy(Fp(cam, camf::Pos), g_t.gamePos, sizeof(g_t.gamePos));
	}
	if (Same(Fp(cam, camf::Quat), g_t.writtenQuat, 4)) {
		std::memcpy(Fp(cam, camf::Quat), g_t.gameQuat, sizeof(g_t.gameQuat));
	}
	o_rebuildView(cam);
	g_t.wrotePose = false;
}

void PublishNone() {
	Frame f {};
	Publish(f);
}

void Apply(void* cam, const WorldView& in, bool hadView, bool hadProj);

// The frame sync, run right after the game's camera follow.
void SyncFrame() {
	g_lastSync.store(GetTickCount64(), std::memory_order_relaxed);
	void* renderer = g_getRenderer();
	void* cam      = g_getGameCamera(renderer);
	void* active   = cam != nullptr ? g_getActiveCamera(renderer) : nullptr;
	// What the lazy rebuilds were told last frame; when we stop overriding, the
	// matrices are marked dirty only after the new frame is published, so a
	// rebuild in between cannot put our old matrices back.
	const Frame& prev   = Current();
	const bool  hadView = cam != nullptr && prev.cam == cam && prev.ourView;
	const bool  hadProj = cam != nullptr && prev.cam == cam && prev.ourProj;

	if (cam != g_t.cam) {
		g_t = Tracked {};   // a new camera object: nothing of ours is in it
		g_t.cam = cam;
	}
	if (cam == nullptr) {
		g_lastInValid = false;
		PublishNone();
		return;
	}
	if (active != cam) {
		g_lastInValid = false;   // the debug camera is in use: hand the game camera back
		RestoreGamePose(cam);
		PublishNone();
		if (hadView) At<uint8_t>(cam, camf::ViewDirty) = 1;
		if (hadProj) At<uint8_t>(cam, camf::ProjDirty) = 1;
		return;
	}

	// 1. The game's own pose this frame.
	float* pos  = Fp(cam, camf::Pos);
	float* quat = Fp(cam, camf::Quat);
	if (!g_t.wrotePose || !Same(pos, g_t.writtenPos, 3)) {
		std::memcpy(g_t.gamePos, pos, sizeof(g_t.gamePos));
	}
	if (!g_t.wrotePose || !Same(quat, g_t.writtenQuat, 4)) {
		std::memcpy(g_t.gameQuat, quat, sizeof(g_t.gameQuat));
	}

	// 2. What the callbacks see.
	WorldView in {};
	GameView(g_t.gamePos, g_t.gameQuat, in.gameView);
	GameProjection(cam, in.gameProj);
	std::memcpy(in.lookAt, Fp(cam, camf::LookAt), sizeof(in.lookAt));
	in.lookAtValid = At<uint8_t>(cam, camf::HasLookAt) != 0;
	in.viewportW   = At<float>(cam, camf::ViewportW);
	in.viewportH   = At<float>(cam, camf::ViewportH);
	g_lastIn       = in;
	g_lastInValid  = true;

	if (const FrameFn frameFn = g_frameFn.load(std::memory_order_relaxed)) {
		frameFn();
	}
	Apply(cam, in, hadView, hadProj);
}

// 3. Runs the callbacks on `in` and puts their matrices into the camera.
void Apply(void* cam, const WorldView& in, bool hadView, bool hadProj) {
	float* pos  = Fp(cam, camf::Pos);
	float* quat = Fp(cam, camf::Quat);
	Frame f {};
	f.cam = cam;
	std::memcpy(f.gamePos, g_t.gamePos, sizeof(f.gamePos));
	std::memcpy(f.gameQuat, g_t.gameQuat, sizeof(f.gameQuat));

	const bool enabled = g_enabled.load(std::memory_order_relaxed);
	const ViewFn viewFn = g_viewFn.load(std::memory_order_relaxed);
	const ProjFn projFn = g_projFn.load(std::memory_order_relaxed);

	float view[16], inv[16], proj[16];
	f.ourView = enabled && viewFn != nullptr && viewFn(in, view) && m4::Finite(view, 16) && m4::Inverse(view, inv);
	f.ourProj = enabled && projFn != nullptr && projFn(in, proj) && m4::Finite(proj, 16);

	// 3a. View: our eye and orientation into the fields, exact matrices after.
	if (f.ourView) {
		const V3 eye { inv[12], inv[13], inv[14] };
		const V3 fwd = m4::Normalize({ -inv[8], -inv[9], -inv[10] });
		const V3 lx  = m4::Normalize(m4::Cross({ inv[4], inv[5], inv[6] }, fwd));
		const V3 up  = m4::Cross(fwd, lx);
		float q[4];
		m4::QuatFromAxes(lx, up, fwd, q);
		const float p[3] { eye.x, eye.y, eye.z };
		std::memcpy(pos, p, sizeof(p));
		std::memcpy(quat, q, sizeof(q));
		std::memcpy(g_t.writtenPos, p, sizeof(p));
		std::memcpy(g_t.writtenQuat, q, sizeof(q));
		g_t.wrotePose = true;
		o_rebuildView(cam);   // keeps the game's own bookkeeping (dirty flag) straight
		std::memcpy(Fp(cam, camf::View), view, sizeof(view));
		std::memcpy(Fp(cam, camf::InvView), inv, sizeof(inv));
		std::memcpy(f.view, view, sizeof(view));
		std::memcpy(f.invView, inv, sizeof(inv));
	} else {
		RestoreGamePose(cam);
		std::memcpy(f.view, in.gameView, sizeof(f.view));
		if (!m4::Inverse(f.view, f.invView)) {
			m4::Identity(f.invView);
		}
	}

	// 3b. Projection: let a pending game rebuild happen first so it cannot
	// overwrite ours later this frame, then put ours in.
	if (f.ourProj) {
		if (At<uint8_t>(cam, camf::ProjDirty) != 0) {
			o_getProjection(cam);
		}
		std::memcpy(Fp(cam, camf::Proj), proj, sizeof(proj));
		std::memcpy(f.proj, proj, sizeof(proj));
	} else {
		std::memcpy(f.proj, in.gameProj, sizeof(f.proj));
	}

	Publish(f);
	if (hadView && !f.ourView) At<uint8_t>(cam, camf::ViewDirty) = 1;   // the game rebuilds its own
	if (hadProj && !f.ourProj) At<uint8_t>(cam, camf::ProjDirty) = 1;
}

// ---- Hooks -------------------------------------------------------------------

// The follow derives next frame's camera offset from the camera's own
// quaternion (0x7BC3C0 reads +0x12C), so it must run on the game's pose or it
// would chase ours. Hand the game its pose for the duration of its tick.
void HandBackPoseForTick() {
	void* cam = g_t.cam;
	if (cam == nullptr || !g_t.wrotePose) {
		return;
	}
	if (Same(Fp(cam, camf::Pos), g_t.writtenPos, 3)) {
		std::memcpy(Fp(cam, camf::Pos), g_t.gamePos, sizeof(g_t.gamePos));
	}
	if (Same(Fp(cam, camf::Quat), g_t.writtenQuat, 4)) {
		std::memcpy(Fp(cam, camf::Quat), g_t.gameQuat, sizeof(g_t.gameQuat));
	}
	At<uint8_t>(cam, camf::ViewDirty) = 1;   // a GetView in between rebuilds, and the hook keeps ours
	g_t.wrotePose = false;
}

void __fastcall HookCamRenderTick(void* layer, float dt) {
	const bool ready = g_cameraHooks.load(std::memory_order_acquire);
	if (ready) {
		HandBackPoseForTick();
	}
	if (const CamRenderTickFn original = Orig(o_camRenderTick)) {
		original(layer, dt);
	}
	if (ready) {
		SyncFrame();
	}
}

void __fastcall HookRebuildView(void* cam) {
	if (const RebuildViewFn original = Orig(o_rebuildView)) {
		original(cam);
	}
	const Frame& f = Current();
	if (cam != nullptr && cam == f.cam && f.ourView) {
		std::memcpy(Fp(cam, camf::View), f.view, sizeof(f.view));
		std::memcpy(Fp(cam, camf::InvView), f.invView, sizeof(f.invView));
	}
}

const float* __fastcall HookGetProjection(void* cam) {
	const GetProjectionFn original = Orig(o_getProjection);
	const float* result = original != nullptr ? original(cam) : Fp(cam, camf::Proj);
	const Frame& f = Current();
	if (cam != nullptr && cam == f.cam && f.ourProj) {
		std::memcpy(Fp(cam, camf::Proj), f.proj, sizeof(f.proj));
	}
	return result;
}

void __fastcall HookScreenToRay(void* cam, const float* px, float* origin, float* dir) {
	const ScreenToRayFn original = Orig(o_screenToRay);
	if (original == nullptr) {   // never expected; leave a harmless ray
		std::memset(origin, 0, 3 * sizeof(float));
		std::memset(dir, 0, 3 * sizeof(float));
		dir[1] = -1.0f;
		return;
	}
	const Frame& f = Current();
	if (cam == nullptr || cam != f.cam || !(f.ourView || f.ourProj) || px == nullptr) {
		original(cam, px, origin, dir);
		return;
	}
	const uintptr_t ret = reinterpret_cast<uintptr_t>(_ReturnAddress()) - g_base;
	if (ret >= d2rsig::Rva(ShadowFitBegin) && ret < d2rsig::Rva(ShadowFitEnd)) {
		// The shadow fit keeps the game's own camera: same fields, game pose.
		alignas(16) uint8_t copy[camf::Size];
		std::memcpy(copy, cam, sizeof(copy));
		std::memcpy(copy + camf::Pos, f.gamePos, sizeof(f.gamePos));
		std::memcpy(copy + camf::Quat, f.gameQuat, sizeof(f.gameQuat));
		original(copy, px, origin, dir);
		return;
	}
	if (const RayFn rayFn = g_rayFn.load(std::memory_order_relaxed); rayFn != nullptr && rayFn(origin, dir)) {
		return;
	}
	const float vpW = At<float>(cam, camf::ViewportW);
	const float vpH = At<float>(cam, camf::ViewportH);
	if (!m4::RayThrough(f.invView, f.proj, vpW, vpH, px[0], px[1], origin, dir)) {
		original(cam, px, origin, dir);
	}
}

void __fastcall HookReleaseCamera(void* cam) {
	// The camera goes back to the pool; the next one from it may be a menu camera.
	if (cam != nullptr && cam == Current().cam) {
		PublishNone();
	}
	if (cam != nullptr && cam == g_t.cam) {
		g_t = Tracked {};
	}
	if (const ReleaseCameraFn original = Orig(o_releaseCamera)) {
		original(cam);
	}
}

// ---- Installation ----------------------------------------------------------

bool Check(const Site& s) {
	return d2rsig::Check(s.rva, s.bytes, s.size);
}

template <typename Fn>
bool Hook(const Site& s, Fn detour, Fn* original) {
	if (*original != nullptr) {
		return true;
	}
	if (!Check(s) || !d2rsig::Hook(s.rva, s.bytes, s.size, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original))) {
		*original = nullptr;
		return false;
	}
	return *original != nullptr;
}

bool InstallCamera() {
	if (g_cameraHooks.load()) {
		return true;
	}
	if (g_getRenderer == nullptr && Check(GetRenderer)) {
		g_getRenderer = reinterpret_cast<GetRendererFn>(d2rsig::Addr(GetRenderer.rva));
	}
	if (g_getGameCamera == nullptr && Check(GetGameCamera)) {
		g_getGameCamera = reinterpret_cast<GetCameraFn>(d2rsig::Addr(GetGameCamera.rva));
	}
	if (g_getActiveCamera == nullptr && Check(GetActiveCamera)) {
		g_getActiveCamera = reinterpret_cast<GetCameraFn>(d2rsig::Addr(GetActiveCamera.rva));
	}
	if (g_getRenderer == nullptr || g_getGameCamera == nullptr || g_getActiveCamera == nullptr) {
		return false;
	}
	// Release first, so a tracked camera can never go back to the pool unseen;
	// the frame sync last, because it is what starts the override.
	if (!Hook(ReleaseCamera, &HookReleaseCamera, &o_releaseCamera)) return false;
	if (!Hook(RebuildView, &HookRebuildView, &o_rebuildView)) return false;
	if (!Hook(GetProjection, &HookGetProjection, &o_getProjection)) return false;
	if (!Hook(ScreenToRay, &HookScreenToRay, &o_screenToRay)) return false;
	if (!Hook(CamRenderTick, &HookCamRenderTick, &o_camRenderTick)) return false;
	g_cameraHooks.store(true, std::memory_order_release);
	return true;
}

}   // namespace

bool Install(const D2RL::PluginContext* ctx) {
	if (ctx == nullptr || ctx->exeBase == 0) {
		return false;
	}
	g_ctx  = ctx;
	g_base = ctx->exeBase;
	d2rsig::Resolve(ctx);
	const bool cameraOk = InstallCamera();
	const bool roomsOk  = drawdist::Install(ctx);
	return cameraOk && roomsOk;
}

void SetEnabled(bool on) { g_enabled.store(on); }
bool IsEnabled() { return g_enabled.load(); }

void SetCallbacks(ViewFn view, ProjFn proj) {
	g_viewFn.store(view);
	g_projFn.store(proj);
}

void SetFrameCallback(FrameFn frame) { g_frameFn.store(frame); }
void SetRayOverride(RayFn ray) { g_rayFn.store(ray); }

bool Refresh() {
	void* cam = g_t.cam;
	const Frame& prev = Current();
	if (!g_cameraHooks.load() || cam == nullptr || !g_lastInValid || prev.cam != cam || !(prev.ourView || prev.ourProj)) {
		return false;
	}
	Apply(cam, g_lastIn, prev.ourView, prev.ourProj);
	return true;
}

bool LookAtNow(float out[3]) {
	void* cam = Current().cam;
	if (cam == nullptr || At<uint8_t>(cam, camf::HasLookAt) == 0) {
		return false;
	}
	std::memcpy(out, Fp(cam, camf::LookAt), 3 * sizeof(float));
	return true;
}

bool InWorld() {
	if (!g_cameraHooks.load() || Current().cam == nullptr) {
		return false;
	}
	// The follow stops running when a game ends; a stale frame is not "in world".
	return GetTickCount64() - g_lastSync.load(std::memory_order_relaxed) < 1000;
}

bool PickRay(float sx, float sy, float origin[3], float dir[3]) {
	if (!InWorld()) {
		return false;
	}
	const Frame& f = Current();
	if (f.cam == nullptr) {
		return false;
	}
	return m4::RayThrough(f.invView, f.proj, At<float>(f.cam, camf::ViewportW), At<float>(f.cam, camf::ViewportH), sx, sy, origin, dir);
}

void SetRenderRadius(int rooms) { drawdist::SetRadius(rooms); }
int  GetRenderRadius() { return drawdist::Radius(); }
bool  SetModelRadius(float units) { return drawdist::SetModelRadius(units); }
float GetModelRadius() { return drawdist::ModelRadius(); }

Status GetStatus() {
	const Frame& f = Current();
	Status s {};
	s.cameraHooks = g_cameraHooks.load();
	s.roomHooks   = drawdist::Installed();
	s.overriding  = f.cam != nullptr && (f.ourView || f.ourProj);
	s.roomsAdded  = drawdist::RoomsAddedLastTick();
	s.roomsLoaded = drawdist::RoomsLoadedTotal();
	return s;
}

}   // namespace d2rcam
