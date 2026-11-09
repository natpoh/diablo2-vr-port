// lit_flat.cpp - see lit_flat.h. The game's lit pipelines are kept as vr/fog_height.cpp keeps its fog pass's (the description or stream, its
// shaders and input layout copied, its root signature held), and a copy with our pixel shader is made for the light now - on the present's
// thread, a few a frame, only while the key is on. Bound in the game's place (vr/native_d3d12.h's psoSwap) while on, in F4.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include "lit_flat.h"

#include <atomic>
#include <cstdarg>
#include <deque>
#include <algorithm>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "native_d3d12.h"   // natived3d::SetOurs
#include "pso_stream.h"
#include "render_bench.h"   // the lit stage's shaders (vr/render_bench_hashes.h)
#include "view_gate.h"

namespace litflat {
namespace {

void (*g_log)(const char*) = nullptr;
void Say(const char* fmt, ...) {
    if (!g_log) return;
    char b[1024];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    g_log(b);
}
uint64_t Fnv(const void* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) { h ^= ((const uint8_t*)p)[i]; h *= 1099511628211ull; }
    return h;
}
std::vector<uint8_t> Copy(const D3D12_SHADER_BYTECODE& b) {
    return b.pShaderBytecode && b.BytecodeLength ? std::vector<uint8_t>((const uint8_t*)b.pShaderBytecode, (const uint8_t*)b.pShaderBytecode + b.BytecodeLength)
                                                 : std::vector<uint8_t>();
}
D3D12_SHADER_BYTECODE Code(const std::vector<uint8_t>& v) { return D3D12_SHADER_BYTECODE{v.empty() ? nullptr : v.data(), v.size()}; }

struct Entry {
    ID3D12PipelineState* game = nullptr;
    ID3D12Device* dev = nullptr;
    Family family = kNotOurs;
    int variant = kVarPlain;                     // the game's cut / blending, copied (Variant; the terrain: plain)
    bool stream = false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};   // (a graphics description, its pointers into the copies below)
    std::vector<uint8_t> bytes;                  // (a stream: its bytes)
    size_t psAt = 0;                             // (a stream: where its pixel shader's D3D12_SHADER_BYTECODE is)
    std::vector<std::vector<uint8_t>> codes;     // the other shaders, copied (they stay put: reserved)
    std::vector<D3D12_INPUT_ELEMENT_DESC> il;
    std::vector<std::string> names;
    // our copies, one for each light: [0] out in the open, [1] underground ([render] potato_light_brightness_caves) - both kept, so an area
    // change only picks the other (the light is baked into the shader: the game's root signature has no room for a constant of ours)
    std::atomic<ID3D12PipelineState*> ours[2] = {};
    std::atomic<uint32_t> gen[2] = {};           // the light each was made for (0: none)
    bool failed = false;
};
SRWLOCK g_lock = SRWLOCK_INIT;
std::deque<std::unique_ptr<Entry>> g_entries;                      // (under g_lock)
std::unordered_map<ID3D12PipelineState*, Entry*> g_byGame;         // (under g_lock; read shared by Swap)
std::atomic<int> g_count{0};
std::atomic<uint32_t> g_familyCount[3] = {};
std::atomic<uint32_t> g_variantCount[kVariants] = {};   // the models' kept, by their cut
std::atomic<uint32_t> g_leftGame{0};                      // lit pipelines with a cut or blending we do not copy: left the game's
// our shaders' slots: 0 the terrain's, 1 + v the models' variant v
constexpr int kSlots = 1 + kVariants;
inline int SlotOf(Family f, int variant) { return f == kTerrain ? 0 : 1 + variant; }

std::atomic<bool> g_on{false};
std::atomic<uint32_t> g_gen[2] = {1, 1};      // [0] the open, [1] underground
std::atomic<float> g_level[2] = {1.0f, 1.0f};
bool (*g_undergroundFn)() = nullptr;          // which light is in force now (the fog's own split: caves and dungeons, vr/sky/sky_choice.cpp)
std::atomic<uint32_t> g_rgb{0xFFFFFF};
std::atomic<bool> g_gameLight{false};        // [render] potato_light_game: the game's ambient + key light, no shadows (kGameLight)
std::atomic<bool> g_area{false};             // the colour follows the area's (g_areaFn), read every 2 s on the present's thread
bool (*g_areaFn)(float rgb[3]) = nullptr;
ULONGLONG g_areaAt = 0;
// the present's: our shaders for each light's g_gen, one a slot (compiled when a kept pipeline first needs it)
uint32_t g_codeGen[2][kSlots] = {};
std::vector<uint8_t> g_code[2][kSlots];
bool g_compileFailed[2][kSlots] = {};
struct Retired { ID3D12PipelineState* p; ULONGLONG at; };
std::vector<Retired> g_retired;
std::atomic<uint32_t> g_swapped{0}, g_notYet{0}, g_notF4{0}, g_builds{0}, g_buildFails{0};
bool g_told = false;

