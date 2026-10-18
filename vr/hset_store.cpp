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
#include <map>
#include <mutex>
#include <vector>

#include "hset_store.h"
#include "ini_txn.h"
#include "turn_mode.h"

namespace hset::store {
namespace {

std::wstring Text(float v) {
    wchar_t b[48];
    swprintf_s(b, L"%.6g", (double)v);
    return b;
}

std::string Narrow(const std::wstring& w) {   // an ini line's text: ASCII (what the table's words are)
    std::string s;
    for (wchar_t c : w) s.push_back(c < 128 ? (char)c : '?');
    return s;
}
std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// The [direct] keys for vr/direct_remap.h's reading and choosing: a key asked for is read once from the file (its text as the file has it, ""
// and absent told apart by `present`), so the whole table the plugin builds from the ini costs one read a key.
struct DirectLines {
    explicit DirectLines(const wchar_t* path) : path_(path) {}
    directinput::layout::IniGet Getter() {
        return [this](const char* key, std::string* text) {
            std::string k = key;
            for (char& c : k) c = (char)std::tolower((unsigned char)c);
            auto it = seen_.find(k);
            if (it == seen_.end()) {
                const ini::Text t = ini::Read(path_, L"direct", Widen(k).c_str());
                it = seen_.emplace(k, std::make_pair(t.present, Narrow(t.text))).first;
            }
            if (!it->second.first) return false;
            *text = it->second.second;
            return true;
        };
    }
    // The line of a key as the file has it ("" if it is not there).
    std::string Raw(const char* key) {
        std::string t;
        Getter()(key, &t);
        return t;
    }
private:
    const wchar_t* path_;
    std::map<std::string, std::pair<bool, std::string>> seen_;
};

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
                (unsigned)__popcnt64(req.changed), __popcnt64(req.changed) == 1 ? "" : "s", why.empty() || r == Result::Saved ? "" : ": ", r == Result::Saved ? "" : why.c_str());
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
    DirectLines lines(path);
    DirectLines* direct = &lines;
    for (int i = 0; i < kCount; ++i) {
        const Def& d = kDefs[i];
        if (IsBindKey(i)) {   // a [direct] line: the pick it is by the list's reading (the plugin's default when the line is not there), and its text as the file has it
            if (s.ready) {
                const std::string key = keys::KeyOf(BindSlot(i));
                s.bind[BindSlot(i)] = direct->Raw(key.c_str());
                s.file[i] = s.values[i] = (float)keys::CurrentPick(direct->Getter(), BindSlot(i));
            }
            continue;
        }
        const float v = s.ready ? Parse(ini::Read(path, d.section, d.key), d.def) : d.def;
        s.file[i] = v;
        s.values[i] = Normalize(i, v);
    }
    if (s.ready) {   // [turn] smooth / snap: an ini without `smooth` says snap=0 for smooth turning (shared/turn_mode.h)
        const float snapRaw = Parse(ini::Read(path, kDefs[kKeySnap].section, kDefs[kKeySnap].key), 0.0f);
        const float smoothKey = Parse(ini::Read(path, kDefs[kKeySmooth].section, kDefs[kKeySmooth].key), -1.0f);
        const float smooth = turnmode::Smooth(snapRaw, smoothKey) ? 1.0f : 0.0f;
        s.file[kKeySnap] = snapRaw;                  // as the file has it (0: a legacy "smooth"), what a conflict is judged by
        s.values[kKeySnap] = Normalize(kKeySnap, turnmode::SnapDegrees(snapRaw));
        s.file[kKeySmooth] = s.values[kKeySmooth] = smooth;
    }
    return s;
}

Result SaveRequest(const wchar_t* path, const Request& r, std::string* why) {
    std::vector<ini::Change> changes;
    DirectLines direct(path);   // the file as it is now: the list's standard (pause_button, left_stick_map) is what the file says at the write
    Mask write = r.changed;
    // A new snap angle in an ini that says snap=0 for smooth turning (no `smooth` key) must not turn it into a snap profile:
    // `smooth` is written with it, as the mode the panel showed. (Switching the mode alone writes `smooth` alone.)
    if ((write & Bit(kKeySnap)) && !(write & Bit(kKeySmooth))) write |= Bit(kKeySmooth);
    for (int i = 0; i < kCount; ++i)
        if (write & Bit(i)) {
            const Def& d = kDefs[i];
            if (IsBindKey(i)) {   // a [direct] line: the list's own change (its text, or the key out when the pick is what the plugin does anyway), judged against the line as the panel read it
                directinput::remap::Change ch;
                if (!keys::Choose(direct.Getter(), BindSlot(i), (int)std::lround(r.values[i]), &ch)) continue;   // Custom is not a thing to write
                const std::wstring was = Widen(r.bind[BindSlot(i)]);
                ini::Change c{d.section, Widen(ch.key), was, was, Widen(ch.text)};
                c.erase = ch.erase;
                changes.push_back(c);
                continue;
            }
            // A key that is missing in the file means what the panel saw when it read it (the default, or for the turn keys what the
            // legacy rule made of it), so a key that is still missing is no conflict.
            changes.push_back({d.section, d.key, Text(r.base[i]), Text(r.base[i]), Text(r.values[i])});
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
    if (!r.sequence || !r.changed || (kCount < 64 && (r.changed >> kCount))) return false;
    for (int i = 0; i < kCount; ++i)
        if (r.changed & Bit(i))
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
