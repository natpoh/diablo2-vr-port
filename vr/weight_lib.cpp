// weight_lib.cpp - see weight_lib.h.

#include "weight_lib.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "d2r_vr_weight.h"
#include "hold.h"

namespace weightlib {

namespace {

namespace hh = heft::hold;
using heft::V3;

constexpr wchar_t kFile[] = L"d2r_vr_weight.dll";

SRWLOCK g_lock = SRWLOCK_INIT;
LogFn g_log = nullptr;
std::wstring g_path;                 // an override (tests); empty: beside this module
Status g_status = Status::NotLooked;
HMODULE g_module = nullptr;
D2rwStepFn g_step = nullptr;
D2rwThrowVelocityFn g_throw = nullptr;
D2rwStabFn g_stab = nullptr;         // optional: a library without it has no stab ([weight] stab_in)
std::string g_name;
ULONGLONG g_lastTry = 0;
int g_toldMissing = -1;              // what the log was last told about the file: 0 missing, 1 there
std::atomic<bool> g_errorTold{false};

void Say(const char* fmt, const char* a = "", const char* b = "") {
    if (!g_log) return;
    char buf[400];
    snprintf(buf, sizeof buf, fmt, a, b);
    g_log(buf);
}

void Anchor() {}   // an address in this module

std::wstring PathOfFile() {
    if (!g_path.empty()) return g_path;
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&Anchor, &mod)) return kFile;
    wchar_t buf[MAX_PATH * 2] = {};
    const DWORD n = GetModuleFileNameW(mod, buf, (DWORD)(sizeof buf / sizeof buf[0]));
    if (!n || n >= sizeof buf / sizeof buf[0]) return kFile;
    std::wstring p(buf, n);
    const size_t slash = p.find_last_of(L"\\/");
    const std::wstring dir = slash == std::wstring::npos ? std::wstring() : p.substr(0, slash + 1);
    // d2rloader\d2r_vr_libs\ (0.168.1): out of d2rloader\plugins, where D2RLoader took it for a plugin and logged
    // "2 plugins rejected" for every player; beside the plugin is still looked at (0.168.0 and the test builds)
    const std::wstring libs = dir + L"..\\d2r_vr_libs\\" + kFile;
    if (GetFileAttributesW(libs.c_str()) != INVALID_FILE_ATTRIBUTES) return libs;
    return dir + kFile;
}

