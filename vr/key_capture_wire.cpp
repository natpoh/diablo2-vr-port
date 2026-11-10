// key_capture_wire.cpp - see key_capture_wire.h.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "key_capture_wire.h"

namespace keycapwire {
namespace {

SRWLOCK g_lock = SRWLOCK_INIT;
keycap::Capture g_cap;              // (g_lock)
int g_resOwner = -1;                // the last word and whose (g_lock)
keycap::Res g_res;
bool g_resNew = false;
ULONGLONG g_charsUntil = 0;         // the letter of the key that answered (WM_CHAR after its WM_KEYDOWN) is eaten too

void Tell(int owner, const keycap::Res& r) {
    g_resOwner = owner;
    g_res = r;
    g_resNew = true;
}

}  // namespace

void Arm(int owner, keycap::Use use) {
    AcquireSRWLockExclusive(&g_lock);
    g_cap.Arm(owner, use, GetTickCount64());
    if (g_resOwner == owner) g_resNew = false;
    ReleaseSRWLockExclusive(&g_lock);
}
void Cancel(int owner) {
    AcquireSRWLockExclusive(&g_lock);
    if (g_cap.Owner() == owner) g_cap.Cancel();
    ReleaseSRWLockExclusive(&g_lock);
}
bool Armed(int owner) {
    AcquireSRWLockExclusive(&g_lock);
    const ULONGLONG now = GetTickCount64();
    if (g_cap.Expire(now)) Tell(g_cap.Owner(), {keycap::R::TimedOut, 0});
    const bool armed = g_cap.Owner() == owner && g_cap.Armed(now);
    ReleaseSRWLockExclusive(&g_lock);
    return armed;
}
bool Take(int owner, keycap::Res* out) {
    AcquireSRWLockExclusive(&g_lock);
    if (g_cap.Expire(GetTickCount64())) Tell(g_cap.Owner(), {keycap::R::TimedOut, 0});
    const bool have = g_resNew && g_resOwner == owner;
    if (have) {
        *out = g_res;
        g_resNew = false;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return have;
}

bool OnWindowMessage(unsigned msg, uintptr_t w, intptr_t l) {
    const ULONGLONG now = GetTickCount64();
    if (msg == WM_CHAR || msg == WM_SYSCHAR || msg == WM_DEADCHAR || msg == WM_SYSDEADCHAR) {
        AcquireSRWLockShared(&g_lock);
        const bool eat = g_cap.Armed(now) || now < g_charsUntil;
        ReleaseSRWLockShared(&g_lock);
        return eat;
    }
    unsigned vk = 0;
    bool down = false, repeat = false, mouse = false;
    switch (msg) {
        case WM_KEYDOWN: case WM_SYSKEYDOWN: vk = (unsigned)w; down = true; repeat = (l & (1 << 30)) != 0; break;
        case WM_KEYUP: case WM_SYSKEYUP: vk = (unsigned)w; break;
        case WM_MBUTTONDOWN: vk = VK_MBUTTON; down = true; mouse = true; break;
        case WM_MBUTTONUP: vk = VK_MBUTTON; mouse = true; break;
        case WM_XBUTTONDOWN: vk = HIWORD(w) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2; down = true; mouse = true; break;
        case WM_XBUTTONUP: vk = HIWORD(w) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2; mouse = true; break;
        default: return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    if (mouse && !g_cap.Armed(now)) { ReleaseSRWLockExclusive(&g_lock); return false; }   // (a mouse button only while armed)
    const int owner = g_cap.Owner();
    const keycap::Res r = g_cap.Feed(vk, down, repeat, now);
    bool ours = r.r != keycap::R::Pass && r.r != keycap::R::TimedOut;
    if (r.r == keycap::R::Took || r.r == keycap::R::Cancelled || r.r == keycap::R::Refused || r.r == keycap::R::TimedOut) Tell(owner, r);
    if (r.r == keycap::R::Took || r.r == keycap::R::Cancelled) g_charsUntil = now + 200;
    ReleaseSRWLockExclusive(&g_lock);
    return ours;
}

}  // namespace keycapwire
