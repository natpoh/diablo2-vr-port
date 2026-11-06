// Offline checks of vr/strike_stats.h ([stats] strike_stats): the blows, speeds and forces per hero and weapon, this session's and all
// time's, the missed swings, and the file's text (written, read back, a broken or foreign file). No game, no clock, no file.
// Run: build/Release/strike_stats_test.exe (exit code 0 = passed)

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "../../vr/strike_stats.h"
#include "../../vr/weapon_names.h"

using namespace strikestats;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static bool Near(double a, double b) { return std::fabs(a - b) < 1e-3; }

static const uint32_t kLsd = CodeOf("lsd"), kAxe = CodeOf("axe");

static const Weapon* W(const Hero* h, uint32_t code) {
    if (!h) return nullptr;
    for (const Weapon& w : h->weapons) if (w.code == code) return &w;
    return nullptr;
}

static void TestCodes() {
    // the game's padding: a space or a zero byte - one code
    const uint32_t zero = 'l' | 's' << 8 | 'd' << 16;
    CHECK(NormCode(zero) == kLsd);
    CHECK(NormCode(kLsd) == kLsd);
    CHECK(CodeText(kLsd) == "lsd");
    CHECK(CodeText(kBareHands) == "-");
    CHECK(CodeOf("-") == kBareHands);
    CHECK(CodeOf("*paw") == kPaws && CodeText(kPaws) == "*paw");
    CHECK(CodeOf(CodeText(CodeOf("9b9")).c_str()) == CodeOf("9b9"));
    // the names: a weapon of the mesh table, one of the extras, a bow (not a melee weapon here)
    CHECK(weaponnames::Name(kLsd) && !strcmp(weaponnames::Name(kLsd), "Long Sword"));
    CHECK(weaponnames::Name(CodeOf("fla")) && !strcmp(weaponnames::Name(CodeOf("fla")), "Flail"));
    CHECK(weaponnames::Name(CodeOf("7gd")) && !strcmp(weaponnames::Name(CodeOf("7gd")), "Colossus Blade"));
    CHECK(weaponnames::Name(CodeOf("lbw")) == nullptr);
    CHECK(weaponnames::Name(kBareHands) == nullptr);
}

static void TestNames() {
    CHECK(ValidName("Bar", 16));
    CHECK(ValidName("Sir_Hack-a-lot", 16));
    CHECK(!ValidName("B", 16));                  // too short
    CHECK(!ValidName("Abcdefghijklmnop", 17));   // 16 letters: too long
    CHECK(!ValidName("Bad name", 16));           // a space
    CHECK(!ValidName("\x01\x02garbage", 16));    // memory that is not a name
    const char unterminated[16] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P'};
    CHECK(!ValidName(unterminated, 16));         // no end within the field
}

static void TestBlows() {
    Store s;
    s.SetHero("Bar", 4);
    s.Blow(kLsd, 3, 2.0f, 1.5f);
    s.Blow(kLsd, 3, 4.0f, -1.0f);    // a swing the weight model measured no force for: speed counted, force not
    s.Blow(kAxe, 2, 3.0f, 2.5f);
    s.Blow(kAxe, 2, NAN, 0.0f);      // neither a speed nor a force: the blow alone
    const Hero* h = s.Current();
    CHECK(h && h->name == "Bar" && h->cls == 4);
    CHECK(h->session.blows == 4 && h->total.blows == 4);
    CHECK(h->session.speedN == 3 && Near(h->session.SpeedAvg(), 3.0) && Near(h->session.speedBest, 4.0));
    CHECK(h->session.forceN == 2 && Near(h->session.ForceAvg(), 2.0) && Near(h->session.forceBest, 2.5));
    const Weapon* l = W(h, kLsd);
    const Weapon* a = W(h, kAxe);
    CHECK(l && a && l->kind == 3 && a->kind == 2);
    CHECK(l->total.blows == 2 && Near(l->total.SpeedAvg(), 3.0) && Near(l->total.speedBest, 4.0) && l->total.forceN == 1 && Near(l->total.forceBest, 1.5));
    CHECK(a->total.blows == 2 && a->total.speedN == 1 && Near(a->total.ForceAvg(), 2.5));
    CHECK(s.Dirty());
    // absurd numbers (a broken reading) never become the best
    s.Blow(kLsd, 3, 1e9f, 1e9f);
    CHECK(Near(W(s.Current(), kLsd)->total.speedBest, 4.0) && Near(W(s.Current(), kLsd)->total.forceBest, 1.5));
    // misses: their own counts, no blow
    s.Miss(kLsd, 3, true);
    s.Miss(kLsd, 3, false);
    s.Miss(kLsd, 3, false);
    CHECK(W(s.Current(), kLsd)->total.missSlow == 1 && W(s.Current(), kLsd)->total.missOff == 2 && W(s.Current(), kLsd)->total.blows == 3);
    CHECK(s.Current()->session.Misses() == 3);
}

