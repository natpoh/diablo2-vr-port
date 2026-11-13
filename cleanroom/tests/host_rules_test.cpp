// host_rules_test.cpp - the pure parts of [hud] game_menu_settings: the host's rules (vr/host_rules.h: the table, the ini's reading, the hello
// and the rules message, the role, the list's lines) and the flat panel (vr/flat_menu.h: its rows, steps, ini texts, layout, hit test, the list's
// showing). No game, no window.
//   host_rules_test.exe
#include "flat_menu.h"
#include "host_rules.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

using namespace hostrules;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static Values All(int a, int b, int c, int d) {
    Values v;
    const int x[4] = {a, b, c, d};
    for (int i = 0; i < 4; ++i) { v.v[i] = x[i]; v.known[i] = true; }
    for (int i = 4; i < kCount; ++i) { v.v[i] = i % 2; v.known[i] = true; }   // the later rules (quick_blow, quick_blow_faster): on / off by turns
    return v;
}

static void Table() {
    // the Mechanics tab's "who hosts decides" keys, each once, ids unique and never 0, a default within its range
    CHECK(kCount == 9);   // ... and quick_blow, quick_blow_faster (2026-11-05), shot_at_release (2026-11-06, id 8), share_hit_outcomes (2026-11-08, id 7), move_during_skills (2026-11-10, id 9)
    std::set<int> ids;
    std::set<std::wstring> keys;
    for (int i = 0; i < kCount; ++i) {
        CHECK(kRules[i].id != 0);
        CHECK(ids.insert(kRules[i].id).second);
        CHECK(keys.insert(std::wstring(kRules[i].section) + L"/" + kRules[i].key).second);
        CHECK(kRules[i].count >= 2 && kRules[i].count <= 3);
        CHECK(kRules[i].fallback >= 0 && kRules[i].fallback < kRules[i].count);
        for (int v = 0; v < kRules[i].count; ++v) CHECK(kRules[i].names[v] && *kRules[i].names[v]);
        CHECK(IndexOf(kRules[i].id) == i);
        CHECK(std::strcmp(Label(i), "?") != 0);
    }
    CHECK(IndexOf(0) == -1 && IndexOf(200) == -1);
    CHECK(keys.count(L"gameplay/flat_attack_path") && keys.count(L"gameplay/melee_direct_client_cleave") &&
          keys.count(L"gameplay/move_while_attacking") && keys.count(L"gameplay/flat_move_while_attacking"));
    // the defaults as vrcam's LoadSettings has them (a missing key)
    CHECK(Parse(IndexOf(1), L"") == 1);   // flat_attack_path 1 (walk; the default since 0.216)
    CHECK(Parse(IndexOf(3), L"") == 1);   // move_while_attacking true
    CHECK(Parse(IndexOf(4), L"") == 0);   // melee_direct_client_cleave false
    CHECK(keys.count(L"gameplay/move_during_skills") && Parse(IndexOf(9), L"") == 1 && Parse(IndexOf(9), L"2") == 2 && Parse(IndexOf(9), L"7") == 2);   // move_during_skills: walk by default (0.216), 2 = walk or run
    CHECK(std::string(ValueName(IndexOf(9), 2)) == "Walk or run" && std::string(ValueName(IndexOf(9), 0)) == "Off");
}

static void Parsing() {
    const int path = IndexOf(1), mwa = IndexOf(2), walk = IndexOf(3);
    CHECK(Parse(path, L"2") == 2);
    CHECK(Parse(path, L"2.9") == 2);      // (int)IniF
    CHECK(Parse(path, L"5") == 2);        // clamped: walk or run
    CHECK(Parse(path, L"-1") == 0);
    CHECK(Parse(path, L"abc") == 1);      // not a number: the default (walk since 0.216)
    CHECK(Parse(mwa, L"1") == 1);
    CHECK(Parse(walk, L"0") == 0);
    CHECK(Parse(walk, L"0.5") == 1);      // IniB: any number but 0
    CHECK(Parse(walk, L"x") == 1);        // the default (true)
    CHECK(Parse(-1, L"1") == 0 && Parse(99, L"1") == 0);
    CHECK(Clamp(path, 7) == 2 && Clamp(path, -3) == 0 && Clamp(walk, 1) == 1);
}

