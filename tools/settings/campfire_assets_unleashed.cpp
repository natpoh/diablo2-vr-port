// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem, Kudlatytrue and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed steamvr/prepare_campfire_sky.py at 79b921a (Kudlatytrue, bearstonem): every step of generate() and the
// options it is driven by (parse() / configure(), TUNED_SCENE), ported line by line with the same numbers and the same order of
// arithmetic, so that the same game gives the same file byte for byte (cleanroom/tests/campfire_assets_unleashed_test.cpp compares it
// with the script's). Changed for D2R VR Authentic: a small JSON reader / writer of our own stands in for Python's json module (keys in
// their order, numbers written as Python writes them); the game's files are read through CascLib (MIT, Ladislav Zezula), read only;
// the result goes into our mod's data folder with arcane_sky.h's receipt instead of Unleashed's installer and its receipt. What comes
// out is Blizzard's file changed and stays on this machine, inside the mod: never in the repository or a build. MPL 2.0, see
// campfire_assets_unleashed.h.

#include "campfire_assets_unleashed.h"
#include "lang.h"   // lang::TX: the result line in the player's language (English in the tests)

#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "CascLib.h"
#include "arcane_sky.h"    // the receipt and its hash, the same as the Arcane sky's

namespace campfireassets {
namespace {

// ================================================================ a small JSON DOM, as Python's json module reads and writes it

struct Bad : std::runtime_error {
    explicit Bad(const std::string& why) : std::runtime_error(why) {}
};

struct Member;
struct Value {
    enum Kind : uint8_t { Null, Bool, Int, Float, String, Array, Object };
    Kind kind = Null;
    bool b = false;
    int64_t i = 0;          // Int (past int64: wrapped, its text in s)
    double d = 0.0;         // Float
    std::string s;          // String; an Int past int64: its digits
    std::vector<Value> a;   // Array
    std::vector<Member> o;  // Object, keys in their order (Python's dict)
};
struct Member {
    std::string key;
    Value value;
};

Value MakeFloat(double d) { Value v; v.kind = Value::Float; v.d = d; return v; }
Value MakeInt(int64_t i) { Value v; v.kind = Value::Int; v.i = i; return v; }
Value MakeBool(bool b) { Value v; v.kind = Value::Bool; v.b = b; return v; }
Value MakeStr(std::string s) { Value v; v.kind = Value::String; v.s = std::move(s); return v; }
Value MakeObj() { Value v; v.kind = Value::Object; return v; }
Value MakeArr() { Value v; v.kind = Value::Array; return v; }

bool IsDict(const Value* v) { return v && v->kind == Value::Object; }
bool IsList(const Value* v) { return v && v->kind == Value::Array; }

// d.get(key): nullptr when not there. Python's .get on a value that is no dict is an AttributeError.
const Value* Get(const Value& obj, const char* key) {
    if (obj.kind != Value::Object) throw Bad("not a JSON object");
    for (const Member& m : obj.o) if (m.key == key) return &m.value;
    return nullptr;
}
Value* Get(Value& obj, const char* key) { return const_cast<Value*>(Get(static_cast<const Value&>(obj), key)); }

// d[key] = v: in its place when there, else last (Python's dict).
Value& Set(Value& obj, const char* key, Value v) {
    if (obj.kind != Value::Object) throw Bad("not a JSON object");
    for (Member& m : obj.o) if (m.key == key) { m.value = std::move(v); return m.value; }
    obj.o.push_back(Member{key, std::move(v)});
    return obj.o.back().value;
}
Value& Set(Value& obj, const char* key, double d) { return Set(obj, key, MakeFloat(d)); }

// d.setdefault(key, v)
Value& SetDefault(Value& obj, const char* key, Value v) {
    if (Value* have = Get(obj, key)) return *have;
    obj.o.push_back(Member{key, std::move(v)});
    return obj.o.back().value;
}

// d[key]: a KeyError when not there.
Value& Need(Value& obj, const char* key) {
    Value* v = Get(obj, key);
    if (!v) throw Bad(std::string("no '") + key + "'");
    return *v;
}

std::string Lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}
bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

// repr(float), as json.dumps writes a float
std::string Repr(double d) {
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d < 0 ? "-Infinity" : "Infinity";
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, d, std::chars_format::scientific);   // shortest round trip, d.ddde+XX
    std::string s(buf, r.ptr);
    const bool neg = !s.empty() && s[0] == '-';
    if (neg) s.erase(0, 1);
    const size_t e = s.find('e');
    std::string digits;
    for (size_t k = 0; k < e; ++k) if (s[k] != '.') digits += s[k];
    const int decpt = atoi(s.c_str() + e + 1) + 1;
    const int n = (int)digits.size();
    std::string out;
    if (decpt <= -4 || decpt > 16) {
        out = digits.substr(0, 1);
        if (n > 1) out += "." + digits.substr(1);
        const int x = decpt - 1;
        std::string es = std::to_string(x < 0 ? -x : x);
        if (es.size() < 2) es = "0" + es;
        out += std::string("e") + (x < 0 ? "-" : "+") + es;
    } else if (decpt <= 0) {
        out = "0." + std::string(-decpt, '0') + digits;
    } else if (decpt >= n) {
        out = digits + std::string(decpt - n, '0') + ".0";
    } else {
        out = digits.substr(0, decpt) + "." + digits.substr(decpt);
    }
    return neg ? "-" + out : out;
}

// float(text), Python's: spaces round it, a sign, inf / nan
double FloatText(const std::string& text) {
    size_t a = 0, b = text.size();
    while (a < b && isspace((unsigned char)text[a])) ++a;
    while (b > a && isspace((unsigned char)text[b - 1])) --b;
    std::string t = text.substr(a, b - a);
    if (!t.empty() && t[0] == '+') t.erase(0, 1);
    if (t.empty() || t[0] == '+') throw Bad("could not convert string to float: '" + text + "'");
    double d = 0.0;
    const auto r = std::from_chars(t.data(), t.data() + t.size(), d, std::chars_format::general);
    if (r.ptr != t.data() + t.size()) throw Bad("could not convert string to float: '" + text + "'");
    if (r.ec == std::errc::result_out_of_range) d = strtod(t.c_str(), nullptr);   // 1e400 -> inf, 1e-400 -> 0, as Python
    else if (r.ec != std::errc()) throw Bad("could not convert string to float: '" + text + "'");
    return d;
}

// float(v) / float(d.get(key, dflt))
double Float(const Value* v, double dflt) {
    if (!v) return dflt;
    switch (v->kind) {
    case Value::Int: return v->s.empty() ? (double)v->i : strtod(v->s.c_str(), nullptr);
    case Value::Float: return v->d;
    case Value::Bool: return v->b ? 1.0 : 0.0;
    case Value::String: return FloatText(v->s);
    default: throw Bad("float() of a JSON value that is no number");
    }
}

// int(v) / int(d.get(key, dflt)) (only ever multiplied and masked to 32 bits, or compared with such)
int64_t Int(const Value* v, int64_t dflt) {
    if (!v) return dflt;
    switch (v->kind) {
    case Value::Int: return v->i;
    case Value::Bool: return v->b ? 1 : 0;
    case Value::Float: {
        if (!std::isfinite(v->d)) throw Bad("cannot convert float to integer");
        const double t = std::trunc(v->d);
        if (std::fabs(t) < 9.0e18) return (int64_t)t;
        return (int64_t)(uint64_t)std::fmod(std::fabs(t), 18446744073709551616.0) * (t < 0 ? -1 : 1);
    }
    case Value::String: {
        const double t = FloatText(v->s);
        if (t != std::trunc(t)) throw Bad("invalid literal for int(): '" + v->s + "'");
        return (int64_t)t;
    }
    default: throw Bad("int() of a JSON value that is no number");
    }
}

std::string IntText(const Value& v) { return v.s.empty() ? std::to_string(v.i) : v.s; }

// str(v) (a name, a file name; Python's repr for the rest - only ever matched or put in a name)
std::string Str(const Value& v) {
    switch (v.kind) {
    case Value::Null: return "None";
    case Value::Bool: return v.b ? "True" : "False";
    case Value::Int: return IntText(v);
    case Value::Float: {
        if (std::isnan(v.d)) return "nan";
        if (std::isinf(v.d)) return v.d < 0 ? "-inf" : "inf";
        return Repr(v.d);
    }
    case Value::String: return v.s;
    case Value::Array: {
        std::string r = "[";
        for (size_t k = 0; k < v.a.size(); ++k) r += (k ? ", " : "") + (v.a[k].kind == Value::String ? "'" + v.a[k].s + "'" : Str(v.a[k]));
        return r + "]";
    }
    default: {
        std::string r = "{";
        for (size_t k = 0; k < v.o.size(); ++k)
            r += (k ? ", '" : "'") + v.o[k].key + "': " + (v.o[k].value.kind == Value::String ? "'" + v.o[k].value.s + "'" : Str(v.o[k].value));
        return r + "}";
    }
    }
}
std::string StrOr(const Value* v, const char* dflt) { return v ? Str(*v) : std::string(dflt); }

// truthiness (c.get('filename') in an if)
bool Truthy(const Value* v) {
    if (!v) return false;
    switch (v->kind) {
    case Value::Null: return false;
    case Value::Bool: return v->b;
    case Value::Int: return v->i != 0 || !v->s.empty();
    case Value::Float: return v->d != 0.0;
    case Value::String: return !v->s.empty();
    case Value::Array: return !v->a.empty();
    default: return !v->o.empty();
    }
}

bool IsStr(const Value* v, const char* s) { return v && v->kind == Value::String && v->s == s; }

// ---------------------------------------------------------------- reading (json.loads)

struct Reader {
    const char* p;
    const char* end;
    int depth = 0;

