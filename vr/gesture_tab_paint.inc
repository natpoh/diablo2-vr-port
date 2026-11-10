// gesture_tab_paint.inc - the skill panel's GESTURES tab ([openxr] gesture_overlay) painted on the tiles window: the screens of
// vr/gesture_overlay.h at the places it gives (its hit test reads the same rectangles), in the window's look - the game's own art and fonts
// with the game look on ([openxr] skill_panel_game_look: the front end's buttons, the lobby's inset fields, the Options arrows, Exocet), ours
// without. The gestures' pictures are vr/gesture_picture.h's geometry stroked here.
//
// Included by vr/tiles_paint.cpp inside its painter's namespace (it uses that file's drawing helpers): not a header of its own.

// -- drawing helpers -------------------------------------------------------------------------------------------------------------------------------

inline Rect GR(gov::R r) { return Rect{r.l, r.t, r.r, r.b}; }
inline Col FromRgba(uint32_t rgba) {
    if ((rgba & 0x00FFFFFFu) == 0u) return Col{220, 196, 120};   // (no colour given: the window's gold)
    return Col{float(rgba & 0xFF), float((rgba >> 8) & 0xFF), float((rgba >> 16) & 0xFF)};
}

// A filled disc and a ring, anti-aliased.
void Disc(Canvas& cv, float cx, float cy, float r, Col c, float alpha = 1.0f) {
    for (int y = std::max(0, int(cy - r - 2)); y < std::min(cv.h, int(cy + r + 2)); ++y)
        for (int x = std::max(0, int(cx - r - 2)); x < std::min(cv.w, int(cx + r + 2)); ++x) {
            const float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
            cv.Over(x, y, c, alpha * Clamp01(r + 0.5f - d));
        }
}
void Ring(Canvas& cv, float cx, float cy, float r, float width, Col c, float alpha = 1.0f) {
    for (int y = std::max(0, int(cy - r - width - 2)); y < std::min(cv.h, int(cy + r + width + 2)); ++y)
        for (int x = std::max(0, int(cx - r - width - 2)); x < std::min(cv.w, int(cx + r + width + 2)); ++x) {
            const float d = std::fabs(std::hypot(x + 0.5f - cx, y + 0.5f - cy) - r);
            cv.Over(x, y, c, alpha * Clamp01(0.5f * width + 0.5f - d));
        }
}
// A polyline with a dark edge under it (so it reads on any ground).
void Stroke(Canvas& cv, const gpic::P2* p, int n, float width, Col c, float alpha = 1.0f) {
    for (int i = 1; i < n; ++i) Seg(cv, p[i - 1].x, p[i - 1].y, p[i].x, p[i].y, width + 3.0f, Col{0, 0, 0}, 0.55f * alpha);
    for (int i = 1; i < n; ++i) Seg(cv, p[i - 1].x, p[i - 1].y, p[i].x, p[i].y, width, c, alpha);
    if (n == 1) Disc(cv, p[0].x, p[0].y, 0.5f * width, c, alpha);
}

// A button: the game's (the front end's with the blue gems) with the look on, else ours.
void GButton(Canvas& cv, Fonts& fonts, Rect r, const char* label, bool hot, bool enabled, int size, Col ink = kGameText) {
    if (g_game) {
        GameButton(cv, fonts, r, label, hot && enabled, enabled, false, ink, size);
        return;
    }
    if (hot && enabled) RoundRect(cv, Rect{r.l - 5.0f, r.t - 5.0f, r.r + 5.0f, r.b + 5.0f}, 11.0f, kGoldBright, 0.20f, Col{0, 0, 0}, 0.0f, 0.0f);
    RoundRect(cv, r, 8.0f, hot && enabled ? Col{58, 46, 28} : Col{26, 26, 30}, 0.99f, hot && enabled ? kGoldBright : (enabled ? Col{104, 106, 116} : Col{50, 50, 56}), 1.0f,
              hot && enabled ? 3.5f : 2.5f);
    Bar(cv, r.l + 4.0f, r.t + 4.0f, r.r - 4.0f, r.t + 9.0f, Col{255, 255, 255}, enabled ? 0.06f : 0.02f);
    CapsCentred(cv, fonts, label, 0.5f * (r.l + r.r), r, size, 2.0f, enabled ? (hot ? kGoldBright : (ink.r == kGameText.r ? kSilver : ink)) : Col{92, 92, 100});
}
// A recessed field (a row's ground, a choice's plate): the lobby's inset box with the look on, else a dark rounded plate.
void GField(Canvas& cv, Rect r, bool hot) {
    if (g_game) {
        Nine(cv, Art(gameui::Sprite::Field), 0, r, 18.0f, 18.0f, 18.0f, 18.0f, std::min(1.0f, (r.b - r.t) / 85.0f), hot ? 1.35f : 1.0f);
        if (hot) RoundRect(cv, r, 8.0f, kGameGold, 0.0f, kGameGoldBright, 0.55f, 2.0f);
        return;
    }
    RoundRect(cv, r, 9.0f, hot ? Col{40, 34, 24} : Col{16, 16, 19}, 0.96f, hot ? kGold : Col{70, 72, 82}, 1.0f, hot ? 2.5f : 1.5f);
}
// One line of words: the game's text font with the look on (its title font when `title`), else ours; fitted to the box, left / centre / right.
void GLine(Canvas& cv, Fonts& fonts, const std::string& text, Rect r, int size, bool title, Col color, hset::Align align = hset::Align::Left) {
    UseFace(fonts, title, text.c_str());
    FitLine(cv, fonts, text, r, size, title && !g_game, color, align);
    fonts.use = nullptr;
}
Col Ink() { return g_game ? kGameText : kSilver; }
Col InkDim() { return g_game ? kGameQuiet : kSilverDim; }
Col InkGold() { return g_game ? kGameGold : kGold; }
Col InkHot() { return g_game ? kGameGoldBright : kGoldBright; }

// -- the pictures -------------------------------------------------------------------------------------------------------------------------------------

const char* TypeWord(uint8_t type) {
    switch (type) {
    case BW_LINK_GT_GLYPH: return uitext::Tr("vr.gestures.type_glyph", "Glyph");
    case BW_LINK_GT_POSE: return uitext::Tr("vr.gestures.type_pose", "Hand pose");
    case BW_LINK_GT_MOTION: return uitext::Tr("vr.gestures.type_motion", "Body motion");
    case BW_LINK_GT_BUTTON: return uitext::Tr("vr.gestures.type_button", "Button");
    default: return "?";
    }
}
const char* HandWord(uint8_t hand, uint8_t type = BW_LINK_GT_POSE) {
    switch (hand) {
    case BW_LINK_GH_LEFT: return uitext::Tr("vr.gestures.hand_left", "Left hand");
    case BW_LINK_GH_RIGHT: return uitext::Tr("vr.gestures.hand_right", "Right hand");
    default: return type == BW_LINK_GT_GLYPH ? uitext::Tr("vr.gestures.hand_either", "Either hand") : uitext::Tr("vr.gestures.hand_both", "Both hands");
    }
}

