// vr/strike_stats_wire.cpp - [stats] strike_stats on the game (see strike_stats_wire.h). The touch notes its blows on vrcam's timer thread
// under a lock; a thread of its own reads the tick every 10 s and writes the file once a minute while the numbers change, and at the end.
// The file lives with the game's saves (Saved Games\Diablo II Resurrected\D2R VR Stats), not beside the ini: it is the player's, it holds
// his heroes' names (local only: never logged, never in a report or an upload), and the mod's uninstall leaves it.
#include "vrcam_internal.h"   // (first: windows.h with NOMINMAX, before the SDK's headers)
#include "strike_stats_wire.h"

#include <shlobj.h>

#include <atomic>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

namespace strikestatswire {
namespace {

std::mutex g_mutex;
strikestats::Store g_store;                // under g_mutex
strikestats::MissTracker g_miss[2];        // vrcam's timer thread only
std::atomic<bool> g_on{false};
std::atomic<uint64_t> g_revision{1};    // a change of the numbers (the Stats page copies them again)
int64_t g_started = 0;
std::wstring g_dir, g_path;                // D2R VR Stats\ and its strike_stats.txt
HANDLE g_thread = nullptr, g_stop = nullptr;
std::atomic<bool> g_loaded{false};         // the file read (or none there): only then is anything noted or written
bool g_backedUp = false;                   // this game's first write keeps the last good file as .bak

constexpr DWORD kPollMs = 10000;           // the tick read
constexpr ULONGLONG kWriteMs = 60000;      // a write at most this often while the numbers change

bool OnNow() { return GetPrivateProfileIntW(L"stats", L"strike_stats", 1, vrc::g_iniPath) != 0; }

bool ReadText(const std::wstring& path, std::string* out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER n{};
    bool ok = GetFileSizeEx(f, &n) && n.QuadPart >= 0 && n.QuadPart < (8 << 20);
    if (ok) {
        out->resize(static_cast<size_t>(n.QuadPart));
        DWORD got = 0;
        ok = n.QuadPart == 0 || (ReadFile(f, out->data(), static_cast<DWORD>(n.QuadPart), &got, nullptr) && got == n.QuadPart);
    }
    CloseHandle(f);
    return ok;
}

// The text to <path>.tmp, flushed to the disk, then renamed over the file: the file is always a whole one.
bool WriteText(const std::string& text) {
    const std::wstring tmp = g_path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const bool ok = WriteFile(f, text.data(), static_cast<DWORD>(text.size()), &put, nullptr) && put == text.size() && FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (!g_backedUp) {   // once a game: the file as the last game left it, should this one's ever turn out broken
        g_backedUp = true;
        CopyFileW(g_path.c_str(), (g_path + L".bak").c_str(), FALSE);
    }
    return MoveFileExW(tmp.c_str(), g_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

// The file (or its .bak) into the store; a file there that reads as neither is kept aside as .broken, never written over.
void Load() {
    std::string text;
    strikestats::Store s;
    const wchar_t* from = nullptr;
    if (ReadText(g_path, &text) && s.Parse(text)) from = L"";
    else if (ReadText(g_path + L".bak", &text) && s.Parse(text)) from = L".bak";
    if (!from && GetFileAttributesW(g_path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        MoveFileExW(g_path.c_str(), (g_path + L".broken").c_str(), MOVEFILE_REPLACE_EXISTING);
        vrc::LogF("strike stats: strike_stats.txt did not read (kept as strike_stats.txt.broken) - counting from nothing");
    }
    s.StartSession();
    size_t heroes = 0;
    uint64_t blows = 0;
    for (const strikestats::Hero& h : s.Heroes()) { ++heroes; blows += h.total.blows; }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_store = s;   // (nothing is noted before: On() waits for the file)
        ++g_revision;
    }
    vrc::LogF("strike stats ([stats] strike_stats): %ls%ls - %u hero(es), %llu blows all time", from ? L"read from D2R VR Stats\\strike_stats.txt" : L"no file yet", from && *from ? L" (its .bak)" : L"",
              static_cast<unsigned>(heroes), static_cast<unsigned long long>(blows));
}

void WriteNow() {
    std::string text;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_store.Dirty()) return;
        text = g_store.Serialize(g_started, static_cast<int64_t>(time(nullptr)));
        g_store.Clean();
    }
    CreateDirectoryW(g_dir.c_str(), nullptr);
    if (!WriteText(text)) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_store.MarkDirty();   // (the next minute tries again)
    }
}

DWORD WINAPI Run(void*) {
    Load();
    g_loaded.store(true);
    ULONGLONG wrote = GetTickCount64();
    bool told = false;
    for (;;) {
        const bool stop = WaitForSingleObject(g_stop, kPollMs) != WAIT_TIMEOUT;
        const bool on = OnNow();
        if (on != g_on.load()) {
            g_on.store(on);
            vrc::LogF("strike stats ([stats] strike_stats): %s", on ? "on - the touch's blows are counted" : "off - nothing counted, the file left as it is");
        }
        const ULONGLONG now = GetTickCount64();
        if (stop || now - wrote >= kWriteMs) {
            wrote = now;
            WriteNow();
            if (!told && GetFileAttributesW(g_path.c_str()) != INVALID_FILE_ATTRIBUTES) {
                told = true;
                vrc::LogF("strike stats: written to D2R VR Stats\\strike_stats.txt (every minute while it changes, and at the end)");
            }
        }
        if (stop) return 0;
    }
}

}  // namespace