static void Wire() {
    uint8_t b[kMaxPayload];
    // hello
    CHECK(BuildHello(0x12345678u, b, sizeof b) == kHelloBytes);
    uint32_t n = 0;
    CHECK(ParseHello(b, kHelloBytes, &n) && n == 0x12345678u);
    CHECK(b[0] == kVersion && b[1] == 0x78 && b[4] == 0x12);   // little endian
    CHECK(!ParseHello(b, kHelloBytes - 1, &n));
    CHECK(BuildHello(0, b, sizeof b) == 0);                     // no number: nothing sent
    CHECK(BuildHello(5, b, 4) == 0);                            // too small a buffer
    b[0] = kVersion + 1;
    CHECK(!ParseHello(b, kHelloBytes, &n));                     // another version
    // rules: every rule, its values, the asker's and the host's numbers
    const Values v = All(2, 1, 0, 1);
    const uint32_t size = BuildRules(77, 99, v, b, sizeof b);
    CHECK(size == kRulesHead + 2 * kCount);
    uint32_t asker = 0, host = 0;
    Values got;
    CHECK(ParseRules(b, size, &asker, &host, &got));
    CHECK(asker == 77 && host == 99);
    for (int i = 0; i < kCount; ++i) CHECK(got.known[i] && got.v[i] == v.v[i]);
    CHECK(!ParseRules(b, size - 1, &asker, &host, &got));      // cut short
    CHECK(!ParseRules(b, size + 1, &asker, &host, &got));      // a byte too many
    CHECK(BuildRules(77, 0, v, b, sizeof b) == 0);              // no host number
    CHECK(BuildRules(77, 99, v, b, kRulesHead + 1) == 0);       // too small
    // a value out of range is clamped on both sides
    Values wild = All(9, 9, 9, 9);
    const uint32_t s2 = BuildRules(1, 2, wild, b, sizeof b);
    CHECK(ParseRules(b, s2, &asker, &host, &got) && got.v[0] == 2 && got.v[2] == 1);
    b[kRulesHead + 1] = 200;   // the first rule's value forged
    CHECK(ParseRules(b, s2, &asker, &host, &got) && got.v[IndexOf(b[kRulesHead])] == kRules[IndexOf(b[kRulesHead])].count - 1);
    // an unknown id (a newer host's rule) is skipped, the rest read
    Values part;
    part.v[0] = 1; part.known[0] = true;
    uint32_t s3 = BuildRules(1, 2, part, b, sizeof b);
    CHECK(s3 == kRulesHead + 2);
    b[kRulesHead] = 250;   // an id this client does not know
    CHECK(ParseRules(b, s3, &asker, &host, &got));
    for (int i = 0; i < kCount; ++i) CHECK(!got.known[i]);
    // a count that does not match the size, more than kMaxRules
    s3 = BuildRules(1, 2, v, b, sizeof b);
    b[9] = static_cast<uint8_t>(kMaxRules + 1);
    CHECK(!ParseRules(b, s3, &asker, &host, &got));
    // the whole message fits D2RLoader's payload even with every rule a message may hold
    CHECK(kRulesHead + 2 * kMaxRules <= kMaxPayload);
}

static void Roles() {
    CHECK(RoleOf(false, 5, 5) == Role::Unknown);   // no fresh answer
    CHECK(RoleOf(true, 5, 5) == Role::Own);        // our own number came back: single player or we host
    CHECK(RoleOf(true, 5, 6) == Role::Joined);
    CHECK(RoleOf(true, 0, 6) == Role::Unknown);
    CHECK(Fresh(1000, 900) && !Fresh(10000, 900) && !Fresh(100, 900) && !Fresh(1000, 0));
    CHECK(RetryMs(0, false) == 3000 && RetryMs(1, false) == 10000 && RetryMs(2, false) == 30000 && RetryMs(3, false) == 60000 && RetryMs(50, false) == 60000);
    CHECK(RetryMs(0, true) == 10000);
}

