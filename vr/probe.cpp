// probe.cpp - one-shot, read-only search for the hero's Granny skeleton and
// pose buffers in the running game, for driving the arms from the controllers.
//
// Nothing here writes to the game. It walks committed readable memory, always
// through SafeCopy into its own buffers (another thread may free a page while
// we look). Triggered by F10; results in d2r_vr_probe.txt next to the plugin.
//
// v1 (2026-10-02) found ~1400 "bone arrays" around "R_weapon_attach" and no
// matrix runs at the hero: most arrays were animation event lists and sorted
// name tables, and the parent index is not right after the name. v2:
//  1. names -> arrays as before, but kept only where some int32 field at a
//     fixed offset forms a real hierarchy (root -1, every parent < index);
//     the offset, raw bytes of the first entries, and the bone names go out;
//  2. who points at each kept array, the structs around those pointers, and
//     who points at THOSE (skeleton -> skeleton instance);
//  3. matrix runs near the hero in three layouts: row-major 4x4, column-major
//     4x4, and 3x4 rows (48 bytes), compared on the ground plane only.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <share.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

namespace probe {

namespace {

#pragma optimize("", off)
bool SafeCopy(void* dst, const void* src, size_t n) noexcept {
    __try { memcpy(dst, src, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
#pragma optimize("", on)

struct Region { uintptr_t base; size_t size; };

std::vector<Region> Regions() {
    std::vector<Region> out;
    MEMORY_BASIC_INFORMATION mbi{};
    for (uintptr_t a = 0x10000; a < 0x7FFFFFFF0000ull; a = (uintptr_t)mbi.BaseAddress + mbi.RegionSize) {
        if (!VirtualQuery((void*)a, &mbi, sizeof mbi)) break;
        const DWORD p = mbi.Protect & 0xFF;
        const bool readable = p == PAGE_READWRITE || p == PAGE_READONLY || p == PAGE_WRITECOPY || p == PAGE_EXECUTE_READWRITE;
        if (mbi.State == MEM_COMMIT && readable && !(mbi.Protect & (PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE)))
            out.push_back({(uintptr_t)mbi.BaseAddress, mbi.RegionSize});
    }
    return out;
}

template <class T> bool Get(uintptr_t a, T* v) { return SafeCopy(v, (const void*)a, sizeof(T)); }

// A short printable C string at p, or empty.
std::string ShortString(uintptr_t p) {
    if (p < 0x10000 || p > 0x7FFFFFFFFFFFull) return {};
    char buf[64];
    int n = 0;
    for (; n < 63; ++n) {
        if (!SafeCopy(buf + n, (const void*)(p + n), 1)) return {};
        if (buf[n] == 0) break;
        if (buf[n] < 32 || buf[n] > 126) return {};
    }
    if (n < 2 || n >= 63) return {};
    return std::string(buf, n);
}

// Every region, a chunk at a time, copied into one buffer: f(buffer, address of buffer[0], bytes).
template <class F> void ForEachChunk(const std::vector<Region>& rs, F f) {
    constexpr size_t kChunk = 32u << 20;
    thread_local std::vector<uint8_t> buf(kChunk);   // the facing search and F10 may run at once
    for (const Region& r : rs)
        for (size_t off = 0; off < r.size; off += kChunk) {
            const size_t n = std::min(kChunk, r.size - off);
            if (SafeCopy(buf.data(), (const void*)(r.base + off), n)) f(buf.data(), r.base + off, n);
        }
}

template <class F> void ForEachQword(const std::vector<Region>& rs, F f) {
    ForEachChunk(rs, [&](const uint8_t* b, uintptr_t at, size_t n) {
        const uint64_t* q = (const uint64_t*)b;
        for (size_t i = 0; i < n / 8; ++i) f(at + i * 8, q[i]);
    });
}

std::string NameAt(uintptr_t field) { uint64_t v = 0; return Get(field, &v) ? ShortString((uintptr_t)v) : std::string(); }

struct Skel {
    uintptr_t bones; size_t stride; int count; int parentOff;
    std::vector<std::string> names; std::vector<int> parents;
};

// Points of one field across the array as a hierarchy: root -1, parents earlier.
bool Hierarchy(uintptr_t base, size_t stride, int count, int off, std::vector<int>* out) {
    std::vector<int> p(count);
    int deeper = 0;
    for (int i = 0; i < count; ++i) {
        int32_t v = 0;
        if (!Get(base + i * stride + off, &v)) return false;
        if (i == 0 ? v != -1 : (v < -1 || v >= i)) return false;
        if (v > 0) ++deeper;
        p[i] = v;
    }
    if (deeper < count / 3) return false;   // a flat list is not a skeleton
    if (out) *out = std::move(p);
    return true;
}

void Hex(FILE* f, uintptr_t a, size_t n, const char* indent) {
    std::vector<uint8_t> b(n);
    if (!SafeCopy(b.data(), (const void*)a, n)) { fprintf(f, "%s(unreadable)\n", indent); return; }
    for (size_t i = 0; i < n; i += 16) {
        fprintf(f, "%s+%03zX:", indent, i);
        for (size_t k = 0; k < 16 && i + k < n; k += 4) {
            uint32_t v; memcpy(&v, &b[i + k], 4); float fl; memcpy(&fl, &v, 4);
            fprintf(f, " %08X", v);
            if (std::isfinite(fl) && fabsf(fl) > 1e-4f && fabsf(fl) < 1e6f) fprintf(f, "(%.3g)", fl);
        }
        fprintf(f, "\n");
    }
}

// Translation of a matrix in each layout, or false.
bool RowMajor(const float* m, float t[3]) {   // translation in [12..14], last column 0 0 0 1
    if (m[3] != 0 || m[7] != 0 || m[11] != 0 || m[15] != 1) return false;
    t[0] = m[12]; t[1] = m[13]; t[2] = m[14]; return true;
}
bool ColMajor(const float* m, float t[3]) {   // translation in [3],[7],[11], last row 0 0 0 1
    if (m[12] != 0 || m[13] != 0 || m[14] != 0 || m[15] != 1) return false;
    t[0] = m[3]; t[1] = m[7]; t[2] = m[11]; return true;
}
bool Rows3x4(const float* m, float t[3]) {    // three rows of (r r r t), 48 bytes, no fourth row
    for (int r = 0; r < 3; ++r) {
        const float n2 = m[r * 4] * m[r * 4] + m[r * 4 + 1] * m[r * 4 + 1] + m[r * 4 + 2] * m[r * 4 + 2];
        if (!(n2 > 0.04f && n2 < 25.0f)) return false;
    }
    t[0] = m[3]; t[1] = m[7]; t[2] = m[11]; return true;
}

}  // namespace

void Run(const float hero[3], const wchar_t* outPath) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, outPath, L"w") || !f) return;
    const ULONGLONG t0 = GetTickCount64();
    fprintf(f, "probe v2 - hero position %.3f %.3f %.3f\n", hero[0], hero[1], hero[2]);