    [[noreturn]] void Fail(const char* what) { throw Bad(std::string("the preset is not JSON (") + what + ")"); }
    void Space() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }
    bool Word(const char* w) {
        const size_t n = strlen(w);
        if ((size_t)(end - p) >= n && memcmp(p, w, n) == 0) { p += n; return true; }
        return false;
    }
    static void PutUtf8(std::string& out, uint32_t cp) {   // a lone surrogate as its three bytes (Python keeps it; writing it fails)
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | cp >> 6); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | cp >> 12); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | cp >> 18); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }
    uint32_t Hex4() {
        if (end - p < 4) Fail("short \\u escape");
        uint32_t v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
            else Fail("bad \\u escape");
        }
        return v;
    }
    std::string String() {
        ++p;   // "
        std::string out;
        for (;;) {
            if (p >= end) Fail("unterminated string");
            const unsigned char c = (unsigned char)*p;
            if (c == '"') { ++p; return out; }
            if (c < 0x20) Fail("control character in a string");
            if (c == '\\') {
                ++p;
                if (p >= end) Fail("unterminated string");
                const char e = *p++;
                switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp = Hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                        const char* back = p;
                        p += 2;
                        const uint32_t lo = Hex4();
                        if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else p = back;
                    }
                    PutUtf8(out, cp);
                    break;
                }
                default: Fail("bad escape");
                }
                continue;
            }
            if (c < 0x80) { out += (char)c; ++p; continue; }
            // UTF-8 (surrogates let through, as Python's surrogatepass reading)
            int n = c >= 0xF0 && c <= 0xF4 ? 3 : c >= 0xE0 ? (c <= 0xEF ? 2 : -1) : c >= 0xC2 ? 1 : -1;
            if (n < 0 || end - p <= n) Fail("bad UTF-8");
            for (int k = 1; k <= n; ++k) if (((unsigned char)p[k] & 0xC0) != 0x80) Fail("bad UTF-8");
            if (c == 0xE0 && (unsigned char)p[1] < 0xA0) Fail("bad UTF-8");
            if (c == 0xF0 && (unsigned char)p[1] < 0x90) Fail("bad UTF-8");
            if (c == 0xF4 && (unsigned char)p[1] > 0x8F) Fail("bad UTF-8");
            out.append(p, (size_t)n + 1);
            p += n + 1;
        }
    }
    Value Number() {
        const char* s = p;
        if (p < end && *p == '-') ++p;
        if (p < end && *p == '0') ++p;
        else if (p < end && *p >= '1' && *p <= '9') { while (p < end && isdigit((unsigned char)*p)) ++p; }
        else Fail("bad number");
        bool isFloat = false;
        if (p + 1 < end && *p == '.' && isdigit((unsigned char)p[1])) {
            isFloat = true;
            ++p;
            while (p < end && isdigit((unsigned char)*p)) ++p;
        }
        if (p < end && (*p == 'e' || *p == 'E')) {
            const char* q = p + 1;
            if (q < end && (*q == '+' || *q == '-')) ++q;
            if (q < end && isdigit((unsigned char)*q)) {
                isFloat = true;
                p = q;
                while (p < end && isdigit((unsigned char)*p)) ++p;
            }
        }
        const std::string text(s, p);
        if (isFloat) return MakeFloat(FloatText(text));
        Value v;
        v.kind = Value::Int;
        const auto r = std::from_chars(text.data(), text.data() + text.size(), v.i);
        if (r.ec != std::errc()) {   // past int64: its digits kept, the value wrapped (only ever masked to 32 bits)
            const bool neg = text[0] == '-';
            uint64_t w = 0;
            for (char c : text) if (c != '-') w = w * 10 + (uint64_t)(c - '0');
            v.i = (int64_t)(neg ? (0 - w) : w);
            v.s = text == "-0" ? std::string("0") : text;
        }
        return v;
    }
    Value Parse() {
        Space();
        if (p >= end) Fail("unexpected end");
        if (++depth > 900) Fail("nested too deep");
        Value v;
        const char c = *p;
        if (c == '{') {
            ++p;
            v.kind = Value::Object;
            Space();
            if (p < end && *p == '}') { ++p; --depth; return v; }
            for (;;) {
                Space();
                if (p >= end || *p != '"') Fail("expected a key");
                std::string key = String();
                Space();
                if (p >= end || *p != ':') Fail("expected ':'");
                ++p;
                Value item = Parse();
                bool seen = false;   // a key twice: the last value in the first one's place (Python's dict)
                for (Member& m : v.o) if (m.key == key) { m.value = std::move(item); seen = true; break; }
                if (!seen) v.o.push_back(Member{std::move(key), std::move(item)});
                Space();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == '}') { ++p; break; }
                Fail("expected ',' or '}'");
            }
        } else if (c == '[') {
            ++p;
            v.kind = Value::Array;
            Space();
            if (p < end && *p == ']') { ++p; --depth; return v; }
            for (;;) {
                v.a.push_back(Parse());
                Space();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == ']') { ++p; break; }
                Fail("expected ',' or ']'");
            }
        } else if (c == '"') {
            v = MakeStr(String());
        } else if (Word("true")) {
            v = MakeBool(true);
        } else if (Word("false")) {
            v = MakeBool(false);
        } else if (Word("null")) {
            v.kind = Value::Null;
        } else if (Word("NaN")) {
            v = MakeFloat(std::nan(""));
        } else if (Word("Infinity")) {
            v = MakeFloat(HUGE_VAL);
        } else if (Word("-Infinity")) {
            v = MakeFloat(-HUGE_VAL);
        } else {
            v = Number();
        }
        --depth;
        return v;
    }
};

Value ParseJson(const std::string& text) {
    Reader r{text.data(), text.data() + text.size()};
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) r.p += 3;   // (utf-8-sig)
    Value v = r.Parse();
    r.Space();
    if (r.p != r.end) r.Fail("extra data");
    return v;
}

// ---------------------------------------------------------------- writing (json.dumps(v, ensure_ascii=False, indent=2))

void PutString(std::string& out, const std::string& s) {
    out += '"';
    for (size_t k = 0; k < s.size(); ++k) {
        const unsigned char c = (unsigned char)s[k];
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (c < 0x20) {
                char u[8];
                snprintf(u, sizeof u, "\\u%04x", c);
                out += u;
            } else {
                if (c == 0xED && k + 1 < s.size() && (unsigned char)s[k + 1] >= 0xA0) throw Bad("a lone surrogate in a string");   // (Python: UnicodeEncodeError)
                out += (char)c;
            }
        }
    }
    out += '"';
}

void Put(std::string& out, const Value& v, int level) {
    switch (v.kind) {
    case Value::Null: out += "null"; return;
    case Value::Bool: out += v.b ? "true" : "false"; return;
    case Value::Int: out += IntText(v); return;
    case Value::Float: out += Repr(v.d); return;
    case Value::String: PutString(out, v.s); return;
    case Value::Array:
        if (v.a.empty()) { out += "[]"; return; }
        out += '[';
        for (size_t k = 0; k < v.a.size(); ++k) {
            out += k ? ",\n" : "\n";
            out.append((size_t)(level + 1) * 2, ' ');
            Put(out, v.a[k], level + 1);
        }
        out += '\n';
        out.append((size_t)level * 2, ' ');
        out += ']';
        return;
    case Value::Object:
        if (v.o.empty()) { out += "{}"; return; }
        out += '{';
        for (size_t k = 0; k < v.o.size(); ++k) {
            out += k ? ",\n" : "\n";
            out.append((size_t)(level + 1) * 2, ' ');
            PutString(out, v.o[k].key);
            out += ": ";
            Put(out, v.o[k].value, level + 1);
        }
        out += '\n';
        out.append((size_t)level * 2, ' ');
        out += '}';
        return;
    }
}

// `part in json.dumps(v).lower()` for an ASCII part with no quote or backslash in it: in a key or a string, lowered
bool DumpHas(const Value& v, const char* part) {
    switch (v.kind) {
    case Value::String: return Has(Lower(v.s), part);
    case Value::Array:
        for (const Value& x : v.a) if (DumpHas(x, part)) return true;
        return false;
    case Value::Object:
        for (const Member& m : v.o) if (Has(Lower(m.key), part) || DumpHas(m.value, part)) return true;
        return false;
    default: return false;
    }
}

// ================================================================ prepare_campfire_sky.py

const char* const MIRROR = "_pcvr_behind";
const char* const CLOUDS = "fx_uicharactercreate_clouds";
const double FIRE[2] = {1060.67, 123.94};   // rogue_bonfire01_01, x and z
const double SURROUND_MIN = 12.0;
const double SCENE_GATE[2] = {1002.2, 107.6};
const double TOWN_GATE[2] = {49.2, 190.0};
const double TOWN_AT_GATE[2] = {TOWN_GATE[0] + FIRE[0] - SCENE_GATE[0], TOWN_GATE[1] + FIRE[1] - SCENE_GATE[1]};
const double SCENE_SECTOR[2] = {-180.0, -55.0};
const double HERO_SIDE[2] = {100.0, 180.0};
const double HERO_SIDE_CLEAR = 55.0;
const double TOWN_GROUND_DROP = 0.12;
const double WALL_LINE_X = 49.2;
const char* const WALLS = "expansion_town_walls";
// TOWN_DEPENDENCIES = (): none of the town's dependency lists merged (only dependencies' setdefault is left of that step)
const char* const KEPT_COMPONENTS[] = {"TransformDefinitionComponent", "ModelDefinitionComponent", "ModelPlatformTierComponent",
                                       "ModelVariationDefinitionComponent"};
const char* const ANIMATED[] = {"SkeletonDefinitionComponent", "PhysicsClothDefinitionComponent", "PhysicsPartialRagdollDefinitionComponent"};
const char* const CLUTTER[] = {"snowprops", "grass", "weeds", "corn_stalks", "shrubs", "bushes", "/grunge/", "/decal/",
                               "bottles", "pottery", "tavern_props", "vases", "urns", "tableware", "pebbles", "rags", "rugs",
                               "planks", "debris", "weapons", "tools", "shop_props", "docktown", "jungle", "tomb", "barracks",
                               "hanging_props", "decorations", "cottage_props", "furniture", "chests", "lights", "cloth"};
const double WALL_FILL_CLEAR = 10.0;
const double PAVE_STEP[2] = {100.0, 26.0};
const double PAVE_HULL[2] = {-29.0, 83.0};
const double PAVE_DY[3] = {-0.03, -0.05, -0.07};
const char* const LIGHT_TEMPLATE = "charactercreate_fill_point01_04";
const char* const FLOORS[] = {"terrain_charactercreate", "ui_skirt_mud", "ui_rock_pile", "town_ground"};
const char* const SKY = "characterselect_sky.model";
const double SKY_TURNS[2] = {120.0, 240.0};
const double SKY_GROW = 0.004;
const double SKY_WIDEN = 1.02;
const double SKY_RADIUS = 1.1547 * 172.8617;
const double SKY_TOP = 0.8466 * 172.8617;
const char* const CAP = "data/hd/env/model/expansion/mountaintop/expansion_mountaintop_skybox/skybox01.model";
const double CAP_SIZE = 326.12;
const double CAP_COVER = 1.35;
const double GROUND_SINK = 0.06;

const double kPi = 3.14159265358979323846;
const double kDegToRad = kPi / 180.0;   // math.radians: x * (pi / 180)
const double kRadToDeg = 180.0 / kPi;   // math.degrees
double Radians(double x) { return x * kDegToRad; }
double Degrees(double x) { return x * kRadToDeg; }

constexpr uint64_t kMask = 0xFFFFFFFFull;

// ---------------------------------------------------------------- the options (parse() and configure(); the module's settings)

struct Cell { int col = 0, row = 0; };
struct Tilt { Cell a, b; double drop = 0.0; };

struct Options {
    // generate()'s arguments
    bool surround = true;
    bool townAtSet = true;
    double townAt[2] = {TOWN_AT_GATE[0], TOWN_AT_GATE[1]};
    double townTurn = 0.0;
    bool ground = true;
    std::vector<double> skyTurns{SKY_TURNS[0], SKY_TURNS[1]};
    bool flipWall = true;
    bool cap = false;
    bool clutter = false;
    std::vector<std::string> drop;
    bool shadowsOff = true;
    // the module's settings (OPTION_GLOBALS), at their defaults
    double capFace = 1.0;
    std::vector<std::string> copies, grows, hide, lifts, lights, marks, moves, spin;
    double farShift = 0.0;
    bool flipFar = false;
    std::string flipSide = "all";
    std::vector<double> groundTurns;
    bool pave = false;
    double paveArea[4] = {-205.0, 225.0, -210.0, 240.0};
    std::vector<double> paveHole;   // empty = None
    std::vector<std::vector<double>> paveSkip;
    std::vector<Tilt> paveTilt;
    bool points = false;
    bool spotSpecularSet = false;
    double spotSpecular = 0.0;
    bool townBiome = false;
    std::vector<std::pair<Cell, Cell>> townKeep;
    bool townShadows = false;
    bool uiWalls = false;
    double uiWallFarYaw = -90.0;
    double uiWallLength = 26.8;
    bool wallFill = true;
};

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t from = 0;
    for (;;) {
        const size_t at = s.find(sep, from);
        out.push_back(s.substr(from, at == std::string::npos ? std::string::npos : at - from));
        if (at == std::string::npos) return out;
        from = at + 1;
    }
}
std::string LStrip(const std::string& s, char c) { size_t k = 0; while (k < s.size() && s[k] == c) ++k; return s.substr(k); }
std::vector<double> Floats(const std::string& s) {
    std::vector<double> out;
    for (const std::string& t : Split(s, ':')) out.push_back(FloatText(t));
    return out;
}