// The stick figure: its bones in `ink`, its head a ring, a closed hand a fist (a filled disc), an open one a small ring; a hand BodyWalk did not
// have hangs at the side, dim.
void DrawFigure(Canvas& cv, const gpic::Figure& f, Col ink, float w, float alpha = 1.0f) {
    const float neckEnd = f.headR > 1.0f ? 1.0f : 0.0f;
    // the neck stops at the head's ring
    const float dx = f.head.x - f.neck.x, dy = f.head.y - f.neck.y, d = std::max(1e-3f, std::hypot(dx, dy));
    const float nx = f.head.x - dx / d * f.headR * neckEnd, ny = f.head.y - dy / d * f.headR * neckEnd;
    Seg(cv, f.neck.x, f.neck.y, nx, ny, w, ink, alpha);
    Seg(cv, f.neck.x, f.neck.y, f.pelvis.x, f.pelvis.y, w, ink, alpha);
    Seg(cv, f.shoulder[0].x, f.shoulder[0].y, f.shoulder[1].x, f.shoulder[1].y, w, ink, alpha);
    Seg(cv, f.hip[0].x, f.hip[0].y, f.hip[1].x, f.hip[1].y, w, ink, alpha);
    for (int k = 0; k < 2; ++k) {
        Seg(cv, f.hip[k].x, f.hip[k].y, f.foot[k].x, f.foot[k].y, w, ink, alpha);
        const float a = f.known[k] ? alpha : 0.45f * alpha;
        Seg(cv, f.shoulder[k].x, f.shoulder[k].y, f.elbow[k].x, f.elbow[k].y, w, ink, a);
        Seg(cv, f.elbow[k].x, f.elbow[k].y, f.hand[k].x, f.hand[k].y, w, ink, a);
        if (f.fist[k]) Disc(cv, f.hand[k].x, f.hand[k].y, 1.25f * w + 1.5f, ink, a);
        else Ring(cv, f.hand[k].x, f.hand[k].y, 0.9f * w + 1.0f, 0.55f * w, ink, a);
    }
    Ring(cv, f.head.x, f.head.y, f.headR, w, ink, alpha);
}

// A thought cloud: a soft pale cloud with a dark rim, and the bubbles to it.
void DrawCloud(Canvas& cv, const gpic::Cloud& c, float rim) {
    const Col edge{30, 26, 22}, fill{232, 228, 216};
    auto Ellipse = [&](float grow, Col col) {
        const float ax = std::max(1.0f, c.coreRx + grow), ay = std::max(1.0f, c.coreRy + grow);
        for (int y = std::max(0, int(c.body.Cy() - ay - 2)); y < std::min(cv.h, int(c.body.Cy() + ay + 2)); ++y)
            for (int x = std::max(0, int(c.body.Cx() - ax - 2)); x < std::min(cv.w, int(c.body.Cx() + ax + 2)); ++x) {
                const float ex = (x + 0.5f - c.body.Cx()) / ax, ey = (y + 0.5f - c.body.Cy()) / ay;
                cv.Over(x, y, col, Clamp01((1.0f - std::sqrt(ex * ex + ey * ey)) * std::min(ax, ay) + 0.5f));
            }
    };
    // the rims first, all of them, then the fills over them: the seams inside are covered, one outline is left round the whole
    for (int i = 0; i < gpic::kBubbles; ++i) Disc(cv, c.bubble[i].x, c.bubble[i].y, c.bubbleR[i] + rim, edge);
    for (int i = 0; i < gpic::kLobes; ++i) Disc(cv, c.lobe[i].x, c.lobe[i].y, c.lobeR[i] + rim, edge);
    Ellipse(rim, edge);
    for (int i = 0; i < gpic::kBubbles; ++i) Disc(cv, c.bubble[i].x, c.bubble[i].y, c.bubbleR[i], fill);
    for (int i = 0; i < gpic::kLobes; ++i) Disc(cv, c.lobe[i].x, c.lobe[i].y, c.lobeR[i], fill);
    Ellipse(0.0f, fill);
}

// A hand pose's zone round a hand on the picture: a soft see-through sphere as big as the zone at the figure's scale, its rim, a highlight.
void DrawZone(Canvas& cv, float cx, float cy, float r, bool big) {
    if (!(r > 1.0f)) return;
    const Col fill{240, 200, 120}, rim{255, 226, 160};
    Disc(cv, cx, cy, r, fill, 0.22f);
    Ring(cv, cx, cy, r, big ? 3.0f : 1.5f, rim, 0.75f);
    Disc(cv, cx - 0.35f * r, cy - 0.35f * r, 0.22f * r, Col{255, 255, 255}, 0.18f);   // (a sphere's light)
}

// Two figures blended (a pose played: from the arms hanging to the pose).
gpic::Figure Blend(const gpic::Figure& a, const gpic::Figure& b, float t) {
    auto L = [t](gpic::P2 p, gpic::P2 q) { return gpic::P2{p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t}; };
    gpic::Figure f = b;
    f.head = L(a.head, b.head);
    f.headR = a.headR + (b.headR - a.headR) * t;
    f.neck = L(a.neck, b.neck);
    f.pelvis = L(a.pelvis, b.pelvis);
    for (int k = 0; k < 2; ++k) {
        f.shoulder[k] = L(a.shoulder[k], b.shoulder[k]);
        f.elbow[k] = L(a.elbow[k], b.elbow[k]);
        f.hand[k] = L(a.hand[k], b.hand[k]);
        f.hip[k] = L(a.hip[k], b.hip[k]);
        f.foot[k] = L(a.foot[k], b.foot[k]);
        f.fist[k] = t > 0.6f ? b.fist[k] : a.fist[k];
    }
    return f;
}
// The pose's arms hanging (where a played pose starts): the same body, the hands at the hips, open.
BW_LinkGesture ArmsDown(const BW_LinkGesture& g) {
    BW_LinkGesture r = g;
    const float side[2] = {-0.21f, 0.21f};
    for (int k = 0; k < 2; ++k) {
        float* h = r.joints[k ? BW_LINK_J_RHAND : BW_LINK_J_LHAND];
        h[0] = side[k];
        h[1] = -0.02f;
        h[2] = -0.04f;
        r.grip[k] = 0.0f;
    }
    r.jointValid |= (1u << BW_LINK_J_LHAND) | (1u << BW_LINK_J_RHAND);
    return r;
}
// Play's mark on a picture pointed at: a dark disc with a light triangle.
void PlayMark(Canvas& cv, Rect box, bool big) {
    const float r = big ? 34.0f : 15.0f, cx = box.r - r - (big ? 14.0f : 5.0f), cy = box.b - r - (big ? 14.0f : 5.0f);
    Disc(cv, cx, cy, r, Col{0, 0, 0}, 0.7f);
    Ring(cv, cx, cy, r, big ? 3.0f : 1.5f, kGoldBright, 0.9f);
    const float s = 0.45f * r;
    for (float k = 0.0f; k <= 1.0f; k += 0.08f)   // (a filled triangle: lines from its left edge to its point)
        Seg(cv, cx - 0.6f * s, cy - s + 2.0f * s * k, cx + s, cy, big ? 3.0f : 1.6f, kGoldBright);
}

