// Offline checks of the headset's words in the player's language ([hud] headset_language): vr/ui_text.h (the lookup, English when there is no
// table, no key or an empty text; {placeholders} in a translation's order), vr/ui_text_tables.h (every table text has its key in lang\en.txt
// with that English, no key twice with two Englishes), the pages as vr/hset_scene.h composes them (English unchanged without a table, the
// translation with one, a line of the guide counted in letters and a text without spaces cut), and vr/ui_lang.cpp (the key off = English,
// the language D2R VR Settings picked read from the mod's lang folder, Chinese given Microsoft YaHei) in a scratch folder.
// Run: build/Release/ui_text_test.exe (exit code 0 = passed); it reads lang\en.txt of the source tree (D2RVR_SOURCE_DIR).

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>

#include "hset_scene.h"
#include "ui_lang.h"
#include "ui_text.h"
#include "ui_text_tables.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using namespace hset;

static Model OnPage(int page) {
    Snapshot s;
    s.ready = true;
    s.values[kKeyHide] = s.file[kKeyHide] = 1.0f;   // the toolbar cut out: bar_place's default 5 is "Wrists"
    Model m;
    m.Open(s);
    m.Click({Kind::Tab, page, 0});
    return m;
}
static bool SceneHas(const Scene& sc, const std::string& text) {
    for (const Item& it : sc)
        if (it.type != Item::Type::Fill && it.text == text) return true;
    return false;
}