    const std::vector<Region> rs = Regions();
    size_t total = 0; for (auto& r : rs) total += r.size;
    fprintf(f, "%zu readable regions, %.0f MB\n", rs.size(), total / 1048576.0);
    fflush(f);

    // 1. names -> arrays -> only real hierarchies
    static const char kName[] = "R_weapon_attach";
    std::vector<uintptr_t> strs;
    ForEachChunk(rs, [&](const uint8_t* b, uintptr_t at, size_t n) {
        for (size_t i = 0; i + sizeof kName <= n; ++i)
            if (b[i] == 'R' && memcmp(b + i, kName, sizeof kName) == 0) strs.push_back(at + i);
    });
    if (strs.empty()) { fprintf(f, "no 'R_weapon_attach'\n"); fclose(f); return; }
    std::unordered_set<uintptr_t> strSet(strs.begin(), strs.end());
    const uintptr_t lo = *std::min_element(strs.begin(), strs.end()), hi = *std::max_element(strs.begin(), strs.end());
    std::vector<uintptr_t> nameFields;
    ForEachQword(rs, [&](uintptr_t at, uint64_t v) { if (v >= lo && v <= hi && strSet.count((uintptr_t)v)) nameFields.push_back(at); });
    fprintf(f, "'R_weapon_attach': %zu strings, %zu pointers\n", strs.size(), nameFields.size());

