// own_labels_wire.cpp - [hud] labels_own: the atlas and its worker (vr/own_labels_wire.h). Only shown: nothing here touches the game.

#include "own_labels_wire.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "game_ui.h"
#include "own_labels_paint.h"
#include "../tools/settings/lang.h"   // lang::FromUtf8

namespace ownlabels {
namespace {

std::atomic<LogFn> g_log{nullptr};
void Log(const char* s) { if (LogFn f = g_log.load()) f(s); }

std::atomic<bool> g_on{false};
std::atomic<const wchar_t* (*)()> g_faceSource{nullptr}, g_titleSource{nullptr};

constexpr size_t kQueueMax = 64;   // names waiting at most; more wait for a later frame

std::mutex g_m;
std::condition_variable g_cv, g_idle;
Atlas g_atlas;
std::vector<uint32_t> g_px;          // the atlas's pixels (made the first time a name is painted)
uint32_t g_rev = 1;                  // the atlas's picture: bumped by every change
std::vector<Want> g_queue;           // names to paint
std::vector<uint64_t> g_queued;      // ... their keys
std::vector<uint64_t> g_failed;      // names GDI could not paint: not asked again (a few)
bool g_busy = false;                 // the worker is painting a batch
bool g_started = false;
uint32_t g_painted = 0, g_restarts = 0;

// The name's face: the game's text font when it is in and has every letter of the name, else ours (nullptr).
const wchar_t* FaceFor(const char* utf8, int8_t font) {
    const auto src = (font == kFontExocet ? g_titleSource : g_faceSource).load();
    const wchar_t* face = src ? src() : nullptr;
    return face && gameui::Covers(face, lang::FromUtf8(utf8)) ? face : nullptr;
}

void Worker() {
    std::vector<uint32_t> cell;
    for (;;) {
        std::vector<Want> batch;
        {
            std::unique_lock<std::mutex> lock(g_m);
            g_cv.wait(lock, [] { return !g_queue.empty(); });
            batch.swap(g_queue);
            g_busy = true;
        }
        struct Done { uint64_t key; int w, h; std::vector<uint32_t> px; };
        std::vector<Done> done;
        std::vector<uint64_t> failed;
        done.reserve(batch.size());
        for (const Want& w : batch) {
            int cw = 0, ch = 0;
            if (Paint(w, FaceFor(w.text, w.font), &cell, &cw, &ch)) done.push_back({KeyOf(w), cw, ch, cell});
            else failed.push_back(KeyOf(w));
        }
        bool restarted = false;
        uint32_t painted = 0;
        {
            std::lock_guard<std::mutex> lock(g_m);
            if (g_px.empty()) g_px.assign((size_t)kAtlasW * kAtlasH, 0u);
            for (const Done& d : done) {
                if (g_atlas.Find(d.key)) continue;
                Cell c;
                if (!g_atlas.Place(d.key, d.w, d.h, &c)) {   // full: start it again empty - the names still shown are asked for again
                    g_atlas.Clear();
                    std::fill(g_px.begin(), g_px.end(), 0u);
                    restarted = true;
                    ++g_restarts;
                    if (!g_atlas.Place(d.key, d.w, d.h, &c)) continue;
                }
                for (int y = 0; y < d.h; ++y)
                    std::copy_n(d.px.data() + (size_t)y * d.w, d.w, g_px.data() + (size_t)(c.y + y) * kAtlasW + c.x);
                ++painted;
            }
            if (painted || restarted) ++g_rev;
            for (uint64_t k : failed)
                if (g_failed.size() < 64) g_failed.push_back(k);
            g_painted += painted;
            g_queued.erase(std::remove_if(g_queued.begin(), g_queued.end(),
                                          [&](uint64_t k) { return std::none_of(g_queue.begin(), g_queue.end(), [&](const Want& q) { return KeyOf(q) == k; }); }),
                           g_queued.end());
            g_busy = false;
        }
        g_idle.notify_all();
        static uint32_t toldPaint = 0;
        if (painted && toldPaint < 3) {
            ++toldPaint;
            char b[200];
            std::snprintf(b, sizeof b, "vrcam: [hud] labels_own - %u item names painted (%u in the atlas)%s", painted, (unsigned)g_atlas.Count(),
                          toldPaint == 3 ? " - no more of these lines" : "");
            Log(b);
        }
        if (restarted) {
            char b[160];
            std::snprintf(b, sizeof b, "vrcam: [hud] labels_own - the names' atlas was full and started again (%u times so far)", g_restarts);
            Log(b);
        }
    }
}

}  // namespace

void SetLog(LogFn fn) { g_log.store(fn); }
void SetWanted(bool on) { g_on.store(on); }
bool Wanted() { return g_on.load(std::memory_order_relaxed); }
void SetFaceSource(const wchar_t* (*fn)()) { g_faceSource.store(fn); }
void SetTitleFaceSource(const wchar_t* (*fn)()) { g_titleSource.store(fn); }

int Resolve(const Want* wants, int n, Cell* cells, uint32_t* rev, UploadFn upload, void* ctx) {
    if (!wants || !cells || !rev || n <= 0) return 0;
    int got = 0;
    bool wake = false;
    {
        std::lock_guard<std::mutex> lock(g_m);
        if (*rev != g_rev && !g_px.empty()) {
            if (!upload || !upload(ctx, g_px.data(), kAtlasW, kAtlasH)) {
                for (int i = 0; i < n; ++i) cells[i] = Cell{};
                return 0;
            }
            *rev = g_rev;
        }
        for (int i = 0; i < n; ++i) {
            cells[i] = Cell{};
            if (!wants[i].text[0] && !wants[i].piece) continue;   // (a piece of our plate has no text)
            const uint64_t key = KeyOf(wants[i]);
            if (const Cell* c = g_atlas.Find(key); c && *rev == g_rev) {
                cells[i] = *c;
                ++got;
                continue;
            }
            if (g_atlas.Find(key) || std::find(g_queued.begin(), g_queued.end(), key) != g_queued.end() || g_queued.size() >= kQueueMax ||
                std::find(g_failed.begin(), g_failed.end(), key) != g_failed.end())
                continue;
            g_queued.push_back(key);
            g_queue.push_back(wants[i]);
            wake = true;
        }
        if (wake && !g_started) {
            g_started = true;
            std::thread(Worker).detach();   // (lives as long as the process: it sleeps on the queue)
        }
    }
    if (wake) g_cv.notify_one();
    return got;
}

bool DumpAtlas(const wchar_t* path, int* err) {
    if (err) *err = 0;
    std::vector<uint32_t> px;
    {
        std::lock_guard<std::mutex> lock(g_m);
        px = g_px;
    }
    if (px.empty() || !path) {
        if (err) *err = -1;
        return false;
    }
    const int w = kAtlasW, h = kAtlasH;
    const uint32_t size = 54 + (uint32_t)w * h * 4;
    std::vector<unsigned char> b(size, 0);
    auto put32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) b[at + i] = (unsigned char)(v >> (8 * i)); };
    b[0] = 'B'; b[1] = 'M';
    put32(2, size); put32(10, 54); put32(14, 40); put32(18, (uint32_t)w); put32(22, (uint32_t)h);
    b[26] = 1; b[28] = 32; put32(34, (uint32_t)w * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint32_t p = px[(size_t)(h - 1 - y) * w + x];   // (bottom-up rows; the atlas is premultiplied RGBA, 0xAABBGGRR: BGRA here)
            unsigned char* q = &b[54 + ((size_t)y * w + x) * 4];
            q[0] = (unsigned char)(p >> 16); q[1] = (unsigned char)(p >> 8); q[2] = (unsigned char)p; q[3] = (unsigned char)(p >> 24);
        }
    FILE* f = nullptr;
    const errno_t open = _wfopen_s(&f, path, L"wb");
    if (open != 0 || !f) {
        if (err) *err = open ? open : EIO;
        return false;
    }
    const bool ok = std::fwrite(b.data(), 1, b.size(), f) == b.size();
    if (!ok && err) *err = errno ? errno : EIO;
    std::fclose(f);
    return ok;
}

bool Drain(int ms) {
    std::unique_lock<std::mutex> lock(g_m);
    return g_idle.wait_for(lock, std::chrono::milliseconds(ms), [] { return g_queue.empty() && !g_busy; });
}

}  // namespace ownlabels
