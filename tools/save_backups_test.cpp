// [support] save_backups' rules (vr/diag/save_backups_rule.h) on made-up files and folders: which files, a good save, when to back up,
// the names, which old folders go.
//   save_backups_test   (exit code 0 = all passed)
#include "diag/save_backups_rule.h"

#include <cstdio>

using namespace vrc::savebackupsrule;

static int g_failed = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAILED line %d: %s\n", __LINE__, #c); ++g_failed; } } while (0)

static void Put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = (uint8_t)(v >> (8 * i));
}

// A character as the game writes one: magic, version, its length, its checksum, then some bytes.
static std::vector<uint8_t> D2s(size_t n) {
    std::vector<uint8_t> b(n, 0);
    b[0] = 0x55; b[1] = 0xAA; b[2] = 0x55; b[3] = 0xAA;
    Put32(b, 4, 0x69);
    Put32(b, 8, (uint32_t)n);
    for (size_t i = 16; i < n; ++i) b[i] = (uint8_t)(i * 7 + 3);
    Put32(b, 12, D2sChecksum(b.data(), n));
    return b;
}

// A shared stash: sections of the given lengths, each 55 AA 55 AA with its length at 16.
static std::vector<uint8_t> D2i(std::vector<uint32_t> lens) {
    std::vector<uint8_t> b;
    for (uint32_t len : lens) {
        const size_t at = b.size();
        b.resize(at + len, 0);
        b[at] = 0x55; b[at + 1] = 0xAA; b[at + 2] = 0x55; b[at + 3] = 0xAA;
        Put32(b, at + 4, 2);
        Put32(b, at + 8, 0x69);
        Put32(b, at + 16, len);
    }
    return b;
}

static bool Ok(const wchar_t* name, const std::vector<uint8_t>& b) { return Valid(name, b.data(), b.size()); }