void Start() {
    if (g_thread) return;
    PWSTR dir = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &dir))) {
        vrc::LogF("strike stats: no Saved Games folder - nothing counted");
        return;
    }
    g_dir = std::wstring(dir) + L"\\Diablo II Resurrected\\D2R VR Stats\\";
    CoTaskMemFree(dir);
    g_path = g_dir + L"strike_stats.txt";
    g_started = static_cast<int64_t>(time(nullptr));
    g_on.store(OnNow());
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, &Run, nullptr, 0, nullptr);
    if (g_thread) SetThreadPriority(g_thread, THREAD_PRIORITY_BELOW_NORMAL);
}

void Stop() {
    if (!g_thread) return;
    SetEvent(g_stop);   // (the thread writes what is there, then ends)
    WaitForSingleObject(g_thread, 5000);
    CloseHandle(g_thread);
    CloseHandle(g_stop);
    g_thread = g_stop = nullptr;
}

bool On() { return g_on.load(std::memory_order_relaxed) && g_loaded.load(std::memory_order_acquire); }

void SetHero(const char* name, int cls) {
    if (!On()) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    const strikestats::Hero* was = g_store.Current();
    const std::string wasName = was ? was->name : std::string("?");
    const int wasCls = was ? was->cls : -2;
    g_store.SetHero(name, cls);
    if (!g_store.Current() || g_store.Current()->name != wasName || g_store.Current()->cls != wasCls) ++g_revision;
}

void Blow(int side, uint64_t strokeKey, uint32_t code, uint32_t kind, float speed, float force) {
    if (!On()) return;
    strikestats::MissTracker::Miss m;
    const bool settled = g_miss[side & 1].Landed(strokeKey, &m);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (settled) g_store.Miss(m.code, m.kind, m.slow);
    g_store.Blow(code, kind, speed, force);
    ++g_revision;
}

void Refused(int side, uint64_t strokeKey, uint32_t code, uint32_t kind, bool slow, uint64_t nowMs) {
    if (!On()) return;
    strikestats::MissTracker::Miss m;
    if (!g_miss[side & 1].Refused(strokeKey, code, kind, slow, nowMs, &m)) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_store.Miss(m.code, m.kind, m.slow);
    ++g_revision;
}

void Tick(uint64_t nowMs) {
    if (!On()) return;
    for (strikestats::MissTracker& t : g_miss) {
        strikestats::MissTracker::Miss m;
        if (!t.Flush(nowMs, &m)) continue;
        std::lock_guard<std::mutex> lock(g_mutex);
        g_store.Miss(m.code, m.kind, m.slow);
        ++g_revision;
    }
}

uint64_t Revision() { return g_revision.load(std::memory_order_relaxed); }

bool Snapshot(strikestats::Store* out, int64_t* started) {
    if (!g_loaded.load()) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (out) *out = g_store;
    if (started) *started = g_started;
    return true;
}

}  // namespace strikestatswire
