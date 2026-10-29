// Offline checks of tools/settings/lang.h: D2R VR Settings' language files read the way the program reads them - UTF-8 with and without
// a BOM, UTF-16 (Notepad's "Unicode"), comments, blank and broken lines, \n and backslashes, {placeholders} - and which file fits the
// language Windows names. A missing key is the program's English (TR in settings.cpp): here, a Table without it.
// Run: build/Release/settings_lang_test.exe (exit code 0 = passed)

#include <cstdio>
#include <string>
#include <vector>

#include "../../tools/settings/lang.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

// what TR does with a table: the file's text, or the English when the key is missing or empty
static std::wstring Tr(const lang::Table& t, const wchar_t* key, const wchar_t* en) {
    const auto it = t.find(key);
    return it != t.end() && !it->second.empty() ? it->second : std::wstring(en);
}

int main() {
    using namespace lang;
    // UTF-8 as Notepad writes it today (no BOM), CRLF; comments with # and ;, a blank line, a line without "=", spaces round key and text
    const std::string plain =
        "# D2R VR Settings - Deutsch\r\n"
        "; another comment = not a key\r\n"
        "\r\n"
        "language.name = Deutsch\r\n"
        "  tab.home   =   Start  \r\n"
        "this line has no equals sign\r\n"
        "= no key\r\n"
        "@run.d2r_launch.label = START\r\n"
        "group.how_you_play = Wie du spielst\r\n";
    const Table t = ParseBytes(plain);
    CHECK(t.size() == 4);
    CHECK(t.at(L"language.name") == L"Deutsch");
    CHECK(t.at(L"tab.home") == L"Start");
    CHECK(t.at(L"@run.d2r_launch.label") == L"START");
    CHECK(t.count(L"; another comment") == 0 && t.count(L"this line has no equals sign") == 0);
    // a missing key, and an empty text, give the English
    CHECK(Tr(t, L"group.how_you_play", L"How you play") == L"Wie du spielst");
    CHECK(Tr(t, L"group.in_the_headset", L"In the headset") == L"In the headset");
    CHECK(Tr(ParseBytes("tab.home =\n"), L"tab.home", L"Home") == L"Home");
    CHECK(Tr(Table(), L"tab.home", L"Home") == L"Home");   // no file at all
    // the same with a UTF-8 BOM, LF only, and no new line at the end: the BOM is not part of the first key
    const Table bom = ParseBytes("\xEF\xBB\xBFtab.home = \xD0\x93\xD0\xBB\xD0\xB0\xD0\xB2\xD0\xBD\xD0\xB0\xD1\x8F\nlanguage.name = \xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9");
    CHECK(bom.size() == 2);
    CHECK(bom.count(L"tab.home") == 1);
    CHECK(bom.at(L"tab.home") == L"\u0413\u043B\u0430\u0432\u043D\u0430\u044F");   // Главная
    CHECK(bom.at(L"language.name") == L"\u0420\u0443\u0441\u0441\u043A\u0438\u0439");
    // a BOM line that starts with a comment is still a comment
    CHECK(ParseBytes("\xEF\xBB\xBF# only a comment\n").empty());
    // CJK, four-byte UTF-8 (a surrogate pair), and the round trip back to UTF-8
    const Table zh = ParseBytes("tab.home = \xE4\xB8\xBB\xE9\xA1\xB5\nx = \xF0\x9F\x8E\xAE");
    CHECK(zh.at(L"tab.home") == L"\u4E3B\u9875");
    CHECK(zh.at(L"x").size() == 2 && zh.at(L"x")[0] == 0xD83C && zh.at(L"x")[1] == 0xDFAE);
    CHECK(ToUtf8(zh.at(L"tab.home")) == "\xE4\xB8\xBB\xE9\xA1\xB5");
    CHECK(ToUtf8(zh.at(L"x")) == "\xF0\x9F\x8E\xAE");
    // a byte that is not UTF-8 spoils only itself
    const Table bad = ParseBytes("a = x\xFFy\nb = ok\nc = cut \xE4\xB8");
    CHECK(bad.at(L"a") == L"x\uFFFDy");
    CHECK(bad.at(L"b") == L"ok");
    CHECK(bad.at(L"c").substr(0, 4) == L"cut ");
    // UTF-16 LE with its BOM (Notepad's "Unicode"), and BE
    std::string le = "\xFF\xFE", be = "\xFE\xFF";
    for (wchar_t c : std::wstring(L"tab.home = \u00DCbersicht\r\n# c\r\nk = v")) {
        le += (char)(c & 0xFF); le += (char)(c >> 8);
        be += (char)(c >> 8); be += (char)(c & 0xFF);
    }
    CHECK(ParseBytes(le).at(L"tab.home") == L"\u00DCbersicht");
    CHECK(ParseBytes(le).at(L"k") == L"v" && ParseBytes(le).size() == 2);
    CHECK(ParseBytes(be).at(L"tab.home") == L"\u00DCbersicht");
    // the text after the FIRST "=" (an "=" in the text stays), the later line of a key wins
    const Table eq = ParseBytes("k = a = b\nk2 = one\nk2 = two\n");
    CHECK(eq.at(L"k") == L"a = b");
    CHECK(eq.at(L"k2") == L"two");
    // \n is a new line, \\ a backslash, any other backslash stays (paths)
    const Table esc = ParseBytes("t = one\\ntwo\np = mods\\D2R-VR\\reshade\nq = a\\\\nb\nr = ends\\\n");
    CHECK(esc.at(L"t") == L"one\ntwo");
    CHECK(esc.at(L"p") == L"mods\\D2R-VR\\reshade");
    CHECK(esc.at(L"q") == L"a\\nb");
    CHECK(esc.at(L"r") == L"ends\\");
    // Escape is Unescape's way back, for whatever text
    for (const wchar_t* s : {L"one\ntwo", L"mods\\D2R-VR\\reshade", L"a\\nb", L"c:\\new\\\\x", L"ends\\", L"plain"})
        CHECK(Unescape(Escape(s)) == s);
    CHECK(Escape(L"mods\\D2R-VR") == L"mods\\D2R-VR");   // a plain path is written as it is
    // {placeholders}: filled by name, in whatever order a translation puts them; unknown or broken ones stay
    CHECK(Fill(L"The game window: {w} x {h}", {{L"w", L"2000"}, {L"h", L"1800"}}) == L"The game window: 2000 x 1800");
    CHECK(Fill(L"{h} \u00D7 {w}", {{L"w", L"1"}, {L"h", L"2"}}) == L"2 \u00D7 1");
    CHECK(Fill(L"{w}{w}", {{L"w", L"x"}}) == L"xx");
    CHECK(Fill(L"a {nope} b {", {{L"w", L"x"}}) == L"a {nope} b {");
    CHECK(Fill(L"no braces", {}) == L"no braces");
    CHECK((Placeholders(L"{w} x {h}, {w}; {not one} {} {x_2}") == std::vector<std::wstring>{L"w", L"h", L"w", L"x_2"}));
    // keys made from English titles
    CHECK(Slug(L"How you play") == L"how_you_play");
    CHECK(Slug(L"View (F1 - F4 in the game)") == L"view_f1_f4_in_the_game");
    CHECK(Slug(L"  UI: F4 body ") == L"ui_f4_body");
    CHECK(Slug(L"Each kind of weapon (type a number; 0 = follow the general one)", 20) == L"each_kind_of_weapon");
    // the file for Windows' language: the same code, the language alone, the first of that language, else none (English)
    const std::vector<std::wstring> files = {L"de", L"es", L"fr", L"pt-BR", L"ru", L"uk", L"zh-Hans"};
    CHECK(Pick(L"de-DE", files) == L"de");
    CHECK(Pick(L"DE-at", files) == L"de");
    CHECK(Pick(L"pt-BR", files) == L"pt-BR");
    CHECK(Pick(L"pt-PT", files) == L"pt-BR");
    CHECK(Pick(L"zh-CN", files) == L"zh-Hans");
    CHECK(Pick(L"ru-RU", files) == L"ru");
    CHECK(Pick(L"uk-UA", files) == L"uk");
    CHECK(Pick(L"en-US", files).empty());
    CHECK(Pick(L"ja-JP", files).empty());
    CHECK(Pick(L"", files).empty());
    CHECK(Pick(L"de-DE", {}).empty());
    if (g_failures) { std::printf("%d check(s) FAILED\n", g_failures); return 1; }
    std::printf("settings_lang_test: all %d checks passed\n", g_checks);
    return 0;
}