void CopyLayout(Entry& e, const D3D12_INPUT_LAYOUT_DESC& l) {
    e.il.assign(l.pInputElementDescs, l.pInputElementDescs + l.NumElements);
    e.names.clear();
    e.names.reserve(e.il.size());
    for (auto& el : e.il) e.names.push_back(el.SemanticName ? el.SemanticName : "");
    for (size_t i = 0; i < e.il.size(); ++i) e.il[i].SemanticName = e.names[i].c_str();
}
// A lit pixel shader of a layout we take? (the stage's hash first: cheap for every other pipeline)
Family Take(const D3D12_SHADER_BYTECODE& ps) {
    if (!ps.pShaderBytecode || !ps.BytecodeLength) return kNotOurs;
    if (renderbench::StageOf(Fnv(ps.pShaderBytecode, ps.BytecodeLength)) != renderbench::kLit) return kNotOurs;
    return FamilyOf((const uint8_t*)ps.pShaderBytecode, ps.BytecodeLength);
}
// Its cut and blending (Variant), checked against the pipeline's own blending (Accept). kVarUnknown: left the game's. Each distinct blended
// lit pipeline is told once (its shader's kind and the first target's blend), so a log says what draws a decal (up to 24 lines a session).
std::mutex g_blendLock;
std::vector<uint64_t> g_blendSeen;
int VariantFor(Family f, const D3D12_SHADER_BYTECODE& ps, const D3D12_BLEND_DESC& bd) {
    const int v = VariantOf((const uint8_t*)ps.pShaderBytecode, ps.BytecodeLength);
    const D3D12_RENDER_TARGET_BLEND_DESC& r0 = bd.RenderTarget[0];
    bool other = r0.LogicOpEnable != FALSE;
    for (int i = 1; i < 8 && bd.IndependentBlendEnable; ++i)
        other |= bd.RenderTarget[i].BlendEnable != FALSE || bd.RenderTarget[i].LogicOpEnable != FALSE;
    const bool rt0 = r0.BlendEnable != FALSE;
    const bool ok = Accept(f, v, rt0, other);
    if (rt0 || other) {
        const uint64_t key = (uint64_t)f | (uint64_t)(v + 1) << 4 | (uint64_t)r0.SrcBlend << 8 | (uint64_t)r0.DestBlend << 16 | (uint64_t)r0.BlendOp << 24 |
                             (uint64_t)r0.SrcBlendAlpha << 32 | (uint64_t)r0.DestBlendAlpha << 40 | (uint64_t)other << 48 | (uint64_t)bd.AlphaToCoverageEnable << 49;
        std::lock_guard<std::mutex> g(g_blendLock);
        if (g_blendSeen.size() < 24 && std::find(g_blendSeen.begin(), g_blendSeen.end(), key) == g_blendSeen.end()) {
            g_blendSeen.push_back(key);
            Say("vrcam: render bench - lit flat: a blended lit pipeline - %s, its cut %s; first target blend %s src %d dst %d op %d, alpha src %d dst %d; "
                "other targets / logic op %s, alpha to coverage %s: %s",
                f == kModels ? "models' family" : "terrain", VariantName(v), rt0 ? "on" : "off", (int)r0.SrcBlend, (int)r0.DestBlend, (int)r0.BlendOp,
                (int)r0.SrcBlendAlpha, (int)r0.DestBlendAlpha, other ? "blend" : "no", bd.AlphaToCoverageEnable ? "on" : "off",
                ok ? "ours copies it (the game's weighted transparency)" : "it stays the game's");
        }
    }
    if (!ok) {
        if (g_leftGame.fetch_add(1) < 4 && !rt0 && !other)
            Say("vrcam: render bench - lit flat: a lit pipeline (%s) with a cut we do not copy (%s): it stays the game's", f == kModels ? "models" : "terrain",
                VariantName(v));
        return kVarUnknown;
    }
    return v;
}
void Add(std::unique_ptr<Entry> e, ID3D12Device* dev, ID3D12PipelineState* pso) {
    dev->AddRef();
    pso->AddRef();   // (its address stays its own while we compare against it)
    e->dev = dev;
    e->game = pso;
    g_familyCount[e->family].fetch_add(1, std::memory_order_relaxed);
    if (e->family == kModels) g_variantCount[e->variant].fetch_add(1, std::memory_order_relaxed);
    AcquireSRWLockExclusive(&g_lock);
    g_byGame[pso] = e.get();
    g_entries.push_back(std::move(e));
    g_count.store((int)g_entries.size(), std::memory_order_release);
    ReleaseSRWLockExclusive(&g_lock);
}

