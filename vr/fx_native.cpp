// fx_native.cpp - see fx_native.h. The uniform layout rules (an array's elements and a matrix's rows 16 bytes apart, a bool as a 32-bit 0 / 1)
// follow ReShade's runtime (crosire/reshade source/runtime.cpp, set_uniform_value_data / get_uniform_value_data, BSD-3-Clause, Patrick Mours).

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wincodec.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "effect_codegen.hpp"
#include "effect_parser.hpp"
#include "effect_preprocessor.hpp"
#include "fx_native.h"
#include "fx_build_order.h"   // the size in use built first, the sizes built before after it

namespace fxnative {
namespace {

constexpr D3D12_RESOURCE_STATES kRead = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr UINT kRing = 16384;       // shader-visible descriptors: ~2000 a frame for the main technique, up to 4 presents in flight (natived3d's ring)
constexpr UINT kCbSlots = 32;       // uniform buffers in flight (one a Render or RenderTechnique: 3 a present, up to 4 presents in flight)
constexpr int kRetireFrames = 8;    // an old build is let go this many Renders after the new one took over
constexpr UINT kTsSlots = 8;        // Renders timed in flight (read back 8 Renders later: natived3d's ring keeps up to 4 presents)
constexpr UINT kTsPer = 2 + 2 * 64; // timestamps a Render: its start and end, and each pass's (64 passes at most timed)

// A thread with a large stack (16 MB): D3DCompile of the effect's biggest shaders, and the D3D12 debug layer's GPU validation instrumenting them
// when a pipeline state is made, go deeper than std::thread's 1 MB. Joined with WaitForSingleObject, closed by the caller.
HANDLE BigThread(std::function<void()> fn) {
    auto* f = new std::function<void()>(std::move(fn));
    const HANDLE h = CreateThread(nullptr, 16u << 20, [](void* p) -> DWORD {
        std::unique_ptr<std::function<void()>> g((std::function<void()>*)p);
        (*g)();
        return 0;
    }, f, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!h) delete f;
    return h;
}
void Join(HANDLE& h) {
    if (!h) return;
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
    h = nullptr;
}

// D3DCompile and D3D12SerializeRootSignature found at run time: the plugin imports neither DLL (d3dcompiler_47.dll is Windows' own, loaded only
// when an effect is built; d3d12.dll is the game's).
using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
using SerializeFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
D3DCompileFn Compiler() {
    static D3DCompileFn fn = [] {
        HMODULE m = LoadLibraryW(L"d3dcompiler_47.dll");
        return m ? (D3DCompileFn)GetProcAddress(m, "D3DCompile") : nullptr;
    }();
    return fn;
}
SerializeFn Serializer() {
    static SerializeFn fn = [] {
        HMODULE m = GetModuleHandleW(L"d3d12.dll");
        if (!m) m = LoadLibraryW(L"d3d12.dll");
        return m ? (SerializeFn)GetProcAddress(m, "D3D12SerializeRootSignature") : nullptr;
    }();
    return fn;
}

template <class T> void Release(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

uint64_t Fnv(const void* data, size_t n, uint64_t h = 1469598103934665603ull) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

DXGI_FORMAT ResourceFormat(DXGI_FORMAT f) {   // the format a texture is made with: typeless where an sRGB view may be wanted
    switch (f) {
        case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
        case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_TYPELESS;
        default: return f;
    }
}
DXGI_FORMAT Linear(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        default: return f;
    }
}
DXGI_FORMAT Srgb(DXGI_FORMAT f) {
    switch (Linear(f)) {
        case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case DXGI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        default: return Linear(f);
    }
}
D3D12_BLEND Blend(reshadefx::blend_factor f) {
    using bf = reshadefx::blend_factor;
    switch (f) {
        case bf::zero: return D3D12_BLEND_ZERO;
        case bf::one: return D3D12_BLEND_ONE;
        case bf::source_color: return D3D12_BLEND_SRC_COLOR;
        case bf::one_minus_source_color: return D3D12_BLEND_INV_SRC_COLOR;
        case bf::dest_color: return D3D12_BLEND_DEST_COLOR;
        case bf::one_minus_dest_color: return D3D12_BLEND_INV_DEST_COLOR;
        case bf::source_alpha: return D3D12_BLEND_SRC_ALPHA;
        case bf::one_minus_source_alpha: return D3D12_BLEND_INV_SRC_ALPHA;
        case bf::dest_alpha: return D3D12_BLEND_DEST_ALPHA;
        case bf::one_minus_dest_alpha: return D3D12_BLEND_INV_DEST_ALPHA;
    }
    return D3D12_BLEND_ONE;
}
D3D12_BLEND_OP BlendOp(reshadefx::blend_op o) {
    using bo = reshadefx::blend_op;
    switch (o) {
        case bo::add: return D3D12_BLEND_OP_ADD;
        case bo::subtract: return D3D12_BLEND_OP_SUBTRACT;
        case bo::reverse_subtract: return D3D12_BLEND_OP_REV_SUBTRACT;
        case bo::min: return D3D12_BLEND_OP_MIN;
        case bo::max: return D3D12_BLEND_OP_MAX;
    }
    return D3D12_BLEND_OP_ADD;
}
D3D12_TEXTURE_ADDRESS_MODE Address(reshadefx::texture_address_mode m) {
    using am = reshadefx::texture_address_mode;
    switch (m) {
        case am::wrap: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        case am::mirror: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        case am::border: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        default: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    }
}
D3D12_RESOURCE_BARRIER Tr(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    return b;
}

// ---- a build: the effect compiled for one size and one set of definitions, with everything it draws with
struct Texture {
    reshadefx::texture info;
    ID3D12Resource* res = nullptr;   // ours (none for a semantic: what is bound is read)
    D3D12_RESOURCE_STATES state = kRead;
    DXGI_FORMAT view = DXGI_FORMAT_UNKNOWN;
    UINT srv = ~0u, srvSrgb = ~0u, rtv = ~0u, rtvSrgb = ~0u, uav = ~0u;   // slots in the build's CPU heaps
    std::vector<uint8_t> pixels;     // a picture to upload (RGBA8, every mip, tightly packed), or update_texture's
    ID3D12Resource* upload = nullptr;
    bool clear = false;              // a render target or storage, cleared to zero once
    // a semantic's binding as written into srv
    ID3D12Resource* boundRes = nullptr;
    DXGI_FORMAT boundFmt = DXGI_FORMAT_UNKNOWN;
    bool boundOnce = false;
    // a picture: its key in Impl::pics (the file, the size, the mips; "" none), and the picture its views show now (null views until it is there)
    std::string pic;
    ID3D12Resource* shown = nullptr;
};
// a picture as a build found it: the file it reads, the texture it fills
struct PicSpec { std::wstring path; uint32_t w = 0, h = 0, levels = 1; std::string name; };
struct Pass {
    reshadefx::pass info;
    ID3D12PipelineState* pso = nullptr;   // (shared between passes alike: Build::psos owns them)
    bool compute = false;
    std::vector<int> rts;                 // the textures drawn into; none: the target (the back buffer)
    struct Bind { UINT slot; int tex; bool srgb; };
    std::vector<Bind> srvs, uavs;
    UINT samplerTable = 0;                // its sampler states' table in Build::samplers (in tables of maxS)
    bool copyAfter = false;               // it draws into the target and a later pass reads COLOR: COLOR copied after it
};
struct Technique {
    std::string name;
    std::vector<Pass> passes;
    bool enabled = false;
};
struct Build {
    uint32_t w = 0, h = 0;
    DXGI_FORMAT target = DXGI_FORMAT_UNKNOWN;
    uint64_t gen = 0;
    reshadefx::effect_module module;
    std::vector<uint8_t> uniforms;
    std::unordered_map<std::string, int> uniformByName;
    std::vector<int> timers, frameTimes;   // uniforms with source = "timer" / "frametime"
    std::vector<Texture> textures;
    std::map<std::string, PicSpec> pics;   // its pictures by key (Texture::pic)
    std::unordered_map<std::string, int> textureByUnique, textureByName;
    std::vector<Technique> techniques;
    std::map<std::string, ID3D12PipelineState*> psos;
    std::unordered_map<std::string, std::string> code;   // entry point -> DXBC
    ID3D12RootSignature* root = nullptr;
    ID3D12DescriptorHeap* cpuSrv = nullptr;   // SRVs, UAVs and the null ones (not shader-visible: copied into the ring per pass)
    ID3D12DescriptorHeap* cpuRtv = nullptr;
    // shader-visible, a table for each pass: the generator numbers the sampler states per entry point (s0 of one pass's shader is not
    // another's) - one table for all, from the first pass, left every other pass's states unmade: the sky's pictures read black (PC2,
    // 2026-10-26: "no sky there, only fog")
    ID3D12DescriptorHeap* samplers = nullptr;
    UINT samplerInc = 0;
    UINT srvInc = 0, rtvInc = 0;
    UINT maxT = 1, maxU = 0, maxS = 1;
    UINT nullSrv = 0, nullUav = 0;              // first slots of maxT null SRVs and maxU null UAVs
    ID3D12Resource* cb = nullptr;
    uint8_t* cbMapped = nullptr;
    UINT cbStride = 256, cbAt = 0;
    bool uploaded = false;
    int uploadsLeft = 0;   // Renders till the pictures' upload buffers go (the GPU has copied them by then)
    bool ok = false;
    std::string error;
    double ms = 0.0;
    uint32_t cached = 0, compiled = 0;

    ~Build() {
        for (Texture& t : textures) { Release(t.res); Release(t.upload); }
        for (auto& p : psos) Release(p.second);
        Release(root);
        Release(cpuSrv);
        Release(cpuRtv);
        Release(samplers);
        if (cb && cbMapped) cb->Unmap(0, nullptr);
        Release(cb);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE Srv(UINT i) const {
        D3D12_CPU_DESCRIPTOR_HANDLE at = cpuSrv->GetCPUDescriptorHandleForHeapStart();
        at.ptr += (SIZE_T)i * srvInc;
        return at;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE Rtv(UINT i) const {
        D3D12_CPU_DESCRIPTOR_HANDLE at = cpuRtv->GetCPUDescriptorHandleForHeapStart();
        at.ptr += (SIZE_T)i * rtvInc;
        return at;
    }
};

struct Job {
    bool compileOnly = false;   // a size built before, its shaders into the disk cache only (Precompile): no device, no textures
    ID3D12Device* dev = nullptr;
    uint32_t w = 0, h = 0;
    DXGI_FORMAT target = DXGI_FORMAT_UNKNOWN;
    uint64_t gen = 0;
    std::map<std::string, std::string> defines;
};

}  // namespace

struct Runtime::Impl {
    Options o;
    void Log(const char* fmt, ...) {
        if (!o.log) return;
        char b[1024];
        va_list a;
        va_start(a, fmt);
        vsnprintf(b, sizeof b, fmt, a);
        va_end(a);
        o.log(b);
    }

    std::unique_ptr<Build> cur;
    std::mutex readyMx;
    std::unique_ptr<Build> ready;              // a finished build, taken over by the next Render
    std::vector<std::pair<std::unique_ptr<Build>, int>> retired;
    HANDLE worker = nullptr;
    std::atomic<bool> busy{false}, cancel{false};
    uint64_t wantGen = 1;                      // the definitions' generation
    std::map<std::string, std::string> defines;
    uint64_t triedGen = 0;                     // the last job started: (size, format, definitions)
    uint32_t triedW = 0, triedH = 0;
    DXGI_FORMAT triedFmt = DXGI_FORMAT_UNKNOWN;
    // What the plugin binds to a semantic - a reference of ours held while it is bound: the eye made's depth (D2RVR_DEPTH_MADE) is freed by
    // turn_warp when the picture's size changes, and the next build's first views of it were made on a freed texture - the game died in the
    // graphics driver at the size change (PC2, 2026-10-26: nvwgf2umx.dll reading 0x2C, twice). One replaced is let go kRetireFrames frames
    // later (the graphics card may still read it).
    struct Sem { ID3D12Resource* res; DXGI_FORMAT fmt; };
    std::unordered_map<std::string, Sem> semantics;
    std::vector<std::pair<ID3D12Resource*, int>> oldSemantics;
    bool toldViews = false;

