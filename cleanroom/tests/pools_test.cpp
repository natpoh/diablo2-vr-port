// The memory pools grown (cleanroom/pools) against the game's own TLSF code, without
// the game: the snapshot (dump_loader/D2R_image.bin) is put into memory as the image,
// as sigscan_test does, and its TLSF functions (create, memalign, free, walk - they
// call nothing outside themselves but an error print) run from there. The two pool
// objects are made at their own addresses on heaps of this test, the way the game
// makes them, and d2rcam::GrowPools does the rest as in the game.
//
//   pools_test <D2R_image.bin>          every case, each in a process of its own
//   pools_test <D2R_image.bin> <case>   one case: grow, enough, off, waiting, again, foreign, broken
//
// grow     both pools full, grown; allocations go on in the new memory, every block given
//          back merges into one free block per pool; a block bigger than the first pool fits
// enough   wanted no more than the pool has: left as it is
// off      0 MB: left as it is, without waiting for the pools
// waiting  not made yet: waits, then grows once made
// again    the process marker says it was grown: not grown twice
// foreign  another vtable: not grown
// broken   a block's size word spoiled: the walk refuses, nothing is handed to the heap

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "d2rcam.h"
#include "game_layout.h"
#include "sigscan.h"

#include <D2RLPlugin/context.h>
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace d2rcam::layout;

// The game's TLSF entry points (RVAs in 3.3.93787 / D2RLoader 1.3.1, RECON.md section 7).
constexpr uint64_t kTlsfCreateWithPool = 0x1215A30;   // tlsf_t (void* mem, size_t bytes)
constexpr uint64_t kTlsfMemalign       = 0x1215D60;   // void* (tlsf_t, size_t align, size_t size)
constexpr uint64_t kTlsfWalkPool       = 0x12160F0;   // void (pool_t, walker, void* user)
constexpr uint64_t kTlsfCodeBegin      = 0x1215000;
constexpr uint64_t kTlsfCodeEnd        = 0x1217000;
constexpr uint64_t kMB                 = 1024 * 1024;

using CreateFn  = void*(__fastcall*)(void* mem, size_t bytes);
using AlignFn   = void*(__fastcall*)(void* tlsf, size_t align, size_t size);
using FreeFn    = void(__fastcall*)(void* tlsf, void* p);
using WalkerFn  = void(__fastcall*)(void* ptr, size_t size, int used, void* user);
using WalkFn    = void(__fastcall*)(void* pool, WalkerFn walker, void* user);

uintptr_t g_base  = 0;
int       g_fails = 0;

#define EXPECT(c, ...)                                         \
	do {                                                       \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
			std::printf(__VA_ARGS__);                          \
			std::printf("\n");                                 \
		}                                                      \
	} while (0)

void __cdecl LogInfo(const D2RL::PluginContext*, const char*) noexcept {}
void __cdecl LogWarn(const D2RL::PluginContext*, const char* m) noexcept { std::printf("  [warn] %s\n", m); }
bool __cdecl CheckExpected(const D2RL::PluginContext*, uint64_t rva, const void* expected, uint32_t n) noexcept {
	return std::memcmp(reinterpret_cast<const void*>(g_base + rva), expected, n) == 0;
}

D2RL::PluginApi     g_api {};
D2RL::PluginContext g_ctx {};

