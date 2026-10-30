// vr/render/depth/depth_fix.cpp - the depth fix: the right eye's camera in the replayed lists and the depth range ([render] depth_fix_mode, camfix), the range at its source (depthsrc) (cut from vr/vrcam.cpp, docs/plan_modules.md M13).
// What it gives: vr/render/depth/depth.h. It sees vrcam.cpp's headers.
#include "../../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// The right eye's camera in the replayed lists. The game's per-object constants are
// the same for both eyes (cb_compare); the camera's are not. Its constant buffers are
// found as the left pass binds them: the native command lists' Set*RootSignature and
// Set*RootConstantBufferView are hooked; each (root signature, parameter) is judged
// once by its content (does it hold the left eye's view matrix, or the camera's
// position?), and the camera ones' GPU addresses are noted. The game's upload buffers
// are known from ReShade's init_resource (vrcam joins before the game makes its
// device), so a GPU address reads as CPU memory. After the left pass is done on the
// GPU those buffers are rewritten for the right eye - the view, its inverse, view *
// projection and its inverse, the camera position - and the lists run again.
namespace camfix {
using namespace reshade::api;
struct Upload { uint64_t va, size; uint8_t* cpu; uint64_t res; };
std::vector<Upload> g_uploads;     // sorted by va
SRWLOCK g_upLock = SRWLOCK_INIT;
std::atomic<bool> g_capture{false};
std::atomic<uint32_t> g_patched{0};
std::atomic<uint32_t> g_nNote{0}, g_nNoRS{0}, g_nNoCpu{0}, g_nJudged{0}, g_nUploads{0}, g_nInit{0};

void OnInitResource(device* dev, const resource_desc& d, const subresource_data*, resource_usage, resource res) {
    if (d.type == resource_type::buffer) g_nInit.fetch_add(1);
    if (d.type != resource_type::buffer || d.heap != memory_heap::cpu_to_gpu) return;
    g_nUploads.fetch_add(1);
    ID3D12Resource* r = (ID3D12Resource*)res.handle;
    void* cpu = nullptr;
    if (!dev->map_buffer_region(res, 0, UINT64_MAX, map_access::write_only, &cpu) || !cpu) return;
    const Upload u{r->GetGPUVirtualAddress(), d.buffer.size, (uint8_t*)cpu, res.handle};
    AcquireSRWLockExclusive(&g_upLock);
    g_uploads.insert(std::upper_bound(g_uploads.begin(), g_uploads.end(), u, [](const Upload& a, const Upload& b) { return a.va < b.va; }), u);
    ReleaseSRWLockExclusive(&g_upLock);
}
void OnDestroyResource(device*, resource res) {
    AcquireSRWLockExclusive(&g_upLock);
    for (size_t i = 0; i < g_uploads.size(); ++i)
        if (g_uploads[i].res == res.handle) { g_uploads.erase(g_uploads.begin() + i); break; }
    ReleaseSRWLockExclusive(&g_upLock);
}
// CPU memory of a GPU address in an upload buffer, and how many bytes follow it there.
uint8_t* Cpu(uint64_t va, uint64_t* room) {
    AcquireSRWLockShared(&g_upLock);
    uint8_t* out = nullptr;
    auto it = std::upper_bound(g_uploads.begin(), g_uploads.end(), va, [](uint64_t v, const Upload& u) { return v < u.va; });
    if (it != g_uploads.begin()) {
        --it;
        if (va < it->va + it->size) { out = it->cpu + (va - it->va); *room = it->va + it->size - va; }
    }
    ReleaseSRWLockShared(&g_upLock);
    return out;
}

// the left view (row-major, v*M) and the right one: the half-eye shift taken twice off x
void Views(double L[16], double R[16]) {
    float v[16];
    AcquireSRWLockShared(&g_leftViewLock); memcpy(v, g_leftView, sizeof v); ReleaseSRWLockShared(&g_leftViewLock);
    for (int i = 0; i < 16; ++i) L[i] = R[i] = v[i];
    R[12] = L[12] - 2.0 * g_eyeHalf.load();
}
bool Inv4(const double M[16], double out[16]) {   // Gauss-Jordan, partial pivoting
    double a[4][8];
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) { a[r][c] = M[r * 4 + c]; a[r][4 + c] = r == c ? 1.0 : 0.0; }
    for (int c = 0; c < 4; ++c) {
        int piv = c;
        for (int r = c + 1; r < 4; ++r) if (fabs(a[r][c]) > fabs(a[piv][c])) piv = r;
        if (fabs(a[piv][c]) < 1e-12) return false;
        if (piv != c) for (int k = 0; k < 8; ++k) std::swap(a[c][k], a[piv][k]);
        const double d = 1.0 / a[c][c];
        for (int k = 0; k < 8; ++k) a[c][k] *= d;
        for (int r = 0; r < 4; ++r) {
            if (r == c || a[r][c] == 0.0) continue;
            const double m = a[r][c];
            for (int k = 0; k < 8; ++k) a[r][k] -= m * a[c][k];
        }
    }
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) out[r * 4 + c] = a[r][4 + c];
    return true;
}
void InvRigid(const double V[16], double out[16]) {   // rotation transposed, translation -t*R^T
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) out[r * 4 + c] = V[c * 4 + r];
    out[3] = out[7] = out[11] = 0.0;
    for (int c = 0; c < 3; ++c) out[12 + c] = -(V[12] * out[c] + V[13] * out[4 + c] + V[14] * out[8 + c]);
    out[15] = 1.0;
}
void Mul(const double A[16], const double B[16], double out[16]) {
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) {
        double s = 0.0;
        for (int k = 0; k < 4; ++k) s += A[r * 4 + k] * B[k * 4 + c];
        out[r * 4 + c] = s;
    }
}
bool Near(const float* f, const double* m, int n, double rel, double abs_) {
    for (int i = 0; i < n; ++i) if (fabs(f[i] - m[i]) > abs_ + rel * fabs(m[i])) return false;
    return true;
}
void Put(float* f, const double* m, int n) { for (int i = 0; i < n; ++i) f[i] = (float)m[i]; }

