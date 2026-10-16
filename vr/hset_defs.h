#pragma once
// The keys the in-headset settings panel offers ([openxr] headset_settings, vr/xr.cpp, docs in
// d2r_vr.ini). One line a key; a new tick box of a feature is one line more. Pages are the panel's tabs
// (at most kMaxPages, at most kRowsPerPage rows each); a toggle is a key with min 0, max 1, step 1.
//
// Four pages: View and Options are rows of this table; Video (the VR render size, vr/hset_video.h) and Body (the T-pose
// measurement, vr/hset_body.h) are drawn by hand and own the keys at the end of the table, which have no row (kNoPage):
// the panel still reads them and writes the ones that changed, with the same conflict check as every other key.
//
// Panel after bearstonem/d2r-pcvr's vr_settings.h (MIT): a table of definitions, a draft of the values,
// Apply writes only what changed.

#include <array>
#include <cstdint>

namespace hset {

constexpr int kMaxPages = 4;
constexpr int kRowsPerPage = 6;
enum Page : int { kPageView = 0, kPageOptions = 1, kPageVideo = 2, kPageBody = 3 };
constexpr int kPageCount = 4;
constexpr int kNoPage = -1;   // a key with no row of its own: a hand-drawn page reads and writes it

struct Def {
    const wchar_t* section;
    const wchar_t* key;
    const char* label;
    const char* detail;
    float def, min, max, step;   // def = what the game assumes when the key is missing from the file
    const char* fmt;             // a printf format for one float, a stepper's shown value ("%.1f m")
    const char* off;             // a toggle's two faces
    const char* on;
    int page;
};

inline constexpr Def kDefs[] = {
    // page 0: how the picture stands
    {L"screen", L"menu_distance_m", "Panel distance", "A panel open in the game: inventory, stash, trader",
     1.6f, 0.5f, 8.0f, 0.1f, "%.1f m", "", "", 0},
    {L"openxr", L"flat_distance_m", "Screen distance", "Menus, loading screens, the flat views F1 and F3",
     2.0f, 0.5f, 10.0f, 0.1f, "%.1f m", "", "", 0},
    {L"openxr", L"flat_width_m", "Screen width", "How wide the menus and panels stand",
     2.4f, 0.5f, 8.0f, 0.1f, "%.1f m", "", "", 0},
    {L"openxr", L"brightness", "Brightness", "Everything in the headset, 1 = as the game draws it",
     1.0f, 0.2f, 1.5f, 0.05f, "%.2f", "", "", 0},
    {L"stereo", L"user_height_m", "Your height", "World and hands scale; BodyWalk's Body Height wins",
     1.75f, 1.10f, 2.30f, 0.01f, "%.2f m", "", "", 0},
    // page 1: options
    {L"openxr", L"laser", "Laser on menus", "Point a controller at a menu and it is the mouse",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", 1},
    {L"openxr", L"laser_hand", "Laser hand", "The hand the laser comes from",
     1.0f, 0.0f, 1.0f, 1.0f, "", "Left", "Right", 1},
    {L"openxr", L"menu_3d", "Menus as a panel", "The world stays in 3D behind an open menu",
     1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", 1},
    // How the right stick turns (shared/turn_mode.h, after bearstonem/d2r-pcvr, MIT): the toggle is its own key, so the angle and
    // the speed are both kept when it is switched. An older ini has no `smooth` and says snap=0 for smooth turning: the store
    // reads it that way (hset_store.cpp), and writes `smooth` with `snap` so an angle chosen later does not flip the mode.
    // The panel's speed starts at 30 (a step of 30 from 30 keeps the default 120 on the grid); the ini takes 10 - 720.
    {L"turn", L"smooth", "Smooth turning", "Right stick: turn continuously, or one snap a flick",
     1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", 1},
    {L"turn", L"snap", "Snap turn angle", "Degrees per flick, while smooth turning is off",
     30.0f, 15.0f, 180.0f, 15.0f, "%.0f deg", "", "", 1},
    {L"turn", L"speed", "Smooth turn speed", "Degrees per second, while smooth turning is on",
     120.0f, 30.0f, 720.0f, 30.0f, "%.0f deg/s", "", "", 1},
    // ADD NEW TICK BOXES HERE, one line each (page 0 is full: its sixth row is the game view's F1 - F4 buttons; page 1 is full too: the
    // next key needs a page of its own - kMaxPages and the tabs' places in the tile window and the panel).
    //
    // -- no row: the Video page. [render] vr_resolution_px = a square's side in pixels (0 = not used), vr_resolution = the older
    //    mode (0 the window's own size, 1 the headset's recommendation, 2..6 a square); shared/render_size.h reads the two together.
    {L"render", L"vr_resolution", "VR render size: mode", "0 window, 1 headset, 2-6 an older square",
     0.0f, 0.0f, 6.0f, 1.0f, "%.0f", "", "", kNoPage},
    {L"render", L"vr_resolution_px", "VR render size: side", "A square's side in pixels, 0 = not used",
     0.0f, 0.0f, 4096.0f, 1.0f, "%.0f", "", "", kNoPage},
    // -- no row: the Body page reads the T-pose measurement (the plugin writes it, vr/calibration.cpp) and can clear it.
    {L"calibration", L"use", "Use the measurement", "The measured height and arm span are in use",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    {L"calibration", L"version", "Measurement stored", "1 = a measurement is stored",
     0.0f, 0.0f, 1.0f, 1.0f, "", "No", "Yes", kNoPage},
    {L"calibration", L"eye_height_m", "Measured eye height", "Metres over the floor",
     0.0f, 0.0f, 3.0f, 0.0001f, "%.2f m", "", "", kNoPage},
    {L"calibration", L"arm_span_m", "Measured arm span", "Metres, grip to grip",
     0.0f, 0.0f, 3.0f, 0.0001f, "%.2f m", "", "", kNoPage},
};
constexpr int kCount = (int)(sizeof kDefs / sizeof kDefs[0]);
static_assert(kCount <= 32, "a change mask is 32 bits");
constexpr int kViewRow = kRowsPerPage - 1;   // the first page's last row is not the table's: the game view's four buttons (hset_model.h)

constexpr bool SameW(const wchar_t* a, const wchar_t* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
// The index in kDefs of a key, -1 if it is not in the table.
constexpr int IndexOf(const wchar_t* section, const wchar_t* key) {
    for (int i = 0; i < kCount; ++i)
        if (SameW(kDefs[i].section, section) && SameW(kDefs[i].key, key)) return i;
    return -1;
}
// The keys the hand-drawn pages work on.
constexpr int kKeyMode = IndexOf(L"render", L"vr_resolution");
constexpr int kKeyPx = IndexOf(L"render", L"vr_resolution_px");
constexpr int kKeyUse = IndexOf(L"calibration", L"use");
constexpr int kKeySmooth = IndexOf(L"turn", L"smooth");   // the turn keys: a legacy ini needs care (hset_store.cpp)
constexpr int kKeySnap = IndexOf(L"turn", L"snap");
constexpr int kKeyVersion = IndexOf(L"calibration", L"version");
constexpr int kKeyEye = IndexOf(L"calibration", L"eye_height_m");
constexpr int kKeySpan = IndexOf(L"calibration", L"arm_span_m");
static_assert(kKeyMode >= 0 && kKeyPx >= 0 && kKeyUse >= 0 && kKeyVersion >= 0 && kKeyEye >= 0 && kKeySpan >= 0, "the Video and Body pages' keys are in the table");
static_assert(kKeySmooth >= 0 && kKeySnap >= 0, "the turn keys are in the table");

constexpr bool IsToggle(const Def& d) { return d.min == 0.0f && d.max == 1.0f && d.step == 1.0f; }

constexpr int PageCount() { return kPageCount; }
constexpr const char* kPageNames[kMaxPages] = {"View", "Options", "Video", "Body"};

constexpr int RowsOn(int page) {
    int n = 0;
    for (const Def& d : kDefs) if (d.page == page) ++n;
    return n;
}
static_assert(RowsOn(kPageView) <= kViewRow, "the first page's table rows leave its last row to the game view's buttons");
// The index in kDefs of the row-th key of a page, -1 past the end.
constexpr int KeyAt(int page, int row) {
    int seen = 0;
    for (int i = 0; i < kCount; ++i)
        if (kDefs[i].page == page) {
            if (seen == row) return i;
            ++seen;
        }
    return -1;
}

using Values = std::array<float, kCount>;

}  // namespace hset