bool Load(const char* path) {
	FILE* f = nullptr;
	if (fopen_s(&f, path, "rb") != 0 || !f) return false;
	std::fseek(f, 0, SEEK_END);
	const long size = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	void* mem = VirtualAlloc(nullptr, static_cast<size_t>(size), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	const bool ok = mem && std::fread(mem, 1, static_cast<size_t>(size), f) == static_cast<size_t>(size);
	std::fclose(f);
	g_base = reinterpret_cast<uintptr_t>(mem);
	g_api.apiSize = D2RL::PluginApiSize;
	g_api.logInfo = &LogInfo;
	g_api.logWarn = &LogWarn;
	g_api.logError = &LogWarn;
	g_api.checkExpectedBytes = &CheckExpected;
	g_ctx.contextSize = D2RL::PluginContextSize;
	g_ctx.api = &g_api;
	g_ctx.exeBase = g_base;
	if (!ok) return false;
	// The TLSF code runs from the snapshot; the vtable's Free slot points where the
	// game's loader would have put it (the snapshot holds the game's own base).
	DWORD old = 0;
	VirtualProtect(reinterpret_cast<void*>(g_base + kTlsfCodeBegin), kTlsfCodeEnd - kTlsfCodeBegin, PAGE_EXECUTE_READ, &old);
	*reinterpret_cast<uintptr_t*>(g_base + TlsfVtable + tlsfalloc::FreeSlot * 8) = g_base + TlsfAllocFree;
	return true;
}

template <typename Fn>
Fn At(uint64_t rva) { return reinterpret_cast<Fn>(g_base + rva); }

struct Heap {
	uintptr_t obj  = 0;
	uintptr_t buf  = 0;
	uint64_t  size = 0;
	void*     tlsf = nullptr;
	std::vector<void*> blocks;
};

// As the game makes a pool: buffer, size, tlsf_create_with_pool, the object's fields.
Heap Make(uint64_t objRva, uint64_t size, bool vtable = true) {
	Heap h;
	h.obj  = g_base + objRva;
	h.size = size;
	h.buf  = reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
	h.tlsf = At<CreateFn>(kTlsfCreateWithPool)(reinterpret_cast<void*>(h.buf), size);
	auto* f = reinterpret_cast<uint64_t*>(h.obj);
	std::memset(f, 0, 0x30);
	f[tlsfalloc::Vtable / 8] = vtable ? g_base + TlsfVtable : g_base + 0x1CB90B0;
	f[tlsfalloc::Buffer / 8] = h.buf;
	f[tlsfalloc::Size / 8]   = size;
	f[tlsfalloc::Tlsf / 8]   = reinterpret_cast<uint64_t>(h.tlsf);
	return h;
}

void Clear(uint64_t objRva) { std::memset(reinterpret_cast<void*>(g_base + objRva), 0, 0x30); }

bool Inside(const Heap& h, const void* p) {
	const auto a = reinterpret_cast<uintptr_t>(p);
	return a >= h.buf && a < h.buf + h.size;
}

// Allocations of the sizes and alignments the game asks for, until the heap says no.
int Fill(Heap& h, std::mt19937& rng, int max = 1 << 30) {
	static const size_t kAlign[] = { 8, 16, 16, 16, 64, 256 };
	int n = 0;
	for (; n < max; ++n) {
		const size_t size  = 16 + rng() % 0x9800;
		const size_t align = kAlign[rng() % 6];
		void* p = At<AlignFn>(kTlsfMemalign)(h.tlsf, align, size);
		if (!p) break;
		EXPECT(reinterpret_cast<uintptr_t>(p) % align == 0, "alignment %zu", align);
		std::memset(p, 0xA5, size);   // the game writes all of it
		h.blocks.push_back(p);
	}
	return n;
}

void FreeAll(Heap& h, std::mt19937& rng) {
	std::shuffle(h.blocks.begin(), h.blocks.end(), rng);
	for (void* p : h.blocks) At<FreeFn>(TlsfFree)(h.tlsf, p);
	h.blocks.clear();
}

struct Walk {
	int    blocks = 0, used = 0;
	size_t freeBytes = 0, largest = 0;
};

void __fastcall Count(void*, size_t size, int used, void* user) {
	auto* w = static_cast<Walk*>(user);
	++w->blocks;
	w->used += used != 0;
	if (!used) {
		w->freeBytes += size;
		w->largest = std::max(w->largest, size);
	}
}

// The game's tlsf_walk_pool over a pool whose first block is at `block`.
Walk WalkFrom(uintptr_t block) {
	Walk w;
	At<WalkFn>(kTlsfWalkPool)(reinterpret_cast<void*>(block + tlsf::SizeWord), &Count, &w);
	return w;
}
Walk WalkFirst(const Heap& h) { return WalkFrom(h.buf + tlsf::Control - tlsf::SizeWord); }

std::string Grow(unsigned r, unsigned e, bool* done = nullptr) {
	char line[400] = "";
	const bool ok = d2rcam::GrowPools(r, e, line, sizeof line);
	if (done) *done = ok;
	std::printf("  %s%s\n", line, ok ? "" : " (waiting)");
	return line;
}

void Start() {
	d2rsig::Resolve(&g_ctx);
	EXPECT(d2rsig::AllFound(), "%s", d2rsig::Summary().c_str());
}

void CaseGrow() {
	Start();
	std::mt19937 rng(7);
	Heap r = Make(RendererPool, 2 * kMB), e = Make(EntityPool, 3 * kMB);
	const int first = Fill(r, rng);
	Fill(e, rng);
	EXPECT(first > 50, "%d blocks fit", first);
	bool done = false;
	const std::string line = Grow(10, 9, &done);
	EXPECT(done, "settled");
	EXPECT(line == "memory pools: renderer 2 -> 10 MB, entity 3 -> 9 MB", "%s", line.c_str());
	// The object is only read.
	EXPECT(reinterpret_cast<uint64_t*>(r.obj)[tlsfalloc::Size / 8] == 2 * kMB, "size field untouched");
	// More fits now, out of the new memory (a few small ones still in the first pool's crumbs).
	const size_t had = r.blocks.size();
	const int more = Fill(r, rng);
	EXPECT(more > 3 * first, "%d more blocks after %d", more, first);
	int outside = 0;
	uintptr_t lowest = ~0ull;
	for (size_t i = had; i < r.blocks.size(); ++i) {
		if (Inside(r, r.blocks[i])) continue;
		++outside;
		lowest = std::min(lowest, reinterpret_cast<uintptr_t>(r.blocks[i]));
	}
	EXPECT(outside > more * 9 / 10, "%d of %d new blocks in the new memory", outside, more);
	EXPECT(WalkFirst(r).blocks > 50, "the first pool walks");
	// Every block back: one free block per pool, the new memory whole again. It was
	// handed out from its start, which is on a 64 KB boundary.
	const uintptr_t region = lowest & ~0xFFFFull;
	FreeAll(r, rng);
	FreeAll(e, rng);
	const Walk w1 = WalkFirst(r);
	EXPECT(w1.blocks == 1 && w1.used == 0, "first pool: %d blocks, %d used", w1.blocks, w1.used);
	EXPECT(w1.freeBytes == ((2 * kMB - tlsf::Control - tlsf::PoolOverhead) & ~7ull), "first pool free %zu", w1.freeBytes);
	// Our region starts on a 64 KB boundary: its one block at its start, 0x18 short of 8 MB.
	const Walk w2 = WalkFrom(region);
	EXPECT(w2.blocks == 1 && w2.used == 0 && w2.freeBytes == 8 * kMB - 0x18, "new memory: %d blocks, %d used, %zu free", w2.blocks, w2.used,
	       w2.freeBytes);
	// Bigger than the whole first pool: only the new memory has it.
	void* big = At<AlignFn>(kTlsfMemalign)(r.tlsf, 16, 5 * kMB);
	EXPECT(big && !Inside(r, big), "5 MB block at %p", big);
	if (big) At<FreeFn>(TlsfFree)(r.tlsf, big);
	// Asked again: nothing more.
	EXPECT(Grow(64, 280) == line, "the same line, nothing grown again");
	const Walk w3 = WalkFrom(region);
	EXPECT(w3.blocks == 1 && w3.freeBytes == 8 * kMB - 0x18, "still one block");
}

void CaseEnough() {
	Start();
	Make(RendererPool, 12 * kMB);
	Make(EntityPool, 70 * kMB);
	bool done = false;
	const std::string line = Grow(12, 64, &done);
	EXPECT(done && line == "memory pools: renderer 12 MB, left as it is (render_pool_mb=12), entity 70 MB, left as it is (entity_pool_mb=64)", "%s",
	       line.c_str());
}

void CaseOff() {
	Start();
	Clear(RendererPool);
	Clear(EntityPool);
	bool done = false;
	const std::string line = Grow(0, 0, &done);
	EXPECT(done && line == "memory pools: renderer left as it is (render_pool_mb=0), entity left as it is (entity_pool_mb=0)", "%s", line.c_str());
}

void CaseWaiting() {
	Start();
	Clear(RendererPool);
	Clear(EntityPool);
	bool done = true;
	std::string line = Grow(16, 80, &done);
	EXPECT(!done && line == "memory pools: renderer not made yet, entity not made yet", "%s", line.c_str());
	Make(EntityPool, 70 * kMB);
	line = Grow(16, 80, &done);
	EXPECT(!done && line == "memory pools: renderer not made yet, entity 70 -> 80 MB", "%s", line.c_str());
	Make(RendererPool, 12 * kMB);
	line = Grow(16, 80, &done);
	EXPECT(done && line == "memory pools: renderer 12 -> 16 MB, entity 70 -> 80 MB", "%s", line.c_str());
}

void CaseAgain() {
	Start();
	Heap r = Make(RendererPool, 12 * kMB);
	Make(EntityPool, 70 * kMB);
	wchar_t name[64];
	swprintf_s(name, L"D2RVR_POOL_%llX", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(r.tlsf)));
	SetEnvironmentVariableW(name, L"67108864");
	const std::string line = Grow(64, 70);
	EXPECT(line == "memory pools: renderer grown before to 64 MB (vrcam loaded again), entity 70 MB, left as it is (entity_pool_mb=70)", "%s",
	       line.c_str());
	EXPECT(WalkFirst(r).blocks == 1, "untouched");
}

