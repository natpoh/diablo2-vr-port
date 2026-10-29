// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed commit 9e9c2410e8df57f72547c691de9971c88b0b014a ("Big objects: trees, houses and walls drawn past the object
// draw distance"), its cleanroom/drawdist/drawdist.cpp hook (HookInRange, HookInRangeIn, SetBigRadius), moved into a file of its own:
// drawdist.cpp (MIT) only calls SetBigRadius / BigRadius / TakeBigKept and lends ResortModels. This file stays MPL 2.0.
//
// Big objects drawn farther ([render] big_radius, shared/unleashed_far_objects.h): the AssetStandbySystem's per-model test hooked the
// first time the setting is on; with it off again the hook hands every call to the game's own test unchanged. Visual only: which of
// the game's own models stay drawn - nothing of the game's state, units or rules is touched.

#include "drawdist.h"

#include "../camera/game_layout.h"
#include "../sigscan/sigscan.h"
#include "../../shared/unleashed_far_objects.h"

#include <Windows.h>
#include <atomic>

namespace d2rcam::drawdist {
namespace {

using InRangeFn = bool(__fastcall*)(const float* box, const float* hero, float radiusSq, int flags);

// Big objects drawn farther: the game's test, hooked once the setting is first on.
InRangeFn             o_inRange = nullptr;
std::atomic<float>    g_bigRadius { 0.0f };
std::atomic<uint32_t> g_bigKept { 0 };
uint64_t              g_nextInRangeTry = 0;

// A detour can run before the loader has stored the trampoline (as drawdist.cpp's Orig).
InRangeFn Original() {
	for (int i = 0; i < (1 << 20); ++i) {
		const InRangeFn f = o_inRange;
		if (f != nullptr) {
			return f;
		}
		YieldProcessor();
	}
	return o_inRange;
}

// The AssetStandbySystem's test for each model: kept drawn while true. A big model's reach grows
// with its size (shared/unleashed_far_objects.h); with the setting off it is the game's own test.
bool __fastcall HookInRange(const float* box, const float* hero, float radiusSq, int flags) {
	const float big = g_bigRadius.load(std::memory_order_relaxed);
	if (big <= 0.0f) {
		return Original()(box, hero, radiusSq, flags);
	}
	const bool in = unleashed::farobjects::InRange(box, hero, radiusSq, big);
	if (in && !unleashed::farobjects::InRange(box, hero, radiusSq, 0.0f)) {
		g_bigKept.fetch_add(1, std::memory_order_relaxed);
	}
	return in;
}

// Its page is decrypted once the game has run it (the hero walked); tried at most twice a second.
bool HookInRangeIn() {
	using d2rcam::layout::ModelInRange;
	if (o_inRange != nullptr) {
		return true;
	}
	const uint64_t now = GetTickCount64();
	if (!HooksReady() || now < g_nextInRangeTry) {
		return false;
	}
	g_nextInRangeTry = now + 500;
	if (!d2rsig::Check(ModelInRange.rva, ModelInRange.bytes, ModelInRange.size) ||
	    !d2rsig::Hook(ModelInRange.rva, ModelInRange.bytes, ModelInRange.size, reinterpret_cast<void*>(&HookInRange), reinterpret_cast<void**>(&o_inRange))) {
		o_inRange = nullptr;
		return false;
	}
	return true;
}

}   // namespace

bool SetBigRadius(float units) {
	const float big = unleashed::farobjects::Normalize(units);
	g_bigRadius.store(big, std::memory_order_relaxed);
	if (big <= 0.0f && o_inRange == nullptr) {
		return true;   // off: the game's test untouched
	}
	if (!HookInRangeIn()) {
		return false;
	}
	ResortModels();   // re-sort now, as for the model radius
	return true;
}

float BigRadius() { return g_bigRadius.load(std::memory_order_relaxed); }
uint32_t TakeBigKept() { return g_bigKept.exchange(0, std::memory_order_relaxed); }

}   // namespace d2rcam::drawdist
