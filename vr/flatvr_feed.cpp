// flatvr_feed.cpp - [render] flatvr_feed: the game's picture and depth handed to FlatVR without ReShade (vr/flatvr_feed.h says what and why).
// Plain D3D12 and Win32 so tools/flatvr_feed_test.cpp runs this very code on WARP and reads the blocks back as FlatVR does.

#include "flatvr_feed.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#include <d3d12.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#include "afr_eye_shared.h"
#include "native_d3d12.h"

#pragma comment(lib, "advapi32.lib")

namespace flatvrfeed {
namespace {

void (*g_log)(const char*) = nullptr;
void Say(const char* fmt, ...) {
    if (!g_log) return;
    char b[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    g_log(b);
}

// ---- the blocks: made once, owner-only (SYSTEM and this user, as FlatVR_DepthProvider and FlatVR's bridge make theirs) ----
template <class T>
struct Block {
    HANDLE map = nullptr;
    T* p = nullptr;
    bool tried = false;
};
Block<SharedTexture> g_colour, g_depth;
Block<ShareStatus> g_status;
Block<AfrHalves> g_halves;

bool OwnerOnly(SECURITY_ATTRIBUTES& sa, PSECURITY_DESCRIPTOR& sd) {
    sd = nullptr;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    DWORD len = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &len);
    std::vector<unsigned char> buf(len ? len : 1);
    const bool got = len && GetTokenInformation(token, TokenUser, buf.data(), len, &len);
    CloseHandle(token);
    if (!got) return false;
    LPSTR sid = nullptr;
    if (!ConvertSidToStringSidA(((TOKEN_USER*)buf.data())->User.Sid, &sid)) return false;
    char sddl[256];
    snprintf(sddl, sizeof sddl, "D:P(A;;GA;;;SY)(A;;GA;;;%s)", sid);
    LocalFree(sid);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(sddl, SDDL_REVISION_1, &sd, nullptr)) return false;
    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle = FALSE;
    return true;
}

template <class T>
T* Make(Block<T>& b, const char* name) {
    if (b.p || b.tried) return b.p;
    b.tried = true;
    SECURITY_ATTRIBUTES sa{};
    PSECURITY_DESCRIPTOR sd = nullptr;
    const bool own = OwnerOnly(sa, sd);
    b.map = CreateFileMappingA(INVALID_HANDLE_VALUE, own ? &sa : nullptr, PAGE_READWRITE, 0, (DWORD)sizeof(T), name);
    const DWORD err = GetLastError();
    if (sd) LocalFree(sd);
    if (!b.map) { Say("vrcam: flatvr feed - %s could not be made (error %lu): FlatVR gets no picture from the plugin", name, err); return nullptr; }
    b.p = (T*)MapViewOfFile(b.map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(T));
    if (!b.p) { Say("vrcam: flatvr feed - %s could not be mapped", name); return nullptr; }
    memset(b.p, 0, sizeof(T));   // ours from here on, whatever an earlier publisher left (FlatVR trusts its pid to duplicate the handle from)
    Say("vrcam: flatvr feed - publishing on %s%s", name, err == ERROR_ALREADY_EXISTS ? " (it was there already: taken over)" : "");
    return b.p;
}

// ---- the shared textures ----
struct Share {
    ID3D12Resource* tex = nullptr;
    HANDLE handle = nullptr;
    uint32_t w = 0, h = 0;
    DXGI_FORMAT f = DXGI_FORMAT_UNKNOWN;
};
struct Retired {
    ID3D12Resource* tex;
    HANDLE handle;
    uint32_t at;
};
std::vector<Retired> g_retired;
uint32_t g_frame = 0;
Share g_colourTex, g_depthTex;
ID3D12Resource* g_pair = nullptr;   // the pair put together (not shared): only whole pairs go to the shared texture
uint32_t g_pairW = 0, g_pairH = 0;
DXGI_FORMAT g_pairF = DXGI_FORMAT_UNKNOWN;
D3D12_RESOURCE_STATES g_pairState = D3D12_RESOURCE_STATE_COPY_DEST;
ID3D12Device* g_dev = nullptr;
EyeRule g_rule;
PairFill g_fill;
Stats g_stats;
bool g_failColour = false, g_failDepth = false;   // said once

// A texture of an old size: its handle cleared first (the block's reader then waits), the texture and handle released kRetireFrames later
void Retire(Share& s) {
    if (s.tex || s.handle) g_retired.push_back({s.tex, s.handle, g_frame});
    s = Share{};
}
void Drain(bool all) {
    for (size_t i = 0; i < g_retired.size();) {
        if (all || g_frame - g_retired[i].at >= kRetireFrames) {
            if (g_retired[i].tex) g_retired[i].tex->Release();
            if (g_retired[i].handle) CloseHandle(g_retired[i].handle);
            g_retired[i] = g_retired.back();
            g_retired.pop_back();
        } else {
            ++i;
        }
    }
}

DXGI_FORMAT ColourTypeless(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
        case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_TYPELESS;
        case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_TYPELESS;
        case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_TYPELESS;
        default: return f;
    }
}

