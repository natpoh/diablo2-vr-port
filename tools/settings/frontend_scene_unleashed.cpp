// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem, Kudlatytrue and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed steamvr/prepare_frontend_scene.py at 1f3b994 (Kudlatytrue, PR #583; merged by bearstonem, PR #584): prepare() and
// every step it runs, ported line by line with the same numbers, the same order of arithmetic and Python's own random numbers
// (random.Random's Mersenne Twister) and math.hypot, so that the same game gives the same file byte for byte
// (cleanroom/tests/frontend_scene_unleashed_test.cpp compares it with the script's). Changed for D2R VR Authentic: a small JSON reader /
// writer of our own stands in for Python's json module (copied from campfire_assets_unleashed.cpp: keys in their order, numbers written as
// Python writes them); measure() reads the game's Granny models with shared/granny_remesh.h's file reader the way podglad_modeli reads
// them; the game's files are read through CascLib (MIT, Ladislav Zezula), read only; frontend_floors.json (prepare_frontend_floor.py's
// registry, made on Unleashed's machines) is not here, so floors() / floor_names() / ground_pavers() give nothing, as the script does
// without that file; the result goes into our mod's data folder with arcane_sky.h's receipt instead of Unleashed's installer. What comes
// out is Blizzard's file changed and stays on this machine, inside the mod: never in the repository or a build. Thanks to Kudlatytrue and
// bearstonem for the 3D main menu. MPL 2.0, see frontend_scene_unleashed.h.
//
// Steps of the script that its own settings switch off are not ported (they never run): scatter() (SCATTER = {}), lights()
// (LIGHTS_ON = False), road()'s own laying of trims and palms (ROAD's only act has TILES, which return first), tiles()' wide ground
// (wide_every 0), ROAD_FLAT, CURB_GRASS, TREES_OUT, LAVA_FALLS (all empty). Each place says so.

#include "frontend_scene_unleashed.h"
#include "frontend_edits_res.h"
#include "lang.h"   // lang::TX: the result line in the player's language (English in the tests)

#ifndef NOMINMAX
#define NOMINMAX 1   // (as CascLib's CascPort.h)
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <functional>
#include <memory>
#include <tuple>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "CascLib.h"
#include "arcane_sky.h"      // the receipt and its hash, the same as the Arcane sky's
#include "granny_remesh.h"   // measure(): the Granny file read and its sections decompressed

namespace frontendscene {

// ================================================================ Python's random.Random and math.hypot

namespace detail {

// CPython's _randommodule.c: MT19937, init_by_array with the seed's 32-bit words (our seeds: one word)
struct PyRandomState {
    uint32_t mt[624];
    int index = 625;
};

static void InitGenrand(PyRandomState* s, uint32_t seed) {
    uint32_t* mt = s->mt;
    mt[0] = seed;
    int mti = 1;
    for (; mti < 624; mti++) mt[mti] = (1812433253U * (mt[mti - 1] ^ (mt[mti - 1] >> 30)) + (uint32_t)mti);
    s->index = mti;
}

static void InitByArray(PyRandomState* s, const uint32_t* key, size_t keyLength) {
    uint32_t* mt = s->mt;
    InitGenrand(s, 19650218U);
    size_t i = 1, j = 0;
    size_t k = (624 > keyLength ? 624 : keyLength);
    for (; k; k--) {
        mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1664525U)) + key[j] + (uint32_t)j;
        i++;
        j++;
        if (i >= 624) { mt[0] = mt[623]; i = 1; }
        if (j >= keyLength) j = 0;
    }
    for (k = 623; k; k--) {
        mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1566083941U)) - (uint32_t)i;
        i++;
        if (i >= 624) { mt[0] = mt[623]; i = 1; }
    }
    mt[0] = 0x80000000U;
}

static uint32_t Genrand(PyRandomState* s) {
    static const uint32_t mag01[2] = {0x0U, 0x9908b0dfU};
    uint32_t* mt = s->mt;
    uint32_t y;
    if (s->index >= 624) {
        int kk;
        for (kk = 0; kk < 624 - 397; kk++) {
            y = (mt[kk] & 0x80000000U) | (mt[kk + 1] & 0x7fffffffU);
            mt[kk] = mt[kk + 397] ^ (y >> 1) ^ mag01[y & 0x1U];
        }
        for (; kk < 623; kk++) {
            y = (mt[kk] & 0x80000000U) | (mt[kk + 1] & 0x7fffffffU);
            mt[kk] = mt[kk + (397 - 624)] ^ (y >> 1) ^ mag01[y & 0x1U];
        }
        y = (mt[623] & 0x80000000U) | (mt[0] & 0x7fffffffU);
        mt[623] = mt[396] ^ (y >> 1) ^ mag01[y & 0x1U];
        s->index = 0;
    }
    y = mt[s->index++];
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9d2c5680U;
    y ^= (y << 15) & 0xefc60000U;
    y ^= (y >> 18);
    return y;
}

PyRandom::PyRandom(unsigned seed) : s_(new PyRandomState) {
    const uint32_t key = seed;   // abs(seed) in 32-bit words, little end first: one word for ours (0 too: [0])
    InitByArray(s_, &key, 1);
}
PyRandom::~PyRandom() { delete s_; }

// random(): 53 bits, a = genrand >> 5, b = genrand >> 6
double PyRandom::Random() {
    const uint32_t a = Genrand(s_) >> 5, b = Genrand(s_) >> 6;
    return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0);
}
unsigned PyRandom::GetRandBits32() { return Genrand(s_); }
// _randbelow_with_getrandbits(n): k = n.bit_length(), getrandbits(k) until below n
size_t PyRandom::RandBelow(size_t n) {
    if (n == 0) throw std::runtime_error("Cannot choose from an empty sequence");
    int k = 0;
    for (size_t v = n; v; v >>= 1) ++k;
    if (k > 32) throw std::runtime_error("a choice from too long a sequence");
    auto bits = [&]() -> size_t { return k == 0 ? 0 : (size_t)(Genrand(s_) >> (32 - k)); };
    size_t r = bits();
    while (r >= n) r = bits();
    return r;
}
// uniform(a, b): a + (b - a) * random()
double PyRandom::Uniform(double a, double b) { return a + (b - a) * Random(); }

// CPython's mathmodule.c vector_norm() for two coordinates (math.hypot since 3.10: lossless scaling, squaring and summing, one
// correction step - not the C library's hypot)
double PyHypot(double x, double y) {
    double vec[2] = {std::fabs(x), std::fabs(y)};
    double max = 0.0;
    bool foundNan = false;
    for (double v : vec) {
        foundNan = foundNan || std::isnan(v);
        if (v > max) max = v;
    }
    if (std::isinf(max)) return max;
    if (foundNan) return std::nan("");
    if (max == 0.0) return max;
    int maxE = 0;
    std::frexp(max, &maxE);
    if (maxE < -1023) {   // subnormals made normal first
        return 2.2250738585072014e-308 * PyHypot(vec[0] / 2.2250738585072014e-308, vec[1] / 2.2250738585072014e-308);
    }
    const double scale = std::ldexp(1.0, -maxE);
    double csum = 1.0, frac1 = 0.0, frac2 = 0.0;
    for (double v : vec) {
        const double xs = v * scale;
        const double prHi = xs * xs, prLo = std::fma(xs, xs, -prHi);   // dl_mul
        const double smHi = csum + prHi, smLo = (csum - smHi) + prHi;   // dl_fast_sum
        csum = smHi;
        frac1 += prLo;
        frac2 += smLo;
    }
    double h = std::sqrt(csum - 1.0 + (frac1 + frac2));
    {
        const double prHi = -h * h, prLo = std::fma(-h, h, -prHi);
        const double smHi = csum + prHi, smLo = (csum - smHi) + prHi;
        csum = smHi;
        frac1 += prLo;
        frac2 += smLo;
    }
    const double xr = csum - 1.0 + (frac1 + frac2);
    h += xr / (2.0 * h);
    return h / scale;
}

}  // namespace detail

namespace {

using detail::PyHypot;
using detail::PyRandom;

// ================================================================ a small JSON DOM, as Python's json module reads and writes it
// (campfire_assets_unleashed.cpp's, the writer compact: separators=(',', ':'))

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
const Value& Need(const Value& obj, const char* key) { return Need(const_cast<Value&>(obj), key); }

std::string Lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}
bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
bool Starts(const std::string& s, const char* part) { return s.rfind(part, 0) == 0; }

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

