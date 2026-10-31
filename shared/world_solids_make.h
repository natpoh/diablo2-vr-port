// world_solids_make.h - [weight] world_contact: a model's solid parts made from its meshes (shared/world_solids.h says how and why) - D2R VR Settings'
// (tools/settings/solids_assets.cpp) and the tests; the plugin only reads the catalogue.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "granny_geometry.h"
#include "world_solids.h"

namespace worldsolids {

// Is a mesh of the model solid? A tree's leaves (its crown) are not.
inline bool UsedMesh(const grannygeom::Mesh& m) {
    const std::string n = Lower(m.name) + " " + Lower(m.material);
    for (const char* w : {"leaf", "leaves", "foliage", "canopy", "frond", "needle", "card", "billboard"})
        if (n.find(w) != std::string::npos) return false;
    return true;
}

// The model's parts from its meshes (those UsedMesh takes, the render layer 1 ones when any is). False when no triangle is there.
inline bool Make(const std::vector<grannygeom::Mesh>& meshes, Model* out) {
    out->parts.clear();
    bool anyLayer1 = false;
    for (const auto& m : meshes) anyLayer1 = anyLayer1 || (UsedMesh(m) && (m.layer & 1));
    std::vector<const grannygeom::Mesh*> used;
    for (const auto& m : meshes)
        if (UsedMesh(m) && (!anyLayer1 || (m.layer & 1)) && !m.tri.empty()) used.push_back(&m);
    if (used.empty()) return false;
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const auto* m : used)
        for (uint32_t i : m->tri)
            for (int c = 0; c < 3; ++c) {
                const float v = m->pos[(size_t)i * 3 + c];
                lo[c] = std::min(lo[c], v);
                hi[c] = std::max(hi[c], v);
            }
    for (int c = 0; c < 3; ++c)
        if (!(hi[c] >= lo[c]) || !std::isfinite(lo[c]) || !std::isfinite(hi[c])) return false;
    std::memcpy(out->lo, lo, sizeof lo);
    std::memcpy(out->hi, hi, sizeof hi);
    const float longest = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
    float cell = std::max(kMinCellUnits, longest / (float)kGridCells);
    for (int attempt = 0; attempt < 6; ++attempt, cell *= 1.4f) {
        int n[3];
        for (int c = 0; c < 3; ++c) n[c] = std::max(1, (int)std::ceil((hi[c] - lo[c]) / cell + 1e-4f));
        const size_t total = (size_t)n[0] * n[1] * n[2];
        if (total > (size_t)8 << 20) continue;
        std::vector<uint8_t> grid(total, 0);
        auto at = [&](int x, int y, int z) -> uint8_t& { return grid[((size_t)z * n[1] + y) * n[0] + x]; };
        auto mark = [&](const float p[3]) {
            int ix[3];
            for (int c = 0; c < 3; ++c) ix[c] = std::clamp((int)std::floor((p[c] - lo[c]) / cell), 0, n[c] - 1);
            at(ix[0], ix[1], ix[2]) = 1;
        };
        // every triangle sampled half a cell apart
        for (const auto* m : used)
            for (size_t t = 0; t + 2 < m->tri.size(); t += 3) {
                const float* a = &m->pos[(size_t)m->tri[t] * 3];
                const float* b = &m->pos[(size_t)m->tri[t + 1] * 3];
                const float* c3 = &m->pos[(size_t)m->tri[t + 2] * 3];
                float edge = 0;
                for (int c = 0; c < 3; ++c) edge = std::max({edge, std::fabs(b[c] - a[c]), std::fabs(c3[c] - a[c]), std::fabs(c3[c] - b[c])});
                const int k = std::clamp((int)std::ceil(edge / (cell * 0.5f)), 1, 4096);
                for (int i = 0; i <= k; ++i)
                    for (int j = 0; i + j <= k; ++j) {
                        const float u = (float)i / k, v = (float)j / k;
                        const float p[3] = {a[0] + (b[0] - a[0]) * u + (c3[0] - a[0]) * v, a[1] + (b[1] - a[1]) * u + (c3[1] - a[1]) * v,
                                            a[2] + (b[2] - a[2]) * u + (c3[2] - a[2]) * v};
                        mark(p);
                    }
            }
        // the inside: an empty cell with a solid one on each of its six sides somewhere along the axis
        std::vector<uint8_t> walled(total, 0);
        auto wallAt = [&](int x, int y, int z) -> uint8_t& { return walled[((size_t)z * n[1] + y) * n[0] + x]; };
        for (int axis = 0; axis < 3; ++axis) {
            const int a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
            for (int i = 0; i < n[a1]; ++i)
                for (int j = 0; j < n[a2]; ++j) {
                    auto cellOf = [&](int s, int* xyz) { xyz[axis] = s; xyz[a1] = i; xyz[a2] = j; };
                    int first = -1, last = -1;
                    for (int s = 0; s < n[axis]; ++s) {
                        int q[3];
                        cellOf(s, q);
                        if (at(q[0], q[1], q[2])) { if (first < 0) first = s; last = s; }
                    }
                    for (int s = 0; s < n[axis]; ++s) {
                        int q[3];
                        cellOf(s, q);
                        if (first >= 0 && s > first && s < last) wallAt(q[0], q[1], q[2]) += 1;
                    }
                }
        }
        for (size_t i = 0; i < total; ++i)
            if (walled[i] == 3) grid[i] = 1;
        // the solid cells joined into boxes
        std::vector<Part> parts;
        std::vector<int> sizes;
        for (int z = 0; z < n[2]; ++z)
            for (int y = 0; y < n[1]; ++y)
                for (int x = 0; x < n[0]; ++x) {
                    if (at(x, y, z) != 1) continue;
                    int x1 = x;
                    while (x1 + 1 < n[0] && at(x1 + 1, y, z) == 1) ++x1;
                    auto rowFull = [&](int yy, int zz) {
                        for (int xx = x; xx <= x1; ++xx) if (at(xx, yy, zz) != 1) return false;
                        return true;
                    };
                    int y1 = y;
                    while (y1 + 1 < n[1] && rowFull(y1 + 1, z)) ++y1;
                    int z1 = z;
                    auto slabFull = [&](int zz) {
                        for (int yy = y; yy <= y1; ++yy) if (!rowFull(yy, zz)) return false;
                        return true;
                    };
                    while (z1 + 1 < n[2] && slabFull(z1 + 1)) ++z1;
                    for (int zz = z; zz <= z1; ++zz)
                        for (int yy = y; yy <= y1; ++yy)
                            for (int xx = x; xx <= x1; ++xx) at(xx, yy, zz) = 2;
                    Part p;
                    const int i0[3] = {x, y, z}, i1[3] = {x1, y1, z1};
                    for (int c = 0; c < 3; ++c) {
                        p.lo[c] = std::max(lo[c], lo[c] + i0[c] * cell);
                        p.hi[c] = std::min(hi[c], lo[c] + (i1[c] + 1) * cell);
                        if (p.hi[c] - p.lo[c] < cell * 0.25f) {   // (a flat model's one cell: as thick as a quarter cell round its middle)
                            const float mid = (p.lo[c] + p.hi[c]) * 0.5f;
                            p.lo[c] = mid - cell * 0.125f;
                            p.hi[c] = mid + cell * 0.125f;
                        }
                    }
                    parts.push_back(p);
                    sizes.push_back((x1 - x + 1) * (y1 - y + 1) * (z1 - z + 1));
                }
        if ((int)parts.size() > kMaxParts && attempt < 5) continue;
        if ((int)parts.size() > kMaxParts) {   // (still too many: the biggest kept)
            std::vector<size_t> order(parts.size());
            for (size_t i = 0; i < order.size(); ++i) order[i] = i;
            std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return sizes[a] > sizes[b]; });
            std::vector<Part> keep;
            for (int i = 0; i < kMaxParts; ++i) keep.push_back(parts[order[(size_t)i]]);
            parts.swap(keep);
        }
        out->parts = std::move(parts);
        out->cell = cell;
        return true;
    }
    return false;
}

}  // namespace worldsolids