// parse_cell: 'H5' -> ('ABCDEFGHIJ'.index('H'), int('5'))
Cell ParseCell(const std::string& text) {
    size_t a = 0, b = text.size();
    while (a < b && isspace((unsigned char)text[a])) ++a;
    while (b > a && isspace((unsigned char)text[b - 1])) --b;
    std::string t = text.substr(a, b - a);
    for (char& c : t) c = (char)toupper((unsigned char)c);
    if (t.empty()) throw Bad("an empty grid cell");
    const char* letters = "ABCDEFGHIJ";
    const char* at = strchr(letters, t[0]);
    if (!at || !*at) throw Bad("substring not found");
    Cell c;
    c.col = (int)(at - letters);
    std::string digits = t.substr(1);
    if (!digits.empty() && digits[0] == '+') digits.erase(0, 1);
    const auto r = std::from_chars(digits.data(), digits.data() + digits.size(), c.row);
    if (digits.empty() || r.ec != std::errc() || r.ptr != digits.data() + digits.size()) throw Bad("invalid literal for int(): '" + t.substr(1) + "'");
    return c;
}

// argparse's rule: a word that starts with '-' is an option unless it is a negative number (the parser has no such options)
bool LooksLikeOption(const std::string& w) {
    if (w.size() < 2 || w[0] != '-') return false;
    size_t k = 1;
    bool digits = false;
    while (k < w.size() && isdigit((unsigned char)w[k])) { ++k; digits = true; }
    if (k == w.size() && digits) return false;   // -\d+
    if (k < w.size() && w[k] == '.') {
        ++k;
        bool frac = false;
        while (k < w.size() && isdigit((unsigned char)w[k])) { ++k; frac = true; }
        if (k == w.size() && frac) return false;   // -\d*\.\d+
    }
    return true;
}

// parse() + configure(): the command line's scene options (the game folder left out) into the settings generate() runs with
Options Configure(const std::vector<std::string>& argv) {
    Options o;
    // parse(): the raw values
    bool pave = false, townBiome = false, townShadows = false, noWallFill = false, uiWalls = false, flipFar = false, capFlip = false, points = false;
    bool noSurround = false, mirror = false, noGround = false, noWallFlip = false, cap = false, clutter = false, lightShadows = false;
    double turn = 0.0;
    std::vector<double> town{TOWN_AT_GATE[0], TOWN_AT_GATE[1]}, sky{SKY_TURNS[0], SKY_TURNS[1]}, groundTurns;
    std::vector<std::string> drop, grow, copy, spin, mark, paveSkip, paveTilt, light, move, lift, hide, townKeep;
    std::string paveHole, flipSide = "all";
    bool haveSpotSpecular = false, haveUiWallLength = false, haveUiWallFarYaw = false, havePaveZ0 = false, haveFarShift = false;
    double spotSpecular = 0.0, uiWallLength = 0.0, uiWallFarYaw = 0.0, paveZ0 = 0.0, farShift = 0.0;
    const std::vector<std::pair<const char*, bool*>> flags = {
        {"--write", nullptr}, {"--tuned", nullptr}, {"--replace", nullptr}, {"--no-surround", &noSurround}, {"--mirror", &mirror},
        {"--no-ground", &noGround}, {"--no-wall-flip", &noWallFlip}, {"--cap", &cap}, {"--cap-flip", &capFlip},
        {"--light-shadows", &lightShadows}, {"--spots-as-points", &points}, {"--town-shadows", &townShadows},
        {"--no-wall-fill", &noWallFill}, {"--ui-walls", &uiWalls}, {"--pave", &pave}, {"--town-biome", &townBiome},
        {"--flip-far-wall", &flipFar}, {"--clutter", &clutter}};
    const std::vector<std::pair<const char*, std::vector<std::string>*>> lists = {   // nargs='*' (strings)
        {"--drop", &drop}, {"--grow", &grow}, {"--copy", &copy}, {"--spin", &spin}, {"--mark", &mark}};
    const std::vector<std::pair<const char*, std::vector<std::string>*>> lists1 = {   // nargs='+'
        {"--pave-skip", &paveSkip}, {"--pave-tilt", &paveTilt}, {"--light", &light}, {"--move", &move}, {"--lift", &lift},
        {"--hide", &hide}, {"--town-keep", &townKeep}};
    const std::vector<std::pair<const char*, std::vector<double>*>> floatLists = {{"--ground-turns", &groundTurns}, {"--sky", &sky}};   // nargs='*'
    const std::vector<std::tuple<const char*, bool*, double*>> floats = {
        {"--turn", nullptr, &turn}, {"--spot-specular", &haveSpotSpecular, &spotSpecular}, {"--ui-wall-length", &haveUiWallLength, &uiWallLength},
        {"--ui-wall-far-yaw", &haveUiWallFarYaw, &uiWallFarYaw}, {"--pave-z0", &havePaveZ0, &paveZ0}, {"--far-wall-shift", &haveFarShift, &farShift}};
    size_t k = 0;
    auto values = [&](size_t min) {
        std::vector<std::string> v;
        while (k < argv.size() && !LooksLikeOption(argv[k])) v.push_back(argv[k++]);
        if (v.size() < min) throw Bad("an option without its value");
        return v;
    };
    auto one = [&]() {
        if (k >= argv.size() || LooksLikeOption(argv[k])) throw Bad("an option without its value");
        return argv[k++];
    };
    while (k < argv.size()) {
        const std::string w = argv[k++];
        bool done = false;
        for (auto& f : flags) if (w == f.first) { if (f.second) *f.second = true; done = true; }
        for (auto& l : lists) if (!done && w == l.first) { *l.second = values(0); done = true; }
        for (auto& l : lists1) if (!done && w == l.first) { *l.second = values(1); done = true; }
        for (auto& l : floatLists) if (!done && w == l.first) {
            l.second->clear();
            for (const std::string& t : values(0)) l.second->push_back(FloatText(t));
            done = true;
        }
        for (auto& f : floats) if (!done && w == std::get<0>(f)) {
            *std::get<2>(f) = FloatText(one());
            if (std::get<1>(f)) *std::get<1>(f) = true;
            done = true;
        }
        if (done) continue;
        if (w == "--town") {
            town = {FloatText(one()), FloatText(one())};
        } else if (w == "--pave-hole") {
            paveHole = one();
        } else if (w == "--wall-flip-side") {
            flipSide = one();
            if (flipSide != "all" && flipSide != "north" && flipSide != "south") throw Bad("--wall-flip-side: invalid choice");
        } else if (w == "--casclib") {
            one();
        } else if (w == "--mod") {
            const std::string m = one();
            if (m != "D2R-PCVR" && m != "pcvr-dev") throw Bad("--mod: invalid choice");
        } else {
            throw Bad("unrecognized argument: " + w);
        }
    }
    // configure()
    o.pave = pave;
    if (!paveSkip.empty()) {
        o.paveSkip.clear();
        for (const std::string& t : paveSkip) o.paveSkip.push_back(Floats(LStrip(t, '@')));
    }
    if (!paveTilt.empty()) {
        o.paveTilt.clear();
        for (const std::string& t : paveTilt) {
            const std::vector<std::string> p = Split(t, ':');
            if (p.size() < 3) throw Bad("--pave-tilt: list index out of range");
            o.paveTilt.push_back(Tilt{ParseCell(p[0]), ParseCell(p[1]), FloatText(p[2])});
        }
    }
    if (!paveHole.empty()) o.paveHole = Floats(LStrip(paveHole, '@'));
    if (havePaveZ0) o.paveArea[2] = paveZ0;
    o.lights = light;
    o.moves = move;
    o.lifts.clear();
    for (const std::string& t : lift) o.lifts.push_back(LStrip(t, '@'));
    o.townBiome = townBiome;
    if (!hide.empty()) o.hide = hide;
    if (!townKeep.empty()) {
        o.townKeep.clear();
        for (const std::string& r : townKeep) {
            const std::vector<std::string> p = Split(r, ':');
            if (p.size() != 2) throw Bad("--town-keep: A1:B2");
            o.townKeep.push_back({ParseCell(p[0]), ParseCell(p[1])});
        }
    }
    o.townShadows = townShadows;
    o.wallFill = !noWallFill;
    o.uiWalls = uiWalls;
    o.grows = grow;
    o.copies = copy;
    o.spin = spin;
    o.groundTurns = groundTurns;
    if (haveUiWallLength) o.uiWallLength = uiWallLength;
    if (haveUiWallFarYaw) o.uiWallFarYaw = uiWallFarYaw;
    o.flipFar = flipFar;
    if (haveFarShift) o.farShift = farShift;
    o.marks = mark;
    o.flipSide = flipSide;
    o.capFace = capFlip ? -1.0 : 1.0;
    o.points = points;
    o.spotSpecularSet = haveSpotSpecular;
    o.spotSpecular = spotSpecular;
    o.surround = !noSurround;
    o.townAtSet = !mirror;
    o.townAt[0] = town[0];
    o.townAt[1] = town[1];
    o.townTurn = turn;
    o.ground = !noGround;
    o.skyTurns = sky;
    o.flipWall = !noWallFlip;
    o.cap = cap;
    o.clutter = clutter;
    o.drop = drop;
    o.shadowsOff = !lightShadows;
    return o;
}