    std::vector<Skel> skels;
    std::unordered_set<uintptr_t> seenBase;
    int arrays = 0;
    for (uintptr_t nf : nameFields) {
        size_t bestStride = 0; int bestRun = 0;
        for (size_t stride = 0x20; stride <= 0x200; stride += 8) {
            int run = 0;
            for (int k = 1; k < 8; ++k) {
                if (NameAt(nf - k * stride).empty() && NameAt(nf + k * stride).empty()) break;
                ++run;
            }
            if (run > bestRun) { bestRun = run; bestStride = stride; }
        }
        if (bestRun < 3) continue;
        uintptr_t base = nf;
        while (!NameAt(base - bestStride).empty()) base -= bestStride;
        if (!seenBase.insert(base).second) continue;
        ++arrays;
        std::vector<std::string> names;
        for (uintptr_t b = base; names.size() < 600; b += bestStride) {
            std::string n = NameAt(b);
            if (n.empty()) break;
            names.push_back(std::move(n));
        }
        const int count = (int)names.size();
        if (count < 10) continue;
        for (int off = 8; off <= 0x40 && off + 4 <= (int)bestStride; off += 4) {
            std::vector<int> parents;
            if (!Hierarchy(base, bestStride, count, off, &parents)) continue;
            skels.push_back({base, bestStride, count, off, std::move(names), std::move(parents)});
            break;
        }
    }
    fprintf(f, "arrays around the name: %d, with a real hierarchy: %zu\n", arrays, skels.size());
    fflush(f);

    // 2. owners of each skeleton, and owners of the owners
    std::unordered_set<uintptr_t> bases; for (auto& s : skels) bases.insert(s.bones);
    std::vector<std::pair<uintptr_t, uintptr_t>> owners;
    ForEachQword(rs, [&](uintptr_t at, uint64_t v) { if (bases.count((uintptr_t)v)) owners.push_back({at, (uintptr_t)v}); });
    // struct starts a little before the Bones field: look for pointers to O-0x18..O
    std::unordered_set<uintptr_t> ownerStarts;
    for (auto& o : owners) for (int k = 0; k <= 0x18; k += 8) ownerStarts.insert(o.first - k);
    std::vector<std::pair<uintptr_t, uintptr_t>> holders;
    ForEachQword(rs, [&](uintptr_t at, uint64_t v) { if (ownerStarts.count((uintptr_t)v)) holders.push_back({at, (uintptr_t)v}); });

