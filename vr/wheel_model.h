#pragma once
// The skill wheel's brain: which page is up, what the pointer is on, what a click does, the slot the trigger casts, the
// editor's two steps and the one request in flight. A frame in, a frame out; nothing of the game, the headset or the
// clock beyond the numbers it is given - so a test drives it with made-up frames (cleanroom/tests/skill_wheel_test.cpp).
//
// The flow is bearstonem/d2r-pcvr's wheel (MIT, steamvr/interactions.h Engine): the wheel picks the slot the trigger
// casts; EDIT SKILLS -> a slot -> a skill (or CLEAR SLOT); the centre is the basic attack. Written again for our types.
//
// The panel has two looks (Style::Wheel, Style::Tiles - [openxr] skill_panel_style). Both give the same cells (wheel::Cell) and
// the same pages, so this model is one; only where a point lands (wheel_geom.h / tiles_geom.h) and how many skills the editor
// shows on a page (8 in the wheel's list, 20 in the tiles' grid) depend on the look.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "hset_model.h"
#include "tiles_geom.h"
#include "wheel_flip.h"
#include "wheel_geom.h"
#include "wheel_types.h"

namespace wheel {

static_assert(kCentre > kOptionsMax && tiles::kGridCount <= kOptionsMax, "an entry of the editor's list must never be taken for a button");

enum class Feedback : uint8_t { None, Hover, Selected, Refused, Applied, Opened, Closed };

// What the painter needs of the model, as plain values; `revision` goes up whenever anything of it changed.
struct View {
    bool open = false;
    Page page = Page::Wheel;
    int hover = kNone;
    int selected = -1;        // the slot the trigger casts, -1 = the layout's own trigger (the basic attack: the game's A)
    int editSlot = -1;
    int editPage = 0, editPages = 1, options = 0;
    Skill option[kOptionsMax];
    Skill slot[kSlotCount];
    Style style = Style::Wheel;
    Hints hints;
    bool ready = false, editable = false;
    int bank = -1;
    Result result = Result::None;
    char status[80] = {};
    uint32_t revision = 0;
    // The tab strip (tiles, [openxr] headset_settings): `tabs` the strip is there, `tab` the one shown. The Settings tab's pages are the
    // settings model (vr/hset_model.h) as the controller last handed it over, with the button under the beam on them.
    bool tabs = false;
    Tab tab = Tab::Skills;
    int flipPlace = 0;   // where the window stands along its pages (vr/wheel_flip.h: 0 Skills, 1 + the Settings page): the dots, the arrows
    hset::Model settings;
    hset::Hit settingsHover;
};

struct In {
    double time = 0;              // seconds, monotonic
    bool allowed = false;         // switched on, focused, in a game with no menu open
    uint64_t session = 0;         // the plugin's session id; a new one forgets everything
    uint32_t epoch = 0;           // the input context; a change closes the wheel
    bool toggle = false;          // the chord's edge this frame
    bool back = false;            // a close key's edge (B / Y / menu, either hand)
    bool pointer = false;         // the pointing hand's ray is on the panel
    float u = 0, v = 0;
    bool click = false;           // its trigger's edge
    Style style = Style::Wheel;   // the look now; a change while the panel is up goes back to its first page
    bool tabs = false;            // tiles: the Settings tab exists ([openxr] headset_settings), so the strip is there
    int openTab = -1;             // the tab a toggle opens it on (a Tab's value); -1 the one it was last on (the tab is kept while the session runs)
    int setTab = -1;              // the tab to show now while it is up (a Tab's value; the top level only); -1 none
    bool close = false;           // the Settings tab asks for the window to close (its Close button, Calibrate body, a view change)
    const Hints* hints = nullptr; // the captions under the slots (tiles); null = none
    uint64_t nowMs = 0;           // GetTickCount64, the snapshot's clock
    const Snapshot* snap = nullptr;
    Reply reply;                  // the newest answer to a request
};

struct Out {
    Feedback feedback = Feedback::None;
    bool request = false;         // `req` is to be carried out on the game's UI thread
    Request req;
    int flip = 0;                 // an arrow at the window's edge was clicked: -1 one place back, +1 on (the controller moves it: it knows the Settings page)
};

class Model {
    View v_;
    Snapshot empty_;
    uint64_t session_ = 0;
    uint32_t epoch_ = 0;
    uint64_t sequence_ = 0;
    Request pending_;
    double pendingAt_ = 0;
    Skill previous_;                   // what the edited slot held when it was chosen
    Skill choices_[kCatalogMax];
    int choiceCount_ = 0;
    int editBank_ = -1;
    double statusUntil_ = 0;
    uint64_t settingsSeen_ = ~0ull;   // the revision of the settings model the view holds a copy of