// The campfire scene as tuned with Unleashed's owner (2026-11-08/09), TUNED_SCENE: what its installer builds by default.
const char* const TUNED_SCENE[] = {
    "--light-shadows", "--town-shadows", "--no-wall-flip", "--flip-far-wall", "--far-wall-shift", "-20", "--sky",
    "--drop", "ui_skybox_characterSelect_sky", "terrain_characterCreate02", "--spin",
    "structure02_01_pcvr_behind:180", "structure04_01_pcvr_behind:90", "structure06_01_pcvr_behind:-90",
    "structure05_01_pcvr_behind:180", "--grow", "terrain_characterCreate03_01:1.08:-3:0.5", "--copy",
    "@terrain_characterCreate01_01:-168.9:88.4:0.04:90", "@terrain_characterCreate01_01:-148.9:88.4:0.04:90",
    "wall_segment_short_53_pcvr_behind:0:10", "wall_segment_short_53_pcvr_behind:0:20",
    "wall_segment_short_10_pcvr_behind:10:0", "wall_segment_short_10_pcvr_behind:45:0",
    "wall_segment_short_09_pcvr_behind:-10:0", "wall_segment_short_09_pcvr_behind:-20:0",
    "@wall_segment_short_81_pcvr_behind:145:108:0", "@wall_segment_short_81_pcvr_behind:145:118:0",
    "@wall_segment_short_81_pcvr_behind:145:128:0", "terrain_characterCreate03_01:-27:0",
    "terrain_characterCreate03_01:-27:37", "terrain_characterCreate03_01:0:37",
    "terrain_characterCreate03_01:27:37", "terrain_characterCreate03_01:27:0", "boulder03_04:-8:41",
    "boulder06_09:0:37", "rock03_10:6:30", "boulder06_12:3:40", "expansion_grass_moorland02_01:0:37",
    "expansion_bush_shad01_07:0:37", "expansion_grass_moorland01_14:4:30", "boulder03_01:-29:37",
    "boulder06_01:-35:45", "rock03_01:-40:30", "expansion_bush_shad02_07:-29:37",
    "expansion_grass_moorland02_07:-20:30", "expansion_grass_moorland01_05:-15:45",
    "@ui_skirt_mud_fill_01:-6:48:-0.36:0:0.45", "@ui_skirt_mud_fill_01:-36:30:-0.35:0:0.45",
    "@ui_skirt_mud_fill_01:8:27:-0.69:0:0.5", "@ui_skirt_mud_fill_01:20:26:-0.68:25:0.5",
    "@ui_skirt_mud_fill_01:32:27:-0.67:-20:0.5", "@wall_stone_medium_a01_03_pcvr_behind:63:-152:-19:0:0.3",
    "@wall_stone_medium_a01_03_pcvr_behind:67:-161:-19:40:0.3",
    "@wall_stone_medium_a01_03_pcvr_behind:57:-158:-19:-30:0.3",
    "@wall_stone_medium_a01_03_pcvr_behind:73:-150:-19:15:0.3",
    "@wall_stone_medium_a01_03_pcvr_behind:77:-157:-19:-50:0.3",
    "@wall_stone_medium_a01_03_pcvr_behind:70:-147:-29:20:0.3",
    "@wall_stone_medium_a01_03_pcvr_behind:74:-154:-29:-40:0.3",
    "@wall_stone_medium_a01_03_pcvr_behind:66:-152:-29:70:0.3", "@ui_skirt_mud_fill_01:3:52:-0.36:85:0.5",
    "@ui_skirt_mud_fill_01:3:63:-0.35:100:0.45", "@boulder03_04:8:34:0", "@rock03_10:21:35:0",
    "@boulder06_09:-1:52:0", "wall_stone_large01_02_pcvr_behind:-30:0", "wall_stone_large01_02_pcvr_behind:-60:0",
    "wall_stone_large01_02_pcvr_behind:-90:0", "wall_stone_medium_a01_03_pcvr_behind:-92:0",
    "@wall_stone_large01_01_pcvr_behind:-20:-183.2:0", "@wall_stone_medium_a01_01_pcvr_behind:-46:-189.2:0",
    "wall_stone_large01_02_pcvr_behind:-30:-16", "wall_stone_large01_02_pcvr_behind:-60:-16",
    "expansion_bush_shad02_07:-25:42", "expansion_bush_shad01_06:-52:58", "expansion_bush_shad02_01:-46:72",
    "expansion_grass_moorland02_06:-34:75", "@ui_skirt_mud_fill_01:45:15:-0.3", "@ui_skirt_mud_fill_01:45:60:0.02",
    "@ui_skirt_mud_fill_01:28:-17:0.05", "@ui_skirt_mud_fill_01:85:14:0.06",
    "@firewood_scattered01_01_pcvr_behind:-30:40:0.075:35:1", "@log_stack02_01_pcvr_behind:-50:60:0.075:80:0.8",
    "@firewood_log01_02:-22:52:0.075:120", "@firewood_log02_02:-52:40:-0.525:60", "@stick01_03:-26:34:0.075:20",
    "@stick02_03:-35:47:0.075:140", "@stick03_05:-18:44:0.075:250", "@stick01_03:-48:30:0.075:300",
    "@rock03_01:-15:58:0.075:0", "@rock_small03_03:-28:27:0.075:90", "@rock_small03_03:-40:52:0.075:200",
    "@boulder06_12:-52:45:0.075:45", "@boulder03_01:-12:36:0.075:160",
    "@rocks_e01_01_pcvr_behind:-33:62:0.075:30:0.7", "@rock_cluster01_01_pcvr_behind:-52:22:0.075:0",
    "@expansion_grass_moorland01_20:-24:57:0.075:0", "@expansion_grass_moorland02_03:-20:30:0.075:90",
    "@expansion_grass_moorland01_20:-50:50:0.075:180", "@expansion_grass_moorland02_03:-14:48:0.075:270",
    "@expansion_grass_moorland01_20:-22:24:0.075:45", "@expansion_bush_dogwood01_03:-54:36:0.075:0:0.8",
    "@expansion_bush_dogwood03_07:-20:62:0.075:90:0.8", "@ui_skirt_mud_fill_01:-26:44:-0.55:30:0.3",
    "@ui_skirt_mud_fill_01:-45:55:-0.5:120:0.35", "@ui_skirt_mud_fill_01:-50:28:-0.47:200:0.4",
    "@chopping_stump_01_pcvr_behind:-42:46:0.075:0", "@wall_segment_short_81_pcvr_behind:72:-160:0:70:0.7",
    "@wall_segment_short_81_pcvr_behind:80:-157:0:70:0.7", "@firewood_log01_02:-8:25:0.075:40",
    "@stick01_03:-14:30:0.075:110", "@stick02_03:-4:22:0.075:200", "@stick03_05:-10:40:0.075:330",
    "@stick02_03:0:52:0.075:60", "@rock_small03_03:-15:22:0.075:30", "@rock_small03_03:-2:28:0.075:150",
    "@rock03_01:-16:46:0.075:270", "@boulder03_01:-10:55:0.075:80",
    "@rocks_e01_01_pcvr_behind:-6:33:0.075:120:0.5", "@expansion_grass_moorland01_20:-12:28:0.075:200",
    "@expansion_grass_moorland02_03:-1:40:0.075:30", "@expansion_grass_moorland01_20:-16:38:0.075:300",
    "@expansion_grass_moorland02_03:-7:57:0.075:160", "@expansion_bush_dogwood03_07:-14:50:0.075:200:0.6",
    "@ui_skirt_mud_fill_01:-9:33:-0.55:300:0.3", "@ui_skirt_mud_fill_01:-3:22:-0.53:60:0.28", "--town-keep",
    "H5:J9", "--hide", "wall_corner01_02_pcvr_behind", "wall_end_l_03_pcvr_behind",
    "wall_crenelation_14_pcvr_behind", "--pave", "--pave-z0", "-204", "--pave-hole", "@-44:38:-19:57", "--move",
    "structure02_01_pcvr_behind:0:-0.5:0", "structure06_1_01:0:-0.5:0", "--lift", "@38.5:-28.2:8:0.4",
};

// ---------------------------------------------------------------- the steps

using Ids = std::unordered_set<int64_t>;

// e.get('components', []): the list, nullptr when not there (iterating anything else fails in Python)
Value* Components(Value& e) {
    Value* c = Get(e, "components");
    if (!c) return nullptr;
    if (c->kind != Value::Array) throw Bad("components is no list");
    for (Value& x : c->a) if (x.kind != Value::Object) throw Bad("a component is no object");
    return c;
}
// comps = e.get('components', []) if isinstance(e, dict) else []
Value* ComponentsIfDict(Value& e) { return e.kind == Value::Object ? Components(e) : nullptr; }

bool IsType(const Value& c, const char* type) { return IsStr(Get(c, "type"), type); }
std::string TypeStr(const Value& c) { return StrOr(Get(c, "type"), ""); }

// next((c for c in comps if c.get('type') == 'TransformDefinitionComponent' and isinstance(c.get('position'), dict)), None)
Value* PlacedTransform(Value* comps) {
    if (!comps) return nullptr;
    for (Value& c : comps->a) if (IsType(c, "TransformDefinitionComponent") && IsDict(Get(c, "position"))) return &c;
    return nullptr;
}
// str(next((c.get('filename', '') for c in comps if c.get('type') == 'ModelDefinitionComponent'), '')).lower()
std::string ModelName(Value* comps) {
    if (!comps) return "";
    for (Value& c : comps->a) if (IsType(c, "ModelDefinitionComponent")) return Lower(StrOr(Get(c, "filename"), ""));
    return "";
}

Ids IdsOf(Value& entities) {
    Ids ids;
    for (Value& e : entities.a) if (e.kind == Value::Object) ids.insert(Int(Get(e, "id"), 0));
    return ids;
}
uint32_t Fresh(uint64_t start, Ids& ids, bool notZero) {
    uint64_t v = start & kMask;
    while (ids.count((int64_t)v) || (notZero && v == 0)) v = (v + 0x01000193) & kMask;
    ids.insert((int64_t)v);
    return (uint32_t)v;
}

// q = qy * q, (bx, by, bz, bw) * (x0, y0, z0, w0) - the script's terms in its order
void TurnQ(Value& q, double bx, double by, double bz, double bw) {
    const double x0 = Float(Get(q, "x"), 0.0), y0 = Float(Get(q, "y"), 0.0), z0 = Float(Get(q, "z"), 0.0), w0 = Float(Get(q, "w"), 0.0);
    Set(q, "x", bw * x0 + bx * w0 + by * z0 - bz * y0);
    Set(q, "y", bw * y0 - bx * z0 + by * w0 + bz * x0);
    Set(q, "z", bw * z0 + bx * y0 - by * x0 + bz * w0);
    Set(q, "w", bw * w0 - bx * x0 - by * y0 - bz * z0);
}
// (0, 1, 0, 0) * q: half a turn about +y
void HalfTurnQ(Value& q) {
    const double x = Float(Get(q, "x"), 0.0), y = Float(Get(q, "y"), 0.0), z = Float(Get(q, "z"), 0.0), w = Float(Get(q, "w"), 0.0);
    Set(q, "x", z);
    Set(q, "y", w);
    Set(q, "z", -x);
    Set(q, "w", -y);
}

Value* FindByName(Value& entities, const std::string& name) {   // str(e.get('name', '')).lower() == name.lower()
    const std::string want = Lower(name);
    for (Value& e : entities.a) if (e.kind == Value::Object && Lower(StrOr(Get(e, "name"), "")) == want) return &e;
    return nullptr;
}

struct Scene {
    const Options& o;
    Value& result;
    Value* entities() { return &Need(result, "entities"); }

    // turned_half
    Value TurnedHalf(const Value& entity, Ids& ids) {
        Value e = entity;
        Set(e, "name", MakeStr(StrOr(Get(e, "name"), "entity") + MIRROR));
        const uint32_t id = Fresh((uint64_t)Int(Get(e, "id"), 0) * 2654435761ull + 0x9E3779B9ull, ids, true);
        Set(e, "id", MakeInt(id));
        if (Value* comps = Components(e))
            for (Value& c : comps->a) {
                if (!IsType(c, "TransformDefinitionComponent")) continue;
                Value* p = Get(c, "position");
                Value* q = Get(c, "orientation");
                if (IsDict(p)) {
                    Set(*p, "x", 2 * FIRE[0] - Float(Get(*p, "x"), 0.0));
                    Set(*p, "z", 2 * FIRE[1] - Float(Get(*p, "z"), 0.0));
                }
                if (IsDict(q)) HalfTurnQ(*q);
            }
        return e;
    }

    // placed_model: the distance from the fire, or < 0 for None
    double PlacedModel(Value& entity) {
        Value* comps = ComponentsIfDict(entity);
        if (!comps) return -1.0;
        bool model = false;
        for (Value& c : comps->a) {
            const std::string k = TypeStr(c);
            if (Has(k, "Light") || Has(k, "Vfx") || Has(k, "Sound") || Has(k, "Audio")) return -1.0;
            if (Has(k, "Model")) model = true;
        }
        Value* pos = nullptr;
        bool found = false;
        for (Value& c : comps->a) if (IsType(c, "TransformDefinitionComponent")) { pos = Get(c, "position"); found = true; break; }
        if (!found || !IsDict(pos) || !model) return -1.0;
        const double x = Float(Get(*pos, "x"), 0.0), z = Float(Get(*pos, "z"), 0.0);
        return std::pow(std::pow(x - FIRE[0], 2.0) + std::pow(z - FIRE[1], 2.0), 0.5);
    }

    bool InTownKeep(double dx, double dz) {
        const double c = (200.0 - dz) / 40.0, r = (dx + 200.0) / 40.0 + 1.0;   // grid_cell
        for (const auto& k : o.townKeep)
            if (k.first.col <= c && c < k.second.col + 1 && k.first.row <= r && r < k.second.row + 1) return true;
        return false;
    }
    static bool InSceneHalf(double dx, double dz) {
        const double a = Degrees(std::atan2(dz, dx));
        return SCENE_SECTOR[0] <= a && a <= SCENE_SECTOR[1];
    }
    static bool BesideHeroes(double dx, double dz) {
        const double a = Degrees(std::atan2(dz, dx));
        return HERO_SIDE[0] <= a && a <= HERO_SIDE[1] && std::hypot(dx, dz) < HERO_SIDE_CLEAR;
    }

    // scene_walls
    std::vector<std::pair<double, double>> SceneWalls() {
        std::vector<std::pair<double, double>> out;
        Value* list = Get(result, "entities");
        if (!list) return out;
        if (list->kind != Value::Array) throw Bad("entities is no list");
        for (Value& e : list->a) {
            Value* comps = ComponentsIfDict(e);
            const std::string model = ModelName(comps);
            const size_t slash = model.rfind('/');
            const std::string name = slash == std::string::npos ? model : model.substr(slash + 1);
            if (!Has(name, "wall") && !Has(name, "gate")) continue;
            if (Value* tr = PlacedTransform(comps)) {
                Value& p = Need(*tr, "position");
                out.push_back({Float(Get(p, "x"), 0.0) - FIRE[0], Float(Get(p, "z"), 0.0) - FIRE[1]});
            }
        }
        return out;
    }