    // The pictures, apart from the builds (Options::pictureWanted; the present's thread, but the reads' queue and results under picMx): one
    // for every texture of the same file and size, kept while wanted - a new build at another size binds the one there (it was made for each
    // build, both builds' 166 MiB on the card at the size change, read and made again), and only the act's sky and the ceiling's stone are on
    // the card (ReShade keeps all 34).
    struct Pic {
        PicSpec spec;
        ID3D12Resource* res = nullptr;    // on the card (uploaded: its copy recorded)
        bool uploaded = false;
        std::vector<uint8_t> pixels;      // read, every mip, to go up with the next Render
        ID3D12Resource* upload = nullptr;
        int uploadLeft = 0;               // Begins till its upload buffer goes
        bool reading = false, failed = false;
        int unwanted = 0;                 // Begins in a row not wanted
        bool wanted = false;              // a texture of the build in use wants it (this Begin)
    };
    std::map<std::string, Pic> pics;
    std::vector<std::pair<ID3D12Resource*, int>> oldPics;   // let go, still maybe read by the card
    uint32_t picReads = 0;
    std::string picsOn, picsOff;   // what came onto the card and went since the last line
    std::mutex picMx;
    std::condition_variable picCv;
    std::deque<std::pair<std::string, PicSpec>> picAsk;                   // to read
    std::vector<std::pair<std::string, std::vector<uint8_t>>> picDone;   // read (empty: could not be)
    std::thread picThread;
    bool picStop = false;
    std::wstring PicturePath(const std::string& src) const;
    void PicturesTick();                                   // Frame: the reads in, wanted or not, made, let go, the build's views
    void PictureUploads(ID3D12GraphicsCommandList* l);    // Render: the pictures read, onto the card
    void BindPictures(Build& b);                          // the build's views of the pictures there

    // the ring and COLOR (the device's, kept across builds)
    ID3D12Device* dev = nullptr;
    ID3D12DescriptorHeap* ring = nullptr;
    UINT ringInc = 0, ringAt = 0;
    ID3D12Resource* color = nullptr;
    D3D12_RESOURCE_DESC colorDesc{};
    std::vector<std::pair<ID3D12Resource*, int>> oldColors;
    ID3D12DescriptorHeap* targetRtv = nullptr;   // the target's two views (linear, sRGB), written each Render
    UINT targetRtvInc = 0;

    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now(), lastFrame = start;
    Stats stats;
    bool begun = false;   // Begin found a build for this frame's back buffer

    // the graphics card's time: a query heap of kTsSlots Renders, read back kTsSlots Renders later
    uint64_t tsHz = 0;
    ID3D12QueryHeap* tsHeap = nullptr;
    ID3D12Resource* tsRead = nullptr;
    const uint64_t* tsMap = nullptr;
    UINT tsFrame = 0;
    ID3D12GraphicsCommandList* tsList = nullptr;   // the Render being timed (nullptr: none)
    UINT tsAt = 0;                                 // its next timestamp
    struct TsSlot { bool used = false; UINT n = 0; std::vector<std::string> names; };   // (copies: a build may be let go before its slot is read)
    TsSlot tsSlots[kTsSlots];
    std::map<std::string, std::pair<double, uint32_t>> tsPass;   // ms per pass summed since TakeStats, and the Renders it ran in
    double tsTotal = 0.0;
    uint32_t tsCount = 0;
    void Stamp() {
        if (tsList && tsAt < kTsPer) tsList->EndQuery(tsHeap, D3D12_QUERY_TYPE_TIMESTAMP, (tsFrame % kTsSlots) * kTsPer + tsAt++);
    }
    void TimingStart(ID3D12GraphicsCommandList* l);
    void TimingEnd();

    ~Impl() {
        cancel = true;
        Join(worker);
        {
            std::lock_guard<std::mutex> l(picMx);
            picStop = true;
        }
        picCv.notify_all();
        if (picThread.joinable()) picThread.join();
        for (auto& p : pics) { Release(p.second.res); Release(p.second.upload); }
        for (auto& p : oldPics) Release(p.first);
        cur.reset();
        ready.reset();
        retired.clear();
        Release(ring);
        Release(color);
        for (auto& c : oldColors) Release(c.first);
        for (auto& c : oldSemantics) Release(c.first);
        for (auto& s : semantics) Release(s.second.res);
        Release(targetRtv);
        Release(tsHeap);
        if (tsRead && tsMap) tsRead->Unmap(0, nullptr);
        Release(tsRead);
    }

    // ---------------------------------------------------------------- the build (a worker thread)
    std::unique_ptr<Build> Make(const Job& job);
    bool Compile(Build& b, const Job& job);
    bool MakeObjects(Build& b, ID3D12Device* d);

    // The sizes built before (cacheDir\sizes.txt, the last first): at a start the ones not in use now are compiled into the disk cache first -
    // the game switching to its VR render size then finds its shaders there (a cold build took 30 s on an i5-11400F, the effect missing meanwhile).
    std::vector<std::pair<uint32_t, uint32_t>> recent;
    // ... and the ones to warm this run (as read at the start): apart from `recent`, which each build rewrites - the size in use is built
    // first now (vr/fx_build_order.h), and its RememberSize took the list as warmed before any was
    std::vector<std::pair<uint32_t, uint32_t>> warm;
    size_t preAt = 0;
    void LoadSizes() {
        if (o.cacheDir.empty()) return;
        std::ifstream f(o.cacheDir + L"\\sizes.txt");
        unsigned w = 0, h = 0;
        char x = 0;
        while (f >> w >> x >> h)
            if (x == 'x' && w >= 16 && h >= 16 && w <= 16384 && h <= 16384 && recent.size() < 4) recent.push_back({w, h});
        warm = recent;
    }
    void RememberSize(uint32_t w, uint32_t h) {
        if (o.cacheDir.empty()) return;
        std::vector<std::pair<uint32_t, uint32_t>> v{{w, h}};
        for (const auto& s : recent) if (s != v[0] && v.size() < 4) v.push_back(s);
        if (v == recent) return;
        recent = v;   // (the warm list stays: a size built this run is skipped there when reached - Frame)
        CreateDirectoryW(o.cacheDir.c_str(), nullptr);
        std::ofstream f(o.cacheDir + L"\\sizes.txt");
        for (const auto& s : recent) f << s.first << 'x' << s.second << '\n';
    }

    void StartJob(ID3D12Device* d, uint32_t w, uint32_t h, DXGI_FORMAT fmt, bool compileOnly = false) {
        if (busy.load()) return;
        Join(worker);
        Job job;
        job.compileOnly = compileOnly;
        job.dev = d;
        job.w = w;
        job.h = h;
        job.target = fmt;
        job.gen = wantGen;
        job.defines = defines;
        if (!compileOnly) {
            triedGen = wantGen;
            triedW = w;
            triedH = h;
            triedFmt = fmt;
        }
        busy = true;
        d->AddRef();
        worker = BigThread([this, job]() {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            std::unique_ptr<Build> b = Make(job);
            job.dev->Release();
            if (b) {   // (a precompile hands nothing over)
                std::lock_guard<std::mutex> l(readyMx);
                ready = std::move(b);
            }
            busy = false;
        });
        if (!worker) {
            job.dev->Release();
            busy = false;
            Log("vrcam: native effects - no thread for the build");
        }
    }