// One slot's shader for a light (the present's thread). `compiled` counts the compiles this present (a few at most: each ~10-30 ms).
bool CompileFor(int set, int slot, uint32_t gen, int* compiled) {
    if (g_codeGen[set][slot] == gen) return !g_compileFailed[set][slot];
    if (*compiled >= 3) return false;   // (the next present)
    ++*compiled;
    g_codeGen[set][slot] = gen;
    g_compileFailed[set][slot] = false;
    g_code[set][slot].clear();
    HMODULE lib = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto compile = lib ? reinterpret_cast<pD3DCompile>(GetProcAddress(lib, "D3DCompile")) : nullptr;
    const bool game = g_gameLight.load();
    const std::string light = LightDefine(game ? 0xFFFFFFu : g_rgb.load(), g_level[set].load());
    const Family f = slot == 0 ? kTerrain : kModels;
    const int variant = slot == 0 ? kVarPlain : slot - 1;
    const D3D_SHADER_MACRO macros[] = {{"LIGHT", light.c_str()}, {"GAME_LIGHT", game ? "1" : "0"}, {"CLIP", ClipMacro(variant)},
                                       {"BLEND", BlendMacro(variant)}, {nullptr, nullptr}};
    ID3DBlob* blob = nullptr;
    ID3DBlob* errors = nullptr;
    const std::string src = Source(f);
    if (compile && SUCCEEDED(compile(src.data(), src.size(), "vrcam-lit-flat", macros, nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                                     &blob, &errors))) {
        const uint8_t* p = (const uint8_t*)blob->GetBufferPointer();
        g_code[set][slot].assign(p, p + blob->GetBufferSize());
    }
    if (g_code[set][slot].empty()) {
        g_compileFailed[set][slot] = true;
        Say("vrcam: render bench - lit flat: our %s shader (%s) did not compile (%s)", f == kModels ? "models'" : "terrain's", VariantName(variant),
            !lib ? "no d3dcompiler_47.dll" : !compile ? "no D3DCompile" : errors ? (const char*)errors->GetBufferPointer() : "unknown");
    }
    if (blob) blob->Release();
    if (errors) errors->Release();
    if (lib) FreeLibrary(lib);
    return !g_compileFailed[set][slot];
}

