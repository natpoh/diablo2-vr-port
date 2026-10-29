// [debug] sound_trace: see sound_trace.h.

#include "sound_trace.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#include "sigscan.h"

namespace soundtrace {
namespace {

constexpr uint64_t RVA_CREATE = 0x1576D40, RVA_PLAY = 0x157CB90;
// push rbp; push rbx; push rsi; push rdi; push r12..r15; lea rbp, [rsp-0x98]; sub rsp, 0x198 (the decrypted image of 3.3.93787 under D2RLoader 1.3.1)
const uint8_t kSigCreate[28] = {0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0xAC,0x24,0x68,0xFF,0xFF,0xFF,0x48,0x81,0xEC,0x98,0x01,0x00,0x00};
// mov [rsp+8], rbx; mov [rsp+0x18], rsi; push rbp; push rdi; push r14; mov rbp, rsp; sub rsp, 0x60; mov rax, [rdx]; mov rsi, rdx; mov rdi, rcx; test rax, rax
const uint8_t kSigPlay[33] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x55,0x57,0x41,0x56,0x48,0x8B,0xEC,0x48,0x83,0xEC,0x60,0x48,0x8B,0x02,0x48,0x8B,0xF2,
                              0x48,0x8B,0xF9,0x48,0x85,0xC0};
using CreateFn = uint64_t (*)(void* engine, void* out, void* resource, uint64_t is3d, uint64_t flag, uint64_t group);
using PlayFn = uint64_t (*)(void* engine, void* ref, uint64_t a, uint64_t b);
CreateFn OrigCreate = nullptr;
PlayFn OrigPlay = nullptr;

std::atomic<bool> g_wanted{false}, g_in{false};
LogFn g_log = nullptr;
void Log(const char* s) { if (g_log) g_log(s); }

// A read of the game's memory that may fail (a stale pointer): never a fault.
bool Read(const void* at, void* to, size_t n) {
    SIZE_T got = 0;
    return at && ReadProcessMemory(GetCurrentProcess(), at, to, n, &got) && got == n;
}

// A sound's name: a path-like string the resource points at (or holds) in its first bytes - the first printable one of 6+ chars with a '/', '\\' or '.'.
bool Printable(const char* s, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (s[i] < 0x20 || s[i] > 0x7E) return false;
    return true;
}
std::string NameOf(void* resource) {
    uint8_t blob[0x100];
    if (!Read(resource, blob, sizeof blob)) return "";
    for (size_t o = 0; o + 8 <= sizeof blob; o += 8) {
        uintptr_t p = 0;
        std::memcpy(&p, blob + o, 8);
        if (p < 0x10000 || p > 0x7FFFFFFFFFFFull) continue;
        char s[160] = {};
        if (!Read((const void*)p, s, sizeof s - 1)) continue;
        const size_t len = strnlen(s, sizeof s - 1);
        if (len >= 6 && len < sizeof s - 1 && Printable(s, len) && (std::strchr(s, '/') || std::strchr(s, '\\') || std::strchr(s, '.'))) return std::string(s, len);
    }
    for (size_t o = 0; o + 6 < sizeof blob; ++o) {   // (a name kept inside the resource)
        const char* s = (const char*)blob + o;
        const size_t len = strnlen(s, sizeof blob - o);
        if (len >= 6 && o + len < sizeof blob && Printable(s, len) && (std::strchr(s, '/') || std::strchr(s, '.'))) return std::string(s, len);
    }
    return "";
}

std::mutex g_lock;
std::unordered_map<uintptr_t, int> g_byResource;    // a resource -> its name's index
std::unordered_map<uintptr_t, int> g_byInstance;    // an instance (what an AudioInstanceRef holds) -> its name's index
std::unordered_map<int, ULONGLONG> g_toldPlay;      // a name -> when its play was last told
std::vector<std::string> g_names;
constexpr size_t kMaxNames = 2000, kMaxInstances = 8192;

uint64_t HookCreate(void* engine, void* out, void* resource, uint64_t is3d, uint64_t flag, uint64_t group) {
    const uint64_t r = OrigCreate(engine, out, resource, is3d, flag, group);
    if (!g_wanted.load(std::memory_order_relaxed) || !resource || !out) return r;
    uintptr_t inst = 0;
    Read(out, &inst, sizeof inst);
    std::lock_guard<std::mutex> lock(g_lock);
    int idx = -1;
    const auto it = g_byResource.find((uintptr_t)resource);
    if (it != g_byResource.end()) {
        idx = it->second;
    } else if (g_names.size() < kMaxNames) {
        const std::string name = NameOf(resource);
        idx = (int)g_names.size();
        g_names.push_back(name.empty() ? "(no name read)" : name);
        g_byResource[(uintptr_t)resource] = idx;
        char b[300];
        snprintf(b, sizeof b, "vrcam: sound trace - sound #%d made: '%s' (resource %llX, %s)", idx, g_names[idx].c_str(), (unsigned long long)(uintptr_t)resource,
                 (is3d & 0xFF) ? "3D" : "2D");
        Log(b);
    }
    if (idx >= 0 && inst) {
        if (g_byInstance.size() > kMaxInstances) g_byInstance.clear();
        g_byInstance[inst] = idx;
    }
    return r;
}

uint64_t HookPlay(void* engine, void* ref, uint64_t a, uint64_t b) {
    if (g_wanted.load(std::memory_order_relaxed) && ref) {
        uintptr_t inst = 0;
        if (Read(ref, &inst, sizeof inst) && inst) {
            std::lock_guard<std::mutex> lock(g_lock);
            const auto it = g_byInstance.find(inst);
            const ULONGLONG now = GetTickCount64();
            if (it != g_byInstance.end()) {
                ULONGLONG& at = g_toldPlay[it->second];
                if (now - at >= 2000) {
                    at = now;
                    char s[300];
                    snprintf(s, sizeof s, "vrcam: sound trace - played #%d '%s'", it->second, g_names[it->second].c_str());
                    Log(s);
                }
            }
        }
    }
    return OrigPlay(engine, ref, a, b);
}

}  // namespace

void SetLog(LogFn fn) { g_log = fn; }

void SetWanted(bool on) {
    if (g_wanted.exchange(on) != on)
        Log(on ? "vrcam: [debug] sound_trace on - each of the game's sounds is told by name when it is first made, and when it plays (at most once in 2 s a name)"
               : "vrcam: [debug] sound_trace off");
}

void Install() {
    static bool failed = false;
    static int tries = 0;
    if (failed || g_in.load() || !g_wanted.load()) return;
    if (!d2rsig::Check(RVA_CREATE, kSigCreate, sizeof kSigCreate) || !d2rsig::Check(RVA_PLAY, kSigPlay, sizeof kSigPlay)) {
        if (++tries == 60) {
            failed = true;
            Log("vrcam: [debug] sound_trace - the game's sound engine is not where 3.3.93787 under D2RLoader 1.3.1 has it (another build?): nothing traced");
        }
        return;
    }
    const bool c = d2rsig::Hook(RVA_CREATE, kSigCreate, sizeof kSigCreate, (void*)&HookCreate, (void**)&OrigCreate);
    const bool p = c && d2rsig::Hook(RVA_PLAY, kSigPlay, sizeof kSigPlay, (void*)&HookPlay, (void**)&OrigPlay);
    g_in.store(c && p);
    failed = !(c && p);
    Log(c && p ? "vrcam: [debug] sound_trace - the sound engine's hooks are in (CreateInstance, Play): the names follow as the game makes its sounds"
               : "vrcam: [debug] sound_trace - a hook on the sound engine FAILED to go in: nothing traced");
}

}  // namespace soundtrace