    void Status(double now, const char* text, double seconds) {
        std::snprintf(v_.status, sizeof v_.status, "%s", text);
        statusUntil_ = now + seconds;
    }
    // (a request already with the game stays pending: its answer is still taken and told by a buzz - it is not left unsaid)
    void CloseNow() {
        v_.open = false;
        v_.page = Page::Wheel;
        v_.hover = kNone;
        v_.editSlot = -1;
        v_.result = Result::None;
        v_.status[0] = 0;
    }
    int PerPage() const { return v_.style == Style::Tiles ? tiles::kGridCount : kOptionsPerPage; }
    void Options() {
        const int per = PerPage();
        v_.editPages = std::max(1, (choiceCount_ + per - 1) / per);
        v_.editPage = std::min(v_.editPage, v_.editPages - 1);
        v_.options = std::max(0, std::min(per, choiceCount_ - v_.editPage * per));
        for (int i = 0; i < kOptionsMax; ++i) v_.option[i] = i < v_.options ? choices_[v_.editPage * per + i] : Skill{};
    }

public:
    const View& Current() const { return v_; }
    int Selected() const { return v_.selected; }
    bool Open() const { return v_.open; }

    void Reset() {
        const uint32_t rev = v_.revision;
        const Style style = v_.style;
        const Hints hints = v_.hints;
        v_ = View{};
        v_.style = style;
        v_.hints = hints;
        v_.revision = rev + 1;
        pending_ = Request{};
        choiceCount_ = 0;
        editBank_ = -1;
        settingsSeen_ = ~0ull;
    }
    // Where the window stands along its pages, for the dots and the arrows (the controller says it every frame).
    void SetPlace(int place) {
        place = std::clamp(place, 0, kFlipPlaces - 1);
        if (place == v_.flipPlace) return;
        v_.flipPlace = place;
        ++v_.revision;
    }
    // The tab shown (the controller's flips and the chord's switch).
    void SetTab(Tab t) {
        if (!v_.tabs || t == v_.tab) return;
        v_.tab = t;
        v_.hover = kNone;
        ++v_.revision;
    }
    // The Settings tab's pages, for the painter (the controller hands them over every frame; the copy is made when they changed).
    void SetSettings(const hset::Model& m, const hset::Hit& hover) {
        if (settingsSeen_ == m.revision && hover == v_.settingsHover) return;
        settingsSeen_ = m.revision;
        v_.settings = m;
        v_.settingsHover = hover;
        ++v_.revision;
    }
    void ClearSelection() {
        if (v_.selected != -1) { v_.selected = -1; ++v_.revision; }
    }