static std::string ReadFile(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
static void WriteFile(const std::wstring& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
}

static void TestLookup() {
    // no table: the English given, the very pointer
    uitext::Set(nullptr, nullptr);
    const char* en = "Apply";
    CHECK(uitext::Tr("vr.hset.apply", en) == en);
    CHECK(uitext::TrF("vr.x", "Page {page} of {pages}", {{"page", "2"}, {"pages", "5"}}) == "Page 2 of 5");
    CHECK(uitext::TrW("vr.x", "ROTATE MAP") == L"ROTATE MAP");
    CHECK(std::wcscmp(uitext::Face(L"Georgia"), L"Georgia") == 0);
    // a table: its text; an empty text or a key it lacks: the English
    static const uitext::Table t = {{"vr.hset.apply", "Применить"}, {"vr.empty", ""}, {"vr.page", "Seite {pages}: {page}"}};
    uitext::Set(&t, L"Microsoft YaHei");
    CHECK(std::string(uitext::Tr("vr.hset.apply", en)) == "Применить");
    CHECK(uitext::Tr("vr.empty", en) == en && uitext::Tr("vr.missing", en) == en);
    CHECK(uitext::TrF("vr.page", "Page {page} of {pages}", {{"page", "2"}, {"pages", "5"}}) == "Seite 5: 2");   // the translation's order
    CHECK(uitext::TrW("vr.hset.apply", "Apply") == L"Применить");
    CHECK(std::wcscmp(uitext::Face(L"Georgia"), L"Microsoft YaHei") == 0 && std::wcscmp(uitext::Face(L"Segoe UI"), L"Microsoft YaHei") == 0);
    CHECK(uitext::Fill("{a} {b} {c} {", {{"a", "1"}, {"b", "x"}}) == "1 x {c} {");   // an unknown name, a lone brace: as they are
    uitext::Set(nullptr, nullptr);
    CHECK(uitext::Slug("Trigger + grip") == "trigger_grip" && uitext::Slug("cmd:0x25, action:map") == "cmd_0x25_action_map" && uitext::Slug("HUD") == "hud");
    CHECK(uitext::Letters("abc") == 3 && uitext::Letters("Применить") == 9 && uitext::Letters("设置") == 2);
}

// Every table text: a "vr." key, one English a key, and lang\en.txt has it with that English (python tools/lang_sync.py en --exe ... made it).
static void TestTables(const std::wstring& root) {
    const lang::Table en = lang::ParseBytes(ReadFile(root + L"\\lang\\en.txt"));
    CHECK(en.size() > 3000);   // (the file was read)
    std::map<std::string, std::string> seen;
    int n = 0, missing = 0, conflicts = 0;
    uitext::ForEachTableText([&](const std::string& key, const char* text) {
        ++n;
        CHECK(key.rfind("vr.", 0) == 0);
        const auto at = seen.find(key);
        if (at != seen.end() && at->second != text) { ++conflicts; std::printf("  %s: two Englishes\n", key.c_str()); }
        seen[key] = text;
        const auto e = en.find(lang::FromUtf8(key));
        if (e == en.end() || e->second != lang::FromUtf8(text)) { ++missing; if (missing <= 10) std::printf("  not in lang\\en.txt as it is: %s\n", key.c_str()); }
    });
    std::printf("  %d table texts\n", n);
    CHECK(n > 150 && conflicts == 0 && missing == 0);
    // the en.txt part the plugin's code writes: keys of their own (the "vr." ones lang_sync found in vr/), English in each
    int vr = 0;
    for (const auto& kv : en) vr += kv.first.rfind(L"vr.", 0) == 0;
    CHECK(vr > n + 150);
}

// The pages: English as before without a table; with one, the translation - the panel's own words, the tables', the guide's.
static void TestScene() {
    uitext::Set(nullptr, nullptr);
    Model opt = OnPage(kPageOptions);
    Scene sc = Compose(opt, {});
    CHECK(SceneHas(sc, "HEADSET SETTINGS") && SceneHas(sc, "Options") && SceneHas(sc, "Laser on menus") && SceneHas(sc, "Apply") && SceneHas(sc, "Off"));
    CHECK(SceneHas(sc, "Point and pull the trigger. B, or a long press of the right stick, closes it."));
    Model hud = OnPage(kPageHud);
    CHECK(SceneHas(Compose(hud, {}), "Wrists"));
    body::Stored st;
    st.any = st.valid = st.use = st.eyeValid = true;
    st.eye = 1.62f;
    st.span = 1.71f;
    CHECK(body::MeasuredText(st) == "Eye height 1.62 m, arm span 1.71 m" && body::SpanText(1.66f, false) == "1.66 m (auto)");
    CHECK(video::PresetLabel(0) == std::string("Window's size") && video::PresetLabel(3) == std::string("2048"));
    CHECK(keys::PickName(keys::CustomPick()) == "Custom (ini)");

    static const uitext::Table t = {
        {"vr.hset.title", "НАСТРОЙКИ В ШЛЕМЕ"},
        {PageKey(kPageOptions), "Опции"},
        {DefKey(IndexOf(L"openxr", L"laser"), "label"), "Лазер на меню"},
        {ValueKey("Off"), "Выкл"},
        {"vr.hset.apply", "Применить"},
        {"vr.hset.status.idle", "Наведите и нажмите курок."},
        {ToolbarKey(0), "Запястья"},
        {"vr.video.preset_window", "Размер окна"},
        {"vr.keys.custom", "Своё (ini)"},
        {firststart::GuideKey(1, "title"), "Ходить, поворачивать, сражаться"},
        {"vr.body.measured", "Рост глаз {eye} м, размах рук {span} м"},
    };
    uitext::Set(&t, nullptr);
    sc = Compose(opt, {});
    CHECK(SceneHas(sc, "НАСТРОЙКИ В ШЛЕМЕ") && SceneHas(sc, "Опции") && SceneHas(sc, "Лазер на меню") && SceneHas(sc, "Применить") && SceneHas(sc, "Выкл"));
    CHECK(SceneHas(sc, "Наведите и нажмите курок.") && !SceneHas(sc, "HEADSET SETTINGS"));
    CHECK(SceneHas(sc, "Close") && SceneHas(sc, "Recentre view"));   // what the file lacks: English
    CHECK(SceneHas(Compose(hud, {}), "Запястья"));
    CHECK(video::PresetLabel(0) == std::string("Размер окна") && keys::PickName(keys::CustomPick()) == "Своё (ini)");
    CHECK(body::MeasuredText(st) == "Рост глаз 1.62 м, размах рук 1.71 м");
    Model guide = OnPage(kPageGuide);
    guide.guide = 1;
    CHECK(SceneHas(Compose(guide, {}), "Ходить, поворачивать, сражаться"));
    uitext::Set(nullptr, nullptr);
    CHECK(SceneHas(Compose(guide, {}), "Walk, turn, fight"));
}

// The guide's lines: counted in letters (Cyrillic is two bytes a letter), a text without spaces cut at a letter; English as before.
static void TestWrap() {
    const std::vector<std::string> en = WrapWords("the quick brown fox jumps over the lazy dog", 10);
    CHECK(en.size() == 5 && en[0] == "the quick" && en[1] == "brown fox");
    const std::vector<std::string> ru = WrapWords("один два три", 8);   // 8 letters, 15 bytes for "один два"
    CHECK(ru.size() == 2 && ru[0] == "один два" && ru[1] == "три");
    const std::vector<std::string> zh = WrapWords("指向一个技能并扣动扳机把它放到槽位上", 8);
    bool fits = zh.size() == 3;
    for (const std::string& l : zh) fits = fits && uitext::Letters(l) <= 8;
    CHECK(fits && zh[0] == "指向一个技能并扣");
    for (int p = 0; p < firststart::kPageCount; ++p)   // every English line of the guide fits its place as before (hset_test measures them too)
        for (const GuideRowLayout& l : GuideRows(p)) {
            for (const std::string& s : l.text) CHECK(uitext::Letters(s) <= (l.named ? kGuideTextChars : kGuideWideChars));
            for (const std::string& s : l.control) CHECK(uitext::Letters(s) <= kGuideControlChars);
        }
}

// vr/ui_lang.cpp in a scratch mod folder: mods\D2R-VR\d2rloader\plugins\d2r_vr.ini (+ d2r_vr_settings.ini), mods\D2R-VR\lang\<code>.txt.
static void TestLoad() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring game = std::wstring(tmp) + L"ui_text_test_" + std::to_wstring(GetCurrentProcessId()) + L"\\";
    const std::wstring mod = game + L"mods\\D2R-VR\\", plugins = mod + L"d2rloader\\plugins\\", langDir = mod + L"lang\\";
    for (const std::wstring& d : {game, game + L"mods\\", mod, mod + L"d2rloader\\", plugins, langDir}) CreateDirectoryW(d.c_str(), nullptr);
    const std::wstring ini = plugins + L"d2r_vr.ini", uiIni = plugins + L"d2r_vr_settings.ini";
    WriteFile(langDir + L"en.txt", "language.name = English\nvr.hset.apply = Apply\n");
    WriteFile(langDir + L"ru.txt", "\xEF\xBB\xBFlanguage.name = Русский\r\ntab.home = Главная\r\nvr.hset.apply = Применить\r\nvr.hset.close =\r\n");
    WriteFile(langDir + L"zh-Hans.txt", "language.name = 简体中文\nvr.hset.apply = 应用\n");
    WriteFile(langDir + L"README.txt", "not a language\n");

    // off (no key): English, whatever Settings picked
    WriteFile(ini, "[hud]\r\nmap=1\r\n");
    WriteFile(uiIni, "[settings]\r\nlanguage=ru\r\n");
    std::string log = uilang::Load(ini.c_str());
    CHECK(log.find("English") != std::string::npos && std::strcmp(uitext::Tr("vr.hset.apply", "Apply"), "Apply") == 0);
    CHECK(uilang::Load(ini.c_str()).empty());   // nothing changed: not read again
    // on: Settings' language
    WriteFile(ini, "[hud]\r\nheadset_language=1\r\n");
    log = uilang::Load(ini.c_str());
    std::printf("  %s\n", log.c_str());
    CHECK(std::strcmp(uitext::Tr("vr.hset.apply", "Apply"), "Применить") == 0);
    CHECK(std::strcmp(uitext::Tr("vr.hset.close", "Close"), "Close") == 0);   // empty in the file: English
    CHECK(std::strcmp(uitext::Tr("tab.home", "Home"), "Home") == 0);          // the program's keys are not the plugin's
    CHECK(std::wcscmp(uitext::Face(L"Georgia"), L"Georgia") == 0);           // Georgia has Cyrillic
    // Chinese: its font
    WriteFile(uiIni, "[settings]\r\nlanguage=zh-Hans\r\n");
    log = uilang::Load(ini.c_str());
    std::printf("  %s\n", log.c_str());
    CHECK(std::strcmp(uitext::Tr("vr.hset.apply", "Apply"), "应用") == 0 && std::wcscmp(uitext::Face(L"Georgia"), L"Microsoft YaHei") == 0);
    // English picked in Settings, a code with no file: English
    WriteFile(uiIni, "[settings]\r\nlanguage=en\r\n");
    uilang::Load(ini.c_str());
    CHECK(std::strcmp(uitext::Tr("vr.hset.apply", "x"), "x") == 0 && std::wcscmp(uitext::Face(L"Georgia"), L"Georgia") == 0);
    WriteFile(uiIni, "[settings]\r\nlanguage=pl\r\n");
    uilang::Load(ini.c_str());
    CHECK(std::strcmp(uitext::Tr("vr.hset.apply", "x"), "x") == 0);
    // switched off again
    WriteFile(uiIni, "[settings]\r\nlanguage=ru\r\n");
    uilang::Load(ini.c_str());
    CHECK(std::strcmp(uitext::Tr("vr.hset.apply", "x"), "Применить") == 0);
    WriteFile(ini, "[hud]\r\nheadset_language=0\r\n");
    uilang::Load(ini.c_str());
    CHECK(std::strcmp(uitext::Tr("vr.hset.apply", "x"), "x") == 0);

    for (const wchar_t* f : {L"en.txt", L"ru.txt", L"zh-Hans.txt", L"README.txt"}) DeleteFileW((langDir + f).c_str());
    DeleteFileW(ini.c_str());
    DeleteFileW(uiIni.c_str());
    for (const std::wstring& d : {langDir, plugins, mod + L"d2rloader\\", mod, game + L"mods\\", game}) RemoveDirectoryW(d.c_str());
}

int main(int argc, char** argv) {
    std::wstring root = lang::FromUtf8(D2RVR_SOURCE_DIR);
    if (argc > 1) root = lang::FromUtf8(argv[1]);
    TestLookup();
    TestTables(root);
    TestScene();
    TestWrap();
    TestLoad();
    uitext::Set(nullptr, nullptr);
    std::printf("ui_text_test: %d of %d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures ? 1 : 0;
}
