// fog_height.cpp - see fog_height.h.

#include "fog_height.h"

#include <windows.h>
#include <d3d12.h>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "dxbc_fog_height.h"
#include "native_d3d12.h"
#include "pso_stream.h"

namespace fogheight {
namespace {

LogFn g_log = nullptr;
void Say(const char* s) { if (g_log) g_log(s); }

// The game's fog pass's pipelines (one per permutation it makes): its description kept - the graphics description, or (as the game makes
// them, PC1 2026-10-27: "the game made a pipeline from a stream") its subobject stream - with the shaders and the input layout copied and
// the root signature held; our variant for the current setting.
struct Entry {
    ID3D12PipelineState* game = nullptr;
    std::atomic<ID3D12PipelineState*> ours{nullptr};
    ID3D12Device* dev = nullptr;
    bool stream = false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};   // (a graphics description)
    std::vector<uint8_t> bytes;                  // (a stream: its bytes, the pointers in it into the copies below)
    size_t psAt = 0;                             // (a stream: where its pixel shader's D3D12_SHADER_BYTECODE is)
    std::vector<std::vector<uint8_t>> codes;     // the shaders, copied (codes[0] the pixel shader)
    std::vector<D3D12_INPUT_ELEMENT_DESC> il;
    std::vector<std::string> names;
    float top = NAN, soft = NAN;   // what `ours` was made for
    bool mist = false, voidAlone = false, noFog = false;
    dxbcfog::Curve curve{};
    bool failed = false;
};
constexpr int kMax = 32;
Entry g_e[kMax];
std::atomic<int> g_n{0};
SRWLOCK g_lock = SRWLOCK_INIT;   // (the adding of entries)
std::atomic<bool> g_on{false};
std::atomic<uint32_t> g_swaps{0}, g_streams{0}, g_streamsUnread{0};
std::atomic<uint32_t> g_bound{0};   // our variant bound in the game's place, in all (never reset: Bound)
struct Retired { ID3D12PipelineState* p; ULONGLONG at; };
std::vector<Retired> g_retired;   // the present's: variants no longer bound, let go a few seconds on

std::vector<uint8_t> Copy(const D3D12_SHADER_BYTECODE& b) {
    return b.pShaderBytecode && b.BytecodeLength ? std::vector<uint8_t>((const uint8_t*)b.pShaderBytecode, (const uint8_t*)b.pShaderBytecode + b.BytecodeLength)
                                                 : std::vector<uint8_t>();
}
D3D12_SHADER_BYTECODE Code(const std::vector<uint8_t>& v) { return D3D12_SHADER_BYTECODE{v.empty() ? nullptr : v.data(), v.size()}; }

// a pipeline state stream's subobjects (vr/pso_stream.h)
using psostream::IsShader;
using psostream::Subobject;

void Told(int n, const dxbcfog::Found& f, const char* how) {
    char b[220];
    snprintf(b, sizeof b, "fog height: the game's fog pass's pipeline %d found (%s; its distance fog at r%u, the view's place at r%u) - thinned with "
             "height when on", n, how, f.t, f.view);
    Say(b);
}

// ours for the setting (on the present's thread); false when the device would not make it
bool Build(Entry& e, float top, float soft, bool mist, bool voidAlone, bool noFog, const dxbcfog::Curve& curve) {
    std::string why;
    const std::vector<uint8_t>& ps = e.codes[0];
    const std::vector<uint8_t> patched = dxbcfog::Patch(ps.data(), ps.size(), top, soft, &why, mist, voidAlone, noFog, &curve);
    if (patched.empty()) {
        if (!e.failed) {
            char b[300];
            snprintf(b, sizeof b, "fog height: the game's fog shader could not be patched (%s) - its fog stays as it is", why.c_str());
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
            snprintf(b, sizeof b, "fog height: the patched fog pipeline was refused by the device (0x%08X) - the game's fog stays as it is", (unsigned)hr);
            Say(b);
        }
        e.failed = true;
        return false;
    }
    ID3D12PipelineState* old = e.ours.exchange(p);
    if (old) g_retired.push_back({old, GetTickCount64()});
    e.top = top;
    e.soft = soft;
    e.mist = mist;
    e.voidAlone = voidAlone;
    e.noFog = noFog;
    e.curve = curve;
    return true;
}

// a new entry under g_lock (null when full)
Entry* Take() {
    const int n = g_n.load();
    return n < kMax ? &g_e[n] : nullptr;
}
void Hold(Entry& e, ID3D12Device* dev, ID3D12PipelineState* pso) {
    dev->AddRef();
    e.dev = dev;
    e.game = pso;
    g_n.store(g_n.load() + 1, std::memory_order_release);
}
void CopyLayout(Entry& e, const D3D12_INPUT_LAYOUT_DESC& l) {
    e.il.assign(l.pInputElementDescs, l.pInputElementDescs + l.NumElements);
    e.names.clear();
    e.names.reserve(e.il.size());
    for (auto& el : e.il) e.names.push_back(el.SemanticName ? el.SemanticName : "");
    for (size_t i = 0; i < e.il.size(); ++i) e.il[i].SemanticName = e.names[i].c_str();
}

}  // namespace