    // town_ground
    bool TownGround(Value& town, const double at[2], double turnDeg, Ids& ids, Value* out) {
        Value* source = Get(town, "terrain");
        Value* comps = IsDict(source) ? Components(*source) : nullptr;
        Value* model = nullptr;
        if (comps) for (Value& c : comps->a) if (IsType(c, "ModelDefinitionComponent") && Truthy(Get(c, "filename"))) { model = &c; break; }
        Value* tr = PlacedTransform(comps);
        if (!model || !tr) return false;
        const double t = Radians(turnDeg);
        const double ct = std::cos(t), st = std::sin(t);
        Value& p = Need(*tr, "position");
        const double x = Float(Get(p, "x"), 0.0) - at[0];
        const double z = Float(Get(p, "z"), 0.0) - at[1];
        const uint32_t id = Fresh(0x5CA1AB1E, ids, false);
        const std::string name = std::string("town_ground") + MIRROR;
        Value e = MakeObj();
        Set(e, "type", MakeStr("Entity"));
        Set(e, "name", MakeStr(name));
        Set(e, "id", MakeInt(id));
        Value comp = MakeObj();
        Set(comp, "type", MakeStr("TransformDefinitionComponent"));
        Set(comp, "name", MakeStr(name + "_TransformDefinitionComponent"));
        Value pos = MakeObj();
        Set(pos, "x", FIRE[0] + ct * x + st * z);
        Set(pos, "y", Float(Get(p, "y"), 0.0) - TOWN_GROUND_DROP);
        Set(pos, "z", FIRE[1] - st * x + ct * z);
        Set(comp, "position", std::move(pos));
        Value q = MakeObj();
        Set(q, "x", 0.0);
        Set(q, "y", std::sin(t * 0.5));
        Set(q, "z", 0.0);
        Set(q, "w", std::cos(t * 0.5));
        Set(comp, "orientation", std::move(q));
        Value sc = MakeObj();
        Set(sc, "x", 1.0);
        Set(sc, "y", 1.0);
        Set(sc, "z", 1.0);
        Set(comp, "scale", std::move(sc));
        Set(comp, "inheritOnlyPosition", MakeBool(false));
        Value m = *model;   // dict(copy.deepcopy(model), name=..., shadowMask=0)
        Set(m, "name", MakeStr(name + "_ModelDefinitionComponent"));
        Set(m, "shadowMask", MakeInt(0));
        Value comps2 = MakeArr();
        comps2.a.push_back(std::move(comp));
        comps2.a.push_back(std::move(m));
        Set(e, "components", std::move(comps2));
        *out = std::move(e);
        return true;
    }

    // with_harrogath_behind (near 14, far 400)
    int HarrogathBehind(Value& town, const double at[2], double turnDeg, bool ground, bool flipWall, bool clutter, bool wallFill) {
        const double nearD = 14.0, farD = 400.0;
        Value& list = *entities();
        for (Value& e : list.a) if (Has(StrOr(Get(e, "name"), ""), MIRROR)) return 0;
        Ids ids = IdsOf(list);
        const double t = Radians(turnDeg);
        const double ct = std::cos(t), st = std::sin(t);
        const double qy[4] = {0.0, std::sin(t * 0.5), 0.0, std::cos(t * 0.5)};
        int added = 0;
        const std::vector<std::pair<double, double>> ownWalls = wallFill ? SceneWalls() : std::vector<std::pair<double, double>>();
        std::vector<Value> made;
        Value* townList = Get(town, "entities");
        if (townList && townList->kind != Value::Array) throw Bad("the town's entities is no list");
        if (townList)
            for (Value& source : townList->a) {
                Value* comps = ComponentsIfDict(source);
                if (!comps) continue;
                bool model = false, other = false, animated = false;
                for (Value& c : comps->a) {
                    const std::string k = TypeStr(c);
                    if (Has(k, "Model")) model = true;
                    if (Has(k, "Light") || Has(k, "Vfx") || Has(k, "Sound") || Has(k, "Audio")) other = true;
                    for (const char* a : ANIMATED) if (k == a) animated = true;
                }
                if (!model || other) continue;
                Value* tr = PlacedTransform(comps);
                if (!tr) continue;
                const std::string modelName = ModelName(comps);
                if (!clutter) {
                    bool junk = false;
                    for (const char* word : CLUTTER) if (Has(modelName, word)) junk = true;
                    if (junk) continue;
                }
                Value& tp = Need(*tr, "position");
                const double x = Float(Get(tp, "x"), 0.0) - at[0];
                const double z = Float(Get(tp, "z"), 0.0) - at[1];
                const double dx = ct * x + st * z, dz = -st * x + ct * z;   // turned about +y
                const double d = std::hypot(dx, dz);
                if (d < nearD || d > farD || BesideHeroes(dx, dz)) continue;
                if (InSceneHalf(dx, dz) && !(InTownKeep(dx, dz) && !Has(modelName, WALLS))) {
                    bool clear = true;
                    for (const auto& w : ownWalls) if (!(std::hypot(dx - w.first, dz - w.second) >= WALL_FILL_CLEAR)) { clear = false; break; }
                    if (!(wallFill && Has(modelName, WALLS) && clear)) continue;
                }
                if (animated) continue;
                Value e = source;
                Set(e, "name", MakeStr(StrOr(Get(e, "name"), "entity") + MIRROR));
                // the picture only: no bodies, no fading walls
                Value kept = MakeArr();
                for (Value& c : Components(e)->a) {
                    bool keep = false;
                    for (const char* k : KEPT_COMPONENTS) if (IsType(c, k)) keep = true;
                    if (keep) kept.a.push_back(c);
                }
                Set(e, "components", std::move(kept));
                if (!o.townShadows)
                    for (Value& c : Get(e, "components")->a) if (IsType(c, "ModelDefinitionComponent")) Set(c, "shadowMask", MakeInt(0));
                bool spin = flipWall && Has(modelName, WALLS) && std::fabs(Float(Get(tp, "x"), 0.0) - WALL_LINE_X) < 4.0 &&
                            (o.flipSide == "all" || (o.flipSide == "north") == (dz > 0.0));
                const bool farLength = o.flipFar && Has(modelName, WALLS) && dz > 120.0 && Has(modelName, "segment");
                spin = spin || farLength;
                const uint32_t id = Fresh((uint64_t)Int(Get(e, "id"), 0) * 2246822519ull + 0x27D4EB2Full, ids, true);
                Set(e, "id", MakeInt(id));
                for (Value& c : Get(e, "components")->a) {
                    if (!IsType(c, "TransformDefinitionComponent")) continue;
                    Value& p = Need(c, "position");
                    Set(p, "x", FIRE[0] + dx + (farLength ? o.farShift : 0.0));
                    Set(p, "z", FIRE[1] + dz);
                    Value* q = Get(c, "orientation");
                    if (IsDict(q) && spin) HalfTurnQ(*q);   // half a turn about its own +y first
                    if (IsDict(q)) TurnQ(*q, qy[0], qy[1], qy[2], qy[3]);
                }
                made.push_back(std::move(e));
                ++added;
            }
        for (Value& e : made) list.a.push_back(std::move(e));
        SetDefault(result, "dependencies", MakeObj());   // (TOWN_DEPENDENCIES is empty: nothing of the town's lists merged)
        if (ground) {
            Value floor;
            if (TownGround(town, at, turnDeg, ids, &floor)) {
                entities()->a.push_back(std::move(floor));
                ++added;
            }
        }
        return added;
    }

    // with_town_behind
    int TownBehind() {
        Value& list = *entities();
        for (Value& e : list.a) if (Has(StrOr(Get(e, "name"), ""), MIRROR)) return 0;
        Ids ids = IdsOf(list);
        std::vector<Value> added;
        for (Value& e : list.a) {
            const double d = PlacedModel(e);
            if (d >= 0.0 && d >= SURROUND_MIN) added.push_back(TurnedHalf(e, ids));
        }
        for (Value& e : added) list.a.push_back(std::move(e));
        return (int)added.size();
    }

    // the first scene sky (not one of our copies)
    bool FindSky(Value* out) {
        for (Value& e : entities()->a)
            if (e.kind == Value::Object && DumpHas(e, SKY) && !Has(StrOr(Get(e, "name"), ""), MIRROR)) { *out = e; return true; }
        return false;
    }
    static Value& FirstTransform(Value& e) {
        if (Value* comps = Components(e))
            for (Value& c : comps->a) if (IsType(c, "TransformDefinitionComponent")) return c;
        throw Bad("the sky has no transform");
    }
    // sky_centre
    static void SkyCentre(Value& sky, double out[2]) {
        Value& tr = FirstTransform(sky);
        Value& p = Need(tr, "position");
        Value* q = Get(tr, "orientation");
        const Value empty = MakeObj();
        const Value& qq = Truthy(q) ? *q : empty;
        const double yaw = 2.0 * std::atan2(Float(Get(qq, "y"), 0.0), Float(Get(qq, "w"), 1.0));
        Value* sc = Get(tr, "scale");
        const Value& ss = Truthy(sc) ? *sc : empty;
        const double r = SKY_RADIUS * Float(Get(ss, "x"), 1.0);
        out[0] = Float(Get(p, "x"), 0.0) + r * std::cos(yaw);
        out[1] = Float(Get(p, "z"), 0.0) - r * std::sin(yaw);
    }

    // with_sky_round
    int SkyRound(const std::vector<double>& turns) {
        Value sky;
        if (!FindSky(&sky)) return 0;
        Ids ids = IdsOf(*entities());
        int added = 0;
        int i = 0;
        for (const double turn : turns) {
            ++i;
            Value e = sky;
            Set(e, "name", MakeStr(StrOr(Get(e, "name"), "sky") + "_" + std::to_string((long long)std::trunc(turn)) + MIRROR));
            const uint32_t id = Fresh((uint64_t)Int(Get(e, "id"), 0) + 0x9E3779B9ull * (uint64_t)i, ids, true);
            Set(e, "id", MakeInt(id));
            const double k = 1.0 + SKY_GROW * i;
            const double t = Radians(turn);
            double centre[2];
            SkyCentre(sky, centre);
            const double ct = std::cos(t), st = std::sin(t);
            const double qy[4] = {0.0, std::sin(t * 0.5), 0.0, std::cos(t * 0.5)};
            if (Value* comps = Components(e))
                for (Value& c : comps->a) {
                    if (!IsType(c, "TransformDefinitionComponent")) continue;
                    Value& p = Need(c, "position");
                    const double x = Float(Get(p, "x"), 0.0) - centre[0], z = Float(Get(p, "z"), 0.0) - centre[1];
                    Set(p, "x", centre[0] + k * (ct * x + st * z));
                    Set(p, "z", centre[1] + k * (-st * x + ct * z));
                    Value* sc = Get(c, "scale");
                    if (IsDict(sc))
                        for (const char* a : {"x", "y", "z"}) Set(*sc, a, Float(Get(*sc, a), 1.0) * k * (strcmp(a, "z") == 0 ? SKY_WIDEN : 1.0));
                    Value* q = Get(c, "orientation");
                    if (IsDict(q)) TurnQ(*q, qy[0], qy[1], qy[2], qy[3]);
                }
            entities()->a.push_back(std::move(e));
            ++added;
        }
        return added;
    }

