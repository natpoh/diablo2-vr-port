// gesture_engine_stub.cpp - a stand-in for BodyWalk's d2r_vr_gestures.dll with the same C interface (shared/gesture_engine_abi.h), built only in
// the test tree (never shipped): the mod's loader, wiring and Gestures tab are built and tested against it before the real engine lands. It keeps
// gestures in memory per class (no files), takes a glyph or motion while its pen is held after a FRESH press (an armed take ignores a trigger already
// held), a pose at the pen's press, and in play a stroke over 10 frames casts the first recorded gesture of that hand's class whose type is a glyph.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "gesture_engine_abi.h"

namespace {

struct Row {
    uint32_t id;
    uint8_t type, hand;
    int32_t skill;
    bool recorded = false, review = false;
    uint32_t rgba = 0xFF3C3CE6u;
    float threshold = 80.0f;
    std::string name;
};

struct Engine {
    BWGE_LogFn log = nullptr;
    std::string set, setName;
    std::map<std::string, std::vector<Row>> rows;   // per class
    std::map<int32_t, std::string> skillNames;
    uint32_t nextId = 1000, revision = 1;
    // the take
    bool active = false, armed = false, drawing = false, started = false, madeNew = false;
    int idx = -1, points = 0;
    uint32_t state = BW_LINK_REC_NONE, takeGesture = 0;
    bool prevPen[2] = {false, false};
    bool heldAtArm[2] = {false, false};
    // play
    int stroke[2] = {0, 0};
    std::vector<BWGE_Cast> casts;
    uint32_t castStamp = 0;
    float lastPercent = -1;
    uint32_t lastGesture = 0, lastStamp = 0;

    void Say(const char* s) { if (log) log(s); }
    std::vector<Row>& Rows() { return rows[set]; }
    int Find(uint32_t id) {
        auto& r = Rows();
        for (int i = 0; i < (int)r.size(); ++i)
            if (r[i].id == id) return i;
        return -1;
    }
    void EndTake() {
        auto& r = Rows();
        const bool valid = idx >= 0 && idx < (int)r.size();
        const bool onePoint = valid && r[idx].type == BW_LINK_GT_POSE;
        const bool ok = valid && (onePoint ? points > 0 : points > 10);
        state = ok ? BW_LINK_REC_SAVED : started ? BW_LINK_REC_FAILED : BW_LINK_REC_CANCELLED;
        if (ok) r[idx].review = true;
        else if (valid && madeNew) { r.erase(r.begin() + idx); ++revision; }
        if (!ok && madeNew) takeGesture = 0;
        active = armed = drawing = false;
        ++revision;
    }
};

bool Pressed(const BWGE_Frame* f, int h) { return f->pen[h] && f->trigger[h] > 0.6f; }

}  // namespace