int main() {
    // which files
    for (const wchar_t* n : {L"Bar.d2s", L"BAR.D2S", L"ModernSharedStashSoftCoreV2.d2i", L"Bar.ctl", L"Bar.key", L"Bar.map", L"Bar.d2rl",
                             L"Bar.ma0", L"Bar.ma3", L"Bar.ma12"})
        CHECK(WantFile(n));
    for (const wchar_t* n : {L"Bar0.ctlo", L"Bar0.keyo", L"Settings.json", L"AchievementTracker.json", L"Bar.ma", L"Bar.ma123",
                             L"Bar.max", L"Bar.mab", L"ModernSharedStashSoftCoreV2.d2i.broken_1018", L"Bar.d2s.tmp", L".d2s", L"Bar"})
        CHECK(!WantFile(n));

    // a good character; zeroed, cut short, a byte changed, the length wrong
    std::vector<uint8_t> c = D2s(3758);
    CHECK(Ok(L"Bar.d2s", c));
    CHECK(!Ok(L"Bar.d2s", std::vector<uint8_t>(3758, 0)));   // the power cut
    CHECK(!Ok(L"Bar.d2s", {}));
    std::vector<uint8_t> cut(c.begin(), c.begin() + 2000);
    CHECK(!Ok(L"Bar.d2s", cut));
    std::vector<uint8_t> flip = c;
    flip[1000] ^= 1;
    CHECK(!Ok(L"Bar.d2s", flip));
    std::vector<uint8_t> tail = c;   // its second half zeroed (a cut in the middle of the disk's write)
    for (size_t i = 1879; i < tail.size(); ++i) tail[i] = 0;
    CHECK(!Ok(L"Bar.d2s", tail));
    std::vector<uint8_t> nomagic = c;
    nomagic[0] = 0;
    CHECK(!Ok(L"Bar.d2s", nomagic));

    // a stash as Serg's: six sections of 68 and one of 148 = 556 bytes
    std::vector<uint8_t> s = D2i({68, 68, 68, 68, 68, 68, 148});
    CHECK(s.size() == 556);
    CHECK(Ok(L"ModernSharedStashSoftCoreV2.d2i", s));
    CHECK(!Ok(L"ModernSharedStashSoftCoreV2.d2i", std::vector<uint8_t>(556, 0)));
    std::vector<uint8_t> scut(s.begin(), s.begin() + 500);
    CHECK(!Ok(L"ModernSharedStashSoftCoreV2.d2i", scut));
    std::vector<uint8_t> sgap = s;
    for (size_t i = 136; i < 204; ++i) sgap[i] = 0;   // the third section zeroed
    CHECK(!Ok(L"ModernSharedStashSoftCoreV2.d2i", sgap));
    std::vector<uint8_t> slong = s;
    slong.push_back(1);   // a byte after the last section
    CHECK(!Ok(L"ModernSharedStashSoftCoreV2.d2i", slong));

    // the others: anything not zero
    CHECK(Ok(L"Bar.map", {0x0c, 0, 0, 0, 3, 0, 0, 0}));
    CHECK(Ok(L"Bar.ma0", {0, 0, 0, 0, 0, 0x90, 0, 0}));
    CHECK(!Ok(L"Bar.ctl", std::vector<uint8_t>(900, 0)));
    CHECK(!Ok(L"Bar.key", {}));

    // when to back up (ticks: 10 000 000 a second)
    const uint64_t sec = 10000000ull, t0 = 1000 * sec;
    std::vector<FileState> saves = {{L"Bar.d2s", 3758, t0}, {L"Bar.ctl", 900, t0}};
    States last = ByKey(saves);
    CHECK(Changed(saves, last, last, t0 + 60 * sec, 5 * sec) == Decide::Same);
    std::vector<FileState> gone = {{L"Bar.d2s", 3758, t0}};   // a character deleted: no new backup
    CHECK(Changed(gone, last, last, t0 + 60 * sec, 5 * sec) == Decide::Same);
    std::vector<FileState> wrote = {{L"BAR.D2S", 3760, t0 + 58 * sec}, {L"Bar.ctl", 900, t0}};
    CHECK(Changed(wrote, last, last, t0 + 60 * sec, 5 * sec) == Decide::Wait);   // 2 s ago, the poll before saw it otherwise
    CHECK(Changed(wrote, last, ByKey(wrote), t0 + 60 * sec, 5 * sec) == Decide::Go);   // the same as 15 s ago: still
    CHECK(Changed(wrote, last, last, t0 + 64 * sec, 5 * sec) == Decide::Go);   // 6 s ago
    std::vector<FileState> added = {{L"Bar.d2s", 3758, t0}, {L"Bar.ctl", 900, t0}, {L"New.d2s", 1100, t0 + 30 * sec}};
    CHECK(Changed(added, last, last, t0 + 60 * sec, 5 * sec) == Decide::Go);
    std::vector<FileState> future = {{L"Bar.d2s", 3758, t0 + 3600 * sec}, {L"Bar.ctl", 900, t0}};   // a clock put back
    CHECK(Changed(future, last, last, t0 + 60 * sec, 5 * sec) == Decide::Wait);
    CHECK(Changed(future, last, ByKey(future), t0 + 75 * sec, 5 * sec) == Decide::Go);
    CHECK(Changed(wrote, last, last, t0 + 58 * sec, 0) == Decide::Go);   // the end: no wait

    // names
    CHECK(FolderName(2026, 11, 3, 9, 5, 7) == L"2026-11-03_09-05-07");
    int day = 0;
    CHECK(ParseFolder(L"2026-11-03_09-05-07", &day) && day == 20261103);
    CHECK(!ParseFolder(L"2026-11-03 09-05-07", nullptr));
    CHECK(!ParseFolder(L"Characters", nullptr));
    CHECK(!ParseFolder(L"2026-10-17_19-59-58-507410Z", nullptr));   // D2RLoader's own backups' shape: not ours
    CHECK(DayBefore(2026, 11, 3, 30) == 20261004);
    CHECK(DayBefore(2024, 3, 1, 1) == 20240229);
    CHECK(DayBefore(2027, 1, 5, 30) == 20261206);

    // which old folders go: 30 days (from 4 Oct on), the newest copy of each file stays, the current game's stays
    const uint64_t kb = 1024;
    std::vector<Folder> all = {
        {L"2026-09-01_10-00-00", 200 * kb, {L"Bar.d2s", L"Old.d2s", L"Stash.d2i"}},   // Old.d2s: in no newer folder - stays
        {L"2026-09-02_10-00-00", 200 * kb, {L"Bar.d2s", L"Stash.d2i"}},
        {L"2026-10-03_23-59-59", 200 * kb, {L"Bar.d2s", L"Stash.d2i"}},
        {L"2026-10-20_10-00-00", 200 * kb, {L"Bar.d2s", L"Stash.d2i"}},
        {L"2026-11-02_22-00-00", 200 * kb, {L"Bar.d2s", L"Stash.d2i"}},   // Bar's last good copy: 3 Nov's lacks it (zeroed)
        {L"2026-11-03_14-00-00", 200 * kb, {L"STASH.D2I"}},
        {L"not ours", 100000 * kb, {L"Bar.d2s"}},
    };
    std::map<std::wstring, std::wstring> newest = Newest(all);
    CHECK(newest[L"bar.d2s"] == L"2026-11-02_22-00-00" && newest[L"stash.d2i"] == L"2026-11-03_14-00-00" &&
          newest[L"old.d2s"] == L"2026-09-01_10-00-00");
    std::vector<std::wstring> del = ToDelete(all, 20261004, 1000000 * kb, L"2026-11-03_14-00-00");
    CHECK(del.size() == 2 && del[0] == L"2026-09-02_10-00-00" && del[1] == L"2026-10-03_23-59-59");
    // at most 500 KB: of the kept four (800 KB), 20 Oct goes; 1 Sep (Old.d2s), 2 Nov (Bar.d2s) and 3 Nov (current) never -> 600 KB
    del = ToDelete(all, 20261004, 500 * kb, L"2026-11-03_14-00-00");
    CHECK(del.size() == 3 && del[2] == L"2026-10-20_10-00-00");
    // two months on, nothing played: every folder is old, the newest copies stay
    del = ToDelete(all, 20270101, 1000000 * kb, L"");
    CHECK(del.size() == 3);
    for (const std::wstring& n : del) CHECK(n != L"2026-09-01_10-00-00" && n != L"2026-11-02_22-00-00" && n != L"2026-11-03_14-00-00");

    std::printf(g_failed ? "save_backups_test: %d FAILED\n" : "save_backups_test: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