// Where a camera buffer keeps the view: learned once per (root signature, parameter)
// against the left view vrcam built (loosely - that one may have moved on with the
// head since), then rewritten from the buffer's own view each time.
struct Layout {
    // view matrices (row-major, v*M) at these words: D2R's camera buffers keep five - four camera
    // records of 192 words (20, 212, 404, 596) and one more at 788 (two passes: all five move for
    // the right eye; with four looked for the fifth, which the shadows use, stayed the left one's)
    int nv = 0, vo[8] = {};
    uint8_t parts[8] = {};         // 2 its inverse follows, 4 view*projection at +64 (projection at +32), 8 its inverse at +80
    int nc = 0, co[4] = {};        // the camera position on its own
};
bool Rigid(const float* m) {   // rows 0-2 a rotation, column 3 = (0,0,0,1)
    for (int r = 0; r < 3; ++r) {
        double len = 0.0;
        for (int c = 0; c < 3; ++c) len += (double)m[r * 4 + c] * m[r * 4 + c];
        if (fabs(len - 1.0) > 1e-3 || fabs(m[r * 4 + 3]) > 1e-6) return false;
    }
    return fabs(m[15] - 1.0f) < 1e-6;
}
// The view and what goes with it, judged against the left view L: rotation within 0.05,
// translation within 2 units (the head may have turned a little since L was built).
Layout Learn(const float* f, int n, const double L[16]) {
    Layout lay;
    double iL[16];
    InvRigid(L, iL);
    std::vector<int> covered;
    for (int o = 0; o + 16 <= n && lay.nv < 8; ++o) {
        if (!Rigid(f + o) || !Near(f + o, L, 12, 0.0, 0.05) || !Near(f + o + 12, L + 12, 3, 0.0, 2.0)) continue;
        double V[16], iV[16];
        for (int i = 0; i < 16; ++i) V[i] = f[o + i];
        InvRigid(V, iV);
        uint8_t parts = 0;
        if (o + 32 <= n && Near(f + o + 16, iV, 16, 1e-5, 2e-2)) parts |= 2;
        if (o + 96 <= n) {
            double P[16], Pi[16], vp[16], ivp[16];
            for (int i = 0; i < 16; ++i) { P[i] = f[o + 32 + i]; Pi[i] = f[o + 48 + i]; }
            Mul(V, P, vp);
            if (Near(f + o + 64, vp, 16, 1e-4, 1e-2)) {
                parts |= 4;
                Mul(Pi, iV, ivp);
                if (Near(f + o + 80, ivp, 16, 1e-3, 1e-1)) parts |= 8;
            }
        }
        lay.vo[lay.nv] = o; lay.parts[lay.nv] = parts; ++lay.nv;
        covered.push_back(o);
        if (parts & 2) covered.push_back(o + 16);
        o += 15;
    }
    for (int o = 0; o + 3 <= n && lay.nc < 4; ++o) {
        if (!Near(f + o, iL + 12, 3, 0.0, 0.25) || fabs(f[o]) < 10.0f) continue;   // a position out in the world, not a small number
        bool inside = false;
        for (int c : covered) if (o >= c && o < c + 16) inside = true;
        if (inside) continue;
        lay.co[lay.nc++] = o;
        o += 2;
    }
    return lay;
}
// The right eye's values over the left eye's, by the learned layout: the buffer's own
// view moved by the eye separation along its x (vrcam's half-eye shift, twice).
// prev (row-major) moved by the right eye's frustum: prev * inverse(P) * P_right, P_right = the
// fold's T(-shift) * P with the other off-axis shift. False if P is not ours.
bool PrevMoved(const float* f, int o, double shift, double out[16]) {
    const double sx = g_projSx.load();
    if (sx == 0.0 || fabs(f[o + 32] - sx) > 1e-3 * fabs(sx)) return false;
    double P[16], Pr[16], Pi[16], prev[16], C[16];
    for (int i = 0; i < 16; ++i) { P[i] = f[o + 32 + i]; prev[i] = f[o + 96 + i]; }
    for (int i = 0; i < 16; ++i) Pr[i] = P[i];
    Pr[8] -= 2.0 * g_projShift.load();
    for (int c = 0; c < 4; ++c) Pr[12 + c] = Pr[12 + c] - shift * Pr[c];
    if (!Inv4(P, Pi)) return false;
    Mul(Pi, Pr, C);
    Mul(prev, C, out);
    for (int i = 0; i < 16; ++i) if (!std::isfinite(out[i])) return false;
    return true;
}
// Both eyes' staged values (see g_replayPrev): each camera record's last-frame view*projection
// made the right eye's.
void PrevToRightEye(float* f, int n, const Layout& lay, double shift) {
    if (!g_replayPrev.load()) return;
    const int mask = g_viewMask.load() ? g_viewMask.load() : (1 << g_replayViews.load()) - 1;
    for (int k = 0; k < lay.nv; ++k) {
        if (!(mask & (1 << k)) || !(lay.parts[k] & 4)) continue;
        const int o = lay.vo[k];
        if (o + 112 > n) continue;
        double out[16];
        if (PrevMoved(f, o, shift, out)) Put(f + o + 96, out, 16);
    }
}
int PatchKnown(float* f, int n, const Layout& lay, double shift, const double right[3]) {
    int done = 0;
    double ax[3] = {right[0], right[1], right[2]};
    const int mask = g_viewMask.load() ? g_viewMask.load() : (1 << g_replayViews.load()) - 1;
    for (int k = 0; k < lay.nv; ++k) {
        if (!(mask & (1 << k))) continue;
        // Folded (replay_proj 3), a view with no projection beside it stays the left one: what reads
        // it pairs it with a projection that carries the eye's shift already (D2R's view at word
        // 980 moved too put the right eye's shading off by a second eye's shift, 2026-10-10).
        if (g_projFix.load() == 3 && !(lay.parts[k] & 4)) continue;
        const int o = lay.vo[k];
        if (o + 16 > n || !Rigid(f + o)) continue;
        double V[16], R[16], iV[16], iR[16];
        for (int i = 0; i < 16; ++i) R[i] = V[i] = f[o + i];
        ax[0] = V[0]; ax[1] = V[4]; ax[2] = V[8];   // the camera's right, in the world
        // Folded (replay_proj 3): the view stays the left one, the projection takes the shift.
        bool fold = false;
        if (g_projFix.load() == 3 && (lay.parts[k] & 4) && o + 80 <= n) {
            const double sx = g_projSx.load();
            fold = sx != 0.0 && fabs(f[o + 32] - sx) < 1e-3 * fabs(sx);
        }
        {   // which way each view goes, once per (layout, view, way)
            static std::vector<std::pair<int, int>> told;
            static SRWLOCK tl = SRWLOCK_INIT;
            const int way = fold ? 1 : 2;
            const int pk = o + 48 <= n ? (int)(f[o + 32] * 1000.0f) : -1;
            AcquireSRWLockExclusive(&tl);
            bool seen = false;
            for (const auto& t : told) if (t.first == pk && t.second == o * 4 + way) seen = true;
            // full: no more lines - native OpenXR's projection moves a little every frame, and a key
            // per frame filled the list and then logged every view of every pair (2026-10-10)
            if (!seen) { if (told.size() < 200) told.push_back({pk, o * 4 + way}); else seen = true; }
            ReleaseSRWLockExclusive(&tl);
            if (!seen) LogF("vrcam: replay - view at word %d (parts %u) %s: P %.4f %.4f %.4f %.4f / %.4f %.4f (ours x %.4f)", o, lay.parts[k],
                            fold ? "FOLDED" : "MOVED", o + 48 <= n ? f[o + 32] : 0.f, o + 48 <= n ? f[o + 37] : 0.f,
                            o + 48 <= n ? f[o + 40] : 0.f, o + 48 <= n ? f[o + 41] : 0.f, o + 48 <= n ? f[o + 43] : 0.f,
                            o + 48 <= n ? f[o + 46] : 0.f, g_projSx.load());
        }
        // A view whose projection is not the eye's (orthographic, P 0.006 - 1.0: the shadow cascades'
        // light cameras, turned nearly as the eye is and so taken for it) is no eye camera: moved,
        // the right eye's shadow maps were drawn from a light shifted by the eyes' distance and its
        // shadows lay where the left eye's were (2026-10-10). Left alone, in every mode.
        if ((lay.parts[k] & 4) && o + 48 <= n) {
            const double sx = g_projSx.load();
            if (sx == 0.0 || fabs(f[o + 32] - sx) > 1e-3 * fabs(sx) || f[o + 43] == 0.0f) continue;
        }
        if (fold) {
            double P[16], Pr[16], Pi[16], vp[16], ivp[16];
            for (int i = 0; i < 16; ++i) P[i] = f[o + 32 + i];
            P[8] -= 2.0 * g_projShift.load();   // the right eye's own off-axis frustum
            // T * P, T moving x by -shift in view space (row-major, v*M): only row 3 changes
            for (int i = 0; i < 16; ++i) Pr[i] = P[i];
            for (int c = 0; c < 4; ++c) Pr[12 + c] = P[12 + c] - shift * P[c];
            bool piOk = Inv4(Pr, Pi);
            for (int i = 0; i < 16 && piOk; ++i) piOk = std::isfinite(Pi[i]);
            Put(f + o + 32, Pr, 16);
            if (piOk) Put(f + o + 48, Pi, 16);
            Mul(V, Pr, vp);
            Put(f + o + 64, vp, 16);
            if ((lay.parts[k] & 8) && o + 96 <= n && piOk) {
                InvRigid(V, iV);
                Mul(Pi, iV, ivp);
                Put(f + o + 80, ivp, 16);
            }
            ++done;
            g_nFold.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        g_nView.fetch_add(1, std::memory_order_relaxed);
        R[12] -= shift;
        InvRigid(V, iV); InvRigid(R, iR);
        Put(f + o, R, 16);
        if ((lay.parts[k] & 2) && o + 32 <= n) Put(f + o + 16, iR, 16);
        if ((lay.parts[k] & 4) && o + 80 <= n) {
            double P[16], vp[16];
            for (int i = 0; i < 16; ++i) P[i] = f[o + 32 + i];
            // The right eye's own off-axis frustum: the left pass's projection (ours - its x
            // scale is vrcam's) shifted the other way. Without it the right eye had the left
            // eye's shift: parallel eyes, the world in front of the screen ("mono").
            double Pi[16];
            bool piOk = false;
            const double ps = g_projShift.load(), sx = g_projSx.load();
            const int fix = g_projFix.load();
            if (fix && ps != 0.0 && sx != 0.0 && fabs(P[0] - sx) < 1e-3 * fabs(sx)) {
                P[8] -= 2.0 * ps;
                Put(f + o + 32, P, 16);
                if (fix == 1) {
                    piOk = Inv4(P, Pi);
                    for (int i = 0; i < 16 && piOk; ++i) piOk = std::isfinite(Pi[i]);
                    if (piOk) Put(f + o + 48, Pi, 16);
                }
            }
            Mul(R, P, vp);
            Put(f + o + 64, vp, 16);
            if ((lay.parts[k] & 8) && o + 96 <= n) {
                double ivp[16];
                if (!piOk) for (int i = 0; i < 16; ++i) Pi[i] = f[o + 48 + i];
                Mul(Pi, iR, ivp);
                Put(f + o + 80, ivp, 16);
            }
        }
        ++done;
    }
    const int posMode = g_replayPositions.load();
    for (int k = 0; k < (posMode == 1 || (posMode == 2 && lay.nc == 1) ? lay.nc : 0); ++k) {
        const int o = lay.co[k];
        if (o + 3 > n) continue;
        for (int i = 0; i < 3; ++i) f[o + i] = (float)(f[o + i] + ax[i] * shift);
        g_nPos.fetch_add(1, std::memory_order_relaxed);
        ++done;
    }
    return done;
}

// One constant buffer, in place (words; n of them): returns what it found.
int Patch(float* f, int n, const double L[16], const double R[16]) {
    double iL[16], iR[16];
    InvRigid(L, iL); InvRigid(R, iR);
    int found = 0;
    std::vector<int> covered;
    for (int o = 0; o + 16 <= n; ++o) {
        if (!Near(f + o, L, 16, 1e-5, 1e-3)) continue;
        Put(f + o, R, 16); found |= 1; covered.push_back(o);
        if (o + 32 <= n && Near(f + o + 16, iL, 16, 1e-5, 2e-2)) { Put(f + o + 16, iR, 16); found |= 2; covered.push_back(o + 16); }
        if (o + 96 <= n) {
            double P[16], Pi[16], vpL[16], vpR[16], ivpL[16], ivpR[16];
            for (int i = 0; i < 16; ++i) { P[i] = f[o + 32 + i]; Pi[i] = f[o + 48 + i]; }
            Mul(L, P, vpL); Mul(R, P, vpR);
            if (Near(f + o + 64, vpL, 16, 1e-4, 1e-2)) {
                Put(f + o + 64, vpR, 16); found |= 4;
                Mul(Pi, iL, ivpL); Mul(Pi, iR, ivpR);
                if (Near(f + o + 80, ivpL, 16, 1e-3, 1e-1)) { Put(f + o + 80, ivpR, 16); found |= 8; }
            }
        }
        o += 15;
    }
    for (int o = 0; o + 3 <= n; ++o) {   // the camera position on its own (not inside a matrix just written)
        if (!Near(f + o, iL + 12, 3, 1e-6, 1e-3)) continue;
        bool inside = false;
        for (int c : covered) if (o >= c && o < c + 16) inside = true;
        if (inside) continue;
        Put(f + o, iR + 12, 3); found |= 16;
        o += 2;
    }
    return found;
}

// Native command list hooks: the current root signature per list, the camera bindings.
using SetRSFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12RootSignature*);
using SetCBVFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
// D3D12 gives each command list type its own vtable (its own functions): the direct
// lists' and the compute lists' are hooked, each with its own trampolines (slot).
SetRSFn OrigSetGfxRS[2] = {}, OrigSetCompRS[2] = {};
SetCBVFn OrigSetGfxCBV[2] = {}, OrigSetCompCBV[2] = {};
thread_local ID3D12GraphicsCommandList* t_list = nullptr;
thread_local ID3D12RootSignature* t_rs[2] = {};
struct Key { ID3D12RootSignature* rs; UINT param; int verdict; Layout lay; };   // verdict 1 camera, 0 not
std::vector<Key> g_keys;
SRWLOCK g_keyLock = SRWLOCK_INIT;
struct Va { uint64_t va; Layout lay; };
std::vector<Va> g_vas;             // this left pass's camera buffers
SRWLOCK g_vaLock = SRWLOCK_INIT;

