#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>

#include "hset_store.h"
#include "ini_txn.h"

namespace hset::store {
namespace {

std::wstring Text(float v) {
    wchar_t b[48];
    swprintf_s(b, L"%.6g", (double)v);
    return b;
}

float Parse(const ini::Text& t, float fallback) {
    if (!t.present) return fallback;
    wchar_t* end = nullptr;
    const float v = std::wcstof(t.text.c_str(), &end);
    return end == t.text.c_str() || !std::isfinite(v) ? fallback : v;
}

struct State {
    std::mutex m;
    Snapshot pub;
    Request queued;
    bool hasQueued = false, busy = false, running = false, stop = false;
    std::wstring path;
    ini::Stamp stamp;
    HANDLE thread = nullptr;
    HANDLE wake = nullptr;
    void (*log)(const char*) = nullptr;
};
State g;

void Say(const char* fmt, ...) {
    void (*log)(const char*);
    {
        std::lock_guard<std::mutex> lock(g.m);
        log = g.log;
    }
    if (!log) return;
    char b[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    log(b);
}

DWORD WINAPI Run(void*) {
    for (;;) {
        WaitForSingleObject(g.wake, 500);
        Request req;
        bool have = false;
        std::wstring path;
        {
            std::lock_guard<std::mutex> lock(g.m);
            if (g.hasQueued) { req = g.queued; g.hasQueued = false; have = true; }
            path = g.path;
        }
        if (have) {
            std::string why;
            const Result r = SaveRequest(path.c_str(), req, &why);
            Say("vrcam: openxr - headset settings: Apply %s (%u key%s)%s%s", r == Result::Saved ? "saved" : r == Result::Conflict ? "refused, the ini changed elsewhere" : "FAILED",
                (unsigned)__popcnt(req.changed), __popcnt(req.changed) == 1 ? "" : "s", why.empty() || r == Result::Saved ? "" : ": ", r == Result::Saved ? "" : why.c_str());
            Snapshot s = LoadSnapshot(path.c_str());
            const ini::Stamp st = ini::StampOf(path.c_str());
            s.sequence = req.sequence;
            s.result = r;
            std::lock_guard<std::mutex> lock(g.m);
            g.pub = s;
            g.stamp = st;
            g.busy = false;
        } else {
            const ini::Stamp st = ini::StampOf(path.c_str());
            bool changed;
            {
                std::lock_guard<std::mutex> lock(g.m);
                changed = st != g.stamp;
            }
            if (changed) {   // an outside edit (D2R VR Settings, a text editor)
                Snapshot s = LoadSnapshot(path.c_str());
                std::lock_guard<std::mutex> lock(g.m);
                s.sequence = g.pub.sequence;
                s.result = g.pub.result;
                g.pub = s;
                g.stamp = st;
            }
        }
        std::lock_guard<std::mutex> lock(g.m);
        if (g.stop && !g.hasQueued) { g.running = false; break; }
    }
    return 0;
}

}  // namespace

Snapshot LoadSnapshot(const wchar_t* path) {
    Snapshot s;
    s.ready = ini::StampOf(path).exists;
    for (int i = 0; i < kCount; ++i) {
        const Def& d = kDefs[i];
        const float v = s.ready ? Parse(ini::Read(path, d.section, d.key), d.def) : d.def;
        s.file[i] = v;
        s.values[i] = Normalize(i, v);
    }
    return s;
}

Result SaveRequest(const wchar_t* path, const Request& r, std::string* why) {
    std::vector<ini::Change> changes;
    for (int i = 0; i < kCount; ++i)
        if (r.changed & (1u << i)) {
            const Def& d = kDefs[i];
            changes.push_back({d.section, d.key, Text(d.def), Text(r.base[i]), Text(r.values[i])});
        }
    if (changes.empty()) return Result::Saved;
    switch (ini::Commit(path, changes, L".before-headset-menu", why)) {
        case ini::Result::Saved: return Result::Saved;
        case ini::Result::Conflict: return Result::Conflict;
        default: return Result::Failed;
    }
}

void Start(const wchar_t* iniPath) {
    std::lock_guard<std::mutex> lock(g.m);
    g.path = iniPath;
    g.pub = LoadSnapshot(iniPath);
    g.stamp = ini::StampOf(iniPath);
    g.stop = false;
    if (!g.wake) g.wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (g.running) return;   // the last one has not ended yet: it goes on
    if (g.thread) { CloseHandle(g.thread); g.thread = nullptr; }
    g.thread = CreateThread(nullptr, 0, Run, nullptr, 0, nullptr);
    g.running = g.thread != nullptr;
}

void Stop() {
    {
        std::lock_guard<std::mutex> lock(g.m);
        g.stop = true;
    }
    if (g.wake) SetEvent(g.wake);
}

void Join(unsigned ms) {
    HANDLE t = nullptr;
    {
        std::lock_guard<std::mutex> lock(g.m);
        t = g.thread;
    }
    if (t) WaitForSingleObject(t, ms);
}

void SetLogger(void (*log)(const char*)) {
    std::lock_guard<std::mutex> lock(g.m);
    g.log = log;
}

Snapshot Read() {
    std::lock_guard<std::mutex> lock(g.m);
    return g.pub;
}

bool Submit(const Request& r) {
    if (!r.sequence || !r.changed || (kCount < 32 && (r.changed >> kCount))) return false;
    for (int i = 0; i < kCount; ++i)
        if (r.changed & (1u << i))
            if (!std::isfinite(r.base[i]) || !std::isfinite(r.values[i]) || Normalize(i, r.values[i]) != r.values[i]) return false;
    std::lock_guard<std::mutex> lock(g.m);
    if (g.busy || !g.running) return false;
    g.queued = r;
    g.hasQueued = true;
    g.busy = true;
    g.pub.sequence = r.sequence;
    g.pub.result = Result::Saving;
    if (g.wake) SetEvent(g.wake);
    return true;
}

}  // namespace hset::store