    // ---------------------------------------------------------------- the frame
    bool Frame(ID3D12Device* d, const D3D12_RESOURCE_DESC& target);
    void Uploads(Build& b, ID3D12GraphicsCommandList* l);
    void WriteSemantics(Build& b);
    bool EnsureColor(ID3D12Device* d, const D3D12_RESOURCE_DESC& target);
    D3D12_GPU_DESCRIPTOR_HANDLE Table(Build& b, const Pass& p, D3D12_GPU_DESCRIPTOR_HANDLE* uavTable);
    UINT RunTechnique(Build& b, Technique& t, ID3D12GraphicsCommandList* l, ID3D12Resource* target, D3D12_RESOURCE_STATES& targetState,
                      D3D12_GPU_VIRTUAL_ADDRESS cb);
    D3D12_GPU_VIRTUAL_ADDRESS Constants(Build& b);
};

// ---- the uniforms' layout, as ReShade's runtime
namespace {
void SetData(Build& b, const reshadefx::uniform& u, const uint8_t* data, size_t size, size_t base) {
    size = std::min(size, (size_t)u.size);
    const size_t arrayLength = u.type.is_array() ? (size_t)u.type.array_length : 1u;
    if (base >= arrayLength || u.offset + size > b.uniforms.size()) return;
    uint8_t* store = b.uniforms.data();
    if (u.type.is_matrix()) {
        for (size_t a = base, i = 0; a < arrayLength; ++a)
            for (size_t row = 0; row < u.type.rows; ++row)
                for (size_t col = 0; i < size / 4 && col < u.type.cols; ++col, ++i)
                    memcpy(store + u.offset + (a * u.type.rows * 4 + (row * 4 + col)) * 4, data + ((a - base) * u.type.components() + (row * u.type.cols + col)) * 4, 4);
    } else if (arrayLength > 1) {
        for (size_t a = base, i = 0; a < arrayLength; ++a)
            for (size_t row = 0; i < size / 4 && row < u.type.rows; ++row, ++i)
                memcpy(store + u.offset + (a * 4 + row) * 4, data + ((a - base) * u.type.components() + row) * 4, 4);
    } else {
        memcpy(store + u.offset, data, size);
    }
}
void GetData(const Build& b, const reshadefx::uniform& u, uint8_t* data, size_t size, size_t base) {
    size = std::min(size, (size_t)u.size);
    const size_t arrayLength = u.type.is_array() ? (size_t)u.type.array_length : 1u;
    if (base >= arrayLength || u.offset + size > b.uniforms.size()) return;
    const uint8_t* store = b.uniforms.data();
    if (u.type.is_matrix()) {
        for (size_t a = base, i = 0; a < arrayLength; ++a)
            for (size_t row = 0; row < u.type.rows; ++row)
                for (size_t col = 0; i < size / 4 && col < u.type.cols; ++col, ++i)
                    memcpy(data + ((a - base) * u.type.components() + (row * u.type.cols + col)) * 4, store + u.offset + (a * (u.type.rows * 4) + (row * 4 + col)) * 4, 4);
    } else if (arrayLength > 1) {
        for (size_t a = base, i = 0; a < arrayLength; ++a)
            for (size_t row = 0; i < size / 4 && row < u.type.rows; ++row, ++i)
                memcpy(data + ((a - base) * u.type.components() + row) * 4, store + u.offset + (a * 4 + row) * 4, 4);
    } else {
        memcpy(data, store + u.offset, size);
    }
}
std::string Annotation(const std::vector<reshadefx::annotation>& as, const char* name) {
    for (const reshadefx::annotation& a : as)
        if (a.name == name) return a.value.string_data;
    return std::string();
}
}  // namespace

// ---------------------------------------------------------------- compile
bool Runtime::Impl::Compile(Build& b, const Job& job) {
    const auto t0 = std::chrono::steady_clock::now();
    reshadefx::preprocessor pp;
    pp.add_macro_definition("__RESHADE__", "60400");
    pp.add_macro_definition("__RESHADE_PERFORMANCE_MODE__", "0");
    pp.add_macro_definition("__RENDERER__", "0xc000");   // D3D12, as ReShade says it
    pp.add_macro_definition("__VENDOR__", "0");
    pp.add_macro_definition("__DEVICE__", "0");
    pp.add_macro_definition("__APPLICATION__", "0");
    pp.add_macro_definition("BUFFER_WIDTH", std::to_string(job.w));
    pp.add_macro_definition("BUFFER_HEIGHT", std::to_string(job.h));
    pp.add_macro_definition("BUFFER_RCP_WIDTH", "(1.0 / BUFFER_WIDTH)");
    pp.add_macro_definition("BUFFER_RCP_HEIGHT", "(1.0 / BUFFER_HEIGHT)");
    pp.add_macro_definition("BUFFER_COLOR_SPACE", "1");
    pp.add_macro_definition("BUFFER_COLOR_BIT_DEPTH", Linear(job.target) == DXGI_FORMAT_R10G10B10A2_UNORM ? "10" : "8");
    for (const auto& d : job.defines) pp.add_macro_definition(d.first, d.second.empty() ? "1" : d.second);
    const std::filesystem::path file(o.effectFile);
    pp.add_include_path(file.parent_path());
    if (!pp.append_file(file)) {
        b.error = "the preprocessor: " + pp.errors();
        return false;
    }
    std::unique_ptr<reshadefx::codegen> cg(reshadefx::create_codegen_hlsl(51, false, false));
    reshadefx::parser parser;
    if (!parser.parse(pp.output(), cg.get())) {
        b.error = "the parser: " + parser.errors();
        return false;
    }
    b.module = cg->module();
    // every entry point's HLSL (one thread: the generator), then D3DCompile on several, the disk cache first
    struct Ep { std::string name, hlsl, cso, profile; std::wstring cacheFile; bool done = false, failed = false; std::string errors; };
    std::vector<Ep> eps;
    for (const auto& e : b.module.entry_points) {
        Ep x;
        x.name = e.first;
        x.profile = e.second == reshadefx::shader_type::vertex ? "vs_5_1" : e.second == reshadefx::shader_type::pixel ? "ps_5_1" : "cs_5_1";
        std::string a, err;
        if (!cg->assemble_code_for_entry_point(e.first, x.hlsl, a, err)) {
            b.error = "the code of " + e.first + ": " + err;
            return false;
        }
        eps.push_back(std::move(x));
    }
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | (o.optimisation <= 0 ? D3DCOMPILE_OPTIMIZATION_LEVEL0 : o.optimisation == 1 ? D3DCOMPILE_OPTIMIZATION_LEVEL1
                                                       : o.optimisation == 2 ? D3DCOMPILE_OPTIMIZATION_LEVEL2 : D3DCOMPILE_OPTIMIZATION_LEVEL3);
    if (!o.cacheDir.empty()) CreateDirectoryW(o.cacheDir.c_str(), nullptr);
    for (Ep& e : eps) {
        uint64_t h = Fnv(e.hlsl.data(), e.hlsl.size());
        h = Fnv(e.profile.data(), e.profile.size(), h);
        h = Fnv(&flags, sizeof flags, h);
        if (o.cacheDir.empty()) continue;
        wchar_t name[32];
        swprintf_s(name, L"%016llx.cso", (unsigned long long)h);
        e.cacheFile = o.cacheDir + L"\\" + name;
        std::ifstream f(e.cacheFile, std::ios::binary);
        if (f) {
            e.cso.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            e.done = !e.cso.empty();
        }
        if (e.done) ++b.cached;
    }
    const D3DCompileFn compile = Compiler();
    if (!compile) { b.error = "no d3dcompiler_47.dll"; return false; }
    std::atomic<size_t> next{0};
    std::atomic<uint32_t> compiledNow{0};
    auto work = [&]() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        for (;;) {
            const size_t i = next.fetch_add(1);
            if (i >= eps.size() || cancel.load()) return;
            Ep& e = eps[i];
            if (e.done) continue;
            ID3DBlob* out = nullptr;
            ID3DBlob* err = nullptr;
            const HRESULT hr = compile(e.hlsl.data(), e.hlsl.size(), nullptr, nullptr, nullptr, e.name.c_str(), e.profile.c_str(), flags, 0, &out, &err);
            compiledNow.fetch_add(1);
            if (FAILED(hr) || !out) {
                e.failed = true;
                if (err) e.errors.assign((const char*)err->GetBufferPointer(), err->GetBufferSize());
            } else {
                e.cso.assign((const char*)out->GetBufferPointer(), out->GetBufferSize());
                e.done = true;
            }
            if (out) out->Release();
            if (err) err->Release();
        }
    };
    int n = o.threads > 0 ? o.threads : (int)std::thread::hardware_concurrency() - 1;
    n = std::clamp(n, 1, 8);
    std::vector<HANDLE> pool;
    for (int i = 1; i < n; ++i)
        if (const HANDLE h = BigThread(work)) pool.push_back(h);
    work();
    for (HANDLE& h : pool) Join(h);
    if (cancel.load()) { b.error = "cancelled"; return false; }
    b.compiled = compiledNow.load();
    for (Ep& e : eps) {
        if (e.failed) {
            b.error = "D3DCompile " + e.name + ": " + e.errors;
            return false;
        }
        if (!e.cacheFile.empty() && GetFileAttributesW(e.cacheFile.c_str()) == INVALID_FILE_ATTRIBUTES) {   // compiled now: kept for the next start
            std::ofstream f(e.cacheFile, std::ios::binary);
            f.write(e.cso.data(), (std::streamsize)e.cso.size());
        }
        b.code[e.name] = std::move(e.cso);
    }
    b.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return true;
}

// ---------------------------------------------------------------- the pictures (WIC), every mip on the CPU
std::wstring Runtime::Impl::PicturePath(const std::string& src) const {
    std::wstring rel(src.begin(), src.end());
    std::replace(rel.begin(), rel.end(), L'/', L'\\');
    if (rel.find(L':') != std::wstring::npos) return rel;
    for (const std::wstring& d : o.textureDirs) {
        const std::wstring p = d + (d.empty() || d.back() == L'\\' ? L"" : L"\\") + rel;
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    }
    return std::wstring();
}

namespace {
// The file at the texture's size (ReShade scales a picture of another size to the texture's), RGBA8, every mip by a 2 x 2 box (ReShade makes
// them on the GPU the same way), tightly packed. False: not read.
bool ReadPicture(IWICImagingFactory* wic, const PicSpec& s, std::vector<uint8_t>& out) {
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    bool ok = SUCCEEDED(wic->CreateDecoderFromFilename(s.path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) &&
              SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
              SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));
    UINT iw = 0, ih = 0;
    if (ok) conv->GetSize(&iw, &ih);
    IWICBitmapSource* srcBmp = conv;
    if (ok && (iw != s.w || ih != s.h)) {
        ok = SUCCEEDED(wic->CreateBitmapScaler(&scaler)) && SUCCEEDED(scaler->Initialize(conv, s.w, s.h, WICBitmapInterpolationModeFant));
        srcBmp = scaler;
    }
    std::vector<uint8_t> top((size_t)s.w * s.h * 4);
    if (ok) ok = SUCCEEDED(srcBmp->CopyPixels(nullptr, s.w * 4, (UINT)top.size(), top.data()));
    Release(scaler);
    Release(conv);
    Release(frame);
    Release(dec);
    if (!ok) return false;
    out = top;
    uint32_t w = s.w, h = s.h;
    std::vector<uint8_t> prev = std::move(top);
    for (uint32_t level = 1; level < s.levels && (w > 1 || h > 1); ++level) {
        const uint32_t nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
        std::vector<uint8_t> m((size_t)nw * nh * 4);
        for (uint32_t y = 0; y < nh; ++y)
            for (uint32_t x = 0; x < nw; ++x)
                for (int c = 0; c < 4; ++c) {
                    const uint32_t x0 = std::min(w - 1, 2 * x), x1 = std::min(w - 1, 2 * x + 1), y0 = std::min(h - 1, 2 * y), y1 = std::min(h - 1, 2 * y + 1);
                    const uint32_t v = prev[((size_t)y0 * w + x0) * 4 + c] + prev[((size_t)y0 * w + x1) * 4 + c] + prev[((size_t)y1 * w + x0) * 4 + c] +
                                       prev[((size_t)y1 * w + x1) * 4 + c];
                    m[((size_t)y * nw + x) * 4 + c] = (uint8_t)((v + 2) / 4);
                }
        out.insert(out.end(), m.begin(), m.end());
        prev = std::move(m);
        w = nw;
        h = nh;
    }
    return true;
}
std::string Utf8(const std::wstring& w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? (size_t)n : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
double PicMiB(const PicSpec& s) {
    double bytes = 0.0;
    for (uint32_t i = 0; i < s.levels; ++i) bytes += (double)std::max(1u, s.w >> i) * std::max(1u, s.h >> i) * 4.0;
    return bytes / (1024.0 * 1024.0);
}
}  // namespace

void Runtime::Impl::PicturesTick() {
    for (auto it = oldPics.begin(); it != oldPics.end();) {
        if (--it->second <= 0) { it->first->Release(); it = oldPics.erase(it); }
        else ++it;
    }
    {   // the reads done
        std::vector<std::pair<std::string, std::vector<uint8_t>>> done;
        {
            std::lock_guard<std::mutex> l(picMx);
            done.swap(picDone);
        }
        for (auto& d : done) {
            const auto it = pics.find(d.first);
            if (it == pics.end()) continue;
            Pic& p = it->second;
            p.reading = false;
            if (d.second.empty()) {
                p.failed = true;
                const std::string path = Utf8(p.spec.path);
                Log("vrcam: native effects - the picture %s could not be read: %s stays black", path.c_str(), p.spec.name.c_str());
                continue;
            }
            ++picReads;
            p.pixels = std::move(d.second);
        }
    }
    if (!cur || !dev) return;
    for (const auto& s : cur->pics)
        if (pics.find(s.first) == pics.end()) pics[s.first].spec = s.second;
    for (auto& e : pics) e.second.wanted = false;
    for (const Texture& t : cur->textures)
        if (!t.pic.empty() && (!o.pictureWanted || o.pictureWanted(t.info.name.c_str()))) pics[t.pic].wanted = true;
    uint32_t on = 0, reading = 0;
    double mib = 0.0;
    for (auto it = pics.begin(); it != pics.end();) {
        Pic& p = it->second;
        const bool wanted = p.wanted;
        if (p.uploadLeft > 0 && --p.uploadLeft == 0) Release(p.upload);
        if (wanted) {
            p.unwanted = 0;
            if (!p.res && p.pixels.empty() && !p.reading && !p.failed) {
                if (!picThread.joinable())
                    picThread = std::thread([this] {
                        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                        IWICImagingFactory* wic = nullptr;
                        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) wic = nullptr;
                        for (;;) {
                            std::pair<std::string, PicSpec> job;
                            {
                                std::unique_lock<std::mutex> l(picMx);
                                picCv.wait(l, [this] { return picStop || !picAsk.empty(); });
                                if (picStop) break;
                                job = std::move(picAsk.front());
                                picAsk.pop_front();
                            }
                            std::vector<uint8_t> px;
                            if (!wic || !ReadPicture(wic, job.second, px)) px.clear();
                            std::lock_guard<std::mutex> l(picMx);
                            picDone.push_back({job.first, std::move(px)});
                        }
                        Release(wic);
                        if (SUCCEEDED(co)) CoUninitialize();
                    });
                {
                    std::lock_guard<std::mutex> l(picMx);
                    picAsk.push_back({it->first, p.spec});
                }
                picCv.notify_one();
                p.reading = true;
            }
            if (!p.res && !p.pixels.empty()) {   // read: made, to go up with the next Render
                D3D12_RESOURCE_DESC rd{};
                rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                rd.Width = p.spec.w;
                rd.Height = p.spec.h;
                rd.DepthOrArraySize = 1;
                rd.MipLevels = (UINT16)p.spec.levels;
                rd.Format = ResourceFormat(DXGI_FORMAT_R8G8B8A8_UNORM);
                rd.SampleDesc.Count = 1;
                D3D12_HEAP_PROPERTIES hp{};
                hp.Type = D3D12_HEAP_TYPE_DEFAULT;
                if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&p.res)))) {
                    p.res = nullptr;
                    p.failed = true;
                    p.pixels.clear();
                    Log("vrcam: native effects - the picture %s could not be made on the graphics card: it stays black", p.spec.name.c_str());
                } else {
                    const std::wstring wn(p.spec.name.begin(), p.spec.name.end());
                    p.res->SetName((L"d2rvr fx picture " + wn).c_str());
                    p.uploaded = false;
                    picsOn += (picsOn.empty() ? "" : ", ") + p.spec.name;
                }
            }
        } else if (++p.unwanted >= std::max(1, o.pictureKeepFrames) && (p.res || !p.pixels.empty())) {
            // let go: the build's views of it null first (BindPictures below; the lists recorded before copied theirs), the card may still read
            // it a few frames
            if (p.res) {
                oldPics.push_back({p.res, kRetireFrames});
                picsOff += (picsOff.empty() ? "" : ", ") + p.spec.name;
            }
            if (p.upload) oldPics.push_back({p.upload, kRetireFrames});
            p.res = p.upload = nullptr;
            p.uploadLeft = 0;
            p.uploaded = false;
            p.pixels.clear();
            p.pixels.shrink_to_fit();
        }
        if (p.reading) ++reading;
        if (p.res) { ++on; mib += PicMiB(p.spec); }
        // a file no texture of the build in use reads now (a definition changed it), let go: forgotten
        if (!wanted && !p.res && !p.reading && p.pixels.empty() && cur->pics.find(it->first) == cur->pics.end()) it = pics.erase(it);
        else ++it;
    }
    BindPictures(*cur);
    stats.pictures = on;
    stats.picturesReading = reading;
    stats.pictureReads = picReads;
    stats.pictureMiB = mib;
    if (!reading && (!picsOn.empty() || !picsOff.empty())) {
        Log("vrcam: native effects - pictures onto the graphics card: %s; let go: %s; now %u there, %.1f MiB (only the act's sky and the ceiling's "
            "stone; %u files read since the start)", picsOn.empty() ? "none" : picsOn.c_str(), picsOff.empty() ? "none" : picsOff.c_str(), on, mib, picReads);
        picsOn.clear();
        picsOff.clear();
    }
}