// Each thread's own cache of verdicts (31,000 root CBVs a pass: no lock on the way).
struct Cached { ID3D12RootSignature* rs; UINT param; int verdict; };
thread_local Cached t_cache[64] = {};
thread_local uint32_t t_cacheGen = 0;
extern std::atomic<uint32_t> g_keyGen;
int VerdictSlow(ID3D12RootSignature* rs, UINT param, uint64_t va);
inline int Verdict(ID3D12RootSignature* rs, UINT param, uint64_t va) {
    if (t_cacheGen != g_keyGen.load(std::memory_order_relaxed)) { t_cacheGen = g_keyGen.load(); for (Cached& x : t_cache) x = {}; }
    Cached& c = t_cache[(((uintptr_t)rs >> 4) ^ (param * 7u)) & 63];
    if (c.rs == rs && c.param == param) return c.verdict;
    const int v = VerdictSlow(rs, param, va);
    if (v >= 0) c = {rs, param, v};
    return v > 0 ? v : 0;
}
// -1: no verdict yet (not in an upload buffer this time)
int VerdictSlow(ID3D12RootSignature* rs, UINT param, uint64_t va) {
    AcquireSRWLockShared(&g_keyLock);
    int v = -1;
    for (size_t i = 0; i < g_keys.size(); ++i) if (g_keys[i].rs == rs && g_keys[i].param == param) { v = g_keys[i].verdict ? 1 + (int)i : 0; break; }
    ReleaseSRWLockShared(&g_keyLock);
    if (v >= 0) return v;
    uint64_t room = 0;
    uint8_t* cpu = Cpu(va, &room);
    if (!cpu) { g_nNoCpu.fetch_add(1); return -1; }   // not an upload buffer: unknown this time, asked again next time
    g_nJudged.fetch_add(1);
    // the whole slot's worth (4 KB): judged by the first 1 KB, a buffer with its view further on
    // (the right eye's shadows: one copy of the left view outside every slot) was no camera
    float f[1024];
    const int n = (int)std::min<uint64_t>(room, sizeof f) / 4;
    memcpy(f, cpu, n * 4);
    double L[16], R[16];
    Views(L, R);
    const Layout lay = Learn(f, n, L);
    const bool cam = lay.nv || lay.nc;
    AcquireSRWLockExclusive(&g_keyLock);
    g_keys.push_back({rs, param, cam ? 1 : 0, lay});
    v = cam ? (int)g_keys.size() : 0;   // 1 + its index
    ReleaseSRWLockExclusive(&g_keyLock);
    if (cam) {
        char vs[160] = "";
        for (int k = 0, at = 0; k < lay.nv && at < 150; ++k) at += snprintf(vs + at, sizeof vs - at, " %d(%u)", lay.vo[k], lay.parts[k]);
        LogF("vrcam: replay - camera constants: root signature %p parameter %u - %d view(s) at%s, %d position(s) at %d %d %d %d",
             (void*)rs, param, lay.nv, vs, lay.nc, lay.co[0], lay.co[1], lay.co[2], lay.co[3]);
    }
    return v;
}
uint64_t Redirect(uint64_t va, const Layout& lay, int key, bool* fresh);
int KeyCount(int key);
extern std::atomic<bool> g_redirect;
extern std::atomic<int> g_nSlots;
constexpr int kSlotsFwd = 256;
// A "camera" key whose buffers fill the slots is an object's (its position near the hero
// passed for the camera's): not a camera from now on, in every thread's cache too.
std::atomic<uint32_t> g_keyGen{0};
void Demote(int key) {
    AcquireSRWLockExclusive(&g_keyLock);
    if (g_keys[key - 1].verdict) {
        g_keys[key - 1].verdict = 0;
        g_keyGen.fetch_add(1);
        LogF("vrcam: replay - root signature %p parameter %u: too many buffers a pass - an object's, not the camera's",
             (void*)g_keys[key - 1].rs, g_keys[key - 1].param);
    }
    ReleaseSRWLockExclusive(&g_keyLock);
}
uint64_t Note(int which, ID3D12GraphicsCommandList* cl, UINT param, uint64_t va) {
    if (!g_capture.load(std::memory_order_relaxed) || !va) return 0;
    ID3D12RootSignature* rs = t_list == cl ? t_rs[which] : nullptr;
    if (!rs) return 0;
    const int key = Verdict(rs, param, va);
    if (!key) return 0;
    AcquireSRWLockShared(&g_keyLock);
    const Layout lay = g_keys[key - 1].lay;
    ReleaseSRWLockShared(&g_keyLock);
    if (g_redirect.load(std::memory_order_relaxed)) {
        bool fresh = false;
        const uint64_t to = Redirect(va, lay, key, &fresh);
        if (fresh && !lay.nv && KeyCount(key) > 24) Demote(key);   // a position-only key with a 25th buffer in a pass: an object's
        return to;
    }
    AcquireSRWLockExclusive(&g_vaLock);
    bool seen = false;
    for (const Va& x : g_vas) if (x.va == va) { seen = true; break; }
    if (!seen) g_vas.push_back({va, lay});
    ReleaseSRWLockExclusive(&g_vaLock);
    return 0;
}
template <int S> void STDMETHODCALLTYPE HookSetGfxRS(ID3D12GraphicsCommandList* cl, ID3D12RootSignature* rs) {
    if (t_list != cl) { t_list = cl; t_rs[1] = nullptr; }
    t_rs[0] = rs; OrigSetGfxRS[S](cl, rs);
}
template <int S> void STDMETHODCALLTYPE HookSetCompRS(ID3D12GraphicsCommandList* cl, ID3D12RootSignature* rs) {
    if (t_list != cl) { t_list = cl; t_rs[0] = nullptr; }
    t_rs[1] = rs; OrigSetCompRS[S](cl, rs);
}
// [render] depth_fix (uitrace.cpp): the game's linear depth range in its b10, ours - here, before Note
// copies the buffer into a camera slot; ReShade tells add-ons of a binding only after this hook ran,
// and the slot kept the game's range (the act 2 town's line came back with the replay, 2026-10-13).
void DepthRange(int which, ID3D12GraphicsCommandList* cl, UINT param, uint64_t va) {
    ID3D12RootSignature* rs = t_list == cl ? t_rs[which] : nullptr;
    if (!rs || !va || !uitrace::DepthFixFor((uint64_t)(uintptr_t)rs, param)) return;
    uint64_t room = 0;
    if (uint8_t* cpu = Cpu(va, &room); cpu && room >= 54 * 16) uitrace::FixDepthRangeAt(cpu);
}
template <int S> void STDMETHODCALLTYPE HookSetGfxCBV(ID3D12GraphicsCommandList* cl, UINT p, D3D12_GPU_VIRTUAL_ADDRESS va) {
    DepthRange(0, cl, p, va);
    const uint64_t to = Note(0, cl, p, va);
    OrigSetGfxCBV[S](cl, p, to ? to : va);
}
template <int S> void STDMETHODCALLTYPE HookSetCompCBV(ID3D12GraphicsCommandList* cl, UINT p, D3D12_GPU_VIRTUAL_ADDRESS va) {
    DepthRange(1, cl, p, va);
    const uint64_t to = Note(1, cl, p, va);
    OrigSetCompCBV[S](cl, p, to ? to : va);
}

