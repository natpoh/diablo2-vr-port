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
const char* HandWord(uint8_t hand) {
    switch (hand) {
    case BW_LINK_GH_LEFT: return uitext::Tr("vr.gestures.hand_left", "Left hand");
    case BW_LINK_GH_RIGHT: return uitext::Tr("vr.gestures.hand_right", "Right hand");
    default: return uitext::Tr("vr.gestures.hand_both", "Both hands");
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

// A gesture's picture in a box (its own dark tile under it): `big` on the review screen.
void DrawGesturePicture(Canvas& cv, Fonts& fonts, const BW_LinkGesture& g, Rect box, bool big, bool hot) {
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
        const Col c = FromRgba(g.rgba);
        if (g.type == BW_LINK_GT_GLYPH) {
            Stroke(cv, p, n, width, c);
        } else {
            Stroke(cv, p, n, width * 0.8f, c);
            gpic::P2 l, r;
            if (gpic::ArrowHead(p, n, big ? 46.0f : 16.0f, &l, &r)) {
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
        break;
    }
    case BW_LINK_GT_POSE: {
        const gpic::Box inner{b.l + margin * 0.5f, b.t + margin * 0.5f, b.r - margin * 0.5f, b.b - margin * 0.5f};
        const gpic::PoseLayout lay = gpic::LayoutPose(inner);
        const gpic::Figure thinker = gpic::StickFigure(g.joints, g.jointValid, g.grip, lay.thinker, big ? 6.0f : 2.0f);
        DrawFigure(cv, thinker, Ink(), big ? 4.0f : 1.6f);
        DrawCloud(cv, lay.cloud, big ? 4.0f : 2.0f);
        const gpic::Figure pose = gpic::StickFigure(g.joints, g.jointValid, g.grip, lay.cloud.inner, big ? 8.0f : 2.0f, 0.28f);   // (leg stubs: the arms bigger)
        DrawFigure(cv, pose, Col{34, 30, 26}, big ? 7.0f : 2.4f);
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
        DrawGesturePicture(cv, fonts, g, GR(gov::PicRect(i)), false, v.hover == gov::RowCell(i, gov::kPic));
        GLine(cv, fonts, g.name[0] ? std::string(g.name) : std::string(TypeWord(g.type)), GR(gov::NameRect(i)), 32, true, rowHot ? InkHot() : InkGold());
        std::string casts = row.skill[0] ? uitext::TrF("vr.gestures.casts", "casts {skill}", {{"skill", row.skill}}) : std::string(uitext::Tr("vr.gestures.no_skill", "casts nothing"));
        if (g.lastPercent >= 0.0f) casts += "   " + uitext::TrF("vr.gestures.last_read", "last read {percent}", {{"percent", Percent(g.lastPercent)}});
        if (g.flags & BW_LINK_GF_REVIEW) casts += "   " + std::string(uitext::Tr("vr.gestures.in_review", "(new take waiting)"));
        GLine(cv, fonts, casts, GR(gov::SkillLineRect(i)), 24, false, row.skill[0] ? Ink() : InkDim());
        GLine(cv, fonts, std::string(TypeWord(g.type)) + " - " + HandWord(g.hand), GR(gov::TypeRect(i)), 22, false, Ink(), hset::Align::Center);
        if (g.type != BW_LINK_GT_BUTTON) {   // the threshold, with its arrows
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
            word = HandWord(gov::HandOfChoice(i));
            text = v.newType == BW_LINK_GT_POSE ? uitext::Tr("vr.gestures.about_hand_pose", "The trigger of this hand takes the pose")
                                                : uitext::Tr("vr.gestures.about_hand", "This hand draws or moves, with its trigger");
        }
        GLine(cv, fonts, word, Rect{r.l + 30, r.t + 14, r.r - 30, r.t + 78}, 44, true, hot ? InkHot() : InkGold(), hset::Align::Center);
        GLine(cv, fonts, text, Rect{r.l + 30, r.t + 82, r.r - 30, r.b - 16}, 26, false, Ink(), hset::Align::Center);
    }
}

void DrawGestureGrid(Canvas& cv, Fonts& fonts, const gov::View& v) {
    std::string title = v.screen == gov::Screen::BindSkill ? std::string(uitext::Tr("vr.gestures.choose_bind", "The skill this gesture casts"))
                                                           : std::string(uitext::Tr("vr.gestures.choose_skill", "New gesture: the skill it casts"));
    if (v.pickPages > 1) title += "   " + uitext::TrF("vr.tiles.page", "Page {page} of {pages}", {{"page", std::to_string(v.pickPage + 1)}, {"pages", std::to_string(v.pickPages)}});
    GLine(cv, fonts, title, Rect{160, 124, 1440, 162}, 30, true, InkGold(), hset::Align::Center);
    if (!v.options) {
        GLine(cv, fonts, uitext::Tr("vr.skills.none", "No skills to choose from"), Rect{200, 380, 1400, 460}, 40, false, InkDim(), hset::Align::Center);
        return;
    }
    for (int i = 0; i < v.options; ++i) {
        const Skill& sk = v.option[i];
        PlateLook k;
        k.hot = v.hover == gov::kGrid0 + i;
        k.active = v.screen == gov::Screen::BindSkill && sk.id == v.bindSkill;
        DrawTile(cv, fonts, GR(gov::GridTileRect(i)), k, &sk, false);
        UseFace(fonts, false, sk.name);
        Text(cv, fonts, sk.name, GR(gov::GridNameRect(i)), 20, false, k.hot ? kGoldBright : kSilver);
        fonts.use = nullptr;
    }
}

void DrawGestureRecording(Canvas& cv, Fonts& fonts, const gov::View& v) {
    std::string title = v.recName[0] ? uitext::TrF("vr.gestures.rec_again", "Re-recording {name}", {{"name", v.recName}})
                                     : uitext::TrF("vr.gestures.rec_new", "Recording: {type}", {{"type", TypeWord(v.recType)}});
    title += " - " + std::string(HandWord(v.recHand));
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

void DrawGestureReview(Canvas& cv, Fonts& fonts, const gov::View& v) {
    if (!v.haveReview) return;
    DrawGesturePicture(cv, fonts, v.review.g, GR(gov::kReviewPic), true, false);
    GLine(cv, fonts, v.review.g.name[0] ? std::string(v.review.g.name) : std::string(TypeWord(v.review.g.type)), GR(gov::kReviewName), 40, true, InkGold(), hset::Align::Center);
    std::string line = std::string(TypeWord(v.review.g.type)) + " - " + HandWord(v.review.g.hand);
    line += " - " + (v.review.skill[0] ? uitext::TrF("vr.gestures.casts", "casts {skill}", {{"skill", v.review.skill}}) : std::string(uitext::Tr("vr.gestures.no_skill", "casts nothing")));
    GLine(cv, fonts, line, GR(gov::kReviewLine), 28, false, Ink(), hset::Align::Center);
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
    case N::Bound: note = uitext::Tr("vr.gestures.bound", "Skill set"); break;
    case N::Coloured: note = uitext::Tr("vr.gestures.coloured", "Colour changed"); break;
    case N::Threshold: note = uitext::Tr("vr.gestures.threshold", "Threshold changed"); break;
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
        hint = v.setKey[0] ? uitext::TrF("vr.gestures.hint_list", "{count} gestures for {set} - point at a picture to change its colour",
                                         {{"count", std::to_string(v.count)}, {"set", v.setKey}})
                           : std::string(uitext::Tr("vr.gestures.hint_none", "Your BodyWalk gestures, per class"));
        break;
    case gov::Screen::Recording: hint = uitext::Tr("vr.gestures.hint_rec", "The trigger is BodyWalk's now - B or Y stops"); break;
    case gov::Screen::Review: hint = uitext::Tr("vr.gestures.hint_review", "Keep it, record it again, or drop this take"); break;
    default: hint = uitext::Tr("vr.gestures.hint_back", "B or Y: one step back"); break;
    }
    const tiles::PRect hl = tiles::kHintLine;
    UseFace(fonts, false, hint.c_str());
    CapsCentred(cv, fonts, hint.c_str(), 0.5f * (hl.l + hl.r), Rect{hl.l, hl.t, hl.r, hl.b}, 22, 1.5f, kSilverDim, false);
    fonts.use = nullptr;
}

void DrawGestureFooter(Canvas& cv, Fonts& fonts, const gov::View& v) {
    int cells[4];
    const int n = gov::FooterCells(v, cells);
    for (int i = 0; i < n; ++i) {
        const int c = cells[i];
        if ((c == gov::kPrev || c == gov::kNext) && (v.screen == gov::Screen::List ? v.pages : v.pickPages) <= 1) continue;   // (one page: no arrows)
        if (c == gov::kNew && v.avail != gov::Avail::Ok) continue;   // (nothing to manage: the fix in the middle says what to do)
        GButton(cv, fonts, GR(gov::FooterRect(i, n)), GestureButtonText(c), v.hover == c, gov::FooterEnabled(v, c) || c == gov::kStop, 30);
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
    }
    DrawGestureLines(cv, fonts, v);
    DrawGestureFooter(cv, fonts, v);
}