void Runtime::Impl::PictureUploads(ID3D12GraphicsCommandList* l) {
    for (auto& e : pics) {
        Pic& p = e.second;
        if (!p.res || p.uploaded || p.pixels.empty()) continue;
        const D3D12_RESOURCE_DESC rd = p.res->GetDesc();
        const UINT n = rd.MipLevels;
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> fp(n);
        std::vector<UINT> rows(n);
        std::vector<UINT64> rowBytes(n);
        UINT64 total = 0;
        dev->GetCopyableFootprints(&rd, 0, n, 0, fp.data(), rows.data(), rowBytes.data(), &total);
        if (p.upload) oldPics.push_back({p.upload, kRetireFrames});
        p.upload = nullptr;
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = total;
        bd.Height = bd.DepthOrArraySize = bd.MipLevels = 1;
        bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        uint8_t* map = nullptr;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&p.upload))) ||
            FAILED(p.upload->Map(0, nullptr, (void**)&map))) {
            Release(p.upload);
            continue;   // (tried again the next Render)
        }
        size_t at = 0;
        for (UINT i = 0; i < n && at < p.pixels.size(); ++i) {
            const size_t tight = (size_t)fp[i].Footprint.Width * 4;
            for (UINT y = 0; y < rows[i] && at + tight <= p.pixels.size(); ++y, at += tight)
                memcpy(map + fp[i].Offset + (size_t)y * fp[i].Footprint.RowPitch, p.pixels.data() + at, tight);
        }
        p.upload->Unmap(0, nullptr);
        for (UINT i = 0; i < n; ++i) {
            D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
            dst.pResource = p.res;
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = i;
            src.pResource = p.upload;
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint = fp[i];
            l->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        }
        const D3D12_RESOURCE_BARRIER br = Tr(p.res, D3D12_RESOURCE_STATE_COPY_DEST, kRead);
        l->ResourceBarrier(1, &br);
        p.uploaded = true;
        p.uploadLeft = kRetireFrames;
        p.pixels.clear();
        p.pixels.shrink_to_fit();
    }
}

void Runtime::Impl::BindPictures(Build& b) {
    for (Texture& t : b.textures) {
        if (t.pic.empty()) continue;
        const auto it = pics.find(t.pic);
        ID3D12Resource* want = it != pics.end() && it->second.res && it->second.uploaded ? it->second.res : nullptr;
        if (want == t.shown) continue;
        t.shown = want;
        const DXGI_FORMAT f = (DXGI_FORMAT)t.info.format;
        D3D12_SHADER_RESOURCE_VIEW_DESC v{};
        v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        v.Texture2D.MipLevels = want ? t.info.levels : 1;
        v.Format = want ? Linear(f) : DXGI_FORMAT_R8G8B8A8_UNORM;
        dev->CreateShaderResourceView(want, &v, b.Srv(t.srv));
        v.Format = want ? Srgb(f) : DXGI_FORMAT_R8G8B8A8_UNORM;
        dev->CreateShaderResourceView(want, &v, b.Srv(t.srvSrgb));
    }
}

