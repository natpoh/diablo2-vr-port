// The places in xr.cpp's render-target heap (vr/rtv_slots.h): handed out and given back, never wrapped over a live chain.
//
//   rtv_slots_test        exit code 0 = every case passed

#include "../../vr/rtv_slots.h"

#include <cstdio>
#include <vector>

namespace {
int g_fails = 0;
int g_checks = 0;
#define CHECK(c)                                               \
	do {                                                       \
		++g_checks;                                            \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s\n", __LINE__, #c); \
		}                                                      \
	} while (0)

// A chain as xr.cpp holds one: where its places start and how many.
struct Chain {
	int at = -1;
	int n = 0;
};
}  // namespace

int main() {
	using Heap = rtvslots::Slots<32>;

	// ---- the basics ----
	{
		Heap h;
		CHECK(h.Used() == 0);
		CHECK(h.Take(3) == 0 && h.Take(3) == 3 && h.Take(1) == 6);
		CHECK(h.Used() == 7 && h.IsUsed(0) && h.IsUsed(6) && !h.IsUsed(7));
		h.Give(3, 3);
		CHECK(h.Used() == 4 && !h.IsUsed(3) && h.IsUsed(2) && h.IsUsed(6));
		CHECK(h.Take(3) == 3);                       // the freed run again, not the end of the heap
		CHECK(h.Take(2) == 7);                       // a run that does not fit in a hole goes past it
		h.Give(0, 1000);                             // (only the heap's own places are given back)
		h.Give(-5, 8);
		CHECK(h.Used() >= 0 && h.Used() <= 32);
		h.Reset();
		CHECK(h.Used() == 0 && h.Take(32) == 0 && h.Take(1) == -1);   // the whole heap, then none
		CHECK(h.Take(0) == -1 && h.Take(-1) == -1 && h.Take(33) == -1);
	}

	// ---- full: nothing is taken, nothing of anybody else's is handed out ----
	{
		Heap h;
		CHECK(h.Take(30) == 0);
		CHECK(h.Take(3) == -1 && h.Used() == 30);    // (a failed ask takes nothing)
		CHECK(h.Take(2) == 30 && h.Take(1) == -1);
		h.Give(10, 2);
		CHECK(h.Take(3) == -1 && h.Take(2) == 10);   // a hole of two takes two, not three
	}

	// ---- holes: a run is found past a used place ----
	{
		Heap h;
		for (int i = 0; i < 32; i += 4) CHECK(h.Take(1) == i / 4);   // 0..7 used
		h.Give(0, 8);
		for (int i = 0; i < 8; ++i) h.Take(1);
		CHECK(h.Used() == 8);
		h.Give(2, 1);
		h.Give(5, 1);
		CHECK(h.Take(2) == 8);                       // the single holes at 2 and 5 are no room for two
		CHECK(h.Take(1) == 2 && h.Take(1) == 5);
	}

	// ---- the monster bar's story: chains made again and again must never take a live chain's places ----
	{
		// the toolbar, the map, the gesture zones, a menu's panel stand; the monster bar's chain is made and unmade 100000 times, 3 images each.
		Heap h;
		Chain live[4];
		for (int i = 0; i < 4; ++i) {
			live[i].n = 3;
			live[i].at = h.Take(3);
			CHECK(live[i].at >= 0);
		}
		const int base = h.Used();
		Chain bar;
		bool overlap = false;
		for (int i = 0; i < 100000; ++i) {
			if (bar.at >= 0) h.Give(bar.at, bar.n);   // EnsureHudOut: the old chain's places back
			bar.n = 3;
			bar.at = h.Take(bar.n);
			if (bar.at < 0) { overlap = true; break; }
			for (const Chain& c : live)
				if (bar.at < c.at + c.n && c.at < bar.at + bar.n) overlap = true;   // the same place twice = a view drawn into the wrong chain's image
			if (h.Used() != base + 3) overlap = true;
		}
		CHECK(!overlap);
		CHECK(h.Used() == base + 3);
	}

	// ---- many chains of many sizes, taken and given at random: a place is never in two chains, and what is given is free again ----
	{
		Heap h;
		std::vector<Chain> chains;
		unsigned seed = 12345u;
		auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
		bool bad = false;
		for (int step = 0; step < 50000; ++step) {
			if (chains.empty() || rnd() % 3 != 0) {
				Chain c;
				c.n = 1 + static_cast<int>(rnd() % 4);
				c.at = h.Take(c.n);
				if (c.at >= 0) {
					for (const Chain& o : chains)
						if (c.at < o.at + o.n && o.at < c.at + c.n) bad = true;
					chains.push_back(c);
				}
			} else {
				const size_t i = rnd() % chains.size();
				h.Give(chains[i].at, chains[i].n);
				chains.erase(chains.begin() + static_cast<long>(i));
			}
			int want = 0;
			for (const Chain& c : chains) want += c.n;
			if (h.Used() != want) bad = true;
		}
		CHECK(!bad);
	}

	std::printf("rtv_slots_test: %d checks, %d failed\n", g_checks, g_fails);
	return g_fails == 0 ? 0 : 1;
}
