// vr/config/reconcile.cpp - the game's own Settings.json as VR and flat mode want it: its interface scale read, TAA, the minimap's
// corner, the windowed size, the frame cap, the safe screen, the safe window (cut from vr/vrcam.cpp, docs/plan_modules.md M2 - the
// map's config group). What it gives: vr/config/settings.h. It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// The game's own interface size ("Safe Screen Percent" in Saved Games\Diablo II
// Resurrected\Settings.json, 50..100), as a fraction: the toolbar's size and
// place follow it (hud.cpp). 1 when it cannot be read.
float GameInterfaceScale() {
    PWSTR dir = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &dir))) path = std::wstring(dir) + L"\\Diablo II Resurrected\\Settings.json";
    if (dir) CoTaskMemFree(dir);
    FILE* f = path.empty() ? nullptr : _wfopen(path.c_str(), L"rb");
    if (!f) return 1.0f;
    std::string text(1 << 16, ' ');
    text.resize(fread(text.data(), 1, text.size(), f));
    fclose(f);
    return safescreen::Scale(text);   // (shared/safe_screen.h: the file's number over 100, 1 when it is missing or out of 30 - 100)
}

// Real stereo with the game's temporal anti-aliasing ("Anti Aliasing" 2 and up in Settings.json):
// it blends each picture with the one before - the other eye's - and every moving thing trails a
// ghost (the staff in the hand, 2026-10-09). At the plugin's start, before the game reads the
// file, it is set to FXAA (1); [stereo] keep_taa=1 leaves it alone.
void NoTemporalAA() {
    // (VR only: flat mode ([mode] platform=0) keeps the player's own anti-aliasing - the shipped ini has afr=1, for VR)
    if (g_set.platform.load() != 1 || !IniB(L"stereo", L"afr", false) || IniB(L"stereo", L"keep_taa", false)) return;
    PWSTR dir = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &dir))) path = std::wstring(dir) + L"\\Diablo II Resurrected\\Settings.json";
    if (dir) CoTaskMemFree(dir);
    FILE* f = path.empty() ? nullptr : _wfopen(path.c_str(), L"rb");
    if (!f) return;
    std::string text(1 << 16, ' ');
    text.resize(fread(text.data(), 1, text.size(), f));
    fclose(f);
    const size_t at = text.find("\"Anti Aliasing\"");
    const size_t colon = at == std::string::npos ? at : text.find(':', at);
    if (colon == std::string::npos) return;
    size_t b = colon + 1;
    while (b < text.size() && (text[b] == ' ' || text[b] == '\t')) ++b;
    size_t e = b;
    while (e < text.size() && isdigit((unsigned char)text[e])) ++e;
    if (e == b) return;
    const int aa = atoi(text.c_str() + b);
    if (aa < 2) return;
    text.replace(b, e - b, "1");
    if (FILE* w = _wfopen(path.c_str(), L"wb")) {
        fwrite(text.data(), 1, text.size(), w);
        fclose(w);
        LogF("vrcam: the game's Anti Aliasing was %d (temporal: it mixes the eyes, a ghost behind everything moving) - set to FXAA ([stereo] keep_taa=1 to keep it)", aa);
    }
}