    std::map<std::string, int> printedKinds;   // one full dump per distinct skeleton
    for (const Skel& s : skels) {
        std::string kind = std::to_string(s.count) + ":" + s.names[0] + ":" + s.names[s.count / 2];
        const bool full = printedKinds[kind]++ < 1;
        fprintf(f, "\n== skeleton bones @%p stride 0x%zX count %d parent@+0x%X %s\n", (void*)s.bones, s.stride, s.count, s.parentOff,
                full ? "" : "(same kind as above)");
        for (auto& o : owners) if (o.second == s.bones) {
            int32_t cnt = -1; Get(o.first - 8, &cnt);
            fprintf(f, "   owner field %p (name@-16 '%s', int@-8 %d)\n", (void*)o.first, NameAt(o.first - 16).c_str(), cnt);
            if (full) Hex(f, o.first - 0x20, 0x60, "      ");
            for (auto& h : holders) if (h.second >= o.first - 0x18 && h.second <= o.first) {
                fprintf(f, "      held by %p (-> %p, owner-0x%llX)\n", (void*)h.first, (void*)h.second, (unsigned long long)(o.first - h.second));
                if (full) Hex(f, h.first - 0x40, 0x100, "         ");
            }
        }
        if (!full) continue;
        fprintf(f, "   first entries:\n");
        Hex(f, s.bones, std::min<size_t>(s.stride * 2, 0x200), "      ");
        for (int i = 0; i < s.count; ++i) fprintf(f, "   [%3d] parent %3d  %s\n", i, s.parents[i], s.names[i].c_str());
    }
    fflush(f);