// Shared: a default heap marked shared, simultaneous access (read by FlatVR's device while the game's queue writes), in COMMON; an NT handle
bool MakeShare(Share& s, ID3D12Device* dev, uint32_t w, uint32_t h, DXGI_FORMAT f, const wchar_t* name) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w;
    d.Height = h;
    d.DepthOrArraySize = d.MipLevels = 1;
    d.Format = f;
    d.SampleDesc.Count = 1;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    ID3D12Resource* t = nullptr;
    HRESULT hr = dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &d, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), (void**)&t);
    if (FAILED(hr) || !t) {
        Say("vrcam: flatvr feed - the shared texture %ux%u (format %u) could not be made: 0x%08lX", w, h, (unsigned)f, (unsigned long)hr);
        return false;
    }
    HANDLE hd = nullptr;
    hr = dev->CreateSharedHandle(t, nullptr, GENERIC_ALL, nullptr, &hd);
    if (FAILED(hr) || !hd) {
        Say("vrcam: flatvr feed - CreateSharedHandle failed: 0x%08lX", (unsigned long)hr);
        t->Release();
        return false;
    }
    t->SetName(name);
    s.tex = t;
    s.handle = hd;
    s.w = w;
    s.h = h;
    s.f = f;
    return true;
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    return b;
}
void Copy(ID3D12GraphicsCommandList* l, ID3D12Resource* dst, uint32_t x, ID3D12Resource* src) {
    D3D12_TEXTURE_COPY_LOCATION d{}, s{};
    d.pResource = dst;
    d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    s.pResource = src;
    s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    l->CopyTextureRegion(&d, x, 0, 0, &s, nullptr);
}

// A new texture in a block: the handle cleared first, then the fields, the handle last (a reader copying the block meanwhile sees
// "nothing yet" rather than a handle with another texture's size); the counter goes with each picture
void Name(SharedTexture* b, const Share& s, uint32_t format) {
    b->nt_handle = 0;
    MemoryBarrier();
    b->width = s.w;
    b->height = s.h;
    b->format = format;
    b->source_pid = GetCurrentProcessId();
    MemoryBarrier();
    b->nt_handle = (uint64_t)(uintptr_t)s.handle;
}

void Halves(const FlatVRAfrEye* e, int eye, bool pair) {
    if (!e || !g_rule.alive || e->stamp_magic != FLATVR_AFR_STAMP_MAGIC) return;
    AfrHalves* h = Make(g_halves, kHalvesName);   // (made only once a mod publishes eyes, as the add-on does)
    if (!h) return;
    h->version = 1;
    if (pair) {
        h->stamp[0] = e->stamp[0];
        h->stamp[1] = e->stamp[1];
    } else {
        h->stamp[0] = h->stamp[1] = e->stamp[eye & 1];
    }
    h->valid = 3u;
    h->frame_counter = g_frame;
}

void DropAll() {
    Retire(g_colourTex);
    Retire(g_depthTex);
    if (g_pair) { g_pair->Release(); g_pair = nullptr; }
    g_pairW = g_pairH = 0;
    Drain(true);
}

}  // namespace

void SetLog(void (*log)(const char*)) { g_log = log; }

