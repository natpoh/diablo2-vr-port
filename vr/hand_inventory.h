#pragma once
// hand_inventory.h - [openxr] hand_inventory: away from town, in VR first person (F4) with native OpenXR and [openxr] menu_3d, the game's
// inventory hangs on the LEFT hand instead of in front of you, and the left stick goes on walking the hero while it is up (Serg, 2026-10-27:
// "the inventory on the left hand outside town, it must not stop me"). In town it stays the panel in front, as before. Unticked: as before.
//
// What it changes is where the game's own interface is shown and where the controls go - never what the game does with them:
//   - the panel: the game's whole inventory window, cut out of the interface layer menu_3d already takes out of the picture (hud.cpp
//     kNativePanel; Biggest / Steady below - the way the Evil Islands VR mod holds the game's HUD blocks on its left hand), hung over the left
//     controller and turned to the eyes (xr.cpp HangPanel), [openxr] hand_inventory_width_m wide;
//   - the laser: always the RIGHT hand while the inventory is up (its trigger the left button, as ever; the left hand's trigger and grip Ctrl
//     and Shift), its point on the cut is the cut's place in the window - the game's own mouse there; the left stick never parks it, it walks;
//   - a button of ours beside the panel, pressed with the laser, puts the inventory in front, full size (menu_3d's panel, as in town), and
//     back on the hand - remembered ([openxr] hand_inventory_front, written by the button): it opens the way it was last left, after a
//     restart too;
//   - an item dragged off the panel and let go there - only while the game says an item is on its cursor - is the game's own controller drop:
//     its inventory closed with the item held (by the game's own inventory action, the pad's Start as BodyWalk's 'D2R: Inventory'). In the
//     pad layout the client drops a cursor item only in InventoryItemManager::Update 0x1618A0 (0x15BDF0 at 0x1619F8 / 0x161A07): with an item
//     on the cursor, no control lock (0x13DE60) and its "armed" byte set - the byte is set every frame the inventory holds the control lock and
//     cleared only by a new game, a load screen and an input-method change (0x89EA60, 0x8A187B, 0x8A54DD), not by the inventory closing. A
//     mouse-style click "outside the panel" cannot do it: the tabbed menu lays a full-screen backdrop with its own click catcher under the
//     page (Controller/ControllerOverlayHD.json TransparentBG, BGClickCatcher). Nothing of ours drops anything;
//   - the left stick: kept from the menu's pad (it never moves the inventory's cursor - the game gets it at 0 0) and given to the walk. The
//     game's own walk reads its stick only past its gate (0x13DE60: the input handler's control-lock count at +0xE40 is 0 and the UI lets
//     the world have input) - and the controller's inventory holds that lock (Serg's test 2026-10-28: the walk read the stick 0 times a
//     second). So, while it does, the stick's own command is sent for it (below: "the walk with the inventory up") - the command the stick's
//     walk sends, with the game's own strength and run rules; when the game's walk reads the stick itself (another panel, no lock), ours
//     stands aside. vrcam counts both and the hero's steps and says it in the log;
//   - the long press of the left stick click (the inventory, both ways) is not refused for the stick's lean away from town
//     ([direct] hold_stick_max_pct): walking, it opens and closes.
// Pure: no game, no Windows, no OpenXR (cleanroom/tests/hand_inventory_test.cpp).

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "client_verdict.h"