// A gesture's picture in a box (its own dark tile under it): `big` on the review screen. `zone`: a hand pose's zone radius per hand, metres.
// `playT` 0..1: it is being played (a glyph's / path's stroke drawn so far, a pose's arms moving into it); `rgba`: another ray colour than its own.
void DrawGesturePicture(Canvas& cv, Fonts& fonts, const BW_LinkGesture& g, Rect box, bool big, bool hot, const float zone[2] = nullptr, float playT = -1.0f,
                        uint32_t rgba = 0) {
    RoundRect(cv, box, big ? 14.0f : 8.0f, Col{8, 8, 10}, 0.92f, hot ? InkHot() : InkGold(), hot ? 1.0f : 0.65f, big ? 3.0f : 2.0f);
    const gpic::Box b{box.l, box.t, box.r, box.b};
    const float margin = big ? 34.0f : 12.0f, width = big ? 9.0f : 4.0f;
    const bool none = g.type != BW_LINK_GT_BUTTON && !(g.flags & BW_LINK_GF_RECORDED) && !(g.flags & BW_LINK_GF_REVIEW);
    if (none) {   // nothing taken yet
        GLine(cv, fonts, uitext::Tr("vr.gestures.no_take", "not recorded"), Rect{box.l + 4, box.t, box.r - 4, box.b}, big ? 40 : 18, false, InkDim(), hset::Align::Center);
        return;
    }
    switch (g.type) {
    case BW_LINK_GT_GLYPH:
    case BW_LINK_GT_MOTION: {
        gpic::P2 p[BW_LINK_GESTURE_POINTS];
        const int n = gpic::Polyline(g.pts, std::min<int>(g.pointCount ? g.pointCount : BW_LINK_GESTURE_POINTS, BW_LINK_GESTURE_POINTS), b, margin, p, BW_LINK_GESTURE_POINTS);
        const Col c = FromRgba(rgba ? rgba : g.rgba);
        const bool playing = playT >= 0.0f && n > 1;
        const int shown = playing ? std::clamp(1 + (int)std::lround(std::clamp(playT, 0.0f, 1.0f) * (n - 1)), 2, n) : n;   // (played: drawn so far)
        if (playing) Stroke(cv, p, n, width * 0.5f, c, 0.18f);   // (the whole path faint under it)
        if (g.type == BW_LINK_GT_GLYPH) {
            Stroke(cv, p, shown, width, c);
        } else {
            Stroke(cv, p, shown, width * 0.8f, c);
            gpic::P2 l, r;
            if (!playing && gpic::ArrowHead(p, n, big ? 46.0f : 16.0f, &l, &r)) {
                const gpic::P2 e = p[n - 1];
                Seg(cv, e.x, e.y, l.x, l.y, width * 0.8f + 3.0f, Col{0, 0, 0}, 0.55f);
                Seg(cv, e.x, e.y, r.x, r.y, width * 0.8f + 3.0f, Col{0, 0, 0}, 0.55f);
                Seg(cv, e.x, e.y, l.x, l.y, width * 0.8f, c);
                Seg(cv, e.x, e.y, r.x, r.y, width * 0.8f, c);
            }
            if (n > 0) {   // the start: a green dot
                Disc(cv, p[0].x, p[0].y, width * 1.6f + 2.0f, Col{0, 0, 0}, 0.6f);
                Disc(cv, p[0].x, p[0].y, width * 1.6f, Col{70, 210, 90});
            }
        }
        if (playing) {   // the pen's tip, bright
            Disc(cv, p[shown - 1].x, p[shown - 1].y, width * 1.5f + 2.0f, Col{0, 0, 0}, 0.5f);
            Disc(cv, p[shown - 1].x, p[shown - 1].y, width * 1.3f, Col{255, 250, 235});
        }
        if (hot && !big && !playing) PlayMark(cv, box, big);
        break;
    }
    case BW_LINK_GT_POSE: {   // the figure in the pose recorded, a zone round each hand of the pose (Serg: "the cloud is the size of the space the hand is in")
        gpic::Figure f = gpic::StickFigure(g.joints, g.jointValid, g.grip, b, margin);
        if (playT >= 0.0f) {   // played: the arms move from hanging into the pose (eased)
            const BW_LinkGesture down = ArmsDown(g);
            const float t = std::clamp(playT * 1.25f, 0.0f, 1.0f), e = t * t * (3.0f - 2.0f * t);
            f = Blend(gpic::StickFigure(down.joints, down.jointValid, down.grip, b, margin), f, e);
        }
        DrawFigure(cv, f, Ink(), big ? 6.0f : 2.0f);
        bool use[2];
        posezones::HandsOf(g.hand, use);
        for (int h = 0; h < 2; ++h)
            if (use[h] && f.known[h]) {
                const float r = zone && zone[h] > 0.0f ? posezones::Clamp(zone[h]) : posezones::kDefaultM;
                DrawZone(cv, f.hand[h].x, f.hand[h].y, r * f.scale, big);
            }
        if (hot && !big && playT < 0.0f) PlayMark(cv, box, big);
        break;
    }
    case BW_LINK_GT_BUTTON: {   // its name in a key frame
        const float w = box.r - box.l, h = box.b - box.t;
        const Rect key{box.l + 0.12f * w, box.t + 0.24f * h, box.r - 0.12f * w, box.b - 0.24f * h};
        RoundRect(cv, Rect{key.l, key.t + 4.0f, key.r, key.b + 4.0f}, 10.0f, Col{0, 0, 0}, 0.7f, Col{0, 0, 0}, 0.0f, 0.0f);   // its shadow
        RoundRect(cv, key, 10.0f, Col{58, 52, 44}, 1.0f, InkGold(), 1.0f, big ? 3.0f : 2.0f);
        Bar(cv, key.l + 6.0f, key.t + 5.0f, key.r - 6.0f, key.t + 0.30f * (key.b - key.t), Col{255, 255, 255}, 0.08f);
        GLine(cv, fonts, g.button[0] ? std::string(g.button) : std::string("?"), Rect{key.l + 4, key.t, key.r - 4, key.b}, big ? 54 : 22, true, InkHot(), hset::Align::Center);
        break;
    }
    default: break;
    }
}

// -- the screens ---------------------------------------------------------------------------------------------------------------------------------------

std::string Percent(float p) { return std::to_string((int)std::lround(std::clamp(p, 0.0f, 100.0f))) + " %"; }