// ---------------------------------------------------------------- the GPU objects (a worker thread: the device is free-threaded)
bool Runtime::Impl::MakeObjects(Build& b, ID3D12Device* d) {
    reshadefx::effect_module& m = b.module;
    // uniforms
    b.uniforms.assign(((size_t)m.total_uniform_size + 15) & ~(size_t)15, 0);
    for (size_t i = 0; i < m.uniforms.size(); ++i) {
        const reshadefx::uniform& u = m.uniforms[i];
        b.uniformByName[u.name] = (int)i;
        const size_t arrayLength = u.type.is_array() ? (size_t)u.type.array_length : 1u;
        for (size_t a = 0; u.has_initializer_value && a < arrayLength; ++a) {   // (as ReShade's reset_uniform_value)
            const reshadefx::constant& c = u.type.is_array() ? u.initializer_value.array_data[a] : u.initializer_value;
            SetData(b, u, (const uint8_t*)c.as_uint, (size_t)u.type.components() * 4, a);
        }
        const std::string src = Annotation(u.annotations, "source");
        if (src == "timer") b.timers.push_back((int)i);
        else if (src == "frametime") b.frameTimes.push_back((int)i);
    }
    b.cbStride = std::max<UINT>(256, (UINT)((b.uniforms.size() + 255) & ~(size_t)255));
    // the binding tables' sizes
    for (const reshadefx::technique& t : m.techniques)
        for (const reshadefx::pass& p : t.passes) {
            for (const auto& tb : p.texture_bindings) b.maxT = std::max(b.maxT, tb.entry_point_binding + 1);
            for (const auto& sb : p.sampler_bindings) b.maxS = std::max(b.maxS, sb.entry_point_binding + 1);
            for (const auto& ub : p.storage_bindings) b.maxU = std::max(b.maxU, ub.entry_point_binding + 1);
        }
    UINT nPasses = 0;
    for (const reshadefx::technique& t : m.techniques) nPasses += (UINT)t.passes.size();
    if (b.maxT > 128 || b.maxS > 16 || b.maxU > 8 || (size_t)b.maxS * std::max(1u, nPasses) > D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE) {
        b.error = "more bindings in a pass than the root signature takes";
        return false;
    }
    // textures
    b.srvInc = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    b.rtvInc = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    UINT nSrv = b.maxT + b.maxU, nRtv = 0;
    for (const reshadefx::texture& ti : m.textures) {
        Texture t;
        t.info = ti;
        b.textureByUnique[ti.unique_name] = (int)b.textures.size();
        b.textureByName[ti.name] = (int)b.textures.size();
        b.textures.push_back(std::move(t));
        nSrv += 2 + (ti.storage_access ? 1 : 0);
        nRtv += ti.render_target ? 2 : 0;
    }
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = std::max(1u, nSrv);
    if (FAILED(d->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&b.cpuSrv)))) { b.error = "no SRV heap"; return false; }
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = std::max(1u, nRtv);
    if (FAILED(d->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&b.cpuRtv)))) { b.error = "no RTV heap"; return false; }
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    hd.NumDescriptors = b.maxS * std::max(1u, nPasses);
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(d->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&b.samplers)))) { b.error = "no sampler heap"; return false; }
    UINT srvAt = 0, rtvAt = 0;
    // the null views: what a slot nothing is bound to reads
    b.nullSrv = srvAt;
    for (UINT i = 0; i < b.maxT; ++i) {
        D3D12_SHADER_RESOURCE_VIEW_DESC v{};
        v.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        v.Texture2D.MipLevels = 1;
        d->CreateShaderResourceView(nullptr, &v, b.Srv(srvAt++));
    }
    b.nullUav = srvAt;
    for (UINT i = 0; i < b.maxU; ++i) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC v{};
        v.Format = DXGI_FORMAT_R32_UINT;
        v.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        d->CreateUnorderedAccessView(nullptr, nullptr, &v, b.Srv(srvAt++));
    }
    for (Texture& t : b.textures) {
        const reshadefx::texture& ti = t.info;
        t.srv = srvAt++;
        t.srvSrgb = srvAt++;
        if (!ti.semantic.empty()) continue;   // what the plugin binds: written by WriteSemantics before the first pass
        const DXGI_FORMAT f = (DXGI_FORMAT)ti.format;
        t.view = f;
        if (const std::string src = Annotation(ti.annotations, "source"); !src.empty()) {
            // a picture: apart from the build (Impl::pics), its views null until it is on the card (BindPictures)
            if (ti.format == reshadefx::texture_format::rgba8 && ti.type == reshadefx::texture_type::texture_2d && !ti.render_target && !ti.storage_access) {
                const std::wstring path = PicturePath(src);
                if (path.empty()) {
                    Log("vrcam: native effects - the picture %s was not found: %s stays black", src.c_str(), ti.unique_name.c_str());
                } else {
                    char k[48];
                    snprintf(k, sizeof k, "|%ux%u|%u", (unsigned)ti.width, (unsigned)ti.height, (unsigned)ti.levels);
                    t.pic = Utf8(path) + k;
                    if (b.pics.find(t.pic) == b.pics.end()) b.pics[t.pic] = PicSpec{path, ti.width, ti.height, std::max<uint32_t>(1, ti.levels), ti.name};
                }
                D3D12_SHADER_RESOURCE_VIEW_DESC v{};
                v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                v.Texture2D.MipLevels = 1;
                v.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                d->CreateShaderResourceView(nullptr, &v, b.Srv(t.srv));
                d->CreateShaderResourceView(nullptr, &v, b.Srv(t.srvSrgb));
                continue;
            }
            Log("vrcam: native effects - %s: a picture into a texture that is not RGBA8 - left empty", ti.unique_name.c_str());
        }
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = ti.type == reshadefx::texture_type::texture_3d ? D3D12_RESOURCE_DIMENSION_TEXTURE3D
                       : ti.type == reshadefx::texture_type::texture_1d ? D3D12_RESOURCE_DIMENSION_TEXTURE1D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = ti.width;
        rd.Height = ti.height;
        rd.DepthOrArraySize = ti.depth;
        rd.MipLevels = ti.levels;
        rd.Format = ResourceFormat(f);
        rd.SampleDesc.Count = 1;
        if (ti.render_target) rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (ti.storage_access) rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if (rd.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
            b.error = ti.unique_name + ": only 2D textures";
            return false;
        }
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        if (FAILED(d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, kRead, nullptr, IID_PPV_ARGS(&t.res)))) {
            b.error = ti.unique_name + ": could not be made";
            return false;
        }
        std::wstring wn(ti.unique_name.begin(), ti.unique_name.end());
        t.res->SetName((L"d2rvr fx " + wn).c_str());
        t.state = kRead;
        D3D12_SHADER_RESOURCE_VIEW_DESC v{};
        v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        v.Texture2D.MipLevels = ti.levels;
        v.Format = Linear(f);
        d->CreateShaderResourceView(t.res, &v, b.Srv(t.srv));
        v.Format = Srgb(f);
        d->CreateShaderResourceView(t.res, &v, b.Srv(t.srvSrgb));
        if (ti.render_target) {
            t.rtv = rtvAt++;
            t.rtvSrgb = rtvAt++;
            D3D12_RENDER_TARGET_VIEW_DESC r{};
            r.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            r.Format = Linear(f);
            d->CreateRenderTargetView(t.res, &r, b.Rtv(t.rtv));
            r.Format = Srgb(f);
            d->CreateRenderTargetView(t.res, &r, b.Rtv(t.rtvSrgb));
            t.clear = true;
        }
        if (ti.storage_access) {
            t.uav = srvAt++;
            D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
            u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            u.Format = Linear(f);
            d->CreateUnorderedAccessView(t.res, nullptr, &u, b.Srv(t.uav));
            t.clear = true;
        }
    }
    // the sampler states: a table for each pass (Build::samplers), its slots as that pass's shaders number them; a slot no state is
    // named for gets a linear clamp one (never an unmade descriptor)
    b.samplerInc = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    auto makeSamplers = [&](const reshadefx::pass& pi, UINT table) {
        D3D12_CPU_DESCRIPTOR_HANDLE base = b.samplers->GetCPUDescriptorHandleForHeapStart();
        base.ptr += (SIZE_T)table * b.maxS * b.samplerInc;
        for (UINT i = 0; i < b.maxS; ++i) {
            D3D12_SAMPLER_DESC sd{};
            sd.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            sd.AddressU = sd.AddressV = sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sd.MaxAnisotropy = 1;
            sd.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
            sd.MaxLOD = D3D12_FLOAT32_MAX;
            d->CreateSampler(&sd, {base.ptr + (SIZE_T)i * b.samplerInc});
        }
        for (const reshadefx::sampler_binding& sb : pi.sampler_bindings) {
            const reshadefx::sampler& s = m.samplers[sb.index];
            D3D12_SAMPLER_DESC sd{};
            sd.Filter = (D3D12_FILTER)s.filter;
            sd.AddressU = Address(s.address_u);
            sd.AddressV = Address(s.address_v);
            sd.AddressW = Address(s.address_w);
            sd.MipLODBias = s.lod_bias;
            sd.MaxAnisotropy = s.filter == reshadefx::filter_mode::anisotropic ? 16 : 1;
            sd.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
            sd.MinLOD = s.min_lod;
            sd.MaxLOD = s.max_lod;
            d->CreateSampler(&sd, {base.ptr + (SIZE_T)sb.entry_point_binding * b.samplerInc});
        }
    };
    UINT samplerTables = 0;
    // the root signature: the uniforms (b0), the textures (t0..), the sampler states (s0..), the storages (u0..)
    {
        D3D12_DESCRIPTOR_RANGE r[3] = {};
        r[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        r[0].NumDescriptors = b.maxT;
        r[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        r[1].NumDescriptors = b.maxS;
        r[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        r[2].NumDescriptors = std::max(1u, b.maxU);
        D3D12_ROOT_PARAMETER p[4] = {};
        p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        p[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        for (int i = 0; i < 3; ++i) {
            p[1 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p[1 + i].DescriptorTable.NumDescriptorRanges = 1;
            p[1 + i].DescriptorTable.pDescriptorRanges = &r[i];
            p[1 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
        D3D12_ROOT_SIGNATURE_DESC rs{};
        rs.NumParameters = b.maxU ? 4 : 3;
        rs.pParameters = p;
        ID3DBlob* blob = nullptr;
        ID3DBlob* err = nullptr;
        const SerializeFn serialize = Serializer();
        const HRESULT hr = serialize ? serialize(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err) : E_FAIL;
        if (SUCCEEDED(hr)) d->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&b.root));
        Release(blob);
        Release(err);
        if (!b.root) { b.error = "no root signature"; return false; }
    }
    // the passes and their pipeline states (alike ones shared)
    for (const reshadefx::technique& ti : m.techniques) {
        Technique t;
        t.name = ti.name;
        for (const reshadefx::pass& pi : ti.passes) {
            Pass p;
            p.info = pi;
            p.compute = !pi.cs_entry_point.empty();
            p.samplerTable = samplerTables++;
            makeSamplers(pi, p.samplerTable);
            for (const std::string& n : pi.render_target_names) {
                if (n.empty()) break;
                const auto it = b.textureByUnique.find(n);
                if (it == b.textureByUnique.end()) { b.error = "pass " + pi.name + ": no texture " + n; return false; }
                p.rts.push_back(it->second);
            }
            for (const reshadefx::texture_binding& tb : pi.texture_bindings) {
                const auto it = b.textureByUnique.find(m.samplers[tb.index].texture_name);
                if (it != b.textureByUnique.end()) p.srvs.push_back({tb.entry_point_binding, it->second, tb.srgb});
            }
            for (const reshadefx::storage_binding& ub : pi.storage_bindings) {
                const auto it = b.textureByUnique.find(m.storages[ub.index].texture_name);
                if (it != b.textureByUnique.end()) p.uavs.push_back({ub.entry_point_binding, it->second, false});
            }
            std::string key = p.compute ? "cs:" + pi.cs_entry_point : "vs:" + pi.vs_entry_point + "|ps:" + pi.ps_entry_point;
            DXGI_FORMAT fmts[8] = {};
            UINT nRt = 0;
            if (!p.compute) {
                if (p.rts.empty()) fmts[nRt++] = pi.srgb_write_enable ? Srgb(b.target) : Linear(b.target);
                for (int ti2 : p.rts) fmts[nRt++] = pi.srgb_write_enable ? Srgb(b.textures[ti2].view) : Linear(b.textures[ti2].view);
                char k[512];
                snprintf(k, sizeof k, "|%u|%d|%d", nRt, (int)pi.topology, pi.srgb_write_enable ? 1 : 0);
                key += k;
                for (UINT i = 0; i < nRt; ++i) {
                    snprintf(k, sizeof k, "|%d:%d%d%d%d%d%d%d%x", (int)fmts[i], pi.blend_enable[i], (int)pi.source_color_blend_factor[i], (int)pi.dest_color_blend_factor[i],
                             (int)pi.color_blend_op[i], (int)pi.source_alpha_blend_factor[i], (int)pi.dest_alpha_blend_factor[i], (int)pi.alpha_blend_op[i],
                             pi.render_target_write_mask[i]);
                    key += k;
                }
            }
            if (auto it = b.psos.find(key); it != b.psos.end()) {
                p.pso = it->second;
            } else {
                ID3D12PipelineState* pso = nullptr;
                if (p.compute) {
                    const std::string& cs = b.code[pi.cs_entry_point];
                    D3D12_COMPUTE_PIPELINE_STATE_DESC cd{};
                    cd.pRootSignature = b.root;
                    cd.CS = {cs.data(), cs.size()};
                    d->CreateComputePipelineState(&cd, IID_PPV_ARGS(&pso));
                } else {
                    const std::string& vs = b.code[pi.vs_entry_point];
                    const std::string& ps = b.code[pi.ps_entry_point];
                    D3D12_GRAPHICS_PIPELINE_STATE_DESC gd{};
                    gd.pRootSignature = b.root;
                    gd.VS = {vs.data(), vs.size()};
                    if (!pi.ps_entry_point.empty()) gd.PS = {ps.data(), ps.size()};
                    gd.BlendState.IndependentBlendEnable = TRUE;
                    for (UINT i = 0; i < 8; ++i) {
                        D3D12_RENDER_TARGET_BLEND_DESC& bd = gd.BlendState.RenderTarget[i];
                        bd.BlendEnable = pi.blend_enable[i];
                        bd.SrcBlend = Blend(pi.source_color_blend_factor[i]);
                        bd.DestBlend = Blend(pi.dest_color_blend_factor[i]);
                        bd.BlendOp = BlendOp(pi.color_blend_op[i]);
                        bd.SrcBlendAlpha = Blend(pi.source_alpha_blend_factor[i]);
                        bd.DestBlendAlpha = Blend(pi.dest_alpha_blend_factor[i]);
                        bd.BlendOpAlpha = BlendOp(pi.alpha_blend_op[i]);
                        bd.LogicOp = D3D12_LOGIC_OP_NOOP;
                        bd.RenderTargetWriteMask = pi.render_target_write_mask[i];
                    }
                    gd.SampleMask = UINT_MAX;
                    gd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
                    gd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
                    gd.RasterizerState.DepthClipEnable = TRUE;
                    gd.DepthStencilState.DepthEnable = FALSE;
                    gd.DepthStencilState.StencilEnable = FALSE;
                    using pt = reshadefx::primitive_topology;
                    gd.PrimitiveTopologyType = pi.topology == pt::point_list ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT
                                               : (pi.topology == pt::line_list || pi.topology == pt::line_strip) ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE
                                                                                                                 : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                    gd.NumRenderTargets = nRt;
                    for (UINT i = 0; i < nRt; ++i) gd.RTVFormats[i] = fmts[i];
                    gd.SampleDesc.Count = 1;
                    d->CreateGraphicsPipelineState(&gd, IID_PPV_ARGS(&pso));
                }
                if (!pso) { b.error = "pass " + pi.name + " (" + key + "): no pipeline state"; return false; }
                b.psos[key] = pso;
                p.pso = pso;
            }
            t.passes.push_back(std::move(p));
        }
        for (size_t i = 0; i < t.passes.size(); ++i) {   // COLOR copied after a pass into the target only when a pass after it reads COLOR
            Pass& p = t.passes[i];
            if (p.compute || !p.rts.empty()) continue;
            for (size_t j = i + 1; j < t.passes.size() && !p.copyAfter; ++j)
                for (const Pass::Bind& x : t.passes[j].srvs)
                    if (b.textures[x.tex].info.semantic == "COLOR") p.copyAfter = true;
        }
        b.techniques.push_back(std::move(t));
    }
    // the uniforms' ring and the pictures' uploads
    {
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = (UINT64)b.cbStride * kCbSlots;
        rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&b.cb))) ||
            FAILED(b.cb->Map(0, nullptr, (void**)&b.cbMapped))) {
            b.error = "no uniform buffer";
            return false;
        }
    }
    return true;
}

std::unique_ptr<Build> Runtime::Impl::Make(const Job& job) {
    auto b = std::make_unique<Build>();
    b->w = job.w;
    b->h = job.h;
    b->target = job.target;
    b->gen = job.gen;
    const auto t0 = std::chrono::steady_clock::now();
    if (!Compile(*b, job)) return b;
    if (job.compileOnly) {
        Log("vrcam: native effects - D2R_DepthFog.fx's shaders for %ux%u (a size built before) ready in the cache in %.1f s (%u from it, %u compiled): "
            "that size is quick to build when the game takes it", job.w, job.h, b->ms / 1000.0, b->cached, b->compiled);
        return nullptr;
    }
    if (!MakeObjects(*b, job.dev)) return b;
    {   // (its pictures are read when wanted, apart from it: PicturesTick)
        size_t n = 0;
        for (const Texture& t : b->textures) n += t.pic.empty() ? 0 : 1;
        Log("vrcam: native effects - %ux%u: %zu pictures in %zu files, read when wanted (the act's sky, the ceiling's stone) and kept across sizes",
            job.w, job.h, n, b->pics.size());
    }
    b->ok = true;
    b->ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return b;
}

