// vcache.cpp - see vcache.h.
#include "vcache.h"

#include <windows.h>
#include <cstdio>

namespace vcache {

std::vector<L3> Detect() {
    std::vector<L3> out;
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationCache, nullptr, &len);
    if (!len) return out;
    std::vector<uint8_t> buf(len);
    if (!GetLogicalProcessorInformationEx(RelationCache, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buf.data(), &len)) return out;
    for (DWORD off = 0; off < len;) {
        const auto* p = (const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)(buf.data() + off);
        if (p->Relationship == RelationCache && p->Cache.Level == 3 && (p->Cache.Type == CacheUnified || p->Cache.Type == CacheData))
            out.push_back({p->Cache.CacheSize, p->Cache.GroupMask.Group, (uint64_t)p->Cache.GroupMask.Mask});
        off += p->Size;
    }
    return out;
}

namespace {
bool g_applied = false;
bool g_told = false;
char g_line[320];

// The CPU set ids of the processors in (group, mask).
std::vector<ULONG> SetsOf(uint16_t group, uint64_t mask) {
    std::vector<ULONG> ids;
    ULONG n = 0;
    GetSystemCpuSetInformation(nullptr, 0, &n, GetCurrentProcess(), 0);
    if (!n) return ids;
    std::vector<uint8_t> buf(n);
    if (!GetSystemCpuSetInformation((PSYSTEM_CPU_SET_INFORMATION)buf.data(), n, &n, GetCurrentProcess(), 0)) return ids;
    for (ULONG off = 0; off < n;) {
        const auto* c = (const SYSTEM_CPU_SET_INFORMATION*)(buf.data() + off);
        if (c->Type == CpuSetInformation && c->CpuSet.Group == group && c->CpuSet.LogicalProcessorIndex < 64 &&
            (mask >> c->CpuSet.LogicalProcessorIndex & 1))
            ids.push_back(c->CpuSet.Id);
        off += c->Size;
    }
    return ids;
}
}  // namespace

void Apply(bool want, void (*log)(const char*)) {
    if (want == g_applied && g_told) return;
    g_told = true;
    if (!want) {
        if (g_applied) {
            SetProcessDefaultCpuSets(GetCurrentProcess(), nullptr, 0);
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
            if (log) log("vrcam: big-L3 cores - off: the game's threads on any core again, the draw thread's priority normal ([render] vcache=0)");
        }
        g_applied = false;
        return;
    }
    g_applied = true;   // decided once per change, whatever comes of it
    const std::vector<L3> caches = Detect();
    const int big = Pick(caches);
    if (big < 0) {
        if (log) {
            snprintf(g_line, sizeof g_line, "vrcam: big-L3 cores - %zu L3 cache(s) of one size: nothing to prefer ([render] vcache has no effect here)", caches.size());
            log(g_line);
        }
        return;
    }
    const std::vector<ULONG> ids = SetsOf(caches[big].group, caches[big].mask);
    const bool setsOk = !ids.empty() && SetProcessDefaultCpuSets(GetCurrentProcess(), ids.data(), (ULONG)ids.size());
    const bool prioOk = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL) != 0;
    if (log) {
        uint64_t others = 0, otherBytes = 0;
        for (int i = 0; i < (int)caches.size(); ++i)
            if (i != big) { if (caches[i].group == caches[big].group) others |= caches[i].mask; otherBytes = caches[i].bytes; }
        snprintf(g_line, sizeof g_line,
                 "vrcam: big-L3 cores - L3 %llu MB on processors %s (group %u) against %llu MB on %s: the game's threads prefer the big one "
                 "(process default CPU sets, %zu processors: %s), the draw thread above normal (%s) - [render] vcache",
                 (unsigned long long)(caches[big].bytes >> 20), Ranges(caches[big].mask).c_str(), caches[big].group,
                 (unsigned long long)(otherBytes >> 20), others ? Ranges(others).c_str() : "?",
                 ids.size(), setsOk ? "set" : "FAILED", prioOk ? "set" : "FAILED");
        log(g_line);
    }
}

}  // namespace vcache