void SetLog(LogFn log) { g_log = log; }

void OnGraphicsPso(ID3D12Device* dev, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d, ID3D12PipelineState* pso) {
    if (!d || !pso || !d->PS.pShaderBytecode || d->StreamOutput.NumEntries) return;
    dxbcfog::Found f;
    if (!dxbcfog::Find((const uint8_t*)d->PS.pShaderBytecode, d->PS.BytecodeLength, &f)) return;   // not the fog pass's
    AcquireSRWLockExclusive(&g_lock);
    Entry* e = Take();
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
    Told(n, f, "a graphics description");
}

void OnStreamPso(ID3D12Device* dev, const D3D12_PIPELINE_STATE_STREAM_DESC* d, ID3D12PipelineState* pso) {
    g_streams.fetch_add(1, std::memory_order_relaxed);
    if (!d || !pso || !d->pPipelineStateSubobjectStream || !d->SizeInBytes || d->SizeInBytes > 65536) return;
    // its pixel shader first (cheap: most of the game's pipelines are not the fog's)
    const uint8_t* s = (const uint8_t*)d->pPipelineStateSubobjectStream;
    const size_t n = d->SizeInBytes;
    size_t psAt = 0;
    const D3D12_SHADER_BYTECODE* ps = nullptr;
    for (size_t at = 0; at + 4 <= n;) {
        uint32_t type = 0;
        std::memcpy(&type, s + at, 4);
        size_t vo = 0, size = 0;
        if (!Subobject(type, &vo, &size) || at + size > n) {
            if (g_streamsUnread.fetch_add(1) == 0) {
                char b[160];
                snprintf(b, sizeof b, "fog height: a pipeline stream of the game's holds a subobject this does not read (type %u) - that pipeline left as it is", type);
                Say(b);
            }
            return;
        }
        if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS) { psAt = at + vo; ps = (const D3D12_SHADER_BYTECODE*)(s + at + vo); }
        at += size;
    }
    if (!ps || !ps->pShaderBytecode) return;
    dxbcfog::Found f;
    if (!dxbcfog::Find((const uint8_t*)ps->pShaderBytecode, ps->BytecodeLength, &f)) return;   // not the fog pass's
    AcquireSRWLockExclusive(&g_lock);
    Entry* e = Take();
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
    Told(count, f, "a pipeline stream");
}

bool StreamPixelShader(const D3D12_PIPELINE_STATE_STREAM_DESC* d, const void** code, size_t* size) {
    if (!d || !d->pPipelineStateSubobjectStream || !d->SizeInBytes || d->SizeInBytes > 65536) return false;
    const uint8_t* s = (const uint8_t*)d->pPipelineStateSubobjectStream;
    for (size_t at = 0; at + 4 <= d->SizeInBytes;) {
        uint32_t type = 0;
        std::memcpy(&type, s + at, 4);
        size_t vo = 0, sz = 0;
        if (!Subobject(type, &vo, &sz) || at + sz > d->SizeInBytes) return false;
        if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS) {
            D3D12_SHADER_BYTECODE b;
            std::memcpy(&b, s + at + vo, sizeof b);
            *code = b.pShaderBytecode;
            *size = b.BytecodeLength;
            return b.pShaderBytecode && b.BytecodeLength;
        }
        at += sz;
    }
    return false;
}

