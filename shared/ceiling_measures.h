#pragma once
// ceiling_measures.h - what the game's own models say about its rooms' ceilings (docs/game_ceiling.md "From the game's models").
// D2R's dungeons are built without ceilings (the view from above: no preset of any indoor area places one - the 2026-10-29 survey), so
//  - [ceiling] height_from_walls: a room's ceiling rests on its walls' top - the median top of the wall, pillar, cliff and column pieces
//    its presets place (data/hd/env/preset/<act>/<area>/*.json: each piece's model bounds, raised by its transform's height and scale)
//  - [ceiling] game_ceiling_beams: the barracks' wooden ceiling frame the game ships and never places (wood_ceiling_frame01): its bottom,
//    top and the size it repeats at are its own bounds; the grid of whole frames made of it, as many as one model holds.
//  - [ceiling] game_ceiling_dome: the cathedral's dome over its chapel - where its highest piece stands among four pillars (Dome below).
// D2R_VR_Settings measures them in the player's game (tools/settings/dome_assets.cpp PrepareCeiling) into a small text file of ours in the
// mod (kFile), vrcam reads it at each area change. Pure; tested in cleanroom/tests/game_ceiling_test.cpp.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <locale>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ceilmeasure {

constexpr const char* kFile = "data/hd/env/model/d2r_vr/ceiling/measures.txt";   // in the mod's D2R-VR.mpq folder
constexpr int kVersion = 5;   // (2: the dome; 3: each area its own biome's presets, every dungeon; 4: domes over the tall pieces, each its slot; 5: the Worldstone's alone)

// ---- the presets (a JSON reader of their own: objects, arrays, strings, numbers, true / false / null) ----------------------------------

