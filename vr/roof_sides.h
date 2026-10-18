// roof_sides.h - [render] roofs_two_sided: a roof's material is built double-sided, so a roof kept over you ([render] keep_roofs,
// vr/roof_keep.h) shows its underside instead of the sky (vr/roof_sides.cpp).
//
// The game draws a roof only from above: its camera never looks up at one, and from below the triangles are culled and the sky
// shows through. Each model material is built once, when its model loads (the material builder, RoofMaterialBuild 0xF99C40: model
// path, file, mesh, Granny material, flags, pass), and the builder asks the material's own Granny data for its "IsDoubleSided"
// member (GrannyFindMember 0x16153A0, called at 0xF9AEDA). While a roof material is being built that one answer is 1 instead of 0:
// the builder then makes its own double-sided variant (culling off, the shader's two-sided path). Only the caller's copy of the
// answer is redirected (the member's value pointer the lookup hands back), never the material in the game's data; every other
// material, and every other member asked, keep the game's own.
//
// Which materials: a roof's own model (its file name has the token "roof": act3_..._roof01.model) AND the material on it named so
// (ext_roof_tile01, slum_roof_b) - a material's name alone is not enough, a wall model's "roof trim" stays as it is. Only the
// environment's models (data/hd/env/model/act1 - act5). No list of names by hand.
//
// When: the tick is read when a material is built - at an area's load. Models the game already has loaded keep their sides until it
// loads them again (another area, or the next start of the game). VR only (the material is built once for every view: from above
// a roof's underside is never in sight, so F1 - F3 look the same).
//
// Found by bearstonem/d2r-pcvr (MIT, v0.3.18: vr/roof_material_rules.h, vr/roof_materials.cpp); written again here on our signatures
// (game_sigs.h, area "Roofs, hanging props"), without its hand-picked list of the Lut Gholein tavern's walls.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace roofsides {

// Addresses in 3.3.93787 (the keys of game_sigs.h) and the bytes each starts with there.
constexpr uint64_t kBuildRva = 0xF99C40, kFindRva = 0x16153A0;
constexpr uint64_t kBuildCallRva = 0xFA17A8, kFlagCallRva = 0xF9AEDA, kFindResultRva = 0x16317C3;
// the material builder: mov [rsp+8], rbx; mov [rsp+10h], rbp; mov [rsp+18h], rsi; push rdi; push r14; push r15; sub rsp, 40h
constexpr uint8_t kBuildSig[24] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x40};
// GrannyFindMember: the same prologue, push rdi; sub rsp, 40h
constexpr uint8_t kFindSig[20] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x40};
// a model's materials built: the six arguments (two on the stack), then call the builder
constexpr uint8_t kBuildCallSig[20] = {0x4C,0x8B,0xCF,0x89,0x44,0x24,0x28,0x41,0x8B,0x04,0x24,0x89,0x44,0x24,0x20,0xE8,0x84,0x84,0xFF,0xFF};
// the builder asks the material's type and object for a member by name, then reads the byte it points to (the flag)
constexpr uint8_t kFlagCallSig[46] = {0x49,0x8B,0x56,0x24,0x4C,0x8D,0x8D,0xF0,0x03,0x00,0x00,0x49,0x8B,0x4E,0x1C,0x4C,0x8D,0x05,0xF8,0xF0,0xE7,0x00,
                                      0x40,0x88,0x74,0x24,0x45,0xE8,0xA6,0xA4,0x67,0x00,0x84,0xC0,0x74,0x10,0x48,0x8B,0x85,0xF8,0x03,0x00,0x00,0x0F,0xB6,0x00};
// the lookup hands back (member definition, member value) - pointers, not a copy: test rdi, rdi; je; mov [rdi], rbx; mov [rdi+8], rsi; mov al, 1
constexpr uint8_t kFindResultSig[14] = {0x48,0x85,0xFF,0x74,0x07,0x48,0x89,0x1F,0x48,0x89,0x77,0x08,0xB0,0x01};