// The corner map for the hand (2026-10-12): the map in the headset - the orb or the flat panel - is the
// game's mini map taken out of the picture ([hud] map=1), and only the one on the right comes into the orb
// (the author, 2026-10-12): the game's map is on the right, the left or in the middle (Fullscreen) - in the
// middle players had the whole-screen map before their eyes, moving with the view, and no orb. At the
// plugin's start, before the game reads the file, in VR with the map out of the picture: "AutoMapMode" set
// to 2 - on the right (the game moves it left while a panel covers the right; seen in the log as the map's
// box on either side) - unless it is 2 already. The game's own options may change it; the next start sets
// it again. A read-only Settings.json (players were told to make it so for the window size) has the flag
// taken off first, as SafeGameWindow does; a file that cannot be written or has no AutoMapMode is logged.
// [hud] map_corner=1 (Settings: "Corner map is" Top left) looks for the map in the left corner only: then the
// left one, 1 (2026-10-28: a player with it set had 2 written, the map never found, left in the picture top right).
void MiniMapForHand() {
    if (g_set.platform.load() != 1 || g_set.hudMap.load() != 1) return;
    const int want = minimapside::CornerFor((int)IniF(L"hud", L"map_corner", 0.0f));   // 1 left, 2 right (AutoMapMode's own numbers)
    const char* const side = want == 1 ? "left" : "right";
    PWSTR dir = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &dir))) path = std::wstring(dir) + L"\\Diablo II Resurrected\\Settings.json";
    if (dir) CoTaskMemFree(dir);
    FILE* f = path.empty() ? nullptr : _wfopen(path.c_str(), L"rb");
    if (!f) return;
    std::string text(1 << 16, ' ');
    text.resize(fread(text.data(), 1, text.size(), f));
    fclose(f);
    const size_t at = text.find("\"AutoMapMode\"");
    const size_t colon = at == std::string::npos ? at : text.find(':', at);
    size_t b = colon == std::string::npos ? colon : colon + 1;
    while (b < text.size() && (text[b] == ' ' || text[b] == '\t')) ++b;
    size_t e = b;
    while (e < text.size() && isdigit((unsigned char)text[e])) ++e;
    if (b >= text.size() || e == b) {
        LogF("vrcam: the game's Settings.json has no AutoMapMode - its map's side left as the game has it (only the map on the "
             "%s comes into the hand: Options > Automap, the mini map on the %s)", side, side);
        return;
    }
    const int mode = atoi(text.c_str() + b);
    if (mode == want) return;
    text.replace(b, e - b, want == 1 ? "1" : "2");
    const DWORD attr = GetFileAttributesW(path.c_str());
    const bool readOnly = attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY);
    if (readOnly) SetFileAttributesW(path.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);
    if (FILE* w = _wfopen(path.c_str(), L"wb")) {
        fwrite(text.data(), 1, text.size(), w);
        fclose(w);
        LogF("vrcam: the game's AutoMapMode was %d (its map not on the %s - only that one comes into the hand, [hud] map_corner) - set to %d, "
             "the map on the %s%s ([hud] map=0 leaves it alone)", mode, side, want, side, readOnly ? ", its read-only flag taken off" : "");
    } else {
        LogF("vrcam: the game's AutoMapMode is %d (its map not on the %s - only that one comes into the hand, [hud] map_corner) and "
             "Settings.json could not be written: put the mini map on the %s in the game's options (Automap)", mode, side, side);
    }
}