static void TestHeroesAndSessions() {
    Store s;
    s.Blow(kLsd, 3, 2.0f, 1.0f);   // no hero read yet: the unnamed hero
    CHECK(s.Current() && s.Current()->name.empty());
    s.SetHero("Sorc", 1);
    s.Blow(kBareHands, 12, 1.0f, 0.5f);
    s.SetHero("Bar", 4);
    s.Blow(kLsd, 3, 5.0f, 3.0f);
    s.SetHero("Sorc", -1);   // back to her: the class read before stays
    s.Blow(kBareHands, 12, 2.0f, 0.5f);
    CHECK(s.Heroes().size() == 3);
    CHECK(s.Current()->name == "Sorc" && s.Current()->cls == 1 && s.Current()->total.blows == 2);
    s.SetHero("not a name!", 2);   // a reading that is not a name: the unnamed hero
    CHECK(s.Current()->name.empty() && s.Heroes().size() == 3);
    // a new session: all time stays, the session starts from nothing
    s.StartSession();
    for (const Hero& h : s.Heroes()) CHECK(h.session.Empty() && !h.total.Empty());
    s.SetHero("Bar", 4);
    s.Blow(kLsd, 3, 3.0f, 2.0f);
    CHECK(s.Current()->session.blows == 1 && s.Current()->total.blows == 2);
    CHECK(W(s.Current(), kLsd)->session.blows == 1 && Near(W(s.Current(), kLsd)->total.speedBest, 5.0) && Near(W(s.Current(), kLsd)->session.speedBest, 3.0));
}

static void TestFile() {
    Store s;
    s.SetHero("Bar", 4);
    s.Blow(kLsd, 3, 2.25f, 1.5f);
    s.Blow(kLsd, 3, 3.75f, 2.0f);
    s.Miss(kLsd, 3, true);
    s.SetHero("Druid_X", 5);
    s.Blow(kPaws, 12, 4.0f, -1.0f);
    s.StartSession();   // (the next game)
    s.SetHero("Bar", 4);
    s.Blow(kAxe, 2, 1.5f, 0.75f);
    const std::string text = s.Serialize(1762400000, 1762403600);
    CHECK(text.find("version 1") != std::string::npos);
    CHECK(text.find("hero Bar 4") != std::string::npos && text.find("weapon lsd 3") != std::string::npos && text.find("weapon *paw 12") != std::string::npos);

    Store r;
    int64_t started = 0, updated = 0;
    CHECK(r.Parse(text, &started, &updated));
    CHECK(started == 1762400000 && updated == 1762403600);
    CHECK(r.Heroes().size() == 2);
    const Hero* bar = nullptr;
    const Hero* dru = nullptr;
    for (const Hero& h : r.Heroes()) (h.name == "Bar" ? bar : dru) = &h;
    CHECK(bar && dru && dru->name == "Druid_X" && dru->cls == 5 && bar->cls == 4);
    CHECK(bar->total.blows == 3 && bar->session.blows == 1 && bar->total.missSlow == 1);
    const Weapon* l = W(bar, kLsd);
    CHECK(l && l->kind == 3 && l->total.blows == 2 && l->session.Empty() && Near(l->total.SpeedAvg(), 3.0) && Near(l->total.speedBest, 3.75) && Near(l->total.ForceAvg(), 1.75));
    CHECK(W(bar, kAxe) && W(bar, kAxe)->session.blows == 1 && Near(W(bar, kAxe)->session.forceBest, 0.75));
    CHECK(W(dru, kPaws) && W(dru, kPaws)->total.blows == 1 && W(dru, kPaws)->total.forceN == 0);
    CHECK(!r.Dirty());
    // written again, it is the same text
    CHECK(r.Serialize(1762400000, 1762403600) == text);
    // the plugin's next game: read, then a new session - all time counts on
    r.StartSession();
    r.SetHero("Bar", 4);
    r.Blow(kLsd, 3, 6.0f, 4.0f);
    CHECK(W(r.Current(), kLsd)->total.blows == 3 && Near(W(r.Current(), kLsd)->total.speedBest, 6.0) && W(r.Current(), kLsd)->session.blows == 1);
    // CRLF (a player opened and saved it in Notepad) reads the same
    std::string crlf;
    for (char c : text) { if (c == '\n') crlf += '\r'; crlf += c; }
    Store c;
    CHECK(c.Parse(crlf) && c.Heroes().size() == 2 && c.Serialize(1762400000, 1762403600) == text);
}