    // with_sky_cap
    int SkyCap() {
        const std::string capName = std::string("sky_cap") + MIRROR;
        for (Value& e : entities()->a) if (e.kind == Value::Object && IsStr(Get(e, "name"), capName.c_str())) return 0;
        Value sky;
        if (!FindSky(&sky)) return 0;
        Value& tr = FirstTransform(sky);
        const Value empty = MakeObj();
        Value* sc = Get(tr, "scale");
        const double band = Float(Get(Truthy(sc) ? *sc : empty, "x"), 1.0);
        double centre[2];
        SkyCentre(sky, centre);
        const double top = Float(Get(Need(tr, "position"), "y"), 0.0) + SKY_TOP * band;
        const double k = CAP_COVER * SKY_RADIUS * band / CAP_SIZE;
        Ids ids = IdsOf(*entities());
        uint64_t id = 0x5CA1AB1F;   // (not taken into ids: nothing after it)
        while (ids.count((int64_t)id)) id = (id + 0x01000193) & kMask;
        const double h = std::sqrt(0.5);
        Value e = MakeObj();
        Set(e, "type", MakeStr("Entity"));
        Set(e, "name", MakeStr(capName));
        Set(e, "id", MakeInt((int64_t)id));
        Value t = MakeObj();
        Set(t, "type", MakeStr("TransformDefinitionComponent"));
        Set(t, "name", MakeStr(capName + "_TransformDefinitionComponent"));
        Value pos = MakeObj();
        Set(pos, "x", centre[0]);
        Set(pos, "y", top);
        Set(pos, "z", centre[1]);
        Set(t, "position", std::move(pos));
        Value q = MakeObj();
        Set(q, "x", 0.0);
        Set(q, "y", 0.0);
        Set(q, "z", -h * o.capFace);
        Set(q, "w", h);
        Set(t, "orientation", std::move(q));
        Value s = MakeObj();
        Set(s, "x", k);
        Set(s, "y", k);
        Set(s, "z", k);
        Set(t, "scale", std::move(s));
        Set(t, "inheritOnlyPosition", MakeBool(false));
        Value m = MakeObj();
        Set(m, "type", MakeStr("ModelDefinitionComponent"));
        Set(m, "name", MakeStr(capName + "_ModelDefinitionComponent"));
        Set(m, "filename", MakeStr(CAP));
        Set(m, "visibleLayers", MakeInt(1));
        Set(m, "lightMask", MakeInt(19));
        Set(m, "shadowMask", MakeInt(0));
        Set(m, "ghostShadows", MakeBool(false));
        Set(m, "floorModel", MakeBool(false));
        Set(m, "terrainBlendEnableYUpBlend", MakeBool(false));
        Set(m, "terrainBlendMode", MakeInt(1));
        Value comps = MakeArr();
        comps.a.push_back(std::move(t));
        comps.a.push_back(std::move(m));
        Set(e, "components", std::move(comps));
        entities()->a.push_back(std::move(e));
        return 1;
    }

    // without_clouds (false: none found)
    bool WithoutClouds() {
        Value* list = IsDict(&result) ? Get(result, "entities") : nullptr;
        if (!IsList(list)) throw Bad("The campfire preset has no entity list; left as it is");
        std::vector<Value> kept;
        bool removed = false;
        for (Value& e : list->a) {
            if (e.kind == Value::Object && Has(Lower(StrOr(Get(e, "name"), "")), CLOUDS)) { removed = true; continue; }
            kept.push_back(std::move(e));
        }
        list->a = std::move(kept);
        return removed;
    }

    // without_light_shadows
    void WithoutLightShadows() {
        Value* list = Get(result, "entities");
        if (!list) return;
        if (list->kind != Value::Array) throw Bad("entities is no list");
        for (Value& e : list->a)
            if (Value* comps = ComponentsIfDict(e))
                for (Value& c : comps->a) {
                    const Value* sc = Get(c, "shadowCaster");
                    if (Has(TypeStr(c), "Light") && !(sc && sc->kind == Value::Bool && !sc->b)) Set(c, "shadowCaster", MakeBool(false));
                }
    }

    // spots_as_points (testing)
    void SpotsAsPoints() {
        Value* list = Get(result, "entities");
        if (!list) return;
        for (Value& e : list->a)
            if (Value* comps = ComponentsIfDict(e))
                for (Value& c : comps->a) {
                    if (!IsType(c, "SpotLightDefinitionComponent")) continue;
                    auto take = [&](const char* key) { const Value* v = Get(c, key); return v ? *v : Value(); };
                    std::string name = StrOr(Get(c, "name"), "");
                    for (size_t at = name.find("Spot"); at != std::string::npos; at = name.find("Spot", at + 5)) name.replace(at, 4, "Point");
                    Value n = MakeObj();
                    Set(n, "type", MakeStr("PointLightDefinitionComponent"));
                    Set(n, "name", MakeStr(name));
                    Set(n, "color", take("color"));
                    Set(n, "power", take("power"));
                    Set(n, "radius", take("distance"));
                    Set(n, "attenuation", take("attenuation"));
                    Set(n, "lightMask", take("lightMask"));
                    Set(n, "isLocalLight", take("isLocalLight"));
                    Set(n, "diffuseContribution", take("diffuseContribution"));
                    Set(n, "specularContribution", take("specularContribution"));
                    c = std::move(n);
                }
    }

    // without(preset, names)
    void Without(const std::vector<std::string>& names) {
        if (names.empty()) return;
        Value& list = *entities();
        std::vector<Value> kept;
        for (Value& e : list.a) {
            const std::string n = Lower(StrOr(Get(e, "name"), ""));
            bool hit = !Has(n, MIRROR);
            if (hit) {
                bool any = false;
                for (const std::string& x : names) if (Has(n, Lower(x).c_str())) any = true;
                hit = any;
            }
            if (!hit) kept.push_back(std::move(e));
        }
        list.a = std::move(kept);
    }

    // with_marks (testing)
    void Marks(const std::vector<std::string>& names, double step = 20.0) {
        Value& list = *entities();
        Ids ids = IdsOf(list);
        int k = 0;
        for (const std::string& name : names) {
            ++k;
            Value src;
            bool found = false;
            for (Value& e : list.a)
                if (e.kind == Value::Object && Has(Lower(StrOr(Get(e, "name"), "")), Lower(name).c_str())) { src = e; found = true; break; }
            if (!found) continue;
            for (int j = 1; j <= k; ++j) {
                Value e = src;
                Set(e, "name", MakeStr(StrOr(Get(src, "name"), "None") + "_mark" + std::to_string(j)));
                const uint32_t id = Fresh((uint64_t)Int(Get(src, "id"), 0) * 2246822519ull + (uint64_t)j * 0x9E3779B9ull, ids, true);
                Set(e, "id", MakeInt(id));
                if (Value* comps = Components(e))
                    for (Value& c : comps->a)
                        if (IsType(c, "TransformDefinitionComponent") && IsDict(Get(c, "position"))) {
                            Value& p = Need(c, "position");
                            Set(p, "y", Float(Get(p, "y"), 0.0) + step * j);
                        }
                list.a.push_back(std::move(e));
            }
        }
    }

    // with_copies
    void Copies(const std::vector<std::string>& specs) {
        Value& list = *entities();
        Ids ids = IdsOf(list);
        for (size_t k = 0; k < specs.size(); ++k) {
            const std::string& spec = specs[k];
            const bool at = !spec.empty() && spec[0] == '@';
            const std::vector<std::string> parts = Split(LStrip(spec, '@'), ':');
            const std::string name = parts[0];
            std::vector<double> nums;
            for (size_t j = 1; j < parts.size(); ++j) nums.push_back(FloatText(parts[j]));
            Value* found = FindByName(list, name);
            if (!found) continue;
            const Value src = *found;
            Value e = src;
            char suffix[16];
            snprintf(suffix, sizeof suffix, "_copy%02d", (int)k);
            Set(e, "name", MakeStr(StrOr(Get(src, "name"), "None") + suffix));
            const uint32_t id = Fresh((uint64_t)Int(Get(src, "id"), 0) * 2246822519ull + (uint64_t)(k + 1) * 0x85EBCA6Bull, ids, true);
            Set(e, "id", MakeInt(id));
            auto num = [&](size_t n) {
                if (n >= nums.size()) throw Bad("--copy " + spec + ": list index out of range");
                return nums[n];
            };
            if (Value* comps = Components(e))
                for (Value& c : comps->a) {
                    if (!(IsType(c, "TransformDefinitionComponent") && IsDict(Get(c, "position")))) continue;
                    Value& p = Need(c, "position");
                    if (at) {
                        const double px = FIRE[0] + num(0), pz = FIRE[1] + num(1);
                        Set(p, "x", px);
                        Set(p, "z", pz);
                        Set(p, "y", Float(Get(p, "y"), 0.0) + (nums.size() > 2 ? nums[2] : 0.0));
                        Value* q = Get(c, "orientation");
                        if (nums.size() > 3 && IsDict(q)) {   // :YAW - turned about its origin, qy * q
                            const double h = Radians(nums[3]) / 2;
                            TurnQ(*q, 0.0, std::sin(h), 0.0, std::cos(h));
                        }
                        if (nums.size() > 4) {   // :SCALE about its origin
                            Value def = MakeObj();
                            Set(def, "x", 1.0);
                            Set(def, "y", 1.0);
                            Set(def, "z", 1.0);
                            Value& sc = SetDefault(c, "scale", std::move(def));
                            for (const char* a : {"x", "y", "z"}) Set(sc, a, Float(Get(sc, a), 1.0) * nums[4]);
                        }
                    } else {
                        Set(p, "x", Float(Get(p, "x"), 0.0) + num(0));
                        Set(p, "z", Float(Get(p, "z"), 0.0) + num(1));
                    }
                }
            list.a.push_back(std::move(e));
        }
    }

    // with_grows
    void Grows(const std::vector<std::string>& specs) {
        for (const std::string& spec : specs) {
            const std::vector<std::string> parts = Split(spec, ':');
            if (parts.size() != 4) throw Bad("--grow " + spec + ": NAME:F:CX:CZ");
            const double f = FloatText(parts[1]), cx = FloatText(parts[2]), cz = FloatText(parts[3]);
            Value* e = FindByName(*entities(), parts[0]);
            if (!e) continue;
            if (Value* comps = Components(*e))
                for (Value& c : comps->a) {
                    if (!(IsType(c, "TransformDefinitionComponent") && IsDict(Get(c, "position")))) continue;
                    Value& p = Need(c, "position");
                    const double px = Float(&Need(p, "x"), 0.0) - FIRE[0], pz = Float(&Need(p, "z"), 0.0) - FIRE[1];
                    Set(p, "x", FIRE[0] + cx + (px - cx) * f);
                    Set(p, "z", FIRE[1] + cz + (pz - cz) * f);
                    Value def = MakeObj();
                    Set(def, "x", 1.0);
                    Set(def, "y", 1.0);
                    Set(def, "z", 1.0);
                    Value& sc = SetDefault(c, "scale", std::move(def));
                    for (const char* a : {"x", "z"}) Set(sc, a, Float(Get(sc, a), 1.0) * f);
                }
        }
    }

    // with_ground_round
    void GroundRound(const std::vector<double>& turns) {
        Value& list = *entities();
        std::vector<Value> sources;
        for (Value& e : list.a) {
            if (e.kind != Value::Object) continue;
            const Value* comps = Get(e, "components");
            if (comps && DumpHas(*comps, "ui_groundplane") && !Has(StrOr(Get(e, "name"), ""), MIRROR)) sources.push_back(e);
        }
        Ids ids = IdsOf(list);
        for (const double turn : turns) {
            const double t = Radians(turn);
            const double ct = std::cos(t), st = std::sin(t);
            const double qy[4] = {0.0, std::sin(t * 0.5), 0.0, std::cos(t * 0.5)};
            const long long whole = (long long)std::trunc(turn);
            for (const Value& src : sources) {
                Value e = src;
                Set(e, "name", MakeStr(StrOr(Get(src, "name"), "None") + "_ground" + std::to_string(whole) + MIRROR));
                const uint32_t id = Fresh((uint64_t)Int(Get(src, "id"), 0) * 2654435761ull + (uint64_t)whole * 0x9E3779B9ull + 7, ids, true);
                Set(e, "id", MakeInt(id));
                if (Value* comps = Components(e))
                    for (Value& c : comps->a) {
                        if (!IsType(c, "TransformDefinitionComponent") || !IsDict(Get(c, "position"))) continue;
                        Value& p = Need(c, "position");
                        const double x = Float(Get(p, "x"), 0.0) - FIRE[0], z = Float(Get(p, "z"), 0.0) - FIRE[1];
                        Set(p, "x", FIRE[0] + ct * x + st * z);
                        Set(p, "z", FIRE[1] - st * x + ct * z);
                        Set(p, "y", Float(Get(p, "y"), 0.0) - GROUND_SINK);
                        Value* q = Get(c, "orientation");
                        if (IsDict(q)) TurnQ(*q, qy[0], qy[1], qy[2], qy[3]);
                    }
                entities()->a.push_back(std::move(e));
            }
        }
    }