// ---------------------------------------------------------------- the frame
void Runtime::Impl::WriteSemantics(Build& b) {
    for (Texture& t : b.textures) {
        if (t.info.semantic.empty()) continue;
        ID3D12Resource* res = nullptr;
        DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
        if (t.info.semantic == "COLOR") {
            res = color;
            fmt = Linear(colorDesc.Format);
        } else if (const auto it = semantics.find(t.info.semantic); it != semantics.end()) {
            res = it->second.res;
            fmt = it->second.fmt;
        }
        if (t.boundOnce && res == t.boundRes && fmt == t.boundFmt) continue;
        t.boundOnce = true;
        t.boundRes = res;
        t.boundFmt = fmt;
        D3D12_SHADER_RESOURCE_VIEW_DESC v{};
        v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        v.Texture2D.MipLevels = 1;
        v.Format = res ? fmt : DXGI_FORMAT_R8G8B8A8_UNORM;
        dev->CreateShaderResourceView(res, &v, b.Srv(t.srv));
        v.Format = res ? Srgb(fmt) : DXGI_FORMAT_R8G8B8A8_UNORM;
        dev->CreateShaderResourceView(res, &v, b.Srv(t.srvSrgb));
    }
}

bool Runtime::Impl::EnsureColor(ID3D12Device* d, const D3D12_RESOURCE_DESC& target) {
    const DXGI_FORMAT want = ResourceFormat(Linear(target.Format));
    // (colorDesc.Format keeps the target's own format, the views'; compared with `want`, the typeless one, it never matched: COLOR, a
    // full-screen texture, was made again every present - 0.36 ms of the CPU here, a new committed resource cleared by the system and
    // the old one retired each frame; PC2, GTX 1050 Ti: the sky and the light fog cost 72 -> 60 fps, 2026-10-27)
    if (color && colorDesc.Width == target.Width && colorDesc.Height == target.Height && colorDesc.Format == target.Format) return true;
    ++stats.colorsMade;
    if (color) oldColors.push_back({color, kRetireFrames});
    color = nullptr;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = target.Width;
    rd.Height = target.Height;
    rd.DepthOrArraySize = rd.MipLevels = 1;
    rd.Format = want;
    rd.SampleDesc.Count = 1;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (FAILED(d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, kRead, nullptr, IID_PPV_ARGS(&color)))) {
        color = nullptr;
        return false;
    }
    color->SetName(L"d2rvr fx COLOR");
    colorDesc = rd;
    colorDesc.Format = target.Format;   // (the view's: the target's own)
    return true;
}