void CaseForeign() {
	Start();
	Make(RendererPool, 12 * kMB, false);
	Make(EntityPool, 70 * kMB);
	bool done = false;
	const std::string line = Grow(64, 0, &done);
	EXPECT(done && line == "memory pools: renderer NOT grown - not the allocator this mod knows (another vtable), entity left as it is (entity_pool_mb=0)",
	       "%s", line.c_str());
}

void CaseBroken() {
	Start();
	std::mt19937 rng(11);
	Heap r = Make(RendererPool, 2 * kMB);
	Make(EntityPool, 70 * kMB);
	Fill(r, rng, 40);
	// The 20th block's size word one block-size short: the blocks no longer reach the end marker.
	auto* word = reinterpret_cast<uint64_t*>(reinterpret_cast<uintptr_t>(r.blocks[20]) - tlsf::SizeWord);
	*word -= 0x20;
	bool done = false;
	const std::string line = Grow(64, 0, &done);
	EXPECT(done && line == "memory pools: renderer NOT grown - its blocks are not laid out as this mod knows, entity left as it is (entity_pool_mb=0)", "%s",
	       line.c_str());
	*word += 0x20;
	const Walk after = WalkFirst(r);
	EXPECT(after.used == 40, "%d blocks, %d used", after.blocks, after.used);   // and the free gaps memalign left before aligned ones
	void* p = At<AlignFn>(kTlsfMemalign)(r.tlsf, 16, 3 * kMB);
	EXPECT(p == nullptr, "nothing was handed to the heap");
}