std::vector<void*> g_hookedFns;   // function addresses hooked already (vtables may share them)
SRWLOCK g_hookLock = SRWLOCK_INIT;
bool HookOne(void* fn, void* detour, void** orig) {
    if (std::find(g_hookedFns.begin(), g_hookedFns.end(), fn) != g_hookedFns.end()) return true;
    const bool ok = MH_CreateHook(fn, detour, orig) == MH_OK && MH_EnableHook(fn) == MH_OK;
    if (ok) g_hookedFns.push_back(fn);
    return ok;
}
template <int S> bool HookTable(void** vt) {
    bool ok = true;
    ok &= HookOne(vt[29], (void*)&HookSetCompRS<S>, (void**)&OrigSetCompRS[S]);
    ok &= HookOne(vt[30], (void*)&HookSetGfxRS<S>, (void**)&OrigSetGfxRS[S]);
    ok &= HookOne(vt[37], (void*)&HookSetCompCBV<S>, (void**)&OrigSetCompCBV[S]);
    ok &= HookOne(vt[38], (void*)&HookSetGfxCBV<S>, (void**)&OrigSetGfxCBV[S]);
    return ok;
}
bool g_slotDone[2] = {};
void OnReset(command_list* cl) {
    if (g_slotDone[0] && g_slotDone[1]) return;
    ID3D12GraphicsCommandList* n = (ID3D12GraphicsCommandList*)cl->get_native();
    const D3D12_COMMAND_LIST_TYPE t = n->GetType();
    const int slot = t == D3D12_COMMAND_LIST_TYPE_DIRECT ? 0 : t == D3D12_COMMAND_LIST_TYPE_COMPUTE ? 1 : -1;
    if (slot < 0) return;
    AcquireSRWLockExclusive(&g_hookLock);
    if (!g_slotDone[slot]) {
        g_slotDone[slot] = true;
        void** vt = *(void***)n;
        const bool ok = slot == 0 ? HookTable<0>(vt) : HookTable<1>(vt);
        LogF("vrcam: replay - %s command list hooks (root signature, root CBV) %s, vtable %p",
             slot == 0 ? "direct" : "compute", ok ? "in" : "FAILED", (void*)vt);
    }
    ReleaseSRWLockExclusive(&g_hookLock);
}