void Build(Entry& e, int set, uint32_t gen) {
    const std::vector<uint8_t>& ps = g_code[set][SlotOf(e.family, e.variant)];
    ID3D12PipelineState* p = nullptr;
    HRESULT hr = E_FAIL;
    const bool was = natived3d::Ours();
    natived3d::SetOurs(true);
    if (e.stream) {
        std::vector<uint8_t> s = e.bytes;
        const D3D12_SHADER_BYTECODE code = Code(ps);
        std::memcpy(&s[e.psAt], &code, sizeof code);
        ID3D12Device2* d2 = nullptr;
        if (SUCCEEDED(e.dev->QueryInterface(__uuidof(ID3D12Device2), (void**)&d2)) && d2) {
            const D3D12_PIPELINE_STATE_STREAM_DESC sd{s.size(), s.data()};
            hr = d2->CreatePipelineState(&sd, __uuidof(ID3D12PipelineState), (void**)&p);
            d2->Release();
        }
    } else {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d = e.desc;
        d.PS = Code(ps);
        hr = e.dev->CreateGraphicsPipelineState(&d, __uuidof(ID3D12PipelineState), (void**)&p);
    }
    natived3d::SetOurs(was);
    if (FAILED(hr) || !p) {
        if (!e.failed && g_buildFails.fetch_add(1) < 4)
            Say("vrcam: render bench - lit flat: a copy of a lit pipeline (%s) was refused by the device (0x%08X): that one stays the game's",
                e.family == kModels ? "models" : "terrain", (unsigned)hr);
        e.failed = true;
        return;
    }
    ID3D12PipelineState* old = e.ours[set].exchange(p);
    e.gen[set].store(gen, std::memory_order_release);
    if (old) g_retired.push_back({old, GetTickCount64()});
    g_builds.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace

void SetLog(void (*log)(const char*)) { g_log = log; }

void OnGraphicsPso(ID3D12Device* dev, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d, ID3D12PipelineState* pso) {
    if (!dev || !d || !pso || d->StreamOutput.NumEntries) return;
    const Family f = Take(d->PS);
    if (f == kNotOurs) return;
    const int v = VariantFor(f, d->PS, d->BlendState);
    if (v == kVarUnknown) return;
    auto e = std::make_unique<Entry>();
    e->family = f;
    e->variant = v;
    e->stream = false;
    e->desc = *d;
    e->codes.reserve(4);
    e->codes = {Copy(d->VS), Copy(d->DS), Copy(d->HS), Copy(d->GS)};
    e->desc.VS = Code(e->codes[0]); e->desc.DS = Code(e->codes[1]); e->desc.HS = Code(e->codes[2]); e->desc.GS = Code(e->codes[3]);
    e->desc.PS = D3D12_SHADER_BYTECODE{};
    CopyLayout(*e, d->InputLayout);
    e->desc.InputLayout = D3D12_INPUT_LAYOUT_DESC{e->il.empty() ? nullptr : e->il.data(), (UINT)e->il.size()};
    e->desc.CachedPSO = D3D12_CACHED_PIPELINE_STATE{};
    if (e->desc.pRootSignature) e->desc.pRootSignature->AddRef();
    Add(std::move(e), dev, pso);
}

void OnStreamPso(ID3D12Device* dev, const D3D12_PIPELINE_STATE_STREAM_DESC* d, ID3D12PipelineState* pso) {
    if (!dev || !d || !pso || !d->pPipelineStateSubobjectStream || !d->SizeInBytes || d->SizeInBytes > 65536) return;
    const uint8_t* s = (const uint8_t*)d->pPipelineStateSubobjectStream;
    size_t psAt = 0;
    if (!psostream::Find(s, d->SizeInBytes, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, &psAt)) return;
    D3D12_SHADER_BYTECODE ps;
    std::memcpy(&ps, s + psAt, sizeof ps);
    const Family f = Take(ps);
    if (f == kNotOurs) return;
    size_t blendAt = 0;   // (no blend subobject: the default, nothing blends)
    D3D12_BLEND_DESC bd{};
    if (psostream::Find(s, d->SizeInBytes, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, &blendAt) && blendAt + sizeof(D3D12_BLEND_DESC) <= d->SizeInBytes)
        std::memcpy(&bd, s + blendAt, sizeof bd);
    const int variant = VariantFor(f, ps, bd);
    if (variant == kVarUnknown) return;
    auto e = std::make_unique<Entry>();
    e->family = f;
    e->variant = variant;
    e->stream = true;
    e->bytes.assign(s, s + d->SizeInBytes);
    e->psAt = psAt;
    e->codes.reserve(16);   // (the stream points into these: they must not move)
    bool ok = true;
    for (size_t at = 0; at + 4 <= e->bytes.size() && ok;) {
        uint32_t type = 0;
        std::memcpy(&type, &e->bytes[at], 4);
        size_t vo = 0, size = 0;
        if (!psostream::Subobject(type, &vo, &size) || at + size > e->bytes.size()) { ok = false; break; }
        uint8_t* v = &e->bytes[at + vo];
        if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS) {
            const D3D12_SHADER_BYTECODE none{};
            std::memcpy(v, &none, sizeof none);   // (ours goes in at the build)
        } else if (psostream::IsShader(type)) {
            D3D12_SHADER_BYTECODE c;
            std::memcpy(&c, v, sizeof c);
            if (e->codes.size() >= 15) { ok = false; break; }
            e->codes.push_back(Copy(c));
            c = Code(e->codes.back());
            std::memcpy(v, &c, sizeof c);
        } else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT) {
            D3D12_INPUT_LAYOUT_DESC l;
            std::memcpy(&l, v, sizeof l);
            CopyLayout(*e, l);
            l = D3D12_INPUT_LAYOUT_DESC{e->il.empty() ? nullptr : e->il.data(), (UINT)e->il.size()};
            std::memcpy(v, &l, sizeof l);
        } else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE) {
            ID3D12RootSignature* r = nullptr;
            std::memcpy(&r, v, sizeof r);
            if (r) r->AddRef();
        } else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO) {
            const D3D12_CACHED_PIPELINE_STATE none{};
            std::memcpy(v, &none, sizeof none);
        } else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT) {
            D3D12_STREAM_OUTPUT_DESC so;
            std::memcpy(&so, v, sizeof so);
            if (so.NumEntries) ok = false;
        }
        at += size;
    }
    if (!ok) return;
    Add(std::move(e), dev, pso);
}