// The game's windowed size, at the plugin's start - before the game reads Settings.json (shared/window_json.h, the policy and its
// tests). VR ([mode] platform=1, native OpenXR and FlatVR alike) has the square window (D2R VR Settings' "Square game window", written
// into Settings.json by the settings, kept by BigWindowTick). FLAT mode (the monitor) has none and uses the window and resolution the
// PLAYER set in the game: the mod chooses and writes no resolution there. A square left in Settings.json - by the settings for VR, or
// by the game itself at its quit (it saves the size its window had) - gives way in flat mode to the player's own window ([openxr]
// window_before, window_mode_before: kept before the square was first written, size and mode); with none kept nothing is written and
// the log says so. The player's own window is kept in every mode while it is the file's. Flat players of 0.166 started in the square:
// the shipped ini has square_window=1.
void ReconcileGameWindow() {
    PWSTR dir = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &dir))) path = std::wstring(dir) + L"\\Diablo II Resurrected\\Settings.json";
    if (dir) CoTaskMemFree(dir);
    FILE* f = path.empty() ? nullptr : _wfopen(path.c_str(), L"rb");
    if (!f) return;
    std::string text(1 << 16, ' ');
    text.resize(fread(text.data(), 1, text.size(), f));
    fclose(f);
    const winjson::Windowed now = winjson::Read(text);
    if (!now.ok) return;
    winjson::Own own;
    {
        wchar_t b[32] = L"";
        GetPrivateProfileStringW(L"openxr", L"window_before", L"", b, 32, g_iniPath);
        swscanf_s(b, L"%dx%d", &own.w, &own.h);
        own.mode = (int)GetPrivateProfileIntW(L"openxr", L"window_mode_before", -1, g_iniPath);
    }
    const bool vr = g_set.platform.load() == 1;
    const winjson::Plan plan = winjson::ForPlatform(vr, IniB(L"openxr", L"square_window", false), now, own);
    if (plan.saveOwn && (plan.own.w != own.w || plan.own.h != own.h || plan.own.mode != own.mode)) {
        wchar_t b[32];
        swprintf_s(b, L"%dx%d", plan.own.w, plan.own.h);
        WritePrivateProfileStringW(L"openxr", L"window_before", b, g_iniPath);
        swprintf_s(b, L"%d", plan.own.mode);
        WritePrivateProfileStringW(L"openxr", L"window_mode_before", b, g_iniPath);
        LogF("vrcam: the player's own window %dx%d (mode %d) kept in d2r_vr.ini ([openxr] window_before) before a square window", plan.own.w, plan.own.h, plan.own.mode);
    }
    if (plan.unknownOwn)
        LogF("vrcam: flat mode ([mode] platform=0): the game's windowed size is %dx%d (VR's square) and no window of yours was kept: the game uses it - set the "
             "window you want in the game's Options > Video (the mod chooses no resolution in flat mode)", now.w, now.h);
    if (!plan.write) return;
    bool ok = false;
    const std::string out = winjson::With(text, plan.w, plan.h, plan.mode, &ok);
    if (!ok || out == text) return;
    const DWORD attr = GetFileAttributesW(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY)) SetFileAttributesW(path.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);
    if (FILE* o = _wfopen(path.c_str(), L"wb")) {
        fwrite(out.data(), 1, out.size(), o);
        fclose(o);
        LogF("vrcam: flat mode ([mode] platform=0): the game's windowed size was %dx%d (VR's square) - %s: set to %dx%d%s", now.w, now.h, plan.why, plan.w, plan.h,
             plan.mode >= 0 ? ", its window mode too" : "");
    } else {
        LogF("vrcam: flat mode: the game's windowed size is %dx%d (VR's square) and Settings.json could not be written: set a window in the game's Options > Video", now.w, now.h);
    }
}