// f'{v:.0f}' / f'{v:.2f}' (correctly rounded, half to even on the exact value - as Python's)
std::string Fixed(double d, int places) {
    if (std::isnan(d)) return "nan";
    if (std::isinf(d)) return d < 0 ? "-inf" : "inf";
    char buf[400];
    const auto r = std::to_chars(buf, buf + sizeof buf, d, std::chars_format::fixed, places);
    return std::string(buf, r.ptr);
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

// truthiness
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
bool IsStr(const Value* v, const std::string& s) { return v && v->kind == Value::String && v->s == s; }

// ---------------------------------------------------------------- reading (json.loads)

struct Reader {
    const char* p;
    const char* end;
    int depth = 0;

    [[noreturn]] void Fail(const char* what) { throw Bad(std::string("not JSON (") + what + ")"); }
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

// json.loads(bytes): a UTF-8 BOM is skipped (utf-8-sig); json.loads(str) (bytes = false) refuses one
Value ParseJson(const std::string& text, bool bytes = true) {
    Reader r{text.data(), text.data() + text.size()};
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
        if (!bytes) throw Bad("Unexpected UTF-8 BOM (decode using utf-8-sig)");
        r.p += 3;
    }
    Value v = r.Parse();
    r.Space();
    if (r.p != r.end) r.Fail("extra data");
    return v;
}

// ---------------------------------------------------------------- writing (json.dumps(v, ensure_ascii=False, separators=(',', ':')))

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

void Put(std::string& out, const Value& v) {
    switch (v.kind) {
    case Value::Null: out += "null"; return;
    case Value::Bool: out += v.b ? "true" : "false"; return;
    case Value::Int: out += IntText(v); return;
    case Value::Float: out += Repr(v.d); return;
    case Value::String: PutString(out, v.s); return;
    case Value::Array:
        out += '[';
        for (size_t k = 0; k < v.a.size(); ++k) {
            if (k) out += ',';
            Put(out, v.a[k]);
        }
        out += ']';
        return;
    case Value::Object:
        out += '{';
        for (size_t k = 0; k < v.o.size(); ++k) {
            if (k) out += ',';
            PutString(out, v.o[k].key);
            out += ':';
            Put(out, v.o[k].value);
        }
        out += '}';
        return;
    }
}

// ================================================================ prepare_frontend_scene.py: the settings

const double kPi = 3.141592653589793;
const double kDegToRad = kPi / 180.0;   // math.radians: x * (pi / 180)
const double kRadToDeg = 180.0 / kPi;   // math.degrees
double Radians(double x) { return x * kDegToRad; }
double Degrees(double x) { return x * kRadToDeg; }

// math.atan2 (CPython's m_atan2: its zeros and infinities, then the C library's)
double PyAtan2(double y, double x) {
    if (std::isnan(x) || std::isnan(y)) return std::nan("");
    if (std::isinf(y)) {
        if (std::isinf(x)) {
            if (std::copysign(1., x) == 1.) return std::copysign(0.25 * kPi, y);
            return std::copysign(0.75 * kPi, y);
        }
        return std::copysign(0.5 * kPi, y);
    }
    if (std::isinf(x) || y == 0.) {
        if (std::copysign(1., x) == 1.) return std::copysign(0., y);
        return std::copysign(kPi, y);
    }
    return std::atan2(y, x);
}
// math.acos (a domain error raises)
double PyAcos(double x) {
    if (std::isnan(x)) return x;
    if (x < -1.0 || x > 1.0) throw Bad("math domain error");
    return std::acos(x);
}
double PySqrt(double x) {
    if (x < 0.0) throw Bad("math domain error");
    return std::sqrt(x);
}
// Python's max(a, b) / min(a, b): the first one unless the second is greater / smaller
double PyMax(double a, double b) { return b > a ? b : a; }
double PyMin(double a, double b) { return b < a ? b : a; }
// math.floor(x) as an int
int64_t PyFloor(double x) {
    if (!std::isfinite(x)) throw Bad("cannot convert float to integer");
    return (int64_t)std::floor(x);
}
// int(x)
int64_t PyInt(double x) {
    if (!std::isfinite(x)) throw Bad("cannot convert float to integer");
    return (int64_t)std::trunc(x);
}

const char* const PRESET_NAME = kPreset;
const char* const CLOUDS = "_clouds";
const char* const COPY = "_pcvr_r";

struct Cam { double x, y, z; };
const Cam CAMERAS[6] = {{0, 0, 0}, {50.0, 4.0, 483.0}, {80.0, 4.0, 110.0}, {23.0, 4.0, -313.5}, {30.0, 4.0, -789.0}, {34.0, 4.0, -1258.5}};
const int ACTS[] = {1, 2, 3, 4, 5};
const double REACH = 700.0;
const double LANE = 180.0;
const double TURNS[] = {90.0, 180.0, 270.0};
const double FRONT = 60.0;
const double NEAR_ = 6.0;
const double FAR_ = 220.0;
const double SINK = 0.06;
double SinkUnder(int act) { return act == 2 ? 2.5 : 0.0; }   // SINK_UNDER = {2: 2.5}

using Strs = std::vector<std::string>;
const Strs GROUND = {"ui_groundplane/terrain_act", "ui_act2_vignette/ui_act2_vignette_terrain"};
const Strs NOT_ = {"terrain_act1_path", "_sky.", "campfire", "bonfire", "wall", "top_cap", "fence", "stoneware"};
// NOT_OK = {}: nothing of an act's own comes round past NOT
Strs NotCopy(int act) { return act == 4 ? Strs{"lava_skull_pile_large03_04"} : Strs{}; }
Strs Plus(Strs a, const Strs& b) { a.insert(a.end(), b.begin(), b.end()); return a; }
const Strs KEEP = Plus(GROUND, {"grass", "weeds", "bush", "fern", "tree", "ivy", "branch", "boulder", "stone", "pebbles", "grunge"});
// FAR_KINDS = (); FAR_ACT = {5: (190.0, ('act5_vignette_mountain',))}
const Strs KEEP_ACT2 = Plus(GROUND, {"grass", "bush", "palm", "mound", "sand_pile", "rock_"});
const Strs KEEP_ACT4 = Plus(GROUND, {"ui_act4_vignette/rocks/", "act4_lava_islands/", "act4_lava_skull_piles/", "act4_mesa_skeleton_piles/",
                                     "act4_mesa_rubbles_props/", "act4_lava_crystals/", "act4_diab_rubble/", "act4_mesa_props/",
                                     "act4_mesa_pillars/", "act4_mesa_rubble/", "act4_lava_lava_kits/", "ui_act4_vignette/architecture/",
                                     "act4_diab_cliffs/", "act4_lava_floor_tiles/", "act4_lava_props/", "act4_mesa_gates/"});
const Strs KEEP_ACT5 = Plus(GROUND, {"expansion_grass", "expansion_shrub", "expansion_flower", "expansion_tree_", "expansion_bush_",
                                     "rocks_snowy", "slush_pile", "snow_pile", "snow_grunge", "ui_pebbles", "rock_pile", "act5_vignette_mountain"});
const Strs& KeepAct(int act) { return act == 2 ? KEEP_ACT2 : act == 4 ? KEEP_ACT4 : act == 5 ? KEEP_ACT5 : KEEP; }
double LaneAct(int act) { return act == 2 ? 120.0 : (act == 4 || act == 5) ? 150.0 : LANE; }
bool SidesOf(int act, double* lo, double* hi) {
    switch (act) {
    case 2: *lo = -85.0; *hi = 200.0; return true;
    case 4: *lo = -190.0; *hi = 190.0; return true;
    case 5: *lo = -190.0; *hi = 450.0; return true;
    default: *lo = -1e9; *hi = 1e9; return false;
    }
}
bool SoundOk(int act) { return act == 2; }
bool WallsOk(int act) { return act == 4; }

const char* const FOREST_TREES[] = {"data/hd/env/foliage/act1/model/act1_trees_beech_frontend/act1_tree_beech_frontend01.model",
                                    "data/hd/env/foliage/act1/model/act1_trees_elm_frontend/act1_tree_elm_frontend01.model"};
const std::pair<double, int> FOREST_RINGS[] = {{35.0, 3}, {65.0, 6}, {100.0, 11}, {140.0, 18}, {185.0, 28}};
const double FOREST_ARC = 110.0;
const double FOREST_JITTER = 0.35;
const double FOREST_SCALE[2] = {0.9, 1.4};
const double FOREST_Y = -0.5;
const double FOREST_NEAR = 30.0;

Strs DarkTrees() {
    Strs out;
    for (const char* m : {"act1_trees_elm/act1_tree_elm02", "act1_trees_beech/act1_tree_beech01", "act1_trees_maple/act1_tree_maple04",
                          "act1_trees_linden/act1_tree_linden02", "act1_trees_linden/act1_tree_linden01", "act1_trees_elm/act1_tree_elm01",
                          "act1_trees_oak/act1_tree_oak03", "act1_trees_maple/act1_tree_maple02"})
        out.push_back(std::string("data/hd/env/foliage/act1/model/") + m + ".model");
    return out;
}
const Strs DARK_TREES = DarkTrees();
const size_t DARK_COUNT = 100;
const double DARK_ARC = 115.0;
const double DARK_NEAR = 40.0, DARK_FAR = 240.0;
const double DARK_GAP[2] = {24.0, 10.0};
const double DARK_CLUMP = 0.4;
const double DARK_SCALE[2] = {0.9, 1.15};
const double DARK_TALL = 1.2;

const double GRID = 40.0;
const double GRID_LEFT = -260.0, GRID_FRONT = 260.0;

// _cells(cols, rows): f'{c}{r}' for c in cols for r in rows (rows: range(a, b))
Strs Cells(const char* cols, int a, int b) {
    Strs out;
    for (const char* c = cols; *c; ++c)
        for (int r = a; r < b; ++r) out.push_back(std::string(1, *c) + std::to_string(r));
    return out;
}
Strs CellsRows(const char* cols, std::initializer_list<int> rows) {
    Strs out;
    for (const char* c = cols; *c; ++c)
        for (int r : rows) out.push_back(std::string(1, *c) + std::to_string(r));
    return out;
}

const Strs CLEAR1 = {"F7", "F8", "G7", "G8", "H8"};
const Strs& Clear(int act) { static const Strs none; return act == 1 ? CLEAR1 : none; }

struct Thick { Strs fields; size_t count; };
const std::vector<Thick> THICK1 = {
    {{"E8", "E9", "F8", "F9", "G8", "G9", "H8", "H9"}, 50},
    {Cells("CD", 5, 10), 50},
    {Cells("IJ", 5, 10), 50},
    {{"E6", "E7", "F6", "F7"}, 20},
    {Cells("IJ", 3, 6), 30},
    {{"E4", "E5"}, 10},
    {{"D3", "D4"}, 6},
    {{"F5"}, 5}};
const double THICK_NEAR = 16.0;
const double THICK_GAP = 7.0;

const Strs GRASS1 = Plus(Plus(Cells("ABC", 6, 14), Cells("DEFGH", 8, 14)), Cells("IJKLM", 6, 14));
Strs GrassModels() {
    Strs out;
    for (const char* m : {"ui_act1_grass_clump05", "ui_act1_grass_clump05", "ui_act1_grass_clump03", "ui_act1_grass_clump_low01",
                          "ui_act1_grass_clump_low01", "ui_act1_grass_clump04", "ui_act1_weeds06", "ui_act1_grass_tall01"})
        out.push_back(std::string("data/hd/env/model/ui/ui_act1_vignette/foliage/") + m + ".model");
    return out;
}
const Strs GRASS_MODELS = GrassModels();
Strs GrassMoreModels() {
    Strs out = GRASS_MODELS;
    for (const char* m : {"ui_act1_bush_hawthorn01", "ui_act1_bush_hawthorn05", "ui_act1_bush_witchhazel01", "ui_act1_bush_witchhazel04",
                          "ui_act1_weeds02", "ui_act1_weeds03", "ui_act1_weeds05"})
        out.push_back(std::string("data/hd/env/model/ui/ui_act1_vignette/foliage/") + m + ".model");
    return out;
}
struct GrassMore { Strs fields; Strs models; unsigned seed; };
const std::vector<GrassMore> GRASS_MORE1 = {
    {Plus({"E6", "E7", "F6", "F7"}, Cells("IJ", 3, 6)), GrassMoreModels(), 5101},
    {Cells("H", 3, 8), GRASS_MODELS, 5102}};
const Strs GRASS_DROP1 = Plus(Cells("ABCDEFGHIJKLM", 11, 14), Cells("ABCDKLM", 1, 14));
const Strs FOLIAGE_DROP1 = Cells("CD", 1, 14);
Strs FoliageHalf1() {
    Strs out = Cells("EJ", 1, 14);
    for (const char* c = "ABCDEFGHIJKLM"; *c; ++c) out.push_back(std::string(1, *c) + "10");
    return out;
}
const Strs FOLIAGE_HALF1 = FoliageHalf1();
// TREES_OUT = {}: none
struct TreeAt { const char* field; const char* model; bool hasOff; double off[2]; bool hasSize; double size; };
const TreeAt TREES_AT1[] = {
    {"F6", "data/hd/env/foliage/act1/model/act1_trees_elm/act1_tree_elm02.model", false, {0.0, 0.0}, false, 1.0},
    {"E5", "data/hd/env/foliage/act1/model/act1_trees_oak/act1_tree_oak03.model", true, {20.0, -20.0}, true, 1.4}};
struct Thin { Strs fields; size_t n; };
const std::vector<Thin> THIN1 = {{CellsRows("ABCDEFGHIJKLM", {12, 13}), 2}, {Cells("ABC", 1, 14), 2}};
const double GRASS_STEP = 5.0;

const char* const A2_PALMS[] = {"data/hd/env/foliage/act2/model/act2_trees_palm/act2_tree_palm01.model",
                                "data/hd/env/foliage/act2/model/act2_trees_palm/act2_tree_palm02.model"};
// SCATTER = {} (Act 2's own scatter put aside): scatter() lays nothing, and off_road() is only its
const double SCATTER_NEAR = 6.0;

// WALLS = {2: ('ui_wall01_01', 'ui_wall01_02', 'ui_wall01_cheap_05', 9, 4, 0.8)}, WALLS_RIGHT_OF = {2: 'ui_wall01_03'}

// ROAD = {2: (run, ends, 340.0, 3.8)}
const Strs ROAD_RUN2 = {"trim_straight02_04", "trim_single02_03", "trim_straight01_11", "trim_straight01_05", "trim_straight03_07",
                        "trim_straight06_14", "trim_straight01_13", "trim_straight03_08"};
const Strs ROAD_ENDS2 = {"trim_pile01_01", "trim_end01_12", "trim_end01_11", "trim_straight05_11", "trim_corner01_07"};

// TILES = {2: ('data/hd/env/preset/act2/town/transw2.json', 137.5, 100.0, -1, 6, 0.4, 1.0, 15.0)}
const char* const TILES2 = "data/hd/env/preset/act2/town/transw2.json";
const double TILE_MID = 137.5, TILE_WIDTH = 100.0, TILE_HIGH = 0.4, TILE_START = 15.0;
const int TILE_FIRST = -1, TILE_COUNT = 6;
const double TILE_CURBS2[4] = {115.0, 160.0, 63.0, 113.0};
const double CURB_LIFT2 = 0.20;
const double TILE_SIDES[2] = {-125.0, 215.0};
const double* const ROAD_SIDES = TILE_SIDES;
const double ROAD_SHIFT2 = 0.0;
const double ROAD_COBBLES_OFF = 14.5;
const double RS_ = 11.6;
std::vector<std::pair<double, double>> RoadPath2() {
    const double pts[][2] = {{0.0, 12.0}, {0.0, -40.0}, {6.0, -65.0}, {20.0, -90.0}, {40.0, -115.0}, {71.0, -154.0}, {102.0, -193.0},
                             {133.0, -232.0}, {164.0, -271.0}, {226.0, -349.0}, {350.0, -505.0}};
    std::vector<std::pair<double, double>> out;
    for (const auto& p : pts) out.push_back({p[0] + RS_, p[1]});
    return out;
}
const double ROAD_HALF = 14.5;
const double ROAD_NARROW2 = 0.8;
struct SidePalm { double along, side, size; };
const SidePalm ROAD_SIDE_PALMS2[] = {{22.0, -22.0, 1.1}, {38.0, 23.0, 1.0}};
const double ROAD_FAR2 = 520.0;
// ROAD_GROUND = {2: ('ui_act2_vignette_terrain_01', 70.0, 0.5, 1.0, 0.0, 0.0)}, ROAD_GROUND_COUNT = {2: 1}
const char* const ROAD_GROUND_NAME = "ui_act2_vignette_terrain_01";
const double RG_EVERY = 70.0, RG_LOWER = 0.5, RG_WIDE = 1.0, RG_WIDE_EVERY = 0.0, RG_WIDE_LOWER = 0.0;
const int RG_COUNT = 1;
// ROAD_FLAT = {}; TILE_ONLY = {2: ('stone_trims/trim_',)}
const char* const TILE_ONLY2 = "stone_trims/trim_";

struct Tower { const char* model; double right, ahead, y, size; };
const Tower TOWERS2[] = {{"data/hd/env/model/act2/town/act2_town_palace_walls/tower01.model", -95.0, 58.0, 0.0, 1.7},
                         {"data/hd/env/model/act2/town/act2_town_palace_walls/tower01.model", -7.0, 205.0, -3.0, 1.15}};
struct Nudge { const char* name; double dy; };
const Nudge NUDGE2[] = {{"act2_wadi_bush01_12", 0.5}, {"act2_grass_clump02_14", 0.5}, {"act2_grass_clump02_19", 0.5}};
struct Piece { std::string model; double right, ahead, y, turn, size; };
std::vector<Piece> SetPieces2() {
    const std::string CL = "data/hd/env/model/act2/bigcliff/act2_bigcliff_cliffs/";
    std::vector<Piece> out;
    const double rows[][2] = {{-6.0, 90.0}, {-36.0, 270.0}, {-66.0, 90.0}, {-96.0, 270.0}, {-126.0, 90.0}, {-156.0, 270.0}, {-186.0, 90.0},
                              {-216.0, 270.0}, {-246.0, 90.0}, {-276.0, 270.0}, {-306.0, 90.0}};
    for (const auto& r : rows) out.push_back({CL + "border02.model", -108.0, r[0], 0.0, r[1], 1.0});
    out.push_back({"data/hd/env/model/ui/ui_groundplane/act2_groundplane.model", 20.0, -100.0, 0.2, 0.0, 1.0});
    return out;
}
const std::vector<Piece> SET_PIECES2 = SetPieces2();
const char* const TILE_KEEP[] = {"TransformDefinitionComponent", "ModelDefinitionComponent", "ModelPlatformTierComponent", "VfxDefinitionComponent"};
const double GRASS_SCALE[2] = {0.8, 1.3};
const double GRASS_NEAR = 4.0;

const Strs IVY_FIELDS1 = {"E6", "E7"};
const double IVY_TREE = 12.0;

const Strs HIDE = {"act1_sky_01", "act2_sky_01", "act3_sky_01", "act4_sky_01", "act5_sky_01", "ui_skybox_act3_depth_fade01_01",
                   "ui_skybox_act5_depth_fade01_01", "wall_posts01_03", "FX_SandHaze_low_small_gusts_01", "FX_SandHaze_low_small_gusts_03",
                   "FX_UIFrontEndAct2_sand_30x30_01", "FX_UIFrontEndAct2_motes_30x30_01"};
// MOON, GLOW_RINGS ...: LIGHTS_ON = False - lights() puts none in the scene

// TOWN = {3: ('data/hd/env/preset/act3/docktown/docktown3.json', (265.8, 268.7), 0.7, (-75.0, 75.0, 12.0, 250.0), 420.0, (-245.0, 245.0))}
const char* const TOWN3 = "data/hd/env/preset/act3/docktown/docktown3.json";
const double TOWN_AT[2] = {265.8, 268.7};
const double TOWN_DY = 0.7;
const double TOWN_ZONE[4] = {-75.0, 75.0, 12.0, 250.0};
const double TOWN_REACH = 420.0;
const double TOWN_LOHI[2] = {-245.0, 245.0};
const double TOWN_ALIGN3[2] = {-4.0, 10.0};
const Strs TOWN_ALWAYS = {"stone_head"};
const double TOWN_WATERLINE3 = -3.22;
const Strs TOWN_FLOATING = {"scum", "act3_docktown_boats", "bobbing_misc", "grass_clump_water", "stone_head", "act2_town_ship", "lily"};
const char* const TOWN_KEEP[] = {"TransformDefinitionComponent", "ModelDefinitionComponent", "ModelPlatformTierComponent", "PrefabPlacementDefinitionComponent"};
const char* const TOWN_SKIP_KINDS[] = {"SkeletonDefinitionComponent", "PhysicsClothDefinitionComponent", "PhysicsPartialRagdollDefinitionComponent"};
const int64_t TOWN_SKIP_ID = 3418903870LL;
const double TOWN_RIGHT_MAX3[2] = {200.0, 239.0};
const char* const TOWN_WATER3 = "water01_01";
const double TOWN_WATER_Y3 = -3.0;
// TOWN_PLATES = {3: (('act3_stone', -15.0, -70.0, 0.35, 80.0, 1.0),)}; GROUND_SWAP = {3: (('terrain_act3_01', 'act3_ground'),)}
const double WATER_GUESS[4] = {-0.83 * 128, -128.0, 0.42 * 128, 128.0};

// LAVA = {4: ('expansion_lavae2_01', 420.0, (-205.0, 200.0))}
const char* const LAVA4 = "expansion_lavae2_01";
const double LAVA_REACH = 420.0;
const double LAVA_LOHI[2] = {-205.0, 200.0};
const double LAVA_GUESS[4] = {0.0, 0.0, 200.0, 200.0};
// LAVA_FALLS = {}: none to try
const Strs DROP_NAMES4 = {"hell_brazier02_01", "ui_wall_destroyed02_01", "rubble_pile02_04", "lava04_off_02", "ui_pillar01_blank_topless_01",
                          "pillar05_05", "gate_chain_single01_03", "gate_chain_single01_04"};
const double DROP4 = 10.5;
const int DROP_OUT4[] = {270};
const double STATUE_SPOTS4[3][2] = {{-15.0, 0.0}, {0.0, -15.0}, {15.0, 0.0}};
const double STATUE_Y = 0.8, STATUE_SIZE = 1.0;
const char* const STATUE_MODELS[] = {"data/hd/env/model/act4/diab/act4_diab_props/statue01.model",
                                     "data/hd/env/model/act4/diab/act4_diab_props/statue01_head_neutral.model"};
Strs StatueTextures() {
    Strs out;
    for (const char* p : {"a", "b"})
        for (const char* m : {"alb", "nrm", "orm"})
            out.push_back(std::string("data/hd/env/texture/act4/diab/act4_diab_props_statue01_") + p + "_" + m + ".texture");
    return out;
}

bool In(const Strs& list, const std::string& s) { return std::find(list.begin(), list.end(), s) != list.end(); }
bool AnyIn(const Strs& parts, const std::string& s) {
    for (const std::string& p : parts) if (Has(s, p)) return true;
    return false;
}

// ---------------------------------------------------------------- floors (prepare_frontend_floor.py's frontend_floors.json)
// The script reads its registry of floor plates beside itself and gives nothing when the file is not there; D2R VR Authentic has no such
// file yet. A later port of prepare_frontend_floor.py feeds these three (the registry's {name: {"model", "maps", "pavers"}}).
struct Paver { double x, y, z, yaw, scale; std::string model; };
// floors(): {model path, lower case: its terrain maps without _ALB.texture}
std::map<std::string, std::string> Floors() { return {}; }
// floor_names(): {'act3_stone': its model path, ...}
std::map<std::string, std::string> FloorNames() { return {}; }
// ground_pavers(name): [[x, y, z, yaw, scale, model], ...] in its space
std::vector<Paver> GroundPavers(const std::string&) { return {}; }

// ================================================================ the steps

using Ids = std::unordered_set<int64_t>;
constexpr uint64_t kMask = 0xFFFFFFFFull;

struct Q { double x, y, z, w; };
// qmul(a, b), the script's terms in its order
Q QMul(const Q& a, const Q& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
Q YawQ(double th) { return {0.0, std::sin(th / 2), 0.0, std::cos(th / 2)}; }
// tuple(float(q.get(k, 0.0)) for k in 'xyzw')
Q QOf(const Value& q) { return {Float(Get(q, "x"), 0.0), Float(Get(q, "y"), 0.0), Float(Get(q, "z"), 0.0), Float(Get(q, "w"), 0.0)}; }
Value QVal(const Q& q) {
    Value v = MakeObj();
    Set(v, "x", q.x);
    Set(v, "y", q.y);
    Set(v, "z", q.z);
    Set(v, "w", q.w);
    return v;
}
Value Vec3(double x, double y, double z) {
    Value v = MakeObj();
    Set(v, "x", x);
    Set(v, "y", y);
    Set(v, "z", z);
    return v;
}
// t.get('orientation') or {'x': 0.0, 'y': 0.0, 'z': 0.0, 'w': 1.0}
Q OrientationOr(Value& t) {
    const Value* o = Get(t, "orientation");
    if (Truthy(o)) return QOf(*o);
    return {0.0, 0.0, 0.0, 1.0};
}

// tilt_q(tilt_x, tilt_z): first about its z, then about its x (float(tilt or 0))
Q TiltQ(const Value* tx, const Value* tz) {
    const double ax = Radians(Truthy(tx) ? Float(tx, 0.0) : 0.0), az = Radians(Truthy(tz) ? Float(tz, 0.0) : 0.0);
    return QMul({std::sin(ax / 2), 0.0, 0.0, std::cos(ax / 2)}, {0.0, 0.0, std::sin(az / 2), std::cos(az / 2)});
}

// entity.get('components', []): the list (each a dict), nullptr when not there
Value* Components(Value& e) {
    Value* c = Get(e, "components");
    if (!c) return nullptr;
    if (c->kind != Value::Array) throw Bad("components is no list");
    for (Value& x : c->a) if (x.kind != Value::Object) throw Bad("a component is no object");
    return c;
}
bool IsType(const Value& c, const char* type) { return IsStr(Get(c, "type"), type); }
// c.get('type') as a set member: a str as it is, anything else marked apart
std::string TypeKey(const Value& c) {
    const Value* t = Get(c, "type");
    if (!t) return "\x01None";
    if (t->kind == Value::String) return t->s;
    return "\x01" + Str(*t);
}
std::set<std::string> Kinds(Value& e) {
    std::set<std::string> k;
    if (Value* comps = Components(e)) for (Value& c : comps->a) k.insert(TypeKey(c));
    return k;
}
bool KindsWithin(const std::set<std::string>& kinds, std::initializer_list<const char*> allowed) {
    for (const std::string& k : kinds) {
        bool ok = false;
        for (const char* a : allowed) if (k == a) { ok = true; break; }
        if (!ok) return false;
    }
    return true;
}

// transform(entity)
Value* Transform(Value& e) {
    if (Value* comps = Components(e)) for (Value& c : comps->a) if (IsType(c, "TransformDefinitionComponent")) return &c;
    return nullptr;
}
// `if t` / `if not t`: a transform there and not an empty dict
bool TruthyT(const Value* t) { return t && Truthy(t); }
// model_of(entity)
std::string ModelOf(Value& e) {
    if (Value* comps = Components(e)) for (Value& c : comps->a) if (IsType(c, "ModelDefinitionComponent")) return Lower(StrOr(Get(c, "filename"), ""));
    return "";
}
std::string Base(const std::string& m) {
    const size_t at = m.rfind('/');
    return at == std::string::npos ? m : m.substr(at + 1);
}
// plain_model(entity, sound)
bool PlainModel(Value& e, bool sound) {
    const std::set<std::string> kinds = Kinds(e);
    if (!kinds.count("ModelDefinitionComponent")) return false;
    if (sound) return KindsWithin(kinds, {"TransformDefinitionComponent", "ModelDefinitionComponent", "ModelPlatformTierComponent", "AudioEmitterComponent"});
    return KindsWithin(kinds, {"TransformDefinitionComponent", "ModelDefinitionComponent", "ModelPlatformTierComponent"});
}
std::string NameStr(const Value& e) { return StrOr(Get(e, "name"), ""); }
bool NameIs(const Value& e, const std::string& s) { return IsStr(Get(e, "name"), s); }
bool NameIn(const Value& e, const Strs& list) {
    const Value* n = Get(e, "name");
    return n && n->kind == Value::String && In(list, n->s);
}

Value& Position(Value& t) { return Need(t, "position"); }
double PX(Value& t) { return Float(&Need(Position(t), "x"), 0.0); }
double PZ(Value& t) { return Float(&Need(Position(t), "z"), 0.0); }

uint32_t Fresh(uint64_t start, Ids& ids) {   // the copies' ids: while new in ids or new == 0: new += 0x01000193
    uint64_t v = start & kMask;
    while (ids.count((int64_t)v) || v == 0) v = (v + 0x01000193) & kMask;
    ids.insert((int64_t)v);
    return (uint32_t)v;
}
// new_id(rnd, ids)
uint32_t NewId(PyRandom& rnd, Ids& ids) {
    uint32_t v = rnd.GetRandBits32();
    while (ids.count((int64_t)v) || v == 0) v = rnd.GetRandBits32();
    ids.insert((int64_t)v);
    return v;
}

// tree(name, ident, model, x, y, z, yaw, scale)
Value Tree(const Value& name, uint32_t ident, const Value& model, double x, double y, double z, double yaw, double sx, double sy, double sz) {
    const std::string n = Str(name);
    Value e = MakeObj();
    Set(e, "type", MakeStr("Entity"));
    Set(e, "name", name);
    Set(e, "id", MakeInt(ident));
    Value comps = MakeArr();
    Value t = MakeObj();
    Set(t, "type", MakeStr("TransformDefinitionComponent"));
    Set(t, "name", MakeStr(n + "_TransformDefinitionComponent"));
    Set(t, "position", Vec3(x, y, z));
    Set(t, "orientation", QVal({0.0, std::sin(yaw / 2), 0.0, std::cos(yaw / 2)}));
    Set(t, "scale", Vec3(sx, sy, sz));
    Set(t, "inheritOnlyPosition", MakeBool(false));
    comps.a.push_back(std::move(t));
    Value m = MakeObj();
    Set(m, "type", MakeStr("ModelDefinitionComponent"));
    Set(m, "name", MakeStr(n + "_ModelDefinitionComponent"));
    Set(m, "filename", model);
    Set(m, "visibleLayers", MakeInt(1));
    Set(m, "lightMask", MakeInt(19));
    Set(m, "shadowMask", MakeInt(0));
    Set(m, "ghostShadows", MakeBool(false));
    Set(m, "floorModel", MakeBool(false));
    Set(m, "terrainBlendEnableYUpBlend", MakeBool(false));
    Set(m, "terrainBlendMode", MakeInt(1));
    comps.a.push_back(std::move(m));
    Set(e, "components", std::move(comps));
    return e;
}
Value Tree(const std::string& name, uint32_t ident, const std::string& model, double x, double y, double z, double yaw, double sx, double sy, double sz) {
    return Tree(MakeStr(name), ident, MakeStr(model), x, y, z, yaw, sx, sy, sz);
}

// c['name'] = f'{name}_{c.get("type", "c")}' for every component
void RenameComponents(Value& e, const std::string& name) {
    if (Value* comps = &Need(e, "components")) {
        if (comps->kind != Value::Array) throw Bad("components is no list");
        for (Value& c : comps->a) Set(c, "name", MakeStr(name + "_" + StrOr(Get(c, "type"), "c")));
    }
}

struct Scene {
    Value preset;
    Ids ids;
    const std::map<std::string, std::string>& sources;
    std::map<int, std::vector<std::pair<double, double>>> roadPath;   // ROAD_PATH (the edits' road over it)
    std::map<int, double> roadNarrow;                                 // ROAD_NARROW
    struct Line { double x0, z0, d0, d1; };
    std::map<int, Line> roadLine;                                     // ROAD_LINE, set by road()
    std::map<int, std::string> townReport;                            // TOWN_REPORT
    std::map<std::string, std::array<double, 4>> waterExtent, lavaExtent;

    explicit Scene(const std::map<std::string, std::string>& src) : sources(src) {
        roadPath[2] = RoadPath2();
        roadNarrow[2] = ROAD_NARROW2;
    }

    Value& Ents() {
        Value& e = Need(preset, "entities");
        if (e.kind != Value::Array) throw Bad("entities is no list");
        return e;
    }
    template <class F>
    size_t RemoveIf(F gone) {   // preset['entities'] = [e for e in ... if not gone(e)]
        std::vector<Value>& v = Ents().a;
        std::vector<Value> keep;
        keep.reserve(v.size());
        size_t out = 0;
        for (size_t k = 0; k < v.size(); ++k) {
            if (gone(v[k], k)) { ++out; continue; }
            keep.push_back(std::move(v[k]));
        }
        v = std::move(keep);
        return out;
    }
    void Extend(std::vector<Value>& added) {
        std::vector<Value>& v = Ents().a;
        for (Value& x : added) v.push_back(std::move(x));
        added.clear();
    }
    // by_name(preset)[name]: the last entity of that name
    size_t ByName(const std::string& name) {
        std::vector<Value>& v = Ents().a;
        for (size_t k = v.size(); k-- > 0;) if (NameIs(v[k], name)) return k;
        throw Bad("no entity '" + name + "' in the preset");
    }

    // depend(preset, models)
    void Depend(const Strs& models) {
        Value& deps = SetDefault(SetDefault(preset, "dependencies", MakeObj()), "models", MakeArr());
        if (deps.kind != Value::Array) throw Bad("dependencies' models is no list");
        std::set<std::string> have;
        for (Value& d : deps.a) if (d.kind == Value::Object) have.insert(Lower(StrOr(Get(d, "path"), "")));
        const std::map<std::string, std::string> plates = Floors();
        for (const std::string& m : models) {
            if (!have.count(Lower(m))) {
                Value p = MakeObj();
                Set(p, "path", MakeStr(m));
                deps.a.push_back(std::move(p));
                have.insert(Lower(m));
            }
            const auto plate = plates.find(Lower(m));
            if (plate != plates.end()) {   // a floor plate of ours: the game's terrain maps it names
                Value& tex = SetDefault(Need(preset, "dependencies"), "textures", MakeArr());
                std::set<std::string> th;
                for (Value& d : tex.a) if (d.kind == Value::Object) th.insert(Lower(StrOr(Get(d, "path"), "")));
                for (const char* w : {"ALB", "NRM", "ORM"}) {
                    const std::string t = plate->second + "_" + w + ".texture";
                    if (!th.count(Lower(t))) {
                        Value p = MakeObj();
                        Set(p, "path", MakeStr(t));
                        tex.a.push_back(std::move(p));
                    }
                }
            }
        }
    }
    void Depend(const std::vector<Value>& models) {   // depend(preset, (a['model'],)) / sorted(models) of file names as the presets hold them
        Strs m;
        for (const Value& v : models) {
            if (v.kind != Value::String) throw Bad("a model file name that is no string");
            m.push_back(v.s);
        }
        Depend(m);
    }

    // field(act, x, z)
    static std::string Field(int act, double x, double z) {
        const Cam& c = CAMERAS[act];
        const double right = c.z - z, ahead = c.x - x;
        const int64_t col = PyFloor((right - GRID_LEFT) / GRID), row = PyFloor((GRID_FRONT - ahead) / GRID) + 1;
        if (0 <= col && col < 26 && row > 0) return std::string(1, (char)(65 + col)) + std::to_string(row);
        return "";
    }
    static std::string FieldOf(int act, Value& t) { return Field(act, PX(t), PZ(t)); }

    // without_clouds(preset)
    size_t WithoutClouds() {
        return RemoveIf([](Value& e, size_t) { return Has(Lower(NameStr(e)), CLOUDS); });
    }
    // hide(preset)
    size_t Hide() {
        return RemoveIf([](Value& e, size_t) { return NameIn(e, HIDE); });
    }

    // fill_round(preset, act, ids)
    std::pair<size_t, size_t> FillRound(int act) {
        if (act == 2 || act == 3) return {0, 0};   // act in TILES or act in TOWN
        const Cam& cam = CAMERAS[act];
        const double cx = cam.x, cy = cam.y, cz = cam.z;
        std::vector<size_t> own;
        std::vector<Value>& ents = Ents().a;
        for (size_t k = 0; k < ents.size(); ++k) {
            Value& e = ents[k];
            Value* t = Transform(e);
            const std::string m = ModelOf(e);
            const std::string name = NameStr(e);
            if (!TruthyT(t) || m.empty() || Has(name, COPY) || Starts(name, "pcvr_")) continue;
            const std::set<std::string> kinds = Kinds(e);
            if (!PlainModel(e, false) && !(SoundOk(act) && PlainModel(e, true)) &&
                !(WallsOk(act) && kinds.count("ModelDefinitionComponent") &&
                  KindsWithin(kinds, {"TransformDefinitionComponent", "ModelDefinitionComponent", "ModelPlatformTierComponent", "WallTransparencyComponent"})))
                continue;
            const std::string base = Base(m);
            if (NameIn(e, NotCopy(act))) continue;
            bool keep = false;
            for (const std::string& kk : KeepAct(act)) if (kk.find('/') != std::string::npos ? Has(m, kk) : Has(base, kk)) { keep = true; break; }
            if (AnyIn(NOT_, base) || !keep) continue;
            Value& p = Position(*t);
            const double dx = Float(&Need(p, "x"), 0.0) - cx, dz = Float(&Need(p, "z"), 0.0) - cz;
            if (PyHypot(dx, dz) > REACH || std::fabs(dz) > LaneAct(act)) continue;
            own.push_back(k);
        }
        std::vector<Value> added;
        int n = 0;
        for (double turn : TURNS) {
            ++n;
            const double th = Radians(turn);
            const double c = std::cos(th), s = std::sin(th);
            const Q yaw = {0.0, std::sin(th / 2), 0.0, std::cos(th / 2)};
            for (size_t k : own) {
                Value& e = Ents().a[k];
                Value& p = Position(*Transform(e));
                const double dx = Float(&Need(p, "x"), 0.0) - cx, dy = Float(Get(p, "y"), 0.0) - cy, dz = Float(&Need(p, "z"), 0.0) - cz;
                const double nx = dx * c + dz * s, nz = -dx * s + dz * c;
                const double r = PyHypot(nx, nz);
                const std::string m = ModelOf(e);
                const bool ground = AnyIn(GROUND, m);
                if (!ground) {
                    const double ahead = r > 1e-6 ? Degrees(PyAcos(PyMax(-1.0, PyMin(1.0, -nx / r)))) : 0.0;
                    if (ahead < FRONT || r < NEAR_) continue;
                }
                double lo, hi;
                SidesOf(act, &lo, &hi);
                if (!ground && !(lo <= -nz && -nz <= hi)) continue;
                double farR = FAR_;
                bool kindHit = false;
                if (act == 5) { farR = 190.0; kindHit = Has(m, "act5_vignette_mountain"); }   // FAR_ACT; FAR_KINDS = ()
                const double kf = (r > farR && kindHit) ? farR / r : 1.0;
                Value x = e;
                {
                    Value& comps = Need(x, "components");
                    std::vector<Value> kept;
                    for (Value& cc : comps.a) if (!IsType(cc, "AudioEmitterComponent") && !IsType(cc, "WallTransparencyComponent")) kept.push_back(std::move(cc));
                    comps.a = std::move(kept);
                }
                const std::string tag = std::string(COPY) + std::to_string((int)turn);
                Set(x, "name", MakeStr(StrOr(Get(e, "name"), "entity") + tag));
                const uint32_t nid = Fresh((uint64_t)Int(Get(e, "id"), 0) * 2654435761ull + 0x9E3779B9ull * (uint64_t)n, ids);
                Set(x, "id", MakeInt(nid));
                Value* t = Transform(x);
                const double su = SinkUnder(act);
                Set(*t, "position", Vec3(cx + nx * kf, cy + dy * kf - (ground ? (SINK * n + su) : su), cz + nz * kf));
                const Q q = OrientationOr(*t);
                Set(*t, "orientation", QVal(QMul(yaw, q)));
                if (kf != 1.0) {
                    const Value* scv = Get(*t, "scale");
                    double sc[3] = {1.0, 1.0, 1.0};
                    if (Truthy(scv)) { sc[0] = Float(Get(*scv, "x"), 1.0); sc[1] = Float(Get(*scv, "y"), 1.0); sc[2] = Float(Get(*scv, "z"), 1.0); }
                    Set(*t, "scale", Vec3(sc[0] * kf, sc[1] * kf, sc[2] * kf));
                }
                for (Value& comp : Need(x, "components").a)
                    if (Truthy(Get(comp, "name")) && &comp != t) Set(comp, "name", MakeStr(Str(*Get(comp, "name")) + tag));
                Set(*t, "name", MakeStr(StrOr(Get(*t, "name"), "transform") + tag));
                added.push_back(std::move(x));
            }
        }
        const size_t na = added.size();
        Extend(added);
        return {own.size(), na};
    }

    // forest(preset, act, ids)
    size_t Forest(int act) {
        if (act != 1) return 0;
        PyRandom rnd(1000 + act);
        const Cam& cam = CAMERAS[act];
        std::vector<Value> added;
        int ring = 0;
        for (const auto& rc : FOREST_RINGS) {
            const double radius = rc.first;
            const int count = rc.second;
            const double step = 2.0 * FOREST_ARC / count;
            for (int i = 0; i < count; ++i) {
                const double behind = -FOREST_ARC + step * (i + 0.5) + rnd.Uniform(-1, 1) * FOREST_JITTER * step;
                const double r = PyMax(FOREST_NEAR, radius + rnd.Uniform(-1, 1) * FOREST_JITTER * Radians(step) * radius);
                const double a = Radians(180.0 + behind);
                const double x = cam.x - r * std::cos(a), z = cam.z - r * std::sin(a);
                const double yaw = rnd.Uniform(0.0, 2.0 * kPi);
                const double sc = rnd.Uniform(FOREST_SCALE[0], FOREST_SCALE[1]);
                const std::string model = FOREST_TREES[(ring + i) % 2];
                const std::string name = "pcvr_forest_" + std::to_string(act) + "_" + std::to_string(ring) + "_" + std::to_string(i);
                const uint32_t nid = NewId(rnd, ids);
                added.push_back(Tree(name, nid, model, x, FOREST_Y, z, yaw, sc, sc, sc));
            }
            ++ring;
        }
        const size_t n = added.size();
        Extend(added);
        Value& deps = SetDefault(SetDefault(preset, "dependencies", MakeObj()), "models", MakeArr());
        std::set<std::string> have;
        for (Value& d : deps.a) if (d.kind == Value::Object) have.insert(Lower(StrOr(Get(d, "path"), "")));
        for (const char* m : FOREST_TREES) {
            if (!have.count(Lower(m))) {
                Value p = MakeObj();
                Set(p, "path", MakeStr(m));
                deps.a.push_back(std::move(p));
            }
        }
        return n;
    }

    // darkwood(preset, act, ids)
    size_t Darkwood(int act) {
        if (act != 1) return 0;
        PyRandom rnd(2000 + act);
        const Cam& cam = CAMERAS[act];
        const double cx = cam.x, cz = cam.z;
        const Strs& clear = Clear(act);
        std::vector<std::pair<double, double>> placed;
        const std::string pre = "pcvr_forest_" + std::to_string(act) + "_";
        for (Value& e : Ents().a)
            if (Starts(NameStr(e), pre.c_str())) {
                Value* t = Transform(e);
                if (!t) throw Bad("a forest tree without its transform");
                placed.push_back({Float(&Need(Position(*t), "x"), 0.0), Float(&Need(Position(*t), "z"), 0.0)});
            }
        struct Mine { double x, z, r; };
        std::vector<Mine> mine;
        std::vector<Value> added;
        int tries = 0;
        while (added.size() < DARK_COUNT && tries < 200000) {
            ++tries;
            double x, z;
            if (!mine.empty() && rnd.Random() < DARK_CLUMP) {
                const Mine b = mine[rnd.RandBelow(mine.size())];
                const double gap = DARK_GAP[0] + (DARK_GAP[1] - DARK_GAP[0]) * (b.r - DARK_NEAR) / (DARK_FAR - DARK_NEAR);
                const double a = rnd.Uniform(0, 2 * kPi);
                const double d = rnd.Uniform(1.0, 1.6) * gap;
                x = b.x + d * std::cos(a);
                z = b.z + d * std::sin(a);
            } else {
                const double r = DARK_NEAR + (DARK_FAR - DARK_NEAR) * PySqrt(rnd.Random());
                const double a = Radians(180.0 + rnd.Uniform(-DARK_ARC, DARK_ARC));
                x = cx - r * std::cos(a);
                z = cz - r * std::sin(a);
            }
            const double dx = x - cx, dz = z - cz;
            const double r = PyHypot(dx, dz);
            if (!(DARK_NEAR <= r && r <= DARK_FAR) || Degrees(PyAcos(PyMax(-1.0, PyMin(1.0, -dx / r)))) < 180.0 - DARK_ARC) continue;
            if (In(clear, Field(act, x, z))) continue;
            const double gap = DARK_GAP[0] + (DARK_GAP[1] - DARK_GAP[0]) * (r - DARK_NEAR) / (DARK_FAR - DARK_NEAR);
            bool close = false;
            for (const auto& pp : placed) if (PyHypot(x - pp.first, z - pp.second) < gap) { close = true; break; }
            if (close) continue;
            placed.push_back({x, z});
            mine.push_back({x, z, r});
            const double sc = rnd.Uniform(DARK_SCALE[0], DARK_SCALE[1]);
            const std::string name = "pcvr_darkwood_" + std::to_string(act) + "_" + std::to_string(added.size());
            const uint32_t nid = NewId(rnd, ids);
            const std::string& model = DARK_TREES[rnd.RandBelow(DARK_TREES.size())];
            const double yaw = rnd.Uniform(0.0, 2.0 * kPi);
            added.push_back(Tree(name, nid, model, x, FOREST_Y, z, yaw, sc, sc * DARK_TALL, sc));
        }
        const size_t n = added.size();
        Extend(added);
        Depend(DARK_TREES);
        return n;
    }

    static bool TreeModel(Value& e) { return Has(Base(ModelOf(e)), "tree"); }

    // thicket(preset, act, ids)
    size_t Thicket(int act) {
        if (act != 1) return 0;
        size_t total = 0;
        int n = 0;
        for (const Thick& g : THICK1) total += ThicketGroup(act, g.fields, g.count, (unsigned)(4000 + act + 100 * n++));
        return total;
    }
    size_t ThicketGroup(int act, const Strs& fields, size_t count, unsigned seed) {
        PyRandom rnd(seed);
        const Cam& cam = CAMERAS[act];
        const double cx = cam.x, cz = cam.z;
        RemoveIf([&](Value& e, size_t) {
            const std::string n = NameStr(e);
            if (!(Has(n, COPY) || Starts(n, "pcvr_forest_"))) return false;
            Value* t = Transform(e);
            if (!TruthyT(t) || !TreeModel(e)) return false;
            return In(fields, FieldOf(act, *t));
        });
        int minC = 1 << 30, maxC = -(1 << 30), minR = 1 << 30, maxR = -(1 << 30);
        for (const std::string& f : fields) {
            const int c = f[0] - 65, r = atoi(f.c_str() + 1);
            minC = std::min(minC, c); maxC = std::max(maxC, c); minR = std::min(minR, r); maxR = std::max(maxR, r);
        }
        std::vector<std::pair<double, double>> placed;
        for (Value& e : Ents().a) {
            Value* t = Transform(e);
            if (TruthyT(t) && TreeModel(e)) placed.push_back({PX(*t), PZ(*t)});
        }
        std::vector<Value> added;
        int tries = 0;
        while (added.size() < count && tries < 200000) {
            ++tries;
            const double right = GRID_LEFT + GRID * rnd.Uniform(minC, maxC + 1);
            const double ahead = GRID_FRONT - GRID * (rnd.Uniform(minR, maxR + 1) - 1);
            const double x = cx - ahead, z = cz - right;
            if (!In(fields, Field(act, x, z)) || PyHypot(x - cx, z - cz) < THICK_NEAR) continue;
            bool close = false;
            for (const auto& pp : placed) if (PyHypot(x - pp.first, z - pp.second) < THICK_GAP) { close = true; break; }
            if (close) continue;
            placed.push_back({x, z});
            const double sc = rnd.Uniform(DARK_SCALE[0], DARK_SCALE[1]);
            const std::string name = "pcvr_thicket_" + std::to_string(act) + "_" + std::to_string(seed) + "_" + std::to_string(added.size());
            const uint32_t nid = NewId(rnd, ids);
            const std::string& model = DARK_TREES[rnd.RandBelow(DARK_TREES.size())];
            const double yaw = rnd.Uniform(0.0, 2.0 * kPi);
            added.push_back(Tree(name, nid, model, x, FOREST_Y, z, yaw, sc, sc * DARK_TALL, sc));
        }
        const size_t n = added.size();
        Extend(added);
        Depend(DARK_TREES);
        return n;
    }

    // grass(preset, act, ids)
    size_t Grass(int act) {
        static const Strs none;
        GrassGroup(act, act == 1 ? GRASS1 : none, GRASS_MODELS, (unsigned)(5000 + act), "pcvr_grass");
        if (act == 1) {
            int k = 0;
            for (const GrassMore& g : GRASS_MORE1) {
                GrassGroup(act, g.fields, g.models, g.seed, "pcvr_grass" + std::to_string(k + 2));
                ++k;
            }
        }
        const Strs& drop = act == 1 ? GRASS_DROP1 : none;
        const Strs& cdrop = act == 1 ? FOLIAGE_DROP1 : none;
        static const Strs kinds = {"grass", "weeds", "bush", "fern", "pebbles", "grunge"};
        RemoveIf([&](Value& e, size_t) {
            const std::string n = NameStr(e);
            Value* t = Transform(e);
            if (!TruthyT(t)) return false;
            const std::string f = FieldOf(act, *t);
            if (Starts(n, "pcvr_grass")) return In(drop, f);
            const std::string base = Base(ModelOf(e));
            return Has(n, COPY) && In(cdrop, f) && AnyIn(kinds, base);
        });
        size_t count = 0;
        for (Value& e : Ents().a) if (Starts(NameStr(e), "pcvr_grass")) ++count;
        return count;
    }
    size_t GrassGroup(int act, const Strs& fieldList, const Strs& models, unsigned seed, const std::string& prefix) {
        PyRandom rnd(seed);
        const Cam& cam = CAMERAS[act];
        const std::set<std::string> fields(fieldList.begin(), fieldList.end());   // sorted(set(fields))
        std::vector<Value> added;
        const int steps = (int)PyInt(GRID / GRASS_STEP);
        for (const std::string& f : fields) {
            const int col = f[0] - 65, row = atoi(f.c_str() + 1);
            for (int i = 0; i < steps; ++i) {
                for (int j = 0; j < steps; ++j) {
                    const double right = GRID_LEFT + GRID * col + GRASS_STEP * (i + rnd.Uniform(0.1, 0.9));
                    const double ahead = GRID_FRONT - GRID * (row - 1) - GRASS_STEP * (j + rnd.Uniform(0.1, 0.9));
                    if (PyHypot(right, ahead) < GRASS_NEAR) continue;
                    const double sc = rnd.Uniform(GRASS_SCALE[0], GRASS_SCALE[1]);
                    const std::string name = prefix + "_" + std::to_string(act) + "_" + std::to_string(added.size());
                    const uint32_t nid = NewId(rnd, ids);
                    const std::string& model = models[rnd.RandBelow(models.size())];
                    const double y = rnd.Uniform(-0.5, 0.0);
                    const double yaw = rnd.Uniform(0.0, 2.0 * kPi);
                    Value g = Tree(name, nid, model, cam.x - ahead, y, cam.z - right, yaw, sc, sc, sc);
                    Set(Need(g, "components").a[1], "lightMask", MakeInt(19));
                    added.push_back(std::move(g));
                }
            }
        }
        const size_t n = added.size();
        Extend(added);
        Depend(models);
        return n;
    }

    // thin(preset, act)
    size_t ThinOut(int act) {
        if (act != 1) return 0;
        size_t total = 0;
        for (const Thin& g : THIN1) total += ThinGroup(act, g.fields, g.n);
        return total;
    }
    size_t ThinGroup(int act, const Strs& fields, size_t n) {
        std::vector<std::pair<double, size_t>> ours;   // (-x, index), stable by the entities' order
        std::vector<Value>& ents = Ents().a;
        for (size_t k = 0; k < ents.size(); ++k) {
            Value& e = ents[k];
            Value* t = Transform(e);
            if (!TruthyT(t) || !TreeModel(e)) continue;
            const std::string nm = NameStr(e);
            if (!(Has(nm, COPY) || Starts(nm, "pcvr_"))) continue;
            if (!In(fields, FieldOf(act, *t))) continue;
            ours.push_back({-PX(*t), k});
        }
        std::stable_sort(ours.begin(), ours.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::unordered_set<size_t> out;
        for (size_t k = 0; k < ours.size(); ++k) if (k % n == 0) out.insert(ours[k].second);
        RemoveIf([&](Value&, size_t k) { return out.count(k) > 0; });
        return out.size();
    }

    static bool Ours(const Value& e) {
        const std::string n = NameStr(e);
        return Has(n, COPY) || Starts(n, "pcvr_");
    }

    // finish(preset, act, ids)
    size_t Finish(int act) {
        static const Strs none;
        static const Strs kinds = {"grass", "weeds", "bush", "fern", "pebbles", "grunge"};   // FOLIAGE_KINDS
        const Strs& half = act == 1 ? FOLIAGE_HALF1 : none;
        struct Plant { double nx, z; size_t k; };
        std::vector<Plant> plants;
        std::vector<Value>& ents = Ents().a;
        for (size_t k = 0; k < ents.size(); ++k) {
            Value& e = ents[k];
            if (!Ours(e)) continue;
            Value* t = Transform(e);
            const std::string at = TruthyT(t) ? FieldOf(act, *t) : std::string();
            if (!In(half, at) || !AnyIn(kinds, Base(ModelOf(e)))) continue;
            plants.push_back({-PX(*t), PZ(*t), k});
        }
        std::stable_sort(plants.begin(), plants.end(), [](const Plant& a, const Plant& b) {
            if (a.nx < b.nx) return true;
            if (b.nx < a.nx) return false;
            return a.z < b.z;
        });
        std::unordered_set<size_t> out;
        for (size_t k = 0; k < plants.size(); ++k) if (k % 2 == 0) out.insert(plants[k].k);
        // TREES_OUT = {}: none of ours out by field (the script still looks at each of ours: at(e) of each)
        for (size_t k = 0; k < ents.size(); ++k) {
            Value& e = ents[k];
            if (!Ours(e)) continue;
            if (Value* t = Transform(e)) if (TruthyT(t)) (void)FieldOf(act, *t);
        }
        RemoveIf([&](Value&, size_t k) { return out.count(k) > 0; });
        PyRandom rnd(6000 + act);
        const Cam& cam = CAMERAS[act];
        if (act == 1) {
            int k = 0;
            for (const TreeAt& ta : TREES_AT1) {
                const double off0 = ta.hasOff ? ta.off[0] : 0.0, off1 = ta.hasOff ? ta.off[1] : 0.0;
                const double sc = ta.hasSize ? ta.size : 1.0;
                const double right = GRID_LEFT + GRID * (ta.field[0] - 65) + GRID / 2 + off0;
                const double ahead = GRID_FRONT - GRID * (atoi(ta.field + 1) - 1) - GRID / 2 + off1;
                const uint32_t nid = NewId(rnd, ids);
                const double yaw = rnd.Uniform(0.0, 2.0 * kPi);
                Ents().a.push_back(Tree("pcvr_tree_at_" + std::to_string(act) + "_" + std::to_string(k), nid, ta.model, cam.x - ahead, FOREST_Y,
                                        cam.z - right, yaw, sc, sc * DARK_TALL, sc));
                Depend(Strs{ta.model});
                ++k;
            }
        }
        return out.size();
    }

    // clear_fields(preset, act)
    size_t ClearFields(int act) {
        const Strs& clear = Clear(act);
        return RemoveIf([&](Value& e, size_t) {
            const std::string n = NameStr(e);
            Value* t = Transform(e);
            const bool ours = Has(n, COPY) || Starts(n, "pcvr_forest_") || Starts(n, "pcvr_darkwood_");
            return ours && TruthyT(t) && TreeModel(e) && In(clear, FieldOf(act, *t));
        });
    }

    // ivy_out(preset, act)
    size_t IvyOut(int act) {
        if (act != 1) return 0;
        const Strs& fields = IVY_FIELDS1;
        std::vector<std::pair<double, double>> trees;
        for (Value& e : Ents().a) {
            Value* t = Transform(e);
            if (TruthyT(t) && TreeModel(e)) trees.push_back({PX(*t), PZ(*t)});
        }
        return RemoveIf([&](Value& e, size_t) {
            Value* t = Transform(e);
            if (Has(NameStr(e), COPY) && TruthyT(t) && Has(Base(ModelOf(e)), "ivy")) {
                const double x = PX(*t), z = PZ(*t);
                if (In(fields, Field(act, x, z))) return true;
                bool nearTree = false;
                for (const auto& tr : trees) if (PyHypot(x - tr.first, z - tr.second) < IVY_TREE) { nearTree = true; break; }
                if (!nearTree) return true;
            }
            return false;
        });
    }

    // walls(preset, act, ids)
    size_t Walls(int act) {
        if (act != 2) return 0;
        PyRandom rnd(8000 + act);
        const std::string first = "ui_wall01_01", second = "ui_wall01_02", cheap = "ui_wall01_cheap_05", rightOf = "ui_wall01_03";
        const int rightN = 9, leftN = 4;
        const double lower = 0.8;
        const size_t iFirst = ByName(first), iSecond = ByName(second);
        double ax, az, bx, bz;
        {
            Value& a = Position(*NeedT(Ents().a[iFirst]));
            Value& b = Position(*NeedT(Ents().a[iSecond]));
            ax = Float(&Need(a, "x"), 0.0); az = Float(&Need(a, "z"), 0.0);
            bx = Float(&Need(b, "x"), 0.0); bz = Float(&Need(b, "z"), 0.0);
        }
        const double step[2] = {bx - ax, bz - az};
        std::vector<Value> added;
        auto put = [&](size_t src, double x, double y, double z, const std::string& k) {
            Value w = Ents().a[iFirst];
            const std::string name = "pcvr_wall_" + std::to_string(act) + "_" + k;
            const uint32_t nid = NewId(rnd, ids);
            Set(w, "name", MakeStr(name));
            Set(w, "id", MakeInt(nid));
            for (Value& c : Need(w, "components").a) Set(c, "name", MakeStr(name + "_" + StrOr(Get(c, "type"), "c")));
            Value* t = NeedT(w);
            Set(*t, "position", Vec3(x, y, z));
            Value o = Need(*NeedT(Ents().a[src]), "orientation");
            if (o.kind != Value::Object) throw Bad("an orientation that is no dict");
            Set(*t, "orientation", o);
            added.push_back(std::move(w));
        };
        const size_t iRight = ByName(rightOf);
        double lx, ly, lz;
        {
            Value& last = Position(*NeedT(Ents().a[iRight]));
            lx = Float(&Need(last, "x"), 0.0); ly = Float(&Need(last, "y"), 0.0); lz = Float(&Need(last, "z"), 0.0);
        }
        for (int k = 1; k <= rightN; ++k) put(iRight, lx + step[0] * k, ly, lz + step[1] * k, "r" + std::to_string(k));
        const size_t iCheap = ByName(cheap);
        double px, py, pz;
        {
            Value& cp = Position(*NeedT(Ents().a[iCheap]));
            px = Float(&Need(cp, "x"), 0.0); py = Float(&Need(cp, "y"), 0.0); pz = Float(&Need(cp, "z"), 0.0);
        }
        for (int k = 0; k <= leftN; ++k) put(iCheap, px - step[0] * k, py - lower, pz - step[1] * k, "l" + std::to_string(k));
        RemoveIf([&](Value&, size_t k) { return k == iCheap; });
        const size_t n = added.size();
        Extend(added);
        return n;
    }
    static Value* NeedT(Value& e) {   // transform(e)[...]: None is a TypeError
        Value* t = Transform(e);
        if (!t) throw Bad("an entity without its transform");
        return t;
    }

    // road(preset, act, ids)
    size_t Road(int act) {
        if (act != 2) return 0;
        PyRandom rnd(9000 + act);   // (used only by the road's own laying, which TILES skips)
        (void)rnd;
        std::vector<std::pair<double, double>> pos;
        for (const std::string& n : ROAD_RUN2) {
            Value* t = NeedT(Ents().a[ByName(n)]);
            pos.push_back({PX(*t), PZ(*t)});
        }
        const double x0 = pos.front().first, z0 = pos.front().second, x1 = pos.back().first, z1 = pos.back().second;
        const double ln = PyHypot(x1 - x0, z1 - z0);
        if (ln == 0.0) throw Bad("float division by zero");
        const double d0 = (x1 - x0) / ln, d1 = (z1 - z0) / ln;
        roadLine[act] = {x0, z0, d0, d1};
        RemoveIf([&](Value& e, size_t) { return NameIn(e, ROAD_ENDS2); });
        // act in TILES: the strips (tiles) bring the road on - the run laid again and again behind and ROAD_PALMS are never reached
        return 0;
    }

    // tiles(preset, act, ids, sources)
    size_t Tiles(int act) {
        if (act != 2 || !roadLine.count(act)) return 0;
        const auto src = sources.find(TILES2);
        if (src == sources.end()) throw Bad(std::string("KeyError: '") + TILES2 + "'");
        Value tile = ParseJson(src->second);
        PyRandom rnd(9500 + act);
        const Cam& cam = CAMERAS[act];
        const double cx = cam.x, cz = cam.z;
        const Line L = roadLine[act];
        const double x0 = L.x0, z0 = L.z0;
        const double d[2] = {L.d0, L.d1};
        if (d[0] == 0.0) throw Bad("float division by zero");
        const double t0 = (cx + TILE_START - x0) / d[0];
        (void)t0;
        const double phi0 = PyAtan2(-d[1], d[0]);
        std::vector<std::pair<double, double>> pts;
        pts.push_back({cx - 90.0, cz + 3.0});
        for (const auto& ra : roadPath[act]) pts.push_back({cx - ra.second, cz - ra.first});
        {
            const auto f = pts.front(), b = pts.back();
            pts.insert(pts.begin(), f);
            pts.push_back(b);
        }
        std::vector<std::pair<double, double>> line;
        for (size_t i = 1; i + 2 < pts.size(); ++i) {
            const auto &p0 = pts[i - 1], &p1 = pts[i], &p2 = pts[i + 1], &p3 = pts[i + 2];
            for (int k = 0; k < 40; ++k) {
                const double t_ = k / 40.0;
                const double t2 = std::pow(t_, 2.0), t3 = std::pow(t_, 3.0);
                auto cr = [&](double a0, double a1, double a2, double a3) {
                    return 0.5 * (2 * a1 + (-a0 + a2) * t_ + (2 * a0 - 5 * a1 + 4 * a2 - a3) * t2 + (-a0 + 3 * a1 - 3 * a2 + a3) * t3);
                };
                line.push_back({cr(p0.first, p1.first, p2.first, p3.first), cr(p0.second, p1.second, p2.second, p3.second)});
            }
        }
        line.push_back(pts[pts.size() - 2]);
        std::vector<double> along = {0.0};
        for (size_t i = 1; i < line.size(); ++i)
            along.push_back(along.back() + PyHypot(line[i].first - line[i - 1].first, line[i].second - line[i - 1].second));
        bool found = false;
        double zero = 0.0;
        for (size_t i = 0; i < line.size(); ++i) if (line[i].first >= cx) { zero = along[i]; found = true; break; }
        if (!found) throw Bad("StopIteration: the road never comes level with the camera");
        for (double& a_ : along) a_ = a_ - zero;
        auto seg = [&](double s) {
            size_t lo = 0, hi = along.size() - 1;
            while (hi - lo > 1) {
                const size_t m_ = (lo + hi) / 2;
                if (along[m_] <= s) lo = m_;
                else hi = m_;
            }
            return lo;
        };
        auto phi = [&](double s) {
            const size_t i = seg(s);
            return PyAtan2(-(line[i + 1].second - line[i].second), line[i + 1].first - line[i].first);
        };
        auto midAt = [&](double s) {
            const size_t i = seg(s);
            const double w = (s - along[i]) / PyMax(1e-6, along[i + 1] - along[i]);
            return std::make_pair(line[i].first + (line[i + 1].first - line[i].first) * w, line[i].second + (line[i + 1].second - line[i].second) * w);
        };
        (void)midAt(0.0);   // sx, sz = mid_at(0.0) (not used further)
        struct Warp { double x, z, by; };
        auto warp = [&](double s, double t) {
            const auto m = midAt(s);
            const double a_ = phi(s);
            return Warp{m.first - std::sin(a_) * t, m.second - std::cos(a_) * t, a_ - phi0};
        };
        auto turned = [&](const Value& q, double by) { return QVal(QMul(YawQ(by), QOf(q))); };
        const double n0[2] = {-std::sin(phi0), -std::cos(phi0)};
        std::vector<Value> added;
        std::set<std::string> models, particles;
        const double farS = ROAD_FAR2;

        // 1. the cobbles: the vignette's ground, on along the road (ROAD_GROUND_COUNT of them)
        size_t gi = 0;
        {
            bool have = false;
            std::vector<Value>& ents = Ents().a;
            for (size_t k = 0; k < ents.size(); ++k) if (NameIs(ents[k], ROAD_GROUND_NAME)) { gi = k; have = true; break; }
            if (!have) throw Bad(std::string("StopIteration: no ") + ROAD_GROUND_NAME);
        }
        double gpx, gpy, gpz;
        {
            Value& gp = Position(*NeedT(Ents().a[gi]));
            gpx = Float(&Need(gp, "x"), 0.0); gpy = Float(&Need(gp, "y"), 0.0); gpz = Float(&Need(gp, "z"), 0.0);
        }
        const double go[2] = {gpx - x0, gpz - z0};
        const double gt = go[0] * n0[0] + go[1] * n0[1] - ROAD_COBBLES_OFF;
        const double gs = -(cx - gpx);
        const double every = RG_EVERY, lower = RG_LOWER;
        auto lay = [&](const char* kind, int j, double s, double scale, double y) {
            const Warp w = warp(s, std::string(kind) == "wide" ? gt * scale : gt);
            if (cz - w.z > ROAD_SIDES[1] + 40.0) return;
            Value g = Ents().a[gi];
            const std::string name = std::string("pcvr_road") + kind + "_" + std::to_string(act) + "_" + std::to_string(j);
            const uint32_t nid = NewId(rnd, ids);
            Set(g, "name", MakeStr(name));
            Set(g, "id", MakeInt(nid));
            RenameComponents(g, name);
            Value* tt = NeedT(g);
            Set(*tt, "position", Vec3(w.x, y, w.z));
            Set(*tt, "orientation", turned(Need(*NeedT(Ents().a[gi]), "orientation"), w.by));
            Set(*tt, "scale", Vec3(scale, 1.0, scale));   // ROAD_FLAT = {}: flat is None
            added.push_back(std::move(g));
        };
        for (int j = 1; gs + every * j <= farS && j <= RG_COUNT; ++j) lay("cobbles", j, gs + every * j, 1.0, gpy - lower * j);
        // the wide ground: wide_every 0 - none (RG_WIDE, RG_WIDE_LOWER unused)
        (void)RG_WIDE; (void)RG_WIDE_EVERY; (void)RG_WIDE_LOWER;

        // 2. the strip's curbs (TILE_ONLY), along the same road
        std::vector<Value> more;
        Value* tileEnts = Get(tile, "entities");
        static std::vector<Value> noEnts;
        std::vector<Value>& tents = tileEnts ? (tileEnts->kind == Value::Array ? tileEnts->a : throw Bad("entities is no list")) : noEnts;
        for (Value& e : tents) {
            Value* tr = Transform(e);
            const std::string m = ModelOf(e);
            if (TruthyT(tr) && Has(m, "stone_trims/trim_") && TILE_CURBS2[0] <= PZ(*tr) && PZ(*tr) <= TILE_CURBS2[1] &&
                PX(*tr) + TILE_CURBS2[2] < TILE_CURBS2[3]) {
                Value f = e;
                Set(f, "name", MakeStr(StrOr(Get(e, "name"), "trim") + "_fill"));
                Set(Position(*NeedT(f)), "x", PX(*tr) + TILE_CURBS2[2]);
                more.push_back(std::move(f));
            }
        }
        const double narrow = roadNarrow.count(act) ? roadNarrow[act] : 1.0;
        for (int k = std::max(TILE_FIRST, 0); k < TILE_FIRST + TILE_COUNT; ++k) {
            for (int pass = 0; pass < 2; ++pass) {
                std::vector<Value>& list = pass == 0 ? tents : more;
                for (Value& e : list) {
                    const std::set<std::string> kinds = Kinds(e);
                    if (!(kinds.count("ModelDefinitionComponent") || kinds.count("VfxDefinitionComponent")) || kinds.count("TerrainDefinitionComponent")) continue;
                    const std::string m = ModelOf(e);
                    if (!Has(m, TILE_ONLY2)) continue;
                    Value* tr = Transform(e);
                    if (!TruthyT(tr)) continue;
                    Value& p = Position(*tr);
                    const double sa = TILE_WIDTH * (k + 1) - Float(&Need(p, "x"), 0.0);
                    double ta = Float(&Need(p, "z"), 0.0) - TILE_MID;
                    if (Has(m, "stone_trims/trim_") && std::fabs(ta) < ROAD_HALF + 8.0) ta *= narrow;
                    ta += ROAD_SHIFT2;
                    if (sa > farS) continue;
                    const Warp w = warp(sa, ta);
                    const double right = cz - w.z, ahead = cx - w.x;
                    if ((ahead > -8.0 && std::fabs(right) < 80.0) || ahead > 60.0) continue;
                    if (!(ROAD_SIDES[0] <= right && right <= ROAD_SIDES[1])) continue;
                    Value n = e;
                    {
                        Value& comps = Need(n, "components");
                        std::vector<Value> kept;
                        for (Value& c : comps.a) {
                            bool keep = false;
                            for (const char* kk : TILE_KEEP) if (IsType(c, kk)) { keep = true; break; }
                            if (keep) kept.push_back(std::move(c));
                        }
                        comps.a = std::move(kept);
                    }
                    const std::string name = "pcvr_tile_" + std::to_string(act) + "_" + std::to_string(k) + "_" + StrOr(Get(e, "name"), "e");
                    const uint32_t nid = NewId(rnd, ids);
                    Set(n, "name", MakeStr(name));
                    Set(n, "id", MakeInt(nid));
                    for (Value& comp : Need(n, "components").a) {
                        Set(comp, "name", MakeStr(name + "_" + StrOr(Get(comp, "type"), "c")));
                        if (IsType(comp, "ModelDefinitionComponent")) models.insert(FileName(comp));
                        if (IsType(comp, "VfxDefinitionComponent")) particles.insert(FileName(comp));
                    }
                    Value* tt = NeedT(n);
                    const double lift = Has(m, "stone_trims/trim_") ? CURB_LIFT2 : 0.0;
                    Set(*tt, "position", Vec3(w.x, Float(Get(p, "y"), 0.0) + TILE_HIGH + lift, w.z));
                    const Q q = OrientationOr(*tt);
                    Set(*tt, "orientation", QVal(QMul(YawQ(phi0 + kPi + w.by), q)));
                    added.push_back(std::move(n));
                }
            }
        }
        // 3. CURB_GRASS = {}: no grass by the curbs; two palms near behind
        auto groundY = [&](double s) { return gpy - lower * PyMax(0.0, (s - gs) / every) - 0.15; };
        const double sh = ROAD_SHIFT2;
        int n = 0;
        for (const SidePalm& sp : ROAD_SIDE_PALMS2) {
            const Warp w = warp(sp.along, sh + sp.side);
            const std::string m = A2_PALMS[n % 2];
            const uint32_t nid = NewId(rnd, ids);
            const double y = groundY(sp.along) - 0.2;
            const double yaw = rnd.Uniform(0, 2 * kPi);
            added.push_back(Tree("pcvr_roadpalm_" + std::to_string(act) + "_" + std::to_string(n), nid, m, w.x, y, w.z, yaw, sp.size, sp.size, sp.size));
            models.insert(m);
            ++n;
        }
        const size_t na = added.size();
        Extend(added);
        Depend(Strs(models.begin(), models.end()));   // sorted(models)
        Value& deps = SetDefault(SetDefault(preset, "dependencies", MakeObj()), "particles", MakeArr());
        std::set<std::string> have;
        for (Value& x : deps.a) if (x.kind == Value::Object) have.insert(Lower(StrOr(Get(x, "path"), "")));
        for (const std::string& f : particles) {
            if (!have.count(Lower(f))) {
                Value p = MakeObj();
                Set(p, "path", MakeStr(f));
                deps.a.push_back(std::move(p));
            }
        }
        return na;
    }
    static std::string FileName(const Value& comp) {   // comp['filename'] as a set member (a str; sorted with str)
        const Value& f = Need(comp, "filename");
        if (f.kind != Value::String) throw Bad("a file name that is no string");
        return f.s;
    }

    // towers(preset, act, ids)
    size_t Towers(int act) {
        PyRandom rnd(9700 + act);
        const Cam& cam = CAMERAS[act];
        size_t out = 0;
        if (act == 2) {
            int k = 0;
            for (const Tower& t : TOWERS2) {
                const uint32_t nid = NewId(rnd, ids);
                Ents().a.push_back(Tree("pcvr_tower_" + std::to_string(act) + "_" + std::to_string(k), nid, t.model, cam.x - t.ahead, t.y, cam.z - t.right,
                                        Radians(200.0), t.size, t.size, t.size));
                Depend(Strs{t.model});
                ++out;
                ++k;
            }
            k = 0;
            for (const Piece& pc : SET_PIECES2) {
                const uint32_t nid = NewId(rnd, ids);
                Ents().a.push_back(Tree("pcvr_piece_" + std::to_string(act) + "_" + std::to_string(k), nid, pc.model, cam.x - pc.ahead, pc.y, cam.z - pc.right,
                                        Radians(pc.turn), pc.size, pc.size, pc.size));
                Depend(Strs{pc.model});
                ++out;
                ++k;
            }
            for (Value& e : Ents().a) {
                const Value* nm = Get(e, "name");
                double dy = 0.0;
                if (nm && nm->kind == Value::String) for (const Nudge& nd : NUDGE2) if (nm->s == nd.name) dy = nd.dy;
                Value* t = Transform(e);
                if (dy != 0.0 && TruthyT(t)) {
                    Value& p = Position(*t);
                    Set(p, "y", Float(&Need(p, "y"), 0.0) - dy);
                    ++out;
                }
            }
        }
        return out;
    }

    // town(preset, act, ids, sources)
    size_t Town(int act) {
        if (act != 3) return 0;
        const auto srcText = sources.find(TOWN3);
        if (srcText == sources.end()) throw Bad(std::string("KeyError: '") + TOWN3 + "'");
        Value src = ParseJson(srcText->second);
        const double ax = TOWN_AT[0], az = TOWN_AT[1], dy = TOWN_DY;
        const double zr0 = TOWN_ZONE[0], zr1 = TOWN_ZONE[1], za0 = TOWN_ZONE[2], za1 = TOWN_ZONE[3];
        const double reach = TOWN_REACH, lo = TOWN_LOHI[0], hi = TOWN_LOHI[1];
        PyRandom rnd(9700 + act);
        const Cam& cam = CAMERAS[act];
        const double cx = cam.x, cz = cam.z;
        const double r0 = TOWN_ALIGN3[0], phi = TOWN_ALIGN3[1];
        const double yaw = kPi / 2 - Radians(phi);
        const Q turn = YawQ(yaw);
        const double cf = std::cos(Radians(phi)), sf = std::sin(Radians(phi));
        std::vector<Value> added;
        std::set<std::string> models;
        long long water = -1;
        {
            std::vector<Value>& ents = Ents().a;
            for (size_t k = 0; k < ents.size(); ++k) if (NameIs(ents[k], TOWN_WATER3)) { water = (long long)k; break; }
        }
        if (water >= 0 && !Truthy(&Ents().a[(size_t)water])) water = -1;   // `if water`: an empty dict is none
        if (water >= 0) Set(Position(*NeedT(Ents().a[(size_t)water])), "y", TOWN_WATER_Y3);
        Value* srcEnts = Get(src, "entities");
        static std::vector<Value> none;
        std::vector<Value>& list = srcEnts ? (srcEnts->kind == Value::Array ? srcEnts->a : throw Bad("entities is no list")) : none;
        for (Value& e : list) {
            const std::set<std::string> kinds = Kinds(e);
            bool skip = Int(Get(e, "id"), 0) == TOWN_SKIP_ID || kinds.count("TerrainDefinitionComponent");
            for (const char* sk : TOWN_SKIP_KINDS) skip = skip || kinds.count(sk);
            if (skip) continue;
            if (!(kinds.count("ModelDefinitionComponent") || kinds.count("PrefabPlacementDefinitionComponent"))) continue;
            Value* tr = Transform(e);
            if (!TruthyT(tr)) continue;
            Value& p = Position(*tr);
            const double ahead = az - Float(&Need(p, "z"), 0.0), right = Float(&Need(p, "x"), 0.0) - ax;
            std::string m = ModelOf(e);
            if (m.empty()) {
                std::string prefab;
                if (Value* comps = Components(e))
                    for (Value& c : comps->a) if (IsType(c, "PrefabPlacementDefinitionComponent")) { prefab = StrOr(Get(c, "prefab"), ""); break; }
                m = Lower(prefab);
            }
            const bool always = AnyIn(TOWN_ALWAYS, m);
            if (!always && zr0 <= right && right <= zr1 && za0 <= ahead && ahead <= za1) continue;
            if (PyHypot(ahead, right) > reach || !(lo <= right && right <= hi)) continue;
            if (Float(Get(p, "y"), 0.0) > 30.0) continue;
            Value n = e;
            {
                Value& comps = Need(n, "components");
                std::vector<Value> kept;
                for (Value& c : comps.a) {
                    bool keep = false;
                    for (const char* kk : TOWN_KEEP) if (IsType(c, kk)) { keep = true; break; }
                    if (keep) kept.push_back(std::move(c));
                }
                comps.a = std::move(kept);
            }
            const std::string name = "pcvr_town_" + std::to_string(act) + "_" + StrOr(Get(e, "name"), "e");
            const uint32_t nid = NewId(rnd, ids);   // (taken even when the piece is left out below, as the script)
            Set(n, "name", MakeStr(name));
            Set(n, "id", MakeInt(nid));
            for (Value& c : Need(n, "components").a) {
                Set(c, "name", MakeStr(name + "_" + StrOr(Get(c, "type"), "c")));
                if (IsType(c, "ModelDefinitionComponent")) models.insert(FileName(c));
            }
            Value* t = NeedT(n);
            double y = Float(Get(p, "y"), 0.0) + dy;
            if (water >= 0 && AnyIn(TOWN_FLOATING, m)) y += Float(&Need(Position(*NeedT(Ents().a[(size_t)water])), "y"), 0.0) - (TOWN_WATERLINE3 + dy);
            const double a2 = ahead * cf - right * sf, r2 = ahead * sf + right * cf + r0;
            if (r2 > TOWN_RIGHT_MAX3[0]) continue;
            Set(*t, "position", Vec3(cx - a2, y, cz - r2));
            const Q q = OrientationOr(*t);
            Set(*t, "orientation", QVal(QMul(turn, q)));
            added.push_back(std::move(n));
        }
        // the water: the menu's own, tiled round you over all of it
        size_t nw = 0;
        double sx = 0.0, sz = 0.0;
        if (water >= 0) {
            Value& wv = Ents().a[(size_t)water];
            const std::string wm = ModelOf(wv);
            const auto ext = waterExtent.find(wm);
            const double x0 = ext != waterExtent.end() ? ext->second[0] : WATER_GUESS[0], z0 = ext != waterExtent.end() ? ext->second[1] : WATER_GUESS[1];
            const double x1 = ext != waterExtent.end() ? ext->second[2] : WATER_GUESS[2], z1 = ext != waterExtent.end() ? ext->second[3] : WATER_GUESS[3];
            Value* wt = NeedT(wv);
            Set(*wt, "orientation", QVal({0.0, 0.0, 0.0, 1.0}));
            const double wpx = Float(&Need(Position(*wt), "x"), 0.0), wpy = Float(&Need(Position(*wt), "y"), 0.0), wpz = Float(&Need(Position(*wt), "z"), 0.0);
            double sc = 1.0;
            {
                const Value* scv = Get(*wt, "scale");
                if (Truthy(scv)) sc = Float(Get(*scv, "x"), 1.0);
            }
            sx = (x1 - x0) * sc;
            sz = (z1 - z0) * sc;
            const int64_t ni = PyInt((reach + 80.0) / PyMax(sx, 1.0)) + 2, nj = PyInt((PyMax(-lo, hi) + 80.0) / PyMax(sz, 1.0)) + 2;
            const double edge = TOWN_RIGHT_MAX3[1];
            const Value waterCopy = wv;
            for (int64_t i = -ni; i <= ni; ++i) {
                for (int64_t j = -nj; j <= nj; ++j) {
                    if (i == 0 && j == 0) continue;
                    const double x = wpx + i * sx;
                    double z = wpz + j * sz;
                    const double mx = x + (x0 + x1) / 2 * sc, mz = z + (z0 + z1) / 2 * sc;
                    const double ahead = cx - mx, right = cz - mz;
                    if (PyHypot(PyMax(std::fabs(ahead) - sx / 2, 0.0), 0.0) > reach || right + sz / 2 < lo || right - sz / 2 > hi) continue;
                    if (right - sz / 2 >= edge) continue;
                    double f = 1.0;
                    if (right + sz / 2 > edge) {
                        f = (edge - (right - sz / 2)) / sz;
                        z = cz - edge - z0 * sc * f;
                    }
                    Value w = waterCopy;
                    const std::string name = "pcvr_townwater_" + std::to_string(act) + "_" + std::to_string(i) + "_" + std::to_string(j);
                    const uint32_t nid = NewId(rnd, ids);
                    Set(w, "name", MakeStr(name));
                    Set(w, "id", MakeInt(nid));
                    RenameComponents(w, name);
                    Set(*NeedT(w), "position", Vec3(x, wpy, z));
                    if (f != 1.0) {
                        const Value* scv = Get(*NeedT(w), "scale");
                        Value ws = Truthy(scv) ? *scv : Vec3(1.0, 1.0, 1.0);
                        if (ws.kind != Value::Object) throw Bad("a scale that is no dict");
                        Set(ws, "z", sc * f);
                        Set(*NeedT(w), "scale", std::move(ws));
                    }
                    added.push_back(std::move(w));
                    ++nw;
                }
            }
        }
        // our floor plates (frontend_floors.json: none here yet) - GROUND_SWAP and TOWN_PLATES lay nothing without them
        const std::map<std::string, std::string> plates = FloorNames();
        if (act == 3) {
            const std::pair<const char*, const char*> swaps[] = {{"terrain_act3_01", "act3_ground"}};
            for (const auto& sw : swaps) {
                const auto plate = plates.find(sw.second);
                if (plate == plates.end()) continue;
                for (size_t k = 0; k < Ents().a.size(); ++k) {
                    Value& e = Ents().a[k];
                    if (!NameIs(e, sw.first)) continue;
                    if (Value* comps = Components(e))
                        for (Value& c : comps->a)
                            if (IsType(c, "ModelDefinitionComponent")) { Set(c, "filename", MakeStr(plate->second)); models.insert(plate->second); }
                    Value& at = Position(*NeedT(e));
                    const double atx = Float(&Need(at, "x"), 0.0), aty = Float(&Need(at, "y"), 0.0), atz = Float(&Need(at, "z"), 0.0);
                    int pk = 0;
                    for (const Paver& pv : GroundPavers(sw.second)) {
                        const uint32_t nid = NewId(rnd, ids);
                        added.push_back(Tree("pcvr_paver_" + std::to_string(act) + "_" + std::to_string(pk++), nid, pv.model, atx + pv.x, aty + pv.y, atz + pv.z,
                                             Radians(pv.yaw), pv.scale, pv.scale, pv.scale));
                        models.insert(pv.model);
                    }
                }
            }
            struct Plate { const char* plate; double r, ah, y, yaw, sc; };
            const Plate townPlates[] = {{"act3_stone", -15.0, -70.0, 0.35, 80.0, 1.0}};
            int k = 0;
            for (const Plate& pl : townPlates) {
                const auto plate = plates.find(pl.plate);
                if (plate != plates.end()) {
                    const uint32_t nid = NewId(rnd, ids);
                    added.push_back(Tree("pcvr_floor_" + std::to_string(act) + "_" + std::to_string(k), nid, plate->second, cx - pl.ah, pl.y, cz - pl.r,
                                         Radians(pl.yaw), pl.sc, pl.sc, pl.sc));
                    models.insert(plate->second);
                }
                ++k;
            }
        }
        const size_t na = added.size();
        Extend(added);
        Depend(Strs(models.begin(), models.end()));
        // the town's textures for the folders of what came over
        std::set<std::string> folders;
        for (const std::string& m : models) {
            const size_t b = m.rfind('/');
            if (b == std::string::npos) throw Bad("IndexError: a model path without a folder");
            const size_t a = b == 0 ? std::string::npos : m.rfind('/', b - 1);
            folders.insert(a == std::string::npos ? m.substr(0, b) : m.substr(a + 1, b - a - 1));
        }
        std::vector<std::string> tex;
        if (Value* deps = Get(src, "dependencies")) {
            if (Value* tl = Get(*deps, "textures")) {
                if (tl->kind != Value::Array) throw Bad("textures is no list");
                for (Value& d : tl->a) {
                    if (d.kind != Value::Object) continue;
                    const std::string path = Lower(StrOr(Get(d, "path"), ""));
                    bool hit = false;
                    for (const std::string& f : folders) if (Has(path, "/" + f + "/") || Has(path, "/" + f + "_")) { hit = true; break; }
                    if (hit) tex.push_back(Str(Need(d, "path")));
                }
            }
        }
        Value& tdeps = SetDefault(SetDefault(preset, "dependencies", MakeObj()), "textures", MakeArr());
        std::set<std::string> have;
        for (Value& d : tdeps.a) if (d.kind == Value::Object) have.insert(Lower(StrOr(Get(d, "path"), "")));
        for (const std::string& t : tex) {
            if (!have.count(Lower(t))) {
                Value p = MakeObj();
                Set(p, "path", MakeStr(t));
                tdeps.a.push_back(std::move(p));
                have.insert(Lower(t));
            }
        }
        townReport[act] = water >= 0 ? std::to_string(na - nw) + " town pieces, " + std::to_string(nw) + " water tiles (" + Fixed(sx, 0) + " x " + Fixed(sz, 0) + ")"
                                     : std::to_string(na) + " town pieces, no water";
        return na;
    }

    // lava(preset, act, ids)
    size_t Lava(int act) {
        if (act != 4) return 0;
        PyRandom rnd(9800 + act);
        const double reach = LAVA_REACH, lo = LAVA_LOHI[0], hi = LAVA_LOHI[1];
        long long si = -1;
        {
            std::vector<Value>& ents = Ents().a;
            for (size_t k = 0; k < ents.size(); ++k) if (Lower(NameStr(ents[k])) == LAVA4) { si = (long long)k; break; }
        }
        if (si < 0 || !Truthy(&Ents().a[(size_t)si])) return 0;
        const Cam& cam = CAMERAS[act];
        const double cx = cam.x, cz = cam.z;
        Value* t0 = NeedT(Ents().a[(size_t)si]);
        const double p0x = Float(&Need(Position(*t0), "x"), 0.0), p0y = Float(&Need(Position(*t0), "y"), 0.0), p0z = Float(&Need(Position(*t0), "z"), 0.0);
        double qy = 0.0, qw = 1.0;
        {
            const Value* o = Get(*t0, "orientation");
            if (Truthy(o)) { qy = Float(Get(*o, "y"), 0.0); qw = Float(Get(*o, "w"), 1.0); }
        }
        const double yaw0 = 2.0 * PyAtan2(qy, qw);
        Set(*t0, "orientation", QVal({0.0, std::sin(yaw0 / 2), 0.0, std::cos(yaw0 / 2)}));
        const std::string sm = ModelOf(Ents().a[(size_t)si]);
        const auto ext = lavaExtent.find(sm);
        const double x0 = ext != lavaExtent.end() ? ext->second[0] : LAVA_GUESS[0], z0 = ext != lavaExtent.end() ? ext->second[1] : LAVA_GUESS[1];
        const double x1 = ext != lavaExtent.end() ? ext->second[2] : LAVA_GUESS[2], z1 = ext != lavaExtent.end() ? ext->second[3] : LAVA_GUESS[3];
        const double w = x1 - x0, d = z1 - z0;
        const double c = std::cos(yaw0), s_ = std::sin(yaw0);
        const double ex[2] = {c * w, -s_ * w}, ez[2] = {s_ * d, c * d};
        const double ux[2] = {c, -s_}, uz[2] = {s_, c};
        const bool acrossX = std::fabs(ux[1]) >= std::fabs(uz[1]);
        const int64_t n = PyInt(reach / PyMax(PyMin(w, d), 1.0)) + 2;
        int narrowed = 0;
        const Value srcCopy = Ents().a[(size_t)si];
        std::vector<Value> added;
        for (int64_t i = -n; i <= n; ++i) {
            for (int64_t j = -n; j <= n; ++j) {
                if (i == 0 && j == 0) continue;
                double px = p0x + i * ex[0] + j * ez[0], pz = p0z + i * ex[1] + j * ez[1];
                double aheads[4], rights[4];
                int q = 0;
                for (double a : {0.0, 1.0})
                    for (double b : {0.0, 1.0}) {
                        const double xx = px + (x0 + a * w) * ux[0] + (z0 + b * d) * uz[0], zz = pz + (x0 + a * w) * ux[1] + (z0 + b * d) * uz[1];
                        aheads[q] = cx - xx;
                        rights[q] = cz - zz;
                        ++q;
                    }
                double rLo = rights[0], rHi = rights[0], aLo = aheads[0], aHi = aheads[0];
                for (int k = 1; k < 4; ++k) { rLo = PyMin(rLo, rights[k]); rHi = PyMax(rHi, rights[k]); aLo = PyMin(aLo, aheads[k]); aHi = PyMax(aHi, aheads[k]); }
                const double nearA = PyMax(PyMax(aLo, 0.0), -aHi), nearR = PyMax(PyMax(rLo, 0.0), -rHi);
                const double nearD = PyHypot(nearA, nearR);
                if (nearD > reach || rHi <= lo || rLo >= hi) continue;
                double f0 = 0.0, f1 = 1.0;
                const double* u = acrossX ? ux : uz;
                const double lo0 = acrossX ? x0 : z0, span = acrossX ? w : d;
                if (rLo < lo || rHi > hi) {
                    const double r0 = cz - (pz + lo0 * u[1]);
                    const double r1 = r0 - span * u[1];
                    if (r1 - r0 == 0.0) throw Bad("float division by zero");
                    const double ta = (lo - r0) / (r1 - r0), tb = (hi - r0) / (r1 - r0);
                    f0 = PyMax(0.0, PyMin(ta, tb));
                    f1 = PyMin(1.0, PyMax(ta, tb));
                    if (f1 - f0 < 0.02) continue;
                    ++narrowed;
                }
                Value e = srcCopy;
                const std::string name = "pcvr_lava_" + std::to_string(act) + "_" + std::to_string(i) + "_" + std::to_string(j);
                const uint32_t nid = NewId(rnd, ids);
                Set(e, "name", MakeStr(name));
                Set(e, "id", MakeInt(nid));
                RenameComponents(e, name);
                Value* t_ = NeedT(e);
                if (f0 > 0.0 || f1 < 1.0) {
                    const double k = f1 - f0;
                    const double shift = lo0 + f0 * span - lo0 * k;
                    px = px + shift * u[0];
                    pz = pz + shift * u[1];
                    const Value* scv = Get(*t_, "scale");
                    Value sc = Truthy(scv) ? *scv : Vec3(1.0, 1.0, 1.0);
                    if (sc.kind != Value::Object) throw Bad("a scale that is no dict");
                    const char* key = acrossX ? "x" : "z";
                    Set(sc, key, Float(Get(sc, key), 1.0) * k);
                    Set(*t_, "scale", std::move(sc));
                }
                Set(*t_, "position", Vec3(px, p0y, pz));
                added.push_back(std::move(e));
            }
        }
        const size_t tiles = added.size();
        // LAVA_FALLS = {}: no lava falls
        Extend(added);
        // the guardians
        size_t spots = 0;
        for (const auto& sp : STATUE_SPOTS4) {
            const double right = sp[0], ahead = sp[1];
            const double th = PyAtan2(right, -ahead) + kPi;
            for (const char* m : STATUE_MODELS) {
                std::string base = Base(m);
                base = base.substr(0, base.size() - std::strlen(".model"));
                const std::string nm = "pcvr_guardian_" + std::to_string(act) + "_" + std::to_string(spots) + "_" + base;
                const uint32_t nid = NewId(rnd, ids);
                added.push_back(Tree(nm, nid, m, cx - ahead, STATUE_Y, cz - right, th, STATUE_SIZE, STATUE_SIZE, STATUE_SIZE));
            }
            ++spots;
        }
        if (spots) {
            Extend(added);
            Depend(Strs(std::begin(STATUE_MODELS), std::end(STATUE_MODELS)));
            Value& tex = SetDefault(SetDefault(preset, "dependencies", MakeObj()), "textures", MakeArr());
            std::set<std::string> have;
            for (Value& dd : tex.a) if (dd.kind == Value::Object) have.insert(Lower(StrOr(Get(dd, "path"), "")));
            for (const std::string& t : StatueTextures()) {
                if (!have.count(t)) {
                    Value p = MakeObj();
                    Set(p, "path", MakeStr(t));
                    tex.a.push_back(std::move(p));
                }
            }
        }
        Strs gone;
        for (const std::string& nm : DROP_NAMES4) for (int r : DROP_OUT4) gone.push_back(nm + COPY + std::to_string(r));
        RemoveIf([&](Value& e, size_t) { return NameIn(e, gone); });
        size_t dropped = 0;
        for (Value& e : Ents().a) {
            const std::string nm = NameStr(e);
            const size_t at = nm.find(COPY);
            if (at != std::string::npos && In(DROP_NAMES4, nm.substr(0, at))) {
                Value& p = Position(*NeedT(e));
                Set(p, "y", Float(&Need(p, "y"), 0.0) - DROP4);
                ++dropped;
            }
        }
        const size_t total = tiles + spots * 2;
        townReport[act] = std::to_string(spots) + " guardians, " + std::to_string(dropped) + " copies brought down " + Repr(DROP4) + ", " + std::to_string(tiles) +
                          " lava tiles (" + Fixed(w, 0) + " x " + Fixed(d, 0) + "; " + std::to_string(narrowed) + " narrowed to the sides " + Fixed(lo, 0) + " / " +
                          Fixed(hi, 0) + "), " + std::to_string(total - tiles - spots * 2) + " lava falls to try";
        return total;
    }

    // apply_edits(preset, act, edits, ids)
    size_t ApplyEdits(int act, const Value* edits) {
        if (!Truthy(edits)) return 0;
        PyRandom rnd(9900 + act);
        const Cam& cam = CAMERAS[act];
        const double cx = cam.x, cz = cam.z;
        Strs gone;
        if (const Value* del = Get(*edits, "deleted"); Truthy(del)) {
            if (del->kind != Value::Array) throw Bad("deleted is no list");
            for (const Value& g : del->a) {
                if (g.kind != Value::String) throw Bad("a deleted name that is no string");
                if (!In(gone, g.s)) gone.push_back(g.s);
            }
        }
        RemoveIf([&](Value& e, size_t) { return NameIn(e, gone); });
        size_t n = gone.size();
        const Value* moved = Get(*edits, "moved");
        if (moved && !Truthy(moved)) moved = nullptr;
        if (moved && moved->kind != Value::Object) throw Bad("moved is no dict");
        if (moved) {
            for (Value& e : Ents().a) {
                const Value* nm = Get(e, "name");
                const Value* d = nullptr;
                if (nm && nm->kind == Value::String) d = Get(*moved, nm->s.c_str());
                Value* t = Truthy(d) ? Transform(e) : nullptr;
                if (!TruthyT(t)) continue;
                Value& p = Position(*t);
                if (Get(*d, "right")) {
                    const double x = cx - Float(&Need(*d, "ahead"), 0.0), z = cz - Float(&Need(*d, "right"), 0.0);
                    Set(p, "x", x);
                    Set(p, "z", z);
                }
                if (Get(*d, "y")) Set(p, "y", Float(Get(*d, "y"), 0.0));
                if (Get(*d, "turn")) {
                    const double a = Radians(Float(Get(*d, "turn"), 0.0));
                    const Q q = OrientationOr(*t);
                    Set(*t, "orientation", QVal(QMul(YawQ(a), q)));
                }
                if (Truthy(Get(*d, "tilt_x")) || Truthy(Get(*d, "tilt_z"))) {
                    const Q q = OrientationOr(*t);
                    Set(*t, "orientation", QVal(QMul(q, TiltQ(Get(*d, "tilt_x"), Get(*d, "tilt_z")))));
                }
                if (Get(*d, "scale_by")) {
                    Value& sc = SetDefault(*t, "scale", Vec3(1.0, 1.0, 1.0));
                    for (const char* k : {"x", "y", "z"}) Set(sc, k, Float(Get(sc, k), 1.0) * Float(Get(*d, "scale_by"), 0.0));
                }
                ++n;
            }
        }
        if (const Value* add = Get(*edits, "added"); Truthy(add)) {
            if (add->kind != Value::Array) throw Bad("added is no list");
            for (const Value& a : add->a) {
                const double sc = Float(Get(a, "scale"), 1.0);
                const Value& name = Need(a, "name");
                const uint32_t nid = NewId(rnd, ids);
                const Value& model = Need(a, "model");
                Value e = Tree(name, nid, model, cx - Float(&Need(a, "ahead"), 0.0), Float(Get(a, "y"), 0.0), cz - Float(&Need(a, "right"), 0.0),
                               Radians(Float(Get(a, "yaw"), 0.0)), sc, sc, sc);
                if (Truthy(Get(a, "tilt_x")) || Truthy(Get(a, "tilt_z"))) {
                    Value* t = NeedT(e);
                    const Q q = QOf(Need(*t, "orientation"));
                    Set(*t, "orientation", QVal(QMul(q, TiltQ(Get(a, "tilt_x"), Get(a, "tilt_z")))));
                }
                Ents().a.push_back(std::move(e));
                std::vector<Value> one{model};
                Depend(one);
                ++n;
            }
        }
        return n;
    }

    // measure the water and lava tiles (prepare(): assets given)
    void MeasureTiles(const FileReader& assets, std::vector<std::string>* notes) {
        std::map<std::string, std::pair<bool, Extent>> cache;   // (the script measures each such entity again; the same answer)
        auto measure = [&](const std::string& model, Extent* out) {
            auto it = cache.find(model);
            if (it == cache.end()) {
                Extent e;
                const bool ok = MeasureModel(assets, model, &e);
                it = cache.emplace(model, std::make_pair(ok, e)).first;
            }
            *out = it->second.second;
            return it->second.first;
        };
        for (Value& e : Ents().a) {
            if (NameIs(e, TOWN_WATER3)) {
                Extent b;
                const std::string m = ModelOf(e);
                if (measure(m, &b)) waterExtent[m] = {b.mn[0], b.mn[2], b.mx[0], b.mx[2]};
            }
            if (Lower(NameStr(e)) == LAVA4) {
                Extent b;
                const std::string m = ModelOf(e);
                if (measure(m, &b)) {
                    lavaExtent[m] = {b.mn[0], b.mn[2], b.mx[0], b.mx[2]};
                    notes->push_back("lava tile " + m + ": x " + Fixed(b.mn[0], 1) + ".." + Fixed(b.mx[0], 1) + ", y " + Fixed(b.mn[1], 2) + ".." +
                                     Fixed(b.mx[1], 2) + ", z " + Fixed(b.mn[2], 1) + ".." + Fixed(b.mx[2], 1));
                }
            }
        }
    }
};

// ================================================================ measure(): podglad_modeli.Big(...).all_meshes() over shared/granny_remesh.h

// potion_assets.Granny / podglad_modeli.Big, as far as all_meshes() reads: a file's types by their own tree, raising (here: Bad) where they do
class Granny {
  public:
    explicit Granny(const std::string& data) {
        static const unsigned char kMagic[16] = {0xe5, 0x9b, 0x49, 0x5e, 0x6f, 0x63, 0x1f, 0x14, 0x1e, 0x13, 0xeb, 0xa9, 0x90, 0xbe, 0xed, 0xc4};
        const size_t limit = 256u * 1024 * 1024;   // potion_assets.LIMIT, as measure() sets it
        if (data.size() > limit || data.size() < 16 || memcmp(data.data(), kMagic, 16) != 0) throw Bad("Expected LE64 Granny file");
        if (data.size() < 32 + 36) throw Bad("Asset range out of bounds");
        uint32_t h[9];
        memcpy(h, data.data() + 32, sizeof h);
        if (h[0] != 7 || h[1] != data.size() || !(1 <= h[4] && h[4] <= 16)) throw Bad("Invalid Granny header");
        uint64_t expanded = 0;
        for (uint32_t i = 0; i < h[4]; ++i) {
            const size_t at = 32 + (size_t)h[3] + (size_t)i * 44;
            if (at + 44 > data.size()) throw Bad("Asset range out of bounds");
            uint32_t s[11];
            memcpy(s, data.data() + at, sizeof s);
            expanded += s[3];
            if (expanded > limit || s[8] > 20000) throw Bad("Oversized Granny data");
        }
        std::string why;
        if (!grannyremesh::Read(data, &f_, &why)) throw Bad(why);
        walk_ = std::make_unique<grannyremesh::Walk>(f_);
        rootObj_ = {f_.h.rootObj.section, f_.h.rootObj.offset};
        rootType_ = {f_.h.rootType.section, f_.h.rootType.offset};
    }

    using Addr = grannyremesh::Addr;
    struct Field { std::string name; uint32_t kind = 0; Addr sub; uint32_t width = 1; Addr ref; };
    using Fields = std::vector<Field>;   // a dict: the last of a name wins

    static const Field* Find(const Fields& fs, const char* name) {
        for (size_t k = fs.size(); k-- > 0;) if (fs[k].name == name) return &fs[k];
        return nullptr;
    }
    static const Field& NeedF(const Fields& fs, const char* name) {
        const Field* f = Find(fs, name);
        if (!f) throw Bad(std::string("KeyError: ") + name);
        return *f;
    }

    Fields Root() { return FieldsOf(rootObj_, rootType_); }

    uint32_t U32(Addr a) const {
        if (!walk_->In(a, 4)) throw Bad("Asset range out of bounds");
        return walk_->Get<uint32_t>(a);
    }
    Addr Ptr(Addr a) const { return walk_->Ptr(a); }

    struct Def { std::vector<std::tuple<std::string, uint32_t, Addr, uint32_t, uint32_t>> fields; uint32_t size = 0; };
    const Def& Definition(Addr t) {
        if (!t.ok()) throw Bad("Missing Granny type");
        const uint64_t key = ((uint64_t)t.s << 32) | t.o;
        auto it = defs_.find(key);
        if (it != defs_.end()) return it->second;
        static const std::map<uint32_t, uint32_t> widths = {{2, 8}, {3, 12}, {4, 12}, {5, 16}, {7, 20}, {8, 8}, {9, 68}, {10, 4}, {11, 1}, {12, 1}, {13, 1},
                                                            {14, 1}, {15, 2}, {16, 2}, {17, 2}, {18, 2}, {19, 4}, {20, 4}, {21, 2}, {22, 8}};
        Def d;
        bool ended = false;
        for (int i = 0; i < 128; ++i) {
            const Addr at = t.at((uint32_t)i * 44);
            const uint32_t kind = U32(at);
            if (!kind) { ended = true; break; }
            const uint32_t width = U32(at.at(20));
            const auto w = widths.find(kind);
            if (w == widths.end() || width > 64) throw Bad("Unsupported Granny field");
            const uint32_t wd = std::max(1u, width);
            d.fields.emplace_back(String(Ptr(at.at(4))), kind, Ptr(at.at(12)), wd, d.size);
            d.size += w->second * wd;
        }
        if (!ended) throw Bad("Unterminated Granny type");
        return defs_.emplace(key, std::move(d)).first->second;
    }
    Fields FieldsOf(Addr ref, Addr type) {
        if (!ref.ok() || !type.ok()) throw Bad("Missing Granny reference");
        Fields out;
        for (const auto& f : Definition(type).fields) out.push_back({std::get<0>(f), std::get<1>(f), std::get<2>(f), std::get<3>(f), ref.at(std::get<4>(f))});
        return out;
    }
    std::string String(Addr a) const {
        if (!a.ok()) return "";
        if (a.s >= f_.sec.size()) throw Bad("Invalid Granny string");
        const std::vector<uint8_t>& d = f_.sec[a.s];
        const size_t stop = std::min(d.size(), (size_t)a.o + 1024);
        for (size_t o = a.o; o < stop; ++o) if (!d[o]) return std::string((const char*)d.data() + a.o, o - a.o);
        throw Bad("Invalid Granny string");
    }
    Fields Reference(const Field& f) {
        if (f.kind != 2) throw Bad("Expected Granny reference");
        return FieldsOf(Ptr(f.ref), f.sub);
    }
    std::vector<Fields> Array(const Field& f) {
        Addr sub = f.sub, ref = f.ref;
        if (f.kind == 7) {
            sub = Ptr(ref);
            ref = ref.at(8);
        } else if (f.kind != 3 && f.kind != 4) {
            throw Bad("Expected Granny array");
        }
        const uint32_t count = U32(ref);
        const Addr base = Ptr(ref.at(4));
        if (count && !base.ok()) throw Bad("Missing Granny array data");
        std::vector<Fields> out;
        if (!count) return out;
        const uint32_t stride = f.kind == 4 ? 8 : Definition(sub).size;
        out.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            const Addr r = base.at(i * stride);
            out.push_back(FieldsOf(f.kind == 4 ? Ptr(r) : r, sub));
        }
        return out;
    }
    // scalar(field): its width of numbers, the normalised kinds divided
    std::vector<double> Scalar(const Field& f) const {
        uint32_t size = 0;
        switch (f.kind) {
        case 10: case 19: case 20: size = 4; break;
        case 13: case 14: size = 1; break;
        case 15: case 16: case 17: case 18: case 21: size = 2; break;
        default: throw Bad("Unsupported geometry component");
        }
        if (!walk_->In(f.ref, size * f.width)) throw Bad("Asset range out of bounds");
        std::vector<double> v(f.width);
        for (uint32_t k = 0; k < f.width; ++k) {
            const Addr a = f.ref.at(k * size);
            switch (f.kind) {
            case 10: v[k] = (double)walk_->Get<float>(a); break;
            case 13: v[k] = (double)walk_->Get<int8_t>(a) / 127.; break;
            case 14: v[k] = (double)walk_->Get<uint8_t>(a) / 255.; break;
            case 15: v[k] = (double)walk_->Get<int16_t>(a); break;
            case 16: v[k] = (double)walk_->Get<uint16_t>(a); break;
            case 17: v[k] = (double)walk_->Get<int16_t>(a) / 32767.; break;
            case 18: v[k] = (double)walk_->Get<uint16_t>(a) / 65535.; break;
            case 19: v[k] = (double)walk_->Get<int32_t>(a); break;
            case 20: v[k] = (double)walk_->Get<uint32_t>(a); break;
            case 21: v[k] = Half(walk_->Get<uint16_t>(a)); break;
            }
        }
        return v;
    }
    uint32_t RawIndex(const Field& f) const {   // self.read(ref, '<H' if kind in (15, 16, 17, 18, 21) else '<I')[0]
        const bool h = f.kind == 15 || f.kind == 16 || f.kind == 17 || f.kind == 18 || f.kind == 21;
        if (!walk_->In(f.ref, h ? 2 : 4)) throw Bad("Asset range out of bounds");
        return h ? walk_->Get<uint16_t>(f.ref) : walk_->Get<uint32_t>(f.ref);
    }
    static double Half(uint16_t h) {
        const int e = (h >> 10) & 0x1F, m = h & 0x3FF;
        double v;
        if (e == 0) v = std::ldexp((double)m, -24);
        else if (e == 31) v = m ? std::nan("") : HUGE_VAL;
        else v = std::ldexp((double)(m | 0x400), e - 25);
        return (h & 0x8000) ? -v : v;
    }

  private:
    grannyremesh::File f_;
    std::unique_ptr<grannyremesh::Walk> walk_;
    Addr rootObj_, rootType_;
    std::map<uint64_t, Def> defs_;
};

// all_meshes()' vertex positions (each mesh's, its own scale applied); raises as the script does. A Position narrower than 3: *broken.
std::vector<std::array<double, 3>> AllMeshPositions(const std::string& data, bool* broken) {
    Granny g(data);
    std::vector<std::array<double, 3>> pts;
    const Granny::Fields root = g.Root();
    for (const Granny::Fields& mesh : g.Array(Granny::NeedF(root, "Meshes"))) {
        const Granny::Fields vd = g.Reference(Granny::NeedF(mesh, "PrimaryVertexData"));
        const std::vector<Granny::Fields> verts = g.Array(Granny::NeedF(vd, "Vertices"));
        const Granny::Fields topo = g.Reference(Granny::NeedF(mesh, "PrimaryTopology"));
        std::vector<Granny::Fields> idx = g.Array(Granny::NeedF(topo, "Indices16"));
        if (idx.empty()) idx = g.Array(Granny::NeedF(topo, "Indices"));
        std::vector<uint32_t> ix;
        for (const Granny::Fields& i : idx) {
            if (i.empty()) throw Bad("StopIteration");
            ix.push_back(g.RawIndex(i.front()));
        }
        std::vector<std::array<double, 3>> data3;
        for (const Granny::Fields& v : verts) {
            const std::vector<double> p = g.Scalar(Granny::NeedF(v, "Position"));
            if (p.size() < 3) *broken = true;
            if (const Granny::Field* nrm = Granny::Find(v, "Normal")) (void)g.Scalar(*nrm);
            if (const Granny::Field* uv = Granny::Find(v, "TextureCoordinates0")) (void)g.Scalar(*uv);
            data3.push_back({p.size() > 0 ? p[0] : 0.0, p.size() > 1 ? p[1] : 0.0, p.size() > 2 ? p[2] : 0.0});
        }
        if (data3.empty() || ix.empty() || ix.size() % 3 || *std::max_element(ix.begin(), ix.end()) >= data3.size()) continue;
        const Granny::Field& pk = Granny::NeedF(verts[0], "Position");
        if ((pk.kind == 13 || pk.kind == 17) && g.Scalar(pk).size() >= 4) {
            for (size_t k = 0; k < data3.size(); ++k) {
                const double kk = std::pow(2.0, 13.0 * g.Scalar(Granny::NeedF(verts[k], "Position"))[3]);
                data3[k] = {data3[k][0] * kk, data3[k][1] * kk, data3[k][2] * kk};
            }
        }
        // (its materials and groups are read in try blocks there: nothing of them changes the positions)
        pts.insert(pts.end(), data3.begin(), data3.end());
    }
    return pts;
}

// ================================================================ the files

bool IsDir(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool ReadFileW(const std::wstring& path, std::string* out) {
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
bool WriteFileW(const std::wstring& path, const void* data, size_t n) {
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

bool CascRead(HANDLE storage, const std::string& name, std::string* out, unsigned long long limit) {
    const std::string full = "data:" + name;
    HANDLE f = nullptr;
    if (!CascOpenFile(storage, full.c_str(), 0, CASC_OPEN_BY_NAME, &f)) return false;
    ULONGLONG size = 0;
    bool ok = CascGetFileSize64(f, &size) && size < limit;
    if (ok) {
        out->resize((size_t)size);
        DWORD got = 0;
        ok = size == 0 || (CascReadFile(f, out->data(), (DWORD)size, &got) && got == size);
    }
    CascCloseFile(f);
    return ok;
}

std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }   // ASCII: our own texts and the file names

std::string ResourceText(int id) {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&ResourceText, &self);
    HRSRC r = FindResourceW(self, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));   // RT_RCDATA
    if (!r) return std::string();
    HGLOBAL g = LoadResource(self, r);
    const DWORD n = SizeofResource(self, r);
    const void* p = g ? LockResource(g) : nullptr;
    return p && n ? std::string((const char*)p, n) : std::string();
}

}  // namespace

std::vector<std::string> SourcePresets() {
    std::set<std::string> s = {TILES2, TOWN3};   // sorted({t[0] for t in TILES.values()} | {t[0] for t in TOWN.values()})
    return std::vector<std::string>(s.begin(), s.end());
}

bool MeasureModel(const FileReader& read, const std::string& model, Extent* out) {
    try {
        const std::string low = Lower(model);
        if (low.size() < 6) return false;
        const std::string stem = low.substr(0, low.size() - 6);   // model.lower()[:-len('.model')]
        std::vector<std::string> names;
        for (int k = 0; k < 6; ++k) names.push_back(stem + "_lod" + std::to_string(k) + ".model");
        names.push_back(low);
        for (const std::string& f : names) {
            std::string bytes;
            if (!read || !read(f, &bytes)) continue;
            std::vector<std::array<double, 3>> pts;
            bool broken = false;
            try {
                pts = AllMeshPositions(bytes, &broken);
            } catch (...) {   // the next lod
                continue;
            }
            if (broken && !pts.empty()) return false;   // (p[k] past a short Position: the script's outer except - None)
            if (!pts.empty()) {
                Extent e;
                for (int k = 0; k < 3; ++k) {
                    e.mn[k] = pts[0][k];
                    e.mx[k] = pts[0][k];
                    for (const auto& p : pts) { e.mn[k] = PyMin(e.mn[k], p[k]); e.mx[k] = PyMax(e.mx[k], p[k]); }
                }
                *out = e;
                return true;
            }
        }
    } catch (...) {
    }
    return false;
}

bool Build(const Input& in, std::string* out, std::vector<std::string>* report, std::string* why) {
    try {
        std::vector<std::string> lines;
        Scene s(in.sources);
        s.preset = ParseJson(in.preset);
        const Value* ents = s.preset.kind == Value::Object ? Get(s.preset, "entities") : nullptr;
        if (!ents || ents->kind != Value::Array) throw Bad("The main menu preset has no entity list; left as it is");
        if (in.assets) s.MeasureTiles(in.assets, &lines);
        const size_t clouds = s.WithoutClouds();
        for (Value& e : s.Ents().a) s.ids.insert(Int(Get(e, "id"), 0));
        const size_t hidden = s.Hide();
        lines.push_back("clouds out: " + std::to_string(clouds) + ", hidden: " + std::to_string(hidden));
        std::map<int, Value> edits;
        for (int act : ACTS) {
            const auto it = in.edits.find(act);
            if (it != in.edits.end()) edits[act] = ParseJson(it->second, false);
        }
        for (int act : ACTS) {
            const Value* ed = edits.count(act) ? &edits[act] : nullptr;
            if (Truthy(ed)) {
                if (const Value* road = Get(*ed, "road"); Truthy(road)) {
                    if (road->kind != Value::Array) throw Bad("road is no list");
                    std::vector<std::pair<double, double>> path;
                    for (const Value& p : road->a) {
                        if (p.kind != Value::Array || p.a.size() != 2) throw Bad("a road point that is not two numbers");
                        path.push_back({Float(&p.a[0], 0.0), Float(&p.a[1], 0.0)});
                    }
                    s.roadPath[act] = path;
                }
                if (const Value* half = Get(*ed, "road_half"); Truthy(half)) s.roadNarrow[act] = Float(half, 0.0) / ROAD_HALF;
            }
            const auto [src, add] = s.FillRound(act);
            const size_t trees = s.Forest(act);
            const size_t outN = s.ClearFields(act);
            const size_t dark = s.Darkwood(act);
            const size_t ivy = s.IvyOut(act);
            const size_t thick = s.Thicket(act);
            const size_t grasses = s.Grass(act);
            const size_t thinned = s.ThinOut(act);
            const size_t finished = s.Finish(act);
            const size_t walled = s.Walls(act);
            const size_t roads = s.Road(act);
            const size_t scattered = 0;   // scatter(): SCATTER = {}
            size_t tiled = s.Tiles(act);
            tiled += s.Towers(act);
            tiled += s.Town(act);
            tiled += s.Lava(act);
            if (s.townReport.count(act)) lines.push_back("act " + std::to_string(act) + ": " + s.townReport[act]);
            const size_t edited = s.ApplyEdits(act, ed);
            if (edited) lines.push_back("act " + std::to_string(act) + ": " + std::to_string(edited) + " of the owner's edits applied");
            std::string clear;
            for (const std::string& f : Clear(act)) clear += (clear.empty() ? "" : ", ") + f;
            lines.push_back("act " + std::to_string(act) + ": " + std::to_string(src) + " pieces of its own, " + std::to_string(add) + " copies round the camera, " +
                            std::to_string(trees) + " trees (" + std::to_string(outN) + " trees out of " + clear + "), " + std::to_string(dark) +
                            " Dark Wood trees, " + std::to_string(thick) + " in thickets, " + std::to_string(thinned) + " thinned out, " +
                            std::to_string(finished) + " more out, " + std::to_string(ivy) + " ivy out, " + std::to_string(grasses) + " grass, " +
                            std::to_string(walled) + " wall pieces, " + std::to_string(roads) + " road pieces, " + std::to_string(scattered) +
                            " scattered, " + std::to_string(tiled) + " from game strips, 0 lights");   // lights(): LIGHTS_ON = False
        }
        std::string text;
        text.reserve(in.preset.size() * 8);
        Put(text, s.preset);
        text += '\n';
        *out = std::move(text);
        if (report) *report = std::move(lines);
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

std::map<int, std::string> EmbeddedEdits() {
    std::map<int, std::string> out;
    const std::pair<int, int> ids[] = {{2, IDR_FRONTEND_EDITS_ACT2}, {3, IDR_FRONTEND_EDITS_ACT3}, {5, IDR_FRONTEND_EDITS_ACT5}};
    for (const auto& a : ids) {
        std::string t = ResourceText(a.second);
        if (!t.empty()) out[a.first] = std::move(t);
    }
    return out;
}

Result Prepare(const std::wstring& game, const std::wstring& modRoot, bool on) {
    Result r;
    // Only into the mod's own data folder (D2RLoader's mods\D2R-VR\D2R-VR.mpq, which Setup's WriteModInfo makes)
    const std::wstring mpq = modRoot.empty() ? std::wstring() : modRoot + L"D2R-VR.mpq\\";
    if (mpq.empty() || !IsDir(mpq)) {
        r.state = kNotHere;
        r.text = lang::TX(L"frontend_scene.no_mod", L"only in the mod's own folder (mods\\D2R-VR\\D2R-VR.mpq), which is not here: the game's own main menu");
        return r;
    }
    const std::string name = std::string("mods/D2R-VR/D2R-VR.mpq/") + PRESET_NAME;   // (the receipt's, '/' between the parts)
    std::wstring path = mpq + Wide(PRESET_NAME);
    for (wchar_t& c : path) if (c == L'/') c = L'\\';
    const std::wstring receiptPath = modRoot + L"frontend_scene.json";
    std::string last, old;
    ReadFileW(receiptPath, &last);
    const bool exists = ReadFileW(path, &old);
    const std::string lastHash = arcanesky::ReceiptValue(last, name);
    const bool ours = exists && !lastHash.empty() && arcanesky::Hash(old.data(), old.size()) == lastHash;

    // Our scene out again (unticked, or the game no longer has the preset); a file of that name the player made or changed stays.
    auto takeOut = [&](const std::wstring& because) {
        if (ours) {
            if (!DeleteFileW(path.c_str())) {
                r.state = kFailed;
                r.text = lang::TX(L"frontend_scene.not_taken_out", L"could not take out mods\\D2R-VR\\D2R-VR.mpq\\...\\frontendact2.json (error {error})",
                                  {{L"error", std::to_wstring(GetLastError())}});
                return r;
            }
            r.wrote = true;
        }
        if (!last.empty()) DeleteFileW(receiptPath.c_str());
        r.text = because;
        if (ours) r.text += L"; " + lang::TX(L"frontend_scene.ours_out", L"our 3D main menu taken out of mods\\D2R-VR");
        else if (exists) r.text += L"; " + lang::TX(L"frontend_scene.not_ours", L"a frontendact2.json in mods\\D2R-VR that is not ours stays as it is");
        return r;
    };
    if (!on) {
        r.state = kOff;
        return takeOut(lang::TX(L"frontend_scene.off", L"off: the main menu as the game has it"));
    }

    std::wstring root = game;
    if (!root.empty() && root.back() == L'\\') root.pop_back();
    HANDLE storage = nullptr;
    if (!CascOpenStorage(root.c_str(), 0, &storage)) {
        r.text = lang::TX(L"frontend_scene.no_archives", L"the game's archives could not be opened (error {error}); it is tried again at the next start",
                          {{L"error", std::to_wstring(GetLastError())}});
        return r;
    }
    Input in;
    const bool read = CascRead(storage, PRESET_NAME, &in.preset, 32ull << 20);
    for (const std::string& w : SourcePresets()) {
        std::string text;
        if (CascRead(storage, w, &text, 32ull << 20)) in.sources[w] = std::move(text);
    }
    in.edits = EmbeddedEdits();
    in.assets = [storage](const std::string& f, std::string* bytes) { return CascRead(storage, f, bytes, 256ull << 20); };
    std::string made, why;
    const bool built = read && Build(in, &made, &r.report, &why);
    CASC_STORAGE_PRODUCT product{};
    const bool haveBuild = CascGetStorageInfo(storage, CascStorageProduct, &product, sizeof product, nullptr) && product.BuildNumber;
    CascCloseStorage(storage);
    if (!read) {
        takeOut(L"");
        r.state = kFailed;
        r.text = lang::TX(L"frontend_scene.no_preset", L"the main menu's preset is not in this game: the game's own main menu");
        return r;
    }
    if (!built) {
        // as Unleashed's installer: whatever is there stays
        r.state = kFailed;
        r.text = lang::TX(L"frontend_scene.refused", L"the 3D main menu could not be made from your game ({why}); the main menu stays as it is",
                          {{L"why", Wide(why)}});
        return r;
    }
    if (!arcanesky::MayReplace(exists, old, made, lastHash)) {
        r.state = kMade;
        r.text = lang::TX(L"frontend_scene.changed_by_you", L"mods\\D2R-VR\\D2R-VR.mpq\\...\\frontendact2.json was changed by you: left as it is");
        return r;
    }
    if (!exists || old != made) {
        if (!MakeDirs(path.substr(0, path.find_last_of(L'\\'))) || !WriteFileW(path, made.data(), made.size())) {
            r.text = lang::TX(L"frontend_scene.not_written", L"could not write mods\\D2R-VR\\D2R-VR.mpq\\...\\frontendact2.json (error {error})",
                              {{L"error", std::to_wstring(GetLastError())}});
            return r;
        }
        r.wrote = true;
    }
    const std::string receipt = arcanesky::Receipt(haveBuild ? std::to_string(product.BuildNumber) : std::string(),
                                                   {{name, arcanesky::Hash(made.data(), made.size())}});
    if (receipt != last && !WriteFileW(receiptPath, receipt.data(), receipt.size())) {
        r.text = lang::TX(L"frontend_scene.no_receipt", L"could not write mods\\D2R-VR\\frontend_scene.json");
        return r;
    }
    r.state = kMade;
    r.text = r.wrote ? lang::TX(L"frontend_scene.made", L"made from your game: the main menu's act scenes round you (mods\\D2R-VR\\D2R-VR.mpq), from the next start of the game")
                     : lang::TX(L"frontend_scene.ready", L"ready: the main menu's act scenes round you (mods\\D2R-VR\\D2R-VR.mpq)");
    return r;
}

}  // namespace frontendscene
