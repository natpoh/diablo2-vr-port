// wrist_status_paint.cpp - the wrist status panel's picture (vr/wrist_status_paint.h). Our own drawing, with the paint kit; the skill icons are the player's
// own cache (vr/wheel_icons.h), never shipped.

#include "wrist_status_paint.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "paint_kit.h"

namespace wriststatuspaint {
namespace {

using wheelpaint::kit::Canvas;
using wheelpaint::kit::Col;
using wheelpaint::kit::Fonts;
using wheelpaint::kit::IconTone;
using wheel::Rect;
using namespace wriststatus;

constexpr Col kBrass{198.0f, 166.0f, 102.0f}, kGreen{126.0f, 200.0f, 138.0f}, kRed{236.0f, 104.0f, 86.0f}, kGold{224.0f, 200.0f, 146.0f}, kText{244.0f, 236.0f, 218.0f};

Col RimOf(CellKind k) {
    switch (k) {
        case CellKind::Companion: return kBrass;
        case CellKind::Benefit: return kGreen;
        case CellKind::Harm: return kRed;
        default: return kGold;
    }
}

const Companion* CompanionOf(int type) {
    for (const Companion& d : kCompanions)
        if (d.type == type) return &d;
    return nullptr;
}
const Effect* EffectOf(int state, bool harm) {
    for (const Effect& e : kEffects)
        if (e.state == state && (e.kind == Kind::Harm) == harm) return &e;
    return nullptr;
}

}  // namespace

void Initials(const char* name, char out[5]) {
    out[0] = 0;
    if (!name) return;
    int n = 0;
    bool start = true;
    for (const char* p = name; *p && n < 3; ++p) {
        if (std::isalpha((unsigned char)*p)) {
            if (start) out[n++] = (char)std::toupper((unsigned char)*p);
            start = false;
        } else {
            start = true;
        }
    }
    if (n < 2) {   // one word: its first three letters
        n = 0;
        for (const char* p = name; *p && n < 3; ++p)
            if (std::isalpha((unsigned char)*p)) out[n++] = (char)std::toupper((unsigned char)*p);
    }
    out[n] = 0;
}

void Render(const Look& look, std::vector<uint32_t>& px) {
    px.assign((size_t)kPicW * kPicH, 0u);
    Canvas cv{px, kPicW, kPicH};
    Fonts fonts;
    for (int i = 0; i < (int)look.n && i < kMaxCells; ++i) {
        const Cell& c = look.cell[(size_t)i];
        const Slot s = SlotOf(i, look.n);
        const float x = (float)s.x, y = (float)s.y;
        const Col rim = RimOf(c.kind);
        const bool dead = c.kind == CellKind::Companion && c.count == 0;
        wheelpaint::kit::RoundRect(cv, Rect{x + 3.0f, y + 3.0f, x + 61.0f, y + 61.0f}, 12.0f, Col{13.0f, 11.0f, 10.0f}, 0.88f, dead ? Col{150.0f, 80.0f, 70.0f} : rim, 0.95f, 2.5f);
        char tag[8] = {};
        std::shared_ptr<const wheelicons::Image> icon;
        if (c.kind == CellKind::More) {
            snprintf(tag, sizeof tag, "+%d", (int)c.id);
        } else {
            if (c.skill >= 0) icon = wheelicons::Get(c.skill);
            if (c.kind == CellKind::Companion) {
                const Companion* d = CompanionOf(c.id);
                snprintf(tag, sizeof tag, "%s", d ? d->tag : "?");
            } else {
                const Effect* e = EffectOf(c.id, c.kind == CellKind::Harm);
                char ini[5];
                Initials(e ? e->name : "?", ini);
                snprintf(tag, sizeof tag, "%s", ini);
            }
        }
        if (icon && !icon->rgba.empty()) {
            wheelpaint::kit::Icon(cv, icon, Rect{x + 9.0f, y + 8.0f, x + 55.0f, y + 54.0f}, dead ? IconTone::Dim : IconTone::Normal);
        } else {
            const int size = c.kind == CellKind::More ? (std::strlen(tag) >= 4 ? 17 : 24) : std::strlen(tag) >= 3 ? 17 : 22;   // three letters need a smaller font to fit the chip
            wheelpaint::kit::Text(cv, fonts, tag, Rect{x + 3.0f, y + 12.0f, x + 61.0f, y + 44.0f}, size, true, dead ? Col{196.0f, 140.0f, 128.0f} : rim);
        }
        if (c.kind == CellKind::Companion) {   // the count in the lower right corner
            wheelpaint::kit::RoundRect(cv, Rect{x + 34.0f, y + 36.0f, x + 62.0f, y + 62.0f}, 9.0f, Col{10.0f, 8.0f, 8.0f}, 0.92f, dead ? Col{150.0f, 80.0f, 70.0f} : kBrass, 0.9f, 1.5f);
            char num[8];
            snprintf(num, sizeof num, "%u", (unsigned)std::min<uint16_t>(c.count, 999));
            wheelpaint::kit::Text(cv, fonts, num, Rect{x + 34.0f, y + 36.0f, x + 62.0f, y + 62.0f}, c.count > 99 ? 14 : 20, true, dead ? Col{255.0f, 132.0f, 112.0f} : kText);
        } else if (c.kind != CellKind::More) {   // a bar along the bottom in the rim's colour: helpful / harmful at a glance, whatever the icon is
            wheelpaint::kit::RoundRect(cv, Rect{x + 14.0f, y + 55.0f, x + 50.0f, y + 58.0f}, 1.5f, rim, 0.9f, rim, 0.0f, 0.0f);
        }
    }
    const float o = std::clamp((float)look.opacity / 20.0f, 0.0f, 1.0f);   // the whole picture, premultiplied: every channel
    if (o < 0.999f)
        for (uint32_t& p : px) {
            const uint32_t r = (uint32_t)((p & 255u) * o + 0.5f), g = (uint32_t)(((p >> 8) & 255u) * o + 0.5f), b = (uint32_t)(((p >> 16) & 255u) * o + 0.5f),
                           a = (uint32_t)((p >> 24) * o + 0.5f);
            p = r | g << 8 | b << 16 | a << 24;
        }
}

}  // namespace wriststatuspaint