// The game's frame cap ("Framerate Cap" in Settings.json), at the plugin's start - before the game reads the file (shared/frame_cap.h,
// the policy and its tests). The headset's rate writes a cap in VR (D2R VR Settings: a game frame per headset frame) and that cap must
// not stay in force in flat mode (the monitor): FLAT uses the player's own - the cap VR wrote goes, the one kept before it ([stereo]
// cap_before) comes back; none kept: nothing is written, the log says so. In VR only the player's own is kept: a cap in the file that
// is not the one VR wrote last ([stereo] cap_written) is his.
void ReconcileFrameCap() {
    PWSTR dir = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &dir))) path = std::wstring(dir) + L"\\Diablo II Resurrected\\Settings.json";
    if (dir) CoTaskMemFree(dir);
    FILE* f = path.empty() ? nullptr : _wfopen(path.c_str(), L"rb");
    if (!f) return;
    std::string text(1 << 16, ' ');
    text.resize(fread(text.data(), 1, text.size(), f));
    fclose(f);
    const int now = framecap::Read(text);
    if (now < 0) return;
    framecap::State s;
    s.own = (int)GetPrivateProfileIntW(L"stereo", L"cap_before", -1, g_iniPath);
    s.written = (int)GetPrivateProfileIntW(L"stereo", L"cap_written", -1, g_iniPath);
    const bool vr = g_set.platform.load() == 1;
    framecap::Plan plan;
    if (vr) {
        if (now != s.written && now != s.own) { plan.saveOwn = true; plan.own = now; }
        // what the settings write for this headset rate and 3D mode, and why (one rule: shared/frame_cap.h VrCapFor)
        const int hz = framecap::HzOfRate((int)IniF(L"stereo", L"headset_rate", 3.0f));
        const framecap::VrCap c = framecap::VrCapFor(hz, IniB(L"stereo", L"afr", false), IniB(L"stereo", L"pair_per_tick", false), xr::On(),
                                                     IniF(L"flatvr", L"frame_sync", 0.0f) != 0.0f);
        LogF("vrcam: frame cap - the game's file has %d; for the headset's %d Hz in this 3D mode D2R VR Authentic Settings writes %d%s (%s)", now, hz, c.value,
             c.factor ? "" : " (none)", framecap::Reason(c.why));
    } else {
        plan = framecap::ForFlat(now, s);
    }
    if (plan.saveOwn) {
        wchar_t b[16];
        swprintf_s(b, L"%d", plan.own);
        WritePrivateProfileStringW(L"stereo", L"cap_before", b, g_iniPath);
        LogF("vrcam: the player's own frame cap %d kept in d2r_vr.ini ([stereo] cap_before) before VR's", plan.own);
    }
    if (plan.unknownOwn)
        LogF("vrcam: flat mode ([mode] platform=0): the game's frame cap is %d (VR's rate) and none of yours was kept: the game uses it - set the cap you want in the "
             "game's Options > Video (the mod chooses no frame cap in flat mode)", now);
    if (!plan.write) return;
    bool ok = false;
    const std::string out = framecap::With(text, plan.value, &ok);
    if (!ok || out == text) return;
    const DWORD attr = GetFileAttributesW(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY)) SetFileAttributesW(path.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);
    if (FILE* o = _wfopen(path.c_str(), L"wb")) {
        fwrite(out.data(), 1, out.size(), o);
        fclose(o);
        LogF("vrcam: flat mode ([mode] platform=0): the game's frame cap was %d (VR's rate) - %s: set to %d", now, plan.why, plan.value);
    } else {
        LogF("vrcam: flat mode: the game's frame cap is %d (VR's rate) and Settings.json could not be written: set the cap you want in the game's Options > Video", now);
    }
}

