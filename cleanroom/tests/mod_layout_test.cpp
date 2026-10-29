// Offline checks of shared/mod_layout.h: D2R VR as the D2RLoader mod D2R-VR - which folder is the mod's, the game's, ReShade's.
// Run: mod_layout_test.exe (exit 0 = all passed).
#include <cstdio>
#include <string>

#include "mod_layout.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main() {
    using namespace modlayout;
    const std::wstring game = L"D:\\Steam\\steamapps\\common\\Diablo II Resurrected\\";
    const std::wstring modPlugins = game + L"mods\\D2R-VR\\d2rloader\\plugins\\";
    const std::wstring oldPlugins = game + L"d2rloader\\plugins\\";
    // the mod layout
    CHECK(ModRootOfPluginDir(modPlugins) == game + L"mods\\D2R-VR\\");
    CHECK(ModRootOfPluginDir(game + L"mods\\D2R-VR\\d2rloader\\plugins") == game + L"mods\\D2R-VR\\");   // no last backslash
    CHECK(ModRootOfPluginDir(game + L"MODS\\D2R-VR\\D2RLoader\\Plugins\\") == game + L"MODS\\D2R-VR\\");  // any case
    CHECK(GameOfPluginDir(modPlugins) == game);
    // the old layout: no mod, the game as before
    CHECK(ModRootOfPluginDir(oldPlugins).empty());
    CHECK(GameOfPluginDir(oldPlugins) == game);
    // neither
    CHECK(ModRootOfPluginDir(game).empty() && GameOfPluginDir(game).empty());
    CHECK(ModRootOfPluginDir(L"").empty() && ModRootOfPluginDir(L"d2rloader\\plugins\\").empty());
    CHECK(ModRootOfPluginDir(game + L"stuff\\D2R-VR\\d2rloader\\plugins\\").empty());   // a plugins folder not under mods\ is no mod
    // ReShade's folder: the mod's own as the mod - with or without its ReShade64.dll yet (a ReShade64.dll beside the game is not ours
    // then) - unless the player's dxgi.dll is beside the game (its base is the game's folder); the game's in the old layout
    CHECK(ReShadeDir(game, ModRootOfPluginDir(modPlugins), false) == game + L"mods\\D2R-VR\\reshade\\");
    CHECK(ReShadeDir(game, ModRootOfPluginDir(modPlugins), true) == game);
    CHECK(ReShadeDir(game, L"", true) == game);
    CHECK(ReShadeDir(L"D:\\G", L"", false) == L"D:\\G\\");
    // D2R VR's own ReShade files (effect, pictures, add-on): in the mod's reshade\ whoever loads ReShade - even with no ReShade64.dll in it (the player's
    // dxgi.dll finds them through its search paths), else the game's folder
    CHECK(OurFilesDir(game, ModRootOfPluginDir(modPlugins)) == game + L"mods\\D2R-VR\\reshade\\");
    CHECK(OurFilesDir(game, L"") == game);
    CHECK(OurFilesDir(L"D:\\G", L"") == L"D:\\G\\");
    // the mod's folders from the game's, and D2RLoader's arguments
    CHECK(ModRoot(game) == game + L"mods\\D2R-VR\\");
    CHECK(ModPlugins(game) == modPlugins);
    CHECK(ModReShade(L"D:\\G") == L"D:\\G\\mods\\D2R-VR\\reshade\\");
    CHECK(LaunchArguments() == L"-mod D2R-VR");
    // another VR mod's folder is not ours
    CHECK(ModRootOfPluginDir(game + L"mods\\D2R-PCVR\\d2rloader\\plugins\\") == game + L"mods\\D2R-PCVR\\");
    CHECK(ModRootOfPluginDir(game + L"mods\\D2R-PCVR\\d2rloader\\plugins\\") != ModRoot(game));
    std::printf("mod_layout_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