void Runtime::Impl::Uploads(Build& b, ID3D12GraphicsCommandList* l) {
    for (Texture& t : b.textures) {
        if (!t.res) continue;
        if (!t.pixels.empty()) {
            const D3D12_RESOURCE_DESC rd = t.res->GetDesc();
            const UINT n = rd.MipLevels;
            std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> fp(n);
            std::vector<UINT> rows(n);
            std::vector<UINT64> rowBytes(n);
            UINT64 total = 0;
            dev->GetCopyableFootprints(&rd, 0, n, 0, fp.data(), rows.data(), rowBytes.data(), &total);
            Release(t.upload);
            D3D12_HEAP_PROPERTIES hp{};
            hp.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC bd{};
            bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            bd.Width = total;
            bd.Height = bd.DepthOrArraySize = bd.MipLevels = 1;
            bd.SampleDesc.Count = 1;
            bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            uint8_t* map = nullptr;
            if (SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&t.upload))) &&
                SUCCEEDED(t.upload->Map(0, nullptr, (void**)&map))) {
                size_t at = 0;
                for (UINT i = 0; i < n && at < t.pixels.size(); ++i) {
                    const size_t tight = (size_t)fp[i].Footprint.Width * 4;
                    for (UINT y = 0; y < rows[i] && at + tight <= t.pixels.size(); ++y, at += tight)
                        memcpy(map + fp[i].Offset + (size_t)y * fp[i].Footprint.RowPitch, t.pixels.data() + at, tight);
                }
                t.upload->Unmap(0, nullptr);
                D3D12_RESOURCE_BARRIER br = Tr(t.res, t.state, D3D12_RESOURCE_STATE_COPY_DEST);
                l->ResourceBarrier(1, &br);
                for (UINT i = 0; i < n; ++i) {
                    D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
                    dst.pResource = t.res;
                    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    dst.SubresourceIndex = i;
                    src.pResource = t.upload;
                    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    src.PlacedFootprint = fp[i];
                    l->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                }
                br = Tr(t.res, D3D12_RESOURCE_STATE_COPY_DEST, t.state);
                l->ResourceBarrier(1, &br);
            }
            t.pixels.clear();
            t.pixels.shrink_to_fit();
            b.uploadsLeft = kRetireFrames;
        }
        if (t.clear) {   // render targets and storages start at zero
            t.clear = false;
            if (t.rtv != ~0u) {
                D3D12_RESOURCE_BARRIER br = Tr(t.res, t.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
                l->ResourceBarrier(1, &br);
                const float zero[4] = {};
                l->ClearRenderTargetView(b.Rtv(t.rtv), zero, 0, nullptr);
                br = Tr(t.res, D3D12_RESOURCE_STATE_RENDER_TARGET, t.state);
                l->ResourceBarrier(1, &br);
            } else if (t.uav != ~0u) {
                if (ringAt + 1 > kRing) ringAt = 0;
                D3D12_CPU_DESCRIPTOR_HANDLE c = ring->GetCPUDescriptorHandleForHeapStart();
                D3D12_GPU_DESCRIPTOR_HANDLE g = ring->GetGPUDescriptorHandleForHeapStart();
                c.ptr += (SIZE_T)ringAt * ringInc;
                g.ptr += (UINT64)ringAt * ringInc;
                ++ringAt;
                dev->CopyDescriptorsSimple(1, c, b.Srv(t.uav), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                D3D12_RESOURCE_BARRIER br = Tr(t.res, t.state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                l->ResourceBarrier(1, &br);
                const UINT zero[4] = {};
                ID3D12DescriptorHeap* heaps[] = {ring, b.samplers};
                l->SetDescriptorHeaps(2, heaps);
                l->ClearUnorderedAccessViewUint(g, b.Srv(t.uav), t.res, zero, 0, nullptr);
                br = Tr(t.res, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, t.state);
                l->ResourceBarrier(1, &br);
            }
        }
    }
    b.uploaded = true;
}

D3D12_GPU_VIRTUAL_ADDRESS Runtime::Impl::Constants(Build& b) {
    const auto now = std::chrono::steady_clock::now();
    const float timer = std::chrono::duration<float, std::milli>(now - start).count();
    const float frame = std::chrono::duration<float, std::milli>(now - lastFrame).count();
    lastFrame = now;
    for (int i : b.timers) SetData(b, b.module.uniforms[i], (const uint8_t*)&timer, 4, 0);
    for (int i : b.frameTimes) SetData(b, b.module.uniforms[i], (const uint8_t*)&frame, 4, 0);
    b.cbAt = (b.cbAt + 1) % kCbSlots;
    memcpy(b.cbMapped + (size_t)b.cbAt * b.cbStride, b.uniforms.data(), b.uniforms.size());
    return b.cb->GetGPUVirtualAddress() + (UINT64)b.cbAt * b.cbStride;
}

// a pass's sampler states (its own table: MakeObjects)
static D3D12_GPU_DESCRIPTOR_HANDLE SamplerTable(const Build& b, const Pass& p) {
    D3D12_GPU_DESCRIPTOR_HANDLE h = b.samplers->GetGPUDescriptorHandleForHeapStart();
    h.ptr += (UINT64)p.samplerTable * b.maxS * b.samplerInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE Runtime::Impl::Table(Build& b, const Pass& p, D3D12_GPU_DESCRIPTOR_HANDLE* uavTable) {
    const UINT n = b.maxT + b.maxU;
    if (ringAt + n > kRing) ringAt = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE c = ring->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE g = ring->GetGPUDescriptorHandleForHeapStart();
    c.ptr += (SIZE_T)ringAt * ringInc;
    g.ptr += (UINT64)ringAt * ringInc;
    ringAt += n;
    dev->CopyDescriptorsSimple(n, c, b.Srv(b.nullSrv), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);   // (the null SRVs and UAVs lie in a row)
    for (const Pass::Bind& x : p.srvs) {
        const Texture& t = b.textures[x.tex];
        D3D12_CPU_DESCRIPTOR_HANDLE d = c;
        d.ptr += (SIZE_T)x.slot * ringInc;
        dev->CopyDescriptorsSimple(1, d, b.Srv(x.srgb ? t.srvSrgb : t.srv), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    for (const Pass::Bind& x : p.uavs) {
        const Texture& t = b.textures[x.tex];
        if (t.uav == ~0u) continue;
        D3D12_CPU_DESCRIPTOR_HANDLE d = c;
        d.ptr += (SIZE_T)(b.maxT + x.slot) * ringInc;
        dev->CopyDescriptorsSimple(1, d, b.Srv(t.uav), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    *uavTable = g;
    uavTable->ptr += (UINT64)b.maxT * ringInc;
    return g;
}

UINT Runtime::Impl::RunTechnique(Build& b, Technique& t, ID3D12GraphicsCommandList* l, ID3D12Resource* target, D3D12_RESOURCE_STATES& targetState,
                                  D3D12_GPU_VIRTUAL_ADDRESS cb) {
    if (o.keepPass) {   // nothing of it to draw (the overlays apart with all their switches off, every present): not even the picture's copy
        bool any = false;
        for (const Pass& p : t.passes) any = any || o.keepPass(p.compute ? p.info.cs_entry_point.c_str() : p.info.ps_entry_point.c_str());
        if (!any) { stats.skipped += (uint32_t)t.passes.size(); return 0; }
    }
    auto to = [&](ID3D12Resource* r, D3D12_RESOURCE_STATES& s, D3D12_RESOURCE_STATES want) {
        if (s == want) return;
        D3D12_RESOURCE_BARRIER br = Tr(r, s, want);
        l->ResourceBarrier(1, &br);
        s = want;
    };
    D3D12_RESOURCE_STATES colorState = kRead;
    auto copyToColor = [&]() {   // COLOR = the target as it is now
        to(target, targetState, D3D12_RESOURCE_STATE_COPY_SOURCE);
        to(color, colorState, D3D12_RESOURCE_STATE_COPY_DEST);
        l->CopyResource(color, target);
        to(color, colorState, kRead);
    };
    copyToColor();
    // the target's views: linear and sRGB (its own format: a back buffer is made typed, so the sRGB one only where the format is typeless)
    D3D12_CPU_DESCRIPTOR_HANDLE trtv[2] = {targetRtv->GetCPUDescriptorHandleForHeapStart(), targetRtv->GetCPUDescriptorHandleForHeapStart()};
    trtv[1].ptr += targetRtvInc;
    {
        D3D12_RENDER_TARGET_VIEW_DESC r{};
        r.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        r.Format = Linear(colorDesc.Format);
        dev->CreateRenderTargetView(target, &r, trtv[0]);
        dev->CreateRenderTargetView(target, &r, trtv[1]);
    }
    ID3D12DescriptorHeap* heaps[] = {ring, b.samplers};
    l->SetDescriptorHeaps(2, heaps);
    UINT drawn = 0;
    for (Pass& p : t.passes) {
        if (o.passLimit > 0 && drawn >= (UINT)o.passLimit) break;
        const bool timed = tsList && tsAt + 3 <= kTsPer;   // its time: the stamps before and after it (room left for the Render's last)
        if (timed) {
            tsSlots[tsFrame % kTsSlots].names.push_back(p.compute ? p.info.cs_entry_point : p.info.ps_entry_point);
            Stamp();
        }
        if (o.keepPass && !o.keepPass(p.compute ? p.info.cs_entry_point.c_str() : p.info.ps_entry_point.c_str())) {   // nothing this frame reads
            ++stats.skipped;
            if (timed) Stamp();
            continue;
        }
        D3D12_GPU_DESCRIPTOR_HANDLE uavT{};
        const D3D12_GPU_DESCRIPTOR_HANDLE srvT = Table(b, p, &uavT);
        if (p.compute) {
            for (const Pass::Bind& x : p.uavs) { Texture& tx = b.textures[x.tex]; if (tx.res) to(tx.res, tx.state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); }
            l->SetComputeRootSignature(b.root);
            l->SetComputeRootConstantBufferView(0, cb);
            l->SetComputeRootDescriptorTable(1, srvT);
            l->SetComputeRootDescriptorTable(2, SamplerTable(b, p));
            if (b.maxU) l->SetComputeRootDescriptorTable(3, uavT);
            l->SetPipelineState(p.pso);
            l->Dispatch(std::max(1u, p.info.viewport_width), std::max(1u, p.info.viewport_height), std::max(1u, p.info.viewport_dispatch_z));
            D3D12_RESOURCE_BARRIER uav{};
            uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            l->ResourceBarrier(1, &uav);
            for (const Pass::Bind& x : p.uavs) { Texture& tx = b.textures[x.tex]; if (tx.res) to(tx.res, tx.state, kRead); }
            ++drawn;
            if (timed) Stamp();
            continue;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE rtvs[8] = {};
        UINT n = 0;
        UINT vw = p.info.viewport_width, vh = p.info.viewport_height;
        if (p.rts.empty()) {
            to(target, targetState, D3D12_RESOURCE_STATE_RENDER_TARGET);
            rtvs[n++] = trtv[p.info.srgb_write_enable ? 1 : 0];
            if (!vw || !vh) { vw = (UINT)colorDesc.Width; vh = colorDesc.Height; }
        } else {
            for (int i : p.rts) {
                Texture& tx = b.textures[i];
                to(tx.res, tx.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
                rtvs[n++] = b.Rtv(p.info.srgb_write_enable ? tx.rtvSrgb : tx.rtv);
                if (!vw || !vh) { vw = tx.info.width; vh = tx.info.height; }
            }
        }
        l->OMSetRenderTargets(n, rtvs, FALSE, nullptr);
        if (p.info.clear_render_targets) {
            const float zero[4] = {};
            for (UINT i = 0; i < n; ++i) l->ClearRenderTargetView(rtvs[i], zero, 0, nullptr);
        }
        const D3D12_VIEWPORT vp{0.0f, 0.0f, (float)vw, (float)vh, 0.0f, 1.0f};
        const D3D12_RECT sc{0, 0, (LONG)vw, (LONG)vh};
        l->RSSetViewports(1, &vp);
        l->RSSetScissorRects(1, &sc);
        l->SetGraphicsRootSignature(b.root);
        l->SetGraphicsRootConstantBufferView(0, cb);
        l->SetGraphicsRootDescriptorTable(1, srvT);
        l->SetGraphicsRootDescriptorTable(2, SamplerTable(b, p));
        if (b.maxU) l->SetGraphicsRootDescriptorTable(3, uavT);
        l->SetPipelineState(p.pso);
        using pt = reshadefx::primitive_topology;
        l->IASetPrimitiveTopology(p.info.topology == pt::point_list ? D3D_PRIMITIVE_TOPOLOGY_POINTLIST
                                  : p.info.topology == pt::line_list ? D3D_PRIMITIVE_TOPOLOGY_LINELIST
                                  : p.info.topology == pt::line_strip ? D3D_PRIMITIVE_TOPOLOGY_LINESTRIP
                                  : p.info.topology == pt::triangle_strip ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        l->DrawInstanced(p.info.num_vertices, 1, 0, 0);
        ++drawn;
        if (timed) Stamp();
        for (int i : p.rts) { Texture& tx = b.textures[i]; to(tx.res, tx.state, kRead); }
        if (p.copyAfter) copyToColor();   // a later pass reads the target as it is now
    }
    return drawn;
}

bool Runtime::Impl::Frame(ID3D12Device* d, const D3D12_RESOURCE_DESC& target) {
    if (!dev) {
        dev = d;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = kRing;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(d->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&ring)))) { ring = nullptr; stats.failed = true; return false; }
        ringInc = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hd.NumDescriptors = 2;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(d->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&targetRtv)))) { targetRtv = nullptr; stats.failed = true; return false; }
        targetRtvInc = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }
    if (d != dev || !ring || !targetRtv) return false;
    for (auto it = retired.begin(); it != retired.end();) it = --it->second <= 0 ? retired.erase(it) : it + 1;
    if (cur && cur->uploadsLeft > 0 && --cur->uploadsLeft == 0)
        for (Texture& t : cur->textures) Release(t.upload);
    for (auto it = oldSemantics.begin(); it != oldSemantics.end();) {
        if (--it->second <= 0) { it->first->Release(); it = oldSemantics.erase(it); }
        else ++it;
    }
    for (auto it = oldColors.begin(); it != oldColors.end();) {
        if (--it->second <= 0) { it->first->Release(); it = oldColors.erase(it); }
        else ++it;
    }
    {   // a finished build takes over
        std::unique_ptr<Build> nb;
        {
            std::lock_guard<std::mutex> l(readyMx);
            nb = std::move(ready);
        }
        if (nb) {
            if (nb->ok) {
                Log("vrcam: native effects - D2R_DepthFog.fx built for %ux%u in %.1f s (%u shaders from the cache, %u compiled; %zu techniques, %zu "
                    "textures, %zu pipeline states)", nb->w, nb->h, nb->ms / 1000.0, nb->cached, nb->compiled, nb->techniques.size(), nb->textures.size(),
                    nb->psos.size());
                if (cur) {   // the old one's technique states go on (vrcam sets them each frame anyway)
                    for (Technique& t : nb->techniques)
                        for (const Technique& o2 : cur->techniques)
                            if (o2.name == t.name) t.enabled = o2.enabled;
                    retired.push_back({std::move(cur), kRetireFrames});
                }
                cur = std::move(nb);
                RememberSize(cur->w, cur->h);
                stats.lastCompileMs = cur->ms;
                stats.cached = cur->cached;
                stats.compiled = cur->compiled;
                if (o.reloaded) o.reloaded();
            } else {
                stats.failed = true;
                Log("vrcam: native effects - D2R_DepthFog.fx could not be built for %ux%u: %s", nb->w, nb->h, nb->error.substr(0, 700).c_str());
            }
        }
    }
    PicturesTick();   // the pictures wanted by the build in use: read, made, its views of them (a new build: of those already there at once)
    const DXGI_FORMAT fmt = Linear(target.Format);
    // The size in use first, then the sizes built before into the cache (vr/fx_build_order.h: they went first, and the game's own size waited
    // behind all of them - a minute of no sky at a new build's first start, PC1 2026-10-29)
    // (a size built this run - the one in use, or one built when the game took it - is in the cache already: not warmed again)
    auto builtThisRun = [&](const std::pair<uint32_t, uint32_t>& s) {
        return (s.first == target.Width && s.second == target.Height) || (cur && cur->w == s.first && cur->h == s.second);
    };
    while (preAt < warm.size() && builtThisRun(warm[preAt])) ++preAt;
    const bool want = !cur || cur->w != target.Width || cur->h != target.Height || cur->target != fmt || cur->gen != wantGen;
    const bool tried = triedGen == wantGen && triedW == target.Width && triedH == target.Height && triedFmt == fmt;
    switch (fxbuildorder::Next(busy.load(), want, tried, preAt < warm.size())) {
    case fxbuildorder::Job::InUse:
        stats.failed = false;
        Log("vrcam: native effects - building D2R_DepthFog.fx for %llux%u in the background", (unsigned long long)target.Width, target.Height);
        StartJob(d, (uint32_t)target.Width, target.Height, fmt);
        break;
    case fxbuildorder::Job::Warm: {
        const auto s = warm[preAt++];
        Log("vrcam: native effects - D2R_DepthFog.fx's shaders for %ux%u (built before) into the cache, in the background (the size in use is "
            "built)", s.first, s.second);
        StartJob(d, s.first, s.second, fmt, true);
        break;
    }
    case fxbuildorder::Job::None: break;
    }
    stats.compiling = busy.load();
    stats.ready = cur && cur->ok;
    if (!stats.ready) return false;
    if (cur->w != target.Width || cur->h != target.Height || cur->target != fmt) return false;   // the build for this size is not there yet
    return EnsureColor(d, target);
}

// The graphics card's time: the slot written kTsSlots Renders ago read (done by now: natived3d's ring keeps up to 4 presents), then this
// Render's stamps begin. A pass's time is its two stamps apart (the barriers around it partly in it); the Render's, its first and last.
void Runtime::Impl::TimingStart(ID3D12GraphicsCommandList* l) {
    tsList = nullptr;
    if (!tsHz || !dev) return;
    if (!tsHeap) {
        D3D12_QUERY_HEAP_DESC qd{};
        qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qd.Count = kTsSlots * kTsPer;
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = (UINT64)qd.Count * 8;
        rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(dev->CreateQueryHeap(&qd, IID_PPV_ARGS(&tsHeap))) ||
            FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tsRead))) ||
            FAILED(tsRead->Map(0, nullptr, (void**)&tsMap))) {
            Release(tsHeap);
            Release(tsRead);
            tsMap = nullptr;
            tsHz = 0;
            return;
        }
    }
    ++tsFrame;
    TsSlot& s = tsSlots[tsFrame % kTsSlots];
    if (s.used && s.n >= 2) {   // this slot's last Render, long done: its times
        const uint64_t* v = tsMap + (size_t)(tsFrame % kTsSlots) * kTsPer;
        const double k = 1000.0 / (double)tsHz;
        tsTotal += (double)(v[s.n - 1] - v[0]) * k;
        ++tsCount;
        for (size_t i = 0; i < s.names.size() && 2 + 2 * i + 1 < s.n; ++i) {
            auto& p = tsPass[s.names[i].empty() ? std::string("?") : s.names[i]];
            p.first += (double)(v[1 + 2 * i + 1] - v[1 + 2 * i]) * k;
            ++p.second;
        }
    }
    s = TsSlot{};
    s.used = true;
    tsList = l;
    tsAt = 0;
    Stamp();   // the Render's start
}
void Runtime::Impl::TimingEnd() {
    if (!tsList) return;
    Stamp();   // its end
    const UINT base = (tsFrame % kTsSlots) * kTsPer;
    tsSlots[tsFrame % kTsSlots].n = tsAt;
    tsList->ResolveQueryData(tsHeap, D3D12_QUERY_TYPE_TIMESTAMP, base, tsAt, tsRead, (UINT64)base * 8);
    tsList = nullptr;
}

// ---------------------------------------------------------------- Runtime
Runtime::Runtime(const Options& o) : m_(std::make_unique<Impl>()) {
    m_->o = o;
    m_->LoadSizes();
}
Runtime::~Runtime() = default;

bool Runtime::Begin(ID3D12Device* dev, ID3D12Resource* bb) {
    m_->begun = dev && bb && m_->Frame(dev, bb->GetDesc());
    return m_->begun;
}

bool Runtime::Render(ID3D12GraphicsCommandList* list, ID3D12Resource* bb) {
    if (!m_->begun || !list || !bb) return false;
    Build& b = *m_->cur;
    bool any = false;
    for (const Technique& t : b.techniques) any = any || t.enabled;
    if (!any && b.uploaded) return false;
    m_->WriteSemantics(b);
    if (!b.uploaded) m_->Uploads(b, list);
    m_->PictureUploads(list);
    m_->BindPictures(b);
    if (!any) return true;
    const D3D12_GPU_VIRTUAL_ADDRESS cb = m_->Constants(b);
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_PRESENT;
    m_->TimingStart(list);
    for (Technique& t : b.techniques)
        if (t.enabled) m_->stats.passes += m_->RunTechnique(b, t, list, bb, state, cb);
    if (state != D3D12_RESOURCE_STATE_PRESENT) {
        D3D12_RESOURCE_BARRIER br = Tr(bb, state, D3D12_RESOURCE_STATE_PRESENT);
        list->ResourceBarrier(1, &br);
    }
    m_->TimingEnd();
    ++m_->stats.renders;
    return true;
}

