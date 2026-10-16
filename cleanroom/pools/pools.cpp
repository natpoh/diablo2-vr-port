// The game's memory pools grown (see d2rcam.h GrowPools, RECON.md section 7).
//
// D2Render keeps its frame data in the "Renderer Pool" and the entity system
// its components in the "Entity System Pool". Each is a TlsfAllocator: one
// buffer of a fixed size (12 MB, 70 MB) made at start and handed to a TLSF heap
// (the public two-level segregated fit allocator). It never grows. Full, it
// returns NULL; the game logs "Failed to allocate from Renderer pool!" and
// writes through the NULL. With more rooms drawn (drawdist) the renderer's runs
// out in dense areas - the Act 3 jungle.
//
// A TLSF heap takes more memory at any time: another pool is one more free
// block closed by an end marker, filed in its free lists. The game has no
// add-pool function of its own (it is inlined into its create), so we lay that
// block out in memory of our own, marked in use, and hand it to the game's
// tlsf_free: it files the block under its size and merges it with nothing (it
// has no neighbours). From then on the game's own allocator hands out of it like
// out of any other free block.
//
// What is written: our own memory, and the pool's free lists by the game's own
// tlsf_free inside the pool's own lock (the SRWLOCK every TlsfAllocator method
// holds around its tlsf call). The pool object is only read. Before anything is
// written the allocator must be the one this was read from: the object's vtable
// is TlsfAllocator's, its Free slot is TlsfAllocator::Free, which calls this
// tlsf_free, the TLSF control sits at the start of the buffer, and the pool's
// own blocks, walked from the first to the end marker, tile it as the layout in
// game_layout.h says. Anything else leaves the pool alone.

#include "pools.h"