    Out Step(const In& in) {
        Out out;
        const View before = v_;
        const Snapshot& snap = in.snap ? *in.snap : empty_;
        const bool fresh = Fresh(snap, in.nowMs, in.session);
        if (in.session != session_) {
            session_ = in.session;
            epoch_ = in.epoch;
            Reset();
        }
        // The wheel closes when the context it opened in is gone (a menu, the focus, the session), not when a stray frame is late.
        if (v_.open && (!in.allowed || in.epoch != epoch_)) {
            CloseNow();
            out.feedback = Feedback::Closed;
        }
        epoch_ = in.epoch;
        if (in.hints) v_.hints = *in.hints;
        v_.tabs = in.tabs && in.style == Style::Tiles;   // (the wheel's look has no strip)
        if (!v_.tabs) v_.tab = Tab::Skills;
        bool restyled = false;
        if (in.style != v_.style) {   // the other look: the same state, back on the first page (the cells are in other places)
            restyled = true;
            v_.style = in.style;
            if (v_.open) {
                v_.page = Page::Wheel;
                v_.editSlot = -1;
                v_.editPage = 0;
                v_.hover = kNone;
            }
        }

        // the answer to the request in flight
        if (pending_.op != Op::None) {
            if (in.reply.sequence == pending_.sequence && in.reply.result != Result::None && in.reply.result != Result::Pending) {
                v_.result = in.reply.result;
                const bool applied = in.reply.result == Result::Applied;
                out.feedback = applied ? Feedback::Applied : Feedback::Refused;
                Status(in.time, applied ? (pending_.op == Op::Clear ? "Slot cleared" : "Assigned")
                                        : "The game did not take it - choose the slot again", 3.0);
                if (applied && pending_.slot == v_.selected && pending_.op == Op::Clear) v_.selected = -1;
                pending_ = Request{};
                if (v_.open) {
                    v_.page = Page::PickSlot;
                    v_.editSlot = -1;
                }
            } else if (in.time - pendingAt_ > 1.5) {
                v_.result = Result::Refused;
                out.feedback = Feedback::Refused;
                Status(in.time, "No answer from the game", 3.0);
                pending_ = Request{};
                if (v_.open) {
                    v_.page = Page::PickSlot;
                    v_.editSlot = -1;
                }
            }
        }

        if (!v_.open) {
            if (in.toggle && in.allowed) {
                const Tab want = v_.tabs && in.openTab == int(Tab::Settings) ? Tab::Settings : v_.tabs && in.openTab == int(Tab::Skills) ? Tab::Skills : v_.tab;
                if (fresh || want == Tab::Settings) {   // (the settings need no game state: they open without)
                    v_.tab = want;
                    v_.open = true;
                    v_.page = Page::Wheel;
                    v_.hover = kNone;
                    v_.result = Result::None;
                    v_.status[0] = 0;
                    out.feedback = Feedback::Opened;
                } else {
                    out.feedback = Feedback::Refused;   // no game state to show: the wheel does not open
                }
            }
        } else {
            if (in.toggle || in.back || in.close) {
                CloseNow();
                out.feedback = Feedback::Closed;
            } else if (v_.tabs && in.setTab >= 0 && v_.page == Page::Wheel && Tab(in.setTab) != v_.tab) {
                v_.tab = Tab(in.setTab);
                v_.hover = kNone;
            }
        }

        if (v_.open) {
            // the editor stands on one bank: the weapon set changing under it ends it
            if (v_.page != Page::Wheel && fresh && snap.bank != editBank_) {
                v_.page = Page::Wheel;
                v_.editSlot = -1;
                pending_ = Request{};
            }
            const int hover = !in.pointer ? int(kNone)
                              : v_.style == Style::Tiles ? tiles::HitTest(v_.page, in.u, in.v, v_.hover, v_.options, v_.tabs, v_.tab == Tab::Settings)
                                                         : HitTest(v_.page, in.u, in.v, v_.hover, v_.options);
            int shown = hover;
            if ((shown == kFlipPrev && v_.flipPlace <= 0) || (shown == kFlipNext && v_.flipPlace >= kFlipPlaces - 1)) shown = kNone;   // (no arrow there)
            if (shown != v_.hover && shown != kNone && out.feedback == Feedback::None) out.feedback = Feedback::Hover;
            v_.hover = shown;
            // (a click in the frame the look changed is not acted on: the player still sees the old picture)
            if (in.click && !restyled && out.feedback != Feedback::Closed && pending_.op == Op::None) Click(in, snap, fresh, &out);
        } else {
            v_.hover = kNone;
        }

        // the slot the trigger casts is gone from the game (cleared in its own menus): the basic attack again
        if (v_.selected >= 0 && fresh && snap.ready && snap.slot[v_.selected].known && snap.slot[v_.selected].id < 0) {
            v_.selected = -1;
            if (out.feedback == Feedback::None) out.feedback = Feedback::Refused;
        }

        if (v_.status[0] && in.time > statusUntil_) v_.status[0] = 0;
        // the painted copy of the game's side
        v_.ready = fresh;
        v_.editable = fresh && snap.editable;
        v_.bank = snap.bank;
        v_.result = pending_.op != Op::None ? Result::Pending : v_.result;
        if (fresh) for (int i = 0; i < kSlotCount; ++i) v_.slot[i] = snap.slot[i];
        else for (int i = 0; i < kSlotCount; ++i) v_.slot[i] = Skill{};
        Options();
        v_.revision = before.revision;   // (compared field by field below)
        if (Differs(before, v_)) ++v_.revision;
        return out;
    }

private:
    static bool Differs(const View& a, const View& b) {
        if (a.open != b.open || a.page != b.page || a.hover != b.hover || a.selected != b.selected || a.editSlot != b.editSlot ||
            a.editPage != b.editPage || a.editPages != b.editPages || a.options != b.options || a.ready != b.ready ||
            a.editable != b.editable || a.bank != b.bank || a.result != b.result || a.style != b.style || !(a.hints == b.hints) ||
            a.tabs != b.tabs || a.tab != b.tab || std::strncmp(a.status, b.status, sizeof a.status) != 0)
            return true;
        for (int i = 0; i < kSlotCount; ++i) if (!(a.slot[i] == b.slot[i])) return true;
        for (int i = 0; i < kOptionsMax; ++i) if (!(a.option[i] == b.option[i])) return true;
        return false;
    }

