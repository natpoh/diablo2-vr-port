// [weight] death_ragdoll: the skinning's anchor - the root of the entity drawn names the body (vr/skin_anchor.h), on a made-up entity tree.
//
// Checks: a mesh two levels under a corpse's root finds that corpse's pose among six alike (the 0.199.0 flicker: none found by the entity alone); a living
// monster of the same model finds none; a pose for the root itself and one for a direct child both match; a root unread (the entity not in the storage, a
// loop, too deep) gives 0; the cache looks up the tree once per entity and again after kKeepMs, and an entity id made again (another generation) is looked up
// anew; two bodies under one root are not known.

#include "../../vr/skin_anchor.h"

#include <cstdio>
#include <map>

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

constexpr uint32_t kNone = 0xFFFFFu;
std::map<uint32_t, uint32_t> g_parent;   // entity -> parent (kNone: a root); not here: not in the storage
int g_reads = 0;

bool ParentOf(uint32_t e, uint32_t* p) {
	++g_reads;
	const auto it = g_parent.find(e);
	if (it == g_parent.end()) return false;
	*p = it->second;
	return true;
}

// Corpse c (0..5): root 0x100+c, its body entity (the skeleton's) 0x200+c, a part 0x300+c under the body, a mesh 0x400+c under the part.
uint32_t Root(int c) { return 0x100u + c; }
uint32_t Body(int c) { return 0x200u + c; }
uint32_t Part(int c) { return 0x300u + c; }
uint32_t Mesh(int c) { return 0x400u + c; }

void Tree() {
	g_parent.clear();
	for (int c = 0; c < 7; ++c) {   // (6 corpses, and a living one of the model: 6)
		g_parent[Root(c)] = kNone;
		g_parent[Body(c)] = Root(c);
		g_parent[Part(c)] = Body(c);
		g_parent[Mesh(c)] = Part(c);
	}
}

void Poses(dollpose::Pose* p, int n) {
	for (int c = 0; c < n; ++c) {
		p[c] = dollpose::Pose{};
		p[c].self = 0x1000u + c;
		p[c].id = 10u + c;
		p[c].entity = Body(c);
		p[c].parts[0] = Part(c);
		p[c].nParts = 1;
	}
}

void TestRootOf() {
	Tree();
	EXPECT(skinanchor::RootOf(Mesh(3), ParentOf) == Root(3), "mesh -> root");
	EXPECT(skinanchor::RootOf(Root(3), ParentOf) == Root(3), "a root is its own");
	EXPECT(skinanchor::RootOf(0x999u, ParentOf) == 0, "not in the storage");
	EXPECT(skinanchor::RootOf(0, ParentOf) == 0 && skinanchor::RootOf(kNone, ParentOf) == 0, "no entity");
	g_parent[0x500u] = 0x501u;
	g_parent[0x501u] = 0x500u;
	EXPECT(skinanchor::RootOf(0x500u, ParentOf) == 0, "a loop");
	g_parent.clear();
	for (uint32_t e = 1; e < 40; ++e) g_parent[e] = e + 1;
	g_parent[40] = kNone;
	EXPECT(skinanchor::RootOf(1, ParentOf) == 0, "deeper than kMaxDepth");
}

void TestByRoot() {
	Tree();
	dollpose::Pose p[6];
	Poses(p, 6);
	skinanchor::Cache cache;
	const auto rootOf = [&](uint32_t e) { return cache.Root(e, 1000, ParentOf); };
	for (int c = 0; c < 6; ++c) {
		EXPECT(dollpose::ByEntity(p, 6, Mesh(c)) < 0, "the entity alone does not find the mesh (corpse %d)", c);
		EXPECT(skinanchor::ByRoot(p, 6, rootOf(Mesh(c)), rootOf) == c, "the mesh's root finds corpse %d", c);
		EXPECT(skinanchor::ByRoot(p, 6, rootOf(Part(c)), rootOf) == c, "a part finds corpse %d", c);
	}
	EXPECT(skinanchor::ByRoot(p, 6, rootOf(Mesh(6)), rootOf) == -1, "a living one of the model: none of ours");
	EXPECT(skinanchor::ByRoot(p, 6, 0, rootOf) == -1, "root unread: not known");
	dollpose::Pose two[2];
	Poses(two, 2);
	two[1].entity = Body(0);   // (another body's pose under the same root)
	EXPECT(skinanchor::ByRoot(two, 2, Root(0), rootOf) == -1, "two bodies under one root: not known");
	two[1].self = two[0].self;   // (the same body twice: it)
	EXPECT(skinanchor::ByRoot(two, 2, Root(0), rootOf) == 0, "one body's two poses: the first");
}

void TestCache() {
	Tree();
	skinanchor::Cache cache;
	g_reads = 0;
	bool looked = false;
	EXPECT(cache.Root(Mesh(2), 100, ParentOf, &looked) == Root(2) && looked, "first: looked up");
	const int reads = g_reads;
	EXPECT(cache.Root(Mesh(2), 200, ParentOf, &looked) == Root(2) && !looked && g_reads == reads, "kept: no look");
	EXPECT(cache.Root(Mesh(2), 100 + skinanchor::Cache::kKeepMs, ParentOf, &looked) == Root(2) && looked, "after kKeepMs: looked up again");
	const uint32_t again = Mesh(2) + 0x100000u;   // (the same slot, another generation: the entity made again)
	g_parent[again] = Part(4);
	EXPECT(cache.Root(again, 300 + skinanchor::Cache::kKeepMs, ParentOf, &looked) == Root(4) && looked, "another generation: looked up anew");
	EXPECT(cache.Root(0, 300, ParentOf, &looked) == 0 && !looked, "no entity: no look");
}

}  // namespace

int main() {
	TestRootOf();
	TestByRoot();
	TestCache();
	if (g_fails) {
		std::printf("skin_anchor_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("skin_anchor_test: all passed\n");
	return 0;
}
