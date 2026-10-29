// roof_sides.cpp - [render] roofs_two_sided (vr/roof_sides.h) in the game: two hooks, the material builder (which material is being
// built, and is it a roof's) and the Granny member lookup it calls (the one answer redirected). Every read of the game's memory is
// guarded; the only write is the lookup's own output to its caller - the pointer to the member's value - never the material itself.
//
// Found by bearstonem/d2r-pcvr (MIT, v0.3.18: vr/roof_materials.cpp); written again here on our signatures.

#include "roof_sides.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "sigscan.h"

namespace roofsides {
namespace {

// Granny's 64-bit structures are packed to four bytes.
#pragma pack(push, 4)
struct Material { const char* name; int mapCount; const void* maps; const void* texture; const void* type; const void* object; };
struct Definition { int kind; const char* name; const void* reference; int width; int extra[3]; uintptr_t reserved; };
#pragma pack(pop)
struct Member { const Definition* definition; const void* value; };
static_assert(offsetof(Material, type) == 0x1C && offsetof(Material, object) == 0x24 && sizeof(Material) == 0x2C, "Granny material layout");
static_assert(sizeof(Definition) == 0x2C && offsetof(Definition, width) == 0x14, "Granny member definition layout");

using BuildFn = void* (*)(const char* model, void* file, void* mesh, const Material* material, uint32_t flags, uint32_t pass);
using FindFn = bool (*)(const void* type, const void* object, const char* name, Member* out);
BuildFn g_origBuild = nullptr;
FindFn g_origFind = nullptr;
LogFn g_log = nullptr;
WantFn g_want = nullptr;
std::atomic<WatchFn> g_watch{nullptr};   // [sky] game_sky: the models whose builds are told
std::atomic<unsigned> g_watchTold{0};
std::atomic<bool> g_ready{false};
std::atomic<unsigned> g_told{0};
thread_local Context* t_current = nullptr;
constexpr int32_t kOne = 1;   // where a redirected answer points: lives as long as the plugin

bool Read(void* to, const void* from, size_t n) noexcept {
    if ((uintptr_t)from < 0x10000) return false;
    __try { memcpy(to, from, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Write(void* to, const void* from, size_t n) noexcept {
    __try { memcpy(to, from, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// A string of the game's, up to N - 1 characters, never read across into the next 4 KiB page past its end.
template <size_t N> bool String(const char* from, char (&out)[N]) {
    if (!from) return false;
    for (size_t at = 0; at < N;) {
        const uintptr_t address = (uintptr_t)from + at;
        const size_t n = std::min({(size_t)64, N - at, (size_t)(4096 - (address & 4095))});
        if (!Read(out + at, (const void*)address, n)) return false;
        for (size_t i = at; i < at + n; ++i)
            if (!out[i]) return true;
        at += n;
    }
    return false;
}

bool HookFind(const void* type, const void* object, const char* name, Member* out) {
    const bool found = g_origFind(type, object, name, out);
    Context* ctx = t_current;
    if (!found || !ctx || !out || !g_ready.load(std::memory_order_acquire) || ctx->type != type || ctx->object != object) return found;
    char member[64]{};
    Member result{};
    Definition def{};
    int32_t value = -1;
    if (!String(name, member) || !Read(&result, out, sizeof result) || !Read(&def, result.definition, sizeof def) ||
        !Read(&value, result.value, sizeof value) || !Override(ctx, type, object, member, def.kind, def.width, value))
        return found;
    // Only the caller's result: the builder reads its flag through this pointer and makes the double-sided variant itself.
    const void* one = &kOne;
    if (Write(&out->value, &one, sizeof one)) ++ctx->changes;
    return found;
}

void* HookBuild(const char* model, void* file, void* mesh, const Material* material, uint32_t flags, uint32_t pass) {
    Context scope{};
    Material m{};
    char rawPath[512]{}, rawName[128]{}, path[512]{}, name[128]{};
    std::string_view p, n;
    const bool chosen = g_ready.load(std::memory_order_acquire) && g_want && Read(&m, material, sizeof m) && String(model, rawPath) &&
                        String(m.name, rawName) && Canonical(std::string_view(rawPath), path, &p) && Canonical(std::string_view(rawName), name, &n) &&
                        Chosen(g_want(), p, n);
    if (chosen) { scope.type = m.type; scope.object = m.object; }
    Scope guard(t_current, chosen ? &scope : nullptr);   // a nested build of another material does not inherit this one
    void* result = g_origBuild(model, file, mesh, material, flags, pass);
    const WatchFn watch = g_watch.load(std::memory_order_acquire);
    if (watch && g_log && (p.empty() ? String(model, rawPath) && Canonical(std::string_view(rawPath), path, &p) : true) && watch(path) &&
        g_watchTold.load() < 96) {
        // the material, its texture, its maps (Granny: a map is {usage, material}, packed to four bytes) and their materials' textures
        Material mm{};
        char mName[128] = "?", mTex[256] = "", usage[4][48] = {}, mapName[4][128] = {}, mapTex[4][256] = {};
        WatchedMap maps[4] = {};
        int mapCount = 0;
        auto textureName = [](const void* texture, char (&out)[256]) {   // granny_texture: its FromFileName first
            const char* from = nullptr;
            return texture && Read(&from, texture, sizeof from) && String(from, out);
        };
        if (Read(&mm, material, sizeof mm)) {
            String(mm.name, mName);
            if (!textureName(mm.texture, mTex)) mTex[0] = 0;
            for (int i = 0; i < mm.mapCount && i < 4 && mm.maps; ++i) {
                const void* entry[2] = {};   // (usage, material) at 16 bytes each
                if (!Read(entry, (const uint8_t*)mm.maps + 16 * i, sizeof entry)) break;
                Material sub{};
                String((const char*)entry[0], usage[i]);
                if (entry[1] && Read(&sub, entry[1], sizeof sub)) {
                    String(sub.name, mapName[i]);
                    if (!textureName(sub.texture, mapTex[i])) mapTex[i][0] = 0;
                }
                maps[i] = {usage[i], mapName[i], mapTex[i][0] ? mapTex[i] : nullptr};
                mapCount = i + 1;
            }
        }
        char key[700];
        snprintf(key, sizeof key, "%s|%s|%p", path, mName, result);
        static std::mutex toldMutex;
        static std::vector<std::string> told;
        bool fresh = false;
        {
            std::lock_guard<std::mutex> lock(toldMutex);
            if (std::find(told.begin(), told.end(), key) == told.end()) { told.push_back(key); fresh = true; }
        }
        if (fresh) {
            g_watchTold.fetch_add(1);
            char line[1600];
            WatchLine(line, sizeof line, path, mName, mTex[0] ? mTex : nullptr, maps, mapCount, result);
            g_log(line);
        }
    }
    if (chosen && g_log && g_told.fetch_add(1) < 64) {
        char line[760];
        snprintf(line, sizeof line, "vrcam: roofs two-sided - %s, material %s: %s", path, name,
                 scope.changes ? "double-sided" : "the lookup was not asked (or already double-sided)");
        g_log(line);
    }
    return result;
}

}  // namespace

void Watch(WatchFn watched) { g_watch.store(watched, std::memory_order_release); }

void Install(LogFn log, WantFn want) {
    if (g_ready.load()) return;
    static bool findIn = false, buildIn = false, toldWait = false;
    g_log = log;
    g_want = want;
    // Once a hook is in, its entry's bytes are ours: the checks of both go first.
    if (!findIn && !buildIn &&
        !(d2rsig::Check(kBuildRva, kBuildSig, sizeof kBuildSig) && d2rsig::Check(kFindRva, kFindSig, sizeof kFindSig) &&
          d2rsig::Check(kBuildCallRva, kBuildCallSig, sizeof kBuildCallSig) && d2rsig::Check(kFlagCallRva, kFlagCallSig, sizeof kFlagCallSig) &&
          d2rsig::Check(kFindResultRva, kFindResultSig, sizeof kFindResultSig))) {
        if (!toldWait && log) { toldWait = true; log("vrcam: roofs two-sided - the material builder is not there yet (or another build): roofs one-sided as the game has them"); }
        return;
    }
    if (!findIn) findIn = d2rsig::Hook(kFindRva, kFindSig, sizeof kFindSig, (void*)&HookFind, (void**)&g_origFind);
    if (!findIn) return;
    if (!buildIn) buildIn = d2rsig::Hook(kBuildRva, kBuildSig, sizeof kBuildSig, (void*)&HookBuild, (void**)&g_origBuild);
    if (!buildIn) return;
    g_ready.store(true, std::memory_order_release);
    if (log) log("vrcam: roofs two-sided - the material builder's hooks in ([render] roofs_two_sided; for roofs built from now on)");
}

}  // namespace roofsides
