// props_presets.h - [weight] knock_props: the knockable props of one HD level preset (data/hd/env/preset/**.json of the player's own game), for the catalogue
// D2R VR Settings makes (tools/settings/props_assets.cpp -> d2r_vr_props/props.bin, shared/props_scene.h). Plain parsing, no game code; tested by
// cleanroom/tests/props_scene_test.cpp.
//
// A preset lists its entities, each with an "id" and its "components". A prop is placed on its own: a transform and a model, nothing that lights, emits or animates
// (moving its matrix moves all of it); fading with walls that cut away is allowed, and so is a static physics body (only the HD renderer's own debris and ragdolls touch it,
// and it stays where it was). Its model's name tells its kind (propscene::KindOfModel). After bearstonem/d2r-pcvr's prepare_props.py (MIT).
#pragma once

#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "props_scene.h"

namespace propspresets {

// A small JSON tree: enough for the game's preset files.
struct Value {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Value> items;                         // Array
    std::vector<std::pair<std::string, Value>> keys;  // Object, in file order
    const Value* Get(const char* k) const {
        for (const auto& kv : keys) if (kv.first == k) return &kv.second;
        return nullptr;
    }
};

class Parser {
    const char* p_;
    const char* e_;
    int depth_ = 0;
    void Ws() { while (p_ < e_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_; }
    bool Lit(const char* w) {
        const char* q = p_;
        for (; *w; ++w, ++q) if (q >= e_ || *q != *w) return false;
        p_ = q;
        return true;
    }
    bool Str(std::string& out) {
        if (p_ >= e_ || *p_ != '"') return false;
        ++p_;
        out.clear();
        while (p_ < e_ && *p_ != '"') {
            if (*p_ == '\\') {
                if (++p_ >= e_) return false;
                const char c = *p_++;
                switch (c) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {   // (names are ASCII: a code point past it is kept as '?')
                        if (e_ - p_ < 4) return false;
                        const unsigned v = static_cast<unsigned>(std::strtoul(std::string(p_, 4).c_str(), nullptr, 16));
                        out += v < 128 ? static_cast<char>(v) : '?';
                        p_ += 4;
                        break;
                    }
                    default: out += c;
                }
            } else {
                out += *p_++;
            }
        }
        if (p_ >= e_) return false;
        ++p_;
        return true;
    }
    bool Val(Value& v) {
        if (++depth_ > 64) return false;
        Ws();
        if (p_ >= e_) return false;
        bool ok = true;
        if (*p_ == '{') {
            v.type = Value::Object;
            ++p_;
            Ws();
            if (p_ < e_ && *p_ == '}') { ++p_; --depth_; return true; }
            for (;;) {
                Ws();
                std::string k;
                if (!Str(k)) return false;
                Ws();
                if (p_ >= e_ || *p_ != ':') return false;
                ++p_;
                v.keys.emplace_back(std::move(k), Value{});
                if (!Val(v.keys.back().second)) return false;
                Ws();
                if (p_ < e_ && *p_ == ',') { ++p_; continue; }
                if (p_ < e_ && *p_ == '}') { ++p_; break; }
                return false;
            }
        } else if (*p_ == '[') {
            v.type = Value::Array;
            ++p_;
            Ws();
            if (p_ < e_ && *p_ == ']') { ++p_; --depth_; return true; }
            for (;;) {
                v.items.emplace_back();
                if (!Val(v.items.back())) return false;
                Ws();
                if (p_ < e_ && *p_ == ',') { ++p_; continue; }
                if (p_ < e_ && *p_ == ']') { ++p_; break; }
                return false;
            }
        } else if (*p_ == '"') {
            v.type = Value::String;
            ok = Str(v.s);
        } else if (Lit("true")) {
            v.type = Value::Bool;
            v.b = true;
        } else if (Lit("false")) {
            v.type = Value::Bool;
        } else if (Lit("null")) {
            v.type = Value::Null;
        } else {
            char* end = nullptr;
            const std::string num(p_, static_cast<size_t>((std::min<ptrdiff_t>)(e_ - p_, 64)));
            v.type = Value::Number;
            v.n = std::strtod(num.c_str(), &end);
            if (end == num.c_str()) return false;
            p_ += end - num.c_str();
        }
        --depth_;
        return ok;
    }

public:
    Parser(const char* data, size_t n) : p_(data), e_(data + n) {}
    bool Parse(Value& out) {
        // a byte-order mark first is allowed
        if (e_ - p_ >= 3 && static_cast<unsigned char>(p_[0]) == 0xEF && static_cast<unsigned char>(p_[1]) == 0xBB && static_cast<unsigned char>(p_[2]) == 0xBF) p_ += 3;
        if (!Val(out)) return false;
        Ws();
        return p_ == e_;
    }
};

inline bool Plain(const std::string& type) {
    return type == "TransformDefinitionComponent" || type == "ModelPlatformTierComponent" || type == "ModelDefinitionComponent" || type == "WallTransparencyComponent";
}

// The knockable props of one preset's text: (id, kind, preset position) rows appended to out. False when the text is not a preset.
inline bool PropsOf(const std::string& text, std::vector<propscene::Row>& out) {
    Value root;
    if (!Parser(text.data(), text.size()).Parse(root) || root.type != Value::Object) return false;
    const Value* ents = root.Get("entities");
    if (!ents || ents->type != Value::Array) return true;
    for (const Value& e : ents->items) {
        if (e.type != Value::Object) continue;
        const Value* comps = e.Get("components");
        const Value* id = e.Get("id");
        if (!comps || comps->type != Value::Array || comps->items.empty() || !id || id->type != Value::Number) continue;
        if (!(id->n > 0 && id->n < 4294967296.0) || id->n != static_cast<double>(static_cast<uint64_t>(id->n))) continue;
        bool plain = true;
        std::string model;
        const Value* transform = nullptr;
        for (const Value& c : comps->items) {
            if (c.type != Value::Object) { plain = false; break; }
            const Value* t = c.Get("type");
            const std::string type = t && t->type == Value::String ? t->s : std::string();
            if (type == "PhysicsBodyDefinitionComponent") {
                const Value* bt = c.Get("bodytype");
                if (!bt || bt->type != Value::String || bt->s != "static") { plain = false; break; }
                continue;
            }
            if (!Plain(type)) { plain = false; break; }
            if (type == "ModelDefinitionComponent") {
                const Value* f = c.Get("filename");
                if (f && f->type == Value::String) model = f->s;
            }
            if (type == "TransformDefinitionComponent") transform = &c;
        }
        if (!plain || model.empty() || !transform) continue;
        const int kind = propscene::KindOfModel(model);
        if (kind < 0) continue;
        const Value* pos = transform->Get("position");
        if (!pos || pos->type != Value::Object) continue;
        const Value* x = pos->Get("x");
        const Value* y = pos->Get("y");
        const Value* z = pos->Get("z");
        if (!x || !y || !z || x->type != Value::Number || y->type != Value::Number || z->type != Value::Number) continue;
        if (std::abs(x->n) >= 1e6 || std::abs(y->n) >= 1e6 || std::abs(z->n) >= 1e6) continue;
        out.push_back({static_cast<uint32_t>(id->n), kind, static_cast<float>(x->n), static_cast<float>(y->n), static_cast<float>(z->n)});
    }
    return true;
}

}  // namespace propspresets