// Nothing to manage: which piece is missing, in so many words, and its fix right there (ENABLE / START BODYWALK) where there is one.
void DrawMissing(Canvas& cv, Fonts& fonts, const gov::View& v) {
    const char* title = "";
    const char* text = "";
    switch (v.avail) {
    case gov::Avail::LinkOff:
        title = uitext::Tr("vr.gestures.miss_link", "Skills to BodyWalk is off");
        text = uitext::Tr("vr.gestures.miss_link_text", "Your gestures live in BodyWalk, and the game sends it your skills only with Skills to BodyWalk on. Enable switches it on now.");
        break;
    case gov::Avail::NotRunning:
        title = uitext::Tr("vr.gestures.miss_bodywalk", "BodyWalk is not running");
        text = v.canStart ? uitext::Tr("vr.gestures.miss_bodywalk_text", "Your gestures are kept and recognised by BodyWalk. Start BodyWalk opens the copy D2R VR installed; the tab connects by itself.")
                          : uitext::Tr("vr.gestures.miss_bodywalk_none", "No BodyWalk was found on this PC. Run D2R VR Setup again: it puts BodyWalk in the game's folder.");
        break;
    case gov::Avail::Starting:
        title = uitext::Tr("vr.gestures.miss_starting", "Starting BodyWalk...");
        text = uitext::Tr("vr.gestures.miss_starting_text", "The tab connects as soon as BodyWalk is up (a few seconds).");
        break;
    case gov::Avail::NoGameLink:
        title = uitext::Tr("vr.gestures.miss_gamelink", "BodyWalk runs but does not answer");
        text = uitext::Tr("vr.gestures.miss_gamelink_text", "It may still be starting. If this stays, this BodyWalk has no Game Link: run D2R VR Setup again to get the BodyWalk it brings.");
        break;
    case gov::Avail::NoSkills:
        title = uitext::Tr("vr.gestures.miss_skills", "This BodyWalk does not take the game's skills");
        text = uitext::Tr("vr.gestures.miss_skills_text", "It is older than Game skills. Run D2R VR Setup again (or update BodyWalk) - there is no switch for it in BodyWalk: Skills to BodyWalk here is the one.");
        break;
    case gov::Avail::NoGestures:
        title = uitext::Tr("vr.gestures.miss_gestures", "This BodyWalk has no in-game gestures yet");
        text = uitext::Tr("vr.gestures.miss_gestures_text", "It takes your skills but cannot show or record gestures for the game. Update BodyWalk to the version with in-game gestures.");
        break;
    case gov::Avail::NoEngine:
        title = uitext::Tr("vr.gestures.miss_engine", "The gesture engine is not installed");
        text = uitext::Tr("vr.gestures.miss_engine_text", "d2r_vr_gestures.dll was not found in the mod's d2r_vr_libs folder (or it is from another version). Run D2R VR Setup again, or untick In-game gesture engine to use BodyWalk.");
        break;
    case gov::Avail::NoHero:
        title = uitext::Tr("vr.gestures.miss_hero", "No hero yet");
        text = uitext::Tr("vr.gestures.miss_hero_text", "Your gestures are kept per class: they show once you play a hero.");
        break;
    default: return;
    }
    GLine(cv, fonts, title, GR(gov::kMissTitle), 44, true, InkGold(), hset::Align::Center);
    UseFace(fonts, false, text);
    Text(cv, fonts, text, GR(gov::kMissText), 30, false, Ink());
    fonts.use = nullptr;
    const gov::Fix fix = gov::FixFor(v.avail, v.canStart);
    if (fix != gov::Fix::None)
        GButton(cv, fonts, GR(gov::kFixRect), fix == gov::Fix::EnableLink ? uitext::Tr("vr.gestures.enable", "Enable") : uitext::Tr("vr.gestures.start_bodywalk", "Start BodyWalk"),
                v.hover == gov::kFix, !v.waiting, 34);
}

void DrawGestureRows(Canvas& cv, Fonts& fonts, const gov::View& v) {
    if (v.avail != gov::Avail::Ok) {
        DrawMissing(cv, fonts, v);
        return;
    }
    if (v.rows == 0) {
        GLine(cv, fonts, uitext::Tr("vr.gestures.empty", "No gestures for this class yet - NEW GESTURE records one"), Rect{160, 400, 1440, 470}, 34, false, InkDim(), hset::Align::Center);
        return;
    }
    for (int i = 0; i < v.rows; ++i) {
        const gov::Row& row = v.row[i];
        const BW_LinkGesture& g = row.g;
        const bool rowHot = gov::IsRowCell(v.hover) && gov::RowOf(v.hover) == i;
        GField(cv, GR(gov::RowRect(i)), rowHot);
        DrawGesturePicture(cv, fonts, g, GR(gov::PicRect(i)), false, v.hover == gov::RowCell(i, gov::kPic), row.zone, v.playId == g.id ? v.playT : -1.0f);
        if (gov::PartUsable(g, gov::kColour)) {   // the ray's colour: a chip by the name (the Colour screen)
            const Rect r = GR(gov::PartRect(i, gov::kColour));
            const bool hot = v.hover == gov::RowCell(i, gov::kColour);
            RoundRect(cv, r, 6.0f, FromRgba(g.rgba), 1.0f, hot ? InkHot() : InkGold(), hot ? 1.0f : 0.7f, hot ? 3.0f : 2.0f);
        }
        GLine(cv, fonts, g.name[0] ? std::string(g.name) : std::string(TypeWord(g.type)), GR(gov::NameRect(i)), 32, true, rowHot ? InkHot() : InkGold());
        std::string casts = row.action[0] ? uitext::TrF("vr.gestures.does", "does {action}", {{"action", gact::NameOf(row.action)}})
                          : row.skill[0] ? (row.mode ? uitext::TrF("vr.gestures.arms", "arms {skill} on the trigger", {{"skill", row.skill}})
                                                     : uitext::TrF("vr.gestures.casts", "casts {skill}", {{"skill", row.skill}}))
                                         : std::string(uitext::Tr("vr.gestures.no_skill", "casts nothing"));
        const bool modeHot = v.hover == gov::RowCell(i, gov::kMode);
        if (modeHot) RoundRect(cv, GR(gov::SkillLineRect(i)), 6.0f, Col{0, 0, 0}, 0.0f, InkHot(), 0.9f, 2.0f);   // (a skill's line steps its mode)
        if (g.lastPercent >= 0.0f) casts += "   " + uitext::TrF("vr.gestures.last_read", "last read {percent}", {{"percent", Percent(g.lastPercent)}});
        if (g.flags & BW_LINK_GF_REVIEW) casts += "   " + std::string(uitext::Tr("vr.gestures.in_review", "(new take waiting)"));
        GLine(cv, fonts, casts, GR(gov::SkillLineRect(i)), 24, false, modeHot ? InkHot() : row.mode && !row.action[0] ? InkGold() : row.skill[0] || row.action[0] ? Ink() : InkDim());
        {   // the type and hand (a glyph's hand: a click steps it)
            const bool handHot = v.hover == gov::RowCell(i, gov::kHand);
            if (handHot) RoundRect(cv, GR(gov::TypeRect(i)), 6.0f, Col{0, 0, 0}, 0.0f, InkHot(), 0.9f, 2.0f);
            GLine(cv, fonts, std::string(TypeWord(g.type)) + " - " + HandWord(g.hand, g.type), GR(gov::TypeRect(i)), 22, false, handHot ? InkHot() : Ink(), hset::Align::Center);
        }
        if (g.type != BW_LINK_GT_BUTTON) {   // the threshold, with its arrows
            if (g.type == BW_LINK_GT_POSE)   // (a hand pose: its zone's size, in cm)
                GLine(cv, fonts, uitext::TrF("vr.gestures.zone_cm", "zone {cm} cm", {{"cm", std::to_string((int)std::lround(100.0f * row.zone[g.hand == BW_LINK_GH_LEFT ? 0 : 1]))}}),
                      GR(gov::ThrTextRect(i)), 24, false, Ink(), hset::Align::Center);
            else
                GLine(cv, fonts, Percent(g.threshold), GR(gov::ThrTextRect(i)), 26, false, Ink(), hset::Align::Center);
            for (int p : {gov::kThrLess, gov::kThrMore}) {
                const Rect r = GR(gov::PartRect(i, p));
                const bool hot = v.hover == gov::RowCell(i, p);
                if (g_game) Fit(cv, Art(p == gov::kThrLess ? gameui::Sprite::ArrowLeft : gameui::Sprite::ArrowRight), hot ? 1 : 0, Rect{r.l + 4, r.t + 4, r.r - 4, r.b - 4});
                else {
                    const float cy = 0.5f * (r.t + r.b), tip = p == gov::kThrLess ? r.l + 10.0f : r.r - 10.0f, back = p == gov::kThrLess ? r.r - 12.0f : r.l + 12.0f;
                    Seg(cv, tip, cy, back, cy - 12.0f, 4.0f, hot ? kGoldBright : kSilverDim);
                    Seg(cv, tip, cy, back, cy + 12.0f, 4.0f, hot ? kGoldBright : kSilverDim);
                }
            }
        }
        const bool armed = v.clearArmed == g.id;
        GButton(cv, fonts, GR(gov::PartRect(i, gov::kRowRerecord)), uitext::Tr("vr.gestures.rerecord", "Re-record"), v.hover == gov::RowCell(i, gov::kRowRerecord),
                gov::PartUsable(g, gov::kRowRerecord) && !v.waiting, 22);
        GButton(cv, fonts, GR(gov::PartRect(i, gov::kRowClear)), armed ? uitext::Tr("vr.gestures.clear_sure", "Sure?") : uitext::Tr("vr.gestures.clear", "Clear"),
                v.hover == gov::RowCell(i, gov::kRowClear), !v.waiting, 22, armed ? (g_game ? kGameGoldBright : kRedBright) : kGameText);
        GButton(cv, fonts, GR(gov::PartRect(i, gov::kRowBind)), uitext::Tr("vr.gestures.bind", "Bind"), v.hover == gov::RowCell(i, gov::kRowBind), !v.waiting, 22);
    }
}

