// table_rows.h - a row id read from the game (an item's class, a monster's, an object's, a skill's...) checked against the GAME's table
// before anything of ours is looked up by it. Pure (cleanroom/tests/table_rows_test.cpp).
//
// Why: data mods (Reimagined, D2RMM merges) add rows to Items / Weapons / Misc / ItemTypes / MonStats / Objects / Skills. A table of ours
// made from the vanilla game, or sized for it, must never be read past its end by such a row: the row is left alone (no kind, no model,
// the game's own behaviour), and the log says so once per table - "row N past the game's table of M - a data mod's row, left alone".
// Visual / input only: nothing of the game is changed by any answer here.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace tablerows {

// The rows of ours that can be filled from a game table of `gameRows` rows when ours holds `cap`: never more than either.
constexpr uint32_t Usable(uint32_t gameRows, uint32_t cap) { return gameRows < cap ? gameRows : cap; }

// `row` may index a table of ours of `rows` entries (the count actually filled, not the array's size alone).
constexpr bool In(uint32_t row, uint32_t rows) { return row < rows; }
inline bool In(int64_t row, uint32_t rows) { return row >= 0 && static_cast<uint64_t>(row) < rows; }

// A rig's parent index as the game's model gives it, for bone `i`: kept when it is an earlier bone (a Granny rig lists a parent before its
// children), else -1 (a root) - a later bone, itself, or past the rig would index past the rig's arrays or make a parent walk loop for ever.
constexpr int32_t EarlierOrRoot(int32_t parent, int32_t i) { return parent >= 0 && parent < i ? parent : -1; }

// One table's "told once": the first row past its end is logged, later ones are not (a log line costs ~1 ms).
struct Once {
    std::atomic<bool> told{false};
    // True exactly once - the caller then logs the line Line() writes.
    bool First() { return !told.exchange(true, std::memory_order_relaxed); }
};

// The log line for a row past a table's end. `table` names the table and who reads it ("Items (weapon kinds)").
inline int Line(char* out, size_t size, const char* table, uint32_t row, uint32_t rows) {
    return std::snprintf(out, size, "table rows: %s - row %u past the game's table of %u - a data mod's row, left alone (no lookup of ours for it)",
                         table ? table : "?", static_cast<unsigned>(row), static_cast<unsigned>(rows));
}

// The log line for a game table bigger than the table of ours made from it: the rows past `cap` are left alone.
inline int CapLine(char* out, size_t size, const char* table, uint32_t gameRows, uint32_t cap) {
    return std::snprintf(out, size, "table rows: %s - the game's table has %u rows, ours holds %u - rows from %u on are a data mod's, left alone",
                         table ? table : "?", static_cast<unsigned>(gameRows), static_cast<unsigned>(cap), static_cast<unsigned>(cap));
}

// Look up `row` in `table` (of `rows` filled entries); `fallback` when it is past the end, with `once` and `log` told the first time
// (a table not built yet - rows 0 - answers `fallback` and tells nothing).
template <class T, class LogFn>
T At(const T* table, uint32_t rows, uint32_t row, T fallback, Once& once, const char* name, LogFn&& log) {
    if (!table || rows == 0) return fallback;   // not built (yet): not a data mod's row, nothing told
    if (In(row, rows)) return table[row];
    if (once.First()) {
        char b[220];
        Line(b, sizeof b, name, row, rows);
        log(b);
    }
    return fallback;
}

}  // namespace tablerows