extern "C" {

__declspec(dllexport) uint32_t __cdecl BWGE_Abi(void) { return BWGE_ABI; }

__declspec(dllexport) void* __cdecl BWGE_Open(const BWGE_Init* init) {
    if (!init || init->size < sizeof(BWGE_Init) || init->abi != BWGE_ABI) return nullptr;
    Engine* e = new Engine;
    e->log = init->log;
    e->Say("stub engine open (the test tree's stand-in)");
    return e;
}

__declspec(dllexport) void __cdecl BWGE_Close(void* p) { delete static_cast<Engine*>(p); }

__declspec(dllexport) void __cdecl BWGE_SetSet(void* p, const char* key, const char* name, const BWGE_Skill* skills, uint32_t n) {
    Engine* e = static_cast<Engine*>(p);
    if (!e || !key) return;
    if (e->set != key) { e->active = e->armed = e->drawing = false; e->state = BW_LINK_REC_NONE; e->idx = -1; ++e->revision; }
    e->set = key;
    e->setName = name ? name : "";
    for (uint32_t i = 0; i < n && skills; ++i) e->skillNames[skills[i].id] = skills[i].name;
}

__declspec(dllexport) void __cdecl BWGE_Feed(void* p, const BWGE_Frame* f) {
    Engine* e = static_cast<Engine*>(p);
    if (!e || !f || f->size < sizeof(BWGE_Frame) || e->set.empty()) return;
    bool pen[2] = {Pressed(f, 0), Pressed(f, 1)};
    if (e->active) {
        auto& r = e->Rows();
        const Row& row = r[e->idx];
        const int hs[2] = {row.hand == BW_LINK_GH_RIGHT ? 1 : 0, row.hand == BW_LINK_GH_LEFT ? 0 : 1};
        bool down = false, edge = false;
        for (int h : hs) {
            if (!pen[h]) e->heldAtArm[h] = false;            // let go once: the next press is a fresh one
            const bool fresh = pen[h] && !e->heldAtArm[h];
            down = down || fresh;
            edge = edge || (fresh && !e->prevPen[h]);
        }
        if (row.type == BW_LINK_GT_POSE) {
            if (edge) { e->points = 1; e->started = true; e->EndTake(); }
        } else if (down) {
            e->drawing = true;
            e->started = true;
            e->state = BW_LINK_REC_DRAWING;
            ++e->points;
        } else if (e->drawing) {
            e->EndTake();
        }
    } else if (f->flags & BWGE_FRAME_PLAY) {   // play: a stroke over 10 frames casts the first recorded glyph of that hand
        for (int h = 0; h < 2; ++h) {
            if (pen[h]) { ++e->stroke[h]; continue; }
            if (e->stroke[h] > 10)
                for (const Row& r : e->Rows())
                    if (r.recorded && r.type == BW_LINK_GT_GLYPH && (r.hand == (h ? BW_LINK_GH_RIGHT : BW_LINK_GH_LEFT) || r.hand == BW_LINK_GH_BOTH)) {
                        e->casts.push_back({r.skill, r.id, 91.0f, ++e->castStamp});
                        e->lastGesture = r.id;
                        e->lastPercent = 91.0f;
                        ++e->lastStamp;
                        break;
                    }
            e->stroke[h] = 0;
        }
    }
    e->prevPen[0] = pen[0];
    e->prevPen[1] = pen[1];
}

__declspec(dllexport) int __cdecl BWGE_Record(void* p, uint32_t gesture, uint8_t type, uint8_t hand, int32_t skill) {
    Engine* e = static_cast<Engine*>(p);
    if (!e) return BW_LINK_GR_BAD;
    if (e->set.empty()) return BW_LINK_GR_NO_SET;
    if (e->active) e->EndTake();
    auto& r = e->Rows();
    int i;
    if (gesture) {
        i = e->Find(gesture);
        if (i < 0) return BW_LINK_GR_NO_GESTURE;
        if (r[i].type == BW_LINK_GT_BUTTON) return BW_LINK_GR_BAD;
        e->madeNew = false;
    } else {
        if (type < BW_LINK_GT_GLYPH || type > BW_LINK_GT_MOTION) return BW_LINK_GR_BAD;
        Row n;
        n.id = e->nextId++;
        n.type = type;
        n.hand = hand;
        n.skill = skill;
        auto it = e->skillNames.find(skill);
        n.name = (it != e->skillNames.end() ? it->second : std::string("Gesture")) + " " + std::to_string(n.id);
        r.push_back(n);
        i = (int)r.size() - 1;
        e->madeNew = true;
    }
    e->idx = i;
    e->takeGesture = r[i].id;
    e->active = e->armed = true;
    e->drawing = e->started = false;
    e->points = 0;
    e->state = BW_LINK_REC_READY;
    e->heldAtArm[0] = e->prevPen[0];   // a pen held as the take is armed does not start it
    e->heldAtArm[1] = e->prevPen[1];
    ++e->revision;
    return BW_LINK_GR_OK;
}

__declspec(dllexport) int __cdecl BWGE_Stop(void* p) {
    Engine* e = static_cast<Engine*>(p);
    if (!e) return BW_LINK_GR_BAD;
    if (e->active) e->EndTake();
    return BW_LINK_GR_OK;
}
__declspec(dllexport) int __cdecl BWGE_Keep(void* p) {
    Engine* e = static_cast<Engine*>(p);
    if (!e) return BW_LINK_GR_BAD;
    for (Row& r : e->Rows()) if (r.review) { r.review = false; r.recorded = true; }
    ++e->revision;
    return BW_LINK_GR_OK;
}
__declspec(dllexport) int __cdecl BWGE_Discard(void* p) {
    Engine* e = static_cast<Engine*>(p);
    if (!e) return BW_LINK_GR_BAD;
    auto& r = e->Rows();
    for (int i = 0; i < (int)r.size(); ++i)
        if (r[i].review) {
            if (!r[i].recorded) r.erase(r.begin() + i);
            else r[i].review = false;
            break;
        }
    ++e->revision;
    return BW_LINK_GR_OK;
}
__declspec(dllexport) int __cdecl BWGE_Clear(void* p, uint32_t g) {
    Engine* e = static_cast<Engine*>(p);
    if (!e) return BW_LINK_GR_BAD;
    const int i = e->Find(g);
    if (i < 0) return BW_LINK_GR_NO_GESTURE;
    e->Rows().erase(e->Rows().begin() + i);
    ++e->revision;
    return BW_LINK_GR_OK;
}
__declspec(dllexport) int __cdecl BWGE_Bind(void* p, uint32_t g, int32_t skill) {
    Engine* e = static_cast<Engine*>(p);
    const int i = e ? e->Find(g) : -1;
    if (i < 0) return BW_LINK_GR_NO_GESTURE;
    e->Rows()[i].skill = skill;
    ++e->revision;
    return BW_LINK_GR_OK;
}
__declspec(dllexport) int __cdecl BWGE_SetColor(void* p, uint32_t g, uint32_t rgba) {
    Engine* e = static_cast<Engine*>(p);
    const int i = e ? e->Find(g) : -1;
    if (i < 0) return BW_LINK_GR_NO_GESTURE;
    e->Rows()[i].rgba = rgba;
    ++e->revision;
    return BW_LINK_GR_OK;
}
__declspec(dllexport) int __cdecl BWGE_SetThreshold(void* p, uint32_t g, float pct) {
    Engine* e = static_cast<Engine*>(p);
    const int i = e ? e->Find(g) : -1;
    if (i < 0) return BW_LINK_GR_NO_GESTURE;
    if (!std::isfinite(pct)) return BW_LINK_GR_BAD;
    e->Rows()[i].threshold = pct;
    ++e->revision;
    return BW_LINK_GR_OK;
}

__declspec(dllexport) uint32_t __cdecl BWGE_List(void* p, BW_LinkGestures* out) {
    Engine* e = static_cast<Engine*>(p);
    if (!e || !out) return 0;
    std::memset(out, 0, sizeof *out);
    std::snprintf(out->setKey, sizeof out->setKey, "%s", e->set.c_str());
    const auto& r = e->Rows();
    out->count = (uint32_t)std::min<size_t>(r.size(), BW_LINK_GESTURES_MAX);
    for (uint32_t i = 0; i < out->count; ++i) {
        BW_LinkGesture& g = out->g[i];
        g.id = r[i].id;
        g.type = r[i].type;
        g.hand = r[i].hand;
        g.flags = uint8_t((r[i].recorded ? BW_LINK_GF_RECORDED | BW_LINK_GF_LIVE : 0) | (r[i].review ? BW_LINK_GF_REVIEW : 0) |
                          (e->active && (int)i == e->idx ? BW_LINK_GF_RECORDING : 0));
        g.skill = r[i].skill;
        g.rgba = r[i].rgba;
        g.lastPercent = -1.0f;
        g.threshold = r[i].threshold;
        std::snprintf(g.name, sizeof g.name, "%s", r[i].name.c_str());
        g.pointCount = BW_LINK_GESTURE_POINTS;
        for (uint32_t k = 0; k < BW_LINK_GESTURE_POINTS; ++k) {   // a circle for every picture
            const float a = 6.2831853f * k / (BW_LINK_GESTURE_POINTS - 1);
            g.pts[k][0] = 0.4f * std::cos(a);
            g.pts[k][1] = 0.4f * std::sin(a);
        }
        g.jointValid = 0x07;
        g.joints[0][1] = 0.72f;
        g.joints[1][1] = 0.42f;
        if (r[i].type == BW_LINK_GT_BUTTON) std::snprintf(g.button, sizeof g.button, "%s", "Left Y");
    }
    out->recordState = e->state;
    out->recordGesture = e->takeGesture;
    if (e->idx >= 0 && e->idx < (int)r.size()) {
        out->recordType = r[e->idx].type;
        out->recordHand = r[e->idx].hand;
        out->recordSkill = r[e->idx].skill;
    } else {
        out->recordSkill = -1;
    }
    out->lastStamp = e->lastStamp;
    out->lastGesture = e->lastGesture;
    out->lastPercent = e->lastPercent;
    out->revision = e->revision;
    return e->revision;
}

__declspec(dllexport) int __cdecl BWGE_PollCasts(void* p, BWGE_Cast* out, int max) {
    Engine* e = static_cast<Engine*>(p);
    if (!e || !out || max <= 0) return 0;
    int n = 0;
    while (n < max && !e->casts.empty()) {
        out[n++] = e->casts.front();
        e->casts.erase(e->casts.begin());
    }
    return n;
}

__declspec(dllexport) uint32_t __cdecl BWGE_Trail(void* p, BW_LinkVrRune* out, uint32_t max) {
    (void)p;
    (void)out;
    (void)max;
    return 0;   // (the stub draws no trail)
}

}  // extern "C"
