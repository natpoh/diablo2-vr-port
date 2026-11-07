// capture_test - the capture key's rules (vr/capture_rule.h): [debug] capture_key's spellings, the modifiers matched exactly, the repeat guard,
// the folder's name and which old folders go.

#include <cstdio>
#include <string>
#include <vector>

#include "capture_rule.h"

using namespace capturerule;

static int g_fail = 0, g_checks = 0;
#define CHECK(c, what)                                                     \
    do {                                                                   \
        ++g_checks;                                                        \
        if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, what); } \
    } while (0)

int main() {
    Key k;
    CHECK(Parse(L"", &k) && k.vk == 0x79 && k.ctrl && k.alt && !k.shift, "empty: Ctrl+Alt+F10");
    CHECK(Parse(L"Ctrl + Shift + F7", &k) && k.vk == 0x76 && k.ctrl && k.shift && !k.alt, "spaces and case");
    CHECK(Parse(L"alt+p", &k) && k.vk == 'P' && k.alt && !k.ctrl, "a letter");
    CHECK(Parse(L"ctrl+0x7A", &k) && k.vk == 0x7A && k.ctrl, "a hex code");
    CHECK(Parse(L"f24", &k) && k.vk == 0x70 + 23 && !k.ctrl && !k.alt, "F24, no modifiers");
    CHECK(Parse(L"none", &k) && k.vk == 0 && Parse(L"off", &k) && k.vk == 0 && Parse(L"0", &k) && k.vk == 0, "none / off / 0: no key");
    CHECK(!Parse(L"ctrl+f25", &k) && !Parse(L"meta+f1", &k) && !Parse(L"ctrl+", &k) && !Parse(L"ctrl+pageup", &k) && !Parse(L"f100", &k), "not understood");
    CHECK(Name(Default()) == "Ctrl+Alt+F10" && Name(Key{}) == "none", "names");
    const Key d = Default();
    CHECK(Matches(d, true, true, false) && !Matches(d, true, false, false) && !Matches(d, true, true, true) && !Matches(Key{}, false, false, false),
          "the modifiers exactly: Ctrl+F10's trace and Ctrl+Alt+Shift+F10 are not it");
    CHECK(MayCapture(10000, 0) && !MayCapture(12000, 10000) && MayCapture(13000, 10000), "one capture per 3 s");
    CHECK(FolderName(2026, 11, 7, 9, 5, 3) == L"capture_20261107-090503", "the folder's name");
    CHECK(IsOurs(L"capture_20261107-090503") && !IsOurs(L"capture_20261107_090503") && !IsOurs(L"capture_x") && !IsOurs(L"sessions"), "ours only");
    std::vector<std::wstring> names = {L"sessions", L"capture_20261107-090503", L"capture_20261101-000000", L"capture_20261102-000000",
                                       L"capture_20261103-000000", L"capture_20261104-000000", L"capture_20261105-000000", L"capture_old"};
    const auto del = ToDelete(names);
    CHECK(del.size() == 1 && del[0] == L"capture_20261101-000000", "the newest five kept, the oldest goes, others never");
    names.resize(4);
    CHECK(ToDelete(names).empty(), "five or fewer: nothing goes");
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