void Register() {
    reshade::register_event<reshade::addon_event::init_resource>(&d2rvr::D3D12Only<&OnInitResource>::Call);
    reshade::register_event<reshade::addon_event::destroy_resource>(&d2rvr::D3D12Only<&OnDestroyResource>::Call);
    reshade::register_event<reshade::addon_event::reset_command_list>(&d2rvr::D3D12Only<&OnReset>::Call);
}
// No waiting for the GPU (2026-10-09): the camera buffers the left pass binds are bound
// from slots of our own GPU buffer instead. At the binding, the game's values are copied
// into an upload staging slot for the left eye and, moved by the eye separation, into
// one for the right; the GPU copies the left ones into the slots before the left pass's
// lists and the right ones before the replay - the queue keeps the order, the CPU never
// waits, and the game's own buffers are never touched.
constexpr int kSlots = 256, kSlotBytes = 4096, kRing = 4;   // slots a pass; staging for kRing pairs
ID3D12Resource* g_slotsBuf = nullptr;      // DEFAULT: what the shaders read
ID3D12Resource* g_stageBuf = nullptr;      // UPLOAD: [ring][eye][slot]
uint8_t* g_stageCpu = nullptr;
uint64_t g_slotsVa = 0;
int g_ring = 0;                            // this pair's staging
struct SlotUse { uint64_t gameVa; int bytes; int key; };
SlotUse g_slotUse[kSlots];
std::atomic<int> g_nSlots{0};
SRWLOCK g_slotLock = SRWLOCK_INIT;
std::atomic<bool> g_redirect{false};       // binding from slots this left pass
std::atomic<uint32_t> g_overflow{0};

bool MakeSlots(ID3D12Device* dev) {
    if (g_slotsBuf) return true;
    D3D12_HEAP_PROPERTIES hp{};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Width = (UINT64)kSlots * kSlotBytes;
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                            __uuidof(ID3D12Resource), (void**)&g_slotsBuf))) return false;
    rd.Width = (UINT64)kRing * 2 * kSlots * kSlotBytes;
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                            __uuidof(ID3D12Resource), (void**)&g_stageBuf))) return false;
    D3D12_RANGE none{0, 0};
    if (FAILED(g_stageBuf->Map(0, &none, (void**)&g_stageCpu))) return false;
    g_slotsVa = g_slotsBuf->GetGPUVirtualAddress();
    LogF("vrcam: replay - camera slots made: %d x %d bytes on the GPU, staging for %d pairs", kSlots, kSlotBytes, kRing);
    return true;
}
uint8_t* Stage(int ring, int eye, int slot) { return g_stageCpu + ((size_t)(ring * 2 + eye) * kSlots + slot) * kSlotBytes; }
uint64_t StageOffset(int ring, int eye, int slot) { return ((uint64_t)(ring * 2 + eye) * kSlots + slot) * kSlotBytes; }

