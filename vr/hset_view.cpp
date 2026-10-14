// hset_view.cpp - see hset_view.h.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>

#include "d2r_vr_shared.h"
#include "direct_input.h"
#include "hset_view.h"

namespace hsetview {
namespace {

constexpr uint32_t kAction[4] = {D2RVR_ACT_VIEW_1, D2RVR_ACT_VIEW_2, D2RVR_ACT_VIEW_5, D2RVR_ACT_VIEW_4};
constexpr directinput::Source kSource = directinput::kSrcOther7;
constexpr ULONGLONG kHoldMs = 120;

std::atomic<uint32_t> g_held{0};
std::atomic<ULONGLONG> g_until{0};

}  // namespace

bool Press(int view) {
    if (view < 1 || view > 4) return false;
    LetGo();
    g_until.store(GetTickCount64() + kHoldMs);
    g_held.store(kAction[view - 1]);
    directinput::Action(kAction[view - 1], true, kSource);
    return true;
}

void LetGo() {
    const uint32_t bits = g_held.exchange(0);
    if (bits) directinput::Action(bits, false, kSource);
}

void Tick() {
    if (g_held.load() && GetTickCount64() >= g_until.load()) LetGo();
}

}  // namespace hsetview
