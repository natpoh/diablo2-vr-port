// arcane_sky.h - the Arcane Sanctuary's sky, made from the player's own game files ([sky] arcane_sky).
//
// After bearstonem/d2r-pcvr (MIT), steamvr/prepare_arcane_sky.py of v0.3.18: the same four
// textures, the same checks, the same pictures, the same one change to the level's visual files.
//   stars.png  - the game's t_fx_stars_far / mid / near (BC4, 1024 x 512) as red / green / blue
//   clouds.png - its t_fx_cloud_noise_03 (BC4, 512 x 512), grey
//   act2_arcane_<period>.json - the level's visual files with their one screen-space weather
//                effect (the flat Stars.particles cards) taken out, every other byte as it was
// What comes out is Blizzard's: made on the player's machine, never in the repository or a build.
// No Windows here: D2R_VR_Settings reads and writes the files (tools/settings/arcane_assets.cpp),
// cleanroom/tests/arcane_sky_test.cpp tests this.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace arcanesky {

constexpr const char* kStars[3] = {
    "data/hd/vfx/textures/noise/t_fx_stars_far.texture",
    "data/hd/vfx/textures/noise/t_fx_stars_mid.texture",
    "data/hd/vfx/textures/noise/t_fx_stars_near.texture",
};
constexpr const char* kClouds = "data/hd/vfx/textures/noise/t_fx_cloud_noise_03.texture";
constexpr const char* kPeriods[7] = {"dawn1", "dawn2", "day", "dusk1", "dusk2", "night", "desecrated"};
// the weather effect taken out, as the files name it (compared in lower case, '/' for '\')
constexpr const char* kWeather = "data/hd/vfx/particles/env/common/weather/stars.particles";
constexpr int kStarsW = 1024, kStarsH = 512, kCloudsW = 512, kCloudsH = 512;

inline std::string VisName(int period) { return std::string("data/hd/env/vis/act2_arcane_") + kPeriods[period] + ".json"; }

struct Gray {
    int w = 0, h = 0;
    std::vector<uint8_t> px;   // w * h, row by row
};

inline uint32_t U32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

// One of D2R's .texture files of one channel, BC4 (format 63): its top level. Every mip the
// header declares is checked first (size and place), as Bear's script does: a file of another layout, a
// truncated one or one of another size per level is refused, with the reason.
inline bool DecodeBC4(const uint8_t* d, size_t n, Gray* out, std::string* why) {
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    if (!d || !out || n < 44) return no("truncated texture");
    const uint32_t magic = U32(d), width = U32(d + 8), height = U32(d + 12), mips = U32(d + 28);
    const uint32_t fmt = (uint32_t)d[4] | (uint32_t)d[5] << 8;
    if (magic != 0x2845443Cu || fmt != 63 || mips < 1 || mips > 14 || width < 4 || width > 2048 || height < 4 || height > 2048 ||
        width % 4 || height % 4)
        return no("not the texture layout known (BC4, format 63)");
    size_t top = 0, topSize = 0;
    for (uint32_t level = 0; level < mips; ++level) {
        if (44 + (size_t)level * 8 > n) return no("truncated mip table");
        const uint32_t size = U32(d + 36 + level * 8), rel = U32(d + 40 + level * 8);
        const size_t offset = 40 + (size_t)level * 8 + rel;
        const uint32_t lw = width >> level ? width >> level : 1, lh = height >> level ? height >> level : 1;
        const size_t expected = (size_t)((lw + 3) / 4) * ((lh + 3) / 4) * 8;
        if (size != expected || offset < 36 + (size_t)mips * 8 || offset + size > n) return no("a mip of the wrong size or place");
        if (level == 0) { top = offset; topSize = size; }
    }
    out->w = (int)width;
    out->h = (int)height;
    out->px.assign((size_t)width * height, 0);
    const uint32_t across = width / 4;
    for (size_t block = 0; block < topSize / 8; ++block) {
        const uint8_t* raw = d + top + block * 8;
        const int a = raw[0], b = raw[1];
        int table[8] = {a, b};
        if (a > b) for (int i = 1; i <= 6; ++i) table[i + 1] = ((7 - i) * a + i * b) / 7;
        else {
            for (int i = 1; i <= 4; ++i) table[i + 1] = ((5 - i) * a + i * b) / 5;
            table[6] = 0;
            table[7] = 255;
        }
        uint64_t idx = 0;
        for (int k = 0; k < 6; ++k) idx |= (uint64_t)raw[2 + k] << (8 * k);
        const size_t bx = block % across * 4, by = block / across * 4;
        for (int i = 0; i < 16; ++i) out->px[(by + i / 4) * width + bx + i % 4] = (uint8_t)table[(idx >> (3 * i)) & 7];
    }
    return true;
}

