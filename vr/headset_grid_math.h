#pragma once
// [headset_shot] grid (vr/headset_grid.h): where its lines and numbers fall in an eye's picture, apart from D3D12 - the same sums as its
// shader, so that a test can check the drawn picture (tools/headset_shot_test.cpp).

#include <cmath>
#include <cstdint>

namespace headgrid {

constexpr float kStepDeg = 10.0f;   // a line every 10 degrees

// The glyphs, 3 x 5 cells: bit (row * 3 + column), the top row first. 0-9, then L and R. The shader has the same table.
constexpr uint32_t kGlyphs[12] = {
    0x7B6F,   // 0: 111 101 101 101 111
    0x749A,   // 1: 010 110 010 010 111 (rows read bit 0 = left)
    0x73E7,   // 2: 111 001 111 100 111
    0x79E7,   // 3: 111 001 111 001 111
    0x49ED,   // 4: 101 101 111 001 001
    0x79CF,   // 5: 111 100 111 001 111
    0x7BCF,   // 6: 111 100 111 101 111
    0x4927,   // 7: 111 001 001 001 001
    0x7BEF,   // 8: 111 101 111 101 111
    0x79EF,   // 9: 111 101 111 001 111
    0x7249,   // L: 100 100 100 100 111
    0x5AEB,   // R: 110 101 110 101 101
};
// Whether cell (column 0..2, row 0..4 from the top) of glyph g is lit.
inline bool Lit(int g, int column, int row) {
    if (g < 0 || g > 11 || column < 0 || column > 2 || row < 0 || row > 4) return false;
    return (kGlyphs[g] >> (row * 3 + column)) & 1u;
}

// The pixel column of a direction tan (from the eye's axis) in a picture w wide whose edges are at tanL .. tanR; the row of tan (up positive)
// in a picture h high, its top at tanU, its bottom at tanD.
inline float ColumnOf(float tan, float tanL, float tanR, uint32_t w) { return (tan - tanL) / (tanR - tanL) * (float)w; }
inline float RowOf(float tan, float tanU, float tanD, uint32_t h) { return (tanU - tan) / (tanU - tanD) * (float)h; }
inline float TanDeg(float deg) { return std::tan(deg * 3.14159265f / 180.0f); }

// The glyphs' size: a cell this many pixels (2 at 600 rows, more on a bigger picture).
inline float CellPx(uint32_t h) { return std::fmax(2.0f, std::round((float)h / 300.0f)); }

}  // namespace headgrid
