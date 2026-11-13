// ao_vr.cpp - see ao_vr.h.

#include "ao_vr.h"

#include <windows.h>
#include <d3d12.h>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dxbc_ao_radius.h"
#include "native_d3d12.h"
#include "pso_stream.h"

namespace aovr {
namespace {

LogFn g_log = nullptr;
void Say(const char* s) { if (g_log) g_log(s); }

// The game's generate pipelines (one per quality permutation it makes): its description kept - the graphics description or its subobject
// stream, with the shaders and the input layout copied and the root signature held - and our variant for the current radius.
struct Entry {
    ID3D12PipelineState* game = nullptr;
    std::atomic<ID3D12PipelineState*> ours{nullptr};
    ID3D12Device* dev = nullptr;
    bool stream = false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    std::vector<uint8_t> bytes;
    size_t psAt = 0;
    std::vector<std::vector<uint8_t>> codes;   // codes[0] the pixel shader
    std::vector<D3D12_INPUT_ELEMENT_DESC> il;
    std::vector<std::string> names;
    float radius = 0.0f;   // what `ours` was made for
    bool failed = false;
};
constexpr int kMax = 16;
Entry g_e[kMax];
std::atomic<int> g_n{0};
SRWLOCK g_lock = SRWLOCK_INIT;
std::atomic<bool> g_on{false};
std::atomic<uint32_t> g_builds{0}, g_bound{0}, g_binds10{0};
struct Retired { ID3D12PipelineState* p; ULONGLONG at; };
std::vector<Retired> g_retired;   // the present's: variants no longer bound, let go a few seconds on

std::vector<uint8_t> Copy(const D3D12_SHADER_BYTECODE& b) {
    return b.pShaderBytecode && b.BytecodeLength ? std::vector<uint8_t>((const uint8_t*)b.pShaderBytecode, (const uint8_t*)b.pShaderBytecode + b.BytecodeLength)
                                                 : std::vector<uint8_t>();
}
D3D12_SHADER_BYTECODE Code(const std::vector<uint8_t>& v) { return D3D12_SHADER_BYTECODE{v.empty() ? nullptr : v.data(), v.size()}; }
using psostream::IsShader;
using psostream::Subobject;

void CopyLayout(Entry& e, const D3D12_INPUT_LAYOUT_DESC& l) {
    e.il.assign(l.pInputElementDescs, l.pInputElementDescs + l.NumElements);
    e.names.clear();
    e.names.reserve(e.il.size());
    for (auto& el : e.il) e.names.push_back(el.SemanticName ? el.SemanticName : "");
    for (size_t i = 0; i < e.il.size(); ++i) e.il[i].SemanticName = e.names[i].c_str();
}
void Hold(Entry& e, ID3D12Device* dev, ID3D12PipelineState* pso) {
    dev->AddRef();
    e.dev = dev;
    e.game = pso;
    g_n.store(g_n.load() + 1, std::memory_order_release);
}
void Told(int n, const char* how) {
    char b[200];
    snprintf(b, sizeof b, "ao vr: the game's ambient occlusion pipeline %d found (%s; Intel's radius and near limit in it) - given a radius for "
             "eyes near the ground when on ([render] ao_vr, first person)", n, how);
    Say(b);
}

// ours for `radius` (the present's thread); false when it could not be made
bool Build(Entry& e, float radius) {
    std::string why;
    const std::vector<uint8_t>& ps = e.codes[0];
    const std::vector<uint8_t> patched = dxbcao::Patch(ps.data(), ps.size(), radius, &why);
    if (patched.empty()) {
        if (!e.failed) {
            char b[260];
            snprintf(b, sizeof b, "ao vr: the game's ambient occlusion shader could not be given a radius (%s) - its AO stays as it is", why.c_str());
            Say(b);
        }
        e.failed = true;
        return false;
    }
    ID3D12PipelineState* p = nullptr;
    HRESULT hr = E_FAIL;
    natived3d::SetOurs(true);   // (our own pipeline: not told to ourselves)
    if (e.stream) {
        std::vector<uint8_t> s = e.bytes;
        const D3D12_SHADER_BYTECODE code{patched.data(), patched.size()};
        std::memcpy(&s[e.psAt], &code, sizeof code);
        ID3D12Device2* d2 = nullptr;
        if (SUCCEEDED(e.dev->QueryInterface(__uuidof(ID3D12Device2), (void**)&d2)) && d2) {
            const D3D12_PIPELINE_STATE_STREAM_DESC sd{s.size(), s.data()};
            hr = d2->CreatePipelineState(&sd, __uuidof(ID3D12PipelineState), (void**)&p);
            d2->Release();
        }
    } else {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d = e.desc;
        d.PS = D3D12_SHADER_BYTECODE{patched.data(), patched.size()};
        hr = e.dev->CreateGraphicsPipelineState(&d, __uuidof(ID3D12PipelineState), (void**)&p);
    }
    natived3d::SetOurs(false);
    if (FAILED(hr) || !p) {
        if (!e.failed) {
            char b[200];
            snprintf(b, sizeof b, "ao vr: the device refused the AO pipeline with our radius (0x%08X) - the game's AO stays as it is", (unsigned)hr);
            Say(b);
        }
        e.failed = true;
        return false;
    }
    ID3D12PipelineState* old = e.ours.exchange(p);
    if (old) g_retired.push_back({old, GetTickCount64()});
    e.radius = radius;
    g_builds.fetch_add(1, std::memory_order_relaxed);
    return true;
}

}  // namespace

void SetLog(LogFn log) { g_log = log; }

void OnGraphicsPso(ID3D12Device* dev, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d, ID3D12PipelineState* pso) {
    if (!d || !pso || !d->PS.pShaderBytecode || d->StreamOutput.NumEntries) return;
    dxbcao::Found f;
    if (!dxbcao::Find((const uint8_t*)d->PS.pShaderBytecode, d->PS.BytecodeLength, &f)) return;   // not the AO's generate pass
    AcquireSRWLockExclusive(&g_lock);
    Entry* e = g_n.load() < kMax ? &g_e[g_n.load()] : nullptr;
    if (!e) { ReleaseSRWLockExclusive(&g_lock); return; }
    e->stream = false;
    e->desc = *d;
    e->codes = {Copy(d->PS), Copy(d->VS), Copy(d->DS), Copy(d->HS), Copy(d->GS)};
    e->desc.PS = Code(e->codes[0]); e->desc.VS = Code(e->codes[1]); e->desc.DS = Code(e->codes[2]); e->desc.HS = Code(e->codes[3]); e->desc.GS = Code(e->codes[4]);
    CopyLayout(*e, d->InputLayout);
    e->desc.InputLayout = D3D12_INPUT_LAYOUT_DESC{e->il.empty() ? nullptr : e->il.data(), (UINT)e->il.size()};
    e->desc.CachedPSO = D3D12_CACHED_PIPELINE_STATE{};
    if (e->desc.pRootSignature) e->desc.pRootSignature->AddRef();
    Hold(*e, dev, pso);
    const int n = g_n.load();
    ReleaseSRWLockExclusive(&g_lock);
    Told(n, "a graphics description");
}

void OnStreamPso(ID3D12Device* dev, const D3D12_PIPELINE_STATE_STREAM_DESC* d, ID3D12PipelineState* pso) {
    if (!d || !pso || !d->pPipelineStateSubobjectStream || !d->SizeInBytes || d->SizeInBytes > 65536) return;
    const uint8_t* s = (const uint8_t*)d->pPipelineStateSubobjectStream;
    const size_t n = d->SizeInBytes;
    size_t psAt = 0;
    const D3D12_SHADER_BYTECODE* ps = nullptr;
    for (size_t at = 0; at + 4 <= n;) {
        uint32_t type = 0;
        std::memcpy(&type, s + at, 4);
        size_t vo = 0, size = 0;
        if (!Subobject(type, &vo, &size) || at + size > n) return;
        if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS) { psAt = at + vo; ps = (const D3D12_SHADER_BYTECODE*)(s + at + vo); }
        at += size;
    }
    if (!ps || !ps->pShaderBytecode) return;
    dxbcao::Found f;
    if (!dxbcao::Find((const uint8_t*)ps->pShaderBytecode, ps->BytecodeLength, &f)) return;   // not the AO's generate pass
    AcquireSRWLockExclusive(&g_lock);
    Entry* e = g_n.load() < kMax ? &g_e[g_n.load()] : nullptr;
    if (!e) { ReleaseSRWLockExclusive(&g_lock); return; }
    e->stream = true;
    e->bytes.assign(s, s + n);
    e->psAt = psAt;
    e->codes.clear();
    e->codes.push_back(Copy(*ps));
    e->codes.reserve(16);   // (the copies stay where they are: the stream points into them)
    bool ok = true;
    for (size_t at = 0; at + 4 <= n && ok;) {
        uint32_t type = 0;
        std::memcpy(&type, &e->bytes[at], 4);
        size_t vo = 0, size = 0;
        Subobject(type, &vo, &size);
        uint8_t* v = &e->bytes[at + vo];
        if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS) {
            const D3D12_SHADER_BYTECODE c = Code(e->codes[0]);
            std::memcpy(v, &c, sizeof c);
        } else if (IsShader(type)) {
            D3D12_SHADER_BYTECODE c;
            std::memcpy(&c, v, sizeof c);
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
            std::memcpy(v, &none, sizeof none);   // (the game's cache is of its own shader: not ours)
        } else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT) {
            D3D12_STREAM_OUTPUT_DESC so;
            std::memcpy(&so, v, sizeof so);
            if (so.NumEntries) ok = false;
        } else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING) {
            D3D12_VIEW_INSTANCING_DESC vi;
            std::memcpy(&vi, v, sizeof vi);
            if (vi.ViewInstanceCount) ok = false;
        }
        at += size;
    }
    if (!ok) { ReleaseSRWLockExclusive(&g_lock); return; }
    Hold(*e, dev, pso);
    const int count = g_n.load();
    ReleaseSRWLockExclusive(&g_lock);
    Told(count, "a pipeline stream");
}