static void Lines() {
    const Values own = All(1, 0, 1, 0), host = All(2, 2, 0, 1);
    auto list = List(Role::Joined, own, host);
    CHECK(list.size() == static_cast<size_t>(kCount + 2));
    CHECK(list[0].title && !list[1].title);
    CHECK(list[2].host);
    CHECK(list[2].text.find("Walk or run") != std::string::npos);   // the host's flat_attack_path 2, not our 1
    CHECK(list[5].text.find(": On") != std::string::npos);          // the host's cleave
    list = List(Role::Own, own, host);
    CHECK(!list[2].host && list[2].text.find(": Walk") != std::string::npos && list[2].text.find("run") == std::string::npos);
    Values none;
    list = List(Role::Joined, own, none);
    CHECK(list[2].text.find(": ?") != std::string::npos);           // a rule the host did not send
    CHECK(std::string(RoleLine(Role::Joined)) != RoleLine(Role::Own) && std::string(RoleLine(Role::Own)) != RoleLine(Role::Unknown));
    CHECK(std::string(ValueName(IndexOf(2), 1)) == "Spear and bow" && std::string(ValueName(IndexOf(4), 0)) == "Off");
}

static void Panel() {
    using namespace flatmenu;
    CHECK(kRows == kOwnCount + kCount);
    std::set<std::wstring> keys;
    for (int r = 0; r < kRows; ++r) {
        const RowDef d = Def(r);
        CHECK(keys.insert(std::wstring(d.section) + L"/" + d.key).second);   // no key twice
        CHECK(d.lo <= d.fallback && d.fallback <= d.hi);
        CHECK(std::strcmp(flatmenu::Label(r), "?") != 0);
    }
    CHECK(keys.count(L"body/flat_weapon_adjust") && keys.count(L"camera/fov"));
    CHECK(IsRule(kOwnCount) && !IsRule(kOwnCount - 1) && RuleOf(kOwnCount) == 0 && RuleOf(0) == -1);
    CHECK(Def(kOwnCount).kind == Kind::Choice && Def(kOwnCount + 2).kind == Kind::Toggle);   // flat_attack_path 0-2, move_while_attacking a box
    // steps
    const RowDef fov = kOwn[0];
    CHECK(Step(fov, 60, 1) == 65 && Step(fov, 60, -1) == 55);
    CHECK(Step(fov, 62, 1) == 65 && Step(fov, 62, -1) == 60);   // a hand-typed value comes onto the grid
    CHECK(Step(fov, 120, 1) == 120 && Step(fov, 20, -1) == 20);
    const RowDef dist = kOwn[1];
    CHECK(Step(dist, 7, 1) == 7.5f && Step(dist, 0, -1) == 0);
    const RowDef box = kOwn[2];
    CHECK(Step(box, 1, 1) == 0 && Step(box, 0, -1) == 1);
    const RowDef path = Def(kOwnCount);
    CHECK(Step(path, 0, 1) == 1 && Step(path, 2, 1) == 2 && Step(path, 1, -1) == 0);
    // the ini's text
    CHECK(IniText(65) == L"65" && IniText(7.5f) == L"7.5" && IniText(0.25f) == L"0.25" && IniText(0) == L"0");
    CHECK(ParseValue(fov, L"75") == 75 && ParseValue(fov, L"") == 60 && ParseValue(fov, L"500") == 120 && ParseValue(fov, L"nan") == 60);
    CHECK(ParseValue(box, L"0.7") == 1 && ParseValue(path, L"2") == 2);
    CHECK(Shown(0, 65) == "65" && Shown(1, 7.5f) == "7.5" && Shown(2, 1) == "On" && Shown(kOwnCount, 2) == "Walk or run");
    CHECK(Def(10).kind == Kind::Choice && Shown(10, 0) == "As in the game's options" && Shown(10, 2) == "Centre" && Shown(10, 3) == "Right");   // [hud] flat_map_place
    // [input] flat_pickup_key: a key row - its name shown, one button (a click arms "press a key"), never stepped, its code kept through the ini text
    CHECK(Def(12).kind == Kind::Key && std::wcscmp(Def(12).key, L"flat_pickup_key") == 0 && Shown(12, 69) == "E" && Shown(12, 0x75) == "F6");
    // the map's own rows (2026-11-10): its turn, its compass's look, its keys - a map key refuses Tab, the pick-up key does not
    CHECK(std::wcscmp(Def(14).key, L"map_rotate_flat") == 0 && Def(14).kind == Kind::Toggle && Shown(14, 1) == "On");
    CHECK(Def(15).kind == Kind::Choice && Shown(15, 0) == "None" && Shown(15, 2) == "Thin: letters only" && Shown(15, 3) == "Ring of letters");
    CHECK(Def(16).kind == Kind::Key && Shown(16, 111) == "Num /" && Def(17).kind == Kind::Key && Shown(17, 106) == "Num *");
    CHECK(Def(18).kind == Kind::Choice && Shown(18, 0) == "Left" && Shown(18, 1) == "Right");
    CHECK(Shown(19, 187) == "=" && Shown(20, 189) == "-" && std::wcscmp(Def(20).key, L"flat_map_zoom_out_key") == 0);
    CHECK(KeyUse(16) == keycap::Use::MapKey && KeyUse(19) == keycap::Use::MapKey && KeyUse(12) == keycap::Use::PickupKey);
    CHECK(!HasSteps(12) && IsKey(12) && !IsKey(11) && Step(Def(12), 69, 1) == 69 && ParseValue(Def(12), L"71") == 71 && ParseValue(Def(12), L"") == 69);
    CHECK(Def(11).kind == Kind::Toggle && Def(13).kind == Kind::Toggle && Def(11).fallback == 0 && Def(13).fallback == 0);   // new: off
    // the layout at 1080p and 720p: inside the screen, rows in order, buttons inside their row
    for (const float h : {1080.0f, 720.0f, 2160.0f}) {
        const float w = h * 16.0f / 9.0f;
        const Layout L = Lay(w, h);
        CHECK(L.panel.x >= 0 && L.panel.y >= 0 && L.panel.x + L.panel.w <= w && L.panel.y + L.panel.h <= h);
        for (int r = 0; r < kRows; ++r) {
            CHECK(L.row[r].h > 0);
            if (r) CHECK(L.row[r].y > L.row[r - 1].y);
            CHECK(L.minus[r].x >= L.row[r].x && L.plus[r].x + L.plus[r].w <= L.row[r].x + L.row[r].w + 0.01f);
            CHECK(L.minus[r].x + L.minus[r].w <= L.value[r].x && L.value[r].x + L.value[r].w <= L.plus[r].x);
        }
        CHECK(L.close.y >= L.row[kRows - 1].y + L.row[kRows - 1].h && L.close.y + L.close.h <= L.panel.y + L.panel.h);
        const Rect b = MenuButton(w, h);
        CHECK(b.x > 0 && b.x + b.w < w && b.y > h * 0.5f && b.y + b.h < h);
    }
    // the hit test: our rows always, the host's rules only where we decide
    const Layout L = Lay(1920, 1080);
    auto mid = [](const Rect& r, float* x, float* y) { *x = r.x + r.w * 0.5f; *y = r.y + r.h * 0.5f; };
    float x, y;
    mid(L.value[2], &x, &y);
    Hit h = HitTest(L, x, y, Role::Joined);
    CHECK(h.part == Part::Value && h.row == 2);
    float next = 0;
    CHECK(Click(h, 1, &next) && next == 0);
    mid(L.plus[0], &x, &y);
    h = HitTest(L, x, y, Role::Own);
    CHECK(h.part == Part::Plus && h.row == 0);
    CHECK(Click(h, 60, &next) && next == 65);
    CHECK(!Click(h, 120, &next));                                     // the end of the range: nothing written
    mid(L.value[0], &x, &y);
    CHECK(HitTest(L, x, y, Role::Own).part == Part::Panel);          // a slider's value is no button
    mid(L.plus[kOwnCount], &x, &y);
    CHECK(HitTest(L, x, y, Role::Own).row == kOwnCount);             // as the host: ours to change
    CHECK(HitTest(L, x, y, Role::Unknown).row == kOwnCount);         // no host's answer: our own box (single player, a game not started)
    CHECK(HitTest(L, x, y, Role::Joined).row == -1);                 // joined: only shown
    mid(L.value[kOwnCount + 2], &x, &y);
    CHECK(HitTest(L, x, y, Role::Joined).row == -1 && HitTest(L, x, y, Role::Own).part == Part::Value);
    mid(L.close, &x, &y);
    CHECK(HitTest(L, x, y, Role::Joined).part == Part::Close);
    CHECK(HitTest(L, 2, 2, Role::Own).part == Part::None);
    // the client's pixel to the overlay's
    ToOverlay(960, 540, 1920, 1080, 3840, 2160, &x, &y);
    CHECK(x == 1920 && y == 1080);
    // the list key and when the list shows
    CHECK(ListKey(0) == 0 && ListKey(1) == 0x24 && ListKey(2) == 0x23 && ListKey(3) == 0x2D && ListKey(9) == 0);
    CHECK(!ListShown(false, true, true, true, false, Role::Joined, 0, 0));      // the key off
    CHECK(ListShown(true, true, true, false, false, Role::Own, 0, 0));          // the key held, flat
    CHECK(!ListShown(true, false, true, false, false, Role::Own, 0, 0));        // ... not in VR
    CHECK(!ListShown(true, true, true, true, true, Role::Joined, 0, 0));        // under the open panel: not twice
    CHECK(ListShown(true, false, false, true, false, Role::Joined, 0, 0));      // the pause menu, joined: VR too
    CHECK(!ListShown(true, true, false, true, false, Role::Own, 0, 0));         // ... our own game: only on the key
    CHECK(ListShown(true, true, false, false, false, Role::Joined, 5000, 1000));   // the first seconds after joining
    CHECK(!ListShown(true, true, false, false, false, Role::Joined, 1000 + kJoinShowMs, 1000));
}

