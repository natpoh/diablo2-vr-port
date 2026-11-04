// vr/table_rows.h: a row id read from the game checked against the table it indexes - a data mod (Reimagined) with more Items rows than
// ours hold, a class past the game's table, a table not built yet, a hero rig's parent that is not an earlier bone. Never a read past the end;
// the log told once per table. Run: table_rows_test.exe
#include "table_rows.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}

int main() {
    using namespace tablerows;

    // ---- a made-up Items table bigger than ours (vanilla ~659 rows; this one 5000, ours holds 4096) ------------------------------------
    constexpr uint32_t kCap = 4096, kGameRows = 5000;
    Check(Usable(kGameRows, kCap) == kCap, "a game table bigger than ours: only our rows filled");
    Check(Usable(700, kCap) == 700, "a game table smaller than ours: its own rows only");
    Check(Usable(0, kCap) == 0, "an empty game table: nothing filled");

    std::vector<uint8_t> kinds(kCap, 0);
    const uint32_t filled = Usable(kGameRows, kCap);
    for (uint32_t i = 0; i < filled; ++i) kinds[i] = (uint8_t)(i % 7 + 1);   // a "kind" per row
    // the guard sits right after the array: a read past `filled` would show 0xEE
    kinds.push_back(0xEE);

    std::vector<std::string> lines;
    auto log = [&](const char* s) { lines.emplace_back(s); };
    Once once;

    Check(At<uint8_t>(kinds.data(), filled, 10, 0, once, "Items (weapon kinds)", log) == 10 % 7 + 1, "a vanilla row: its kind");
    Check(At<uint8_t>(kinds.data(), filled, kCap - 1, 0, once, "Items (weapon kinds)", log) == (kCap - 1) % 7 + 1, "the last row held: its kind");
    Check(lines.empty(), "rows inside the table: nothing told");

    Check(At<uint8_t>(kinds.data(), filled, kCap, 0, once, "Items (weapon kinds)", log) == 0, "the first row past ours: the fallback, not the guard byte");
    Check(At<uint8_t>(kinds.data(), filled, 4999, 0, once, "Items (weapon kinds)", log) == 0, "a data mod's row: the fallback");
    Check(At<uint8_t>(kinds.data(), filled, 0xFFFFFFFFu, 0, once, "Items (weapon kinds)", log) == 0, "a garbage class: the fallback");
    Check(lines.size() == 1, "rows past the table: told once, not per lookup");
    if (!lines.empty()) {
        Check(lines[0].find("row 4096 past the game's table of 4096") != std::string::npos, "the line names the row and the table's size");
        Check(lines[0].find("a data mod's row, left alone") != std::string::npos, "the line says it is left alone");
        Check(lines[0].find("Items (weapon kinds)") != std::string::npos, "the line names the table");
    }

    // a second table has its own "told once"
    Once other;
    Check(At<uint8_t>(kinds.data(), filled, 6000, 0, other, "Items (item codes)", log) == 0, "another table's row past its end: the fallback");
    Check(lines.size() == 2, "each table tells once of its own");

    // a table not built yet (rows 0, or no table): the fallback, nothing told (not a data mod's row)
    Once notYet;
    Check(At<uint8_t>(kinds.data(), 0, 3, 9, notYet, "Items (not built)", log) == 9, "a table not built: the fallback");
    Check(At<uint8_t>(nullptr, 100, 3, 9, notYet, "Items (no table)", log) == 9, "no table: the fallback");
    Check(lines.size() == 2, "a table not built: nothing told");

    // the cap line
    char b[260];
    CapLine(b, sizeof b, "Items (weapon kinds by class)", kGameRows, kCap);
    Check(std::strstr(b, "has 5000 rows, ours holds 4096") != nullptr, "the cap line names both sizes");

    // In: signed and unsigned
    Check(In(0u, 1u) && !In(1u, 1u) && !In(0u, 0u), "In (unsigned)");
    Check(In((int64_t)0, 1u) && !In((int64_t)-1, 5u) && !In((int64_t)5, 5u), "In (signed): -1 is never a row");

    // ---- a hero rig's parents: only an earlier bone is a parent --------------------------------------------------------------------------
    Check(EarlierOrRoot(-1, 0) == -1, "the root stays a root");
    Check(EarlierOrRoot(0, 1) == 0, "an earlier bone: kept");
    Check(EarlierOrRoot(4, 5) == 4, "the bone just before: kept");
    Check(EarlierOrRoot(5, 5) == -1, "itself: a root (a walk would loop)");
    Check(EarlierOrRoot(7, 5) == -1, "a later bone: a root (a cycle possible)");
    Check(EarlierOrRoot(2000, 5) == -1, "past the rig: a root (would index past its arrays)");
    Check(EarlierOrRoot(-7, 5) == -1, "a negative other than -1: a root");
    {   // a rig with a bad parent: every walk up ends, every index stays inside
        const int32_t raw[6] = {-1, 0, 1, 9, 3, 4};   // bone 3 names 9 (past the rig of 6)
        int32_t parent[6];
        for (int i = 0; i < 6; ++i) parent[i] = EarlierOrRoot(raw[i], i);
        bool ends = true;
        for (int i = 0; i < 6; ++i) {
            int steps = 0;
            for (int p = parent[i]; p >= 0; p = parent[p])
                if (++steps > 6 || p >= 6) { ends = false; break; }
        }
        Check(ends, "every walk up a sanitised rig ends inside it");
        Check(parent[3] == -1 && parent[4] == 3 && parent[2] == 1, "only the bad parent is cut");
    }

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("table_rows_test: all passed\n");
    return 0;
}
