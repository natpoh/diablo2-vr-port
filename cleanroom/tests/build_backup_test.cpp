// Build backups (shared/build_backup_rule.h, tools/settings/build_backup.cpp): which files are the build, the folders' names, the manifest,
// which old backups go, the restore's order - and a real round trip in a scratch folder: a made-up game folder backed up, changed, restored
// (the running settings program moved aside, the made data's extra file removed, the player's language file kept), pruned, deleted.
//   build_backup_test   (exit code 0 = all passed)
#include "../../tools/settings/build_backup.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <string>

using namespace buildbackup;

static int g_failed = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAILED line %d: %s\n", __LINE__, #c); ++g_failed; } } while (0)

static bool In(const wchar_t* rel) { return AreaOf(rel) >= 0; }

static void Put(const std::wstring& path, const std::string& text) {
    for (size_t at = path.find(L'\\', 3); at != std::wstring::npos; at = path.find(L'\\', at + 1)) CreateDirectoryW(path.substr(0, at).c_str(), nullptr);
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD n = 0;
    if (f != INVALID_HANDLE_VALUE) { WriteFile(f, text.data(), (DWORD)text.size(), &n, nullptr); CloseHandle(f); }
}
static std::string Get(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return "<none>";
    char buf[4096];
    DWORD n = 0;
    ReadFile(f, buf, sizeof buf, &n, nullptr);
    CloseHandle(f);
    return std::string(buf, n);
}
// (long paths too)
static void RemoveAll(const std::wstring& dir) {
    const std::wstring d = L"\\\\?\\" + dir;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((d + L"*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveAll(dir + n + L"\\");
            else DeleteFileW((d + n).c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(d.c_str());
}
static bool There(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }
static bool HasEntry(const std::vector<Entry>& list, const wchar_t* rel) {
    for (const Entry& e : list) if (_wcsicmp(e.rel.c_str(), rel) == 0) return true;
    return false;
}
static Info Made(const wchar_t* folder, const wchar_t* made, int kind, bool complete = true) {
    Info i;
    i.folder = folder; i.made = made; i.kind = kind; i.complete = complete;
    return i;
}
static bool Has(const std::vector<std::wstring>& v, const wchar_t* s) {
    for (const std::wstring& x : v) if (x == s) return true;
    return false;
}

static void Pure() {
    // wildcards
    CHECK(Match(L"*.dll", L"d2r_vr_weight.dll"));
    CHECK(Match(L"*.DLL", L"Game_Link.dll"));
    CHECK(!Match(L"*.dll", L"d2r_vr_weight.dll.before_0202"));
    CHECK(Match(L"d2r_vr*.ini", L"d2r_vr.ini"));
    CHECK(Match(L"d2r_vr*.ini", L"d2r_vr_headset.ini"));
    CHECK(!Match(L"d2r_vr*.ini", L"d2r_vr.ini.before-0.223.0"));
    CHECK(Match(L"a?c", L"abc") && !Match(L"a?c", L"ac"));
    CHECK(MatchAny(L"*.fx;*.fxh;*.png", L"D2R_Campfire_Unleashed.fxh"));
    CHECK(!MatchAny(L"*.fx;*.fxh", L"D2R_DepthFog.fx.before_0186"));

    // what the build is
    CHECK(In(L"game\\D2R_VR_Settings.exe"));
    CHECK(In(L"game\\D2R_VR_Headset_Report.exe"));
    CHECK(!In(L"game\\D2R.exe"));
    CHECK(!In(L"game\\D2R_VR_Settings.exe.before_0201"));
    CHECK(In(L"game\\mods\\D2R-VR\\d2rloader\\plugins\\d2rl-vrcam.dll"));
    CHECK(In(L"game\\mods\\D2R-VR\\d2rloader\\plugins\\d2r_vr.ini"));
    CHECK(In(L"game\\mods\\D2R-VR\\d2rloader\\plugins\\d2r_vr.default.ini"));
    CHECK(In(L"game\\mods\\D2R-VR\\d2rloader\\plugins\\d2r_vr_settings.ini"));
    CHECK(In(L"game\\mods\\D2R-VR\\d2rloader\\plugins\\d2r_vr_autotune.txt"));
    CHECK(!In(L"game\\mods\\D2R-VR\\d2rloader\\plugins\\d2r_vr.ini.before-0.223.0"));
    CHECK(!In(L"game\\mods\\D2R-VR\\d2rloader\\plugins\\d2rl-vrcam.dll.before_0201"));
    CHECK(!In(L"game\\mods\\D2R-VR\\d2rloader\\plugins\\d2r_vr_frames.csv"));
    CHECK(!In(L"game\\mods\\D2R-VR\\d2rloader\\plugins\\d2r_vr_fx_cache\\a.bin"));        // a cache the plugin makes again
    CHECK(!In(L"game\\mods\\D2R-VR\\d2rloader\\plugins\\d2r_vr_skill_icons\\x.json"));
    CHECK(!In(L"game\\mods\\D2R-VR\\d2rloader\\logs\\d2r-vr-vrcam.log"));
    CHECK(In(L"game\\mods\\D2R-VR\\d2rloader\\d2r_vr_libs\\d2r_vr_weight.dll"));
    CHECK(In(L"game\\mods\\D2R-VR\\d2rloader\\d2r_vr_libs\\ofxr\\ofxr.json"));
    CHECK(!In(L"game\\mods\\D2R-VR\\d2rloader\\d2r_vr_libs\\d2r_vr_weight.dll.before_019212"));
    CHECK(In(L"game\\mods\\D2R-VR\\lang\\de.txt"));
    CHECK(!In(L"game\\mods\\D2R-VR\\lang\\de.txt.before_0193"));
    CHECK(In(L"game\\mods\\D2R-VR\\D2R-VR.mpq\\modinfo.json"));
    CHECK(In(L"game\\mods\\D2R-VR\\D2R-VR.mpq\\data\\hd\\env\\texture\\d2r_vr\\sky\\act1.dds"));
    CHECK(In(L"game\\mods\\D2R-VR\\shipped_data\\data\\hd\\env\\texture\\d2r_vr\\sky\\act1.dds"));
    CHECK(In(L"game\\mods\\D2R-VR\\reshade\\reshade-shaders\\Shaders\\D2R_DepthFog.fx"));
    CHECK(In(L"game\\mods\\D2R-VR\\reshade\\reshade-shaders\\Textures\\D2R_Sky_ours\\a.png"));
    CHECK(!In(L"game\\mods\\D2R-VR\\reshade\\ReShade.log"));
    CHECK(!In(L"game\\mods\\D2R-VR\\reshade\\D2RLoader 2026-10-18 20-28-52_1.png"));       // a screenshot
    CHECK(In(L"game\\mods\\D2R-VR\\sky_dome.json"));
    CHECK(In(L"game\\mods\\D2R-VR\\world_holes.stamp"));
    CHECK(In(L"game\\mods\\D2R-VR\\vrmod-manifest.json"));
    CHECK(In(L"game\\mods\\D2R-VR\\launcher_icon.png"));
    CHECK(!In(L"game\\mods\\D2R-VR\\vrmod-manifest.json.before_0179"));
    CHECK(!In(L"game\\mods\\D2R-VR\\BodyWalkVR\\BodyWalkVR.exe"));                         // BodyWalk Portable: BodyWalk's, not the build's
    CHECK(!In(L"game\\mods\\D2R-VR\\build_backups\\0.223.1_2026-11-25_14-03-22\\backup.txt"));   // never a backup inside a backup
    CHECK(!In(L"game\\mods\\D2R-VR\\build_backups\\x\\game\\mods\\D2R-VR\\sky_dome.json"));
    CHECK(In(L"bodywalk\\plugins\\game_link\\game_link.dll"));
    CHECK(In(L"bodywalk\\game_link\\d2r.txt"));
    CHECK(!In(L"bodywalk\\plugins\\game_link\\profile.json"));                             // BodyWalk's profiles are never touched
    CHECK(!In(L"bodywalk\\settings.json"));
    CHECK(!In(L"elsewhere\\d2r_vr.ini"));
    // the .mpq's modinfo.json is the .mpq's (mirrored) area, not the receipts'
    CHECK(Areas()[AreaOf(L"game\\mods\\D2R-VR\\D2R-VR.mpq\\modinfo.json")].mirror);
    CHECK(!Areas()[AreaOf(L"game\\mods\\D2R-VR\\lang\\de.txt")].mirror);

    // names
    Stamp t; t.y = 2026; t.mo = 11; t.d = 25; t.h = 14; t.mi = 3; t.s = 22;
    CHECK(FolderName(L"0.223.1", t, kManual) == L"0.223.1_2026-11-25_14-03-22");
    CHECK(FolderName(L"0.223.1", t, kUpdate) == L"0.223.1_2026-11-25_14-03-22_update");
    CHECK(FolderName(L"0.223.1", t, kRestore) == L"0.223.1_2026-11-25_14-03-22_before_restore");
    CHECK(FolderName(L"0.2/2\\3:", t, kManual) == L"0.223_2026-11-25_14-03-22");
    CHECK(FolderName(L"", t, kManual) == L"unknown_2026-11-25_14-03-22");
    CHECK(StampText(t) == L"2026-11-25 14:03:22");
    CHECK(IsBackupName(L"0.223.1_2026-11-25_14-03-22"));
    CHECK(IsBackupName(L"0.223.1_2026-11-25_14-03-22_update"));
    CHECK(FolderName(L"0.223.1", t, kManual, kSettings) == L"0.223.1_2026-11-25_14-03-22_settings");
    CHECK(FolderName(L"0.223.1", t, kRestore, kSettings) == L"0.223.1_2026-11-25_14-03-22_settings_before_restore");
    CHECK(IsBackupName(L"0.223.1_2026-11-25_14-03-22_settings"));
    CHECK(IsBackupName(L"0.223.1_2026-11-25_14-03-22_settings_before_restore"));
    CHECK(!IsBackupName(L"0.223.1_2026-11-25_14-03-22_settings_update"));
    {   // the settings areas: the plugins folder's own setting files only
        int settingsAreas = 0;
        for (const Area& a : Areas()) if (a.settings) ++settingsAreas;
        CHECK(settingsAreas == 1);
        CHECK(Areas()[AreaOf(L"game\\mods\\D2R-VR\\d2rloader\\plugins\\d2r_vr_headset.ini")].settings);
        CHECK(!Areas()[AreaOf(L"game\\mods\\D2R-VR\\d2rloader\\plugins\\d2rl-vrcam.dll")].settings);
        CHECK(!Areas()[AreaOf(L"game\\D2R_VR_Settings.exe")].settings);
    }
    CHECK(IsBackupName(L"unknown_2026-11-25_14-03-22_before_restore"));
    CHECK(!IsBackupName(L"_replaced"));
    CHECK(!IsBackupName(L"0.223.1_2026-11-25"));
    CHECK(!IsBackupName(L"0.223.1_2026-11-25_14-03-22_other"));
    CHECK(!IsBackupName(L"0.223.1_2026-1x-25_14-03-22"));
    CHECK(!IsBackupName(L"..\\x_2026-11-25_14-03-22"));   // never a path

    // the manifest
    Info in;
    in.version = L"0.223.1"; in.made = L"2026-11-25 14:03:22"; in.kind = kUpdate;
    std::vector<Entry> es = {{L"game\\mods\\D2R-VR\\d2rloader\\plugins\\d2rl-vrcam.dll", 12000000}, {L"game\\D2R_VR_Settings.exe", 6000000},
                             {L"bodywalk\\game_link\\d2r.txt", 900}};
    const std::wstring text = Manifest(in, es);
    Info back;
    std::vector<Entry> got;
    CHECK(ParseManifest(text, &back, &got));
    CHECK(back.version == L"0.223.1" && back.made == L"2026-11-25 14:03:22" && back.kind == kUpdate && back.scope == kBuild && back.complete);
    {
        Info sin = in;
        sin.kind = kManual; sin.scope = kSettings;
        Info sback;
        CHECK(ParseManifest(Manifest(sin, {{L"game\\mods\\D2R-VR\\d2rloader\\plugins\\d2r_vr.ini", 4}}), &sback, nullptr) && sback.scope == kSettings);
    }
    CHECK(back.files == 3 && back.bytes == 18000900);
    CHECK(got.size() == 3 && got[1].rel == L"game\\D2R_VR_Settings.exe" && got[1].size == 6000000);
    Info bad;
    CHECK(!ParseManifest(L"something else", &bad, nullptr));
    CHECK(!ParseManifest(L"D2R VR build backup\r\nfile=5|game\\..\\..\\Windows\\x.dll\r\n", &bad, nullptr));
    CHECK(!ParseManifest(L"D2R VR build backup\r\nfile=5|C:\\Windows\\x.dll\r\n", &bad, nullptr));
    CHECK(!ParseManifest(L"D2R VR build backup\r\nfile=5|game\\D2R.exe\r\n", &bad, nullptr));     // not a file of the build
    CHECK(!ParseManifest(L"D2R VR build backup\r\nfile=5\r\n", &bad, nullptr));

    // which go: the automatic ones past the newest 3, every unfinished one, never the player's own or a kept one
    std::vector<Info> list = {
        Made(L"m1", L"2026-11-01 10:00:00", kManual), Made(L"u1", L"2026-11-02 10:00:00", kUpdate), Made(L"u2", L"2026-11-03 10:00:00", kUpdate),
        Made(L"r1", L"2026-11-04 10:00:00", kRestore), Made(L"u3", L"2026-11-05 10:00:00", kUpdate), Made(L"m2", L"2026-11-06 10:00:00", kManual),
        Made(L"x1", L"2026-11-07 10:00:00", kManual, false), Made(L"u4", L"2026-11-08 10:00:00", kUpdate)};
    std::vector<std::wstring> gone = Prune(list, 3);
    CHECK(gone.size() == 3 && Has(gone, L"u1") && Has(gone, L"u2") && Has(gone, L"x1"));
    gone = Prune(list, 3, {L"u1", L"x1"});   // kept ones stay (and count)
    CHECK(gone.size() == 1 && Has(gone, L"u2"));
    gone = Prune(list, 3, {L"u4"});
    CHECK(gone.size() == 3 && Has(gone, L"u1") && Has(gone, L"u2") && Has(gone, L"x1"));
    gone = Prune(list, 0);                   // (at least one)
    CHECK(gone.size() == 5 && !Has(gone, L"u4") && !Has(gone, L"m1") && !Has(gone, L"m2"));
    gone = Prune(list, 50);                  // (at most kKeepMax)
    CHECK(gone.size() == 1 && Has(gone, L"x1"));
    SortNewestFirst(&list);
    CHECK(list.front().folder == L"u4" && list.back().folder == L"m1");

    // the restore's order: the made data, libraries, the plugin, its settings, the settings program last
    std::vector<Entry> mixed = {{L"game\\D2R_VR_Settings.exe", 1}, {L"game\\mods\\D2R-VR\\d2rloader\\plugins\\d2r_vr.ini", 1},
                                {L"game\\mods\\D2R-VR\\d2rloader\\plugins\\d2rl-vrcam.dll", 1}, {L"game\\mods\\D2R-VR\\D2R-VR.mpq\\modinfo.json", 1},
                                {L"game\\D2R.exe", 1}, {L"game\\mods\\D2R-VR\\d2rloader\\d2r_vr_libs\\d2r_vr_grip.dll", 1},
                                {L"game\\D2R_VR_Headset_Report.exe", 1}, {L"bodywalk\\plugins\\game_link\\game_link.dll", 1}};
    const std::vector<Entry> ord = RestoreOrder(mixed);
    CHECK(ord.size() == 7);   // (D2R.exe is not the build's)
    CHECK(ord[0].rel == L"game\\mods\\D2R-VR\\D2R-VR.mpq\\modinfo.json");
    CHECK(ord[1].rel == L"game\\mods\\D2R-VR\\d2rloader\\d2r_vr_libs\\d2r_vr_grip.dll");
    CHECK(ord[2].rel == L"bodywalk\\plugins\\game_link\\game_link.dll");
    CHECK(ord[3].rel == L"game\\mods\\D2R-VR\\d2rloader\\plugins\\d2rl-vrcam.dll");
    CHECK(ord[4].rel == L"game\\mods\\D2R-VR\\d2rloader\\plugins\\d2r_vr.ini");
    CHECK(ord[5].rel == L"game\\D2R_VR_Headset_Report.exe");
    CHECK(ord[6].rel == L"game\\D2R_VR_Settings.exe");

    // a mirrored restore removes the made data the backup did not have; elsewhere a newer file stays
    const std::vector<Entry> was = {{L"game\\mods\\D2R-VR\\D2R-VR.mpq\\a.json", 1}, {L"game\\mods\\D2R-VR\\sky_dome.json", 1}};
    const std::vector<Entry> now = {{L"game\\mods\\D2R-VR\\D2R-VR.mpq\\a.json", 1}, {L"game\\mods\\D2R-VR\\D2R-VR.mpq\\B.json", 1},
                                    {L"game\\mods\\D2R-VR\\new_thing.json", 1}, {L"game\\mods\\D2R-VR\\SKY_DOME.JSON", 1},
                                    {L"game\\mods\\D2R-VR\\lang\\xx.txt", 1}, {L"game\\mods\\D2R-VR\\d2rloader\\d2r_vr_libs\\new.dll", 1}};
    const std::vector<std::wstring> extra = MirrorExtras(was, now);
    CHECK(extra.size() == 2 && Has(extra, L"game\\mods\\D2R-VR\\D2R-VR.mpq\\B.json") && Has(extra, L"game\\mods\\D2R-VR\\new_thing.json"));

    CHECK(SizeText(259ull * 1024 * 1024) == L"259 MB");
    CHECK(SizeText(13002342) == L"12.4 MB");
    CHECK(SizeText(640 * 1024) == L"640 KB");
}

static void RoundTrip() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring root = std::wstring(tmp) + L"build_backup_test_" + std::to_wstring(GetCurrentProcessId()) + L"\\";
    Places p;
    p.game = root + L"Diablo II Resurrected\\";
    p.bodywalk = root + L"BodyWalkVR\\";
    const std::wstring mod = p.game + L"mods\\D2R-VR\\";
    CHECK(!BuildInstalled(p.game));
    {
        const Outcome none = Make(p, kManual, L"0.1", 3, Now());
        CHECK(none.code == kNothing);
    }
    // the build as Setup leaves it, the player's settings, and what is not the build
    Put(p.game + L"D2R.exe", "the game");
    Put(p.game + L"D2R_VR_Settings.exe", "settings 0.219.2");
    Put(p.game + L"D2R_VR_Headset_Report.exe", "report 0.219.2");
    Put(mod + L"d2rloader\\plugins\\d2rl-vrcam.dll", "plugin 0.219.2");
    Put(mod + L"d2rloader\\plugins\\d2r_vr.ini", "[openxr]\r\non=1\r\nmy_tuning=7\r\n");
    Put(mod + L"d2rloader\\plugins\\d2r_vr.ini.before-0.219.0", "an old copy");
    Put(mod + L"d2rloader\\plugins\\d2r_vr_fx_cache\\a.bin", "a cache");
    Put(mod + L"d2rloader\\logs\\d2r-vr-vrcam.log", "a log");
    Put(mod + L"d2rloader\\d2r_vr_libs\\d2r_vr_weight.dll", "weight 0.219.2");
    Put(mod + L"lang\\de.txt", "deutsch 0.219.2");
    Put(mod + L"D2R-VR.mpq\\modinfo.json", "{ \"name\": \"D2R-VR\" }");
    Put(mod + L"D2R-VR.mpq\\data\\hd\\env\\texture\\d2r_vr\\sky\\act1.dds", "sky 0.219.2");
    Put(mod + L"sky_dome.json", "receipt 0.219.2");
    Put(mod + L"vrmod-manifest.json", "manifest 0.219.2");
    Put(mod + L"reshade\\reshade-shaders\\Shaders\\D2R_DepthFog.fx", "fx 0.219.2");
    Put(mod + L"BodyWalkVR\\BodyWalkVR.exe", "portable");
    Put(p.bodywalk + L"plugins\\game_link\\game_link.dll", "link 0.219.2");
    Put(p.bodywalk + L"plugins\\game_link\\profile.json", "the player's profile");
    Put(p.bodywalk + L"game_link\\d2r.txt", "actions 0.219.2");
    CHECK(BuildInstalled(p.game));
    const std::vector<Entry> scan = Scan(p);
    CHECK(scan.size() == 13);
    CHECK(HasEntry(scan, L"game\\D2R_VR_Settings.exe") && HasEntry(scan, L"bodywalk\\game_link\\d2r.txt"));
    CHECK(!HasEntry(scan, L"game\\D2R.exe") && !HasEntry(scan, L"bodywalk\\plugins\\game_link\\profile.json"));
    CHECK(!HasEntry(scan, L"game\\mods\\D2R-VR\\d2rloader\\plugins\\d2r_vr.ini.before-0.219.0"));

    Stamp t1; t1.y = 2026; t1.mo = 11; t1.d = 20; t1.h = 9; t1.mi = 0; t1.s = 0;
    const Outcome made = Make(p, kManual, L"0.219.2", 3, t1);
    CHECK(made.code == kDone);
    CHECK(made.folder == L"0.219.2_2026-11-20_09-00-00");
    CHECK(made.files == 13);
    const std::wstring b1 = BackupsDir(p.game) + made.folder + L"\\";
    CHECK(Get(b1 + L"game\\mods\\D2R-VR\\d2rloader\\plugins\\d2r_vr.ini").find("my_tuning=7") != std::string::npos);
    CHECK(There(b1 + L"backup.txt") && There(b1 + L"HOW_TO_RESTORE.txt"));
    CHECK(!There(b1 + L"game\\mods\\D2R-VR\\d2rloader\\plugins\\d2r_vr_fx_cache\\a.bin"));
    CHECK(!There(b1 + L"game\\mods\\D2R-VR\\BodyWalkVR\\BodyWalkVR.exe"));
    {
        const std::vector<Info> l = List(p.game);
        CHECK(l.size() == 1 && l[0].complete && l[0].version == L"0.219.2" && l[0].kind == kManual && l[0].files == 13);
        CHECK(Scan(p).size() == 13);   // (the backup folder is not part of the build)
    }

    // an update: new files over the old ones, a newer library, made data of its own, the player's own language file
    Put(p.game + L"D2R_VR_Settings.exe", "settings 0.223.1");
    Put(mod + L"d2rloader\\plugins\\d2rl-vrcam.dll", "plugin 0.223.1");
    Put(mod + L"d2rloader\\plugins\\d2r_vr.ini", "[openxr]\r\non=1\r\nmy_tuning=9\r\nnew_key=1\r\n");
    Put(mod + L"d2rloader\\d2r_vr_libs\\d2r_vr_gait.dll", "gait 0.223.1");
    Put(mod + L"D2R-VR.mpq\\data\\hd\\env\\texture\\d2r_vr\\sky\\act1.dds", "sky 0.223.1");
    Put(mod + L"D2R-VR.mpq\\data\\global\\excel\\new.txt", "made by 0.223.1");
    Put(mod + L"new_receipt.json", "receipt of 0.223.1");
    Put(mod + L"lang\\xx.txt", "the player's own language");
    DeleteFileW((mod + L"lang\\de.txt").c_str());
    // Setup's backup before that update would have been an automatic one; two more, then one past the keep
    Stamp t2 = t1; t2.d = 21;
    CHECK(Make(p, kUpdate, L"0.223.1", 2, t2).code == kDone);
    Stamp t3 = t1; t3.d = 22;
    CHECK(Make(p, kUpdate, L"0.223.1", 2, t3).code == kDone);
    Stamp t4 = t1; t4.d = 23;
    CHECK(Make(p, kUpdate, L"0.223.1", 2, t4).code == kDone);
    {
        const std::vector<Info> l = List(p.game);   // the oldest automatic one went; the player's own stays
        CHECK(l.size() == 3);
        if (l.size() != 3) for (const Info& i : l) std::printf("  %ls complete=%d kind=%d made=%ls\n", i.folder.c_str(), (int)i.complete, i.kind, i.made.c_str());
        CHECK(l.size() == 3 && l[0].folder == L"0.223.1_2026-11-23_09-00-00_update" && l[1].folder == L"0.223.1_2026-11-22_09-00-00_update" &&
              l[2].folder == L"0.219.2_2026-11-20_09-00-00");
    }
    // a backup that did not finish is never restored, and goes at the next backup
    CreateDirectoryW((BackupsDir(p.game) + L"0.223.1_2026-11-24_09-00-00").c_str(), nullptr);
    CHECK(Restore(p, L"0.223.1_2026-11-24_09-00-00", 2, t4).code == kBadBackup);
    CHECK(Restore(p, L"not_ours", 2, t4).code == kBadBackup);

    // restore 0.219.2 while "D2R_VR_Settings.exe" runs (a mapped image: it can be moved, not written over)
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    const std::string settingsOld = Get(b1 + L"game\\D2R_VR_Settings.exe");
    CopyFileW(self, (p.game + L"D2R_VR_Settings.exe").c_str(), FALSE);
    HMODULE running = LoadLibraryExW((p.game + L"D2R_VR_Settings.exe").c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    CHECK(running != nullptr);
    {
        HANDLE w = CreateFileW((p.game + L"D2R_VR_Settings.exe").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(w == INVALID_HANDLE_VALUE);   // (it really is held)
        if (w != INVALID_HANDLE_VALUE) CloseHandle(w);
    }
    Stamp t5 = t1; t5.d = 25;
    const Outcome r = Restore(p, L"0.219.2_2026-11-20_09-00-00", 2, t5);
    CHECK(r.code == kDone);
    if (r.code != kDone) std::printf("  restore: %ls\n", r.error.c_str());
    CHECK(r.settingsReplaced);
    CHECK(r.folder == L"unknown_2026-11-25_09-00-00_before_restore");   // (made-up files carry no version)
    CHECK(Get(p.game + L"D2R_VR_Settings.exe") == settingsOld);
    CHECK(Get(mod + L"d2rloader\\plugins\\d2rl-vrcam.dll") == "plugin 0.219.2");
    CHECK(Get(mod + L"d2rloader\\plugins\\d2r_vr.ini").find("my_tuning=7") != std::string::npos);
    CHECK(Get(mod + L"d2rloader\\plugins\\d2r_vr.ini").find("new_key") == std::string::npos);
    CHECK(Get(mod + L"D2R-VR.mpq\\data\\hd\\env\\texture\\d2r_vr\\sky\\act1.dds") == "sky 0.219.2");
    CHECK(!There(mod + L"D2R-VR.mpq\\data\\global\\excel\\new.txt"));   // the newer build's made data: gone
    CHECK(!There(mod + L"new_receipt.json"));                           // ... with its receipt
    CHECK(Get(mod + L"sky_dome.json") == "receipt 0.219.2");
    CHECK(Get(mod + L"lang\\de.txt") == "deutsch 0.219.2");             // back
    CHECK(Get(mod + L"lang\\xx.txt") == "the player's own language");   // stays
    CHECK(Get(mod + L"d2rloader\\d2r_vr_libs\\d2r_vr_gait.dll") == "gait 0.223.1");   // a newer library stays (harmless)
    CHECK(Get(p.bodywalk + L"plugins\\game_link\\game_link.dll") == "link 0.219.2");
    CHECK(Get(p.bodywalk + L"plugins\\game_link\\profile.json") == "the player's profile");
    CHECK(Get(mod + L"d2rloader\\plugins\\d2r_vr_fx_cache\\a.bin") == "a cache");     // not the build's: untouched
    CHECK(Get(mod + L"d2rloader\\logs\\d2r-vr-vrcam.log") == "a log");
    CHECK(Get(p.game + L"D2R.exe") == "the game");
    {   // what was there is a backup of its own, the 0.223.1 settings in it
        const std::wstring b = BackupsDir(p.game) + r.folder + L"\\";
        CHECK(Get(b + L"game\\mods\\D2R-VR\\d2rloader\\plugins\\d2r_vr.ini").find("my_tuning=9") != std::string::npos);
        CHECK(Get(b + L"game\\mods\\D2R-VR\\D2R-VR.mpq\\data\\global\\excel\\new.txt") == "made by 0.223.1");
        // the unfinished one went with that backup; the restored one stayed although it is the oldest
        const std::vector<Info> l = List(p.game);
        bool unfinished = false, restored = false;
        for (const Info& i : l) {
            if (i.folder == L"0.223.1_2026-11-24_09-00-00") unfinished = true;
            if (i.folder == L"0.219.2_2026-11-20_09-00-00") restored = true;
        }
        CHECK(!unfinished && restored);
        // the restore's own backup counts: of the automatic ones it and the newest update stay (keep 2), the older update went
        CHECK(l.size() == 3 && l[0].kind == kRestore && l[1].folder == L"0.223.1_2026-11-23_09-00-00_update");
    }
    CHECK(There(BackupsDir(p.game) + kReplaced));
    FreeLibrary(running);
    CleanReplaced(p.game);
    CHECK(!There(BackupsDir(p.game) + kReplaced));
    // no .restore_tmp / .tmp left anywhere in the build
    for (const Entry& e : Scan(p)) CHECK(e.rel.find(L"_tmp") == std::wstring::npos);

    // Save settings: the settings files only; restoring them leaves the build's code and its made data as they are
    {
        const std::vector<Entry> settingsNow = Scan(p, kSettings);
        CHECK(settingsNow.size() == 1 && HasEntry(settingsNow, L"game\\mods\\D2R-VR\\d2rloader\\plugins\\d2r_vr.ini"));
        Stamp s1 = t1; s1.d = 27;
        const Outcome so = Make(p, kManual, L"0.219.2", 10, s1, nullptr, {}, kSettings);
        CHECK(so.code == kDone && so.folder == L"0.219.2_2026-11-27_09-00-00_settings" && so.files == 1);
        Info si;
        std::vector<Entry> se;
        CHECK(ReadBackup(p.game, so.folder, &si, &se) && si.scope == kSettings && se.size() == 1);
        // the player tries other settings, and the build changes too
        Put(mod + L"d2rloader\\plugins\\d2r_vr.ini", "[openxr]\r\non=0\r\ntried=1\r\n");
        Put(mod + L"d2rloader\\plugins\\d2rl-vrcam.dll", "plugin 0.224.0");
        Put(mod + L"D2R-VR.mpq\\data\\made_by_0224.json", "made");
        Stamp s2 = t1; s2.d = 28;
        const Outcome sr = Restore(p, so.folder, 10, s2);
        CHECK(sr.code == kDone);
        CHECK(sr.folder == L"unknown_2026-11-28_09-00-00_settings_before_restore");
        CHECK(Get(mod + L"d2rloader\\plugins\\d2r_vr.ini").find("my_tuning=7") != std::string::npos);   // the settings back
        CHECK(Get(mod + L"d2rloader\\plugins\\d2rl-vrcam.dll") == "plugin 0.224.0");                    // the code not
        CHECK(Get(mod + L"D2R-VR.mpq\\data\\made_by_0224.json") == "made");                             // nor the made data
        CHECK(!sr.settingsReplaced && sr.settingsPending.empty());
        {   // what was there: a settings backup of its own, with the tried settings
            Info bi;
            std::vector<Entry> be;
            CHECK(ReadBackup(p.game, sr.folder, &bi, &be) && bi.scope == kSettings && bi.kind == kRestore && be.size() == 1);
            CHECK(Get(BackupsDir(p.game) + sr.folder + L"\\game\\mods\\D2R-VR\\d2rloader\\plugins\\d2r_vr.ini").find("tried=1") != std::string::npos);
        }
        // a settings manifest naming a file of the build is not ours
        Info bad;
        CHECK(!ParseManifest(L"D2R VR build backup\r\nscope=settings\r\nfile=5|game\\D2R_VR_Settings.exe\r\n", &bad, nullptr));
        Put(mod + L"d2rloader\\plugins\\d2rl-vrcam.dll", "plugin 0.219.2");
        DeleteFileW((mod + L"D2R-VR.mpq\\data\\made_by_0224.json").c_str());
    }

    // the running settings program cannot even be moved aside (FAT / exFAT, or held without delete sharing): the restored one waits beside it
    // as D2R_VR_Settings.exe.restore_new, and FinishPending puts it in place once the program that restored it has exited
    {
        const std::wstring exe = p.game + L"D2R_VR_Settings.exe";
        Put(exe, "settings 0.224.0");
        HANDLE held = CreateFileW(exe.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);   // (no delete sharing: no rename)
        CHECK(held != INVALID_HANDLE_VALUE);
        Stamp s3 = t1; s3.d = 29;
        const Outcome o = Restore(p, L"0.219.2_2026-11-20_09-00-00", 10, s3);
        CHECK(o.code == kDone);
        if (o.code != kDone) std::printf("  pending restore: %ls\n", o.error.c_str());
        CHECK(!o.settingsReplaced);
        CHECK(o.settingsPending == exe + kPending);
        CHECK(Get(exe + kPending) == settingsOld);          // the restored one, beside
        CloseHandle(held);
        CHECK(Get(exe) == "settings 0.224.0");              // the old one still in place
        // a process standing in for the restoring program: FinishPending waits for its end, then swaps
        STARTUPINFOW si{sizeof si};
        PROCESS_INFORMATION pi{};
        std::wstring cmd = L"cmd.exe /c ping -n 2 127.0.0.1 >nul";
        CHECK(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi));
        const ULONGLONG t0 = GetTickCount64();
        CHECK(FinishPending(pi.dwProcessId, exe + kPending, exe, 20000));
        CHECK(WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0);   // (it waited for the end)
        CHECK(GetTickCount64() - t0 >= 500);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CHECK(Get(exe) == settingsOld && !There(exe + kPending));
        // a process long gone (no such id): at once
        Put(exe + kPending, "again");
        CHECK(FinishPending(0xFFFFFFF0u, exe + kPending, exe, 20000) && Get(exe) == "again");
    }

    // a backup whose file was changed afterwards is not restored
    {
        const std::wstring f = BackupsDir(p.game) + L"0.223.1_2026-11-23_09-00-00_update\\game\\mods\\D2R-VR\\d2rloader\\plugins\\d2rl-vrcam.dll";
        Put(f, "x");
        CHECK(Restore(p, L"0.223.1_2026-11-23_09-00-00_update", 2, t5).code == kBadBackup);
    }
    // delete: only a folder of ours
    CHECK(!Remove(p.game, L"..\\..\\Diablo II Resurrected"));
    CHECK(!Remove(p.game, L"_replaced"));
    CHECK(Remove(p.game, L"0.223.1_2026-11-23_09-00-00_update"));
    CHECK(!There(BackupsDir(p.game) + L"0.223.1_2026-11-23_09-00-00_update"));
    CHECK(There(p.game + L"D2R.exe"));

    // long paths: a made data file deep enough that its path in a backup passes 260 characters
    {
        std::wstring deep = mod + L"D2R-VR.mpq\\data\\";
        while (deep.size() < 240) deep += L"a_rather_long_folder_name\\";
        Put(L"\\\\?\\" + deep + L"file.json", "deep");
        Stamp t6 = t1; t6.d = 26;
        const Outcome o = Make(p, kManual, L"0.1", 2, t6);
        CHECK(o.code == kDone);
        if (o.code != kDone) std::printf("  long: %ls\n", o.error.c_str());
    }

    // clean up the scratch folder
    RemoveAll(root);
    CHECK(!There(root));
}

int main() {
    Pure();
    RoundTrip();
    if (g_failed) { std::printf("%d FAILED\n", g_failed); return 1; }
    std::printf("build_backup_test: all passed\n");
    return 0;
}
