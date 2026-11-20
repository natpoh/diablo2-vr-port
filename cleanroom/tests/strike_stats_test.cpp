// Offline checks of vr/strike_stats.h and vr/strike_share.h ([stats] strike_stats): the blows, speeds and forces per hero and weapon, this session's and all
// time's, the missed swings, and the file's text (written, read back, a broken or foreign file). No game, no clock, no file.
// Run: build/Release/strike_stats_test.exe (exit code 0 = passed)

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "../../vr/strike_stats.h"
#include "../../vr/strike_share.h"
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
    CHECK(text.find("version 2") != std::string::npos);
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

// ---- version 2: the distributions, the fatigue curve, the weapon's steel ---------------------------------------------------------------
static void TestBuckets() {
    CHECK(kSpeedBuckets == 14 && kForceBuckets == 10);
    CHECK(SpeedBucket(0.0f) == 0 && SpeedBucket(0.49f) == 0 && SpeedBucket(0.5f) == 1 && SpeedBucket(0.99f) == 1);   // [edge i-1, edge i)
    CHECK(SpeedBucket(3.0f) == 6 && SpeedBucket(4.99f) == 8 && SpeedBucket(9.99f) == 12 && SpeedBucket(10.0f) == 13 && SpeedBucket(55.0f) == 13);
    CHECK(ForceBucket(0.1f) == 0 && ForceBucket(0.25f) == 1 && ForceBucket(1.2f) == 4 && ForceBucket(6.0f) == 9 && ForceBucket(100.0f) == 9);
    Store s;
    s.SetHero("Bar", 4);
    s.Blow(kLsd, 4, 2.2f, 1.2f, 0, 3.5f);
    s.Blow(kLsd, 4, 2.4f, -1.0f, 0);
    s.Blow(kLsd, 4, 7.5f, 4.5f, 3);
    s.Blow(kAxe, 5, 0.0f, 0.0f, 200);   // no speed, no force: counted, in no bucket; minute 200 goes into the last
    s.Miss(kLsd, 4, true, 0.7f);
    s.Miss(kLsd, 4, false, 2.9f);
    s.Miss(kAxe, 5, false);             // no speed: counted, in no bucket
    const Hero* h = s.Current();
    CHECK(h && h->kinds.size() == 2 && h->kinds[0].kind == 4 && h->kinds[1].kind == 5);
    const Hist& sw = h->kinds[0].total;
    CHECK(sw.hit[SpeedBucket(2.2f)] == 2 && sw.hit[SpeedBucket(7.5f)] == 1 && sw.force[ForceBucket(1.2f)] == 1 && sw.force[ForceBucket(4.5f)] == 1);
    CHECK(sw.miss[SpeedBucket(0.7f)] == 1 && sw.miss[SpeedBucket(2.9f)] == 1);
    CHECK(h->kinds[1].total.Empty() && W(h, kAxe)->total.blows == 1 && W(h, kAxe)->total.Misses() == 1);
    CHECK(h->curveTotal.size() == (size_t)kCurveMinutes && h->curveTotal[0].blows == 2 && h->curveTotal[0].speedN == 2 && Near(h->curveTotal[0].speedSum, 4.6));
    CHECK(h->curveTotal[3].blows == 1 && h->curveTotal[kCurveMinutes - 1].blows == 1 && h->curveTotal[kCurveMinutes - 1].speedN == 0);
    CHECK(Near(W(h, kLsd)->kg, 3.5));
    // a new session: its distributions and curve from nothing, all time's stay
    s.StartSession();
    CHECK(s.Heroes()[0].kinds[0].session.Empty() && !s.Heroes()[0].kinds[0].total.Empty() && s.Heroes()[0].curveSession.empty() && s.Heroes()[0].curveTotal[0].blows == 2);
    s.SetHero("Bar", 4);
    s.Blow(kLsd, 4, 3.1f, 1.0f, 1);
    CHECK(s.Current()->kinds[0].session.hit[SpeedBucket(3.1f)] == 1 && s.Current()->kinds[0].total.hit[SpeedBucket(3.1f)] == 1 && s.Current()->curveSession[1].blows == 1);
    // the file keeps all of it, word for word
    const std::string text = s.Serialize(10, 20);
    CHECK(text.find("version 2") != std::string::npos && text.find("hist 4 all hit") != std::string::npos && text.find("hist 4 now hit") != std::string::npos);
    CHECK(text.find("curve all 0 2 2 4.600") != std::string::npos && text.find("weapon lsd 4") != std::string::npos && text.find(" 3.50\n") != std::string::npos);
    Store r;
    CHECK(r.Parse(text) && r.Serialize(10, 20) == text);
    CHECK(r.Heroes()[0].kinds.size() == 1 && r.Heroes()[0].kinds[0].total.miss[SpeedBucket(0.7f)] == 1 && Near(r.Heroes()[0].curveTotal[3].speedSum, 7.5));
    // a version 1 file reads into version 2 (no distributions yet), a broken hist line is skipped, not the file
    Store o;
    CHECK(o.Parse("version 1\nhero Bar 4 1\nall 2 4.0 2 3.0 0 0 0 0 0\nweapon lsd 4 1\nall 2 4.0 2 3.0 0 0 0 0 0\n") && o.Heroes()[0].kinds.empty());
    CHECK(o.Parse("version 2\nhero Bar 4 1\nhist 4 all hit 1 2 3\nhist 4 all miss 0 0 0 0 0 0 1 0 0 0 0 0 0 0\ncurve all 999 1 1 1.0\n"));
    CHECK(o.Heroes()[0].kinds.size() == 1 && o.Heroes()[0].kinds[0].total.hit[0] == 0 && o.Heroes()[0].kinds[0].total.miss[6] == 1 && o.Heroes()[0].curveTotal.empty());
    // the missed swing keeps the speed of its first refused touch
    MissTracker t;
    MissTracker::Miss m;
    CHECK(!t.Refused(9, kLsd, 4, true, 0, &m, 0.6f));
    CHECK(!t.Refused(9, kLsd, 4, false, 10, &m, 2.0f));
    CHECK(t.Flush(MissTracker::kMissMs, &m) && Near(m.speed, 0.6) && m.slow);
}