static void TestBrokenFiles() {
    Store s;
    CHECK(!s.Parse(""));                                   // empty (a crash before the first write)
    CHECK(!s.Parse("hero Bar 4\nall 1 2 1 2 0 0 0 0 0\n"));   // no version first: not ours
    CHECK(!s.Parse("version 0\n"));
    CHECK(!s.Parse(std::string(64, '\0')));               // a zeroed file (a power cut)
    CHECK(s.Heroes().empty());
    // a newer file: unknown records skipped, the rest read
    CHECK(s.Parse("version 1\nsession 1 2\nhero Bar 4 7\nall 2 4.0 2 3.0 0 0 0 0 0\nmedals 3\nweapon lsd 3 7\nall 2 4.0 2 3.0 0 0 0 0 0 extra 9\n"));
    CHECK(s.Heroes().size() == 1 && s.Heroes()[0].total.blows == 2 && W(&s.Heroes()[0], kLsd) && W(&s.Heroes()[0], kLsd)->total.blows == 2);
    // a cut line (written halfway) or negative sums: that record skipped, not the file
    CHECK(s.Parse("version 1\nhero Bar 4 1\nall 2 4.0 2\nweapon axe 2 1\nall 1 -5 1 1 0 0 0 0 0\n"));
    CHECK(s.Heroes().size() == 1 && s.Heroes()[0].total.blows == 0 && W(&s.Heroes()[0], kAxe) && W(&s.Heroes()[0], kAxe)->total.blows == 0);
    // a name in the file that is not a name: the unnamed hero
    CHECK(s.Parse("version 1\nhero <script> 1 1\nall 1 1 1 1 0 0 0 0 0\n") && s.Heroes().size() == 1 && s.Heroes()[0].name.empty());
}

static void TestLimits() {
    Store s;
    char name[16];
    for (int i = 0; i < static_cast<int>(kMaxHeroes) + 5; ++i) {
        snprintf(name, sizeof name, "Hero%c%c", 'a' + i / 26, 'a' + i % 26);
        s.SetHero(name, 0);
        s.Blow(kLsd, 3, 2.0f, 1.0f);
    }
    CHECK(s.Heroes().size() == kMaxHeroes);
    CHECK(s.Current() && s.Current()->name == name);   // the current hero is kept
    bool firstGone = true;
    for (const Hero& h : s.Heroes()) firstGone = firstGone && h.name != "Heroaa";
    CHECK(firstGone);   // the one used longest ago went
    Store w;
    w.SetHero("Bar", 4);
    for (uint32_t i = 0; i < kMaxWeapons + 3; ++i) w.Blow(CodeOf("w00") + (i % 10) * 0x100 + (i / 10) * 0x10000, 1, 2.0f, 1.0f);
    CHECK(w.Current()->weapons.size() == kMaxWeapons && w.Current()->total.blows == kMaxWeapons + 3);
}

static void TestMisses() {
    MissTracker t;
    MissTracker::Miss m;
    // a stroke refused (too slow), then landed on the next pass: a blow, no miss
    CHECK(!t.Refused(1, kLsd, 3, true, 1000, &m));
    CHECK(!t.Landed(1, &m));
    CHECK(!t.Flush(5000, &m));
    // refused after its blow (the other monster): still no miss
    CHECK(!t.Refused(1, kLsd, 3, false, 1100, &m));
    CHECK(!t.Flush(5000, &m));
    // a stroke refused on many passes: one miss, its first reason, settled by time
    CHECK(!t.Refused(2, kLsd, 3, true, 2000, &m));
    CHECK(!t.Refused(2, kLsd, 3, false, 2040, &m));
    CHECK(!t.Flush(2000 + MissTracker::kMissMs - 1, &m));
    CHECK(t.Flush(2000 + MissTracker::kMissMs, &m) && m.slow && m.code == kLsd);
    CHECK(!t.Flush(9000, &m));   // once
    // settled by the next stroke's refusal, and by the next stroke's blow
    CHECK(!t.Refused(3, kAxe, 2, false, 10000, &m));
    CHECK(t.Refused(4, kAxe, 2, true, 10300, &m) && !m.slow && m.code == kAxe);
    CHECK(t.Landed(5, &m) && m.slow);
    CHECK(!t.Flush(20000, &m));
}

int main() {
    TestCodes();
    TestNames();
    TestBlows();
    TestHeroesAndSessions();
    TestFile();
    TestBrokenFiles();
    TestLimits();
    TestMisses();
    std::printf("strike_stats_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
