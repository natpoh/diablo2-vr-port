// flat_menu.cpp - [hud] game_menu_settings on the game: the pause menu's button and the panel (D2RLoader's overlay), the mouse (the game
// window's procedure), the ini's writes (WritePrivateProfileString, one key at a time, on a thread of its own), the host's rules channel
// "HR" and the console's "vrrules". What it does and why: vr/flat_menu.h, vr/host_rules.h.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>

#include <D2RLPlugin/api.h>

#include <atomic>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "flat_menu.h"
#include "flat_menu_wire.h"
#include "key_capture_wire.h"   // a key row's "press a key" (vr/key_capture.h)
#include "host_rules.h"
#include "log_async.h"

namespace gamestate { bool PauseMenuOpen(); }   // (vr/gamestate.cpp)

namespace flatmenu {
namespace {

using namespace D2RL;
namespace nw = D2RL::Network;
namespace ov = D2RL::Overlay;
using hostrules::Role;
using hostrules::Values;

const PluginContext* g_ctx = nullptr;
std::wstring g_ini;

void Say(const char* fmt, ...) {
    if (!g_ctx) return;
    char b[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    logasync::Info(g_ctx, b);
}

// ---- the keys ----
std::atomic<bool> g_on{false};
std::atomic<bool> g_flat{false};
std::atomic<int> g_listKey{0};      // [hud] host_rules_key's choice
std::atomic<int> g_panelKey{0};     // [hud] game_menu_key's choice

// ---- the host's rules ----
std::mutex g_rulesLock;              // the values below
Values g_own;                        // ours (LoadSettings)
Values g_host;                       // the host's last answer
uint32_t g_hostNonce = 0;
std::atomic<uint64_t> g_hostAt{0};   // when it came
uint32_t g_shownFor = 0;             // the host whose rules the join's list was shown for
std::atomic<uint64_t> g_joinShowAt{0};
uint32_t g_myNonce = 0;              // this process's number (Init)

Role RoleNow(uint64_t now) {
    std::lock_guard<std::mutex> lock(g_rulesLock);
    return hostrules::RoleOf(hostrules::Fresh(now, g_hostAt.load()), g_myNonce, g_hostNonce);
}
void RulesNow(uint64_t now, Role* role, Values* own, Values* host) {
    std::lock_guard<std::mutex> lock(g_rulesLock);
    *role = hostrules::RoleOf(hostrules::Fresh(now, g_hostAt.load()), g_myNonce, g_hostNonce);
    *own = g_own;
    *host = g_host;
}

// ---- the channel ----
const NetworkService* g_net = nullptr;
nw::ChannelHandle g_channel = nw::InvalidChannelHandle;
std::atomic<bool> g_netDead{false};

void __cdecl OnHost(const PluginContext*, nw::ChannelHandle, nw::PeerHandle peer, uint16_t id, const void* data, uint32_t size, void*) noexcept {
    if (g_netDead.load() || id != hostrules::kMsgHello) return;
    uint32_t asker = 0;
    if (!hostrules::ParseHello(data, size, &asker)) return;
    Values own;
    {
        std::lock_guard<std::mutex> lock(g_rulesLock);
        own = g_own;
    }
    uint8_t b[hostrules::kMaxPayload];
    const uint32_t n = hostrules::BuildRules(asker, g_myNonce, own, b, sizeof b);
    if (n && g_net && g_ctx) g_net->sendToClient(g_ctx, g_channel, peer, hostrules::kMsgRules, b, n);
    static std::atomic<int> told{0};
    if (asker != g_myNonce && told.fetch_add(1) < 4) Say("vrcam: host rules - a joined player's mod asked our rules, answered (%u rules)", (unsigned)hostrules::kCount);
}
void __cdecl OnClient(const PluginContext*, nw::ChannelHandle, uint16_t id, const void* data, uint32_t size, void*) noexcept {
    if (g_netDead.load() || id != hostrules::kMsgRules) return;
    uint32_t asker = 0, host = 0;
    Values v;
    if (!hostrules::ParseRules(data, size, &asker, &host, &v) || asker != g_myNonce) return;
    const uint64_t now = GetTickCount64();
    bool first = false, changed = false;
    {
        std::lock_guard<std::mutex> lock(g_rulesLock);
        for (int i = 0; i < hostrules::kCount; ++i) if (v.known[i] != g_host.known[i] || v.v[i] != g_host.v[i]) changed = true;
        changed = changed || host != g_hostNonce;
        g_host = v;
        g_hostNonce = host;
        g_hostAt.store(now);
        if (host != g_myNonce && host != g_shownFor) { g_shownFor = host; first = true; }
    }
    if (first) g_joinShowAt.store(now);   // a game someone else hosts: its rules on the screen for a while (flat_menu.h ListShown)
    if (changed) {
        std::string s;
        for (int i = 0; i < hostrules::kCount; ++i) {
            char b[96];
            snprintf(b, sizeof b, "%s%ls=%s", i ? ", " : "", hostrules::kRules[i].key, v.known[i] ? hostrules::kRules[i].names[hostrules::Clamp(i, v.v[i])] : "?");
            s += b;
        }
        Say("vrcam: host rules - %s: %s", host == g_myNonce ? "our own game (single player or we host), our keys" : "joined, the host's", s.c_str());
    }
}
std::atomic<int> g_failed{0};
void __cdecl OnConnection(const PluginContext*, const nw::ConnectionEvent* ev, void*) noexcept {
    if (!ev || g_netDead.load()) return;
    if (ev->state == nw::ConnectionState::Rejected) g_failed.fetch_add(1);
    static std::atomic<int> told{0};
    if (told.fetch_add(1) < 8) Say("vrcam: host rules - the channel: state %u, reason %u", (unsigned)ev->state, (unsigned)ev->reason);
}

void NetTick(uint64_t now, bool inGame) {
    static uint64_t triedAt = 0, helloAt = 0;
    static bool was = false;
    if (!g_net || g_channel == nw::InvalidChannelHandle || g_netDead.load()) return;
    if (!inGame) {
        if (was) { triedAt = 0; g_failed.store(0); }   // a new game: asked again at once
        was = false;
        return;
    }
    was = true;
    nw::ChannelInfo info{};
    info.structSize = nw::ChannelInfoSize;
    const bool got = g_net->getChannelInfo(g_ctx, g_channel, &info) == nw::Result::Success;
    const bool conn = got && info.connectionState == nw::ConnectionState::Connected;
    const bool connecting = got && info.connectionState == nw::ConnectionState::Connecting;
    if (conn) {
        g_failed.store(0);
        if (now - helloAt >= hostrules::kHelloMs) {
            helloAt = now;
            uint8_t b[hostrules::kHelloBytes];
            if (const uint32_t n = hostrules::BuildHello(g_myNonce, b, sizeof b)) g_net->sendToHost(g_ctx, g_channel, hostrules::kMsgHello, b, n);
        }
        return;
    }
    if (triedAt && now - triedAt < hostrules::RetryMs((unsigned)g_failed.load(), connecting)) return;
    if (triedAt && !connecting) g_failed.fetch_add(1);
    triedAt = now;
    const nw::Result r = g_net->connectToHost(g_ctx, g_channel);
    static int toldR = -1;
    if ((int)r != toldR) {
        toldR = (int)r;
        Say("vrcam: host rules - asked the host's channel: %u (0 = asked; online, or a host without this mod version: no answer, asked again "
            "later, up to once a minute)", (unsigned)r);
    }
}

// ---- the panel's values and the ini's writes ----
std::mutex g_valLock;
float g_val[kRows] = {};
std::atomic<bool> g_open{false};
std::atomic<bool> g_fromPause{false};   // opened from the pause menu's button (closes with it); else by the key (stays)

struct Job { int row = -1; float value = 0; };   // row -1: read every row again
std::mutex g_jobLock;
std::condition_variable g_jobCv;
std::deque<Job> g_jobs;
std::thread g_writer;
bool g_stop = false;

float ReadRow(int r) {
    const RowDef d = Def(r);
    wchar_t t[64] = L"";
    GetPrivateProfileStringW(d.section, d.key, L"", t, (DWORD)std::size(t), g_ini.c_str());
    return ParseValue(d, t);
}
void ReadAll() {
    float v[kRows];
    for (int r = 0; r < kRows; ++r) v[r] = ReadRow(r);
    std::lock_guard<std::mutex> lock(g_valLock);
    for (int r = 0; r < kRows; ++r) g_val[r] = v[r];
}
void WriterLoop() {
    for (;;) {
        Job j;
        {
            std::unique_lock<std::mutex> lock(g_jobLock);
            g_jobCv.wait(lock, [] { return g_stop || !g_jobs.empty(); });
            if (g_stop) return;
            j = g_jobs.front();
            g_jobs.pop_front();
        }
        if (j.row >= 0 && j.row < kRows && !g_ini.empty()) {
            const RowDef d = Def(j.row);
            const std::wstring text = IniText(j.value);
            const BOOL ok = WritePrivateProfileStringW(d.section, d.key, text.c_str(), g_ini.c_str());   // one key: never the whole file
            const float back = ReadRow(j.row);
            Say("vrcam: game menu settings - [%ls] %ls=%ls %s", d.section, d.key, text.c_str(),
                ok && std::fabs(back - j.value) < 1e-3f ? "written (the ini's watch applies it where the key is live)" : "NOT written - the ini could not be written");
        }
        ReadAll();
    }
}
void Post(const Job& j) {
    std::lock_guard<std::mutex> lock(g_jobLock);
    if (!g_writer.joinable()) {
        g_stop = false;
        g_writer = std::thread(WriterLoop);
    }
    if (j.row < 0) for (const Job& q : g_jobs) if (q.row < 0) return;   // one read again is enough
    g_jobs.push_back(j);
    g_jobCv.notify_one();
}

// ---- the overlay ----
const OverlayService* g_overlay = nullptr;
std::atomic<ov::CallbackHandle> g_frameCb{ov::InvalidCallbackHandle};
std::atomic<float> g_ow{0}, g_oh{0};       // the overlay's screen, its last frame
std::atomic<int> g_hover{-1};              // the panel's part under the pointer: row * 8 + part, -2 the pause menu's button
std::atomic<bool> g_keyHeld{false};

ov::Color C(int r, int g, int b, float a = 1.0f) { return {r / 255.0f, g / 255.0f, b / 255.0f, a}; }
void Fill(const ov::Frame* f, const Rect& r, ov::Color c, float round = 0) {
    ov::FilledRectangleRequest q{};
    q.structSize = ov::FilledRectangleRequestSize;
    q.canvas = f->canvas;
    q.rect = {r.x, r.y, r.x + r.w, r.y + r.h};
    q.color = c;
    q.rounding = round;
    g_overlay->drawFilledRectangle(g_ctx, &q);
}
void Outline(const ov::Frame* f, const Rect& r, ov::Color c, float thick) {
    ov::RectangleRequest q{};
    q.structSize = ov::RectangleRequestSize;
    q.canvas = f->canvas;
    q.rect = {r.x, r.y, r.x + r.w, r.y + r.h};
    q.color = c;
    q.thickness = thick;
    g_overlay->drawRectangle(g_ctx, &q);
}
float Width(const ov::Frame* f, float size, const std::string& s) {
    if (s.empty() || !g_overlay->measureText) return 0;
    ov::MeasureTextRequest q{};
    q.structSize = ov::MeasureTextRequestSize;
    q.canvas = f->canvas;
    q.textSize = size;
    q.text = s.data();
    q.textLength = (uint32_t)std::min<size_t>(s.size(), ov::MaxTextBytes);
    ov::TextMetrics m{};
    m.structSize = ov::TextMetricsSize;
    return g_overlay->measureText(g_ctx, &q, &m) == ov::Result::Success ? m.width : (float)s.size() * size * 0.5f;
}
enum class Align { Left, Center, Right };
void Text(const ov::Frame* f, const Rect& r, float size, ov::Color c, const std::string& s, Align a = Align::Left) {
    if (s.empty()) return;
    ov::TextRequest q{};
    q.structSize = ov::TextRequestSize;
    q.canvas = f->canvas;
    const float x = a == Align::Center ? r.x + (r.w - Width(f, size, s)) * 0.5f : a == Align::Right ? r.x + r.w - Width(f, size, s) : r.x;
    q.position = {x, r.y + (r.h - size) * 0.5f};
    q.color = c;
    q.textSize = size;
    q.text = s.data();
    q.textLength = (uint32_t)std::min<size_t>(s.size(), ov::MaxTextBytes);
    g_overlay->drawText(g_ctx, &q);
}

const ov::Color kBack = C(12, 18, 24, 0.94f), kRowA = C(24, 34, 44, 0.9f), kRowB = C(18, 27, 36, 0.9f), kButton = C(36, 56, 70),
                kHot = C(70, 112, 124), kOn = C(40, 96, 84), kText = C(230, 236, 238), kQuiet = C(150, 170, 180), kAccent = C(214, 186, 120),
                kWarn = C(241, 207, 139), kEdge = C(120, 100, 60);

void Button(const ov::Frame* f, const Rect& r, const std::string& s, float size, bool hot, bool lit = false) {
    Fill(f, r, hot ? kHot : lit ? kOn : kButton, 4);
    Text(f, r, size, kText, s, Align::Center);
}

void DrawList(const ov::Frame* f, uint64_t now) {
    Role role;
    Values own, host;
    RulesNow(now, &role, &own, &host);
    const std::vector<hostrules::Line> lines = hostrules::List(role, own, host);
    const float s = f->screenHeight > 0 ? f->screenHeight / 1080.0f : 1.0f;
    const float size = 20.0f * s, lineH = 28.0f * s, pad = 14.0f * s;
    float w = 0;
    for (const hostrules::Line& l : lines) w = std::max(w, Width(f, size, l.text));
    const Rect box{24.0f * s, 24.0f * s, w + 2 * pad, lines.size() * lineH + 2 * pad};
    Fill(f, box, kBack, 6);
    Outline(f, box, kEdge, 1.5f * s);
    float y = box.y + pad;
    for (const hostrules::Line& l : lines) {
        Text(f, {box.x + pad, y, w, lineH}, size, l.title ? kAccent : l.host ? kWarn : kText, l.text);
        y += lineH;
    }
}

// A key row's capture (vr/key_capture_wire.h): its answer written like a click's value; a refused key said under the row for a moment.
std::atomic<int> g_keyRefusedRow{-1};
std::atomic<unsigned> g_keyRefusedVk{0};
std::atomic<uint64_t> g_keyRefusedAt{0};
void KeyAnswers(uint64_t now) {
    for (int r = 0; r < kOwnCount; ++r) {
        if (!IsKey(r)) continue;
        keycap::Res res;
        if (!keycapwire::Take(keycapwire::kFlatMenu + r, &res)) continue;
        char name[32];
        keycap::Text(res.vk, name, sizeof name);
        if (res.r == keycap::R::Took) {
            {
                std::lock_guard<std::mutex> lock(g_valLock);
                g_val[r] = static_cast<float>(res.vk);   // shown at once; the writer reads it back
            }
            Post({r, static_cast<float>(res.vk)});
            Say("vrcam: game menu settings - [%ls] %ls = %u (%s, pressed)", Def(r).section, Def(r).key, res.vk, name);
        } else if (res.r == keycap::R::Refused) {
            g_keyRefusedRow.store(r);
            g_keyRefusedVk.store(res.vk);
            g_keyRefusedAt.store(now);
        }
    }
}

void DrawPanel(const ov::Frame* f, uint64_t now) {
    KeyAnswers(now);
    const Layout L = Lay(f->screenWidth, f->screenHeight);
    float v[kRows];
    {
        std::lock_guard<std::mutex> lock(g_valLock);
        for (int r = 0; r < kRows; ++r) v[r] = g_val[r];
    }
    Role role;
    Values own, host;
    RulesNow(now, &role, &own, &host);
    const int hover = g_hover.load();
    Fill(f, L.panel, kBack, 8);
    Outline(f, L.panel, kEdge, 2.0f * L.scale);
    Text(f, L.title, L.text * 1.25f, kAccent, uitext::Tr("vr.flatmenu.title", "D2R VR settings"), Align::Center);
    Text(f, L.ownHead, L.text, kAccent, uitext::Tr("vr.flatmenu.own", "Your game (2D) - saved in d2r_vr.ini, live"));
    Text(f, L.rulesHead, L.text, kAccent, uitext::Tr("vr.rules.title", "Host rules (who hosts decides)"));
    Text(f, L.role, L.small, role == Role::Joined ? kWarn : kQuiet, hostrules::RoleLine(role));
    const bool joined = role == Role::Joined;
    for (int r = 0; r < kRows; ++r) {
        Fill(f, L.row[r], (r & 1) ? kRowB : kRowA);
        Rect label = L.row[r];
        label.x += 10.0f * L.scale;
        label.w = L.minus[r].x - label.x - 8.0f * L.scale;
        Text(f, label, L.small, kText, Label(r));
        const int i = RuleOf(r);
        if (i >= 0 && joined) {   // the host's value, only shown
            const std::string s = host.known[i] ? hostrules::ValueName(i, host.v[i]) : std::string("?");
            Text(f, L.value[r], L.small, kWarn, s + "  (" + uitext::Tr("vr.flatmenu.host", "host") + ")", Align::Center);
            continue;
        }
        const std::string shown = Shown(r, v[r]);
        if (HasSteps(r)) {
            Button(f, L.minus[r], "-", L.text, hover == r * 8 + (int)Part::Minus);
            Fill(f, L.value[r], kRowB, 4);
            Text(f, L.value[r], L.small, kText, shown, Align::Center);
            Button(f, L.plus[r], "+", L.text, hover == r * 8 + (int)Part::Plus);
        } else if (IsKey(r)) {   // a key: its name; armed, "press a key"; a key it cannot be, why (3 s)
            const bool armed = keycapwire::Armed(keycapwire::kFlatMenu + r);
            Button(f, L.value[r], armed ? uitext::Tr("vr.flatmenu.press_key", "Press a key (Esc: cancel)") : shown, L.small,
                   hover == r * 8 + (int)Part::Value, armed);
            if (g_keyRefusedRow.load() == r && now - g_keyRefusedAt.load() < 3000) {
                char name[32];
                keycap::Text(g_keyRefusedVk.load(), name, sizeof name);
                const char* why = keycap::Refusal(g_keyRefusedVk.load(), KeyUse(r));
                Rect note = label;
                Text(f, note, L.small, kWarn, std::string(name) + ": " + (why ? why : "?"), Align::Right);
            }
        } else {
            Button(f, L.value[r], shown, L.small, hover == r * 8 + (int)Part::Value, v[r] >= 0.5f);
        }
    }
    Button(f, L.close, uitext::Tr("vr.flatmenu.close", "Close (Esc)"), L.text, hover == (int)Part::Close);
}

void __cdecl OnFrame(const PluginContext*, const ov::Frame* f, void*) noexcept {
    if (!f || !g_overlay || !g_on.load()) return;
    g_ow.store(f->screenWidth);
    g_oh.store(f->screenHeight);
    const uint64_t now = GetTickCount64();
    const bool flat = g_flat.load(), pause = gamestate::PauseMenuOpen();
    if (g_open.load() && !PanelStays(flat, g_fromPause.load(), pause)) {
        g_open.store(false);
        Say("vrcam: game menu settings - the panel closed (%s)", flat ? "with the pause menu" : "not the flat game");
    }
    const bool open = g_open.load();
    if (!open)   // a key row's capture never outlives the panel (its next key would be taken with nothing on the screen)
        for (int r = 0; r < kOwnCount; ++r)
            if (IsKey(r)) keycapwire::Cancel(keycapwire::kFlatMenu + r);
    if (open) DrawPanel(f, now);
    else if (flat && pause) {
        const Rect b = MenuButton(f->screenWidth, f->screenHeight);
        Fill(f, b, g_hover.load() == -2 ? kHot : kButton, 6);
        Outline(f, b, kEdge, 1.5f * b.h / 52.0f);
        Text(f, b, 24.0f * b.h / 52.0f, kText, uitext::Tr("vr.flatmenu.button", "D2R VR settings"), Align::Center);
    }
    if (ListShown(true, flat, g_keyHeld.load(), pause, open, RoleNow(now), now, g_joinShowAt.load())) DrawList(f, now);
}

// D2RLoader takes a frame callback only while the plugin loads (PluginSDK README, "Display-only Overlays"): 0.202.2 asked from the update
// thread once the box was on and got Busy (4) every time - no button, no panel (Serg's log 2026-11-05). So: once, at the load, whether the
// box is on or not; OnFrame draws nothing with it off (one atomic read a frame).
void Register() {
    if (!g_overlay || !g_ctx || g_frameCb.load() != ov::InvalidCallbackHandle) return;
    ov::CallbackRegistration reg{};
    reg.structSize = ov::CallbackRegistrationSize;
    reg.phase = ov::Phase::AfterGameUi;
    reg.callback = &OnFrame;
    ov::CallbackHandle h = ov::InvalidCallbackHandle;
    const ov::Result r = g_overlay->registerFrameCallback(g_ctx, &reg, &h);
    if (r == ov::Result::Success) g_frameCb.store(h);
    char b[240];
    snprintf(b, sizeof b, "vrcam: game menu settings - D2RLoader's overlay %s (result %u)",
             r == ov::Result::Success ? "taken at the load: the panel, the pause menu's button, the rules' list draw with [hud] game_menu_settings on"
                                      : "NOT taken - nothing is drawn, the key and the button do nothing", (unsigned)r);
    g_ctx->LogInfo(b);
}
void Unregister() {
    const ov::CallbackHandle h = g_frameCb.exchange(ov::InvalidCallbackHandle);
    if (h != ov::InvalidCallbackHandle && g_overlay && g_ctx) g_overlay->unregisterFrameCallback(g_ctx, h);
}
bool CanDraw() { return g_frameCb.load() != ov::InvalidCallbackHandle; }

void Open(bool fromPause, const char* how) {
    g_fromPause.store(fromPause);
    g_open.store(true);
    g_hover.store(-1);
    Post({});   // the values as the ini has them now
    Say("vrcam: game menu settings - the panel opened %s", how);
}

// ---- the console ----
ConsoleCommandResult __cdecl CmdRules(D2R::Game::Client*, const ConsoleCommandContext*, void*) noexcept {
    if (!g_ctx) return ConsoleCommandResult::Failed;
    Role role;
    Values own, host;
    RulesNow(GetTickCount64(), &role, &own, &host);
    for (const hostrules::Line& l : hostrules::List(role, own, host)) g_ctx->WriteConsoleMessage(l.text.c_str());
    if (!g_on.load())
        g_ctx->WriteConsoleMessage(uitext::Tr("vr.rules.console_off", "(The host is asked only with Settings > Mechanics > \"In-game D2R VR settings\" ticked.)"));
    return ConsoleCommandResult::Handled;
}

}  // namespace

void Init(const PluginContext* ctx, const wchar_t* iniPath) {
    g_ctx = ctx;
    g_ini = iniPath ? iniPath : L"";
    {
        LARGE_INTEGER t{};
        QueryPerformanceCounter(&t);
        uint32_t n = (uint32_t)(t.QuadPart ^ (t.QuadPart >> 32)) ^ (GetCurrentProcessId() * 2654435761u);
        g_myNonce = n ? n : 1;
    }
    for (int i = 0; i < hostrules::kCount; ++i) { g_own.v[i] = hostrules::kRules[i].fallback; g_own.known[i] = true; }
    for (int r = 0; r < kRows; ++r) g_val[r] = Def(r).fallback;
    if (!ctx) return;
    if (ctx->QueryService(&g_overlay) != ServiceQueryResult::Success || !HasOverlayServiceField(g_overlay, OverlayServiceRequiredSize)) {
        g_overlay = nullptr;
        ctx->LogInfo("vrcam: game menu settings - D2RLoader has no overlay service: no in-game panel ([hud] game_menu_settings)");
    } else {
        Register();
    }
    if (ctx->QueryService(&g_net) != ServiceQueryResult::Success || !HasNetworkServiceField(g_net, NetworkServiceRequiredSize)) {
        g_net = nullptr;
        ctx->LogInfo("vrcam: host rules - D2RLoader has no network service: the host's rules are not shown to a joined player");
    } else {
        nw::ChannelRegistration reg{};
        reg.structSize = nw::ChannelRegistrationSize;
        reg.localChannelId = hostrules::kChannel;
        reg.compatibilityToken = hostrules::kToken;
        reg.hostMessage = &OnHost;
        reg.clientMessage = &OnClient;
        reg.connectionState = &OnConnection;
        const nw::Result r = g_net->registerChannel(ctx, &reg, &g_channel);
        char b[200];
        snprintf(b, sizeof b, "vrcam: host rules - channel %u %s (a joined player's mod reads the host's rules: only shown, nothing changes in play)",
                 (unsigned)hostrules::kChannel, r == nw::Result::Success ? "registered" : "NOT registered");
        ctx->LogInfo(b);
        if (r != nw::Result::Success) g_channel = nw::InvalidChannelHandle;
    }
    if (!ctx->RegisterConsoleCommand("vrrules", &CmdRules, "vrrules - the host's rules (who hosts decides) and who decides in this game"))
        ctx->LogError("vrcam: the console command vrrules did not register");
}

void Shutdown() {
    g_netDead.store(true);
    Unregister();
    if (g_net && g_ctx && g_channel != nw::InvalidChannelHandle) g_net->unregisterChannel(g_ctx, g_channel);
    g_channel = nw::InvalidChannelHandle;
    {
        std::lock_guard<std::mutex> lock(g_jobLock);
        g_stop = true;
        g_jobCv.notify_all();
    }
    if (g_writer.joinable()) g_writer.join();
}

void Configure(bool on, int panelKey, int listKey, bool flat) {
    const bool was = g_on.exchange(on);
    const int keyWas = g_panelKey.exchange(panelKey);
    g_listKey.store(listKey);
    g_flat.store(flat);
    if (was != on || (on && keyWas != panelKey))
        Say("vrcam: game menu settings %s ([hud] game_menu_settings %d, game_menu_key %d, host_rules_key %d)",
            on ? "ON: the panel on its key and the pause menu's button (flat), the host's rules' list" : "off", on ? 1 : 0, panelKey, listKey);
    if (!on) { g_open.store(false); g_keyHeld.store(false); }
}

bool TakesKey(unsigned vk) {
    return vk && g_on.load() && g_flat.load() && PanelKey(g_panelKey.load()) == vk;
}

void SetOwnRules(const Values& v) {
    std::lock_guard<std::mutex> lock(g_rulesLock);
    g_own = v;
}

void Tick(uint64_t nowMs, bool inWorld) {
    static uint64_t at = 0, readAt = 0;
    if (nowMs - at < 50) return;
    at = nowMs;
    const bool on = g_on.load();
    NetTick(nowMs, on && inWorld);
    if (g_open.load() && nowMs - readAt >= 2000) {   // an edit elsewhere (D2R VR Settings) shows while the panel is up
        readAt = nowMs;
        Post({});
    }
}

bool OnWindowMessage(void* hwnd, unsigned msg, uintptr_t w, intptr_t l, intptr_t* result) {
    static bool swallowUp = false, swallowEscUp = false;
    if (!g_on.load() || !g_flat.load()) {
        g_keyHeld.store(false);
        return false;
    }
    static PanelKeyGate gate;   // this window's thread only
    const unsigned panelVk = PanelKey(g_panelKey.load());
    const bool keyMsg = msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP;   // (F10: the SYS ones)
    if (keyMsg && panelVk && w == panelVk) {   // [hud] game_menu_key: the panel itself, menus or not
        const bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
        const PanelKeyGate::Act a = gate.Feed((unsigned)w, panelVk, down, (l & (1 << 30)) != 0, (GetKeyState(VK_CONTROL) & 0x8000) != 0);
        if (a == PanelKeyGate::Act::Toggle) {
            if (g_open.load()) {
                g_open.store(false);
                swallowUp = false;
                Say("vrcam: game menu settings - the panel closed (its key)");
            } else if (CanDraw()) {
                Open(false, "by its key");
            } else {
                static bool told = false;
                if (!told) { told = true; Say("vrcam: game menu settings - its key pressed, but D2RLoader's overlay is not ours: nothing to show"); }
            }
        }
        if (a != PanelKeyGate::Act::Pass) {
            *result = 0;
            return true;
        }
    }
    const unsigned key = ListKey(g_listKey.load());
    if (key && key != panelVk && (msg == WM_KEYDOWN || msg == WM_KEYUP) && w == key) {   // the rules' list while the key is held
        g_keyHeld.store(msg == WM_KEYDOWN);
        *result = 0;
        return true;
    }
    const bool pause = gamestate::PauseMenuOpen();
    if (g_open.load() && !PanelStays(true, g_fromPause.load(), pause)) g_open.store(false);
    const bool open = g_open.load();
    if (msg == WM_KEYUP && w == VK_ESCAPE && swallowEscUp) {   // the releases of the presses that closed the panel (with or without the pause menu)
        swallowEscUp = false;
        *result = 0;
        return true;
    }
    if (msg == WM_LBUTTONUP && swallowUp) {
        swallowUp = false;
        *result = 0;
        return true;
    }
    if (!pause && !open) {   // no pause menu, no panel: nothing of ours on the screen
        if (g_hover.load() != -1) g_hover.store(-1);
        return false;
    }
    if (msg == WM_KEYDOWN && w == VK_ESCAPE && open) {   // Esc closes our panel only: the pause menu stays (none opens)
        g_open.store(false);
        swallowEscUp = true;
        *result = 0;
        return true;
    }
    const bool mouse = msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST;
    if (!mouse) return false;
    if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) {
        if (!open) return false;
        *result = 0;
        return true;
    }
    RECT rc{};
    GetClientRect((HWND)hwnd, &rc);
    float x = 0, y = 0;
    const float ow = g_ow.load(), oh = g_oh.load();
    ToOverlay((float)GET_X_LPARAM(l), (float)GET_Y_LPARAM(l), (float)rc.right, (float)rc.bottom, ow, oh, &x, &y);
    const Role role = RoleNow(GetTickCount64());
    if (msg == WM_MOUSEMOVE) {   // the game keeps its pointer
        if (open) {
            const Hit h = HitTest(Lay(ow, oh), x, y, role);
            g_hover.store(h.part == Part::Close ? (int)Part::Close : h.row >= 0 ? h.row * 8 + (int)h.part : -1);
        } else {
            g_hover.store(MenuButton(ow, oh).In(x, y) ? -2 : -1);
        }
        return false;
    }
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
        if (!open) {
            if (!pause || !CanDraw() || !MenuButton(ow, oh).In(x, y)) return false;
            Open(true, "from the pause menu");
        } else {
            const Hit h = HitTest(Lay(ow, oh), x, y, role);
            float now = 0, next = 0;
            if (h.part == Part::Close) {
                g_open.store(false);
            } else if (h.row >= 0 && h.part == Part::Value && IsKey(h.row)) {   // a key row: the next key pressed is its value (Esc: as it was)
                if (keycapwire::Armed(keycapwire::kFlatMenu + h.row)) keycapwire::Cancel(keycapwire::kFlatMenu + h.row);
                else keycapwire::Arm(keycapwire::kFlatMenu + h.row, KeyUse(h.row));
            } else if (h.row >= 0) {
                {
                    std::lock_guard<std::mutex> lock(g_valLock);
                    now = g_val[h.row];
                }
                if (Click(h, now, &next)) {
                    {
                        std::lock_guard<std::mutex> lock(g_valLock);
                        g_val[h.row] = next;   // shown at once; the writer reads it back
                    }
                    Post({h.row, next});
                }
            }
        }
        swallowUp = true;
        *result = 0;
        return true;
    }
    if (open) {   // the panel is modal: no other button reaches the game under it
        *result = 0;
        return true;
    }
    return false;
}

}  // namespace flatmenu