    // 3. matrix runs near the hero, three layouts, ground plane only
    struct Run_ { uintptr_t start; int len; int layout; float t[3]; };
    std::vector<Run_> runs;
    static const char* kLayout[] = {"row-major 4x4", "column-major 4x4", "3x4 rows"};
    for (int layout = 0; layout < 3; ++layout) {
        const size_t sz = layout == 2 ? 48 : 64;
        auto test = [&](const float* m, float t[3]) { return layout == 0 ? RowMajor(m, t) : layout == 1 ? ColMajor(m, t) : Rows3x4(m, t); };
        ForEachChunk(rs, [&](const uint8_t* b, uintptr_t at, size_t n) {
            const size_t first = (16 - (at & 15)) & 15;
            for (size_t i = first; i + sz <= n; i += 16) {
                float t[3];
                if (!test((const float*)(b + i), t)) continue;
                if (!std::isfinite(t[0]) || fabsf(t[0] - hero[0]) > 15 || fabsf(t[2] - hero[2]) > 15) continue;
                size_t s0 = i, s1 = i; float tt[3];
                while (s0 >= sz && test((const float*)(b + s0 - sz), tt)) s0 -= sz;
                while (s1 + 2 * sz <= n && test((const float*)(b + s1 + sz), tt)) s1 += sz;
                const int len = (int)((s1 - s0) / sz) + 1;
                if (len >= 8) runs.push_back({at + s0, len, layout, {t[0], t[1], t[2]}});
                i = s1 + sz - 16;
            }
        });
    }
    fprintf(f, "\nmatrix runs near the hero (>= 8): %zu\n", runs.size());
    std::unordered_set<uintptr_t> runStarts; for (auto& r : runs) runStarts.insert(r.start);
    std::vector<std::pair<uintptr_t, uintptr_t>> runOwners;
    ForEachQword(rs, [&](uintptr_t at, uint64_t v) { if (runStarts.count((uintptr_t)v)) runOwners.push_back({at, (uintptr_t)v}); });
    int shown = 0;
    for (auto& r : runs) {
        if (++shown > 200) break;
        fprintf(f, "  run @%p %s len %d (a matrix at %.2f %.2f %.2f)", (void*)r.start, kLayout[r.layout], r.len, r.t[0], r.t[1], r.t[2]);
        for (const Skel& s : skels) if (s.count == r.len) { fprintf(f, "  <- bone count of @%p", (void*)s.bones); break; }
        fprintf(f, "\n");
        for (auto& o : runOwners) if (o.second == r.start) {
            fprintf(f, "     pointed from %p\n", (void*)o.first);
            Hex(f, o.first - 0x30, 0x80, "        ");
        }
    }
    fprintf(f, "\ndone in %llu ms\n", (unsigned long long)(GetTickCount64() - t0));
    fclose(f);
}

// The hero's model-to-world matrix as the renderer keeps it (found 2026-10-02):
// row-major, rows (c*s, 0, -?*s, 0) (0, s, 0, 0) (.., 0, .., 0) (x, y, z, 1) -
// a turn about the vertical with a uniform scale s (0.93 for the paladin), the
// translation at the hero. Several copies exist, one per mesh piece; any will do.
// Returns its address or 0. Reads only.
// Every matrix in memory that reads as "the hero, turned about the vertical":
// translation on the hero, scale s, rotation about y only. Several match (the
// first one found never turned - 2026-10-02 log: model 0.0 while the hero
// walked every way), so all go back and vrcam watches which one turns.
// Near-zero off-axis terms are allowed: a matrix built from a quaternion is
// rarely exactly 0 there.
int FindHeroMatrices(const float hero[3], uintptr_t* out, int max) {
    const std::vector<Region> rs = Regions();
    int found = 0;
    ForEachChunk(rs, [&](const uint8_t* b, uintptr_t at, size_t n) {
        if (found >= max) return;
        const size_t first = (16 - (at & 15)) & 15;
        for (size_t i = first; i + 64 <= n && found < max; i += 16) {
            const float* m = (const float*)(b + i);
            if (m[15] != 1.0f || fabsf(m[12] - hero[0]) > 2.0f || fabsf(m[14] - hero[2]) > 2.0f) continue;
            const float s = m[5];
            if (!(s > 0.3f && s < 3.0f)) continue;
            const float eps = 1e-3f * s;
            if (fabsf(m[1]) > eps || fabsf(m[3]) > 1e-6f || fabsf(m[4]) > eps || fabsf(m[6]) > eps || fabsf(m[7]) > 1e-6f ||
                fabsf(m[9]) > eps || fabsf(m[11]) > 1e-6f) continue;
            if (fabsf(m[0] * m[0] + m[2] * m[2] - s * s) > 0.05f * s * s || fabsf(m[8] * m[8] + m[10] * m[10] - s * s) > 0.05f * s * s) continue;
            out[found++] = at + i;
        }
    });
    return found;
}

// Every place in memory holding the hero's x and z (z right after x, or after
// one float), with the floats around it, appended under `tag`. Two runs with
// the hero facing different ways show which spot also carries the facing.
void FindHero(const float hero[3], const wchar_t* outPath, const char* tag) {
    FILE* f = _wfsopen(outPath, L"a", _SH_DENYWR);
    if (!f) return;
    const ULONGLONG t0 = GetTickCount64();
    fprintf(f, "\n##### %s - hero %.3f %.3f %.3f\n", tag, hero[0], hero[1], hero[2]);
    const std::vector<Region> rs = Regions();
    int hits = 0;
    ForEachChunk(rs, [&](const uint8_t* b, uintptr_t at, size_t n) {
        const size_t first = (4 - (at & 3)) & 3;
        for (size_t i = first; i + 12 <= n && hits < 600; i += 4) {
            float x; memcpy(&x, b + i, 4);
            if (fabsf(x - hero[0]) > 2.0f) continue;
            float z1, z2; memcpy(&z1, b + i + 4, 4); memcpy(&z2, b + i + 8, 4);
            const int zoff = fabsf(z1 - hero[2]) < 2.0f ? 4 : fabsf(z2 - hero[2]) < 2.0f ? 8 : 0;
            if (!zoff) continue;
            ++hits;
            fprintf(f, "HIT %p (z at +%d)\n", (void*)(at + i), zoff);
            const size_t s = i >= 0x40 ? i - 0x40 : 0, e = std::min(n, i + 0x60);
            for (size_t k = s & ~(size_t)15; k + 16 <= e; k += 16) {
                float v[4]; memcpy(v, b + k, 16);
                fprintf(f, "   %+05d: %12.5f %12.5f %12.5f %12.5f\n", (int)k - (int)i, v[0], v[1], v[2], v[3]);
            }
        }
    });
    fprintf(f, "%d hits in %llu ms\n", hits, (unsigned long long)(GetTickCount64() - t0));
    fclose(f);
}

}  // namespace probe