    // with_spins
    void Spins(const std::vector<std::string>& names) {
        for (const std::string& spec : names) {
            const size_t colon = spec.find(':');
            const std::string name = colon == std::string::npos ? spec : spec.substr(0, colon);
            const std::string degText = colon == std::string::npos ? std::string() : spec.substr(colon + 1);
            const double deg = !degText.empty() ? FloatText(degText) : 180.0;
            const double h = Radians(deg) / 2;
            const double bx = 0.0, by = std::sin(h), bz = 0.0, bw = std::cos(h);
            const std::string want = Lower(name);
            std::vector<Value*> hit;
            for (Value& e : entities()->a) if (e.kind == Value::Object && want == Lower(StrOr(Get(e, "name"), ""))) hit.push_back(&e);
            if (hit.empty())
                for (Value& e : entities()->a) if (e.kind == Value::Object && Has(Lower(StrOr(Get(e, "name"), "")), want.c_str())) hit.push_back(&e);
            for (Value* e : hit)
                if (Value* comps = Components(*e))
                    for (Value& c : comps->a) {
                        Value* q = IsType(c, "TransformDefinitionComponent") ? Get(c, "orientation") : nullptr;
                        if (IsDict(q)) TurnQ(*q, bx, by, bz, bw);
                    }
        }
    }

    // with_ui_walls
    void UiWalls() {
        Value* found = nullptr;
        for (Value& e : entities()->a) if (e.kind == Value::Object && IsStr(Get(e, "name"), "ui_wall01_01")) { found = &e; break; }
        if (!found) return;
        const Value src = *found;
        auto where = [](Value& e, double out[2]) {
            Value* tr = PlacedTransform(Components(e));
            if (!tr) return false;
            Value& p = Need(*tr, "position");
            out[0] = Float(&Need(p, "x"), 0.0) - FIRE[0];
            out[1] = Float(&Need(p, "z"), 0.0) - FIRE[1];
            return true;
        };
        auto townWall = [&](Value& e) {
            if (e.kind != Value::Object || !Has(StrOr(Get(e, "name"), ""), MIRROR)) return false;
            const std::string model = ModelName(Components(e));
            double at[2];
            const bool placed = where(e, at);
            if (!Has(model, WALLS) || !placed) return false;
            const double x = at[0], z = at[1];
            return (std::fabs(x + 59.0) < 5.0 && -130.0 < z && z < 125.0) || (z > 120.0 && x < 150.0);
        };
        Value& list = *entities();
        std::vector<Value> kept;
        for (Value& e : list.a) if (!townWall(e)) kept.push_back(std::move(e));
        list.a = std::move(kept);
        Ids ids = IdsOf(list);
        Value s = src;
        double sxz[2];
        if (!where(s, sxz)) throw Bad("ui_wall01_01 has no place");
        const double sx = sxz[0];
        const double L = o.uiWallLength;
        struct Spot { double x, z; bool yawSet; double yaw; };
        std::vector<Spot> spots;
        double z = -16.4 + 11.0 + L / 2;
        while (z - L / 2 < 132.0) { spots.push_back({sx, z, false, 0.0}); z += L; }
        for (const double zz : {-50.0, -96.8 - L / 4 - L / 2}) spots.push_back({sx, zz, false, 0.0});
        double x = -59.0 + L / 2;
        while (x + L / 2 <= 150.0) { spots.push_back({x, 137.0, true, o.uiWallFarYaw}); x += L; }
        for (size_t k = 0; k < spots.size(); ++k) {
            Value e = src;
            char nm[32];
            snprintf(nm, sizeof nm, "ui_wall01_pcvr_%02d", (int)k);
            Set(e, "name", MakeStr(nm));
            const uint32_t id = Fresh(0x51A11000ull + (uint64_t)k * 0x01000193ull, ids, true);
            Set(e, "id", MakeInt(id));
            if (Value* comps = Components(e))
                for (Value& c : comps->a) {
                    if (!IsType(c, "TransformDefinitionComponent")) continue;
                    Value& p = Need(c, "position");
                    Set(p, "x", FIRE[0] + spots[k].x);
                    Set(p, "z", FIRE[1] + spots[k].z);
                    if (spots[k].yawSet) {
                        const double h = Radians(spots[k].yaw) / 2;
                        Value q = MakeObj();
                        Set(q, "x", 0.0);
                        Set(q, "y", std::sin(h));
                        Set(q, "z", 0.0);
                        Set(q, "w", std::cos(h));
                        Set(c, "orientation", std::move(q));
                    }
                }
            list.a.push_back(std::move(e));
        }
    }

    // with_pave
    void Pave() {
        Value* found = FindByName(*entities(), "terrain_charactercreate01_01");
        if (!found) return;
        const Value src = *found;
        Ids ids = IdsOf(*entities());
        const double x0 = o.paveArea[0], x1 = o.paveArea[1], z0 = o.paveArea[2], z1 = o.paveArea[3];
        const bool hole = !o.paveHole.empty();
        auto holeAt = [&](size_t n) {
            if (n >= o.paveHole.size()) throw Bad("--pave-hole: tuple index out of range");
            return o.paveHole[n];
        };
        int n = 0, row = 0;
        double z = z0;
        while (z < z1) {
            double x = x0 - (row % 2 ? PAVE_STEP[0] / 2 : 0.0);
            const bool holed = hole && (std::min)(z + 31.0, holeAt(3)) - (std::max)(z, holeAt(2)) > 2.0;
            if (holed) x = holeAt(1);
            int col = 0;
            while (x < x1) {
                bool hit = false;
                for (const auto& s : o.paveSkip) {
                    if (s.size() != 2) throw Bad("--pave-skip: X:Z");
                    if (x <= s[0] && s[0] < x + 114.0 && z <= s[1] && s[1] < z + 31.0) { hit = true; break; }
                }
                if (hit) { x += PAVE_STEP[0]; ++col; continue; }
                Value e = src;
                char suffix[24];
                snprintf(suffix, sizeof suffix, "_pave%03d", n);
                Set(e, "name", MakeStr(StrOr(Get(src, "name"), "None") + suffix));
                const uint32_t id = Fresh((uint64_t)Int(Get(src, "id"), 0) * 2654435761ull + (uint64_t)(n + 1) * 0x9E3779B1ull, ids, true);
                Set(e, "id", MakeInt(id));
                if (Value* comps = Components(e))
                    for (Value& c : comps->a) {
                        if (!(IsType(c, "TransformDefinitionComponent") && IsDict(Get(c, "position")))) continue;
                        Value& p = Need(c, "position");
                        Set(p, "x", FIRE[0] + x - PAVE_HULL[0]);
                        Set(p, "z", FIRE[1] + z - PAVE_HULL[1]);
                        Set(p, "y", Float(Get(p, "y"), 0.0) + PAVE_DY[(row + col) % 3]);
                        for (const Tilt& tl : o.paveTilt) {
                            const double cz1 = 200.0 - 40.0 * tl.a.col, cz0 = 200.0 - 40.0 * (tl.b.col + 1);
                            const double cx0 = (tl.a.row - 1) * 40.0 - 200.0, cx1 = tl.b.row * 40.0 - 200.0;
                            const double ox = (std::min)(x + 114.0, cx1) - (std::max)(x, cx0);
                            const double oz = (std::min)(z + 31.0, cz1) - (std::max)(z, cz0);
                            if (ox >= 10.0 && oz >= 10.0) {
                                const double th = std::asin((std::max)(-1.0, (std::min)(1.0, -tl.drop / 31.0)));
                                Set(p, "y", Float(&Need(p, "y"), 0.0) - (PAVE_HULL[1] + 31.0) * tl.drop / 31.0);
                                const double h = th / 2;
                                Value def = MakeObj();
                                Set(def, "x", 0.0);
                                Set(def, "y", 0.0);
                                Set(def, "z", 0.0);
                                Set(def, "w", 1.0);
                                Value& q = SetDefault(c, "orientation", std::move(def));
                                const double qx = Float(Get(q, "x"), 0.0), qy = Float(Get(q, "y"), 0.0), qz = Float(Get(q, "z"), 0.0),
                                             qw = Float(Get(q, "w"), 0.0);
                                const double bx = std::sin(h), bw = std::cos(h);
                                Set(q, "x", bw * qx + bx * qw);
                                Set(q, "y", bw * qy - bx * qz);
                                Set(q, "z", bw * qz + bx * qy);
                                Set(q, "w", bw * qw - bx * qx);
                            }
                        }
                    }
                entities()->a.push_back(std::move(e));
                ++n;
                ++col;
                x += PAVE_STEP[0];
            }
            ++row;
            z += PAVE_STEP[1];
        }
    }

    // with_lights
    void Lights() {
        Value* found = nullptr;
        for (Value& e : entities()->a) if (e.kind == Value::Object && IsStr(Get(e, "name"), LIGHT_TEMPLATE)) { found = &e; break; }
        if (!found) return;
        const Value src = *found;
        Ids ids = IdsOf(*entities());
        for (size_t k = 0; k < o.lights.size(); ++k) {
            const std::vector<double> v = Floats(LStrip(o.lights[k], '@'));
            if (v.size() != 8) throw Bad("--light: X:Z:Y:R:G:B:POWER:RADIUS");
            Value e = src;
            char nm[32];
            snprintf(nm, sizeof nm, "pcvr_light%02d", (int)k);
            Set(e, "name", MakeStr(nm));
            const uint32_t id = Fresh(0x51A7E000ull + (uint64_t)k * 7919ull, ids, false);
            Set(e, "id", MakeInt(id));
            if (Value* comps = Components(e))
                for (Value& c : comps->a) {
                    if (IsType(c, "TransformDefinitionComponent")) {
                        Value p = MakeObj();
                        Set(p, "x", FIRE[0] + v[0]);
                        Set(p, "y", v[2]);
                        Set(p, "z", FIRE[1] + v[1]);
                        Set(c, "position", std::move(p));
                    } else if (IsType(c, "PointLightDefinitionComponent")) {
                        Value col = MakeObj();
                        Set(col, "x", v[3]);
                        Set(col, "y", v[4]);
                        Set(col, "z", v[5]);
                        Set(c, "color", std::move(col));
                        Set(c, "power", v[6]);
                        Set(c, "radius", v[7]);
                    }
                }
            entities()->a.push_back(std::move(e));
        }
    }

    // MOVES and LIFTS
    void MovesAndLifts() {
        for (Value& e : entities()->a) {
            if (e.kind != Value::Object) continue;
            const std::string name = Lower(StrOr(Get(e, "name"), ""));
            Value* tr = PlacedTransform(Components(e));
            if (!tr) continue;
            Value& p = Need(*tr, "position");
            for (const std::string& spec : o.moves) {
                const std::vector<std::string> s = Split(spec, ':');
                if (s.size() != 4) throw Bad("--move " + spec + ": NAME:DX:DY:DZ");
                if (name == Lower(s[0])) {
                    Set(p, "x", Float(Get(p, "x"), 0.0) + FloatText(s[1]));
                    Set(p, "y", Float(Get(p, "y"), 0.0) + FloatText(s[2]));
                    Set(p, "z", Float(Get(p, "z"), 0.0) + FloatText(s[3]));
                }
            }
            bool floor = false;
            for (const char* f : FLOORS) if (Has(name, f)) floor = true;
            if (floor) continue;
            for (const std::string& spec : o.lifts) {
                const std::vector<double> v = Floats(spec);
                if (v.size() != 4) throw Bad("--lift: X:Z:R:DY");
                if (std::hypot(Float(Get(p, "x"), 0.0) - FIRE[0] - v[0], Float(Get(p, "z"), 0.0) - FIRE[1] - v[1]) <= v[2])
                    Set(p, "y", Float(Get(p, "y"), 0.0) + v[3]);
            }
        }
    }