inline int32_t Rel32(const uint8_t* p) {
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}
// The model's build call reaches the builder and the builder's flag call reaches the lookup (from the sigs' own rel32s).
inline bool CallsReach() {
    const int64_t build = (int64_t)kBuildCallRva + 20 + Rel32(kBuildCallSig + 16);
    const int64_t find = (int64_t)kFlagCallRva + 32 + Rel32(kFlagCallSig + 28);
    return build == (int64_t)kBuildRva && find == (int64_t)kFindRva;
}

// A path as the game names it, made plain: lower case, '/' for '\', "data:" read as "data/".
template <size_t N>
inline bool Canonical(std::string_view in, char (&out)[N], std::string_view* view) {
    if (in.size() >= N) return false;
    for (size_t i = 0; i < in.size(); ++i) {
        const unsigned char c = (unsigned char)in[i];
        if (c < 32 || c >= 127) return false;
        out[i] = c == '\\' ? '/' : (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : (char)c;
    }
    if (in.size() >= 5 && std::string_view(out, 5) == "data:") out[4] = '/';
    *view = std::string_view(out, in.size());
    return true;
}

// "roof" as a word of a name: at its start or after '_' or '/', and followed by its end, '_', '/' or a digit
// (roof, slum_roof_b, roof01 - not "roofless" or "proof").
inline bool RoofToken(std::string_view s) {
    for (size_t at = s.find("roof"); at != std::string_view::npos; at = s.find("roof", at + 4)) {
        const size_t end = at + 4;
        const bool before = at == 0 || s[at - 1] == '_' || s[at - 1] == '/';
        const bool after = end == s.size() || s[end] == '_' || s[end] == '/' || (s[end] >= '0' && s[end] <= '9');
        if (before && after) return true;
    }
    return false;
}

// A roof material of a roof model of the environment (both made plain by Canonical first).
inline bool Selected(std::string_view model, std::string_view material) {
    constexpr std::string_view prefix = "data/hd/env/model/act";
    if (!model.starts_with(prefix) || model.size() <= prefix.size() + 2) return false;
    const char act = model[prefix.size()];
    if (act < '1' || act > '5' || model[prefix.size() + 1] != '/') return false;
    if (model.find("..") != std::string_view::npos || !model.ends_with(".model")) return false;
    std::string_view file = model.substr(model.find_last_of('/') + 1);
    file.remove_suffix(6);   // ".model"
    return RoofToken(file) && RoofToken(material);
}

// The build of this material is to be double-sided: the tick on (and VR), and a roof's.
inline bool Chosen(bool wanted, std::string_view model, std::string_view material) { return wanted && Selected(model, material); }

// The answer to change: inside the build of a chosen material, the lookup of THAT material's "IsDoubleSided", a 32-bit or 8-bit
// boolean member (Granny kinds 19, 20) that says 0. Anything else - another material's build nested inside, another member, a member
// that is already 1 - stays the game's own.
struct Context { const void* type = nullptr; const void* object = nullptr; unsigned changes = 0; };
inline bool Override(const Context* ctx, const void* type, const void* object, std::string_view member, int kind, int width, int value) {
    return ctx && ctx->type && ctx->object && ctx->type == type && ctx->object == object && member == "IsDoubleSided" &&
           (kind == 19 || kind == 20) && (width == 0 || width == 1) && value == 0;
}

// The build in progress on this thread, for its own lookups only: a nested build of a material not chosen clears it, and the outer
// one gets it back.
struct Scope {
    Context*& slot;
    Context* previous;
    Scope(Context*& s, Context* next) : slot(s), previous(s) { slot = next; }
    ~Scope() { slot = previous; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

// ---- the game side (vr/roof_sides.cpp) ----
using LogFn = void (*)(const char* line);
using WantFn = bool (*)();   // the tick (and VR), asked at each material build
// vrcam's update thread, twice a second while the tick is on: both hooks in once the game's code is there (decrypted); retried until.
void Install(LogFn log, WantFn want);

}  // namespace roofsides