namespace json {
struct Value {
    enum Type { kNull, kBool, kNum, kStr, kArr, kObj } type = kNull;
    double num = 0.0;
    std::string str;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;
    const Value* Get(const char* key) const {
        for (const auto& kv : obj) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    double Num(const char* key, double fallback) const {
        const Value* v = Get(key);
        return v && v->type == kNum ? v->num : fallback;
    }
};
class Reader {
  public:
    explicit Reader(const std::string& s) : s_(s) {}
    bool Parse(Value* v) { return Val(v, 0) && (Ws(), i_ == s_.size()); }
  private:
    void Ws() { while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\r' || s_[i_] == '\n')) ++i_; }
    bool Lit(const char* w) {
        size_t n = 0;
        while (w[n]) ++n;
        if (s_.compare(i_, n, w) != 0) return false;
        i_ += n;
        return true;
    }
    bool Str(std::string* out) {
        if (i_ >= s_.size() || s_[i_] != '"') return false;
        ++i_;
        out->clear();
        while (i_ < s_.size() && s_[i_] != '"') {
            char c = s_[i_++];
            if (c == '\\') {
                if (i_ >= s_.size()) return false;
                c = s_[i_++];
                if (c == 'u') { if (i_ + 4 > s_.size()) return false; i_ += 4; c = '?'; }   // (names are ASCII: kept as a mark)
                else if (c == 'n') c = '\n'; else if (c == 't') c = '\t'; else if (c == 'r') c = '\r'; else if (c == 'b') c = '\b'; else if (c == 'f') c = '\f';
            }
            out->push_back(c);
        }
        if (i_ >= s_.size()) return false;
        ++i_;
        return true;
    }
    bool Val(Value* v, int depth) {
        if (depth > 64) return false;
        Ws();
        if (i_ >= s_.size()) return false;
        const char c = s_[i_];
        if (c == '{') {
            ++i_;
            v->type = Value::kObj;
            Ws();
            if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
            for (;;) {
                std::string key;
                Ws();
                if (!Str(&key)) return false;
                Ws();
                if (i_ >= s_.size() || s_[i_] != ':') return false;
                ++i_;
                v->obj.emplace_back(std::move(key), Value());
                if (!Val(&v->obj.back().second, depth + 1)) return false;
                Ws();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
                return false;
            }
        }
        if (c == '[') {
            ++i_;
            v->type = Value::kArr;
            Ws();
            if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
            for (;;) {
                v->arr.emplace_back();
                if (!Val(&v->arr.back(), depth + 1)) return false;
                Ws();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
                return false;
            }
        }
        if (c == '"') { v->type = Value::kStr; return Str(&v->str); }
        if (Lit("true")) { v->type = Value::kBool; v->num = 1; return true; }
        if (Lit("false")) { v->type = Value::kBool; return true; }
        if (Lit("null")) return true;
        const char* from = s_.c_str() + i_;
        char* end = nullptr;
        v->num = std::strtod(from, &end);
        if (end == from) return false;
        v->type = Value::kNum;
        i_ += (size_t)(end - from);
        return true;
    }
    const std::string& s_;
    size_t i_ = 0;
};
}  // namespace json

// One model a preset places: its path (lower case, as written: "data/hd/env/model/....model"), its height and its height's scale; and
// ([ceiling] game_ceiling_dome) its place across, its scale and its turn (a quaternion x y z w) in the preset
struct Placement {
    std::string model;
    float y = 0.0f, scaleY = 1.0f;
    float x = 0.0f, z = 0.0f, scaleX = 1.0f, scaleZ = 1.0f;
    float q[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

inline std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Every model the preset's entities place: each object with a "components" list - its ModelDefinitionComponent's filename, raised by
// the TransformDefinitionComponent in the same list (none: at 0, scale 1)
inline void Placements(const json::Value& v, std::vector<Placement>* out) {
    if (v.type == json::Value::kObj) {
        const json::Value* comps = v.Get("components");
        if (comps && comps->type == json::Value::kArr) {
            const json::Value* tf = nullptr;
            for (const json::Value& c : comps->arr) {
                const json::Value* t = c.Get("type");
                if (t && t->type == json::Value::kStr && t->str == "TransformDefinitionComponent") { tf = &c; break; }
            }
            for (const json::Value& c : comps->arr) {
                const json::Value* t = c.Get("type");
                const json::Value* f = c.Get("filename");
                if (!t || t->type != json::Value::kStr || t->str != "ModelDefinitionComponent" || !f || f->type != json::Value::kStr || f->str.empty()) continue;
                Placement p;
                p.model = Lower(f->str);
                if (tf) {
                    if (const json::Value* pos = tf->Get("position")) {
                        p.x = (float)pos->Num("x", 0.0);
                        p.y = (float)pos->Num("y", 0.0);
                        p.z = (float)pos->Num("z", 0.0);
                    }
                    if (const json::Value* sc = tf->Get("scale")) {
                        p.scaleX = (float)sc->Num("x", 1.0);
                        p.scaleY = (float)sc->Num("y", 1.0);
                        p.scaleZ = (float)sc->Num("z", 1.0);
                    }
                    if (const json::Value* o = tf->Get("orientation")) {
                        p.q[0] = (float)o->Num("x", 0.0);
                        p.q[1] = (float)o->Num("y", 0.0);
                        p.q[2] = (float)o->Num("z", 0.0);
                        p.q[3] = (float)o->Num("w", 1.0);
                    }
                }
                out->push_back(p);
            }
        }
        for (const auto& kv : v.obj) Placements(kv.second, out);
    } else if (v.type == json::Value::kArr) {
        for (const json::Value& e : v.arr) Placements(e, out);
    }
}
inline bool Placements(const std::string& presetJson, std::vector<Placement>* out) {
    json::Value v;
    if (!json::Reader(presetJson).Parse(&v)) return false;
    Placements(v, out);
    return true;
}

// ---- the walls' top ---------------------------------------------------------------------------------------------------------------

// A piece that stands as a room's wall: its model's name says wall, pillar, cliff (the caves') or column
inline bool IsWallPiece(const std::string& model) {
    const std::string m = Lower(model);
    for (const char* w : {"wall", "pillar", "cliff", "column"}) if (m.find(w) != std::string::npos) return true;
    return false;
}
// Taller than this as placed, model units: a wall, not its cap, trim or rubble (the survey's rule; the hero is about 7.5 tall)
constexpr float kWallMinHeight = 6.0f;

// The top of a placed wall piece, or a negative number when it does not count (not a wall's name, or lower than kWallMinHeight)
inline float WallTop(const Placement& p, float modelMinY, float modelMaxY) {
    if (!IsWallPiece(p.model) || (modelMaxY - modelMinY) * p.scaleY <= kWallMinHeight) return -1.0f;
    return p.y + p.scaleY * modelMaxY;
}
// The room's walls' top: the median of its wall pieces' tops (0: none)
inline float Median(std::vector<float> tops) {
    if (tops.empty()) return 0.0f;
    std::sort(tops.begin(), tops.end());
    const size_t n = tops.size();
    return n % 2 ? tops[n / 2] : 0.5f * (tops[n / 2 - 1] + tops[n / 2]);
}

// Which presets build each area (the game's biome names, as the game sets them): the start of their paths under data/hd/env/preset/.
// Indoor areas only - Chaos Sanctuary and the Pandemonium Fortress are open to the void by design.
struct Area { const char* biome; const char* presets; };
constexpr Area kAreas[] = {
    {"act1_caves", "act1/caves/"}, {"act1_crypt", "act1/crypt/"}, {"act1_barracks", "act1/barracks/"}, {"act1_catacombs", "act1/catacomb/"},
    {"act1_cathedral", "act1/cathedrl/"}, {"act2_sewer", "act2/sewer/"}, {"act2_tomb", "act2/tomb/"}, {"act2_palace", "act2/palace/"},
    {"act2_palace_clean", "act2/palace/"}, {"act2_palace_cells", "act2/palace/"}, {"act2_maggot", "act2/maggot/"}, {"act3_sewer", "act3/sewer/"},
    {"act3_temple", "act3/temple/"}, {"act3_spider", "act3/spider/"}, {"act3_jungle_dungeon", "act3/jungle/dung"},
    {"expansion_icecave", "expansion/icecave/"}, {"expansion_baallair", "expansion/baallair/"}, {"expansion_wildtemple", "expansion/wildtemple/"},
    // every dungeon (2026-10-29): the barracks' courtyard, the Durance of Hate, the Worldstone Keep's throne room
    {"act1_court_to_barracks", "act1/barracks/"}, {"act3_travincal", "act3/travincal/"}, {"expansion_baallair_throneroom", "expansion/baallair/"},
};
// The biome a preset names ("biomeFilename": ".../<biome>.json"), or "" - an area is measured from its folder's presets of its own biome
inline std::string BiomeOf(const std::string& presetJson) {
    const size_t key = presetJson.find("\"biomeFilename\"");
    if (key == std::string::npos) return std::string();
    const size_t colon = presetJson.find(':', key), q1 = colon == std::string::npos ? colon : presetJson.find('"', colon + 1),
                 q2 = q1 == std::string::npos ? q1 : presetJson.find('"', q1 + 1);
    if (q2 == std::string::npos) return std::string();
    std::string b = presetJson.substr(q1 + 1, q2 - q1 - 1);
    b = b.substr(b.find_last_of('/') + 1);
    if (b.size() > 5 && b.compare(b.size() - 5, 5, ".json") == 0) b.resize(b.size() - 5);
    return Lower(b);
}

// ---- the barracks' beams ------------------------------------------------------------------------------------------------------------

// The frame as the game made it, from its own file (lod0's bounds): the size it repeats at across (x) and along (z) - its cross beams end
// where the next frame's first beam starts - and its bottom and top over its model's floor
struct Beam { float periodX = 0.0f, periodZ = 0.0f, bottom = 0.0f, top = 0.0f; int cols = 0, rows = 0; unsigned indices = 0; };

// The grid of whole frames one model holds (16-bit indices: 65536 vertices at most, `verts` a frame): the most nearly square in world
// units, then the largest
inline bool BeamGrid(unsigned verts, float periodX, float periodZ, int* cols, int* rows) {
    *cols = *rows = 0;
    if (!verts || periodX <= 0.0f || periodZ <= 0.0f) return false;
    const unsigned most = 65536u / verts;
    double best = -1.0;
    for (unsigned c = 1; c <= most; ++c) {
        const unsigned r = most / c;
        if (!r) break;
        const double sideX = (double)c * periodX, sideZ = (double)r * periodZ, shorter = sideX < sideZ ? sideX : sideZ;
        const double score = std::floor(shorter * 100.0) * 1e6 + (double)(c * r);   // (the shorter side first, then the count)
        if (score > best) { best = score; *cols = (int)c; *rows = (int)r; }
    }
    return *cols > 0;
}

// ---- [ceiling] game_ceiling_dome: the cathedral's dome over its chapel ----------------------------------------------------------------
// Serg (0.188, the Cathedral): "the same convex dome as we made with ReShade ... over the chapel". Our post vault rose there as one even
// half ellipsoid over whatever stood through it - the altar's canopy (baldachin01, 36.5 high; the columns and pillars 19.5 - 19.7). The
// game's preset says where and how: the area's highest piece (the canopy) stands in an 80 x 80 crossing with a pillar at each corner. So the
// dome stands on those four pillars - its middle theirs, its rim through them - and rises as a half ellipsoid just high enough to pass over
// the whole of the canopy's top. D2R_VR_Settings measures it from the preset and makes the vault (the slab with the dome in it); vrcam finds
// the canopy among the game's own pieces (its place in the preset, and a pillar's beside it) and lays the vault there, world-fixed.
// [ceiling] game_ceiling_worldstone_dome (Serg: the Worldstone Chamber, level 132, "как в храме", "над кристаллом"; then "мне нужно его
// поменять только на 132 уровне ... и только там где куст" - not over every tall piece): the same over the Worldstone - in the Throne of
// Destruction's biome (the throne room, 131, and the Worldstone Chamber, 132), the one piece standing the most over its ceiling (the walls'
// top) of those at least kTallFactor times it with a pillar in each quarter round them past their footprint: crystalflame_01 (45.6 over
// 9.9; the throne room's arch 34.4 - not made). Nowhere else.
constexpr float kTallFactor = 2.0f;
constexpr const char* kWorldstoneBiome = "expansion_baallair_throneroom";
constexpr const char* kCathedral = "act1_cathedral";   // (its dome: [ceiling] game_ceiling_dome, the first slot)

struct Dome {
    std::string biome;
    int slot = 0;                  // its vault's model and unit (gameceiling::kVaultModelFiles): 0 the cathedral's
    float anchor[3] = {};          // the piece it stands over (the area's highest): its place in its preset
    float witness[3] = {};         // one of the four pillars: its place in the same preset (with the anchor, the game's preset found)
    float centreX = 0.0f, centreZ = 0.0f;   // its middle in the preset: the four pillars' middle
    float radius = 0.0f;           // its rim, through the farthest of the four pillars
    float top = 0.0f;              // the anchor's top over the preset's floor
    float reach = 0.0f;            // the anchor's farthest footprint corner from the middle (inside the rim)
    float springs = 0.0f;          // the pillars' top: the ceiling's height the vault's mesh is made for
    float rise = 0.0f;             // the mesh's rise there (RiseOver)
    float half = 0.0f;             // the mesh's half side round its middle, model (= world) units
    unsigned indices = 0;          // its mesh's index count (as the game has it: the log's check)
};

// How high the half ellipsoid `radius` round rises to stay over a top `over` above the ceiling wherever the anchor's footprint reaches -
// out to `reach` from the middle, where it stands rise * sqrt(1 - (reach / radius)^2) high. 0: nothing to pass over, or no room for it.
inline float RiseOver(float over, float reach, float radius) {
    if (!(over > 0.0f) || !(radius > 0.0f) || !(reach >= 0.0f) || !(reach < radius)) return 0.0f;
    const double k = (double)reach / radius;
    return (float)(over / std::sqrt(1.0 - k * k));
}

// A piece's corner turned by its quaternion (x y z w) and scaled: v + 2w(u x v) + 2u x (u x v)
inline void Turn(const float q[4], const float v[3], float out[3]) {
    const double ux = q[0], uy = q[1], uz = q[2], w = q[3];
    const double tx = 2.0 * (uy * v[2] - uz * v[1]), ty = 2.0 * (uz * v[0] - ux * v[2]), tz = 2.0 * (ux * v[1] - uy * v[0]);
    out[0] = (float)(v[0] + w * tx + (uy * tz - uz * ty));
    out[1] = (float)(v[1] + w * ty + (uz * tx - ux * tz));
    out[2] = (float)(v[2] + w * tz + (ux * ty - uy * tx));
}

inline bool IsPillar(const std::string& model) {
    const size_t slash = model.find_last_of('/');
    return Lower(slash == std::string::npos ? model : model.substr(slash + 1)).find("pillar") != std::string::npos;
}

// The dome of one preset's pieces. `boundsOf(model, mn, mx)`: a model's own bounds (false: unknown - the piece left out). False and `why`:
// no dome there (no piece, its highest under `minTop`, no four pillars round it past its footprint, its footprint out past them, or
// nothing higher than them). A pillar counts standing at least kWallMinHeight over the preset's floor (not a footing under it) and
// farther from the highest piece's middle than its footprint reaches (the Worldstone's footprint, 84 x 64, has pillars inside it).
template <class BoundsOf>
// `onPiece` (the domes over tall pieces - Serg on level 132: "над кристаллом"): its middle the piece's own (its footprint's), its rim through
// the farthest of the four pillars; else (the cathedral's, as made first) the four pillars' middle.
bool DomeOf(const std::vector<Placement>& placed, BoundsOf boundsOf, Dome* out, std::string* why, float minTop = 0.0f, bool onPiece = false) {
    *out = Dome{};
    auto modelOf = [](const Placement& p) {
        return p.model.compare(0, 18, "data/hd/env/model/") == 0 && p.model.size() > 6 && p.model.compare(p.model.size() - 6, 6, ".model") == 0;
    };
    // the anchor: the highest piece placed (the vis-box blockers under data/hd/env/light/ are not drawn)
    const Placement* anchor = nullptr;
    float amn[3] = {}, amx[3] = {}, top = 0.0f;
    for (const Placement& p : placed) {
        float mn[3], mx[3];
        if (!modelOf(p) || !boundsOf(p.model, mn, mx)) continue;
        const float t = p.y + p.scaleY * mx[1];
        if (!anchor || t > top) { anchor = &p; top = t; std::memcpy(amn, mn, sizeof mn); std::memcpy(amx, mx, sizeof mx); }
    }
    if (!anchor) { *why = "no piece with known bounds"; return false; }
    if (top < minTop) { *why = "its highest piece under twice the room's height"; return false; }
    // its footprint: its bounds' corners as placed (turned, scaled), across
    float foot[8][2], lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
    for (int c = 0; c < 8; ++c) {
        const float v[3] = {((c & 1) ? amx[0] : amn[0]) * anchor->scaleX, ((c & 2) ? amx[1] : amn[1]) * anchor->scaleY,
                            ((c & 4) ? amx[2] : amn[2]) * anchor->scaleZ};
        float w[3];
        Turn(anchor->q, v, w);
        foot[c][0] = anchor->x + w[0];
        foot[c][1] = anchor->z + w[2];
        for (int k = 0; k < 2; ++k) { lo[k] = (std::min)(lo[k], foot[c][k]); hi[k] = (std::max)(hi[k], foot[c][k]); }
    }
    const float fx = 0.5f * (lo[0] + hi[0]), fz = 0.5f * (lo[1] + hi[1]);
    float footReach = 0.0f;
    for (const auto& f : foot) footReach = (std::max)(footReach, std::hypot(f[0] - fx, f[1] - fz));
    // the four pillars round it: the nearest in each quarter round the footprint's middle
    const Placement* pillar[4] = {};
    float best[4] = {1e30f, 1e30f, 1e30f, 1e30f}, ptop[4] = {};
    for (const Placement& p : placed) {
        float mn[3], mx[3];
        if (!modelOf(p) || !IsPillar(p.model) || !boundsOf(p.model, mn, mx) || p.y + p.scaleY * mx[1] < kWallMinHeight) continue;
        const int q = (p.x >= fx ? 1 : 0) | (p.z >= fz ? 2 : 0);
        const float d = std::hypot(p.x - fx, p.z - fz);
        if (d <= footReach) continue;   // (inside its footprint)
        if (d < best[q]) { best[q] = d; pillar[q] = &p; ptop[q] = p.y + p.scaleY * mx[1]; }
    }
    for (const Placement* p : pillar)
        if (!p) { *why = "no pillar in every quarter round the highest piece"; return false; }
    float cx = 0.0f, cz = 0.0f;
    for (const Placement* p : pillar) { cx += 0.25f * p->x; cz += 0.25f * p->z; }
    if (onPiece) { cx = fx; cz = fz; }
    float radius = 0.0f, reach = 0.0f;
    for (const Placement* p : pillar) radius = (std::max)(radius, std::hypot(p->x - cx, p->z - cz));
    for (const auto& f : foot) reach = (std::max)(reach, std::hypot(f[0] - cx, f[1] - cz));
    if (!(reach < radius)) { *why = "the highest piece stands out past its pillars"; return false; }
    const float springs = Median({ptop[0], ptop[1], ptop[2], ptop[3]});
    if (!(top > springs)) { *why = "nothing stands higher than the pillars"; return false; }
    Dome& d = *out;
    d.anchor[0] = anchor->x; d.anchor[1] = anchor->y; d.anchor[2] = anchor->z;
    d.witness[0] = pillar[0]->x; d.witness[1] = pillar[0]->y; d.witness[2] = pillar[0]->z;
    d.centreX = cx;
    d.centreZ = cz;
    d.radius = radius;
    d.top = top;
    d.reach = reach;
    d.springs = springs;
    d.rise = RiseOver(top - springs, reach, radius);
    for (const Placement& p : placed) d.half = (std::max)(d.half, (std::max)(std::fabs(p.x - cx), std::fabs(p.z - cz)));   // (the preset's reach; Settings adds its margin)
    return true;
}

// The dome as the file writes it (its numbers to 4 places, Format's): the vault's mesh made from these is the same whether the dome was
// measured now or read back from the last make - the same file, not written again
inline float AsWritten(float v) {
    char b[48];
    std::snprintf(b, sizeof b, "%.4f", v);
    std::istringstream in(b);
    in.imbue(std::locale::classic());
    float r = v;
    in >> r;
    return r;
}
inline void AsWritten(Dome* d) {
    for (float* v : {&d->anchor[0], &d->anchor[1], &d->anchor[2], &d->witness[0], &d->witness[1], &d->witness[2], &d->centreX, &d->centreZ,
                     &d->radius, &d->top, &d->reach, &d->springs, &d->rise, &d->half})
        *v = AsWritten(*v);
}

// ---- the game's own pieces, found ([ceiling] game_ceiling_dome; vrcam reads them from the renderer: knockprops::PresetPieces) ---------

// One piece as the renderer has it: its parent (the preset's root) and its place under it, and where it is drawn
struct Piece { uint32_t parent = 0; float local[3] = {}, world[3] = {}; };
constexpr float kPlaceMatch = 1e-3f;   // a place in the preset, as written there (the props' catalogue matches the same way)
constexpr float kWorldMatch = 0.05f;   // two pieces as far apart drawn as in the preset

// The anchor's place in the world: a piece at its place in the preset whose parent also has a piece at the witness's, the two as far
// apart drawn as in the preset (the preset not turned or scaled). The number of such anchors (`world` the anchor's when it is 1).
inline int MatchPreset(const std::vector<Piece>& pieces, const float anchor[3], const float witness[3], float world[3]) {
    auto at = [](const float a[3], const float b[3], float tol) {
        return std::fabs(a[0] - b[0]) <= tol && std::fabs(a[1] - b[1]) <= tol && std::fabs(a[2] - b[2]) <= tol;
    };
    int found = 0;
    for (const Piece& a : pieces) {
        if (!at(a.local, anchor, kPlaceMatch)) continue;
        bool agrees = false;
        for (const Piece& w : pieces) {
            if (&w == &a || w.parent != a.parent || !at(w.local, witness, kPlaceMatch)) continue;
            const float drawn[3] = {w.world[0] - a.world[0], w.world[1] - a.world[1], w.world[2] - a.world[2]};
            const float placed[3] = {witness[0] - anchor[0], witness[1] - anchor[1], witness[2] - anchor[2]};
            if (at(drawn, placed, kWorldMatch)) { agrees = true; break; }
        }
        if (!agrees) continue;
        if (++found == 1) std::memcpy(world, a.world, 3 * sizeof(float));
    }
    return found;
}

// ---- the file ------------------------------------------------------------------------------------------------------------------------

struct Measures {
    bool haveBeam = false;
    Beam beam;
    std::vector<std::pair<std::string, float>> walls;   // area -> its walls' top, model (= world) units
    std::vector<Dome> domes;   // the cathedral's (slot 0) and the domes over tall pieces, each its slot
    int dropped = 0;           // domes measured but not made this time (a vault's file not ours): the next make measures again
};
// An area's domes (`cathedral`: its own, slot 0; `tall`: the others)
inline std::vector<Dome> DomesOf(const Measures& m, const std::string& biome, bool cathedral, bool tall) {
    std::vector<Dome> out;
    for (const Dome& d : m.domes)
        if (d.biome == biome && (d.slot == 0 ? cathedral : tall)) out.push_back(d);
    return out;
}

inline std::string Format(const Measures& m) {
    char line[256];
    std::string s = "# D2R VR - the game's rooms measured from its own models by D2R_VR_Settings ([ceiling] height_from_walls, game_ceiling_beams, game_ceiling_dome)\n";
    s += "version " + std::to_string(kVersion) + "\n";
    if (m.haveBeam) {
        std::snprintf(line, sizeof line, "beam %.4f %.4f %.4f %.4f %d %d %u\n", m.beam.periodX, m.beam.periodZ, m.beam.bottom, m.beam.top,
                      m.beam.cols, m.beam.rows, m.beam.indices);
        s += line;
    }
    for (const auto& w : m.walls) {
        std::snprintf(line, sizeof line, "walls %s %.2f\n", w.first.c_str(), w.second);
        s += line;
    }
    if (m.dropped > 0) {
        std::snprintf(line, sizeof line, "dropped %d\n", m.dropped);
        s += line;
    }
    for (const Dome& d : m.domes) {   // (the places in the preset as written there: the game's pieces are found by them)
        std::snprintf(line, sizeof line, "dome %s %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %u %d\n", d.biome.c_str(),
                      d.anchor[0], d.anchor[1], d.anchor[2], d.witness[0], d.witness[1], d.witness[2], d.centreX, d.centreZ, d.radius, d.top,
                      d.reach, d.springs, d.rise, d.half, d.indices, d.slot);
        s += line;
    }
    return s;
}

// False: not ours, or of another version (nothing taken)
inline bool Parse(const std::string& text, Measures* out) {
    *out = Measures{};
    bool version = false;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        in.imbue(std::locale::classic());
        std::string w;
        if (!(in >> w)) continue;
        if (w == "version") {
            int v = 0;
            if (!(in >> v) || v != kVersion) { *out = Measures{}; return false; }
            version = true;
        } else if (w == "beam") {
            Beam b;
            if ((in >> b.periodX >> b.periodZ >> b.bottom >> b.top >> b.cols >> b.rows >> b.indices) && b.periodX > 0.0f && b.periodZ > 0.0f &&
                b.top >= b.bottom && b.cols > 0 && b.rows > 0) {
                out->beam = b;
                out->haveBeam = true;
            }
        } else if (w == "walls") {
            std::string name;
            float h = 0.0f;
            if ((in >> name >> h) && h > 0.0f) out->walls.emplace_back(name, h);
        } else if (w == "dropped") {
            int n = 0;
            if (in >> n) out->dropped = n;
        } else if (w == "dome") {
            Dome d;
            if ((in >> d.biome >> d.anchor[0] >> d.anchor[1] >> d.anchor[2] >> d.witness[0] >> d.witness[1] >> d.witness[2] >> d.centreX >> d.centreZ >>
                 d.radius >> d.top >> d.reach >> d.springs >> d.rise >> d.half >> d.indices >> d.slot) &&
                d.radius > 0.0f && d.reach >= 0.0f && d.reach < d.radius && d.top > d.springs && d.rise > 0.0f && d.half > 0.0f && d.indices > 0 &&
                d.slot >= 0) {
                out->domes.push_back(d);
            }
        }
    }
    if (!version) *out = Measures{};
    return version;
}

// The area's walls' top (0: not measured)
inline float WallsTopOf(const Measures& m, const std::string& biome) {
    for (const auto& w : m.walls) if (w.first == biome) return w.second;
    return 0.0f;
}

}  // namespace ceilmeasure