// From the binding hook: the slot to bind instead of the game's buffer (0: bind the game's).
int KeyCount(int key) {
    AcquireSRWLockShared(&g_slotLock);
    int c = 0;
    const int n = g_nSlots.load();
    for (int i = 0; i < n; ++i) c += g_slotUse[i].key == key;
    ReleaseSRWLockShared(&g_slotLock);
    return c;
}
// A buffer's slot this pass, found without a lock: the binding hook runs on the game's five
// recording threads ~250 times a pass, and the exclusive lock with a scan of the slots under
// it held them up - a third of the draw thread's time waited in it (2026-10-09, 69 pairs/s
// with the replay against 108 without). Open addressing, va 0 = empty, cleared each left pass;
// an entry is written whole under g_slotLock (slot first, then va), read without it.
constexpr int kHash = 1024;   // a power of two, 4 x kSlots
std::atomic<uint64_t> g_hashVa[kHash];
std::atomic<int> g_hashSlot[kHash];
uint32_t HashOf(uint64_t va) { return (uint32_t)((va >> 8) * 0x9E3779B97F4A7C15ull >> 54) & (kHash - 1); }
int FindSlot(uint64_t va) {
    for (uint32_t h = HashOf(va), i = 0; i < kHash; ++i, h = (h + 1) & (kHash - 1)) {
        const uint64_t v = g_hashVa[h].load(std::memory_order_acquire);
        if (v == va) return g_hashSlot[h].load(std::memory_order_relaxed);
        if (!v) return -1;
    }
    return -1;
}
void PutSlot(uint64_t va, int slot) {   // under g_slotLock
    for (uint32_t h = HashOf(va), i = 0; i < kHash; ++i, h = (h + 1) & (kHash - 1))
        if (!g_hashVa[h].load(std::memory_order_relaxed)) {
            g_hashSlot[h].store(slot, std::memory_order_relaxed);
            g_hashVa[h].store(va, std::memory_order_release);
            return;
        }
}
void ClearSlots() { for (int i = 0; i < kHash; ++i) g_hashVa[i].store(0, std::memory_order_relaxed); }
// Both eyes' values are made in ordinary (cached) memory first, outside g_slotLock, and only then written to the staging
// slots under it (2026-10-18). The staging buffer is an upload heap - write-combined: reading it back is many times slower
// than writing it - and the patches below read and rewrite it, the right eye's copy read the left one back: an outside
// profile of the replay found the draw thread waiting a quarter of its time for this lock, held by the game's other recording
// threads through those reads, and ~9% more in their memcpy. Now the lock covers the slot's reservation and two straight
// writes. The slots full - all 256 every pass in that log, ~3000 more binds a pair overflowing - is found without the lock
// (each of those took it before, only to find no room) and before any copying.
uint64_t Redirect(uint64_t va, const Layout& lay, int key, bool* fresh) {
    if (const int i = FindSlot(va); i >= 0) return g_slotsVa + (uint64_t)i * kSlotBytes;
    if (g_nSlots.load(std::memory_order_relaxed) >= kSlots) { *fresh = true; g_overflow.fetch_add(1, std::memory_order_relaxed); return 0; }
    uint64_t room = 0;
    uint8_t* cpu = Cpu(va, &room);
    if (!cpu) { *fresh = true; return 0; }
    const int bytes = (int)std::min<uint64_t>(room, kSlotBytes) & ~3;
    alignas(16) uint8_t left[kSlotBytes], right[kSlotBytes];   // the eyes' values, cached; to the staging slots at the end
    memcpy(left, cpu, bytes);
    if (g_replayPrev.load() >= 1) PrevToRightEye((float*)left, bytes / 4, lay, 2.0 * g_eyeHalf.load() * g_shiftMul.load());
    memcpy(right, left, bytes);
    if (g_replayPrev.load() == 2) {   // the right eye's "last frame" = the left eye's current view*projection
        const int mask = g_viewMask.load() ? g_viewMask.load() : (1 << g_replayViews.load()) - 1;
        for (int k = 0; k < lay.nv; ++k) {
            if (!(mask & (1 << k)) || !(lay.parts[k] & 4)) continue;
            const int o = lay.vo[k];
            if ((o + 112) * 4 <= bytes) memcpy(right + (o + 96) * 4, left + (o + 64) * 4, 64);
            // and the rest of "the pass before" as two passes give the right record (cb dump,
            // 2026-10-10): the left eye's view from its third word at +130, its inverse at +144,
            // its projection's first 12 words at +160 - zeros in the left record. Without them the
            // right eye's character came out darker (its lighting blended against nothing).
            if ((o + 172) * 4 <= bytes) {
                memcpy(right + (o + 130) * 4, left + (o + 2) * 4, 14 * 4);
                memcpy(right + (o + 144) * 4, left + (o + 16) * 4, 16 * 4);
                memcpy(right + (o + 160) * 4, left + (o + 32) * 4, 12 * 4);
            }
        }
    }
    double L[16], R[16];
    Views(L, R);
    const double right3[3] = {L[0], L[4], L[8]};
    PatchKnown((float*)right, bytes / 4, lay, 2.0 * g_eyeHalf.load() * g_shiftMul.load(), right3);
    AcquireSRWLockExclusive(&g_slotLock);
    const int n = g_nSlots.load();
    if (const int i = FindSlot(va); i >= 0) { ReleaseSRWLockExclusive(&g_slotLock); return g_slotsVa + (uint64_t)i * kSlotBytes; }
    *fresh = true;
    if (n >= kSlots) { ReleaseSRWLockExclusive(&g_slotLock); g_overflow.fetch_add(1); return 0; }
    memcpy(Stage(g_ring, 0, n), left, bytes);    // write-only into the write-combined staging
    memcpy(Stage(g_ring, 1, n), right, bytes);
    g_slotUse[n] = {va, bytes, key};
    g_nSlots.store(n + 1);
    PutSlot(va, n);
    ReleaseSRWLockExclusive(&g_slotLock);
    return g_slotsVa + (uint64_t)n * kSlotBytes;
}
// Our copy list's body: this pair's staged values of one eye into the slots.
void RecordSlots(ID3D12GraphicsCommandList* l, int eye, int from = 0, int to = -1) {
    const int n = to < 0 ? g_nSlots.load() : to;
    for (int i = from; i < n; ++i)
        l->CopyBufferRegion(g_slotsBuf, (UINT64)i * kSlotBytes, g_stageBuf, StageOffset(g_ring, eye, i), (UINT64)g_slotUse[i].bytes);
}

