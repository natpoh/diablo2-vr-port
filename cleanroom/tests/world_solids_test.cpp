// [weight] world_contact: the level pieces' solid parts made from their meshes (shared/world_solids.h, shared/granny_geometry.h).
//
//   world_solids_test                      the synthetic checks
//   world_solids_test <file.model> ...     ... and each of the game's own model files: its meshes (name, material, layer, vertices, triangles, bounds) and the
//                                          parts made of it; a file whose name has "doorway" / "gate" also checks its opening stays open
//
// Checks: a doorway of three boxes' faces keeps its opening and its jambs are solid; a closed box is filled inside (one part); an L corner is two parts and
// the inside of the L is free; a flat quad is one thin part; a tree's leaf meshes and a bush's path are left out; the catalogue writes, reads, finds an entity
// by its id and place and picks the variant whose bounds are the renderer's.

#include "world_solids_make.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_fails = 0;

#define EXPECT(c, ...)                                         \
	do {                                                       \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
			std::printf(__VA_ARGS__);                          \
			std::printf("\n");                                 \
		}                                                      \
	} while (0)

// The six faces of a box as twelve triangles, appended to a mesh.
void AddBox(grannygeom::Mesh& m, float x0, float y0, float z0, float x1, float y1, float z1) {
	const uint32_t base = (uint32_t)(m.pos.size() / 3);
	for (int i = 0; i < 8; ++i) {
		m.pos.push_back(i & 1 ? x1 : x0);
		m.pos.push_back(i & 2 ? y1 : y0);
		m.pos.push_back(i & 4 ? z1 : z0);
	}
	const int q[6][4] = { { 0, 1, 3, 2 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 3, 7, 5 } };
	for (const auto& f : q) {
		for (int k : { f[0], f[1], f[2], f[0], f[2], f[3] }) m.tri.push_back(base + (uint32_t)k);
	}
}

bool Solid(const worldsolids::Model& m, float x, float y, float z) {
	for (const auto& p : m.parts)
		if (x >= p.lo[0] && x <= p.hi[0] && y >= p.lo[1] && y <= p.hi[1] && z >= p.lo[2] && z <= p.hi[2]) return true;
	return false;
}

void TestDoorway() {
	// a wall 10 units wide (x), 15 tall (y), 3 thick (z) with an opening 4 wide and 10 tall in its middle: two jambs and a lintel, each a closed box
	grannygeom::Mesh m;
	AddBox(m, -5, 0, -1.5f, -2, 15, 1.5f);
	AddBox(m, 2, 0, -1.5f, 5, 15, 1.5f);
	AddBox(m, -2, 10, -1.5f, 2, 15, 1.5f);
	worldsolids::Model out;
	const bool ok = worldsolids::Make({ m }, &out);
	std::printf("doorway: %d parts, cell %.2f; opening solid at its middle: %d, a jamb's middle: %d, the lintel's: %d\n", (int)out.parts.size(), out.cell,
	            Solid(out, 0, 5, 0), Solid(out, -3.5f, 5, 0), Solid(out, 0, 12.5f, 0));
	EXPECT(ok && !Solid(out, 0, 5, 0) && !Solid(out, 1.2f, 2, 0) && !Solid(out, -1.2f, 9, 0), "the opening is open");
	EXPECT(Solid(out, -3.5f, 5, 0) && Solid(out, 3.5f, 5, 0) && Solid(out, 0, 12.5f, 0), "the jambs and the lintel are solid inside");
	EXPECT(out.parts.size() <= 8, "a few parts: %d", (int)out.parts.size());
}