bool FileThere(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

D2rwV3 ToC(const V3& v) { return { v.x, v.y, v.z }; }
D2rwQ ToC(const heft::Q& q) { return { q.x, q.y, q.z, q.w }; }
V3 FromC(const D2rwV3& v) { return { v.x, v.y, v.z }; }
heft::Q FromC(const D2rwQ& q) { return heft::QNorm({ q.x, q.y, q.z, q.w }); }

// The library behind heft::hold::Engine.
class LibEngine final : public hh::Engine {
public:
    bool Step(heft::State& st, const heft::Body& ours, const heft::Piece* pieces, int nPieces, bool twoHandedKind, const heft::Grip* grips, const heft::Controller* from,
              const heft::Controller* to, int n, double dt, const hh::Settings& s) const override {
        D2rwStepFn step = g_step;
        if (step && n >= 1 && n <= 2 && nPieces >= 1 && nPieces <= 3) {
            D2rwHold h {};
            h.size          = sizeof h;
            h.nPieces       = nPieces;
            for (int i = 0; i < nPieces; ++i) h.pieces[i] = { ToC(pieces[i].a), ToC(pieces[i].b), pieces[i].radius, pieces[i].mass };
            h.twoHandedKind = twoHandedKind ? 1 : 0;
            h.gravity       = s.gravity;
            h.damping       = s.damping;
            h.strength      = s.strength * (n == 2 && s.twoHandsDouble ? 2.0 : 1.0);   // ([weight] two_hands_double)
            h.snap          = s.snap;
            h.flags         = s.modelFlags;
            h.substep       = 0;
            D2rwHand hands[2] {};
            for (int i = 0; i < n; ++i) {
                hands[i].palm       = ToC(grips[i].at);
                hands[i].gripRot    = ToC(grips[i].rot);
                hands[i].fromPos    = ToC(from[i].pose.pos);
                hands[i].fromRot    = ToC(from[i].pose.rot);
                hands[i].fromVel    = ToC(from[i].vel);
                hands[i].fromAngVel = ToC(from[i].angVel);
                hands[i].toPos      = ToC(to[i].pose.pos);
                hands[i].toRot      = ToC(to[i].pose.rot);
                hands[i].toVel      = ToC(to[i].vel);
                hands[i].toAngVel   = ToC(to[i].angVel);
            }
            // our state is the centre of mass of OUR body; the library takes the grip frame (its body has its own mass and centre)
            const V3 comAt = heft::Rotate(st.rot, ours.com);
            D2rwState state;
            state.pos    = ToC(heft::Sub(st.com, comAt));
            state.rot    = ToC(st.rot);
            state.angVel = ToC(st.angVel);
            state.vel    = ToC(heft::Sub(st.vel, heft::Cross(st.angVel, comAt)));
            const int32_t r = step(&h, hands, n, dt, &state);
            if (r >= 0) {
                st.rot    = FromC(state.rot);
                st.angVel = FromC(state.angVel);
                const V3 comOut = heft::Rotate(st.rot, ours.com);
                st.com    = heft::Add(FromC(state.pos), comOut);
                st.vel    = heft::Add(FromC(state.vel), heft::Cross(st.angVel, comOut));
                return r == 1;
            }
            if (!g_errorTold.exchange(true)) Say("vrcam: weight library - D2rwStep refused a step: our own step is used for it");
        }
        // the library cannot take this step: ours
        heft::World w;
        w.snapDistance = s.snap;
        w.gravity.y    = -9.81 * s.gravity;
        return heft::Step(st, ours, grips, from, to, n, dt, w);
    }

    bool ThrowVelocity(const uint64_t* t, const V3* vel, int n, uint64_t now, V3* out) const override {
        D2rwThrowVelocityFn fn = g_throw;
        if (!fn || n <= 0 || n > 64 || !out) return false;
        D2rwV3 v[64];
        for (int i = 0; i < n; ++i) v[i] = ToC(vel[i]);
        D2rwV3 r {};
        if (fn(t, v, n, now, &r) != 1) return false;
        *out = FromC(r);
        return true;
    }
};

LibEngine g_engine;

// Our state (the centre of mass of OUR body) <-> the library's (the grip frame).
D2rwState ToLib(const heft::State& st, const heft::Body& b) {
    const V3 comAt = heft::Rotate(st.rot, b.com);
    D2rwState s;
    s.pos    = ToC(heft::Sub(st.com, comAt));
    s.rot    = ToC(st.rot);
    s.angVel = ToC(st.angVel);
    s.vel    = ToC(heft::Sub(st.vel, heft::Cross(st.angVel, comAt)));
    return s;
}
void FromLib(const D2rwState& s, const heft::Body& b, heft::State& st) {
    st.rot    = FromC(s.rot);
    st.angVel = FromC(s.angVel);
    const V3 comAt = heft::Rotate(st.rot, b.com);
    st.com    = heft::Add(FromC(s.pos), comAt);
    st.vel    = heft::Add(FromC(s.vel), heft::Cross(st.angVel, comAt));
}

// [weight] stab_in: the library's stab behind heft::stab::Engine.
class LibStab final : public heft::stab::Engine {
public:
    int Step(heft::State& st, const heft::Body& b, V3 tipLocal, V3 baseLocal, const heft::contact::Capsule* bodies, int n, const heft::stab::Strike* strike, const V3* gaps,
             int nGaps, double dt, const heft::stab::Params& p, heft::stab::Blob* blob, heft::stab::Report* out) const override {
        static_assert(sizeof(heft::stab::Blob) == sizeof(D2rwStabState), "the mod's blob is the library's state");
        D2rwStabFn fn = g_stab;
        if (!fn || !blob || n < 0) return -1;
        D2rwStabber s {};
        s.size = sizeof s;
        s.tip = ToC(tipLocal);
        s.base = ToC(baseLocal);
        s.com = ToC(b.com);
        s.mass = b.mass;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) s.inertia[i * 3 + j] = b.inertia.m[i][j];
        s.angleCos = p.angleCos;
        s.requiredSpeed = p.minSpeed;
        s.tipZone = p.tipM;
        s.sharpness = p.sharpness;
        s.shellDamper = p.shellDamper;
        s.shellThickness = p.shellM;
        s.damper = p.fleshDamper;
        s.depthLimit = p.depthLimit;
        s.lockSpeed = p.lockSpeed;
        s.lockTime = p.lockTime;
        s.unlockForce = p.unlockForce;
        s.unstabThreshold = p.unstabM;
        s.unstabDelay = p.unstabDelay;
        s.perpendicular = p.perpM;
        s.tearDistance = p.breakM;
        s.missingTime = p.missingS;
        D2rwBody cb[64];
        const int nb = std::min(n, 64);
        for (int i = 0; i < nb; ++i) cb[i] = { ToC(bodies[i].a), ToC(bodies[i].b), bodies[i].radius, ToC(bodies[i].vel), bodies[i].id, bodies[i].part };
        D2rwStrike sk {};
        if (strike) {
            sk.body = strike->body < nb ? strike->body : -1;
            sk.point = ToC(strike->point);
            sk.normal = ToC(strike->normal);
            sk.thrusting = strike->thrusting ? 1 : 0;
            sk.before = ToLib(strike->before, b);
        }
        D2rwV3 g[2];
        const int ng = std::min(nGaps, 2);
        for (int i = 0; i < ng; ++i) g[i] = ToC(gaps[i]);
        D2rwState state = ToLib(st, b);
        D2rwStabOut o {};
        const int32_t r = fn(&s, cb, nb, strike ? &sk : nullptr, ng ? g : nullptr, ng, dt, reinterpret_cast<D2rwStabState*>(blob->bytes), &state, &o);
        if (r < 0) {
            if (!g_errorTold.exchange(true)) Say("vrcam: weight library - D2rwStab refused a step: the weapon leaves the wound");
            return -1;
        }
        FromLib(state, b, st);
        if (out) {
            heft::stab::Report& rep = *out;
            rep = {};
            rep.entered = (o.events & 1) != 0;
            rep.left = (o.events & 2) != 0;
            rep.in = r == 1;
            rep.locked = o.locked != 0;
            std::snprintf(rep.why, sizeof rep.why, "%s", o.why);
            rep.id = o.id;
            rep.depth = o.depth;
            rep.deepest = o.deepest;
            rep.speed = o.speed;
            rep.lateral = o.lateral;
            rep.pull = o.pull;
            rep.force = o.force;
            rep.limit = o.limit;
            rep.entry = FromC(o.entry);
            rep.axis = FromC(o.dir);
            rep.judged = (o.events & 32) != 0;
            rep.thrusting = strike && strike->thrusting;
            rep.cosInto = o.cosInto;
            rep.fromTip = o.fromTip;
            if (strike) rep.point = strike->point;
        }
        return r;
    }
};