void DrawGestureChoices(Canvas& cv, Fonts& fonts, const gov::View& v) {
    const char* title = v.screen == gov::Screen::NewType ? uitext::Tr("vr.gestures.choose_type", "New gesture: what kind?")
                                                         : uitext::Tr("vr.gestures.choose_hand", "New gesture: which hand?");
    GLine(cv, fonts, title, Rect{160, 132, 1440, 180}, 34, true, InkGold(), hset::Align::Center);
    const int n = v.choices;
    for (int i = 0; i < n; ++i) {
        const Rect r = GR(gov::ChoiceRect(i, n));
        const bool hot = v.hover == gov::kChoice0 + i;
        GField(cv, r, hot);
        std::string word, text;
        if (v.screen == gov::Screen::NewType) {
            word = TypeWord(uint8_t(BW_LINK_GT_GLYPH + i));
            text = i == 0 ? uitext::Tr("vr.gestures.about_glyph", "A shape drawn in the air - the trigger held is the pen")
                 : i == 1 ? uitext::Tr("vr.gestures.about_pose", "Where your hands are and how they are closed - taken at the trigger's press")
                          : uitext::Tr("vr.gestures.about_motion", "A movement - the trigger starts it, the trigger again ends it");
        } else {
            word = HandWord(gov::HandOfChoice(i), v.newType);
            text = v.newType == BW_LINK_GT_POSE ? uitext::Tr("vr.gestures.about_hand_pose", "The trigger of this hand takes the pose")
                 : v.newType == BW_LINK_GT_GLYPH && i == 2 ? uitext::Tr("vr.gestures.about_hand_either", "Drawn by either hand, with its trigger - either casts it")
                                                           : uitext::Tr("vr.gestures.about_hand", "This hand draws or moves, with its trigger");
        }
        GLine(cv, fonts, word, Rect{r.l + 30, r.t + 14, r.r - 30, r.t + 78}, 44, true, hot ? InkHot() : InkGold(), hset::Align::Center);
        GLine(cv, fonts, text, Rect{r.l + 30, r.t + 82, r.r - 30, r.b - 16}, 26, false, Ink(), hset::Align::Center);
    }
}

// BindSkill's game actions: a tile each with its name, the one bound framed; without the engine's call, what it needs.
void DrawGestureActions(Canvas& cv, Fonts& fonts, const gov::View& v) {
    if (!v.actionsOk) {
        UseFace(fonts, false, "x");
        Text(cv, fonts, uitext::Tr("vr.gestures.actions_need", "Game actions are kept by the in-game gesture engine: tick In-game gesture engine in D2R VR Settings (Gestures), with a d2r_vr_gestures.dll that binds actions."),
             Rect{220, 330, 1380, 520}, 32, false, InkDim());
        fonts.use = nullptr;
        return;
    }
    for (int i = 0; i < v.actCount; ++i) {
        const gact::Def& d = gact::All().At(v.act[i]);
        const Rect r = GR(gov::GridRect(i));
        const bool hot = v.hover == gov::kGrid0 + i, cur = std::strcmp(v.bindAction, d.id) == 0;
        GField(cv, r, hot);
        if (cur) RoundRect(cv, Rect{r.l - 4, r.t - 4, r.r + 4, r.b + 4}, 10.0f, Col{0, 0, 0}, 0.0f, kGoldBright, 1.0f, 3.0f);
        const char* kind = d.kind == gact::Kind::Pad ? uitext::Tr("vr.gestures.kind_pad", "pad") : d.kind == gact::Kind::Item ? uitext::Tr("vr.gestures.kind_item", "item")
                         : d.kind == gact::Kind::Key ? uitext::Tr("vr.gestures.kind_key", "keyboard")
                                                     : uitext::Tr("vr.gestures.kind_game", "game");
        GLine(cv, fonts, kind, Rect{r.l + 6, r.t + 8, r.r - 6, r.t + 36}, 18, false, InkDim(), hset::Align::Center);
        UseFace(fonts, false, d.name);
        Text(cv, fonts, d.name, Rect{r.l + 8, r.t + 40, r.r - 8, r.b - 8}, 22, false, hot ? kGoldBright : cur ? InkHot() : kSilver);
        fonts.use = nullptr;
    }
}

void DrawGestureGrid(Canvas& cv, Fonts& fonts, const gov::View& v) {
    if (v.screen == gov::Screen::BindSkill) {   // Skills | Game actions
        for (int i = 0; i < 2; ++i)
            GButton(cv, fonts, GR(gov::BindTabRect(i)), i ? uitext::Tr("vr.gestures.tab_actions", "Game actions") : uitext::Tr("vr.gestures.tab_skills", "Skills"),
                    v.hover == gov::kBindTabSkills + i || v.bindTab == i, !(v.bindTrigger && i == 0), 26, v.bindTab == i ? kGameGoldBright : kGameText);   // (a trigger's: actions only)
        if (v.pickPages > 1)
            GLine(cv, fonts, uitext::TrF("vr.tiles.page", "Page {page} of {pages}", {{"page", std::to_string(v.pickPage + 1)}, {"pages", std::to_string(v.pickPages)}}),
                  Rect{1150, 124, 1500, 162}, 24, false, InkDim(), hset::Align::Center);
        if (v.bindTab) { DrawGestureActions(cv, fonts, v); return; }
    } else {
        std::string title = uitext::Tr("vr.gestures.choose_skill", "New gesture: the skill it casts");
        if (v.pickPages > 1) title += "   " + uitext::TrF("vr.tiles.page", "Page {page} of {pages}", {{"page", std::to_string(v.pickPage + 1)}, {"pages", std::to_string(v.pickPages)}});
        GLine(cv, fonts, title, Rect{160, 124, 1440, 162}, 30, true, InkGold(), hset::Align::Center);
    }
    if (!v.options) {
        GLine(cv, fonts, uitext::Tr("vr.skills.none", "No skills to choose from"), Rect{200, 380, 1400, 460}, 40, false, InkDim(), hset::Align::Center);
        return;
    }
    for (int i = 0; i < v.options; ++i) {
        const Skill& sk = v.option[i];
        PlateLook k;
        k.hot = v.hover == gov::kGrid0 + i;
        k.active = v.screen == gov::Screen::BindSkill && sk.id == v.bindSkill && !v.bindAction[0];
        DrawTile(cv, fonts, GR(gov::GridTileRect(i)), k, &sk, false);
        UseFace(fonts, false, sk.name);
        Text(cv, fonts, sk.name, GR(gov::GridNameRect(i)), 20, false, k.hot ? kGoldBright : kSilver);
        fonts.use = nullptr;
    }
}