// The three star layers as one picture, RGBA: far red, mid green, near blue, alpha 255.
inline bool StarsRGBA(const Gray& farL, const Gray& midL, const Gray& nearL, std::vector<uint8_t>* rgba, std::string* why) {
    const Gray* l[3] = {&farL, &midL, &nearL};
    for (const Gray* g : l)
        if (g->w != kStarsW || g->h != kStarsH || g->px.size() != (size_t)kStarsW * kStarsH) { if (why) *why = "a star layer not 1024 x 512"; return false; }
    rgba->assign((size_t)kStarsW * kStarsH * 4, 255);
    for (size_t i = 0; i < (size_t)kStarsW * kStarsH; ++i)
        for (int c = 0; c < 3; ++c) (*rgba)[i * 4 + c] = l[c]->px[i];
    return true;
}

// The cloud noise, grey in RGB, alpha 255.
inline bool CloudsRGBA(const Gray& cloud, std::vector<uint8_t>* rgba, std::string* why) {
    if (cloud.w != kCloudsW || cloud.h != kCloudsH || cloud.px.size() != (size_t)kCloudsW * kCloudsH) { if (why) *why = "the cloud noise not 512 x 512"; return false; }
    rgba->assign((size_t)kCloudsW * kCloudsH * 4, 255);
    for (size_t i = 0; i < cloud.px.size(); ++i) (*rgba)[i * 4] = (*rgba)[i * 4 + 1] = (*rgba)[i * 4 + 2] = cloud.px[i];
    return true;
}

// ---------------------------------------------------------------- the level's visual files

inline size_t Count(const std::string& s, const char* what) {
    size_t n = 0;
    const size_t len = strlen(what);
    for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + len)) ++n;
    return n;
}

// The visual file with "screenspaceVfx":"" in place of the stars - every other byte as it was. Only the
// file as it is known (2026-10): a VisualDataDefinition, one VisualDataComponent, one screenspaceVfx and it
// naming those stars; anything else is refused with the reason, and the game's own stays.
inline bool StripWeather(const std::string& json, std::string* out, std::string* why) {
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    if (json.size() > 1024 * 1024) return no("an oversized visual file");
    if (Count(json, "\"VisualDataDefinition\"") != 1) return no("not one visual definition");
    if (Count(json, "\"VisualDataComponent\"") != 1) return no("not one visual component");
    const char* key = "\"screenspaceVfx\"";
    if (Count(json, key) != 1) return no("not one screen-space effect");
    size_t at = json.find(key) + strlen(key);
    auto skip = [&] { while (at < json.size() && (json[at] == ' ' || json[at] == '\t' || json[at] == '\r' || json[at] == '\n')) ++at; };
    skip();
    if (at >= json.size() || json[at] != ':') return no("the screen-space effect is not a string");
    ++at;
    skip();
    if (at >= json.size() || json[at] != '"') return no("the screen-space effect is not a string");
    const size_t from = at++;
    std::string value;
    for (; at < json.size() && json[at] != '"'; ++at) {
        char c = json[at];
        if (c == '\\') {   // only "\\" and "\/" are known in a path; anything else: not the file known
            if (++at >= json.size() || (json[at] != '\\' && json[at] != '/')) return no("an escape in the screen-space effect");
            c = json[at];
        }
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        value += c;
    }
    if (at >= json.size()) return no("the screen-space effect is not closed");
    if (value != kWeather) return no("the weather effect changed - the game's own background kept");
    *out = json.substr(0, from) + "\"\"" + json.substr(at + 1);
    return true;
}