// [hud] game_menu_key: the key, its messages (F10 a system key: down / up either way), the panel's staying open.
static void PanelKeys() {
    using namespace flatmenu;
    using Act = PanelKeyGate::Act;
    CHECK(PanelKey(0) == 0 && PanelKey(1) == 0x79 && PanelKey(2) == 0x2D && PanelKey(3) == 0x2E && PanelKey(4) == 0x21 && PanelKey(5) == 0x22);
    CHECK(PanelKey(6) == 0 && PanelKey(-1) == 0);
    for (int c = 1; c <= 5; ++c) {   // never one of ours already: F9 mouse look, F11 recentre, F12 next view
        CHECK(PanelKey(c) != 0x78 && PanelKey(c) != 0x7A && PanelKey(c) != 0x7B);
    }
    {   // a press: toggle, its repeats and its release swallowed
        PanelKeyGate g;
        CHECK(g.Feed(0x79, 0x79, true, false, false) == Act::Toggle);
        CHECK(g.Feed(0x79, 0x79, true, true, false) == Act::Swallow);
        CHECK(g.Feed(0x79, 0x79, true, true, true) == Act::Swallow);   // Ctrl pressed meanwhile: still our press
        CHECK(g.Feed(0x79, 0x79, false, false, false) == Act::Swallow);
        CHECK(!g.taken);
        CHECK(g.Feed(0x79, 0x79, true, false, false) == Act::Toggle);   // the next press toggles again
        CHECK(g.Feed(0x79, 0x79, false, false, false) == Act::Swallow);
    }
    {   // Ctrl + F10 (the frame trace): the game's / the plugin's own, its release too
        PanelKeyGate g;
        CHECK(g.Feed(0x79, 0x79, true, false, true) == Act::Pass);
        CHECK(g.Feed(0x79, 0x79, true, true, true) == Act::Pass);
        CHECK(g.Feed(0x79, 0x79, false, false, false) == Act::Pass);
    }
    {   // a repeat of a press we did not see go down (the key was down before the box went on): not ours
        PanelKeyGate g;
        CHECK(g.Feed(0x79, 0x79, true, true, false) == Act::Pass);
        CHECK(g.Feed(0x79, 0x79, false, false, false) == Act::Pass);
    }
    {   // other keys, the key off
        PanelKeyGate g;
        CHECK(g.Feed(0x7A, 0x79, true, false, false) == Act::Pass);
        CHECK(g.Feed(0x79, 0, true, false, false) == Act::Pass);
        CHECK(!g.taken);
    }
    // the panel stays: from the key, whatever the pause menu does; from the button, with the pause menu; never in VR
    CHECK(PanelStays(true, false, false) && PanelStays(true, false, true));
    CHECK(PanelStays(true, true, true) && !PanelStays(true, true, false));
    CHECK(!PanelStays(false, false, true) && !PanelStays(false, true, true));
}

int main() {
    Table();
    Parsing();
    Wire();
    Roles();
    Lines();
    Panel();
    PanelKeys();
    std::printf("host_rules_test: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