static void TestShare() {
    Store s;
    s.SetHero("Bar", 4);
    s.Blow(kLsd, 4, 2.0f, 1.0f, 0, 3.0f);
    s.Blow(kLsd, 4, 4.0f, 3.0f, 1, 3.0f);
    s.Blow(CodeOf("7gd"), 4, 3.0f, -1.0f, 1);   // a second sword, no steel measured: not in the kg average
    s.Miss(kLsd, 4, true, 0.4f);
    s.SetHero("Secret_Name", 1);
    s.Blow(kBareHands, 1, 1.5f, -1.0f, 0);
    const std::string id = "0123456789abcdef0123456789abcdef";
    strikeshare::Conditions c;
    c.weightOn = 1;
    c.forceOn = 1;
    c.seated = 0;
    c.heightM = 1.82f;
    c.minSpeed = 0.8f;
    c.strokeCm = 8.0f;
    c.quickBlow = 1;
    const std::string text = strikeshare::Build(s, id, "0.219.2 <b>", c);
    // no hero names, no item codes: only classes
    CHECK(!text.empty() && text.rfind("d2rvr-stats 1\n", 0) == 0);
    CHECK(text.find("Bar") == std::string::npos && text.find("Secret") == std::string::npos && text.find("lsd") == std::string::npos && text.find("7gd") == std::string::npos);
    CHECK(text.find("hero") == std::string::npos && text.find("<b>") == std::string::npos && text.find("version 0.219.2\n") != std::string::npos);
    CHECK(text.find("install " + id + "\n") != std::string::npos);
    CHECK(text.find("cond weight=1 force=1 seated=0 height=1.82 min_speed=0.80 stroke_cm=8.0 quick_blow=1\n") != std::string::npos);
    CHECK(text.find("kind sword blows=3 slow=1 off=0 speed_n=3 speed_sum=9.0 force_n=2 force_sum=4.0 kg=3.00\n") != std::string::npos);
    CHECK(text.find("kind unarmed blows=1 ") != std::string::npos);
    CHECK(text.find("hit sword 0 0 0 0 1 0 1 0 1 0 0 0 0 0\n") != std::string::npos);   // 2.0, 3.0, 4.0
    CHECK(text.find("miss sword 1 0 0 0 0 0 0 0 0 0 0 0 0 0\n") != std::string::npos);  // 0.4 m/s: [0, 0.5)
    CHECK(text.find("curve 0:2:2:3.5 1:2:2:7.0\n") != std::string::npos);   // minute 0: sword 2.0 + fists 1.5; minute 1: 4.0 + 3.0
    // not a valid id, or nothing to share: nothing
    CHECK(strikeshare::Build(s, "not-an-id", "1", c).empty() && strikeshare::Build(Store{}, id, "1", c).empty());
    CHECK(strikeshare::ValidInstall(id) && !strikeshare::ValidInstall("0123456789ABCDEF0123456789abcdef"));
    const uint8_t bytes[16] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0, 0, 0, 0, 0, 0, 0xff, 0x10};
    CHECK(strikeshare::InstallFromBytes(bytes) == "0123456789abcdef000000000000ff10");
    // the answer read back
    const strikeshare::Compare cmp = strikeshare::ParseCompare(
        "d2rvr-compare 1\r\nplayers 154\nclass sword players 120 blows 54000 p10 2.10 p25 2.70 p50 3.30 p75 4.00 p90 4.80 force_p50 2.10 landed 86\n"
        "you sword speed 3.60 speed_pct 72 force 2.40 force_pct 55 blows 812 blows_pct 90\nclass bogus players 3\nclass axe players 7 p50 nan\n");
    CHECK(cmp.ok && cmp.players == 154 && cmp.rows.size() == 2);
    CHECK(cmp.rows[0].kind == "sword" && cmp.rows[0].players == 120 && cmp.rows[0].blows == 54000 && Near(cmp.rows[0].p50, 3.3) && Near(cmp.rows[0].p90, 4.8));
    CHECK(cmp.rows[0].you && cmp.rows[0].speedPct == 72 && cmp.rows[0].forcePct == 55 && cmp.rows[0].blowsPct == 90 && Near(cmp.rows[0].yourSpeed, 3.6) && Near(cmp.rows[0].landed, 86));
    CHECK(cmp.rows[1].kind == "axe" && !cmp.rows[1].you && cmp.rows[1].players == 7 && cmp.rows[1].p50 == 0.0f);
    CHECK(!strikeshare::ParseCompare("<html>502 Bad Gateway</html>").ok && !strikeshare::ParseCompare("").ok);
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
    TestBuckets();
    TestShare();
    std::printf("strike_stats_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