bool Runtime::RenderTechnique(reshade::api::effect_technique tech, ID3D12GraphicsCommandList* list, ID3D12Resource* target, bool backBuffer) {
    if (!m_->begun || !list || !target || !tech.handle) return false;
    const D3D12_RESOURCE_DESC td = target->GetDesc();
    if (td.Width != m_->colorDesc.Width || td.Height != m_->colorDesc.Height) return false;   // (COLOR is the back buffer's size)
    Build& b = *m_->cur;
    if (tech.handle > b.techniques.size()) return false;
    m_->WriteSemantics(b);
    if (!b.uploaded) m_->Uploads(b, list);
    m_->PictureUploads(list);
    m_->BindPictures(b);
    const D3D12_GPU_VIRTUAL_ADDRESS cb = m_->Constants(b);
    const D3D12_RESOURCE_STATES entry = backBuffer ? D3D12_RESOURCE_STATE_PRESENT : D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES state = entry;
    m_->stats.passes += m_->RunTechnique(b, b.techniques[tech.handle - 1], list, target, state, cb);
    if (state != entry) {
        D3D12_RESOURCE_BARRIER br = Tr(target, state, entry);
        list->ResourceBarrier(1, &br);
    }
    ++m_->stats.techniques;
    return true;
}

void Runtime::BindSemantic(const char* semantic, ID3D12Resource* res, DXGI_FORMAT format) {
    Impl::Sem& s = m_->semantics[semantic];
    if (s.res == res) { s.fmt = format; return; }
    if (res) res->AddRef();
    if (s.res) m_->oldSemantics.push_back({s.res, kRetireFrames});
    s = {res, format};
}
void Runtime::SetTimestampFrequency(uint64_t hz) { m_->tsHz = hz; }

Stats Runtime::TakeStats() {
    Stats s = m_->stats;
    if (m_->cur) { s.width = m_->cur->w; s.height = m_->cur->h; }
    m_->stats.passes = m_->stats.renders = m_->stats.techniques = m_->stats.skipped = 0;
    if (m_->tsCount) {   // the averages, the dearest passes first (the entry points' names without the compiler's F__ / E__)
        s.timed = m_->tsCount;
        s.gpuMs = m_->tsTotal / m_->tsCount;
        struct Row { double ms; std::string name; double whenRun; uint32_t share; };
        std::vector<Row> v;
        for (const auto& p : m_->tsPass)
            v.push_back({p.second.first / m_->tsCount, p.first.size() > 3 && p.first[1] == '_' && p.first[2] == '_' ? p.first.substr(3) : p.first,
                         p.second.second ? p.second.first / p.second.second : 0.0, (uint32_t)(100.0 * p.second.second / m_->tsCount + 0.5)});
        std::sort(v.begin(), v.end(), [](const Row& a, const Row& c) { return a.ms > c.ms; });
        char b[96];
        for (size_t i = 0; i < v.size() && i < 8; ++i) {
            if (v[i].share < 95) snprintf(b, sizeof b, "%s%s %.2f (%.2f in %u%%)", i ? ", " : "", v[i].name.c_str(), v[i].ms, v[i].whenRun, v[i].share);
            else snprintf(b, sizeof b, "%s%s %.2f", i ? ", " : "", v[i].name.c_str(), v[i].ms);
            s.dearest += b;
        }
    }
    m_->tsPass.clear();
    m_->tsTotal = 0.0;
    m_->tsCount = 0;
    return s;
}

reshade::api::effect_uniform_variable Runtime::find_uniform_variable(const char*, const char* name) const {
    if (!m_->cur || !name) return {0};
    const auto it = m_->cur->uniformByName.find(name);
    return {it == m_->cur->uniformByName.end() ? 0 : (uint64_t)it->second + 1};
}
#define FXN_UNIFORM(var) \
    if (!m_->cur || !(var).handle || (var).handle > m_->cur->module.uniforms.size()) return; \
    const reshadefx::uniform& u = m_->cur->module.uniforms[(size_t)(var).handle - 1];
void Runtime::get_uniform_value_bool(reshade::api::effect_uniform_variable v, bool* values, size_t count, size_t arrayIndex) const {
    FXN_UNIFORM(v)
    uint32_t data[64] = {};
    count = std::min<size_t>({count, u.size / 4, 64});
    GetData(*m_->cur, u, (uint8_t*)data, count * 4, arrayIndex);
    for (size_t i = 0; i < count; ++i) values[i] = u.type.is_floating_point() ? ((float*)data)[i] != 0.0f : data[i] != 0;
}
void Runtime::get_uniform_value_float(reshade::api::effect_uniform_variable v, float* values, size_t count, size_t arrayIndex) const {
    FXN_UNIFORM(v)
    if (u.type.is_floating_point()) { GetData(*m_->cur, u, (uint8_t*)values, count * 4, arrayIndex); return; }
    int32_t data[64] = {};
    count = std::min<size_t>({count, u.size / 4, 64});
    GetData(*m_->cur, u, (uint8_t*)data, count * 4, arrayIndex);
    for (size_t i = 0; i < count; ++i) values[i] = u.type.is_signed() ? (float)data[i] : (float)(uint32_t)data[i];
}
void Runtime::get_uniform_value_int(reshade::api::effect_uniform_variable v, int32_t* values, size_t count, size_t arrayIndex) const {
    FXN_UNIFORM(v)
    if (u.type.is_integral()) { GetData(*m_->cur, u, (uint8_t*)values, count * 4, arrayIndex); return; }
    float data[64] = {};
    count = std::min<size_t>({count, u.size / 4, 64});
    GetData(*m_->cur, u, (uint8_t*)data, count * 4, arrayIndex);
    for (size_t i = 0; i < count; ++i) values[i] = (int32_t)data[i];
}
void Runtime::get_uniform_value_uint(reshade::api::effect_uniform_variable v, uint32_t* values, size_t count, size_t arrayIndex) const {
    get_uniform_value_int(v, (int32_t*)values, count, arrayIndex);
}
void Runtime::set_uniform_value_bool(reshade::api::effect_uniform_variable v, const bool* values, size_t count, size_t arrayIndex) {
    FXN_UNIFORM(v)
    uint32_t data[64] = {};
    count = std::min<size_t>(count, 64);
    for (size_t i = 0; i < count; ++i) {
        if (u.type.is_floating_point()) { const float f = values[i] ? 1.0f : 0.0f; memcpy(&data[i], &f, 4); }
        else data[i] = values[i] ? 1u : 0u;
    }
    SetData(*m_->cur, u, (const uint8_t*)data, count * 4, arrayIndex);
}
void Runtime::set_uniform_value_float(reshade::api::effect_uniform_variable v, const float* values, size_t count, size_t arrayIndex) {
    FXN_UNIFORM(v)
    if (u.type.is_floating_point()) { SetData(*m_->cur, u, (const uint8_t*)values, count * 4, arrayIndex); return; }
    int32_t data[64] = {};
    count = std::min<size_t>(count, 64);
    for (size_t i = 0; i < count; ++i) data[i] = (int32_t)values[i];
    SetData(*m_->cur, u, (const uint8_t*)data, count * 4, arrayIndex);
}
void Runtime::set_uniform_value_int(reshade::api::effect_uniform_variable v, const int32_t* values, size_t count, size_t arrayIndex) {
    FXN_UNIFORM(v)
    if (!u.type.is_floating_point()) { SetData(*m_->cur, u, (const uint8_t*)values, count * 4, arrayIndex); return; }
    float data[64] = {};
    count = std::min<size_t>(count, 64);
    for (size_t i = 0; i < count; ++i) data[i] = (float)values[i];
    SetData(*m_->cur, u, (const uint8_t*)data, count * 4, arrayIndex);
}
void Runtime::set_uniform_value_uint(reshade::api::effect_uniform_variable v, const uint32_t* values, size_t count, size_t arrayIndex) {
    FXN_UNIFORM(v)
    if (!u.type.is_floating_point()) { SetData(*m_->cur, u, (const uint8_t*)values, count * 4, arrayIndex); return; }
    float data[64] = {};
    count = std::min<size_t>(count, 64);
    for (size_t i = 0; i < count; ++i) data[i] = (float)values[i];
    SetData(*m_->cur, u, (const uint8_t*)data, count * 4, arrayIndex);
}
#undef FXN_UNIFORM

reshade::api::effect_technique Runtime::find_technique(const char*, const char* name) {
    if (!m_->cur || !name) return {0};
    for (size_t i = 0; i < m_->cur->techniques.size(); ++i)
        if (m_->cur->techniques[i].name == name) return {(uint64_t)i + 1};
    return {0};
}
bool Runtime::get_technique_state(reshade::api::effect_technique t) const {
    return m_->cur && t.handle && t.handle <= m_->cur->techniques.size() && m_->cur->techniques[(size_t)t.handle - 1].enabled;
}
void Runtime::set_technique_state(reshade::api::effect_technique t, bool enabled) {
    if (m_->cur && t.handle && t.handle <= m_->cur->techniques.size()) m_->cur->techniques[(size_t)t.handle - 1].enabled = enabled;
}
reshade::api::effect_texture_variable Runtime::find_texture_variable(const char*, const char* name) const {
    if (!m_->cur || !name) return {0};
    const auto it = m_->cur->textureByName.find(name);
    return {it == m_->cur->textureByName.end() ? 0 : (uint64_t)it->second + 1};
}
void Runtime::update_texture(reshade::api::effect_texture_variable v, uint32_t width, uint32_t height, const void* pixels) {
    if (!m_->cur || !v.handle || v.handle > m_->cur->textures.size() || !pixels) return;
    Texture& t = m_->cur->textures[(size_t)v.handle - 1];
    if (!t.res || t.info.width != width || t.info.height != height || t.info.format != reshadefx::texture_format::rgba8) return;
    t.pixels.assign((const uint8_t*)pixels, (const uint8_t*)pixels + (size_t)width * height * 4);
    m_->cur->uploaded = false;   // (the next Render uploads it; mips past the first stay as they were)
}
void Runtime::update_texture_bindings(const char* semantic, reshade::api::resource_view srv, reshade::api::resource_view) {
    if (srv.handle && semantic && m_->o.viewResource) {   // a view the plugin's stand-in made (vr/native_rsapi.h): its texture bound
        DXGI_FORMAT f = DXGI_FORMAT_UNKNOWN;
        if (ID3D12Resource* r = m_->o.viewResource(srv.handle, &f)) {
            BindSemantic(semantic, r, f != DXGI_FORMAT_UNKNOWN ? f : r->GetDesc().Format);
            return;
        }
    }
    if (srv.handle && !m_->toldViews) {
        m_->toldViews = true;
        m_->Log("vrcam: native effects - a ReShade view bound to %s: not used without ReShade (the plugin binds its own)", semantic ? semantic : "?");
    }
}
void Runtime::get_texture_binding(reshade::api::effect_texture_variable, reshade::api::resource_view* srv, reshade::api::resource_view* srvSrgb) const {
    if (srv) *srv = {0};
    if (srvSrgb) *srvSrgb = {0};
}
bool Runtime::get_preprocessor_definition_for_effect(const char*, const char* name, char* value, size_t* size) const {
    const auto it = m_->defines.find(name ? name : "");
    if (it == m_->defines.end()) return false;
    if (value && size && *size) {
        const size_t n = std::min(*size - 1, it->second.size());
        memcpy(value, it->second.data(), n);
        value[n] = 0;
    }
    if (size) *size = it->second.size() + 1;
    return true;
}
void Runtime::set_preprocessor_definition_for_effect(const char*, const char* name, const char* value) {
    if (!name) return;
    const std::string v = value ? value : "";
    if (auto it = m_->defines.find(name); it != m_->defines.end() && it->second == v) return;
    m_->defines[name] = v;
    ++m_->wantGen;   // the next Render starts a build with it
}
void Runtime::get_screenshot_width_and_height(uint32_t* w, uint32_t* h) const {
    if (w) *w = (uint32_t)m_->colorDesc.Width;
    if (h) *h = m_->colorDesc.Height;
}

}  // namespace fxnative