    // HIDE
    void Hide() {
        std::vector<std::string> names;
        for (const std::string& n : o.hide) names.push_back(Lower(n));
        Value& list = *entities();
        std::vector<Value> kept;
        for (Value& e : list.a) {
            const bool gone = e.kind == Value::Object && std::find(names.begin(), names.end(), Lower(StrOr(Get(e, "name"), ""))) != names.end();
            if (!gone) kept.push_back(std::move(e));
        }
        list.a = std::move(kept);
    }
};

// generate(): the scene's file, as written
std::string Generate(const std::string& raw, const std::string* townRaw, const Options& o) {
    if (raw.size() > 16u * 1024 * 1024) throw Bad("Unexpectedly large preset");
    Value result = ParseJson(raw);
    Scene s{o, result};
    if (!s.WithoutClouds()) throw Bad("Campfire preset changed: no cloud effect found; native preset retained");
    s.Without(o.drop);
    if (o.shadowsOff) s.WithoutLightShadows();
    if (o.points) s.SpotsAsPoints();
    if (o.spotSpecularSet) {
        for (Value& e : s.entities()->a)
            if (Value* comps = ComponentsIfDict(e))
                for (Value& c : comps->a) if (IsType(c, "SpotLightDefinitionComponent")) Set(c, "specularContribution", o.spotSpecular);
    }
    Value town;
    bool townRead = false;
    auto readTown = [&]() -> Value& {
        if (!townRead) {
            if (!townRaw) throw Bad("townwest.json could not be read from the game");
            town = ParseJson(*townRaw);
            if (town.kind != Value::Object) throw Bad("townwest.json is no preset");
            townRead = true;
        }
        return town;
    };
    if (o.surround && o.townAtSet) {
        s.HarrogathBehind(readTown(), o.townAt, o.townTurn, o.ground, o.flipWall, o.clutter, o.wallFill);
    } else if (o.surround) {
        s.TownBehind();
    }
    if (o.surround && !o.skyTurns.empty()) s.SkyRound(o.skyTurns);
    if (o.surround && o.cap) s.SkyCap();
    if (o.surround && o.uiWalls) s.UiWalls();
    if (!o.grows.empty()) s.Grows(o.grows);
    if (!o.copies.empty()) s.Copies(o.copies);
    if (!o.groundTurns.empty()) s.GroundRound(o.groundTurns);
    if (!o.spin.empty()) s.Spins(o.spin);
    if (o.pave) s.Pave();
    if (o.townBiome && o.surround) {
        const Value* biome = Get(readTown(), "biomeFilename");
        if (Truthy(biome)) Set(result, "biomeFilename", *biome);
    }
    if (!o.lights.empty()) s.Lights();
    if (!o.moves.empty() || !o.lifts.empty()) s.MovesAndLifts();
    if (!o.hide.empty()) s.Hide();
    if (!o.marks.empty()) s.Marks(o.marks);
    std::string out;
    out.reserve(raw.size() * 6);
    Put(out, result, 0);
    out += '\n';
    return out;
}

// ================================================================ the files

bool IsDir(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool ReadFile(const std::wstring& path, std::string* out) {
    out->clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart < (64ll << 20);
    if (ok) {
        out->resize((size_t)size.QuadPart);
        DWORD got = 0;
        ok = out->empty() || (::ReadFile(f, out->data(), (DWORD)out->size(), &got, nullptr) && got == out->size());
    }
    CloseHandle(f);
    return ok;
}

// Written beside itself first, then moved over the old one: a game loading the menu meanwhile never meets half a file.
bool WriteFile(const std::wstring& path, const void* data, size_t n) {
    const std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const bool ok = ::WriteFile(f, data, (DWORD)n, &put, nullptr) && put == n;
    CloseHandle(f);
    if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(tmp.c_str()); return false; }
    return true;
}

bool MakeDirs(const std::wstring& dir) {
    for (size_t at = dir.find(L'\\', 3); at != std::wstring::npos; at = dir.find(L'\\', at + 1)) CreateDirectoryW(dir.substr(0, at).c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    return IsDir(dir);
}

bool CascRead(HANDLE storage, const char* name, std::string* out) {
    const std::string full = std::string("data:") + name;
    HANDLE f = nullptr;
    if (!CascOpenFile(storage, full.c_str(), 0, CASC_OPEN_BY_NAME, &f)) return false;
    ULONGLONG size = 0;
    bool ok = CascGetFileSize64(f, &size) && size < (32ull << 20);
    if (ok) {
        out->resize((size_t)size);
        DWORD got = 0;
        ok = size == 0 || (CascReadFile(f, out->data(), (DWORD)size, &got) && got == size);
    }
    CascCloseFile(f);
    return ok;
}

std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }   // ASCII: our own texts and the file names

}  // namespace

bool Build(const std::string& preset, const std::string* town, bool tuned, std::string* out, std::string* why) {
    std::vector<std::string> argv;
    if (tuned) argv.assign(std::begin(TUNED_SCENE), std::end(TUNED_SCENE));
    return BuildWith(preset, town, argv, out, why);
}

bool BuildWith(const std::string& preset, const std::string* town, const std::vector<std::string>& options, std::string* out, std::string* why) {
    try {
        *out = Generate(preset, town, Configure(options));
        return true;
    } catch (const std::exception& e) {
        if (why) *why = e.what();
        out->clear();
        return false;
    } catch (...) {
        if (why) *why = "unknown error";
        out->clear();
        return false;
    }
}

Result Prepare(const std::wstring& game, const std::wstring& modRoot, bool on) {
    Result r;
    // Only into the mod's own data folder (D2RLoader's mods\D2R-VR\D2R-VR.mpq, which Setup's WriteModInfo makes)
    const std::wstring mpq = modRoot.empty() ? std::wstring() : modRoot + L"D2R-VR.mpq\\";
    if (mpq.empty() || !IsDir(mpq)) {
        r.state = kNotHere;
        r.text = lang::TX(L"campfire_scene.no_mod", L"only in the mod's own folder (mods\\D2R-VR\\D2R-VR.mpq), which is not here: the game's own scene");
        return r;
    }
    const std::string name = std::string("mods/D2R-VR/D2R-VR.mpq/") + kPreset;   // (the receipt's, '/' between the parts)
    std::wstring path = mpq + Wide(kPreset);
    for (wchar_t& c : path) if (c == L'/') c = L'\\';
    const std::wstring receiptPath = modRoot + L"campfire_scene.json";
    std::string last, old;
    ReadFile(receiptPath, &last);
    const bool exists = ReadFile(path, &old);
    const std::string lastHash = arcanesky::ReceiptValue(last, name);
    const bool ours = exists && !lastHash.empty() && arcanesky::Hash(old.data(), old.size()) == lastHash;

    // Our scene out again (unticked, or the game no longer has the preset); a file of that name the player made or changed stays.
    auto takeOut = [&](const std::wstring& because) {
        if (ours) {
            if (!DeleteFileW(path.c_str())) {
                r.state = kFailed;
                r.text = lang::TX(L"campfire_scene.not_taken_out", L"could not take out mods\\D2R-VR\\D2R-VR.mpq\\...\\charactercreate.json (error {error})",
                                  {{L"error", std::to_wstring(GetLastError())}});
                return r;
            }
            r.wrote = true;
        }
        if (!last.empty()) DeleteFileW(receiptPath.c_str());
        r.text = because;
        if (ours) r.text += L"; " + lang::TX(L"campfire_scene.ours_out", L"our character creation scene taken out of mods\\D2R-VR");
        else if (exists) r.text += L"; " + lang::TX(L"campfire_scene.not_ours", L"a charactercreate.json in mods\\D2R-VR that is not ours stays as it is");
        return r;
    };
    if (!on) {
        r.state = kOff;
        return takeOut(lang::TX(L"campfire_scene.off", L"off ([campfire] enabled): character creation as the game has it"));
    }

    std::wstring root = game;
    if (!root.empty() && root.back() == L'\\') root.pop_back();
    HANDLE storage = nullptr;
    if (!CascOpenStorage(root.c_str(), 0, &storage)) {
        r.text = lang::TX(L"campfire_scene.no_archives", L"the game's archives could not be opened (error {error}); it is tried again at the next start",
                          {{L"error", std::to_wstring(GetLastError())}});
        return r;
    }
    std::string preset, town;
    const bool read = CascRead(storage, kPreset, &preset);
    const bool haveTown = CascRead(storage, kTown, &town);
    CASC_STORAGE_PRODUCT product{};
    const bool haveBuild = CascGetStorageInfo(storage, CascStorageProduct, &product, sizeof product, nullptr) && product.BuildNumber;
    CascCloseStorage(storage);
    if (!read) {
        takeOut(L"");
        r.state = kFailed;
        r.text = lang::TX(L"campfire_scene.no_preset", L"character creation's preset is not in this game: the game's own scene");
        return r;
    }
    // The scene as tuned (Harrogath round the fire, its road, rocks and firewood); failing that, the plain one (Unleashed's installer)
    std::string made, why, whyPlain;
    bool tuned = Build(preset, haveTown ? &town : nullptr, true, &made, &why);
    if (!tuned && !Build(preset, haveTown ? &town : nullptr, false, &made, &whyPlain)) {
        // as Unleashed's installer: whatever is there stays
        r.state = kFailed;
        r.text = lang::TX(L"campfire_scene.refused", L"the scene could not be made from your game ({why}); character creation stays as it is",
                          {{L"why", Wide(whyPlain)}});
        return r;
    }
    if (!arcanesky::MayReplace(exists, old, made, lastHash)) {
        r.state = kMade;
        r.text = lang::TX(L"campfire_scene.changed_by_you", L"mods\\D2R-VR\\D2R-VR.mpq\\...\\charactercreate.json was changed by you: left as it is");
        return r;
    }
    if (!exists || old != made) {
        if (!MakeDirs(path.substr(0, path.find_last_of(L'\\'))) || !WriteFile(path, made.data(), made.size())) {
            r.text = lang::TX(L"campfire_scene.not_written", L"could not write mods\\D2R-VR\\D2R-VR.mpq\\...\\charactercreate.json (error {error})",
                              {{L"error", std::to_wstring(GetLastError())}});
            return r;
        }
        r.wrote = true;
    }
    const std::string receipt = arcanesky::Receipt(haveBuild ? std::to_string(product.BuildNumber) : std::string(),
                                                   {{name, arcanesky::Hash(made.data(), made.size())}});
    if (receipt != last && !WriteFile(receiptPath, receipt.data(), receipt.size())) {
        r.text = lang::TX(L"campfire_scene.no_receipt", L"could not write mods\\D2R-VR\\campfire_scene.json");
        return r;
    }
    r.state = kMade;
    r.text = r.wrote ? lang::TX(L"campfire_scene.made", L"made from your game: character creation's scene round the fire (mods\\D2R-VR\\D2R-VR.mpq), from the next start of the game")
                     : lang::TX(L"campfire_scene.ready", L"ready: character creation's scene round the fire (mods\\D2R-VR\\D2R-VR.mpq)");
    if (!tuned)
        r.text += L" - " + lang::TX(L"campfire_scene.plain", L"the plain scene: the tuned one could not be made ({why})", {{L"why", Wide(why)}});
    return r;
}

}  // namespace campfireassets
