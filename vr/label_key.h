// label_key.h - which item label a paint is, told by what it shows (its name's text and its box's size), not by where it lies.
//
// The game lays out every ground label into a list of records (0x144 bytes: the box at +0, the name at +0x24; LabelLayout 0x1FA9F0 from
// 0x15170CC), then SORTS the list - MSVC's std::sort, 0x1514F70 called right after the layout loop (its insertion-sort threshold
// `cmp rax, 0x29C4` = 33 records) - and paints it afterwards (LabelPaint 0x1FA8E0 from 0x1517989). The records move by value: at the paint
// a record's address holds whichever label sorted there. vrcam kept each label's size (labels_size, labels_far, the pointed item's
// point_pickup_label_pct) and its unit by the name's and the box's addresses, so the size and the pointed look went to a neighbour - flat,
// Alt with many names, a crosshair item lit ([input] flat_pickup_key_on / flat_item_hitbox): one name painted 2.5x in its normal box
// over the next label, and the pointed item's large box round a normal-size name (a player's screenshot, 0.217, 2026-11-14).
// The name's text and the box's width and height move with the record; the box's place may not (the game stacks the names after the sort).
//
// Pure; the test is cleanroom/tests/label_key_test.cpp, the wiring vr/hud/labels.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

namespace labelkey {

// FNV-1a of the name up to its end (or `most` bytes): the colour codes in it count (two items of one name in two colours differ).
inline uint64_t TextHash(const char* s, size_t most) {
    uint64_t h = 1469598103934665603ull;
    if (!s) return 0;
    for (size_t i = 0; i < most && s[i]; ++i) h = (h ^ (uint8_t)s[i]) * 1099511628211ull;
    return h;
}

struct Key {
    uint64_t text = 0;
    int32_t w = -1, h = -1;   // the box's size, -1 = no box
};
constexpr size_t kNameMax = 0x80;   // the record's name buffer
inline Key Of(const char* name, const int32_t* box) {
    Key k;
    k.text = TextHash(name, kNameMax);
    if (box) { k.w = box[2]; k.h = box[3]; }
    return k;
}
inline bool Same(const Key& a, const Key& b) { return a.text == b.text && a.w == b.w && a.h == b.h; }

// The labels laid out lately, the newest first: each one's size (k; -1 = not an item's label) and its unit. Written by the layout, read by
// the paint - both on the game's interface thread.
template <int N> struct Ring {
    static_assert(N > 0);
    struct Entry { Key key; float k = 0.0f; uintptr_t unit = 0; uint64_t seq = 0; };
    Entry e[N]{};
    uint64_t seq = 0;
    void Note(const Key& key, float k, uintptr_t unit) {
        ++seq;
        e[seq % N] = {key, k, unit, seq};
    }
    // The newest entry of exactly this label (its text and its box's size); else, by its text alone, the newest one when every entry of that
    // text agrees on `field` (the game changed the box's size after the layout, a name shown once) - two that disagree: none.
    template <class F> const Entry* Find(const Key& key, F agree) const {
        const Entry* best = nullptr;
        for (const Entry& x : e)
            if (x.seq && Same(x.key, key) && (!best || x.seq > best->seq)) best = &x;
        if (best || !key.text) return best;
        for (const Entry& x : e) {
            if (!x.seq || x.key.text != key.text) continue;
            if (best && !agree(*best, x)) return nullptr;
            if (!best || x.seq > best->seq) best = &x;
        }
        return best;
    }
    // This label's size, or `fallback` when it is not known.
    float K(const Key& key, float fallback) const {
        const Entry* x = Find(key, [](const Entry& a, const Entry& b) { return a.k == b.k; });
        return x ? x->k : fallback;
    }
    // This label's unit, or 0.
    uintptr_t Unit(const Key& key) const {
        const Entry* x = Find(key, [](const Entry& a, const Entry& b) { return a.unit == b.unit; });
        return x ? x->unit : 0;
    }
};

}  // namespace labelkey