// ---------------------------------------------------------------- a picture file, a hash

inline uint32_t Crc32(const uint8_t* d, size_t n, uint32_t crc = 0) {
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) {
        crc ^= d[i];
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

// A PNG of RGBA rows (8 bits): zlib's stored blocks, no compression - what ReShade loads
// (stb_image) reads it as any other. 2 MB for the stars, 1 MB for the clouds.
inline std::vector<uint8_t> Png(int w, int h, const uint8_t* rgba) {
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    auto be = [](std::vector<uint8_t>& v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back((uint8_t)(x >> s)); };
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        be(png, (uint32_t)data.size());
        const size_t start = png.size();
        png.insert(png.end(), type, type + 4);
        png.insert(png.end(), data.begin(), data.end());
        be(png, Crc32(png.data() + start, png.size() - start));
    };
    std::vector<uint8_t> ihdr;
    be(ihdr, (uint32_t)w);
    be(ihdr, (uint32_t)h);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});   // 8 bits, RGBA, deflate, no filter method, no interlace
    chunk("IHDR", ihdr);
    std::vector<uint8_t> raw;   // each row: filter 0, then its pixels
    raw.reserve((size_t)h * ((size_t)w * 4 + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba + (size_t)y * w * 4, rgba + (size_t)(y + 1) * w * 4);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    for (size_t at = 0; at < raw.size();) {
        const size_t len = raw.size() - at < 65535 ? raw.size() - at : 65535;
        z.push_back(at + len >= raw.size() ? 1 : 0);
        z.push_back((uint8_t)len);
        z.push_back((uint8_t)(len >> 8));
        z.push_back((uint8_t)~len);
        z.push_back((uint8_t)(~len >> 8));
        z.insert(z.end(), raw.begin() + at, raw.begin() + at + len);
        at += len;
    }
    uint32_t a = 1, b = 0;   // Adler-32
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    be(z, b << 16 | a);
    chunk("IDAT", z);
    chunk("IEND", {});
    return png;
}

// FNV-1a, 64 bits, as 16 hex digits: what the receipt keeps of each file written and of the
// game's build (.build.info) - enough to tell "ours, unchanged" and "the game was updated".
inline std::string Hash(const void* data, size_t n) {
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < n; ++i) { h ^= ((const uint8_t*)data)[i]; h *= 1099511628211ull; }
    char s[17];
    for (int i = 15; i >= 0; --i) { s[i] = "0123456789abcdef"[h & 15]; h >>= 4; }
    s[16] = 0;
    return s;
}

// The receipt: {"version":1,"game_build":"...","files":{"name":"hash",...}} - written by us, read back by
// these finders only (no general JSON reader). "" = not in it.
inline std::string ReceiptValue(const std::string& receipt, const std::string& key) {
    const std::string k = "\"" + key + "\":\"";
    const size_t at = receipt.find(k);
    if (at == std::string::npos) return "";
    const size_t from = at + k.size(), to = receipt.find('"', from);
    return to == std::string::npos ? "" : receipt.substr(from, to - from);
}

struct Written { std::string name, hash; };
inline std::string Receipt(const std::string& gameBuild, const std::vector<Written>& files) {
    std::string r = "{\n  \"version\": 1,\n  \"made_from\": \"the player's own game, by D2R_VR_Settings (after bearstonem/d2r-pcvr)\",\n"
                    "  \"game_build\":\"" + gameBuild + "\",\n  \"files\": {\n";
    for (size_t i = 0; i < files.size(); ++i)
        r += "    \"" + files[i].name + "\":\"" + files[i].hash + "\"" + (i + 1 < files.size() ? ",\n" : "\n");
    return r + "  }\n}\n";
}

// A visual file of the mod's folder may be replaced when it is not there, already what we would
// write, or ours unchanged (its hash in the last receipt); a file the player made or changed stays.
inline bool MayReplace(bool exists, const std::string& oldData, const std::string& newData, const std::string& lastHash) {
    if (!exists || oldData == newData) return true;
    return !lastHash.empty() && Hash(oldData.data(), oldData.size()) == lastHash;
}

}  // namespace arcanesky