// [debug] replay_scan_go 0 -> 1, once: every copy of the left eye's camera (the view, and the
// view*projection, of the first camera slot with a view) anywhere in the game's upload buffers,
// logged with whether one of our slots covers it - the right eye's shadows stayed where the left
// eye had them: a pass reads the camera from a buffer the root CBV hook never sees.
std::atomic<int> g_scanWant{0};
void ScanCopies() {
    AcquireSRWLockShared(&g_slotLock);
    const int n = g_nSlots.load();
    int si = -1, vo = -1; uint8_t parts = 0;
    for (int i = 0; i < n && si < 0; ++i) {
        const int key = g_slotUse[i].key;
        if (key <= 0) continue;
        AcquireSRWLockShared(&g_keyLock);
        const Layout lay = g_keys[key - 1].lay;
        ReleaseSRWLockShared(&g_keyLock);
        if (lay.nv) { si = i; vo = lay.vo[0]; parts = lay.parts[0]; }
    }
    float V[16] = {}, VP[16] = {};
    if (si >= 0) {
        const float* f = (const float*)Stage(g_ring, 0, si);
        memcpy(V, f + vo, sizeof V);
        if (parts & 4) memcpy(VP, f + vo + 64, sizeof VP);
    }
    std::vector<std::pair<uint64_t, int>> slotsCopy;
    for (int i = 0; i < n; ++i) slotsCopy.push_back({g_slotUse[i].gameVa, g_slotUse[i].bytes});
    ReleaseSRWLockShared(&g_slotLock);
    if (si < 0) { Log("vrcam: replay scan - no camera slot with a view this pass"); return; }
    AcquireSRWLockShared(&g_upLock);
    const std::vector<Upload> ups = g_uploads;
    ReleaseSRWLockShared(&g_upLock);
    uint64_t scanned = 0; int hits = 0, uncovered = 0;
    const double t0 = flog::UsNow();
    for (const Upload& u : ups) {
        if (!u.cpu || u.size < 64) continue;
        scanned += u.size;
        const float* w = (const float*)u.cpu;
        const uint64_t nw = u.size / 4;
        for (uint64_t k = 0; k + 16 <= nw; ++k) {
            const bool isV = w[k] == V[0] && w[k + 1] == V[1] && w[k + 5] == V[5] && memcmp(w + k, V, 64) == 0;
            const bool isVP = !isV && (parts & 4) && w[k] == VP[0] && w[k + 5] == VP[5] && memcmp(w + k, VP, 64) == 0;
            if (!isV && !isVP) continue;
            const uint64_t va = u.va + k * 4;
            bool covered = false;
            for (const auto& sc : slotsCopy) if (va >= sc.first && va < sc.first + (uint64_t)sc.second) covered = true;
            ++hits; if (!covered) ++uncovered;
            if (hits <= 40) LogF("vrcam: replay scan - %s at %llx (buffer %llx + %llu of %llu)%s", isV ? "view" : "view*proj",
                                 (unsigned long long)va, (unsigned long long)u.va, (unsigned long long)(k * 4),
                                 (unsigned long long)u.size, covered ? " - in a slot" : " - NOT in a slot");
            k += 15;
        }
    }
    LogF("vrcam: replay scan - %d copies (%d not in a slot) in %.1f MB of %zu upload buffers, %.1f ms; slots %d",
         hits, uncovered, scanned / 1048576.0, ups.size(), (flog::UsNow() - t0) / 1000.0, n);
}
void BeginLeft(bool redirect) {
    AcquireSRWLockExclusive(&g_vaLock); g_vas.clear(); ReleaseSRWLockExclusive(&g_vaLock);
    g_ring = (g_ring + 1) % kRing;
    g_nSlots.store(0);
    ClearSlots();
    g_redirect.store(redirect && g_slotsBuf);
    g_capture.store(true);
}

// What was rewritten, to be put back: the game does not write a camera buffer again
// while the camera holds still - it kept drawing the left eye with our right camera
// (stereo for a few seconds, then both eyes the right one).
struct Saved { uint8_t* cpu; int n; float orig[256], mine[256]; };
std::vector<Saved> g_saved;
// After the replay is done on the GPU, before the game's next left pass reaches the GPU:
// the left values back, where the game has not written new ones meanwhile.
void Restore() {
    for (const Saved& sv : g_saved)
        if (memcmp(sv.cpu, sv.mine, sv.n * 4) == 0) memcpy(sv.cpu, sv.orig, sv.n * 4);
    g_saved.clear();
}
// After the left pass is done on the GPU: its camera buffers, rewritten for the right eye.
int RightEye() {
    g_capture.store(false);
    std::vector<Va> vas;
    AcquireSRWLockExclusive(&g_vaLock); vas.swap(g_vas); ReleaseSRWLockExclusive(&g_vaLock);
    double L[16], R[16];
    Views(L, R);
    const double shift = 2.0 * g_eyeHalf.load();
    const double right[3] = {L[0], L[4], L[8]};
    int done = 0;
    for (const Va& x : vas) {
        uint64_t room = 0;
        uint8_t* cpu = Cpu(x.va, &room);
        if (!cpu) continue;
        float f[256];
        const int n = (int)std::min<uint64_t>(room, sizeof f) / 4;
        memcpy(f, cpu, n * 4);
        Saved sv;
        sv.cpu = cpu; sv.n = n;
        memcpy(sv.orig, f, n * 4);
        if (PatchKnown(f, n, x.lay, shift, right)) {
            memcpy(cpu, f, n * 4);
            memcpy(sv.mine, f, n * 4);
            g_saved.push_back(sv);
            ++done;
        }
    }
    if (g_patched.fetch_add(1) % 900 == 0) {
        LogF("vrcam: replay - the right eye's camera: %d of %zu camera buffers rewritten", done, vas.size());
        LogF("vrcam: replay - root CBVs seen %u (no root signature %u, not in an upload buffer %u, judged %u); buffers made %u, upload ones %u, known %zu",
             g_nNote.load(), g_nNoRS.load(), g_nNoCpu.load(), g_nJudged.load(), g_nInit.load(), g_nUploads.load(), g_uploads.size());
    }
    return done;
}
}  // namespace camfix

