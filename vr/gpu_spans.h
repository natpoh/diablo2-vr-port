// gpu_spans.h - named spans of the graphics card's time (timestamp queries) on the present's thread, for the 10 s lines: what each part of
// the eye made (vr/turn_warp.cpp, vr/xr.cpp) costs a frame - the weak PC's budget (docs/plan_no_reshade.md: a GTX 1050 Ti at 72 frames/s).
//
//   gpuspans::Setup(dev, queue)            once (the queue's timestamp frequency)
//   gpuspans::FrameStart()                 once a frame, before its first span: the slot of kSlots frames ago read (long done on the GPU)
//   int s = gpuspans::Begin(list, "name")  ... gpuspans::End(list, s)   - on any list of that queue, nested or not
//   gpuspans::Resolve(list)                on the frame's last list: the stamps into the read-back buffer
//   gpuspans::Take(&frames)                the averages since the last call, "name 0.42, other 0.10 ..." (ms a frame, dearest first)
// Nothing is recorded before Setup, or when the frame's spans run out. The names are kept as pointers: string literals.
#pragma once

#include <d3d12.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace gpuspans {

constexpr unsigned kSlots = 8;     // frames in flight before a slot is read again
constexpr unsigned kSpans = 48;    // spans a frame

struct State {
    ID3D12QueryHeap* heap = nullptr;
    ID3D12Resource* read = nullptr;
    const uint64_t* map = nullptr;
    double msPerTick = 0.0;
    unsigned frame = 0;
    struct Slot {
        const char* names[kSpans] = {};
        unsigned n = 0;
        bool resolved = false;
    } slots[kSlots];
    struct Sum { const char* name; double ms; };
    std::vector<Sum> sums;
    unsigned frames = 0;
};
inline State g;

inline bool Setup(ID3D12Device* dev, ID3D12CommandQueue* q) {
    if (g.heap) return true;
    UINT64 hz = 0;
    if (!dev || !q || FAILED(q->GetTimestampFrequency(&hz)) || !hz) return false;
    D3D12_QUERY_HEAP_DESC qd{};
    qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qd.Count = kSlots * kSpans * 2;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = (UINT64)qd.Count * 8;
    rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(dev->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void**)&g.heap)) ||
        FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), (void**)&g.read)) ||
        FAILED(g.read->Map(0, nullptr, (void**)&g.map))) {
        if (g.heap) g.heap->Release();
        if (g.read) g.read->Release();
        g.heap = nullptr;
        g.read = nullptr;
        g.map = nullptr;
        return false;
    }
    g.msPerTick = 1000.0 / (double)hz;
    return true;
}

inline void FrameStart() {
    if (!g.heap) return;
    ++g.frame;
    State::Slot& s = g.slots[g.frame % kSlots];
    if (s.resolved && s.n) {
        const uint64_t* v = g.map + (size_t)(g.frame % kSlots) * kSpans * 2;
        for (unsigned i = 0; i < s.n; ++i) {
            if (!s.names[i] || v[2 * i + 1] < v[2 * i]) continue;   // (a span not ended, or not run)
            const double ms = (double)(v[2 * i + 1] - v[2 * i]) * g.msPerTick;
            if (ms > 1000.0) continue;
            auto it = std::find_if(g.sums.begin(), g.sums.end(), [&](const State::Sum& x) { return x.name == s.names[i] || strcmp(x.name, s.names[i]) == 0; });
            if (it == g.sums.end()) g.sums.push_back({s.names[i], ms});
            else it->ms += ms;
        }
        ++g.frames;
    }
    s = State::Slot{};
}

inline int Begin(ID3D12GraphicsCommandList* l, const char* name) {
    if (!g.heap || !l) return -1;
    State::Slot& s = g.slots[g.frame % kSlots];
    if (s.n >= kSpans) return -1;
    const unsigned i = s.n++;
    s.names[i] = name;
    l->EndQuery(g.heap, D3D12_QUERY_TYPE_TIMESTAMP, (g.frame % kSlots) * kSpans * 2 + 2 * i);
    return (int)i;
}
inline void End(ID3D12GraphicsCommandList* l, int span) {
    if (!g.heap || !l || span < 0) return;
    l->EndQuery(g.heap, D3D12_QUERY_TYPE_TIMESTAMP, (g.frame % kSlots) * kSpans * 2 + 2 * (unsigned)span + 1);
}
inline void Resolve(ID3D12GraphicsCommandList* l) {
    if (!g.heap || !l) return;
    State::Slot& s = g.slots[g.frame % kSlots];
    if (!s.n || s.resolved) return;
    const UINT base = (g.frame % kSlots) * kSpans * 2;
    l->ResolveQueryData(g.heap, D3D12_QUERY_TYPE_TIMESTAMP, base, s.n * 2, g.read, (UINT64)base * 8);
    s.resolved = true;
}

// "name 0.42, other 0.10" - ms a frame over the frames read since the last call, dearest first; *frames how many
inline std::string Take(unsigned* frames) {
    std::vector<State::Sum> v = g.sums;
    const unsigned n = g.frames;
    g.sums.clear();
    g.frames = 0;
    if (frames) *frames = n;
    if (!n) return std::string();
    std::sort(v.begin(), v.end(), [](const State::Sum& a, const State::Sum& b) { return a.ms > b.ms; });
    std::string out;
    char b[96];
    for (const State::Sum& x : v) {
        snprintf(b, sizeof b, "%s%s %.2f", out.empty() ? "" : ", ", x.name, x.ms / n);
        out += b;
    }
    return out;
}

}  // namespace gpuspans
