// app_thread.cpp - see app_thread.h: D2RLoader's runOnUiThread as the pump's submit function, and the log of which thread each module's
// network calls come from.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "app_thread.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>

#include <D2RLPlugin/api.h>

#include "log_async.h"

using namespace D2RL;

namespace appthread {
namespace {

Pump g_pump;
const PluginContext* g_ctx = nullptr;
const ThreadService* g_threads = nullptr;
std::atomic<bool> g_dead{true};
std::atomic<DWORD> g_uiTid{0};   // the UI thread's id: learned from the first job of ours that ran there

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

void __cdecl UiJob(const PluginContext*, void* user) noexcept {
    if (g_dead.load()) return;
    const DWORD tid = GetCurrentThreadId();
    g_uiTid.store(tid);
    g_pump.Drain(uiticket::FromUser(user), tid);
}

bool Submit(uint32_t token) {
    if (g_dead.load() || !g_ctx || !g_threads || !g_threads->runOnUiThread) return false;
    return g_threads->runOnUiThread(g_ctx, &UiJob, uiticket::ToUser(token)) == Threads::Result::Success;
}

}  // namespace

void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    if (!ctx) return;
    if (ctx->QueryService(&g_threads) != ServiceQueryResult::Success || !HasThreadServiceField(g_threads, ThreadServiceRequiredSize)) g_threads = nullptr;
    if (!g_threads || !g_threads->runOnUiThread) {
        g_threads = nullptr;
        Log("vrcam: app thread - no thread service: a joined player's channels (host rules, hit share, attack path) are not asked for");
        return;
    }
    g_pump.SetSubmit(&Submit);
    g_dead.store(false);
}

void Shutdown() {
    g_dead.store(true);
    g_pump.Clear();
}

bool Post(Slot slot, std::function<void()> fn) {
    if (g_dead.load()) return false;
    return g_pump.Post(slot, std::move(fn), GetTickCount64(), GetCurrentThreadId());
}

void MarkUiJob() {
    DWORD expected = 0;
    g_uiTid.compare_exchange_strong(expected, GetCurrentThreadId());
}

void NoteCall(const char* module, const char* call) {
    static std::mutex m;
    static std::string seen;   // "|module|call|" for each pair told
    const std::string key = std::string("|") + module + "|" + call + "|";
    {
        std::lock_guard<std::mutex> lock(m);
        if (seen.find(key) != std::string::npos) return;
        seen += key;
    }
    const DWORD me = GetCurrentThreadId(), ui = g_uiTid.load();
    char b[420];
    snprintf(b, sizeof b, "vrcam: %s - the first %s runs on thread %lu: %s (the UI thread's job id %lu; the plugin's timer posts from %lu)", module, call, (unsigned long)me,
             ui && me == ui ? "the game's UI thread = the app thread, as the network SDK wants" : "NOT the UI thread - the network SDK would assert here", (unsigned long)ui,
             (unsigned long)g_pump.PosterTid());
    Log(b);
}

}  // namespace appthread