// The game's interface size ("Safe Screen Percent" in Settings.json), at the plugin's start - before the game reads the file (shared/safe_screen.h,
// the policy and its tests). At the game's default, 100, the interface is cut off at the edges in the headset (a player lowered it to 70 by hand):
// VR starts it at 80 where the player has chosen none (any value but 100 is his choice and stays) - [hud] safe_screen_vr, on - and keeps the
// 100 he had ([hud] safe_screen_before) and what VR wrote ([hud] safe_screen_written). FLAT mode (and the box unticked) has the player's own: VR's
// 80 goes, the kept value comes back; none kept: nothing is written, the log says so. Returns true when it wrote the file (the interface scale
// the toolbar's cut and the hand's globes are worked out from is read again by the caller).
bool ReconcileSafeScreen() {
    PWSTR dir = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &dir))) path = std::wstring(dir) + L"\\Diablo II Resurrected\\Settings.json";
    if (dir) CoTaskMemFree(dir);
    FILE* f = path.empty() ? nullptr : _wfopen(path.c_str(), L"rb");
    const bool vr = g_set.platform.load() == 1, box = IniB(L"hud", L"safe_screen_vr", true);
    if (!f) {
        if (vr && box) Log("vrcam: the game's Settings.json is not there yet (the game writes it when it quits the first time): its interface size is the game's 100 for this run, "
                           "VR starts it at 80 from the next one ([hud] safe_screen_vr)");
        return false;
    }
    std::string text(1 << 16, ' ');
    text.resize(fread(text.data(), 1, text.size(), f));
    fclose(f);
    const int now = safescreen::Read(text);
    if (now < 0) {
        if (vr && box) Log("vrcam: the game's Settings.json has no Safe Screen Percent: its interface size is left as the game has it (VR starts it at 80 once the key is there)");
        return false;
    }
    safescreen::State s;
    s.own = (int)GetPrivateProfileIntW(L"hud", L"safe_screen_before", -1, g_iniPath);
    s.written = (int)GetPrivateProfileIntW(L"hud", L"safe_screen_written", -1, g_iniPath);
    // VR: the picture's shape decides what VR starts at and whether two side panels (a trader or the mercenary on the left, the inventory on the right)
    // fit it: in a square picture they do up to 80 % (shared/safe_screen.h, the numbers of the game's own layout) - above it the outer columns are off the
    // picture's edges or under the other panel, and cannot be seen or pointed at; the headset's own, taller shape (2496 x 2688) needs 74.
    float aspect = 1.0f;
    if (vr) {
        wchar_t hp[MAX_PATH];
        wcscpy_s(hp, g_iniPath);
        if (wchar_t* slash = wcsrchr(hp, L'\\')) slash[1] = 0;
        wcscat_s(hp, L"d2r_vr_headset.ini");   // what the headset's runtime recommended at the last native session
        const rendersize::Pick pick = rendersize::FromIni((int)IniF(L"render", L"vr_resolution", 0.0f), (int)IniF(L"render", L"vr_resolution_px", 0.0f));
        const bool native = IniB(L"openxr", L"on", false);   // (FlatVR has no render size: its picture is the window)
        const winjson::Windowed win = winjson::Read(text);
        aspect = safescreen::PictureAspect(native && pick.kind == rendersize::Kind::Square, native && pick.kind == rendersize::Kind::Headset,
                                           (int)GetPrivateProfileIntW(L"headset", L"recommended_w", 0, hp), (int)GetPrivateProfileIntW(L"headset", L"recommended_h", 0, hp),
                                           IniB(L"openxr", L"square_window", false), win.ok ? win.w : 0, win.ok ? win.h : 0);
    }
    const safescreen::Plan plan = vr && box ? safescreen::ForVr(now, s, true, safescreen::VrValueFor(aspect)) : safescreen::ForFlat(now, s, false);
    auto fitLine = [&](int pct) {
        if (!vr) return;
        const int fit = safescreen::FitPercent(aspect);
        if (safescreen::PanelsFit(pct, aspect))
            LogF("vrcam: interface size %d%%: two open side panels (a trader or the mercenary and the inventory) fit the picture (its width %.2f of its height, up to %d%%)", pct, aspect, fit);
        else
            LogF("vrcam: interface size %d%% is too big for the picture (its width %.2f of its height): two open side panels (a trader or the mercenary and the inventory) overlap "
                 "or run off its edges, their outer columns cannot be reached - %d%% or less", pct, aspect, fit);
    };
    wchar_t b[16];
    if (plan.saveOwn) {
        swprintf_s(b, L"%d", plan.own);
        WritePrivateProfileStringW(L"hud", L"safe_screen_before", b, g_iniPath);
    }
    if (plan.saveWritten) {
        swprintf_s(b, L"%d", plan.written);
        WritePrivateProfileStringW(L"hud", L"safe_screen_written", b, g_iniPath);
    }
    if (plan.clearWritten) WritePrivateProfileStringW(L"hud", L"safe_screen_written", L"-1", g_iniPath);
    if (plan.unknownOwn)
        LogF("vrcam: the game's interface size is %d (VR's) and none of yours was kept: the game uses it - set the size you want in the game's Options > Video "
             "([hud] safe_screen_vr off keeps the game's own)", now);
    if (!plan.write) {
        if (vr && box && now != safescreen::kGameDefault)
            LogF("vrcam: the game's interface size is %d%% - %s: left as it is", now, now == s.written ? "VR's" : "yours (not the game's default 100)");
        fitLine(now);
        return false;
    }
    bool ok = false;
    const std::string out = safescreen::With(text, plan.value, &ok);
    if (!ok || out == text) return false;
    const DWORD attr = GetFileAttributesW(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY)) SetFileAttributesW(path.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);
    if (FILE* o = _wfopen(path.c_str(), L"wb")) {
        fwrite(out.data(), 1, out.size(), o);
        fclose(o);
        if (vr && box)
            LogF("vrcam: the game's interface size was the game's default %d%% - %s: set to %d%% for VR (your %d%% is kept in d2r_vr.ini and comes back in flat mode; "
                 "[hud] safe_screen_vr=0 leaves it alone)", now, plan.why, plan.value, now);
        else
            LogF("vrcam: flat mode: the game's interface size was %d%% (VR's) - %s: set to %d%%", now, plan.why, plan.value);
        fitLine(plan.value);
        return true;
    }
    LogF("vrcam: the game's interface size is %d%% and Settings.json could not be written: set it in the game's Options > Video", now);
    return false;
}