#include "../camera/game_layout.h"
#include "../sigscan/sigscan.h"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace d2rcam::pools {
namespace {

using namespace d2rcam::layout;

using TlsfFreeFn = void(__fastcall*)(void* tlsf, void* ptr);

constexpr uint64_t kMB    = 1024ull * 1024ull;
constexpr unsigned kMaxMb = 4096;   // per pool; more is a typo

enum class St { Waiting, Grown, GrownBefore, Enough, Off, Left };

struct Pool {
	const char* name;           // for the log
	const char* key;            // its [render] key
	uint64_t    rva;            // the TlsfAllocator object
	St          st     = St::Waiting;
	uint64_t    before = 0;     // bytes
	uint64_t    after  = 0;
	unsigned    wanted = 0;     // MB
	char        why[96] = "";
};

Pool       g_pools[2] = { { "renderer", "render_pool_mb", RendererPool }, { "entity", "entity_pool_mb", EntityPool } };
std::mutex g_lock;

bool ReadableProtect(DWORD p) {
	if (p & (PAGE_GUARD | PAGE_NOACCESS)) return false;
	return (p & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

// Every page of [a, a + n) committed and readable.
bool Readable(uintptr_t a, size_t n) {
	const uintptr_t end = a + n;
	while (a < end) {
		MEMORY_BASIC_INFORMATION mi {};
		if (!VirtualQuery(reinterpret_cast<void*>(a), &mi, sizeof(mi)) || mi.State != MEM_COMMIT || !ReadableProtect(mi.Protect)) {
			return false;
		}
		a = reinterpret_cast<uintptr_t>(mi.BaseAddress) + mi.RegionSize;
	}
	return true;
}

bool Read(void* dst, uintptr_t src, size_t n) {
	if (src == 0 || !Readable(src, n)) return false;
	__try {
		std::memcpy(dst, reinterpret_cast<const void*>(src), n);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// The pool's own blocks from the first to its end marker, stepped the way the
// game's tlsf_walk_pool steps: true when they tile the pool exactly, every size
// is a block size, every "previous free" flag says what the block before is,
// and no two free blocks touch (TLSF always merges them). Under the pool's lock.
bool WalkPool(uintptr_t buffer, uint64_t size) {
	if (size < tlsf::Control + tlsf::PoolOverhead + tlsf::MinBlock) return false;
	const uintptr_t first = buffer + tlsf::Control - tlsf::SizeWord;   // its prev_phys word is the control's last
	const uintptr_t end   = buffer + tlsf::Control + ((size - tlsf::Control - tlsf::PoolOverhead) & ~7ull);
	__try {
		uintptr_t b        = first;
		bool      prevFree = false;
		for (;;) {
			const uint64_t w    = *reinterpret_cast<const volatile uint64_t*>(b + tlsf::SizeWord);
			const bool     free = (w & tlsf::FreeBit) != 0;
			if (((w & tlsf::PrevFreeBit) != 0) != prevFree) return false;
			const uint64_t len = w & ~(tlsf::FreeBit | tlsf::PrevFreeBit);
			if (b == end) return len == 0 && !free;
			if (len < tlsf::MinBlock || (len & 7) != 0 || len > end - b - tlsf::SizeWord || (free && prevFree)) return false;
			prevFree = free;
			b += tlsf::SizeWord + len;
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool CallFree(TlsfFreeFn fn, void* heap, void* p) {
	__try {
		fn(heap, p);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// The object's Free (vtable slot 4) is TlsfAllocator::Free, and the call inside
// it (where its signature has it) goes to tlsf_free.
bool FreeIsTlsfFree(uintptr_t vtable, uintptr_t allocFree, uintptr_t tlsfFree) {
	uintptr_t slot = 0;
	uint8_t   call[5] {};
	if (!Read(&slot, vtable + tlsfalloc::FreeSlot * sizeof(uintptr_t), sizeof(slot)) || slot != allocFree) return false;
	if (!Read(call, allocFree + tlsfalloc::FreeCallAt, sizeof(call)) || call[0] != 0xE8) return false;
	int32_t rel = 0;
	std::memcpy(&rel, call + 1, sizeof(rel));
	return allocFree + tlsfalloc::FreeCallAt + sizeof(call) + static_cast<intptr_t>(rel) == tlsfFree;
}

// Per process, by the heap: set once a pool is grown, so a second load of the
// plugin does not grow it again.
void MarkerName(uintptr_t heap, wchar_t* out, size_t n) { swprintf_s(out, n, L"D2RVR_POOL_%llX", static_cast<unsigned long long>(heap)); }

void Leave(Pool& p, const char* why) {
	p.st = St::Left;
	strncpy_s(p.why, why, _TRUNCATE);
}

void Try(Pool& p, unsigned wantMb) {
	if (p.st != St::Waiting) return;
	p.wanted = wantMb;
	if (wantMb == 0) {
		p.st = St::Off;
		return;
	}
	const uint64_t keys[4] = { p.rva, TlsfVtable, TlsfAllocFree, TlsfFree };
	uintptr_t      at[4] {};
	for (int i = 0; i < 4; ++i) {
		at[i] = d2rsig::Addr(keys[i]);
		if (at[i] == 0) {
			if (d2rsig::NotInBuild(keys[i])) Leave(p, "its code is not found in this game build");
			return;   // else its page is not decrypted yet
		}
	}
	const uintptr_t obj = at[0];
	uint64_t f[6] {};   // vtable, parent, buffer, size, tlsf, lock
	if (!Read(f, obj, sizeof(f))) {
		Leave(p, "its object does not read");
		return;
	}
	const uintptr_t vt   = f[tlsfalloc::Vtable / 8];
	const uintptr_t buf  = f[tlsfalloc::Buffer / 8];
	const uint64_t  size = f[tlsfalloc::Size / 8];
	const uintptr_t heap = f[tlsfalloc::Tlsf / 8];
	if (vt == 0 || heap == 0) return;   // not made yet
	if (vt != at[1]) return Leave(p, "not the allocator this mod knows (another vtable)");
	if (!FreeIsTlsfFree(at[1], at[2], at[3])) return Leave(p, "not the allocator this mod knows (another Free)");
	if (heap != buf || size < kMB || size >= tlsf::MaxBlock) return Leave(p, "not the allocator this mod knows (laid out otherwise)");
	p.before = p.after = size;
	if (static_cast<uint64_t>(wantMb) * kMB <= size) {
		p.st = St::Enough;
		return;
	}
	wchar_t marker[64];
	MarkerName(heap, marker, 64);
	wchar_t was[32] {};
	if (GetEnvironmentVariableW(marker, was, 32) > 0) {
		p.after = _wcstoui64(was, nullptr, 10);
		p.st    = St::GrownBefore;
		return;
	}
	// The block: header {prev_phys, size word} at r, payload at r + 0x10; the end
	// marker's header right after the payload, inside the last 0x18 bytes.
	const uint64_t bytes = (static_cast<uint64_t>(wantMb) * kMB - size + 0xFFFF) & ~0xFFFFull;
	const uint64_t len   = bytes - tlsf::Payload - tlsf::SizeWord;
	void*          mem   = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (mem == nullptr) {
		char why[96];
		snprintf(why, sizeof(why), "no %llu MB of memory to give it", static_cast<unsigned long long>(bytes / kMB));
		return Leave(p, why);
	}
	const uintptr_t r = reinterpret_cast<uintptr_t>(mem);
	*reinterpret_cast<uint64_t*>(r + tlsf::SizeWord)          = len;   // in use, the one before in use
	*reinterpret_cast<uint64_t*>(r + tlsf::Payload + len)     = 0;     // the end marker: size 0, in use
	auto* lock = reinterpret_cast<SRWLOCK*>(obj + tlsfalloc::Lock);
	AcquireSRWLockExclusive(lock);
	uint64_t again[6] {};
	const bool same  = Read(again, obj, sizeof(again)) && std::memcmp(again, f, tlsfalloc::Lock) == 0;
	const bool laid  = same && WalkPool(buf, size);
	const bool given = laid && CallFree(reinterpret_cast<TlsfFreeFn>(at[3]), reinterpret_cast<void*>(heap), reinterpret_cast<void*>(r + tlsf::Payload));
	ReleaseSRWLockExclusive(lock);
	if (!given) {
		if (!laid) VirtualFree(mem, 0, MEM_RELEASE);   // never handed over
		return Leave(p, !same ? "it changed while it was looked at" : !laid ? "its blocks are not laid out as this mod knows" : "the game's tlsf_free failed");
	}
	p.after = size + bytes;
	p.st    = St::Grown;
	wchar_t v[32];
	swprintf_s(v, L"%llu", static_cast<unsigned long long>(p.after));
	SetEnvironmentVariableW(marker, v);
}

const char* Mb(uint64_t bytes, char* buf, size_t n) {
	if (bytes % kMB == 0) snprintf(buf, n, "%llu", static_cast<unsigned long long>(bytes / kMB));
	else snprintf(buf, n, "%.1f", static_cast<double>(bytes) / kMB);
	return buf;
}

void Describe(const Pool& p, char* out, size_t n) {
	char a[24], b[24];
	switch (p.st) {
	case St::Waiting: snprintf(out, n, "%s not made yet", p.name); break;
	case St::Grown: snprintf(out, n, "%s %s -> %s MB", p.name, Mb(p.before, a, sizeof(a)), Mb(p.after, b, sizeof(b))); break;
	case St::GrownBefore: snprintf(out, n, "%s grown before to %s MB (vrcam loaded again)", p.name, Mb(p.after, b, sizeof(b))); break;
	case St::Enough: snprintf(out, n, "%s %s MB, left as it is (%s=%u)", p.name, Mb(p.before, a, sizeof(a)), p.key, p.wanted); break;
	case St::Off: snprintf(out, n, "%s left as it is (%s=0)", p.name, p.key); break;
	case St::Left: snprintf(out, n, "%s NOT grown - %s", p.name, p.why); break;
	}
}

}   // namespace

bool Grow(unsigned renderMb, unsigned entityMb, char* line, size_t lineSize) {
	std::unique_lock<std::mutex> lock(g_lock, std::try_to_lock);
	if (!lock.owns_lock()) return false;
	const unsigned want[2] = { std::min(renderMb, kMaxMb), std::min(entityMb, kMaxMb) };
	bool settled = true;
	for (int i = 0; i < 2; ++i) {
		Try(g_pools[i], want[i]);
		settled &= g_pools[i].st != St::Waiting;
	}
	if (line != nullptr && lineSize > 0) {
		char one[2][160];
		Describe(g_pools[0], one[0], sizeof(one[0]));
		Describe(g_pools[1], one[1], sizeof(one[1]));
		snprintf(line, lineSize, "memory pools: %s, %s", one[0], one[1]);
	}
	return settled;
}

}   // namespace d2rcam::pools