void DrawGestureRecording(Canvas& cv, Fonts& fonts, const gov::View& v) {
    std::string title = v.recName[0] ? uitext::TrF("vr.gestures.rec_again", "Re-recording {name}", {{"name", v.recName}})
                                     : uitext::TrF("vr.gestures.rec_new", "Recording: {type}", {{"type", TypeWord(v.recType)}});
    title += " - " + std::string(HandWord(v.recHand, v.recType));
    if (v.recSkill[0]) title += " - " + uitext::TrF("vr.gestures.for_skill", "for {skill}", {{"skill", v.recSkill}});
    GLine(cv, fonts, title, GR(gov::kRecTitle), 36, true, InkGold(), hset::Align::Center);
    // which trigger is the pen: the hand chosen (a pose of both hands: either)
    const std::string trigger = v.recHand == BW_LINK_GH_LEFT ? uitext::Tr("vr.gestures.trigger_left", "the LEFT trigger")
                              : v.recHand == BW_LINK_GH_RIGHT ? uitext::Tr("vr.gestures.trigger_right", "the RIGHT trigger")
                                                              : uitext::Tr("vr.gestures.trigger_either", "a trigger");
    const std::string how = v.recType == BW_LINK_GT_GLYPH ? uitext::TrF("vr.gestures.how_glyph2", "Hold {trigger} and draw the shape in the air in front of you. Let go to finish.", {{"trigger", trigger}})
                          : v.recType == BW_LINK_GT_POSE  ? uitext::TrF("vr.gestures.how_pose2", "Hold the pose - your hands where you want them, a fist closed for a grab - then press {trigger}. It is taken at once.", {{"trigger", trigger}})
                                                          : uitext::TrF("vr.gestures.how_motion2", "Press {trigger}, make the movement, press it again.", {{"trigger", trigger}});
    UseFace(fonts, false, how.c_str());
    Text(cv, fonts, how.c_str(), GR(gov::kRecText), 38, false, Ink());
    fonts.use = nullptr;
    // the state on a plate, a red light while it records
    const Rect plate = GR(gov::kRecState);
    GField(cv, plate, false);
    const char* state = v.failed ? uitext::Tr("vr.gestures.state_failed", "Not taken - try again")
                      : v.queued ? uitext::Tr("vr.gestures.state_letgo", "Let go of the trigger...")
                      : v.recordState == BW_LINK_REC_NONE ? uitext::Tr("vr.gestures.state_asking", "Asking BodyWalk...")
                      : v.recordState == BW_LINK_REC_DRAWING ? (v.recType == BW_LINK_GT_GLYPH ? uitext::Tr("vr.gestures.state_drawing", "Drawing...") : uitext::Tr("vr.gestures.state_moving", "Recording..."))
                      : v.recordState == BW_LINK_REC_SAVED ? uitext::Tr("vr.gestures.state_saved", "Taken")
                                                           : uitext::Tr("vr.gestures.state_ready", "Ready");
    const bool drawing = v.recordState == BW_LINK_REC_DRAWING && !v.failed;
    const float cy = 0.5f * (plate.t + plate.b);
    Disc(cv, plate.l + 60.0f, cy, 20.0f, Col{0, 0, 0}, 0.7f);
    Disc(cv, plate.l + 60.0f, cy, 16.0f, v.failed ? Col{120, 120, 120} : drawing ? Col{240, 50, 40} : Col{150, 40, 32});
    GLine(cv, fonts, state, Rect{plate.l + 100.0f, plate.t, plate.r - 30.0f, plate.b}, 48, true, v.failed ? InkDim() : drawing ? (g_game ? kGameGoldBright : kRedBright) : InkHot(),
          hset::Align::Center);
}

// The ray's colours as swatches, `now` framed, the one pointed at lit.
void DrawSwatches(Canvas& cv, const gov::View& v, uint32_t now, bool big) {
    for (int i = 0; i < gov::kSwatches; ++i) {
        const Rect r = GR(gov::SwatchRect(i, big));
        const bool hot = v.hover == gov::kSwatch0 + i, cur = (now | 0xFF000000u) == gov::kPalette[i];
        if (cur) RoundRect(cv, Rect{r.l - 6, r.t - 6, r.r + 6, r.b + 6}, big ? 14.0f : 8.0f, Col{0, 0, 0}, 0.0f, kGoldBright, 1.0f, big ? 4.0f : 3.0f);
        RoundRect(cv, r, big ? 10.0f : 6.0f, FromRgba(gov::kPalette[i]), 1.0f, hot ? InkHot() : Col{20, 18, 16}, 1.0f, hot ? (big ? 5.0f : 3.0f) : 2.0f);
    }
}

// Colour: a glyph's / motion's ray colour - the gesture in the colour pointed at, the swatches under it.
void DrawGestureColour(Canvas& cv, Fonts& fonts, const gov::View& v) {
    if (!v.haveColour) return;
    const BW_LinkGesture& g = v.colour.g;
    GLine(cv, fonts, uitext::TrF("vr.gestures.colour_title", "Ray colour: {name}", {{"name", g.name[0] ? std::string(g.name) : std::string(TypeWord(g.type))}}),
          GR(gov::kColourTitle), 34, true, InkGold(), hset::Align::Center);
    const int hot = v.hover >= gov::kSwatch0 && v.hover < gov::kSwatch0 + gov::kSwatches ? v.hover - gov::kSwatch0 : -1;
    DrawGesturePicture(cv, fonts, g, GR(gov::kColourPic), true, false, nullptr, -1.0f, hot >= 0 ? gov::kPalette[hot] : 0u);
    DrawSwatches(cv, v, g.rgba, true);
}

