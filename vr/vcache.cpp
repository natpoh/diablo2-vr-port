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
bool g_told = false;
bool g_wantDone = false;
int g_howDone = 0;
Plan g_done;   // what the last Apply did (undone before the next)
char g_line[400];

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

const char* HowName(int how) {
    switch (HowOf(how)) {
        case kProcessSets: return "the process's default CPU sets alone (vcache_how 1, a test)";
        case kPriority: return "the draw thread above normal alone (vcache_how 2, a test)";
        case kAsBefore: return "the process's default CPU sets and the draw thread above normal, as 0.217 (vcache_how 3, a test)";
        default: return "the draw thread's own CPU sets only";
    }
}
}  // namespace

void Apply(bool want, int how, void (*log)(const char*)) {
    how = HowOf(how);
    if (g_told && want == g_wantDone && (!want || how == g_howDone)) return;
    g_told = true;
    // what the last one did, undone first (a how changed live is a clean switch, never two at once)
    if (g_done.processDefault) SetProcessDefaultCpuSets(GetCurrentProcess(), nullptr, 0);
    if (g_done.drawThreadSets) SetThreadSelectedCpuSets(GetCurrentThread(), nullptr, 0);
    if (g_done.raisePriority) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    const bool hadAny = g_done.processDefault || g_done.drawThreadSets || g_done.raisePriority;
    g_done = Plan{};
    g_wantDone = want;
    g_howDone = how;
    if (!want) {
        if (hadAny && log) log("vrcam: big-L3 cores - off: every thread on any core again, no priority changed ([render] vcache=0)");
        return;
    }
    const std::vector<L3> caches = Detect();
    const int big = Pick(caches);
    if (big < 0) {
        if (log) {
            snprintf(g_line, sizeof g_line, "vrcam: big-L3 cores - %zu L3 cache(s) of one size: nothing to prefer ([render] vcache has no effect here)", caches.size());
            log(g_line);
        }
        return;
    }
    // By default only the draw thread (vcache.h PlanFor): the game's job workers keep every processor, no priority is raised.
    const Plan plan = PlanFor(true, big, how);
    const std::vector<ULONG> ids = SetsOf(caches[big].group, caches[big].mask);
    bool setsOk = true, prioOk = true;
    if (plan.drawThreadSets) {
        setsOk = !ids.empty() && SetThreadSelectedCpuSets(GetCurrentThread(), ids.data(), (ULONG)ids.size());
        g_done.drawThreadSets = setsOk;
    }
    if (plan.processDefault) {
        const bool ok = !ids.empty() && SetProcessDefaultCpuSets(GetCurrentProcess(), ids.data(), (ULONG)ids.size());
        setsOk = setsOk && ok;
        g_done.processDefault = ok;
    }
    if (plan.raisePriority) {
        prioOk = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL) != 0;
        g_done.raisePriority = prioOk;
    }
    if (log) {
        uint64_t others = 0, otherBytes = 0;
        for (int i = 0; i < (int)caches.size(); ++i)
            if (i != big) { if (caches[i].group == caches[big].group) others |= caches[i].mask; otherBytes = caches[i].bytes; }
        snprintf(g_line, sizeof g_line,
                 "vrcam: big-L3 cores - L3 %llu MB on processors %s (group %u) against %llu MB on %s: %s (%zu processors; sets %s, priority %s) - [render] vcache",
                 (unsigned long long)(caches[big].bytes >> 20), Ranges(caches[big].mask).c_str(), caches[big].group,
                 (unsigned long long)(otherBytes >> 20), others ? Ranges(others).c_str() : "?", HowName(how), ids.size(),
                 plan.drawThreadSets || plan.processDefault ? (setsOk ? "set" : "FAILED") : "untouched",
                 plan.raisePriority ? (prioOk ? "above normal" : "FAILED") : "untouched");
        log(g_line);
    }
}

}  // namespace vcache
