// vr/diag/flog.cpp - [debug] frame_log: every pair, view build, hero pose and present as one CSV line (cut from vr/vrcam.cpp, docs/plan_modules.md M1)
#include "diag.h"
#include "../vrcam_internal.h"
#include "sigscan.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <share.h>

namespace vrc {

// Frame log ([debug] frame_log): every pair, view build, hero pose and
// present as one CSV line, stamped with QueryPerformanceCounter in
// microseconds - the clock every process shares, so FlatVR's own per-frame
// trace (flat_vr_pose_trace.csv, switched on by the flag file this writes)
// lines up with it. Lines collect in memory; the timer thread writes them out.
namespace flog {
std::atomic<bool> g_on{false};
std::atomic<ULONGLONG> g_diagUntil{0};   // [debug] diag_go: the frame log runs until then
void StartDiag();
static SRWLOCK g_lock = SRWLOCK_INIT;
static std::string g_buf;
constexpr uint64_t kDrawCounterRva = 0x33ED6D8, kFrameTimeRva = 0x27D31D0;   // as RVA_DRAW_COUNTER / RVA_FRAME_TIME below

double UsNow() {
    static const double k = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return 1e6 / (double)f.QuadPart; }();
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)c.QuadPart * k;
}
uint32_t Pass() { uint32_t v = 0; SafeRead(&v, (void*)d2rsig::Addr(kDrawCounterRva), 4); return v; }
float Dt() { float v = -1.0f; SafeRead(&v, (void*)d2rsig::Addr(kFrameTimeRva), 4); return v; }

void Line(const char* fmt, ...) {
    if (!g_on.load(std::memory_order_relaxed)) return;
    char b[768];
    int n = snprintf(b, sizeof b, "%.0f,", UsNow());
    va_list a; va_start(a, fmt);
    const int m = vsnprintf(b + n, sizeof b - n, fmt, a);
    va_end(a);
    n = std::min<int>(n + std::max(m, 0), (int)sizeof b - 1);
    AcquireSRWLockExclusive(&g_lock);
    if (g_buf.size() < (16u << 20)) { g_buf.append(b, n); g_buf += '\n'; }
    ReleaseSRWLockExclusive(&g_lock);
}

std::wstring FlagPath() {
    wchar_t p[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", p, MAX_PATH);
    if (!n || n >= MAX_PATH - 60) return L"";
    return std::wstring(p) + L"\\BodyWalkVR\\flat_vr_pose_trace.on";
}

// The timer thread: open / close with the setting, write what collected.
void Tick(bool want) {
    static FILE* f = nullptr;
    static size_t written = 0;
    if (want && !f) {
        wchar_t path[MAX_PATH];
        wcscpy_s(path, g_iniPath);
        if (wchar_t* slash = wcsrchr(path, L'\\')) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"d2r_vr_frames.csv");
        f = _wfsopen(path, L"wb", _SH_DENYWR);
        if (!f) return;
        written = 0;
        fputs("# t_us = QueryPerformanceCounter in microseconds (FlatVR's flat_vr_pose_trace.csv uses the same clock)\n"
              "# S: t_us,S,pass,dt,heldYaw,headYaw,headPitch,headRoll,sampleStamp          - a pair begins (head and turn held for both passes)\n"
              "# V: t_us,V,pass,dt,eye,targetYaw,camYaw,lookX,lookZ,stamp                  - a world view built\n"
              "# P: t_us,P,pass,dt,eye,matrix,rawX,rawZ,rawYaw,lookX,lookZ,usedYaw,bodyYaw,eyeX,eyeY,eyeZ,result,logicFacing,logicAgeMs - a hero pose\n"
              "# R: t_us,R,pass,eye,stamp                                                    - ReShade on a present\n"
              "# C: t_us,C,pass,eye,addr:yaw,...                                             - every hero matrix copy at the pose (address: low 20 bits)\n"
              "# Y: t_us,Y,pass,replayed,waitPrev,waitRestore,holdLeft,waitLastPair,camera,submit,presentR,copyL,presentL - a one-pass pair's steps, us\n", f);
        if (const std::wstring flag = FlagPath(); !flag.empty())
            if (FILE* t = _wfopen(flag.c_str(), L"wb")) fclose(t);
        g_on.store(true);
        Log("vrcam: frame log ON - d2r_vr_frames.csv beside the ini; FlatVR writes flat_vr_pose_trace.csv alongside");
    }
    if (!f) return;
    std::string out;
    AcquireSRWLockExclusive(&g_lock); out.swap(g_buf); ReleaseSRWLockExclusive(&g_lock);
    if (!out.empty()) { fwrite(out.data(), 1, out.size(), f); written += out.size(); }
    if (!want || written > (300u << 20)) {
        g_on.store(false);
        AcquireSRWLockExclusive(&g_lock); out.clear(); out.swap(g_buf); ReleaseSRWLockExclusive(&g_lock);
        if (!out.empty()) fwrite(out.data(), 1, out.size(), f);
        fclose(f); f = nullptr;
        if (const std::wstring flag = FlagPath(); !flag.empty()) DeleteFileW(flag.c_str());
        Log(want ? "vrcam: frame log stopped at 300 MB" : "vrcam: frame log OFF");
    } else fflush(f);
}
}  // namespace flog

}  // namespace vrc
