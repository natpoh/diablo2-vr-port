// ally_portraits_wire.cpp - [hud] allies_place: the allies' portraits under the map in the hand, or on the right wrist (vr/ally_portraits.h).
//
// What the party and the pets are comes from vr/ally_portraits_game.cpp (read five times a second while the allies are not the game's row); the picture
// is painted on a worker thread of its own, only when a tile changed (a life, a name, who is there, the count across). xr.cpp asks each frame whether
// there is a picture to hang and fetches the newest one: under the map it hangs a map (the orb in the hand, or the big map), on the wrist where this
// file's glance put the panel - the BACK of the right wrist turned to the eyes, as the wrist status (vr/hand_vitals.h Reveal, the wrist status'
// place settings). F4 only: the view gate decides when the reads are queued, when the glance runs and when the layer is asked for.
//
// Nothing here writes to the game, presses anything or takes a control from anyone.

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "ally_portraits_wire.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

#include <D2RLPlugin/api.h>

#include "ally_portraits_game.h"
#include "ally_portraits_paint.h"
#include "direct_input.h"
#include "view_gate.h"
#include "wrist_status_wire.h"
#include "armed_icon.h"        // [hud] skill_under_map: the right hand's skill as the mana medallion picks it (armedicon::Pick)
#include "bow_quiver_wire.h"   // ... the bow's chosen skill
#include "melee_arm_game.h"    // ... the armed melee skill
#include "link.h"              // (BodyWalk's Game Link types, which skill_link_wire.h names)
#include "skill_link_wire.h"   // ... a glyph's armed skill on either trigger
#include "wheel_icons.h"       // ... its icon's revision (a decoded icon repaints)

namespace allyportraits {
namespace {

const D2RL::PluginContext* g_ctx = nullptr;
void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

std::atomic<int> g_place{alliesplace::kDefault};
std::atomic<float> g_size{125.0f};
std::atomic<int> g_mapCols{alliesplace::kMapColsDefault}, g_wristCols{alliesplace::kWristColsDefault};

// -- the painter: one worker thread, the newest look wins ---------------------------------------------------------------------------------

class Painter {
public:
    // The draw thread. A look not the same as the last one asked for is a new picture with a new revision; the same look is the revision it had.
    uint32_t Submit(const Look& look) {
        std::unique_lock<std::mutex> lock(m_);
        if (valid_ && look == last_) return rev_;
        last_ = look;
        valid_ = true;
        want_ = look;
        has_ = true;
        rev_ = ++serial_;
        if (!th_.joinable()) th_ = std::thread([this] { Run(); });
        const uint32_t rev = rev_;
        lock.unlock();
        cv_.notify_one();
        return rev;
    }
    bool Fetch(std::vector<uint32_t>* out, uint32_t* rev, uint32_t* w, uint32_t* h) {
        std::lock_guard<std::mutex> lock(m_);
        if (done_ == 0 || done_ == fetched_) return false;
        out->swap(px_);
        *rev = done_;
        *w = w_;
        *h = h_;
        fetched_ = done_;
        return true;
    }
    void Reset() {
        std::lock_guard<std::mutex> lock(m_);
        valid_ = has_ = false;
        fetched_ = done_ = 0;
        px_.clear();
        ++epoch_;
    }
    void Stop() {
        {
            std::lock_guard<std::mutex> lock(m_);
            stop_ = true;
        }
        cv_.notify_all();
        if (th_.joinable()) th_.join();
    }

private:
    void Run() {
        std::vector<uint32_t> local;
        for (;;) {
            Look look;
            uint32_t rev = 0, epoch = 0;
            {
                std::unique_lock<std::mutex> lock(m_);
                cv_.wait(lock, [this] { return stop_ || has_; });
                if (stop_) return;
                look = want_;
                rev = rev_;
                epoch = epoch_;
                has_ = false;
            }
            allyportraitspaint::Render(look, local);
            std::lock_guard<std::mutex> lock(m_);
            if (epoch != epoch_) continue;   // a reset came while painting: this picture belongs to the last session
            px_.swap(local);
            w_ = (uint32_t)PicW(look);
            h_ = (uint32_t)PicH(look);
            done_ = rev;
        }
    }

