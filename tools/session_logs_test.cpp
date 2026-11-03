// [support] session_logs' rules (vr/diag/session_logs_rule.h) on made-up folders: the names, the files copied, which old folders go.
//   session_logs_test   (exit code 0 = all passed)
#include "diag/session_logs_rule.h"

#include <cstdio>

using namespace vrc::sessionlogsrule;

static int g_failed = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAILED line %d: %s\n", __LINE__, #c); ++g_failed; } } while (0)

int main() {
    CHECK(FolderName(2026, 11, 3, 9, 5, 7) == L"2026-11-03_09-05-07");
    int day = 0;
    CHECK(ParseFolder(L"2026-11-03_09-05-07", &day) && day == 20261103);
    CHECK(!ParseFolder(L"2026-11-03 09-05-07", nullptr));
    CHECK(!ParseFolder(L"backup", nullptr));
    CHECK(!ParseFolder(L"2026-11-03_09-05-0x", nullptr));

    CHECK(WantFile(L"d2r-vr-vrcam.log"));
    CHECK(WantFile(L"d2r_vr_start.txt"));
    CHECK(WantFile(L"D2RLOADER.LOG"));
    CHECK(!WantFile(L"headset_shot_20261031_215445.txt"));
    CHECK(!WantFile(L"headset_shot_20261031_215445_left.png"));
    CHECK(!WantFile(L"d2r_vr_frames.csv"));
    CHECK(!WantFile(L"crash.dmp"));

    CHECK(DayBefore(2026, 11, 3, 30) == 20261004);
    CHECK(DayBefore(2026, 3, 1, 1) == 20260228);
    CHECK(DayBefore(2024, 3, 1, 1) == 20240229);
    CHECK(DayBefore(2027, 1, 5, 30) == 20261206);

    // 30 days: the two from before 4 Oct go; then 10 MB at most: the oldest kept ones go, never the current game's
    const uint64_t mb = 1 << 20;
    std::vector<Folder> all = {{L"2026-11-02_22-25-50", 4 * mb}, {L"2026-09-01_10-00-00", 1 * mb}, {L"2026-10-03_23-59-59", 1 * mb},
                               {L"2026-10-04_00-00-00", 3 * mb}, {L"2026-11-01_15-25-00", 5 * mb}, {L"2026-11-03_10-00-00", 2 * mb},
                               {L"not ours", 100 * mb}};
    std::vector<std::wstring> gone = ToDelete(all, 20261004, 1000 * mb, L"2026-11-03_10-00-00");
    CHECK(gone.size() == 2 && gone[0] == L"2026-09-01_10-00-00" && gone[1] == L"2026-10-03_23-59-59");
    gone = ToDelete(all, 20261004, 10 * mb, L"2026-11-03_10-00-00");   // kept 14 MB: 4 Oct (3) and 1 Nov (5) go too -> 6 MB
    CHECK(gone.size() == 4 && gone[2] == L"2026-10-04_00-00-00" && gone[3] == L"2026-11-01_15-25-00");
    gone = ToDelete({{L"2026-11-03_10-00-00", 50 * mb}}, 20261004, 10 * mb, L"2026-11-03_10-00-00");   // the current one alone: stays
    CHECK(gone.empty());

    std::printf(g_failed ? "session_logs_test: %d FAILED\n" : "session_logs_test: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
