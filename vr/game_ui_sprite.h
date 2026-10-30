#pragma once
// game_ui_sprite.h - the game's HD interface sprites ('SpA1' .sprite files, data\hd\global\ui\...) as pictures we can draw: the sheet's
// frames side by side, RGBA, straight alpha. Pure (no Windows, no files): bytes in, a sheet out, so a test feeds it made-up sheets
// (cleanroom/tests/game_ui_sprite_test.cpp).
//
// The format, as the game's own sheets have it (version 31, 4 components): a 40-byte header - 'SpA1', u16 version, u16 frame width,
// u32 sheet width, u32 height, ... u32 frame count at 20, ... u32 pixel bytes and u32 components at 32 - then width x height RGBA rows.
// The frames stand left to right; a sheet may pad every frame or leave the last gutter out. First worked out by bearstonem/d2r-pcvr's
// steamvr/prepare_icons.py (MIT); the same reading as tools/skill_icons/prepare_skill_icons.py sprite_frame, written again here.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace gameui {

struct Sheet {
    int width = 0, height = 0;        // the whole sheet
    int frameWidth = 0, frames = 0;   // one frame's width, how many
    int stride = 0;                   // from one frame's left edge to the next one's
    std::vector<uint8_t> rgba;        // width x height x 4
    bool Ok() const { return frames > 0 && frameWidth > 0 && height > 0 && rgba.size() == (size_t)width * height * 4; }
    // One pixel of frame `f` (x < frameWidth, y < height), RGBA.
    const uint8_t* At(int f, int x, int y) const { return &rgba[((size_t)y * width + (size_t)f * stride + x) * 4]; }
};

inline uint32_t ReadU32(const std::string& d, size_t at) {
    uint32_t v;
    std::memcpy(&v, d.data() + at, 4);
    return v;
}
inline uint16_t ReadU16(const std::string& d, size_t at) {
    uint16_t v;
    std::memcpy(&v, d.data() + at, 2);
    return v;
}

// The sheet in `data`; false (and `why`) for anything that is not a sheet as the game's own are.
inline bool Decode(const std::string& data, Sheet* out, const char** why = nullptr) {
    auto fail = [&](const char* w) {
        if (why) *why = w;
        return false;
    };
    if (data.size() < 40 || std::memcmp(data.data(), "SpA1", 4) != 0) return fail("not a sprite sheet");
    const uint16_t version = ReadU16(data, 4), frameWidth = ReadU16(data, 6);
    const uint32_t width = ReadU32(data, 8), height = ReadU32(data, 12), count = ReadU32(data, 20);
    const uint32_t size = ReadU32(data, 32), components = ReadU32(data, 36);
    if (version != 31 || components != 4) return fail("another pixel format");
    if (count == 0 || count > 64 || frameWidth == 0 || width == 0 || height == 0 || width > 16384 || height > 16384) return fail("odd size");
    if ((uint64_t)size != (uint64_t)width * height * 4 || data.size() != 40 + (size_t)size) return fail("odd size");
    uint32_t stride;
    if (width % count == 0) stride = width / count;
    else if (count > 1 && width > count * frameWidth && (width - count * frameWidth) % (count - 1) == 0) stride = frameWidth + (width - count * frameWidth) / (count - 1);
    else return fail("cannot tell the frame stride");
    if (stride < frameWidth || stride - frameWidth > 8 || (uint64_t)(count - 1) * stride + frameWidth > width) return fail("odd frame gutter");
    out->width = (int)width;
    out->height = (int)height;
    out->frameWidth = frameWidth;
    out->frames = (int)count;
    out->stride = (int)stride;
    out->rgba.assign(data.begin() + 40, data.end());
    return true;
}

}  // namespace gameui