    std::mutex m_;
    std::condition_variable cv_;
    std::thread th_;
    Look last_, want_;
    bool valid_ = false, has_ = false, stop_ = false;
    uint32_t serial_ = 0, rev_ = 0, done_ = 0, fetched_ = 0, epoch_ = 0, w_ = 0, h_ = 0;
    std::vector<uint32_t> px_;
};

// Two pictures, each its own painter: under the map (kUnderMap: the skills' row and the allies when they are there) and on the right wrist (kOnWrist:
// the allies) - both can be up at once (the skills under the map, the allies on the wrist).
Painter& P(int where) {
    static Painter* painter[2] = {new Painter, new Painter};   // joined in Shutdown; no static destructor runs a thread's end at unload
    return *painter[where & 1];
}

bool g_wanted[2] = {};   // the draw thread's: hung lately
uint32_t g_needRev[2] = {};
std::atomic<bool> g_skills{false};   // [hud] skill_under_map

// [hud] skill_under_map: the armed skills, from where the mana and life medallions take theirs (vr/render/effects/effects.cpp's icon block): the right
// hand's - the bow's chosen skill, else a glyph's on the right trigger, else the armed melee skill (armedicon::Pick); the left hand's - a glyph's on the
// left trigger. With their names when the game side found them.
void AddSkills(Look* look) {
    bool meleeReady = false, bowReady = false;
    int bowSkill = -1;
    const int meleeSkill = meleearm::On() ? meleearm::Armed(&meleeReady) : -1;
    bowquiverwire::ChosenIcon(&bowSkill, &bowReady);
    const int glyphRight = skilllinkwire::SelectIcon() ? skilllinkwire::ArmedSkill(1) : -1;
    const armedicon::Source right = bowSkill < 0 && glyphRight >= 0 ? armedicon::Source{glyphRight, true} : armedicon::Pick(meleeSkill, meleeReady, bowSkill, bowReady);
    const int left = skilllinkwire::SelectIcon() ? skilllinkwire::ArmedSkill(0) : -1;
    look->nSkills = SkillTiles(right.skill, right.enough, left, true, &look->skill);
    game::WantSkillNames(right.skill, left);
    for (int j = 0; j < look->nSkills; ++j) game::SkillName(look->skill[(size_t)j].skill, look->skill[(size_t)j].name);
    if (look->nSkills) look->art = wheelicons::Revision();   // (an icon decoded later repaints)
}

// -- the right wrist's glance (place 2): direct_input's frames, the wrist status' reveal for the right wrist --------------------------------------

struct Glance {
    handvitals::Reveal reveal;
    bool seen = false;
    uint32_t session = 0;
    double lastTime = 0.0;
};
Glance g_glance;   // OnFrame's alone (Teardown between sessions)
struct Published {
    handvitals::Out out;
    bool active = false;
    ULONGLONG tick = 0;
};
std::mutex g_pubMutex;
Published g_pub;

bool g_wristHadTiles = false;   // OnFrame's: the last read had tiles (the panel does not come up for nobody)

void OnFrame(const directinput::Frame& f, void*) {
    Config c;
    c.place = g_place.load(std::memory_order_relaxed);
    if (!WristOn(c)) {
        if (g_glance.seen) {
            g_glance = Glance{};
            std::lock_guard<std::mutex> lock(g_pubMutex);
            g_pub = Published{};
        }
        return;
    }
    if (!g_glance.seen || f.session != g_glance.session) {
        g_glance = Glance{};
        g_glance.seen = true;
        g_glance.session = f.session;
    }
    float dt = 0.0f;
    if (g_glance.lastTime > 0.0 && f.time > g_glance.lastTime) dt = (float)(f.time - g_glance.lastTime);
    if (dt > 0.25f) { g_glance.reveal.Reset(); dt = 0.0f; }
    g_glance.lastTime = f.time;
    Look look;
    double ageMs = 0.0;
    g_wristHadTiles = game::Latest(&look, &ageMs) && Fresh(ageMs) && look.n > 0;
    const wriststatus::Config ws = wriststatus::Settings();   // its place: the same as the wrist status panel's
    handvitals::In in;
    in.dt = dt;
    in.head.valid = f.head.valid == 1;
    in.head.pos = {f.head.pos[0], f.head.pos[1], f.head.pos[2]};
    in.head.rot = {f.head.rot[0], f.head.rot[1], f.head.rot[2], f.head.rot[3]};
    for (int h = 0; h < 2; ++h) {
        const directinput::Hand& src = f.hand[h];
        in.hand[h].valid = src.grip.valid == 1;
        const hset::Q rot{src.grip.rot[0], src.grip.rot[1], src.grip.rot[2], src.grip.rot[3]};
        in.hand[h].pos = wriststatus::Anchor({src.grip.pos[0], src.grip.pos[1], src.grip.pos[2]}, rot, ws.alongCm);
        in.hand[h].rot = rot;
        in.hand[h].squeeze = src.squeeze;
    }
    in.cfg = wriststatus::RevealConfig(ws, 1);   // the right wrist
    const bool f4 = viewgate::F4();
    in.gate.hideNow = !f4 || (f.flags & directinput::kOtherView) != 0;
    in.gate.hide = (f.flags & directinput::kFocused) == 0 || (f.flags & directinput::kInMenu) != 0 || (f.flags & directinput::kUiUp) != 0 || !g_wristHadTiles;
    const handvitals::Out out = g_glance.reveal.Step(in);
    std::lock_guard<std::mutex> lock(g_pubMutex);
    g_pub.out = out;
    g_pub.active = g_glance.reveal.Active();
    g_pub.tick = GetTickCount64();
}

}  // namespace

void Init(const D2RL::PluginContext* ctx) {
    g_ctx = ctx;
    game::Init(ctx);
    directinput::Listen(&OnFrame, nullptr);   // (returns at once unless the allies are on the wrist)
}

void Configure(const Config& config) {
    const Config c = Sanitized(config);
    const int was = g_place.exchange(c.place);
    g_size.store(c.sizePct);
    g_mapCols.store(c.mapCols);
    g_wristCols.store(c.wristCols);
    const bool skillsWas = g_skills.exchange(c.skills);
    if (c.skills != skillsWas) Log(c.skills ? "vrcam: skill under the map on ([hud] skill_under_map: the armed skills over the allies, VR F4)" : "vrcam: skill under the map off");
    if (c.place != was) {
        static const char* const kName[] = {"the game's own row at the top", "under the map in the hand", "on the right wrist"};
        char b[220];
        snprintf(b, sizeof b, "vrcam: allies shown %s ([hud] allies_place=%d; %d across under the map, %d on the wrist; VR F4)", kName[c.place], c.place, c.mapCols,
                 c.wristCols);
        Log(b);
    }
}

Config Settings() {
    Config c;
    c.place = g_place.load(std::memory_order_relaxed);
    c.sizePct = g_size.load(std::memory_order_relaxed);
    c.mapCols = g_mapCols.load(std::memory_order_relaxed);
    c.wristCols = g_wristCols.load(std::memory_order_relaxed);
    c.skills = g_skills.load(std::memory_order_relaxed);
    return c;
}
bool Enabled() { return GameRowHidden(Settings()); }
bool GridWanted() { return GridOn(Settings()); }
bool WristWanted() { return WristOn(Settings()); }
bool UnderMapWanted() { const Config c = Settings(); return GridOn(c) || c.skills; }

// (the reads serve all three: our grid, our wrist tiles, and the width of the game's corner hidden while the allies are elsewhere)
void Tick(bool inGame) { game::Tick((Enabled() || g_skills.load(std::memory_order_relaxed)) && inGame && viewgate::F4()); }

bool Want(int where, uint32_t* needRevision) {
    where &= 1;
    Look look;
    double ageMs = 0.0;
    const Config c = Settings();
    const bool allies = (where == kOnWrist ? WristOn(c) : GridOn(c)) && game::Latest(&look, &ageMs) && Fresh(ageMs);
    if (!allies) look = Look{};
    look.cols = (uint8_t)(where == kOnWrist ? c.wristCols : c.mapCols);
    if (where == kUnderMap && c.skills) AddSkills(&look);   // [hud] skill_under_map: a row of its own over the allies
    const bool show = viewgate::F4() && (look.n > 0 || look.nSkills > 0);
    if (!show) {
        if (g_wanted[where]) {
            g_wanted[where] = false;
            P(where).Reset();   // gone: its picture is forgotten, the next time paints afresh
        }
        return false;
    }
    const uint32_t rev = P(where).Submit(look);
    if (!g_wanted[where]) {
        g_wanted[where] = true;
        g_needRev[where] = rev;   // it starts being wanted: not shown before a picture of this moment
        static int told = 0;
        if (told++ < 4) {
            char b[160];
            snprintf(b, sizeof b, "vrcam: ally portraits - %s: %d tile%s, %d across, %d skill%s over them", where == kOnWrist ? "on the wrist" : "under the map", (int)look.n,
                     look.n == 1 ? "" : "s", AcrossOf(look), (int)look.nSkills, look.nSkills == 1 ? "" : "s");
            Log(b);
        }
    }
    *needRevision = g_needRev[where];
    return true;
}

bool WristPanel(WristLayer* layer) {
    if (!WristWanted() || !viewgate::F4()) return false;
    Published p;
    {
        std::lock_guard<std::mutex> lock(g_pubMutex);
        p = g_pub;
    }
    if (!p.active || GetTickCount64() - p.tick > 250) return false;   // no frame lately: nothing, rather than a panel frozen in the air
    const float a = std::clamp(p.out.alpha, 0.0f, 1.0f);
    if (!(a > 0.0f) || !p.out.pose.valid) return false;
    const wriststatus::Config ws = wriststatus::Settings();
    layer->pose = p.out.pose;
    layer->panelHM = wriststatus::PanelHeightM(ws);
    layer->panelUp = wriststatus::Enabled() && wriststatus::ContentFor(ws, 1) != 0;   // the wrist status' own panel on the right wrist too: ours over it
    layer->tileWidthM = WristWidthM(ws.sizeCm, Settings().wristCols);
    layer->grow = handvitals::kShrinkWhenFaded + (1.0f - handvitals::kShrinkWhenFaded) * a;
    layer->alpha = a;
    return true;
}

bool GameCornerToHide(float* units) {
    Look look;
    double ageMs = 0.0;
    if (!Enabled() || !viewgate::F4()) return false;
    const bool known = game::Latest(&look, &ageMs) && Fresh(ageMs);   // (none to go by: the whole row is hidden)
    int players = 0, row = 0;
    for (int i = 0; i < look.n; ++i) {
        const Tile& t = look.tile[(size_t)i];
        if (t.kind == Kind::Player) ++players;
        else row += t.kind == Kind::More ? t.count : 1;   // (the last tile stands for those left out: the game draws them all)
    }
    *units = HideUnits(known, players, row);
    return *units > 0.0f;
}

bool Picture(int where, std::vector<uint32_t>* px, uint32_t* revision, uint32_t* width, uint32_t* height) {
    if (!P(where).Fetch(px, revision, width, height)) return false;
    static bool told = false;
    if (!told) {   // (the portraits are read with the first picture, on the painter's thread)
        told = true;
        char b[160];
        snprintf(b, sizeof b, "vrcam: ally portraits - %d of the game's %d portraits found beside the plugin%s", allyportraitspaint::PortraitsFound(), kIconCount - 1,
                 allyportraitspaint::PortraitsFound() ? "" : " (none: initials; D2R VR Settings copies them from your game)");
        Log(b);
    }
    return true;
}

void Teardown() {
    for (int where = 0; where < 2; ++where) {
        g_wanted[where] = false;
        g_needRev[where] = 0;
        P(where).Reset();
    }
    g_glance = Glance{};
    std::lock_guard<std::mutex> lock(g_pubMutex);
    g_pub = Published{};
}

void Shutdown() {
    directinput::Unlisten(&OnFrame, nullptr);
    P(kUnderMap).Stop();
    P(kOnWrist).Stop();
    g_wanted[0] = g_wanted[1] = false;
}

}  // namespace allyportraits