LibStab g_stabEngine;

}  // namespace

void SetLog(LogFn fn) { g_log = fn; }

void SetPath(const wchar_t* path) {
    AcquireSRWLockExclusive(&g_lock);
    g_path = path ? path : L"";
    g_toldMissing = -1;
    g_lastTry = 0;
    ReleaseSRWLockExclusive(&g_lock);
}

Status GetStatus() {
    AcquireSRWLockShared(&g_lock);
    const Status s = g_status;
    ReleaseSRWLockShared(&g_lock);
    return s;
}

bool Loaded() { return GetStatus() == Status::Loaded; }

const char* Name() { return Loaded() ? g_name.c_str() : ""; }

bool Probe() {
    AcquireSRWLockExclusive(&g_lock);
    const bool there = FileThere(PathOfFile());
    if (g_status != Status::Loaded && g_toldMissing != (there ? 1 : 0)) {
        g_toldMissing = there ? 1 : 0;
        if (there) Say("vrcam: weight library - d2r_vr_weight.dll is in d2rloader\\d2r_vr_libs (or beside the plugin); it is loaded when [weight] hands_model 1 asks for it");
        else Say("vrcam: weight library - there is no d2r_vr_weight.dll in d2rloader\\d2r_vr_libs (or beside the plugin): [weight] hands_model 1 keeps our own physics");
    }
    ReleaseSRWLockExclusive(&g_lock);
    return there;
}