void TestClosedBoxAndCorner() {
	{
		grannygeom::Mesh m;
		AddBox(m, 0, 0, 0, 6, 6, 6);
		worldsolids::Model out;
		worldsolids::Make({ m }, &out);
		std::printf("closed box: %d part(s)\n", (int)out.parts.size());
		EXPECT(out.parts.size() == 1 && Solid(out, 3, 3, 3), "filled, one part: %d", (int)out.parts.size());
	}
	{   // an L corner: a wall along x and one along z, meeting at the origin
		grannygeom::Mesh m;
		AddBox(m, 0, 0, 0, 12, 10, 2);
		AddBox(m, 0, 0, 0, 2, 10, 12);
		worldsolids::Model out;
		worldsolids::Make({ m }, &out);
		std::printf("L corner: %d parts; the inside of the L solid: %d\n", (int)out.parts.size(), Solid(out, 7, 5, 7));
		EXPECT(!Solid(out, 7, 5, 7) && !Solid(out, 4, 5, 4) && Solid(out, 10, 5, 1) && Solid(out, 1, 5, 10), "the L's inside is free, its arms solid");
		EXPECT(out.parts.size() >= 2 && out.parts.size() <= 4, "two or so parts: %d", (int)out.parts.size());
	}
	{   // a flat quad (a plank's face): one thin part
		grannygeom::Mesh m;
		m.pos = { 0, 0, 0, 8, 0, 0, 8, 0, 3, 0, 0, 3 };
		m.tri = { 0, 1, 2, 0, 2, 3 };
		worldsolids::Model out;
		worldsolids::Make({ m }, &out);
		EXPECT(out.parts.size() == 1 && out.parts[0].hi[1] - out.parts[0].lo[1] > 0.1f && out.parts[0].hi[1] - out.parts[0].lo[1] < 0.5f, "a thin part: %d, %.2f thick",
		       (int)out.parts.size(), out.parts.empty() ? 0.0f : out.parts[0].hi[1] - out.parts[0].lo[1]);
	}
}

void TestLeavesAndBushes() {
	grannygeom::Mesh trunk, leaves;
	trunk.name = "tree_trunk";
	AddBox(trunk, -1, 0, -1, 1, 20, 1);
	leaves.name = "tree_leaves01";
	leaves.material = "act1_tree_maple01_b";
	AddBox(leaves, -8, 12, -8, 8, 24, 8);
	worldsolids::Model out;
	worldsolids::Make({ trunk, leaves }, &out);
	std::printf("tree: %d parts, the crown solid: %d, the trunk: %d\n", (int)out.parts.size(), Solid(out, 6, 18, 6), Solid(out, 0, 5, 0));
	EXPECT(!Solid(out, 6, 18, 6) && Solid(out, 0, 5, 0), "the trunk only");
	using worldsolids::Classify;
	EXPECT(Classify("data/hd/env/foliage/act2/model/act2_bushes/act2_bush04.model") == worldsolids::kIgnore, "a bush is left out");
	EXPECT(Classify("data/hd/env/foliage/global/model/act1_grassclumps/grass01.model") == worldsolids::kIgnore, "grass is left out");
	EXPECT(Classify("data/hd/env/foliage/global/model/act3_saplings/sapling01.model") == worldsolids::kIgnore, "a sapling is left out");
	EXPECT(Classify("data/hd/env/foliage/act1/model/act1_trees_maple/act1_tree_maple01.model") == 0, "a tree stays");
	EXPECT(Classify("data/hd/env/model/act1/barracks/act1_barracks_walls/wall_blank01.model") == 0, "a wall stays");
}