    void Click(const In& in, const Snapshot& snap, bool fresh, Out* out) {
        const int h = v_.hover;
        if (h == kNone) return;
        switch (v_.page) {
        case Page::Wheel:
            if (h == kFlipPrev || h == kFlipNext) {
                out->flip = h == kFlipPrev ? -1 : 1;
            } else if (h == kTabSkills || h == kTabSettings) {
                const Tab t = h == kTabSettings ? Tab::Settings : Tab::Skills;
                if (v_.tabs && t != v_.tab) {   // (the strip is where it was: the beam stays on its tab)
                    v_.tab = t;
                    out->feedback = Feedback::Hover;
                }
            } else if (h == kEdit) {
                if (fresh && snap.editable) {
                    v_.page = Page::PickSlot;
                    editBank_ = snap.bank;
                    out->feedback = Feedback::Hover;
                } else {
                    Status(in.time, fresh ? "Skills cannot be changed in this layout" : "Skills unavailable", 3.0);
                    out->feedback = Feedback::Refused;
                }
            } else if (h == kClose) {
                CloseNow();
                out->feedback = Feedback::Closed;
            } else if (h == kCentre) {
                v_.selected = -1;
                CloseNow();
                out->feedback = Feedback::Selected;
            } else if (h >= 0 && h < kSlotCount) {
                const Skill& s = snap.slot[h];
                const Cast how = kSlotDefs[h].cast;
                if (how == Cast::Layout) {   // A: the trigger stays the layout's attack, which is the game's A button
                    v_.selected = -1;
                    CloseNow();
                    out->feedback = Feedback::Selected;
                } else if (how == Cast::None) {
                    Status(in.time, "That slot cannot be cast from here", 3.0);
                    out->feedback = Feedback::Refused;
                } else if (fresh && s.known && s.id >= 0 && s.available) {
                    v_.selected = h;
                    CloseNow();
                    out->feedback = Feedback::Selected;
                } else {
                    out->feedback = Feedback::Refused;
                }
            }
            break;
        case Page::PickSlot:
            if (h == kBack) {
                v_.page = Page::Wheel;
            } else if (h >= 0 && h < kSlotCount && fresh && snap.editable && snap.slot[h].known) {
                v_.editSlot = h;
                v_.editPage = 0;
                previous_ = snap.slot[h];
                choiceCount_ = (int)std::min<uint32_t>(snap.count, (uint32_t)kCatalogMax);
                for (int i = 0; i < choiceCount_; ++i) choices_[i] = snap.catalog[i];
                v_.page = Page::PickSkill;
                out->feedback = Feedback::Hover;
            } else {
                out->feedback = Feedback::Refused;
            }
            break;
        case Page::PickSkill:
            if (h == kBack) {
                v_.page = Page::PickSlot;
                v_.editSlot = -1;
            } else if (h == kPrev) {
                v_.editPage = std::max(0, v_.editPage - 1);
            } else if (h == kNext) {
                v_.editPage = std::min(v_.editPages - 1, v_.editPage + 1);
            } else if ((h == kCentre || (h >= 0 && h < v_.options)) && fresh && snap.editable && v_.editSlot >= 0) {
                Request r;
                r.sequence = ++sequence_;
                r.session = session_;
                r.slot = v_.editSlot;
                r.bank = editBank_;
                r.prevSkill = previous_.id;
                r.prevOwner = previous_.owner;
                if (h == kCentre) {
                    r.op = Op::Clear;
                } else {
                    r.op = Op::Assign;
                    r.skill = v_.option[h].id;
                    r.owner = v_.option[h].owner;
                }
                pending_ = r;
                pendingAt_ = in.time;
                out->request = true;
                out->req = r;
                Status(in.time, "Saving...", 3.0);
            } else {
                out->feedback = Feedback::Refused;
            }
            break;
        }
        Options();
    }
};

}  // namespace wheel