// D2R itself dies at its title screen with a window taller than about 2400 ("A Fixed sized vector
// tried to reallocate" in its blz-log.txt, then its own crash handler; 2600 and 3000 tried, 2026-10-11).
// Players were told on Discord to put 3000x3000 into Settings.json and make it read-only: every start
// died, and vrcam's start guard then switched native OpenXR off - flat on the monitor. At the plugin's
// start, before the game reads the file: a windowed size taller than 2400 is cut to 2400 (a square
// stays square), the read-only flag taken off first (it kept the bad size for good).
void SafeGameWindow() {
    if (g_set.platform.load() != 1) return;   // flat mode: the game's own window and resolution, nothing of ours written (the cut is VR's, for its squares)
    PWSTR dir = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &dir))) path = std::wstring(dir) + L"\\Diablo II Resurrected\\Settings.json";
    if (dir) CoTaskMemFree(dir);
    FILE* f = path.empty() ? nullptr : _wfopen(path.c_str(), L"rb");
    if (!f) return;
    std::string text(1 << 16, ' ');
    text.resize(fread(text.data(), 1, text.size(), f));
    fclose(f);
    const char* key = "\"Screen Resolution (Windowed)\"";
    const size_t at = text.find(key);
    const size_t q1 = at == std::string::npos ? at : text.find('"', text.find(':', at));
    const size_t q2 = q1 == std::string::npos ? q1 : text.find('"', q1 + 1);
    if (q2 == std::string::npos) return;
    int w = 0, h = 0;
    if (sscanf_s(text.c_str() + q1 + 1, "%dx%d", &w, &h) != 2) return;
    // the game builds its title screen at the size it starts with, and dies there when it is very tall
    // (3000 even on a 4000x4000 monitor; 2400 lives): it starts as tall as the main monitor holds, 2400 at most - D2R VR Settings' square
    // window grows it to its size after the title screen (BigWindowTick), on a monitor that holds it
    // and no taller than the main monitor holds (its work area less a frame): the menus need no more
    int limit = 2400;
    {
        MONITORINFO mi{sizeof mi};
        if (GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi))
            limit = std::max(480, std::min(limit, (int)(mi.rcWork.bottom - mi.rcWork.top) - 45));
    }
    if (h <= limit) return;
    const int nh = limit, nw = w == h ? limit : w;
    const std::string size = std::to_string(nw) + "x" + std::to_string(nh);
    text.replace(q1 + 1, q2 - q1 - 1, size);
    const DWORD attr = GetFileAttributesW(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY)) SetFileAttributesW(path.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);
    if (FILE* o = _wfopen(path.c_str(), L"wb")) {
        fwrite(text.data(), 1, text.size(), o);
        fclose(o);
        LogF("vrcam: the game's windowed size was %dx%d - taller than the main monitor holds (%d; D2R builds its title screen at "
             "that size and closes past ~2400): set to %s%s (D2R VR Authentic Settings' square window grows it in a game)", w, h, limit, size.c_str(),
             attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY) ? ", its read-only flag taken off" : "");
    } else {
        LogF("vrcam: the game's windowed size is %dx%d - taller than ~2400 D2R itself closes at its title screen, and Settings.json "
             "could not be written: set it to 2400x2400 or less by hand", w, h);
    }
}

}  // namespace vrc