const char* TrigRoleWord(int role) {
    switch (role) {
    case 1: return uitext::Tr("vr.gestures.trig_glyphs", "Glyphs");
    case 2: return uitext::Tr("vr.gestures.trig_shift", "Shift");
    case 3: return uitext::Tr("vr.gestures.trig_action", "Action");
    default: return uitext::Tr("vr.gestures.trig_before", "As before");
    }
}
// Triggers: each trigger's role (as before / glyphs / Shift / an action) - one trigger can be the glyphs' and the other kept as it was.
void DrawGestureTriggers(Canvas& cv, Fonts& fonts, const gov::View& v) {
    GLine(cv, fonts, uitext::Tr("vr.gestures.trig_title", "What each trigger does in play (F4)"), Rect{160, 128, 1440, 176}, 32, true, InkGold(), hset::Align::Center);
    for (int row = 0; row < 2; ++row) {
        const int role = v.trigRole[row];
        std::string label = row == 0 ? uitext::Tr("vr.gestures.trig_right", "Right trigger") : uitext::Tr("vr.gestures.trig_left", "Left trigger");
        label += ": ";
        label += role == 1 ? (row == 0 ? uitext::Tr("vr.gestures.trig_about_glyphs_r", "draws glyphs and casts the skill a Select glyph armed on it (its RT slot)")
                                       : uitext::Tr("vr.gestures.trig_about_glyphs_l", "draws glyphs and casts the skill a Select glyph armed on it (its LT + RT slot) - no Shift"))
               : role == 2 ? uitext::Tr("vr.gestures.trig_about_shift", "the game's Shift")
               : role == 3 ? uitext::TrF("vr.gestures.trig_about_action", "does {action}", {{"action", v.trigAction[row][0] ? gact::NameOf(v.trigAction[row]) : "?"}})
                           : (row == 0 ? uitext::Tr("vr.gestures.trig_about_before_r", "as before (its skill slot / the attack)")
                                       : uitext::Tr("vr.gestures.trig_about_before_l", "as before (the game's Shift)"));
        GLine(cv, fonts, label, GR(gov::TrigLabelRect(row)), 28, false, Ink(), hset::Align::Center);
        for (int r = 0; r < gov::kTrigRoles; ++r)
            GButton(cv, fonts, GR(gov::TrigRoleRect(row, r)), TrigRoleWord(r), v.hover == gov::kTrigRole0 + row * gov::kTrigRoles + r || role == r, true, 28,
                    role == r ? kGameGoldBright : kGameText);
        if (role == 3)
            GButton(cv, fonts, GR(gov::TrigActionRect(row)), uitext::Tr("vr.gestures.trig_choose", "Choose its action"), v.hover == gov::kTrigAction0 + row, true, 24);
    }
}

void DrawGestureReview(Canvas& cv, Fonts& fonts, const gov::View& v) {
    if (!v.haveReview) return;
    DrawGesturePicture(cv, fonts, v.review.g, GR(gov::kReviewPic), true, false, v.review.zone, v.playId == v.review.g.id ? v.playT : -1.0f);
    if (gov::HasRay(v.review.g)) DrawSwatches(cv, v, v.review.g.rgba, false);   // the ray's colour, right here
    GLine(cv, fonts, v.review.g.name[0] ? std::string(v.review.g.name) : std::string(TypeWord(v.review.g.type)), GR(gov::kReviewName), 40, true, InkGold(), hset::Align::Center);
    // the line in two halves: the type and hand (a glyph's hand steps there), what it does (a skill's mode steps there)
    const std::string kindLine = std::string(TypeWord(v.review.g.type)) + " - " + HandWord(v.review.g.hand, v.review.g.type);
    const std::string doesLine = v.review.action[0] ? uitext::TrF("vr.gestures.does", "does {action}", {{"action", gact::NameOf(v.review.action)}})
                               : v.review.skill[0] ? (v.review.mode ? uitext::TrF("vr.gestures.arms", "arms {skill} on the trigger", {{"skill", v.review.skill}})
                                                                    : uitext::TrF("vr.gestures.casts", "casts {skill}", {{"skill", v.review.skill}}))
                                                   : std::string(uitext::Tr("vr.gestures.no_skill", "casts nothing"));
    if (v.hover == gov::kRevHand) RoundRect(cv, GR(gov::kReviewHandHalf), 8.0f, Col{0, 0, 0}, 0.0f, InkHot(), 0.9f, 2.0f);
    if (v.hover == gov::kRevMode) RoundRect(cv, GR(gov::kReviewModeHalf), 8.0f, Col{0, 0, 0}, 0.0f, InkHot(), 0.9f, 2.0f);
    GLine(cv, fonts, kindLine, GR(gov::kReviewHandHalf), 28, false, v.hover == gov::kRevHand ? InkHot() : Ink(), hset::Align::Right);
    GLine(cv, fonts, doesLine, GR(gov::kReviewModeHalf), 28, false, v.hover == gov::kRevMode ? InkHot() : v.review.mode ? InkGold() : Ink(), hset::Align::Left);
    if (v.review.g.type == BW_LINK_GT_POSE) {   // its zone's size, "< Zone 12 cm >"
        const float r = v.review.zone[v.review.g.hand == BW_LINK_GH_LEFT ? 0 : 1];
        GLine(cv, fonts, uitext::TrF("vr.gestures.zone_size", "Zone {cm} cm", {{"cm", std::to_string((int)std::lround(100.0f * r))}}), GR(gov::kZoneText), 30, false,
              Ink(), hset::Align::Center);
        for (int c : {gov::kZoneLess, gov::kZoneMore}) {
            const Rect rr = GR(c == gov::kZoneLess ? gov::kZoneLessRect : gov::kZoneMoreRect);
            const bool hot = v.hover == c;
            if (g_game) Fit(cv, Art(c == gov::kZoneLess ? gameui::Sprite::ArrowLeft : gameui::Sprite::ArrowRight), hot ? 1 : 0, Rect{rr.l + 4, rr.t + 4, rr.r - 4, rr.b - 4});
            else {
                const float cy = 0.5f * (rr.t + rr.b), tip = c == gov::kZoneLess ? rr.l + 10.0f : rr.r - 10.0f, back = c == gov::kZoneLess ? rr.r - 12.0f : rr.l + 12.0f;
                Seg(cv, tip, cy, back, cy - 14.0f, 4.5f, hot ? kGoldBright : kSilverDim);
                Seg(cv, tip, cy, back, cy + 14.0f, 4.5f, hot ? kGoldBright : kSilverDim);
            }
        }
    }
}

const char* GestureButtonText(int cell) {
    switch (cell) {
    case gov::kNew: return uitext::Tr("vr.gestures.new", "New gesture");
    case gov::kPrev: return uitext::Tr("vr.tiles.previous", "Previous");
    case gov::kNext: return uitext::Tr("vr.tiles.next", "Next");
    case gov::kBack: return uitext::Tr("vr.tiles.back", "Back");
    case gov::kKeep: return uitext::Tr("vr.gestures.keep", "Keep");
    case gov::kRerecord: return uitext::Tr("vr.gestures.rerecord", "Re-record");
    case gov::kDiscard: return uitext::Tr("vr.gestures.discard", "Discard");
    case gov::kStop: return uitext::Tr("vr.gestures.stop", "Stop: B / Y");
    case gov::kRetry: return uitext::Tr("vr.gestures.retry", "Try again");
    case gov::kNoSkill: return uitext::Tr("vr.gestures.none", "No skill");
    case gov::kPlay: return uitext::Tr("vr.gestures.play", "Play");
    case gov::kZonesToggle: return "";   // (worded with its state: GestureFooterText)
    case gov::kTriggers: return uitext::Tr("vr.gestures.triggers", "Triggers");
    default: return "";
    }
}