namespace handinv {

constexpr float kWidthDefault = 0.52f, kWidthMin = 0.20f, kWidthMax = 1.00f;   // [openxr] hand_inventory_width_m: the inventory window's width
                                                                                 // (0.40 first; Serg 2026-10-28: 30 % bigger)
constexpr float kLiftM = 0.02f;   // the panel's lower edge this far above the controller

inline float Width(float v) { return std::isfinite(v) ? (v < kWidthMin ? kWidthMin : v > kWidthMax ? kWidthMax : v) : kWidthDefault; }

// A panel of the game that is the inventory: the pad layout's tabbed menu (its first page is the inventory; its other pages - the
// character, the skills, the quests - come with it) or the inventory panel itself.
inline bool IsInventoryPanel(const char* name) {
    if (!name) return false;
    return std::strcmp(name, "UISwitcher") == 0 || std::strncmp(name, "PlayerInventory", 15) == 0;
}

// Why it is not in force (None: it is). Kept apart for the log.
enum class Off { None, Key, NotF4, NoPanel, NoLaser, NoGame, AreaUnknown, Town, OtherMenu, Chat, Count };

struct Facts {
    bool keyOn = false;      // [openxr] hand_inventory
    bool f4 = false;         // viewgate::F4() and native OpenXR running
    bool panel = false;      // a menu goes out as a panel of its own (vrcam MenuAs3d: [openxr] menu_3d, the interface layer found)
    bool laser = false;      // [openxr] laser: items are picked by pointing (without it the left stick is the inventory's only cursor)
    bool inWorld = false;    // in a game area (not the title, not the character screens)
    int level = 0;           // the hero's area (Levels.txt id), 0 = not known
    bool menu = false;       // a menu is open (gamestate MenuOpen)
    bool inventoryOnly = false;   // ... and every menu open is the inventory (IsInventoryPanel)
    bool chat = false;       // the chat line is open
};

// Away from town, in F4, with panels: the long press of the left stick click is not refused for its lean, and an inventory opened goes to the hand.
inline Off Away(const Facts& f) {
    if (!f.keyOn) return Off::Key;
    if (!f.f4) return Off::NotF4;
    if (!f.panel) return Off::NoPanel;
    if (!f.laser) return Off::NoLaser;
    if (!f.inWorld) return Off::NoGame;
    if (f.level <= 0) return Off::AreaUnknown;
    if (clientverdict::IsTown(f.level)) return Off::Town;
    return Off::None;
}

// In force now: away from town and the inventory open, nothing else - a waypoint, the game menu, an NPC's speech stay in front, as before.
inline Off InForce(const Facts& f) {
    const Off a = Away(f);
    if (a != Off::None) return a;
    if (!f.menu || !f.inventoryOnly) return Off::OtherMenu;
    if (f.chat) return Off::Chat;
    return Off::None;
}

inline const char* OffText(Off o) {
    switch (o) {
    case Off::None: return "in force";
    case Off::Key: return "off ([openxr] hand_inventory=0)";
    case Off::NotF4: return "not VR first person (F4) with native OpenXR";
    case Off::NoPanel: return "menus are not a panel of their own ([openxr] menu_3d=0, or the interface's layer not found yet)";
    case Off::NoLaser: return "the laser is off ([openxr] laser=0): the left stick is the inventory's cursor, not the walk";
    case Off::NoGame: return "no game area (the title or the character screens)";
    case Off::AreaUnknown: return "the hero's area is not known yet";
    case Off::Town: return "in town - the inventory stays in front";
    case Off::OtherMenu: return "no inventory open, or another menu with it - in front, as before";
    case Off::Chat: return "the chat line is open";
    default: return "?";
    }
}

// -- where it hangs -----------------------------------------------------------------------------------------------------------------
// OpenXR's axes (x right, y up, z back); one space for the hand and the eyes (xr.cpp: g_space, where the laser casts too).
struct V3 { float x = 0.0f, y = 0.0f, z = 0.0f; };
inline V3 Add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 Sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 Mul(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Len(V3 a) { return std::sqrt(Dot(a, a)); }

struct Place {
    bool ok = false;
    V3 center, right, up, normal;   // the quad's middle and its axes: normal = its front, toward the eyes (the quad's +z)
    float w = 0.0f, h = 0.0f;       // metres
};

// The panel held in the hand like a tablet by its lower edge: its middle over the controller, its lower edge kLiftM above it, upright (its
// up as near the room's as its turn to the eyes lets it be) and turned to the eyes. aspect = height / width of the picture. Not ok: the
// eyes on the hand (under 10 cm), or a pose that is not a number.
inline Place OnHand(V3 hand, V3 eyes, float widthM, float aspect) {
    Place p;
    if (!std::isfinite(hand.x + hand.y + hand.z + eyes.x + eyes.y + eyes.z) || !(aspect > 0.0f) || !std::isfinite(aspect)) return p;
    p.w = Width(widthM);
    p.h = p.w * aspect;
    V3 at = hand;
    for (int pass = 0; pass < 8; ++pass) {   // turned to the eyes from the hand, then again from the middle it gave, until it settles
        V3 n = Sub(eyes, at);
        const float d = Len(n);
        if (!(d > 0.10f)) return Place{};
        n = Mul(n, 1.0f / d);
        V3 up = Sub(V3{0.0f, 1.0f, 0.0f}, Mul(n, n.y));   // the room's up, across the line of sight
        if (Len(up) < 1e-3f) return Place{};               // looked at from straight above or below: no up
        up = Mul(up, 1.0f / Len(up));
        p.normal = n;
        p.up = up;
        p.right = Cross(up, n);
        at = Add(hand, Mul(up, 0.5f * p.h + kLiftM));
    }
    p.center = at;
    p.ok = true;
    return p;
}

// The place as a quaternion (x y z w) of the axes right, up, normal.
inline void Orientation(const Place& p, float q[4]) {
    const V3 x = p.right, y = p.up, z = p.normal;
    const float tr = x.x + y.y + z.z;
    if (tr > 0.0f) { const float k = std::sqrt(tr + 1.0f) * 2.0f; q[0] = (y.z - z.y) / k; q[1] = (z.x - x.z) / k; q[2] = (x.y - y.x) / k; q[3] = 0.25f * k; }
    else if (x.x > y.y && x.x > z.z) { const float k = std::sqrt(1.0f + x.x - y.y - z.z) * 2.0f; q[0] = 0.25f * k; q[1] = (y.x + x.y) / k; q[2] = (z.x + x.z) / k; q[3] = (y.z - z.y) / k; }
    else if (y.y > z.z) { const float k = std::sqrt(1.0f + y.y - x.x - z.z) * 2.0f; q[0] = (y.x + x.y) / k; q[1] = 0.25f * k; q[2] = (z.y + y.z) / k; q[3] = (z.x - x.z) / k; }
    else { const float k = std::sqrt(1.0f + z.z - x.x - y.y) * 2.0f; q[0] = (z.x + x.z) / k; q[1] = (z.y + y.z) / k; q[2] = 0.25f * k; q[3] = (x.y - y.x) / k; }
}

// -- the inventory window cut out of the interface -------------------------------------------------------------------------------------
// Serg (2026-10-28): on the hand the game's WHOLE inventory window, nothing else of the interface - the way the Evil Islands VR mod holds the
// game's HUD blocks on its left hand (ei_vr game/vr/hand_hud.gd): the block is found in the picture itself, a coarse grid of it a second,
// cells drawn joined into patches; the biggest is the inventory window (the tabbed menu's page, or the inventory panel). It grows at once, keeps
// a larger size kShrinkS (a tooltip that reached past it) and is let go kGoneS after it is no longer found. The laser's hit on it is mapped
// back to the block's place in the window - the game's own mouse there. Nothing of the game's layout is touched.
constexpr int kGridW = 96;                // cells across the picture (EI: 96)
constexpr float kDrawnTransmittance = 0.15f;  // the interface layer keeps transmittance in alpha (1 = nothing): under this a pixel is drawn - the
                                              // panels' opaque art, not the tabbed menu's backdrop over the whole screen (ControllerOverlayHD.json
                                              // TransparentBG: black at 0.7, transmittance 0.3 - at 0.4 the cut was the whole picture, Serg's log
                                              // 2026-10-28 14:06:37)
constexpr float kFindEveryS = 1.0f, kShrinkS = 8.0f, kGoneS = 2.0f;

struct Box {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // picture pixels, x1 / y1 past the last
    bool ok() const { return x1 > x0 && y1 > y0; }
    int w() const { return x1 - x0; }
    int h() const { return y1 - y0; }
    bool Has(float x, float y) const { return x >= (float)x0 && x < (float)x1 && y >= (float)y0 && y < (float)y1; }
    bool Encloses(const Box& b) const { return b.x0 >= x0 && b.y0 >= y0 && b.x1 <= x1 && b.y1 <= y1; }
    bool operator==(const Box& b) const { return x0 == b.x0 && y0 == b.y0 && x1 == b.x1 && y1 == b.y1; }
};
inline Box Merge(const Box& a, const Box& b) {
    if (!a.ok()) return b;
    if (!b.ok()) return a;
    return Box{a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1};
}

// The grid's rows for a picture of w x h: kGridW across, as many square cells down (16 - 192).
inline int GridH(uint32_t w, uint32_t h) {
    if (!w || !h) return 0;
    const int g = (int)std::lround((double)kGridW * h / w);
    return g < 16 ? 16 : g > 192 ? 192 : g;
}
// The picture's row / column a cell is sampled at (its middle).
inline uint32_t SampleAt(int cell, int cells, uint32_t size) { return (uint32_t)(((double)cell + 0.5) * size / cells); }

// The biggest patch of drawn cells (8-connected; at least 3 cells), as picture pixels grown by a quarter cell and held to the picture.
// drawn: gw * gh, row by row. Not ok: nothing drawn.
inline Box Biggest(const uint8_t* drawn, int gw, int gh, uint32_t w, uint32_t h) {
    Box best;
    if (!drawn || gw <= 0 || gh <= 0 || !w || !h) return best;
    std::vector<uint8_t> seen((size_t)gw * gh, 0);
    std::vector<int> stack;
    int bestCount = 0;
    for (int start = 0; start < gw * gh; ++start) {
        if (!drawn[start] || seen[start]) continue;
        stack.assign(1, start);
        seen[start] = 1;
        int x0 = gw, y0 = gh, x1 = -1, y1 = -1, count = 0;
        while (!stack.empty()) {
            const int c = stack.back();
            stack.pop_back();
            const int cx = c % gw, cy = c / gw;
            ++count;
            if (cx < x0) x0 = cx;
            if (cy < y0) y0 = cy;
            if (cx > x1) x1 = cx;
            if (cy > y1) y1 = cy;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = cx + dx, ny = cy + dy;
                    if (nx < 0 || ny < 0 || nx >= gw || ny >= gh) continue;
                    const int n = ny * gw + nx;
                    if (drawn[n] && !seen[n]) { seen[n] = 1; stack.push_back(n); }
                }
        }
        if (count < 3 || count <= bestCount) continue;
        bestCount = count;
        const double cw = (double)w / gw, ch = (double)h / gh;
        auto clampTo = [](double v, uint32_t hi) { return (int)(v < 0.0 ? 0.0 : v > hi ? hi : v); };
        best = Box{clampTo((x0 - 0.25) * cw, w), clampTo((y0 - 0.25) * ch, h), clampTo((x1 + 1.25) * cw, w), clampTo((y1 + 1.25) * ch, h)};
    }
    return best;
}

// The block kept steady as the picture changes (EI's _steady, for one block): bigger at once, smaller only after kShrinkS of being smaller,
// gone after kGoneS of not being found. Step once a search (dt its seconds).
class Steady {
public:
    Box Step(const Box& found, float dt) {
        if (!found.ok()) {
            missing_ += dt;
            if (missing_ >= kGoneS) { box_ = Box{}; smaller_ = 0.0f; }
            return box_;
        }
        missing_ = 0.0f;
        if (!box_.ok()) { box_ = found; smaller_ = 0.0f; }
        else if (box_.Encloses(found)) {
            if (box_ == found) smaller_ = 0.0f;
            else if ((smaller_ += dt) >= kShrinkS) { box_ = found; smaller_ = 0.0f; }
        } else {
            box_ = Merge(box_, found);   // grew (or moved): at once
            smaller_ = 0.0f;
        }
        return box_;
    }
    Box Now() const { return box_; }
    void Reset() { *this = Steady{}; }
private:
    Box box_;
    float smaller_ = 0.0f, missing_ = 0.0f;
};

// A point of the quad that shows the box (u right, v down from its top left, 0..1 on it - and past its edges for a drag carried off it) as the
// picture's fraction (0..1, held inside the picture): the box's own place in the window, and the window round it beyond its edges.
inline void ToPicture(const Box& b, float u, float v, uint32_t w, uint32_t h, float* pu, float* pv) {
    const float x = (float)b.x0 + u * (float)b.w(), y = (float)b.y0 + v * (float)b.h();
    const float last = 0.9999f;
    *pu = w ? std::fmin(std::fmax(x / (float)w, 0.0f), last) : 0.0f;
    *pv = h ? std::fmin(std::fmax(y / (float)h, 0.0f), last) : 0.0f;
}

// The interface layer's alpha (R16G16B16A16_FLOAT: a half float at byte 6 of each pixel) as a float.
inline float HalfToFloat(uint16_t v) {
    const uint32_t sign = (v >> 15) & 1u, exp = (v >> 10) & 0x1Fu, man = v & 0x3FFu;
    float f;
    if (exp == 0) f = std::ldexp((float)man, -24);
    else if (exp == 31) f = man ? NAN : INFINITY;
    else f = std::ldexp((float)(man | 0x400u), (int)exp - 25);
    return sign ? -f : f;
}
inline bool Drawn(uint16_t alphaHalf) {
    const float t = HalfToFloat(alphaHalf);
    return std::isfinite(t) && t < kDrawnTransmittance;
}

// The laser let go off the inventory (a drag carried off it: the game's own drop, if an item is on its cursor)? On the hand the quad IS the
// inventory's window - off the quad is off it, whatever the cut found; in front, off the picture or on it outside the inventory's window. On
// our button: never.
inline bool ReleasedOff(bool onHand, bool onQuad, bool onButton, bool inInventoryWindow) {
    if (onButton) return false;
    if (!onQuad) return true;
    return !onHand && !inInventoryWindow;
}

// -- the button beside the panel: the inventory to the front and back to the hand -------------------------------------------------------
// Serg (2026-10-28): "a small button on it: one press puts the inventory full screen, the next brings it back onto the left hand". Ours,
// drawn beside the panel's top right corner, pressed with the laser. Its side: a share of the panel's width, held to 3.5 - 12 cm.
inline float ButtonSide(float panelW) {
    const float s = 0.09f * panelW;
    return s < 0.035f ? 0.035f : s > 0.12f ? 0.12f : s;
}
// Its middle, in the panel's own frame (x right, y up, from the panel's middle): just right of the top right corner.
inline void ButtonAt(float panelW, float panelH, float* x, float* y) {
    const float s = ButtonSide(panelW);
    *x = 0.5f * panelW + 0.6f * s;
    *y = 0.5f * panelH - 0.5f * s;
}

// -- the walk with the inventory up: the stick's own command, sent by us --------------------------------------------------------------
// Serg's test (2026-10-28 14:06-14:07): with the tabbed menu up the game's walk never read the stick - its gate 0x13DE60 (the control lock
// the controller's inventory holds) kept it from the walk every frame; the game got the stick at 0 0 (it never moved the inventory's
// cursor either). The game itself does not walk the hero with its controller inventory open; with its mouse-and-keyboard inventory it does
// (the left button held on the ground). The closest honest way: the very command the stick's walk sends - 0xFEFE0, "move in this
// direction" (docs/move_recon.md section 4: -> 0x3B3010 -> the command funnel 0xFABE0 with 0x42), as [gameplay] move_while_attacking
// already sends it - with the game's own direction, strength, Run toggle, stamina and forced walk (move_attack.h MakeWalk), once a game
// frame, on the game's UI thread; let go, the stick's own stop (the map click of type 6 at the hero's own cell). No server call.
constexpr uint32_t kWalkEveryMs = 40;   // the game's frame (25 a second): the stick's walk is sent once a frame (0x14AA10)
constexpr uint32_t kGameWalksMs = 200;  // the game's own walk read the stick this lately: it walks, ours stands aside

// The stick as the game's walk takes it (x right, y up on its screen, length <= 1) on the game's grid: ControllerManager's (x, -y) turned
// by -45 degrees (docs/move_recon.md section 3, 0x14C6EA: the record's +0x195C / +0x1960).
inline void ScreenToGrid(float x, float y, float* gx, float* gy) {
    constexpr float k = 0.70710678f;
    const float a = x, b = -y;
    *gx = k * (a + b);
    *gy = k * (b - a);
}

// What to send this game frame. pushed: the stick out of the game's dead zone; gameWalks: the game's walk read it lately; walking: ours
// sent a walk since the last stop.
enum class Send { Nothing, Walk, Stop };
inline Send WalkStep(bool inForce, bool pushed, bool gameWalks, bool walking) {
    if (inForce && pushed && !gameWalks) return Send::Walk;
    if (walking && !pushed) return Send::Stop;   // let go (or the inventory closed with it at rest): the stick's own stop
    return Send::Nothing;                        // (closed with it pushed: the game's own walk goes on from here)
}

// -- the walk while it hangs: the log's evidence ----------------------------------------------------------------------------------------
// vrcam counts, while the panel is on the hand, how often the game's walk read its stick (HookStickGet at the walk's call) and how far the
// hero went (the look-at point, in cells of the game's grid); once a second of a stick held it says which of the three it was. Pure, so the verdict can be checked offline.
enum class Walk { NotAsked, Walks, ReadNoStep, NotRead, OursWalks, OursNoStep };
inline Walk Verdict(bool stickHeld, unsigned walkReads, float movedCells, unsigned ourSends = 0) {
    if (!stickHeld) return Walk::NotAsked;
    const bool moved = movedCells > 1.0f;   // (a cell of the game's grid: 2 world units)
    if (walkReads == 0) return ourSends ? (moved ? Walk::OursWalks : Walk::OursNoStep) : Walk::NotRead;   // the game's gate (0x13DE60) kept its walk from the stick
    return moved ? Walk::Walks : Walk::ReadNoStep;
}
inline const char* WalkText(Walk w) {
    switch (w) {
    case Walk::Walks: return "the hero WALKS with the inventory on the hand (the game's own walk took the stick)";
    case Walk::ReadNoStep: return "the game's walk read the stick, but the hero did not move (the game holds him while its inventory is up)";
    case Walk::NotRead: return "the game's walk did not read the stick - the game's own gate (its control lock while the inventory is up) holds the hero";
    case Walk::OursWalks: return "the hero WALKS with the inventory up: the game's walk is locked, the stick's own walk command (0xFEFE0) sent for it";
    case Walk::OursNoStep: return "the stick's own walk command (0xFEFE0) sent, but the hero did not move";
    default: return "the stick at rest";
    }
}

}  // namespace handinv
