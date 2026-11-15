// Which item label a paint is (vr/label_key.h): by its name's text and its box's size, not by its record's address - the game sorts its label
// records between the layout and the paint (std::sort 0x1514F70). The flat case of 2026-11-14 (0.217): Alt with many names, the crosshair's item
// lit at 250 % - one name drawn 2.5x in another label's normal box, the lit item's large box round a normal-size name. Run: label_key_test.exe
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "label_key.h"

namespace {
int g_fails = 0, g_checks = 0;
#define CHECK(c)                                                                \
    do {                                                                        \
        ++g_checks;                                                             \
        if (!(c)) { ++g_fails; std::printf("  FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

using namespace labelkey;

// The game's record: the box at +0, the name at +0x24 (0x144 bytes in all) - here only what matters.
struct Record {
    int32_t box[4];
    char name[kNameMax];
    uintptr_t unit;
};
Record Make(const char* name, int32_t x, int32_t y, int32_t w, int32_t h, uintptr_t unit) {
    Record r{};
    r.box[0] = x; r.box[1] = y; r.box[2] = w; r.box[3] = h;
    std::snprintf(r.name, sizeof r.name, "%s", name);
    r.unit = unit;
    return r;
}

// Lay out (the lit one's box made 2.5x), note, sort as the game does, paint: each paint must find its own label's size and unit.
void TestSorted() {
    std::vector<Record> list = {Make("\xFF" "c0Scythe", 300, 500, 90, 24, 0x1000), Make("\xFF" "c3Heraldic Shield", 320, 420, 190, 24, 0x2000),
                                Make("\xFF" "c0Short Sword", 600, 610, 140, 24, 0x3000), Make("\xFF" "c0Scythe", 900, 380, 90, 24, 0x4000)};
    const uintptr_t lit = 0x1000;   // the crosshair's item: the first Scythe
    Ring<128> ring;
    struct ByAddress { const void* box; float k; };   // the old way: the record's address
    std::vector<ByAddress> old;
    for (Record& r : list) {
        float k = 1.0f;
        if (r.unit == lit) {   // ResizeLabelBox about the same bottom middle
            k = 2.5f;
            const int32_t w = (int32_t)(r.box[2] * k), h = (int32_t)(r.box[3] * k);
            const int32_t mid = r.box[0] + r.box[2] / 2, bottom = r.box[1] + r.box[3];
            r.box[0] = mid - w / 2; r.box[1] = bottom - h; r.box[2] = w; r.box[3] = h;
        }
        ring.Note(Of(r.name, r.box), k, r.unit);
        old.push_back({&r, k});
    }
    // the game's sort (by the box's top here; what it sorts by does not matter, only that the records move)
    std::sort(list.begin(), list.end(), [](const Record& a, const Record& b) { return a.box[1] < b.box[1]; });
    int oldWrong = 0;
    for (const Record& r : list) {
        const Key key = Of(r.name, r.box);
        const float want = r.unit == lit ? 2.5f : 1.0f;
        CHECK(ring.K(key, -9.0f) == want);   // its own size: the name drawn to fit its own box
        CHECK(ring.Unit(key) == r.unit);     // its own unit (the two Scythes told apart by the lit one's larger box)
        CHECK((ring.Unit(key) == lit) == (r.unit == lit));   // the pointed look on the pointed item's label alone
        for (const ByAddress& o : old)
            if (o.box == &r && o.k != want) ++oldWrong;
    }
    CHECK(oldWrong >= 2);   // by the address: the 2.5x size went to a neighbour and the lit label lost it - the screenshot
    std::printf("  sorted list: by the address %d of %zu labels painted at another label's size; by what they show 0\n", oldWrong, list.size());
}

void TestFallbacks() {
    Ring<8> ring;
    const int32_t box[4] = {0, 0, 100, 20}, moved[4] = {50, 70, 100, 20}, other[4] = {0, 0, 120, 20};
    ring.Note(Of("Gold", box), 1.5f, 7);
    CHECK(ring.K(Of("Gold", moved), 0.0f) == 1.5f);   // the game moved the box after the sort: the same label
    CHECK(ring.K(Of("Gold", other), 0.0f) == 1.5f);   // its size changed by the game: by the text alone, one label of it
    CHECK(ring.K(Of("Silver", box), 0.25f) == 0.25f); // never laid out: the caller's
    CHECK(ring.Unit(Of("Silver", box)) == 0);
    ring.Note(Of("Gold", other), 2.0f, 8);            // two of that text, sizes differing: an unknown size means none
    const int32_t third[4] = {0, 0, 140, 20};
    CHECK(ring.K(Of("Gold", third), 0.5f) == 0.5f);
    CHECK(ring.Unit(Of("Gold", third)) == 0);
    CHECK(ring.K(Of("Gold", box), 0.0f) == 1.5f && ring.K(Of("Gold", other), 0.0f) == 2.0f);   // the exact ones still known
    ring.Note(Of("Waypoint", box), -1.0f, 9);         // not an item's label
    CHECK(ring.K(Of("Waypoint", box), 0.0f) < 0.0f);
    // the newest wins: a lit item no longer lit is laid out at its normal size again
    ring.Note(Of("Axe", other), 2.5f, 10);
    ring.Note(Of("Axe", box), 1.0f, 10);
    CHECK(ring.K(Of("Axe", box), 0.0f) == 1.0f && ring.K(Of("Axe", other), 0.0f) == 2.5f);
    // the colour codes count; no name, no box
    CHECK(TextHash("\xFF" "c3Ring", kNameMax) != TextHash("\xFF" "c9Ring", kNameMax));
    CHECK(TextHash(nullptr, kNameMax) == 0);
    const Key none = Of("Gold", nullptr);
    CHECK(none.w == -1 && none.h == -1);
    // the ring wraps: old labels fall out
    Ring<4> small;
    for (int i = 0; i < 6; ++i) { char n[8]; std::snprintf(n, sizeof n, "L%d", i); small.Note(Of(n, box), 1.0f + i, (uintptr_t)i); }
    CHECK(small.K(Of("L0", box), -1.0f) == -1.0f && small.K(Of("L5", box), -1.0f) == 6.0f);
}
}  // namespace

int main() {
    TestSorted();
    TestFallbacks();
    std::printf("label_key_test: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
