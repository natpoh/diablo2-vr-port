// Finds the game's addresses in the running build from game_sigs.h.
// See sigscan.h; the patterns are made by tools/make_sigs.py.
//
// The game's code is decrypted page by page as the game first runs it, so at
// load some pages do not read yet (the keyboard move, the map click: not
// before a game area). An address on such a page waits: looked at again where
// 3.3.93787 has it whenever it is asked for, and searched for again when more
// of .text has become readable.

#include "sigscan.h"

#include "game_sigs.h"

#include <D2RLPlugin/context.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace d2rsig {
namespace {

constexpr size_t kCount = sizeof(kSigs) / sizeof(kSigs[0]);

enum State : uint8_t {
	Unknown,   // not looked for yet
	Found,
	Waiting,   // not found, but part of .text does not read yet
	Missing,   // not found in a .text that reads whole
};

struct Entry {
	std::atomic<uint64_t> rva { 0 };
	std::atomic<uint8_t>  state { Unknown };
	std::atomic<bool>     searched { false };   // found by the search, not where 3.3.93787 has it
	int                   places = 0;           // matches of the last search
	bool                  differs = false;      // where 3.3.93787 has it the code reads, and is other code
	ULONGLONG             lastTry = 0;
};

struct Image {
	uint32_t size = 0;
	uint32_t textRva = 0, textSize = 0;
	uint32_t pdataRva = 0, pdataSize = 0;
};

const D2RL::PluginContext* g_ctx = nullptr;
uintptr_t                  g_base = 0;
Image                      g_img;
Entry                      g_e[kCount];
std::atomic<bool>          g_resolved { false };   // the first Resolve is done
std::recursive_mutex       g_lock;
std::string                g_summary = "game code: addresses not looked for yet";
size_t                     g_readableAtSearch = 0;   // readable .text pages at the last search
ULONGLONG                  g_lastSearch = 0;
std::atomic<uint32_t>      g_gen { 0 };   // bumped whenever an entry changes, and on every rescan
int                        g_shift = 0;

bool SafeRead(void* dst, uintptr_t src, size_t n) {
	__try {
		std::memcpy(dst, reinterpret_cast<const void*>(src), n);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool ReadableProtect(DWORD p) {
	if (p & (PAGE_GUARD | PAGE_NOACCESS)) return false;
	return (p & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

// Every page of [a, a + n) committed and readable - never touch one that is not
// (the game's decryption is behind them).
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

bool ReadLive(void* dst, uint64_t rva, size_t n) { return Readable(g_base + rva, n) && SafeRead(dst, g_base + rva, n); }

void Logf(bool warn, const char* fmt, ...) {
	if (g_ctx == nullptr) return;
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (warn) g_ctx->LogWarn(buf);
	else g_ctx->LogInfo(buf);
}

struct Pattern {
	std::vector<uint8_t> b;
	std::vector<uint8_t> any;   // 1 = any byte here
};

Pattern Parse(const char* s) {
	Pattern p;
	while (s != nullptr && *s) {
		if (*s == ' ') { ++s; continue; }
		if (s[0] == '?') {
			p.b.push_back(0);
			p.any.push_back(1);
		} else {
			const char hex[3] = { s[0], s[1], 0 };
			p.b.push_back(static_cast<uint8_t>(strtoul(hex, nullptr, 16)));
			p.any.push_back(0);
		}
		s += 2;
	}
	return p;
}

bool MatchAt(const uint8_t* at, const Pattern& p) {
	for (size_t i = 0; i < p.b.size(); ++i) {
		if (!p.any[i] && at[i] != p.b[i]) return false;
	}
	return true;
}

enum class Live { Match, Differs, Unreadable };

Live MatchLive(uint64_t rva, const Pattern& p) {
	std::vector<uint8_t> buf(p.b.size());
	if (p.b.empty() || !ReadLive(buf.data(), rva, buf.size())) return Live::Unreadable;
	return MatchAt(buf.data(), p) ? Live::Match : Live::Differs;
}

bool ReadImage(Image* img) {
	IMAGE_DOS_HEADER dos {};
	IMAGE_NT_HEADERS64 nt {};
	if (!SafeRead(&dos, g_base, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE) return false;
	if (!SafeRead(&nt, g_base + dos.e_lfanew, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE) return false;
	img->size = nt.OptionalHeader.SizeOfImage;
	const auto& exc = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
	img->pdataRva = exc.VirtualAddress;
	img->pdataSize = exc.Size;
	const uintptr_t first = g_base + dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
	for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
		IMAGE_SECTION_HEADER s {};
		if (!SafeRead(&s, first + i * sizeof(s), sizeof(s))) return false;
		if (std::memcmp(s.Name, ".text", 6) == 0) {
			img->textRva = s.VirtualAddress;
			img->textSize = s.Misc.VirtualSize;
			return true;
		}
	}
	return false;
}

// .text as it reads now; pages that do not read stay zero.
struct Text {
	std::vector<uint8_t> bytes;
	size_t               readable = 0, pages = 0;
};

Text ReadText() {
	Text t;
	t.bytes.assign(g_img.textSize, 0);
	for (size_t off = 0; off < t.bytes.size(); off += 0x1000) {
		const size_t n = std::min<size_t>(0x1000, t.bytes.size() - off);
		++t.pages;
		if (ReadLive(t.bytes.data() + off, g_img.textRva + off, n)) ++t.readable;
	}
	return t;
}

size_t ReadablePages() {
	size_t n = 0;
	for (size_t off = 0; off < g_img.textSize; off += 0x1000) n += Readable(g_base + g_img.textRva + off, 1);
	return n;
}

// Where the pattern sits in .text: its RVAs, at most two.
std::vector<uint64_t> Search(const Text& t, const Pattern& p) {
	std::vector<uint64_t> hits;
	// The longest run of fixed bytes is searched for; the rest is compared at each hit.
	size_t best = 0, bestLen = 0;
	for (size_t i = 0; i < p.b.size();) {
		if (p.any[i]) { ++i; continue; }
		size_t j = i;
		while (j < p.b.size() && !p.any[j]) ++j;
		if (j - i > bestLen) { best = i; bestLen = j - i; }
		i = j;
	}
	const std::vector<uint8_t>& text = t.bytes;
	if (bestLen == 0 || text.size() < p.b.size()) return hits;
	const std::boyer_moore_horspool_searcher searcher(p.b.begin() + best, p.b.begin() + best + bestLen);
	auto it = text.begin() + best;
	const auto end = text.end() - (p.b.size() - best - bestLen);
	while (hits.size() < 2) {
		it = std::search(it, end, searcher);
		if (it == end) break;
		const size_t start = static_cast<size_t>(it - text.begin()) - best;
		if (MatchAt(text.data() + start, p)) hits.push_back(g_img.textRva + start);
		++it;
	}
	return hits;
}

int Index(uint64_t rva) {
	for (size_t i = 0; i < kCount; ++i) {
		if (kSigs[i].rva == rva) return static_cast<int>(i);
	}
	return -1;
}

int IndexOf(const char* name) {
	for (size_t i = 0; name != nullptr && i < kCount; ++i) {
		if (std::strcmp(kSigs[i].name, name) == 0) return static_cast<int>(i);
	}
	return -1;
}

// The address an instruction reaches: its end + the int32 at dispAt (+ addend).
uint64_t ReadRef(uint64_t insn, const Sig& s) {
	int32_t d = 0;
	if (!ReadLive(&d, insn + s.dispAt, 4)) return 0;
	return insn + s.insnLen + static_cast<int64_t>(d) + s.addend;
}

uint64_t FunctionEnd(uint64_t begin) {
	const size_t n = g_img.pdataSize / sizeof(RUNTIME_FUNCTION);
	size_t lo = 0, hi = n;
	while (lo < hi) {   // sorted by BeginAddress
		const size_t mid = (lo + hi) / 2;
		RUNTIME_FUNCTION f {};
		if (!ReadLive(&f, g_img.pdataRva + mid * sizeof(f), sizeof(f))) return 0;
		if (f.BeginAddress == begin) return f.EndAddress;
		if (f.BeginAddress < begin) lo = mid + 1;
		else hi = mid;
	}
	return 0;
}

// One entry: where 3.3.93787 has it, then (with `text`) by the search.
// Returns the new state; on Found the entry holds the address.
State Try(size_t i, const Text* text) {
	const Sig& s = kSigs[i];
	Entry& e = g_e[i];
	e.lastTry = GetTickCount64();
	uint64_t got = 0;
	bool searched = false;
	if (s.kind == Kind::FuncEnd || s.of != nullptr) {
		const int of = IndexOf(s.of);
		const uint8_t st = of >= 0 ? g_e[of].state.load() : Missing;
		if (st != Found) return st == Missing ? Missing : Waiting;
		got = s.kind == Kind::FuncEnd ? FunctionEnd(g_e[of].rva.load()) : ReadRef(g_e[of].rva.load(), s);
		if (!got) return Missing;
	} else {
		const Pattern p = Parse(s.pattern);
		const uint64_t site = s.site + g_shift;
		uint64_t start = 0;
		const Live here = MatchLive(site, p);
		e.differs = here == Live::Differs;
		if (here == Live::Match) {
			start = site;
		} else if (text != nullptr) {
			const std::vector<uint64_t> hits = Search(*text, p);
			e.places = static_cast<int>(hits.size());
			if (hits.size() == 1) {
				start = hits[0];
				searched = true;
			} else if (hits.empty()) {
				return text->readable < text->pages ? Waiting : Missing;
			} else {
				return Missing;   // two places: which one is ours is not known
			}
		} else {
			return Waiting;
		}
		const uint64_t at = start + s.offset;
		got = s.kind == Kind::Code ? at : ReadRef(at, s);
		// A function found through a call must still start as it did.
		if (got && s.kind == Kind::Ref && s.body != nullptr && MatchLive(got, Parse(s.body)) != Live::Match) {
			Logf(true, "game code: %s - the call found leads to 0x%llX, which does not look like it", s.name, static_cast<unsigned long long>(got));
			return Missing;
		}
	}
	if (!got || got >= g_img.size) return Missing;
	e.searched.store(searched);
	e.rva.store(got);
	if (searched || got != s.rva) {
		Logf(false, "game code: %s found at 0x%llX (3.3.93787: 0x%llX)", s.name, static_cast<unsigned long long>(got),
		     static_cast<unsigned long long>(s.rva));
	}
	return Found;
}

void Set(size_t i, State st) {
	const uint8_t was = g_e[i].state.exchange(st);
	if (st == Missing && was != Missing) {
		const Sig& s = kSigs[i];
		if (g_e[i].places > 1) Logf(true, "game code: %s NOT found - the pattern is in %d places - %s off", s.name, g_e[i].places, s.without);
		else Logf(true, "game code: %s NOT found - %s off", s.name, s.without);
	}
}

void BuildSummary() {
	int found = 0, moved = 0;
	std::string waiting, missing;
	for (size_t i = 0; i < kCount; ++i) {
		const uint8_t st = g_e[i].state.load();
		if (st == Found) {
			++found;
			moved += g_e[i].searched.load() || g_e[i].rva.load() != kSigs[i].rva;
			continue;
		}
		std::string& list = st == Missing ? missing : waiting;
		if (!list.empty()) list += ", ";
		list += kSigs[i].name;
		list += " (";
		list += kSigs[i].without;
		list += ")";
	}
	char buf[160];
	if (found == static_cast<int>(kCount)) {
		snprintf(buf, sizeof(buf), "game code: all %d addresses found", found);
		g_summary = buf;
		if (moved) g_summary += " (" + std::to_string(moved) + " moved)";
		return;
	}
	snprintf(buf, sizeof(buf), "game code: %d of %d addresses found", found, static_cast<int>(kCount));
	g_summary = buf;
	if (!missing.empty()) g_summary += " - not found: " + missing;
	if (!waiting.empty()) g_summary += " - waiting for the game to run that code: " + waiting;
}

// An entry still waiting: looked at where 3.3.93787 has it (cheap), no search.
// Never blocks a game thread: skipped while Resolve is busy.
void Late(int i) {
	if (i < 0 || g_e[i].state.load() != Waiting || GetTickCount64() - g_e[i].lastTry < 250) return;
	std::unique_lock<std::recursive_mutex> lock(g_lock, std::try_to_lock);
	if (!lock.owns_lock() || g_e[i].state.load() != Waiting) return;
	const int of = IndexOf(kSigs[i].of);
	if (of >= 0) Late(of);
	if (Try(i, nullptr) == Found) {
		Set(i, Found);
		BuildSummary();
		g_gen.fetch_add(1);
		if (std::all_of(std::begin(g_e), std::end(g_e), [](const Entry& e) { return e.state.load() == Found; })) {
			Logf(false, "%s", g_summary.c_str());
		}
	}
}

}   // namespace

void SetShiftTest(int delta) { g_shift = delta; }

void Resolve(const D2RL::PluginContext* ctx, bool rescan) {
	if (ctx == nullptr || ctx->exeBase == 0) return;
	std::lock_guard<std::recursive_mutex> lock(g_lock);
	const bool first = !g_resolved.load();
	if (first) {
		g_ctx = ctx;
		g_base = ctx->exeBase;
		if (!ReadImage(&g_img)) {
			g_summary = "game code: the image's headers do not read - no address found";
			Logf(true, "%s", g_summary.c_str());
			for (Entry& e : g_e) e.state.store(Missing);
			g_resolved.store(true);
			return;
		}
	}
	std::vector<uint8_t> before(kCount);
	for (size_t i = 0; i < kCount; ++i) before[i] = g_e[i].state.load();
	// Where 3.3.93787 has them, every time (cheap).
	bool waiting = false, differs = false;
	for (size_t i = 0; i < kCount; ++i) {
		const uint8_t st = g_e[i].state.load();
		if (st == Found || (st == Missing && !rescan)) continue;
		if (rescan) g_e[i].places = 0;
		const State now = Try(i, nullptr);
		if (now != Waiting) {
			Set(i, now);
			continue;
		}
		g_e[i].state.store(Waiting);
		waiting = true;
		differs |= g_e[i].differs;
	}
	// The search: at once when code sits where an address was expected (another
	// build); a page that does not read yet is waited for. Later only when more
	// of .text reads than at the last search.
	bool search = false;
	if (waiting) {
		const ULONGLONG now = GetTickCount64();
		if (first) {
			search = differs;
			g_lastSearch = now;
		} else if (rescan) {
			search = true;
			g_lastSearch = now;
		} else if (now - g_lastSearch >= (differs ? 5000u : 60000u)) {
			g_lastSearch = now;
			search = ReadablePages() > g_readableAtSearch;
		}
	}
	if (search) {
		const Text text = ReadText();
		g_readableAtSearch = text.readable;
		g_lastSearch = GetTickCount64();
		if (text.readable < text.pages) {
			Logf(false, "game code: %zu of %zu pages of the game's code read so far (the rest decrypts as the game runs it)", text.readable,
			     text.pages);
		}
		for (size_t i = 0; i < kCount; ++i) {
			if (g_e[i].state.load() == Waiting && kSigs[i].pattern[0] != 0) Set(i, Try(i, &text));
		}
		for (size_t i = 0; i < kCount; ++i) {   // the ones read out of another entry
			if (g_e[i].state.load() == Waiting && kSigs[i].pattern[0] == 0) Set(i, Try(i, nullptr));
		}
	}
	bool changed = first;
	for (size_t i = 0; i < kCount; ++i) changed |= before[i] != g_e[i].state.load();
	BuildSummary();
	g_resolved.store(true);
	if (changed || rescan) g_gen.fetch_add(1);
	if (changed || rescan) {
		bool allFound = true;
		for (const Entry& e : g_e) allFound &= e.state.load() == Found;
		Logf(!allFound && !first, "%s", g_summary.c_str());
	}
}

uint64_t Rva(uint64_t rva) {
	const int i = Index(rva);
	if (i < 0 || !g_resolved.load()) return rva;
	Late(i);
	return g_e[i].state.load() == Found ? g_e[i].rva.load() : 0;
}

uintptr_t Addr(uint64_t rva) {
	const uint64_t r = Rva(rva);
	return r ? g_base + r : 0;
}

namespace {
// Not where 3.3.93787 has it: the loader's own checks would compare the old bytes.
bool Moved(int i) { return g_e[i].searched.load() || g_e[i].rva.load() != kSigs[i].rva; }

// The expected bytes against the bytes there, the ones that move with the code
// (the body's ??) left out.
bool CheckMoved(int i, const void* expected, uint32_t size) {
	const Pattern body = Parse(kSigs[i].body);
	std::vector<uint8_t> live(size);
	if (!ReadLive(live.data(), g_e[i].rva.load(), size)) return false;
	const auto* e = static_cast<const uint8_t*>(expected);
	for (uint32_t k = 0; k < size; ++k) {
		if (k < body.any.size() && body.any[k]) continue;
		if (live[k] != e[k]) return false;
	}
	return true;
}
}   // namespace

bool Check(uint64_t rva, const void* expected, uint32_t size) {
	if (g_ctx == nullptr) return false;
	const int i = Index(rva);
	if (i < 0 || !g_resolved.load()) return g_ctx->CheckExpectedBytes(rva, expected, size);
	if (!Rva(rva)) return false;
	return Moved(i) ? CheckMoved(i, expected, size) : g_ctx->CheckExpectedBytes(rva, expected, size);
}

bool Hook(uint64_t rva, const void* expected, uint32_t size, void* detour, void** original) {
	if (g_ctx == nullptr) return false;
	const int i = Index(rva);
	if (i < 0 || !g_resolved.load()) return g_ctx->InstallInlineHook(rva, expected, size, detour, original);
	const uint64_t at = Rva(rva);
	if (!at) return false;
	if (!Moved(i)) return g_ctx->InstallInlineHook(rva, expected, size, detour, original);
	std::vector<uint8_t> live(size);
	if (!CheckMoved(i, expected, size) || !ReadLive(live.data(), at, size)) return false;
	return g_ctx->InstallInlineHook(at, live.data(), size, detour, original);
}

bool Patch(uint64_t rva, const void* expected, uint32_t expectedSize, const void* bytes, uint32_t size) {
	if (g_ctx == nullptr) return false;
	const int i = Index(rva);
	if (i < 0 || !g_resolved.load()) return g_ctx->PatchBytes(rva, expected, expectedSize, bytes, size);
	const uint64_t at = Rva(rva);
	if (!at) return false;
	if (!Moved(i)) return g_ctx->PatchBytes(rva, expected, expectedSize, bytes, size);
	std::vector<uint8_t> live(expectedSize);
	if (!CheckMoved(i, expected, expectedSize) || !ReadLive(live.data(), at, expectedSize)) return false;
	return g_ctx->PatchBytes(at, live.data(), expectedSize, bytes, size);
}

uint32_t Generation() { return g_gen.load(); }

std::string Report() {
	std::lock_guard<std::recursive_mutex> lock(g_lock);
	std::string r;
	char line[512];
	for (size_t i = 0; i < kCount; ++i) {
		const Sig& s = kSigs[i];
		const uint8_t st = g_e[i].state.load();
		const char* state = st == Found ? (g_e[i].searched.load() || g_e[i].rva.load() != s.rva ? "moved" : "ok")
		                    : st == Missing ? "missing" : "waiting";
		snprintf(line, sizeof(line), "%s\t%s\t0x%llX\t0x%llX\t%s\t%s\n", s.name, s.area, static_cast<unsigned long long>(s.rva),
		         static_cast<unsigned long long>(st == Found ? g_e[i].rva.load() : 0), state, s.without);
		r += line;
	}
	return r;
}

std::string Summary() {
	std::lock_guard<std::recursive_mutex> lock(g_lock);
	return g_summary;
}

bool AllFound() {
	for (const Entry& e : g_e) {
		if (e.state.load() != Found) return false;
	}
	return true;
}

}   // namespace d2rsig
