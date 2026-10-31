// D2R VR Headset Report's arithmetic and hints (tools/headset_report/report_stats.h) on made-up numbers - among them the numbers of the players
// it was made for: a Quest 3 over SteamVR whose eyes did not fuse (0.189), a Quest 2 over Link whose STAGE gave nothing (0.184 - 0.186).
// No headset, no OpenXR.
//
//   headset_report_test   (exit code 0 = all passed)

#include <cmath>
#include <cstdio>
#include <string>

#include "mod_files.h"
#include "report_stats.h"

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (!(cond)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

using namespace headsetreport;

static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
static bool Has(const std::vector<std::string>& v, const char* part) {
    for (const std::string& s : v) if (s.find(part) != std::string::npos) return true;
    return false;
}

// The zzangu Quest 3 numbers (d2r_vr.log of 2026-10-30): a mirror pair, nothing odd in the angles themselves.
static Facts Quest3() {
    Facts f;
    f.session = true;
    f.frames = 700;
    f.gapMm = 68.1f;
    f.fov[0] = {-54.0f, 40.0f, 44.0f, -55.0f};
    f.fov[1] = {-40.0f, 54.0f, 44.0f, -55.0f};
    f.recW[0] = f.recW[1] = 1648;
    f.recH[0] = f.recH[1] = 1824;
    f.stageListed = true;
    for (int i = 0; i < 700; ++i) { f.headStage.Add(true, 0xF); f.headLocal.Add(true, 0xF); }
    f.handsLooked = true;
    f.profile[0] = f.profile[1] = "/interaction_profiles/oculus/touch_controller";
    for (int h = 0; h < 2; ++h)
        for (int s = 0; s < 2; ++s)
            for (int i = 0; i < 700; ++i) { f.grip[h][s].Add(true, 0xF); f.aim[h][s].Add(true, 0xF); }
    return f;
}

static void TestSeries() {
    Series s;
    CHECK(s.n == 0 && s.Mean() == 0.0f && s.Spread() == 0.0f);
    s.Add(2.0f); s.Add(4.0f); s.Add(std::nanf("")); s.Add(3.0f);
    CHECK(s.n == 3 && Near(s.Mean(), 3.0f) && Near(s.lo, 2.0f) && Near(s.hi, 4.0f) && Near(s.Spread(), 2.0f));
    FovSeries fs;
    fs.Add({-54.0f, 40.0f, 44.0f, -55.0f});
    fs.Add({-54.2f, 40.0f, 44.0f, -55.0f});
    CHECK(Near(fs.Mean().l, -54.1f) && Near(fs.Spread(), 0.2f, 1e-3f));
}

static void TestSymmetry() {
    const Facts q = Quest3();
    Symmetry s = CheckSymmetry(q.fov[0], q.fov[1]);
    CHECK(s.mirrored && s.sameVertical);
    CHECK(Near(s.outerL, 54.0f) && Near(s.outerR, 54.0f) && Near(s.innerL, 40.0f) && Near(s.innerR, 40.0f));
    CHECK(Near(s.wideL, 94.0f) && Near(s.tallL, 99.0f));
    // the same angles given to both eyes (a runtime / layer that copies the left eye's): not a mirror pair
    s = CheckSymmetry(q.fov[0], q.fov[0]);
    CHECK(!s.mirrored && s.sameVertical);
    // symmetric per eye (a centred fov): still a mirror pair
    s = CheckSymmetry({-45.0f, 45.0f, 45.0f, -45.0f}, {-45.0f, 45.0f, 45.0f, -45.0f});
    CHECK(s.mirrored);
    // within the tolerance, and just past it
    CHECK(CheckSymmetry({-54.0f, 40.0f, 44.0f, -55.0f}, {-40.4f, 54.4f, 44.4f, -55.0f}).mirrored);
    CHECK(!CheckSymmetry({-54.0f, 40.0f, 44.0f, -55.0f}, {-40.0f, 54.6f, 44.0f, -55.0f}).mirrored);
    CHECK(!CheckSymmetry({-54.0f, 40.0f, 44.0f, -55.0f}, {-40.0f, 54.0f, 41.0f, -55.0f}).sameVertical);
}

static void TestFlagsAndLocated() {
    CHECK(FlagsText(0) == "----");
    CHECK(FlagsText(0xF) == "PpRr");
    CHECK(FlagsText(kOriValid | kPosValid) == "P-R-");
    CHECK(FlagsText(kOriValid | kOriTracked) == "--Rr");
    CHECK(Whole(0x3) && !Whole(0x1) && !Whole(0x2));
    Located l;
    CHECK(l.Text() == "not looked at");
    l.Add(true, 0xF);
    l.Add(true, 0x0);
    l.Add(false, 0xF);
    l.Add(true, 0xF);
    CHECK(l.frames == 4 && l.calls == 3 && l.whole == 2 && l.tracked == 2 && l.seen.size() == 2);
    CHECK(l.Text() == "whole 2/4, tracked 2/4, the call failed 1 times, flags PpRr ----");
}

static void TestFovLine() {
    const Facts q = Quest3();
    // the plugin's own line (vr/xr.cpp TellEyes), so the report and a d2r_vr.log read side by side
    CHECK(FovLine(68.1f, 0.0f, q.fov[0], q.fov[1]) ==
          "eyes 68.1 mm apart, 0.00 deg between their views; left fov -54.0 40.0 44.0 -55.0, right -40.0 54.0 44.0 -55.0 (left right up down, deg)");
}

static void TestHints() {
    // no session (the game running): said, nothing else guessed
    {
        Facts f;
        const auto h = Hints(f);
        CHECK(h.size() == 1 && Has(h, "No session ran"));
    }
    // the runtime there but no headset (Virtual Desktop not streaming, Link off): said so
    {
        Facts f;
        f.noHeadset = true;
        const auto h = Hints(f);
        CHECK(h.size() == 1 && Has(h, "found no headset"));
    }
    // the game running: the runtime was not asked at all, and the player is told to close it
    {
        Facts f;
        f.gameRunning = true;
        const auto h = Hints(f);
        CHECK(h.size() == 1 && Has(h, "close the game"));
    }
    // the Quest 3: nothing in the numbers stands out (its fault is elsewhere: the game's picture)
    {
        const auto h = Hints(Quest3());
        CHECK(h.size() == 1 && Has(h, "Nothing unusual"));
    }
    // both eyes in one place; an odd gap; a moving gap
    {
        Facts f = Quest3();
        f.gapMm = 0.0f;
        CHECK(Has(Hints(f), "in one place"));
        f.gapMm = 45.0f;
        CHECK(Has(Hints(f), "outside the usual"));
        f.gapMm = 64.0f;
        f.gapSpreadMm = 2.0f;
        CHECK(Has(Hints(f), "eye distance moved"));
    }
    // a pair not mirrored, up/down unequal, a fov that misses straight ahead, canted displays, a fov that moves
    {
        Facts f = Quest3();
        f.fov[1] = f.fov[0];
        CHECK(Has(Hints(f), "not a mirror pair"));
        f = Quest3();
        f.fov[1].u = 40.0f;
        CHECK(Has(Hints(f), "differ up/down"));
        f = Quest3();
        f.fov[0].r = -2.0f;
        CHECK(Has(Hints(f), "left eye's fov does not contain straight ahead"));
        f = Quest3();
        f.cantDeg = 10.0f;
        CHECK(Has(Hints(f), "canted"));
        f = Quest3();
        f.fovSpreadDeg = 3.0f;
        CHECK(Has(Hints(f), "fov changed"));
        f = Quest3();
        f.fovSpaceDiffDeg = 1.0f;
        CHECK(Has(Hints(f), "between the spaces"));
        f = Quest3();
        f.recW[1] = 1600;
        CHECK(Has(Hints(f), "recommended pictures differ"));
    }
    // the Quest 2 over Link: STAGE gives the head nothing, LOCAL does - on Meta's runtime the mod takes LOCAL as the room by default
    {
        Facts f = Quest3();
        f.metaRuntime = true;
        f.headStage = Located{};
        for (int i = 0; i < 700; ++i) f.headStage.Add(true, 0x0);
        const auto h = Hints(f);
        CHECK(Has(h, "Quest-over-Link") && Has(h, "room_by_head=2"));
        f.metaRuntime = false;
        CHECK(Has(Hints(f), "room_by_head=1"));
        // no STAGE at all
        f = Quest3();
        f.stageListed = false;
        CHECK(Has(Hints(f), "no STAGE space"));
        // STAGE part of the time
        f = Quest3();
        f.headStage = Located{};
        for (int i = 0; i < 10; ++i) f.headStage.Add(true, i < 5 ? 0xF : 0x0);
        CHECK(Has(Hints(f), "5 of 10 frames only"));
        // not tracking at all
        f = Quest3();
        f.headLocal = Located{};
        for (int i = 0; i < 10; ++i) f.headLocal.Add(true, 0x0);
        CHECK(Has(Hints(f), "not tracking"));
    }
    // the hands: none, a grip with nothing in STAGE while LOCAL has it (the hands at the waist), never whole, never read
    {
        Facts f = Quest3();
        f.profile[1].clear();
        CHECK(Has(Hints(f), "No controller profile for the right hand"));
        f = Quest3();
        f.grip[0][1] = Located{};
        for (int i = 0; i < 10; ++i) f.grip[0][1].Add(true, 0x0);
        CHECK(Has(Hints(f), "left grip has no whole pose in STAGE while LOCAL"));
        f = Quest3();
        f.grip[1][0] = Located{};
        f.grip[1][1] = Located{};
        for (int i = 0; i < 10; ++i) { f.grip[1][0].Add(true, 0x0); f.grip[1][1].Add(true, 0x0); }
        const auto h = Hints(f);
        CHECK(Has(h, "right grip has no whole pose in STAGE while the aim"));
        f.aim[1][1] = Located{};
        for (int i = 0; i < 10; ++i) f.aim[1][1].Add(true, 0x0);
        CHECK(Has(Hints(f), "right grip was never located whole"));
        f = Quest3();
        f.handsLooked = false;
        CHECK(Has(Hints(f), "never had the focus"));
    }
    // a session without one sampled frame
    {
        Facts f;
        f.session = true;
        CHECK(Has(Hints(f), "no frame could be sampled"));
    }
}

// D2R VR's files (tools/headset_report/mod_files.h): ReShade.ini's search paths, the effect's markers, the preset's top keys, the hints.
static void TestModFiles() {
    using namespace modfiles;
    // EffectSearchPaths as ReShade writes them: comma-separated, "\**" = with the subfolders
    {
        const auto p = SplitSearchPaths(".\\reshade-shaders\\Shaders\\**, C:\\Shaders\\ ,\"D:\\x y\\*\",,");
        CHECK(p.size() == 3);
        CHECK(p[0].path == ".\\reshade-shaders\\Shaders" && p[0].recursive);
        CHECK(p[1].path == "C:\\Shaders" && !p[1].recursive);
        CHECK(p[2].path == "D:\\x y" && !p[2].recursive);
        CHECK(SplitSearchPaths("").empty());
        CHECK(SplitSearchPaths("C:\\").size() == 1 && SplitSearchPaths("C:\\")[0].path == "C:\\");
    }
    {
        CHECK(Resolve("C:\\Game\\mods\\D2R-VR\\reshade", ".\\reshade-shaders\\Shaders") == "C:\\Game\\mods\\D2R-VR\\reshade\\reshade-shaders\\Shaders");
        CHECK(Resolve("C:\\Game\\", "reshade-shaders") == "C:\\Game\\reshade-shaders");
        CHECK(Resolve("C:\\Game", "D:\\Shaders") == "D:\\Shaders");
        CHECK(Resolve("C:\\Game", "\\\\pc\\share") == "\\\\pc\\share");
        CHECK(Resolve("C:\\Game", ".") == "C:\\Game");
    }
    // the markers: the uniforms vrcam asks ReShade for, as whole identifiers
    {
        const Markers m = FindMarkers("uniform bool GameBarOn < hidden = true; > = false;\nuniform bool GameMapOn;\nuniform float QvN < hidden = true; > = 0.0;");
        CHECK(m.bar && m.map && m.qvn && m.Current());
        const Markers old = FindMarkers("uniform bool GameBarOnly; float QvNx; // GameMapOn_ is not it");
        CHECK(!old.bar && !old.qvn && !old.map && !old.Current());
        CHECK(MarkersText(old) == "GameBarOn NO, GameMapOn NO, QvN NO");
    }
    // the preset's top keys (before the first section; a BOM before them)
    {
        const std::string preset = "\xEF\xBB\xBFPreprocessorDefinitions=A=1\r\nTechniques=D2R_DepthFog@D2R_DepthFog.fx,D2R_DepthFog_R@D2R_DepthFog.fx\r\n\r\n"
                                   "[D2R_DepthFog.fx]\r\nTechniques=not this\r\n";
        CHECK(TopValue(preset, "Techniques") == "D2R_DepthFog@D2R_DepthFog.fx,D2R_DepthFog_R@D2R_DepthFog.fx");
        CHECK(TopValue(preset, "preprocessordefinitions") == "A=1");
        CHECK(TopValue(preset, "TechniqueSorting").empty());
    }
    // ReShade's effect cache: the files of one compile (shader model and source hash)
    {
        CHECK(CacheCompile("reshade-D2R_DepthFog-49152-2334929096412360845-0-F__PS_StampR.cso") == "49152-2334929096412360845");
        CHECK(CacheCompile("reshade-d2r_depthfog-49152-77-0-F__VS.asm") == "49152-77");
        CHECK(CacheCompile("reshade-Other-49152-1-0-x.cso").empty() && CacheCompile("reshade-D2R_DepthFog-49152").empty());
    }
    // Unleashed / d2r-pcvr by their file names; our own Unleashed include is not one
    {
        CHECK(UnleashedFile("d2rl-pcvr.dll", false) && UnleashedFile("pcvr-install.json", false) && UnleashedFile("D2R_Frame_ReShade.ini", false));
        CHECK(UnleashedFile("some_Unleashed_thing.txt", false));
        CHECK(!UnleashedFile("D2R_FogEase_Unleashed.fxh", false));
        CHECK(UnleashedFile("openxr_loader.dll", true) && !UnleashedFile("openxr_loader.dll", false));
        CHECK(!UnleashedFile("d2rl-vrcam.dll", false));
    }
    // the hints: the zzangu case (no_reshade=0, the current effect in the mod), a second and older copy, nothing found, another mod
    {
        ModFacts f;
        CHECK(Hints(f).empty());   // no mod beside the program: nothing said
        f.modFound = true;
        f.noReshade = 0;
        f.noReshadeDefault = 1;
        IniSeen mod;
        mod.path = "C:\\Game\\mods\\D2R-VR\\reshade\\ReShade.ini";
        mod.dllBeside = true;
        mod.depthFog.push_back({"C:\\Game\\mods\\D2R-VR\\reshade\\reshade-shaders\\Shaders\\D2R_DepthFog.fx", {true, true, true}});
        f.inis.push_back(mod);
        auto h = Hints(f);
        CHECK(h.size() == 1 && Has(h, "no_reshade=0"));
        f.inis[0].depthFog.push_back({"C:\\Game\\mods\\D2R-VR\\reshade\\reshade-shaders\\Shaders\\old\\D2R_DepthFog.fx", {false, false, false}});
        h = Hints(f);
        CHECK(Has(h, "2 copies of D2R_DepthFog.fx") && Has(h, "old\\D2R_DepthFog.fx is an older effect"));
        // an ini with no ReShade beside it is not read while another has one: not judged
        IniSeen stray;
        stray.path = "C:\\Game\\ReShade.ini";
        f.inis.push_back(stray);
        CHECK(!Has(Hints(f), "C:\\Game\\ReShade.ini"));
        f.inis[1].dllBeside = true;
        CHECK(Has(Hints(f), "C:\\Game\\ReShade.ini: its EffectSearchPaths reach no D2R_DepthFog.fx"));
        f = ModFacts{};
        f.modFound = true;
        f.otherVrMods.push_back("mods\\D2R-PCVR (d2rl-pcvr.dll)");
        f.unleashedSigns.push_back("C:\\Game\\d2rloader\\pcvr-install.json");
        h = Hints(f);
        CHECK(Has(h, "Another VR mod's files are here: mods\\D2R-PCVR") && Has(h, "d2r-pcvr are beside the game or in D2R VR's folders (1"));
        // another mod's ReShade (d2r-pcvr's own D2R_DepthFog.fx, no QvN): listed, not judged; one cache folder for both: said
        f = ModFacts{};
        f.modFound = true;
        IniSeen ours;
        ours.path = "C:\\Game\\mods\\D2R-VR\\reshade\\ReShade.ini";
        ours.dllBeside = true;
        ours.cache = "C:\\Temp\\ReShade";
        ours.depthFog.push_back({"C:\\Game\\mods\\D2R-VR\\reshade\\reshade-shaders\\Shaders\\D2R_DepthFog.fx", {true, true, true}});
        IniSeen bear = ours;
        bear.ours = false;
        bear.path = "C:\\Game\\mods\\D2R-PCVR\\reshade\\ReShade.ini";
        bear.cache = "c:\\temp\\RESHADE";
        bear.depthFog[0] = {"C:\\Game\\mods\\D2R-PCVR\\reshade\\reshade-shaders\\Shaders\\D2R_DepthFog.fx", {true, true, false}};
        f.inis = {bear, ours};
        h = Hints(f);
        CHECK(h.size() == 1 && Has(h, "share one effect cache, C:\\Temp\\ReShade") && !Has(h, "older effect"));
        f.inis[0].cache = "D:\\Other";
        CHECK(Hints(f).empty());
        // no_reshade as shipped: nothing said
        f = ModFacts{};
        f.modFound = true;
        f.noReshade = 1;
        f.noReshadeDefault = 1;
        CHECK(Hints(f).empty());
    }
}

int main() {
    TestModFiles();
    TestSeries();
    TestSymmetry();
    TestFlagsAndLocated();
    TestFovLine();
    TestHints();
    std::printf("headset_report_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