// -- [render] depth_fix_mode 2: the game's depth range at its source (vr/depth_source.h) ---------------------------------------
// After the game's frame-constant builder (BuildFrame 0xE3B590: renderer, frame, camera, time) this frame's upload memory holds
// word 53 (renderer +0x11BB8 -> +0x350) and the camera constants (renderer +0x11A08 -> +0xD0: the projection uploaded for our
// camera). The range is written there once, before any pass reads it. Only while our camera is the game's (VR, in a world): the
// game's camera from above keeps its own range. bearstonem/d2r-pcvr (MIT) found the place and its layout (vr/dlss_depth.cpp).
namespace depthsrc {
constexpr uint64_t RVA_BUILD_FRAME = 0xE3B590;
const uint8_t kSigBuildFrame[11] = {0x4C,0x8B,0xDC,0x53,0x48,0x81,0xEC,0x00,0x02,0x00,0x00};
// the layout the write relies on, checked in the game's code before the hook goes in
constexpr uint64_t RVA_FRAME_CAMERA = 0xE34FF3, RVA_FRAME_MAP = 0xE3B63D, RVA_FRAME_RANGE = 0xE3BE07, RVA_FRAME_STORE = 0xE3BE56;
const uint8_t kSigFrameCamera[7] = {0x48,0x8D,0x8B,0x00,0x1A,0x01,0x00};                 // lea rcx, [rbx+11A00h]
const uint8_t kSigFrameMap[7] = {0x48,0x8B,0xBE,0xB8,0x1B,0x01,0x00};                    // mov rdi, [rsi+11BB8h]
const uint8_t kSigFrameRange[20] = {0xF3,0x41,0x0F,0x10,0x94,0x24,0x60,0x01,0x00,0x00,0xF3,0x41,0x0F,0x10,0x8C,0x24,0x64,0x01,0x00,0x00};
const uint8_t kSigFrameStore[7] = {0x0F,0x11,0x87,0x50,0x03,0x00,0x00};                  // movups [rdi+350h], xmm0
using BuildFrameFn = void (*)(void* renderer, void* frame, void* camera, float time);
BuildFrameFn Orig = nullptr;
std::atomic<bool> g_on{false};
std::atomic<uint32_t> g_written{0}, g_writtenEver{0}, g_notOurs{0}, g_unread{0}, g_notRange{0};
std::atomic<float> g_far{0.0f};   // the far end of the range last written (the log; 0 none yet)
bool g_in = false, g_failed = false;
float LastFar() { return g_far.load(std::memory_order_relaxed); }

void Hook(void* renderer, void* frame, void* camera, float time) {
    Orig(renderer, frame, camera, time);
    if (!g_on.load(std::memory_order_relaxed) || !renderer || !camera) return;
    if (!g_enabled.load(std::memory_order_relaxed) || !g_inWorld.load(std::memory_order_relaxed)) {
        g_notOurs.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    uintptr_t cameraConstants = 0, constants = 0;
    float projection[16], range[4];
    auto* r = static_cast<uint8_t*>(renderer);
    if (!SafeRead(&cameraConstants, r + 0x11A08, sizeof cameraConstants) || cameraConstants < 0x10000 ||
        !SafeRead(projection, reinterpret_cast<void*>(cameraConstants + 0xD0), sizeof projection) ||
        !SafeRead(&constants, r + 0x11BB8, sizeof constants) || constants < 0x10000 ||
        !SafeRead(range, reinterpret_cast<void*>(constants + 0x350), sizeof range)) {
        g_unread.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!depthrange::PerspectiveRange(projection, range)) {
        g_notRange.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!SafeWriteMem(reinterpret_cast<void*>(constants + 0x350), range, 3 * sizeof(float))) {   // (the fourth lane stays the game's)
        g_unread.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_written.fetch_add(1, std::memory_order_relaxed);
    g_far.store(range[2] + range[0], std::memory_order_relaxed);
    if (g_writtenEver.fetch_add(1, std::memory_order_relaxed) == 0)
        LogF("vrcam: [render] depth_fix_mode 2 - the first frame written at the source: near %.3f, far %.1f (every pass decodes it alike)",
             range[2], range[2] + range[0]);
}
// The hook, once mode 2 is wanted and its layout checks hold. The game decrypts its code a page at a time when it first runs it, and at
// the settings' first load (the game's start) the renderer's never has: the checks failed there and mode 2 gave up for good - on PC1,
// 3.3.94995, every address of ours found where 3.3.93787 has them two seconds later, nothing written all session (2026-10-27). Now
// asked again from Tick until they hold; given up only once the resolver has every address and they still fail 30 s on.
void TryHook() {
    if (!g_on.load() || g_in || g_failed) return;
    if (!d2rsig::Addr(RVA_BUILD_FRAME)) return;   // (its code may still be encrypted: asked again)
    if (!d2rsig::Check(RVA_FRAME_CAMERA, kSigFrameCamera, sizeof kSigFrameCamera) || !d2rsig::Check(RVA_FRAME_MAP, kSigFrameMap, sizeof kSigFrameMap) ||
        !d2rsig::Check(RVA_FRAME_RANGE, kSigFrameRange, sizeof kSigFrameRange) ||
        !d2rsig::Check(RVA_FRAME_STORE, kSigFrameStore, sizeof kSigFrameStore)) {
        static ULONGLONG firstMiss = 0;
        if (!d2rsig::AllFound()) return;   // (the resolver itself still waits for code to be decrypted)
        const ULONGLONG now = GetTickCount64();
        if (!firstMiss) firstMiss = now;
        if (now - firstMiss < 30000) return;
        g_failed = true;
        Log("vrcam: [render] depth_fix_mode 2 - the game's frame builder is not what 3.3.93787 has (30 s after every address was found): nothing "
            "written (mode 1 still works)");
        return;
    }
    g_in = d2rsig::Hook(RVA_BUILD_FRAME, kSigBuildFrame, sizeof kSigBuildFrame, (void*)&Hook, (void**)&Orig);
    g_failed = !g_in;
    Log(g_in ? "vrcam: [render] depth_fix_mode 2 - the game's frame builder hooked: the depth range is written once a frame at its source"
             : "vrcam: [render] depth_fix_mode 2 - the game's frame builder could not be hooked: nothing written");
}
// Live (mode 2 or not); the hook goes in the first time mode 2 is wanted and its code reads (TryHook, again from Tick).
void Configure(bool on) {
    g_on.store(on);
    TryHook();
}
// The hook again while it is not in (its code decrypted later); every 20 s while mode 2 is on: what became of the frames.
void Tick() {
    TryHook();
    static ULONGLONG at = 0;
    const ULONGLONG now = GetTickCount64();
    if (!g_on.load() || !g_in || now - at < 20000) return;
    at = now;
    const uint32_t w = g_written.exchange(0), n = g_notOurs.exchange(0), u = g_unread.exchange(0), k = g_notRange.exchange(0);
    LogF("vrcam: [render] depth_fix_mode 2 - last 20 s: %u frames written; left alone: %u not our camera (not VR / not in a world), "
         "%u unreadable, %u not the game's kind of projection or range", w, n, u, k);
}
}  // namespace depthsrc

}  // namespace vrc