void Present(ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12Resource* bb, const FlatVRAfrEye* eyeBlock, ID3D12Resource* depth,
             unsigned depthState, bool depthFresh) {
    if (!dev || !q || !bb) return;
    if (g_dev && g_dev != dev) {   // the game made its device again: what was made on the old one goes
        if (g_colour.p) g_colour.p->nt_handle = 0;
        if (g_depth.p) g_depth.p->nt_handle = 0;
        DropAll();
    }
    g_dev = dev;
    if (ShareStatus* st = Make(g_status, kStatusName)) {
        st->device_api = kApiD3D12;
        st->share_kind = kShareNtHandle;
        st->source_pid = GetCurrentProcessId();
        st->struct_bytes = sizeof(ShareStatus);   // (last: a reader trusts the fields that fit)
    }
    SharedTexture* col = Make(g_colour, kColourName);
    SharedTexture* dep = Make(g_depth, kDepthName);
    if (!col) return;
    ++g_frame;
    ++g_stats.presents;
    Drain(false);

    const int eye = eyeBlock ? g_rule.Eye(eyeBlock->version, eyeBlock->enabled, eyeBlock->eye, eyeBlock->frame, eyeBlock->swap, GetTickCount64()) : -1;
    if (eye < 0) g_fill.mask = 0;
    const D3D12_RESOURCE_DESC bd = bb->GetDesc();
    if (bd.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || !bd.Width || !bd.Height || bd.SampleDesc.Count > 1) return;
    const uint32_t w = (uint32_t)bd.Width, h = bd.Height, sw = eye >= 0 ? 2 * w : w;
    const DXGI_FORMAT tl = ColourTypeless(bd.Format);

    if (!g_colourTex.tex || g_colourTex.w != sw || g_colourTex.h != h || g_colourTex.f != tl) {
        col->nt_handle = 0;
        Retire(g_colourTex);
        if (!MakeShare(g_colourTex, dev, sw, h, tl, L"d2rvr flatvr feed picture")) {
            if (!g_failColour) { g_failColour = true; Say("vrcam: flatvr feed - no picture for FlatVR (the shared texture)"); }
            return;
        }
        Name(col, g_colourTex, (uint32_t)bd.Format);
        g_stats.colour = true;
        Say("vrcam: flatvr feed - FlatVR's picture from the plugin (no ReShade): %ux%u%s, format %u, through handle %p", sw, h,
            eye >= 0 ? " (a stereo pair, eyes side by side)" : "", (unsigned)bd.Format, g_colourTex.handle);
    }
    if (eye >= 0 && (!g_pair || g_pairW != sw || g_pairH != h || g_pairF != tl)) {
        if (g_pair) g_retired.push_back({g_pair, nullptr, g_frame});   // (the GPU may still be reading it)
        g_pair = nullptr;
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = sw;
        d.Height = h;
        d.DepthOrArraySize = d.MipLevels = 1;
        d.Format = tl;
        d.SampleDesc.Count = 1;
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        if (SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource),
                                                   (void**)&g_pair))) {
            g_pair->SetName(L"d2rvr flatvr feed pair");
            g_pairW = sw;
            g_pairH = h;
            g_pairF = tl;
            g_pairState = D3D12_RESOURCE_STATE_COPY_DEST;
            g_fill.mask = 0;
        } else {
            g_pair = nullptr;
        }
    }
    // the depth: our copy of the scene's (scenedepth), when it was made for this present
    bool depthNow = false;
    D3D12_RESOURCE_DESC dd{};
    if (dep && depth && depthFresh) {
        dd = depth->GetDesc();
        if (!g_depthTex.tex || g_depthTex.w != dd.Width || g_depthTex.h != dd.Height || g_depthTex.f != dd.Format) {
            dep->nt_handle = 0;
            Retire(g_depthTex);
            if (MakeShare(g_depthTex, dev, (uint32_t)dd.Width, dd.Height, dd.Format, L"d2rvr flatvr feed depth")) {
                Name(dep, g_depthTex, (uint32_t)dd.Format);
                g_stats.depthOk = true;
                Say("vrcam: flatvr feed - FlatVR's depth from the plugin: %llux%u, format %u", (unsigned long long)dd.Width, dd.Height, (unsigned)dd.Format);
            } else if (!g_failDepth) {
                g_failDepth = true;
                Say("vrcam: flatvr feed - no depth for FlatVR (the shared texture): its 3D from the depth has none");
            }
        }
        depthNow = g_depthTex.tex != nullptr;
    }

    ID3D12GraphicsCommandList* l = natived3d::OpenList(dev);
    if (!l) return;   // (our list was not free: this present goes without - FlatVR keeps the last)
    bool publish = false;
    {
        D3D12_RESOURCE_BARRIER b = Transition(bb, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
        l->ResourceBarrier(1, &b);
    }
    if (eye >= 0 && g_pair) {
        if (g_pairState != D3D12_RESOURCE_STATE_COPY_DEST) {
            D3D12_RESOURCE_BARRIER b = Transition(g_pair, g_pairState, D3D12_RESOURCE_STATE_COPY_DEST);
            l->ResourceBarrier(1, &b);
        }
        Copy(l, g_pair, (uint32_t)eye * w, bb);
        D3D12_RESOURCE_BARRIER b = Transition(g_pair, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        l->ResourceBarrier(1, &b);
        g_pairState = D3D12_RESOURCE_STATE_COPY_SOURCE;
        const uint32_t before = g_fill.mask;
        if (g_fill.Add(eye)) {
            D3D12_RESOURCE_BARRIER in = Transition(g_colourTex.tex, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
            l->ResourceBarrier(1, &in);
            l->CopyResource(g_colourTex.tex, g_pair);
            D3D12_RESOURCE_BARRIER out = Transition(g_colourTex.tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
            l->ResourceBarrier(1, &out);
            publish = true;
            ++g_stats.pairs;
        } else if (before & (1u << eye)) {
            ++g_stats.halfLost;   // the same eye twice: that pair started again
        }
    } else if (eye < 0) {
        D3D12_RESOURCE_BARRIER in = Transition(g_colourTex.tex, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        l->ResourceBarrier(1, &in);
        Copy(l, g_colourTex.tex, 0, bb);
        D3D12_RESOURCE_BARRIER out = Transition(g_colourTex.tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        l->ResourceBarrier(1, &out);
        publish = true;
        ++g_stats.singles;
    }
    {
        D3D12_RESOURCE_BARRIER b = Transition(bb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
        l->ResourceBarrier(1, &b);
    }
    if (depthNow) {
        const D3D12_RESOURCE_STATES ds = (D3D12_RESOURCE_STATES)depthState;
        D3D12_RESOURCE_BARRIER in[2] = {Transition(depth, ds, D3D12_RESOURCE_STATE_COPY_SOURCE),
                                        Transition(g_depthTex.tex, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST)};
        l->ResourceBarrier(2, in);
        l->CopyResource(g_depthTex.tex, depth);
        D3D12_RESOURCE_BARRIER out[2] = {Transition(depth, D3D12_RESOURCE_STATE_COPY_SOURCE, ds),
                                         Transition(g_depthTex.tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON)};
        l->ResourceBarrier(2, out);
        ++g_stats.depths;
    }
    natived3d::Submit(q);

    // the counters, with the copies on the queue (as the add-on: a reader may meet the texture a frame early, never another size's)
    if (publish) {
        col->frame_counter = g_frame;
        Halves(eyeBlock, eye, eye >= 0);
    }
    if (depthNow) dep->frame_counter = g_frame;
}

// (each present while the feed does not run: the textures retired once, freed kRetireFrames presents later; a switch back makes them again)
void Idle() {
    if (g_colour.p) g_colour.p->nt_handle = 0;
    if (g_depth.p) g_depth.p->nt_handle = 0;
    g_fill.mask = 0;
    if (g_colourTex.tex) Retire(g_colourTex);
    if (g_depthTex.tex) Retire(g_depthTex);
    if (g_pair) { g_retired.push_back({g_pair, nullptr, g_frame}); g_pair = nullptr; g_pairW = g_pairH = 0; }
    if (!g_retired.empty()) { ++g_frame; Drain(false); }
}

Stats TakeStats() {
    const Stats s = g_stats;
    g_stats.presents = g_stats.pairs = g_stats.singles = g_stats.depths = g_stats.halfLost = 0;
    return s;
}

}  // namespace flatvrfeed
