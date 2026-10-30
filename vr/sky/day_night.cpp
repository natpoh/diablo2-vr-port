// vr/sky/day_night.cpp - cut from vr/vrcam.cpp (docs/plan_modules.md M8); what it gives: vr/sky/world_light.h. It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// The game's own day and night (2026-10-05). The game draws a debug line with
// the time of day ("End of dusk, start of night" ... and "Env Cycle = %i") from
// an environment struct it reaches as [global] + 8: +0x00 the period (0..5, its
// six names), +0x08 the time in ticks (25 a second), +0x0C the light now, 0..255,
// which the game eases from period to period, +0x20/+0x28 the sun's direction.
// The global is found through that code - the only place in the image where
//   48 8B 0D <global> E8 <getter> 48 63 C8 4C 8D 05 <"%s"> BA 32 00 00 00 48 8D 05 <the names>
// stands - so another build of the game is found again; the reads are guarded.
namespace env {
std::atomic<uintptr_t> g_slot{0};   // the global's address, 0 = not found yet

bool SafeRead(const void* p, void* out, size_t n) {
    __try { memcpy(out, p, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool PageReadable(uintptr_t a) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!VirtualQuery((void*)a, &mi, sizeof mi) || mi.State != MEM_COMMIT) return false;
    const DWORD pr = mi.Protect & 0xFF;
    return !(mi.Protect & PAGE_GUARD) && pr != PAGE_NOACCESS;
}

// Every 5 s until found (the image's pages are decrypted late); on the update thread.
void Find() {
    if (g_slot.load() || !g_base) return;
    static ULONGLONG last = 0;
    if (GetTickCount64() - last < 5000) return;
    last = GetTickCount64();
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!SafeRead((void*)g_base, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) return;
    if (!SafeRead((void*)(g_base + dos.e_lfanew), &nt, sizeof nt) || nt.Signature != IMAGE_NT_SIGNATURE) return;
    static const uint8_t kSig[] = {0x48, 0x8B, 0x0D, 0, 0, 0, 0, 0xE8, 0, 0, 0, 0, 0x48, 0x63, 0xC8, 0x4C, 0x8D, 0x05, 0, 0, 0, 0,
                                   0xBA, 0x32, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x05};
    static const bool kAny[] = {0, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    constexpr size_t kChunk = 0x10000, kLen = sizeof kSig;
    static uint8_t buf[kChunk + kLen];
    const uintptr_t end = g_base + nt.OptionalHeader.SizeOfImage;
    for (uintptr_t a = g_base + 0x1000; a < end; a += kChunk) {
        const size_t n = (size_t)std::min<uintptr_t>(kChunk + kLen, end - a);
        if (!PageReadable(a) || !SafeRead((void*)a, buf, n)) continue;
        for (size_t i = 0; i + kLen <= n; ++i) {
            if (buf[i] != 0x48 || buf[i + 1] != 0x8B) continue;
            size_t k = 0;
            while (k < kLen && (kAny[k] || buf[i + k] == kSig[k])) ++k;
            if (k < kLen) continue;
            int32_t disp;
            memcpy(&disp, buf + i + 3, 4);
            g_slot.store(a + i + 7 + disp);
            LogF("vrcam: the game's day and night found - environment through the global at +0x%llX", (unsigned long long)(a + i + 7 + disp - g_base));
            return;
        }
    }
    static bool told = false;
    if (!told) { told = true; Log("vrcam: the game's day and night not found yet (looked for the Env Cycle code) - the sky stays as set"); }
}

// The light now (0..255) and the period (0..5); false in menus, between games or not found.
bool Now(int* light, int* period) {
    const uintptr_t slot = g_slot.load();
    uintptr_t obj = 0, e = 0;
    int32_t v[4];
    if (!slot || !SafeRead((void*)slot, &obj, 8) || obj < 0x10000 || !SafeRead((void*)(obj + 8), &e, 8) || e < 0x10000 ||
        !SafeRead((void*)e, v, sizeof v))
        return false;
    if (v[0] < 0 || v[0] > 5 || v[3] < 0 || v[3] > 255) return false;
    *period = v[0];
    *light = v[3];
    return true;
}
}  // namespace env

// What the sky and the open-air fog are multiplied by now: 1 by day, [sky]
// night_brightness at night, in between as the game's own light goes - so they
// darken and brighten with the world, at its pace.
// The daylight 0 (full night) .. 1 (full day) from the game's light; -1 not known.
// (The act 2 town's light stands at 32 while its period runs round in seconds: that IS its light - the sky shows night there, and the fog
// darkens with it as everywhere. Serg, 2026-10-28: "why is the fog bright at night" - a check that took the town's reading as no day, the fog
// as set, is gone again: the fog follows the light the scene is lit with, as the sky does.)
float DayNightKRaw(int* light, int* period) {
    if (!g_set.skyDayNight.load() || !env::Now(light, period)) return -1.0f;
    const float lo = g_set.lightNight.load(), hi = std::max(lo + 1.0f, g_set.lightDay.load());
    return std::clamp((*light - lo) / (hi - lo), 0.0f, 1.0f);
}
// For the fog: unknown = 0, the act's colour as set (what it was before day and night).
float DayNightK() { int l = 0, p = 0; return std::max(0.0f, DayNightKRaw(&l, &p)); }

float DayNightMul() {
    int light = 0, period = 0;
    const float k = DayNightKRaw(&light, &period);
    if (k < 0.0f) return 1.0f;
    static int toldLight = -100, toldPeriod = -1;
    if (abs(light - toldLight) >= 10 || period != toldPeriod) {
        toldLight = light; toldPeriod = period;
        static const char* const kNames[6] = {"end of night, dawn", "end of dawn, day", "day", "end of day, dusk", "end of dusk, night", "night"};
        LogF("vrcam: the game's light %d (%s) - sky and our effect's fog x%.2f; the game's fog's colour %s", light, kNames[period],
             g_set.nightBright.load() + (1.0f - g_set.nightBright.load()) * k, g_set.fogKeepsColour.load() ? "the act's as set ([fog] fog_keeps_colour)" : "the same");
    }
    return g_set.nightBright.load() + (1.0f - g_set.nightBright.load()) * k;
}

}  // namespace vrc