ID3D12PipelineState* Swap(ID3D12PipelineState* pso) {
    if (!g_on.load(std::memory_order_relaxed)) return pso;
    const int n = g_n.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
        if (g_e[i].game == pso) {
            ID3D12PipelineState* o = g_e[i].ours.load(std::memory_order_acquire);
            if (!o) return pso;
            g_bound.fetch_add(1, std::memory_order_relaxed);
            g_binds10.fetch_add(1, std::memory_order_relaxed);
            return o;
        }
    return pso;
}

void Frame(bool on, float radiusUnits, float metres) {
    const int n = g_n.load(std::memory_order_acquire);
    const ULONGLONG now = GetTickCount64();
    static ULONGLONG builtAt = 0;
    static float toldRadius = -1.0f;
    bool ready = n > 0;
    if (on && radiusUnits > 1e-3f && std::isfinite(radiusUnits))
        for (int i = 0; i < n; ++i) {
            Entry& e = g_e[i];
            if (e.failed) { ready = false; continue; }
            const bool none = !e.ours.load();
            const bool moved = std::fabs(radiusUnits - e.radius) > 0.05f * radiusUnits;
            if (none || (moved && now - builtAt >= 1000)) {
                if (Build(e, radiusUnits)) builtAt = now;
            }
            ready = ready && e.ours.load() != nullptr;
        }
    else ready = false;
    const bool was = g_on.exchange(on && ready);
    if (on && ready && std::fabs(radiusUnits - toldRadius) > 0.05f * radiusUnits) {
        toldRadius = radiusUnits;
        char b[260];
        snprintf(b, sizeof b, "ao vr: the game's ambient occlusion with a radius of %.2f m (%.2f world units) and its near limit 1.2 times that "
                 "in %d pipeline(s) - its strength, fade and blur its own ([render] ao_vr_radius_m, first person)", metres, radiusUnits, n);
        Say(b);
    }
    if (was && !(on && ready)) { Say("ao vr: off - the game's own ambient occlusion"); toldRadius = -1.0f; }
    size_t keep = 0;
    for (const Retired& r : g_retired) {
        if (now - r.at > 5000) r.p->Release();
        else g_retired[keep++] = r;
    }
    g_retired.resize(keep);
    static ULONGLONG toldAt = 0;
    if (g_on.load() && now - toldAt > 10000) {
        toldAt = now;
        char b[160];
        snprintf(b, sizeof b, "ao vr, 10 s: %u binds of the AO with our radius (%d of the game's pipelines kept, %u variants made in all)",
                 g_binds10.exchange(0), n, g_builds.load());
        Say(b);
    }
}

int Kept() { return g_n.load(); }
uint32_t Builds() { return g_builds.load(); }
uint32_t Bound() { return g_bound.load(); }

}  // namespace aovr