struct Case {
	const char* name;
	void (*run)();
};
const Case kCases[] = { { "grow", CaseGrow }, { "enough", CaseEnough }, { "off", CaseOff }, { "waiting", CaseWaiting },
	                    { "again", CaseAgain }, { "foreign", CaseForeign }, { "broken", CaseBroken } };

}   // namespace

int main(int argc, char** argv) {
	if (argc < 2) {
		std::printf("pools_test <D2R_image.bin> [case]\n");
		return 2;
	}
	if (argc == 2) {   // every case in a process of its own (the pools keep their state)
		int bad = 0;
		for (const Case& c : kCases) {
			char cmd[1024];
			snprintf(cmd, sizeof cmd, "\"\"%s\" \"%s\" %s\"", argv[0], argv[1], c.name);
			const int rc = std::system(cmd);
			std::printf("%s: %s\n", c.name, rc == 0 ? "ok" : "FAILED");
			bad += rc != 0;
		}
		return bad;
	}
	if (!Load(argv[1])) {
		std::printf("cannot read %s\n", argv[1]);
		return 2;
	}
	for (const Case& c : kCases) {
		if (std::strcmp(c.name, argv[2]) == 0) {
			c.run();
			return g_fails ? 1 : 0;
		}
	}
	std::printf("no case %s\n", argv[2]);
	return 2;
}