ID3D12PipelineState* Swap(ID3D12PipelineState* pso) {
    if (!g_on.load(std::memory_order_relaxed)) return pso;
    const int n = g_n.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
        if (g_e[i].game == pso) {
            ID3D12PipelineState* o = g_e[i].ours.load(std::memory_order_acquire);
            if (o) { g_swaps.fetch_add(1, std::memory_order_relaxed); g_bound.fetch_add(1, std::memory_order_relaxed); return o; }
            return pso;
        }
    return pso;
}

uint32_t Bound() { return g_bound.load(std::memory_order_relaxed); }

void Frame(bool on, float top, float soft, bool mist, bool voidAlone, bool noFog, const Curve& oc) {
    dxbcfog::Curve curve;
    curve.on = oc.on;
    curve.start = oc.start; curve.end = oc.end; curve.curve = oc.curve; curve.strength = oc.strength; curve.ease = oc.ease;
    const int n = g_n.load(std::memory_order_acquire);
    bool ready = true;
    if (on)
        for (int i = 0; i < n; ++i) {
            Entry& e = g_e[i];
            if (e.failed) continue;
            if (e.top != top || e.soft != soft || e.mist != mist || e.voidAlone != voidAlone || e.noFog != noFog || e.curve != curve) {
                const bool curveChanged = e.curve != curve || std::isnan(e.top);   // (the first variant: which curve, once)
                if (!Build(e, top, soft, mist, voidAlone, noFog, curve)) continue;
                if (i == 0 && curveChanged) {
                    char c[360];
                    if (curve.on)
                        snprintf(c, sizeof c, "fog height: the game's fog by our curve ([fog] game_fog_our_curve): from %.0f, full at %.0f, curve %.2f, "
                                 "strength %.2f - %.0f%% half way%s", curve.start, curve.end, curve.curve, curve.strength,
                                 100.0f * dxbcfog::CurveFog(curve, 0.5f * (curve.start + curve.end)),
                                 curve.ease ? "; eased in over its first 40 units ([fog] ease_in)" : "");
                    else
                        snprintf(c, sizeof c, "fog height: the game's fog by its own exponential curve ([fog] game_fog_our_curve off)");
                    Say(c);
                }
                if (i == 0) {
                    char b[420];
                    const char* v = noFog ? (voidAlone ? "; the void left alone; the distance fog's colour left to the step before the effects"
                                                       : "; the distance fog's colour left to the step before the effects")
                                          : voidAlone ? "; the void left alone (no mist over it: underground)" : "";
                    if (top > 1.0e8f)
                        snprintf(b, sizeof b, "fog height: the game's fog pass, no height thinning%s%s; %d pipeline(s)",
                                 mist ? ", its mist fogged by distance as well" : "", v, n);
                    else
                        snprintf(b, sizeof b, "fog height: the game's fog thinned with height - in full up to %.0f units above the eye, fading out to "
                                 "none at %.0f%s%s; %d pipeline(s)", top - soft, top, mist ? ", its mist fogged by distance as well" : "", v, n);
                    Say(b);
                }
            }
            ready = ready && e.ours.load() != nullptr;
        }
    const bool was = g_on.exchange(on && n > 0 && ready);   // (a variant without a mist in its permutation is made all the same: the height alone)
    if (was && !on) Say("fog height: off - the game's own fog pass as it is");
    // variants no longer bound: let go once the GPU is surely past them
    const ULONGLONG now = GetTickCount64();
    size_t keep = 0;
    for (const Retired& r : g_retired) {
        if (now - r.at > 5000) r.p->Release();
        else g_retired[keep++] = r;
    }
    g_retired.resize(keep);
    static ULONGLONG toldAt = 0;
    if (on && now - toldAt > 10000) {
        toldAt = now;
        char b[260];
        snprintf(b, sizeof b, "fog height, 10 s: %u binds of the thinned fog pass (%d of the game's fog pipelines known; %u made from streams so far%s)",
                 g_swaps.exchange(0), n, g_streams.load(), n ? "" : " - none of the fog's: the fog stays as it is");
        Say(b);
    }
}

}  // namespace fogheight