void SetAreaColour(bool (*fn)(float rgb[3])) { g_areaFn = fn; }
void SetUnderground(bool (*fn)()) { g_undergroundFn = fn; }

void Configure(bool on, float level, uint32_t rgb, bool area, float levelCaves, bool gameLight) {
    auto clampL = [](float v) { return v < 0.0f ? 0.0f : v > 16.0f ? 16.0f : v; };
    level = clampL(level);
    levelCaves = clampL(levelCaves < 0.0f ? level : levelCaves);   // (no caves' level given: the open's)
    rgb &= 0xFFFFFF;
    if (gameLight) area = false;   // (the game's own light: no colour of ours)
    const bool wasGame = g_gameLight.exchange(gameLight);
    const bool wasArea = g_area.exchange(area);
    // (with the area's colour the colour is Frame's to set: only the levels and the switch to it count here)
    const bool colourChanged = gameLight != wasGame || area != wasArea || (!area && rgb != g_rgb.load());
    const bool changed[2] = {colourChanged || level != g_level[0].load(), colourChanged || levelCaves != g_level[1].load()};
    g_level[0].store(level);
    g_level[1].store(levelCaves);
    if (!area) g_rgb.store(rgb);
    else if (!wasArea) { g_rgb.store(0xFFFFFF); g_areaAt = 0; }
    for (int k = 0; k < 2; ++k) if (changed[k]) g_gen[k].fetch_add(1);
    const bool was = g_on.exchange(on);
    if (on != was || (on && (changed[0] || changed[1])))
        Say("vrcam: render bench - lit flat %s (the Potato light): %s; light #%06X x %.2f in the open, x %.2f underground; the game's lit pipelines "
            "kept: %u models' (their cut copied: %u plain, %u albedo, %u opacity, %u both, %u foliage blended, %u grunge blended; %u left the "
            "game's), %u terrain (a copy for each light, made a few a frame)%s", on ? "ON" : "off",
            !on ? "the game's own lit pass again"
            : gameLight ? "the ground, walls, objects, monsters and the hero drawn in their own colour times the GAME'S OWN light - the area's "
                          "ambient (its probe's sky colours x the vis file's iblIntensity) and its key light (sun / moon colour x power x N.L) as the "
                          "game hands them to its lit pass, no shadows, torches, light loops or reflections ([render] potato_light_game; VR first "
                          "person only)"
                        : "the ground, walls, objects, monsters and the hero drawn in their own colour times one light, evenly - no sun, shadows, "
                          "torches or light loops in the lit pass (VR first person only)",
            gameLight ? 0xFFFFFFu : g_rgb.load(), level, levelCaves, g_familyCount[kModels].load(), g_variantCount[kVarPlain].load(),
            g_variantCount[kVarAlbedoRef].load(), g_variantCount[kVarOpacityRef].load(), g_variantCount[kVarBothRef].load(),
            g_variantCount[kVarFoliage].load(), g_variantCount[kVarGrunge].load(), g_leftGame.load(), g_familyCount[kTerrain].load(),
            area ? "; its colour the area's own, followed every 2 s" : "");
}

namespace {
int SetNow() { return g_undergroundFn && g_undergroundFn() ? 1 : 0; }
}

