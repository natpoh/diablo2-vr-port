#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "direct_hit_mode.h"   // the Melee page's Direct hit: one tick over [gameplay] melee_direct and melee_direct_client (shared/)
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

// -- the Co-op page's files -----------------------------------------------------------------------------------------
std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)std::max(n, 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
bool ReadAll(const std::wstring& path, std::string* out) {
    out->clear();
    const HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart < (1 << 22);   // (a status file of the plugin's is a few kilobytes)
    if (ok) {
        out->resize((size_t)size.QuadPart);
        DWORD got = 0;
        ok = size.QuadPart == 0 || (ReadFile(f, out->data(), (DWORD)size.QuadPart, &got, nullptr) && got == (DWORD)size.QuadPart);
    }
    CloseHandle(f);
    return ok;
}
std::wstring PathIn(const std::wstring& dir, const char* name) {
    std::wstring p = dir;
    if (!p.empty() && p.back() != L'\\' && p.back() != L'/') p += L'\\';
    return p + Widen(name);
}
// D2R.exe's folder + d2rloader\plugins\: where the plugin's DLL is and where it reads its ini (beside the DLL).
std::wstring DefaultRoomsDir() {
    wchar_t exe[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    std::wstring dir(exe, n);
    const size_t slash = dir.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : dir.substr(0, slash + 1) + L"d2rloader\\plugins\\";
}
int64_t NowUtc() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (int64_t)((((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10000000ull) - 11644473600ll;
}
// What a reading of the folder is compared by: a new State only when one of these moved.
struct CoopSeen {
    ini::Stamp dll, ini, status;
    int ageMin = -1;
    uint64_t written = 0;
    bool failed = false;
    bool halfRead = false;   // the status file is there but did not parse (the plugin writing it): read again next time
    bool operator==(const CoopSeen&) const = default;
};
CoopSeen SeenOf(const std::wstring& dir, const coop::State* st, int64_t now) {
    CoopSeen s;
    s.dll = ini::StampOf(PathIn(dir, roomsbridge::kPluginDll).c_str());
    s.ini = ini::StampOf(PathIn(dir, roomsbridge::kIniName).c_str());
    s.status = ini::StampOf(PathIn(dir, roomsbridge::kStatusName).c_str());
    int64_t at = 0;
    if (st && st->statusThere && roomsbridge::ParseUtc(st->status.updated, &at)) s.ageMin = (int)std::clamp<int64_t>((now - at) / 60, 0, 1 << 20);
    s.halfRead = st && st->installed && s.status.exists && !st->statusThere;
    return s;
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
    // the Co-op page's
    std::wstring roomsDir;
    bool roomsDirSet = false;          // a test's folder
    coop::Edit coopQueued;
    bool hasCoop = false;
    uint64_t coopWritten = 0;          // the last edit carried out, and how
    bool coopFailed = false;
    CoopSeen coopSeen;
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

// The Co-op page's files again when one of them changed (or the status grew a minute older, or an edit was carried out). The thread's.
void RefreshCoop() {
    std::wstring dir;
    std::shared_ptr<const coop::State> was;
    CoopSeen seenWas;
    uint64_t written;
    bool failed;
    {
        std::lock_guard<std::mutex> lock(g.m);
        dir = g.roomsDir;
        was = g.pub.coop;
        seenWas = g.coopSeen;
        written = g.coopWritten;
        failed = g.coopFailed;
    }
    const int64_t now = NowUtc();
    CoopSeen seen = SeenOf(dir, was.get(), now);
    seen.written = written;
    seen.failed = failed;
    if (was && seen == seenWas && !seen.halfRead) return;
    std::shared_ptr<const coop::State> st = LoadCoop(dir.c_str(), now, written, failed);
    seen = SeenOf(dir, st.get(), now);   // (the age of the status just read)
    seen.written = written;
    seen.failed = failed;
    std::lock_guard<std::mutex> lock(g.m);
    g.pub.coop = std::move(st);
    g.coopSeen = seen;
}

DWORD WINAPI Run(void*) {
    for (;;) {
        WaitForSingleObject(g.wake, 500);
        Request req;
        bool have = false;
        std::wstring path, roomsDir;
        coop::Edit edit;
        bool haveEdit = false;
        {
            std::lock_guard<std::mutex> lock(g.m);
            if (g.hasQueued) { req = g.queued; g.hasQueued = false; have = true; }
            if (g.hasCoop) { edit = g.coopQueued; g.hasCoop = false; haveEdit = true; }
            path = g.path;
            roomsDir = g.roomsDir;
        }
        if (haveEdit) {   // the Co-op page: one [relay] line of d2r-rooms.ini
            std::string why;
            const bool ok = WriteCoop(roomsDir.c_str(), edit, &why);
            Say("vrcam: openxr - headset settings: Co-op: [relay] %s = %s %s d2r-rooms.ini%s%s", edit.key.c_str(), edit.value.c_str(), ok ? "written to" : "NOT written to",
                ok ? "" : ": ", ok ? " (the D2R Rooms plugin reads it on the next CREATE GAME)" : why.c_str());
            std::lock_guard<std::mutex> lock(g.m);
            g.coopWritten = edit.sequence;
            g.coopFailed = !ok;
        }
        RefreshCoop();
        if (have) {
            std::string why;
            const Result r = SaveRequest(path.c_str(), req, &why);
            Say("vrcam: openxr - headset settings: Apply %s (%u key%s)%s%s", r == Result::Saved ? "saved" : r == Result::Conflict ? "refused, the ini changed elsewhere" : "FAILED",
                (unsigned)req.changed.Count(), req.changed.Count() == 1 ? "" : "s", why.empty() || r == Result::Saved ? "" : ": ", r == Result::Saved ? "" : why.c_str());
            Snapshot s = LoadSnapshot(path.c_str());
            const ini::Stamp st = ini::StampOf(path.c_str());
            s.sequence = req.sequence;
            s.result = r;
            std::lock_guard<std::mutex> lock(g.m);
            s.coop = g.pub.coop;
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
                s.coop = g.pub.coop;
                g.pub = s;
                g.stamp = st;
            }
        }
        std::lock_guard<std::mutex> lock(g.m);
        if (g.stop && !g.hasQueued && !g.hasCoop) { g.running = false; break; }
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
        if (IsGame(i)) continue;   // the game's own (the Players row): not in the ini, the default stands
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
        // Direct hit: melee_direct_client's row is the mode the three keys say (shared/direct_hit_mode.h: 0 Off, 1 Two-handed extras, 2 By the rules,
        // 3 Always; a missing scope: what quick blow made of By the rules in 0.204); melee_direct's value 1 while it is By the rules either way (the page's
        // line). The file's texts are what a conflict is judged by.
        const float directRaw = Parse(ini::Read(path, kDefs[kKeyMeleeDirect].section, kDefs[kKeyMeleeDirect].key), 0.0f);
        const float clientRaw = Parse(ini::Read(path, kDefs[kKeyMeleeDirectClient].section, kDefs[kKeyMeleeDirectClient].key), 0.0f);
        const float scopeRaw = Parse(ini::Read(path, kDefs[kKeyMeleeDirectScope].section, kDefs[kKeyMeleeDirectScope].key), -1.0f);
        const float quickRaw = Parse(ini::Read(path, kDefs[kKeyQuickBlow].section, kDefs[kKeyQuickBlow].key), kDefs[kKeyQuickBlow].def);
        const int mode = directhit::FromKeys((int)std::lround(directRaw), (int)std::lround(clientRaw), (int)std::lround(scopeRaw), quickRaw > 0.5f);
        s.file[kKeyMeleeDirect] = directRaw;
        s.file[kKeyMeleeDirectClient] = clientRaw;
        s.file[kKeyMeleeDirectScope] = s.values[kKeyMeleeDirectScope] = scopeRaw;
        s.values[kKeyMeleeDirect] = mode == directhit::kCleave || mode == directhit::kRules ? 1.0f : 0.0f;
        s.values[kKeyMeleeDirectClient] = (float)mode;
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
    // Direct hit's row writes its keys, as D2R VR Settings' choice does (shared/direct_hit_mode.h): melee_direct, melee_direct_client, and the scope for
    // Two-handed extras / By the rules (Off / Always leave it as it is).
    int directText[3] = {0, 0, -1};
    directhit::ToKeys((int)std::lround(r.values[kKeyMeleeDirectClient]), &directText[0], &directText[1], &directText[2]);
    const Mask directKeys = Bit(kKeyMeleeDirect) | Bit(kKeyMeleeDirectClient) | (directText[2] >= 0 ? Bit(kKeyMeleeDirectScope) : Mask{});
    if (write & (Bit(kKeyMeleeDirect) | Bit(kKeyMeleeDirectClient))) write |= directKeys;
    for (int i = 0; i < kCount; ++i)
        if (write & Bit(i)) {
            const Def& d = kDefs[i];
            if (IsGame(i)) continue;   // the game's own: never written here
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
            const int directSlot = i == kKeyMeleeDirect ? 0 : i == kKeyMeleeDirectClient ? 1 : i == kKeyMeleeDirectScope ? 2 : -1;
            changes.push_back({d.section, d.key, Text(r.base[i]), Text(r.base[i]), directSlot >= 0 ? Text((float)directText[directSlot]) : Text(r.values[i])});
        }
    if (changes.empty()) return Result::Saved;
    switch (ini::Commit(path, changes, L".before-headset-menu", why)) {
        case ini::Result::Saved: return Result::Saved;
        case ini::Result::Conflict: return Result::Conflict;
        default: return Result::Failed;
    }
}

std::shared_ptr<const coop::State> LoadCoop(const wchar_t* dir, int64_t nowUtc, uint64_t written, bool writeFailed) {
    auto st = std::make_shared<coop::State>();
    const std::wstring d = dir ? dir : L"";
    st->dir = Utf8(d);
    while (!st->dir.empty() && (st->dir.back() == '\\' || st->dir.back() == '/')) st->dir.pop_back();
    st->written = written;
    st->writeFailed = writeFailed;
    st->installed = !d.empty() && ini::StampOf(PathIn(d, roomsbridge::kPluginDll).c_str()).exists;
    std::string text;
    st->iniThere = !d.empty() && ReadAll(PathIn(d, roomsbridge::kIniName), &text);
    st->cfg = roomsbridge::ParseIni(st->iniThere ? text : roomsbridge::DefaultIni());
    if (!d.empty() && ReadAll(PathIn(d, roomsbridge::kStatusName), &text))
        if (const auto s = roomsbridge::ParseStatus(text)) {
            st->statusThere = true;
            st->status = *s;
            int64_t at = 0;
            if (roomsbridge::ParseUtc(s->updated, &at)) st->ageMin = (int)std::clamp<int64_t>((nowUtc - at) / 60, 0, 1 << 20);
        }
    return st;
}

bool WriteCoop(const wchar_t* dir, const coop::Edit& e, std::string* why) {
    const auto fail = [&](const char* w) { if (why) *why = w; return false; };
    if (!coop::Sane(e)) return fail("not one of [relay] enabled / mode / use with a value the plugin reads");
    const std::wstring d = dir ? dir : L"";
    if (d.empty() || !ini::StampOf(PathIn(d, roomsbridge::kPluginDll).c_str()).exists) return fail("the D2R Rooms plugin is not installed (no d2rl-d2r-rooms.dll)");
    const std::wstring path = PathIn(d, roomsbridge::kIniName), tmp = path + L".tmp";
    std::string text;
    const bool there = ReadAll(path, &text);   // the file as it is now (the player may have changed it in Notepad)
    const std::string out = roomsbridge::WithKey(there ? text : roomsbridge::DefaultIni(), "relay", e.key, e.value);
    const HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return fail("could not make d2r-rooms.ini.tmp");
    DWORD put = 0;
    const bool wrote = WriteFile(f, out.data(), (DWORD)out.size(), &put, nullptr) && put == (DWORD)out.size();
    CloseHandle(f);
    if (!wrote || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return fail(wrote ? "could not move d2r-rooms.ini.tmp over d2r-rooms.ini" : "could not write d2r-rooms.ini.tmp");
    }
    return true;
}

void SetRoomsDir(const wchar_t* dir) {
    std::lock_guard<std::mutex> lock(g.m);
    g.roomsDir = dir ? dir : L"";
    g.roomsDirSet = dir != nullptr;
    g.coopSeen = {};
    g.pub.coop.reset();   // read again
}

bool SubmitCoop(const coop::Edit& e) {
    if (!e.sequence || !coop::Sane(e)) return false;
    std::lock_guard<std::mutex> lock(g.m);
    if (g.hasCoop || !g.running) return false;
    g.coopQueued = e;
    g.hasCoop = true;
    if (g.wake) SetEvent(g.wake);
    return true;
}

void Start(const wchar_t* iniPath) {
    std::lock_guard<std::mutex> lock(g.m);
    g.path = iniPath;
    const std::shared_ptr<const coop::State> coop = g.pub.coop;
    g.pub = LoadSnapshot(iniPath);
    if (!g.roomsDirSet) g.roomsDir = DefaultRoomsDir();
    if (coop) g.pub.coop = coop;   // (the thread reads it again when anything changed)
    else {   // the first reading now, so the Co-op page has it the moment the panel opens
        g.pub.coop = LoadCoop(g.roomsDir.c_str(), NowUtc(), g.coopWritten, g.coopFailed);
        g.coopSeen = SeenOf(g.roomsDir, g.pub.coop.get(), NowUtc());
        g.coopSeen.written = g.coopWritten;
        g.coopSeen.failed = g.coopFailed;
    }
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
    if (!r.sequence || !r.changed || (r.changed & ~kAllKeys)) return false;
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