bool Ensure() {
    AcquireSRWLockExclusive(&g_lock);
    if (g_status == Status::Loaded) {
        ReleaseSRWLockExclusive(&g_lock);
        return true;
    }
    const ULONGLONG now = GetTickCount64();
    if (g_lastTry && now - g_lastTry < 2000) {   // looked a moment ago
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    g_lastTry = now;
    const std::wstring path = PathOfFile();
    if (!FileThere(path)) {
        if (g_toldMissing != 0) {
            g_toldMissing = 0;
            Say("vrcam: weight library - there is no d2r_vr_weight.dll in d2rloader\\d2r_vr_libs (or beside the plugin): [weight] hands_model 1 keeps our own physics");
        }
        g_status = Status::Missing;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    HMODULE mod = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!mod) {
        char why[64];
        snprintf(why, sizeof why, "error %lu", GetLastError());
        Say("vrcam: weight library - d2r_vr_weight.dll could not be loaded (%s): our own physics", why);
        g_status = Status::BrokenExports;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    const D2rwAbiFn abi = (D2rwAbiFn)GetProcAddress(mod, "D2rwAbi");
    const D2rwNameFn name = (D2rwNameFn)GetProcAddress(mod, "D2rwName");
    const D2rwStepFn step = (D2rwStepFn)GetProcAddress(mod, "D2rwStep");
    const D2rwThrowVelocityFn thr = (D2rwThrowVelocityFn)GetProcAddress(mod, "D2rwThrowVelocity");
    if (!abi || !step) {
        Say("vrcam: weight library - d2r_vr_weight.dll lacks D2rwAbi / D2rwStep: not used");
        FreeLibrary(mod);
        g_status = Status::BrokenExports;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    const int32_t v = abi();
    if (v != D2RW_ABI_VERSION) {
        char b[32];
        snprintf(b, sizeof b, "%d", (int)v);
        char w[32];
        snprintf(w, sizeof w, "%d", (int)D2RW_ABI_VERSION);
        Say("vrcam: weight library - d2r_vr_weight.dll speaks interface version %s, the mod needs %s: not used", b, w);
        FreeLibrary(mod);
        g_status = Status::BadAbi;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    g_module = mod;
    g_step   = step;
    g_throw  = thr;
    g_stab   = (D2rwStabFn)GetProcAddress(mod, "D2rwStab");   // (optional)
    g_name   = name && name() ? name() : "unnamed";
    g_status = Status::Loaded;
    hh::SetEngine(&g_engine);
    if (g_stab) heft::stab::SetEngine(&g_stabEngine);
    Say("vrcam: weight library - %s loaded from in d2rloader\\d2r_vr_libs (or beside the plugin): [weight] hands_model 1 moves the held weapon with it%s", g_name.c_str(),
        g_stab ? "; [weight] stab_in sticks thrusts in with its stab" : "; it has no stab (D2rwStab): [weight] stab_in stops at the surface");
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

}  // namespace weightlib