void Frame() {
    const ULONGLONG now = GetTickCount64();
    if (!g_retired.empty()) {   // copies no longer bound: let go after 5 s (a recorded list may still name them that long? never this long)
        size_t keep = 0;
        for (size_t i = 0; i < g_retired.size(); ++i) {
            if (now - g_retired[i].at >= 5000) g_retired[i].p->Release();
            else g_retired[keep++] = g_retired[i];
        }
        g_retired.resize(keep);
    }
    if (!g_on.load(std::memory_order_relaxed)) return;
    if (g_area.load(std::memory_order_relaxed) && g_areaFn && now - g_areaAt >= 2000) {   // the area's colour: a new tint makes the copies again
        g_areaAt = now;
        float c[3];
        if (g_areaFn(c)) {
            const uint32_t t = AreaTint(c), was = g_rgb.load();
            if (t != was) {
                g_rgb.store(t);
                g_gen[0].fetch_add(1);
                g_gen[1].fetch_add(1);
                Say("vrcam: render bench - lit flat: the area's colour now #%06X (was #%06X)", t, was);
            }
        }
    }
    // the light in force first, then the other (so an area change finds it ready); 24 copies a present in all
    const int first = SetNow();
    const int n = g_count.load(std::memory_order_acquire);
    int built = 0, compiled = 0;
    for (int pass = 0; pass < 2 && built < 24; ++pass) {
        const int set = pass == 0 ? first : 1 - first;
        const uint32_t gen = g_gen[set].load();
        for (int step = 0; step < n && built < 24; ++step) {
            AcquireSRWLockShared(&g_lock);
            Entry* e = (size_t)step < g_entries.size() ? g_entries[(size_t)step].get() : nullptr;
            ReleaseSRWLockShared(&g_lock);
            if (!e || e->failed || e->gen[set].load(std::memory_order_relaxed) == gen) continue;
            if (!CompileFor(set, SlotOf(e->family, e->variant), gen, &compiled)) continue;   // (its shader did not compile, or not yet)
            Build(*e, set, gen);
            ++built;
        }
    }
}

ID3D12PipelineState* Swap(ID3D12PipelineState* pso) {
    if (!g_on.load(std::memory_order_relaxed) || !g_count.load(std::memory_order_relaxed)) return pso;
    AcquireSRWLockShared(&g_lock);
    const auto it = g_byGame.find(pso);
    Entry* e = it != g_byGame.end() ? it->second : nullptr;
    ReleaseSRWLockShared(&g_lock);
    if (!e) return pso;
    if (!viewgate::F4()) { g_notF4.fetch_add(1, std::memory_order_relaxed); return pso; }
    // the light in force; its copy for an older light while the new one is being made (never the game's own lit pass in between: with the
    // sun's shadows left out it reads all in shadow), else the other light's
    const int set = SetNow();
    ID3D12PipelineState* o = e->ours[set].load(std::memory_order_acquire);
    if (!o) o = e->ours[1 - set].load(std::memory_order_acquire);
    if (!o) {
        g_notYet.fetch_add(1, std::memory_order_relaxed);
        return pso;
    }
    g_swapped.fetch_add(1, std::memory_order_relaxed);
    return o;
}

std::string Line() {
    const bool on = g_on.load();
    const uint32_t sw = g_swapped.exchange(0), notYet = g_notYet.exchange(0), notF4 = g_notF4.exchange(0), builds = g_builds.exchange(0);
    if (!on) {
        if (!g_told) return std::string();
        g_told = false;
        return "vrcam: render bench, 10 s: lit flat off - the game's lit pass again (potato_light / bench_lit_flat 0)";
    }
    g_told = true;
    int made[2] = {};
    const uint32_t gen0 = g_gen[0].load(), gen1 = g_gen[1].load();
    AcquireSRWLockShared(&g_lock);
    for (const auto& e : g_entries) { made[0] += e->gen[0].load() == gen0 ? 1 : 0; made[1] += e->gen[1].load() == gen1 ? 1 : 0; }
    const int n = (int)g_entries.size();
    ReleaseSRWLockShared(&g_lock);
    char b[560];
    snprintf(b, sizeof b, "vrcam: render bench, 10 s: LIT FLAT (the Potato light %s #%06X, x %.2f in the open, x %.2f underground; now %s) - binds given "
             "ours %u, still the game's: %u (no copy yet) %u (not F4); copies made for the lights now: open %d, underground %d of %d (%u in these 10 s, "
             "%u refused in all)",
             g_gameLight.load() ? "in the game's own colours" : "of one colour", g_gameLight.load() ? 0xFFFFFFu : g_rgb.load(), g_level[0].load(),
             g_level[1].load(), SetNow() ? "underground" : "in the open", sw, notYet, notF4, made[0], made[1], n, builds,
             g_buildFails.load());
    return b;
}

}  // namespace litflat