void TestCatalogue() {
	std::vector<worldsolids::Model> models(3);
	models[0].lo[0] = -1; models[0].hi[0] = 1; models[0].hi[1] = 10; models[0].parts.push_back({ { -1, 0, -1 }, { 1, 10, 1 } });
	models[1].lo[0] = -2; models[1].hi[0] = 2; models[1].hi[1] = 12; models[1].parts.push_back({ { -2, 0, -2 }, { 2, 12, 2 } });
	models[2].flags = worldsolids::kIgnore;
	std::vector<worldsolids::Row> rows(3);
	rows[0].id = 77; rows[0].x = 12.5f; rows[0].z = 117.5f; rows[0].models[0] = 0; rows[0].models[1] = 1;
	rows[1].id = 5; rows[1].x = 1; rows[1].models[0] = 2;
	rows[2].id = 77; rows[2].x = 22.5f; rows[2].z = 127.5f; rows[2].models[0] = 1;
	const std::vector<uint8_t> data = worldsolids::Encode(models, rows);
	worldsolids::Catalog c;
	EXPECT(c.Parse(data.data(), data.size()) && c.Rows() == 3 && c.Models() == 3, "it reads back");
	const float at[3] = { 12.5f, 0, 117.5f }, elsewhere[3] = { 12.5f, 0, 118.5f };
	const worldsolids::Row* r = c.Find(77, at);
	EXPECT(r && !c.Find(77, elsewhere) && c.Knows(5) && !c.Knows(6), "found by id and place");
	if (r) {
		const float lo[3] = { -2, 0, 0 }, hi[3] = { 2, 12, 0 };
		float off = 0;
		const worldsolids::Model* m = c.Pick(*r, lo, hi, &off);
		EXPECT(m == c.ModelAt(1) && off < 1e-6f, "the variant whose bounds are the renderer's: %p off %.2f", (const void*)m, off);
	}
	std::vector<uint8_t> bad = data;
	bad.pop_back();
	EXPECT(!c.Parse(bad.data(), bad.size()), "a cut file is refused");
}

void TestFile(const char* path) {
	std::ifstream f(path, std::ios::binary);
	std::stringstream ss;
	ss << f.rdbuf();
	const std::string bytes = ss.str();
	grannyremesh::File file;
	std::string why;
	if (!grannyremesh::Read(bytes, &file, &why)) {
		std::printf("%s: not read - %s\n", path, why.c_str());
		++g_fails;
		return;
	}
	std::vector<grannygeom::Mesh> meshes;
	if (!grannygeom::MeshesOf(file, &meshes, &why)) {
		std::printf("%s: no meshes - %s\n", path, why.c_str());
		++g_fails;
		return;
	}
	std::printf("%s: %d mesh(es)\n", path, (int)meshes.size());
	for (const auto& m : meshes) {
		float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
		for (size_t i = 0; i + 2 < m.pos.size(); i += 3)
			for (int c = 0; c < 3; ++c) { lo[c] = std::min(lo[c], m.pos[i + c]); hi[c] = std::max(hi[c], m.pos[i + c]); }
		std::printf("   mesh '%s' material '%s' layer %d: %d vertices, %d triangles, (%.2f %.2f %.2f) .. (%.2f %.2f %.2f)%s\n", m.name.c_str(), m.material.c_str(), m.layer,
		            (int)(m.pos.size() / 3), (int)(m.tri.size() / 3), lo[0], lo[1], lo[2], hi[0], hi[1], hi[2], worldsolids::UsedMesh(m) ? "" : "  (left out)");
	}
	worldsolids::Model out;
	const bool ok = worldsolids::Make(meshes, &out);
	std::printf("   -> %s: %d parts, cell %.2f units, bounds (%.2f %.2f %.2f) .. (%.2f %.2f %.2f); flags by its path %u\n", ok ? "made" : "NOT made", (int)out.parts.size(),
	            out.cell, out.lo[0], out.lo[1], out.lo[2], out.hi[0], out.hi[1], out.hi[2], (unsigned)worldsolids::Classify(path));
	for (const auto& p : out.parts)
		std::printf("      (%.2f %.2f %.2f) .. (%.2f %.2f %.2f)\n", p.lo[0], p.lo[1], p.lo[2], p.hi[0], p.hi[1], p.hi[2]);
	if (!ok) ++g_fails;
}

}   // namespace

int main(int argc, char** argv) {
	TestDoorway();
	TestClosedBoxAndCorner();
	TestLeavesAndBushes();
	TestCatalogue();
	for (int i = 1; i < argc; ++i) TestFile(argv[i]);
	if (g_fails) {
		std::printf("world_solids_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("world_solids_test: all passed\n");
	return 0;
}