// The note (what happened, what is in the way) in the window's red line, what to do in the dim one under it.
void DrawGestureLines(Canvas& cv, Fonts& fonts, const gov::View& v) {
    using N = gov::Note;
    std::string note;
    switch (v.note) {
    case N::NoAnswer: note = uitext::Tr("vr.gestures.no_answer", "No answer from BodyWalk"); break;
    case N::Busy: note = uitext::Tr("vr.gestures.busy", "BodyWalk is busy - try again"); break;
    case N::NoGesture: note = uitext::Tr("vr.gestures.gone", "That gesture is gone"); break;
    case N::Bad: note = uitext::Tr("vr.gestures.bad", "BodyWalk did not take that"); break;
    case N::NoSet: note = uitext::Tr("vr.gestures.no_set", "BodyWalk has no list for this class"); break;
    case N::Kept: note = uitext::Tr("vr.gestures.kept", "Kept"); break;
    case N::Discarded: note = uitext::Tr("vr.gestures.discarded", "Discarded"); break;
    case N::Cleared: note = uitext::Tr("vr.gestures.cleared", "Gesture cleared"); break;
    case N::Bound: note = uitext::Tr("vr.gestures.bound2", "Binding set"); break;
    case N::Coloured: note = uitext::Tr("vr.gestures.coloured", "Colour changed"); break;
    case N::Threshold: note = uitext::Tr("vr.gestures.threshold", "Threshold changed"); break;
    case N::NoActions: note = uitext::Tr("vr.gestures.no_actions", "Game actions need the in-game gesture engine"); break;
    case N::HandSet: note = uitext::Tr("vr.gestures.hand_set", "Hand changed"); break;
    case N::ModeSet: note = uitext::Tr("vr.gestures.mode_set", "Mode changed"); break;
    case N::NoModeSet: note = uitext::Tr("vr.gestures.no_mode_set", "This gesture engine cannot keep a mode yet (cast once only)"); break;
    case N::NoHandSet: note = uitext::Tr("vr.gestures.no_hand_set", "This gesture engine cannot change the hand - record it again with the hand you want"); break;
    case N::ClearAgain: note = uitext::Tr("vr.gestures.clear_again", "Clear again to delete it"); break;
    case N::ChooseReview: note = uitext::Tr("vr.gestures.choose_review", "Choose Keep, Re-record or Discard"); break;
    case N::NoSkills: note = uitext::Tr("vr.gestures.no_skills", "The game's skills are not read yet"); break;
    case N::Failed: note = uitext::Tr("vr.gestures.failed", "BodyWalk could not take that - try again"); break;
    case N::Cancelled: note = uitext::Tr("vr.gestures.cancelled", "Recording stopped"); break;
    case N::Stopped: note = uitext::Tr("vr.gestures.stopped", "Recording stopped"); break;
    default: break;
    }
    if (note.empty() && v.screen == gov::Screen::List && v.lastShown) {
        note = v.lastName[0] ? uitext::TrF("vr.gestures.last_match", "Last stroke: {name} {percent}", {{"name", v.lastName}, {"percent", Percent(v.lastPercent)}})
             : v.lastPercent >= 0.0f ? uitext::TrF("vr.gestures.last_none", "Last stroke: no match (best {percent})", {{"percent", Percent(v.lastPercent)}})
                                     : std::string();
    }
    if (note.empty() && v.screen == gov::Screen::List && v.pages > 1)
        note = uitext::TrF("vr.tiles.page", "Page {page} of {pages}", {{"page", std::to_string(v.page + 1)}, {"pages", std::to_string(v.pages)}});
    const tiles::PRect rl = tiles::kRedLine;
    if (!note.empty()) {
        if (g_game) GameWord(cv, fonts, note.c_str(), Rect{rl.l, rl.t, rl.r, rl.b}, 30, kRed);
        else CapsCentred(cv, fonts, note.c_str(), 0.5f * (rl.l + rl.r), Rect{rl.l, rl.t, rl.r, rl.b}, 30, 2.5f, kRed);
    }
    std::string hint;
    switch (v.screen) {
    case gov::Screen::List:
        hint = v.setKey[0] ? uitext::TrF("vr.gestures.hint_list3", "{count} gestures for {set} - a picture plays it, a skill's line: cast once or arm the trigger",
                                         {{"count", std::to_string(v.count)}, {"set", v.setKey}})
                           : std::string(uitext::Tr("vr.gestures.hint_none", "Your BodyWalk gestures, per class"));
        break;
    case gov::Screen::Recording: hint = uitext::Tr("vr.gestures.hint_rec", "The trigger is BodyWalk's now - B or Y stops"); break;
    case gov::Screen::Review:
        hint = v.haveReview && v.review.g.type == BW_LINK_GT_GLYPH
                   ? uitext::Tr("vr.gestures.hint_review_glyph", "Keep it, record it again, or drop this take - its hand line steps Right / Left / Either")
                   : uitext::Tr("vr.gestures.hint_review", "Keep it, record it again, or drop this take");
        break;
    case gov::Screen::Colour: hint = uitext::Tr("vr.gestures.hint_colour", "The trail it draws in the air and its flash take this colour - B or Y: back"); break;
    case gov::Screen::Triggers: hint = uitext::Tr("vr.gestures.hint_triggers3", "A glyph trigger: draw for a glyph, hold still to aim, let go to cast - B or Y: back"); break;
    default: hint = uitext::Tr("vr.gestures.hint_back", "B or Y: one step back"); break;
    }
    const tiles::PRect hl = tiles::kHintLine;
    UseFace(fonts, false, hint.c_str());
    CapsCentred(cv, fonts, hint.c_str(), 0.5f * (hl.l + hl.r), Rect{hl.l, hl.t, hl.r, hl.b}, 22, 1.5f, kSilverDim, false);
    fonts.use = nullptr;
}

void DrawGestureFooter(Canvas& cv, Fonts& fonts, const gov::View& v) {
    int cells[5];
    const int n = gov::FooterCells(v, cells);
    for (int i = 0; i < n; ++i) {
        const int c = cells[i];
        if ((c == gov::kPrev || c == gov::kNext) && (v.screen == gov::Screen::List ? v.pages : v.pickPages) <= 1) continue;   // (one page: no arrows)
        if (c == gov::kNew && v.avail != gov::Avail::Ok) continue;   // (nothing to manage: the fix in the middle says what to do)
        const char* label = c == gov::kZonesToggle ? (v.zonesShown ? uitext::Tr("vr.gestures.zones_shown", "Zones: shown") : uitext::Tr("vr.gestures.zones_hidden", "Zones: hidden"))
                                                   : GestureButtonText(c);
        GButton(cv, fonts, GR(gov::FooterRect(i, n)), label, v.hover == c, gov::FooterEnabled(v, c) || c == gov::kStop, 30);
    }
}

// The whole tab (under the tab strip the title plate holds).
void DrawGestures(Canvas& cv, Fonts& fonts, const gov::View& v) {
    switch (v.screen) {
    case gov::Screen::List: DrawGestureRows(cv, fonts, v); break;
    case gov::Screen::NewType:
    case gov::Screen::NewHand: DrawGestureChoices(cv, fonts, v); break;
    case gov::Screen::NewSkill:
    case gov::Screen::BindSkill: DrawGestureGrid(cv, fonts, v); break;
    case gov::Screen::Recording: DrawGestureRecording(cv, fonts, v); break;
    case gov::Screen::Review: DrawGestureReview(cv, fonts, v); break;
    case gov::Screen::Colour: DrawGestureColour(cv, fonts, v); break;
    case gov::Screen::Triggers: DrawGestureTriggers(cv, fonts, v); break;
    }
    DrawGestureLines(cv, fonts, v);
    DrawGestureFooter(cv, fonts, v);
}
