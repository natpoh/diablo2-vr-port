#pragma once
// The game's addresses in the running build. The mod knows them as RVAs of
// D2R 3.3.93787 under D2RLoader 1.3.1; another build moves them, and the
// patterns in game_sigs.h (tools/make_sigs.py) find them again in the image in
// memory. Every place that used an RVA asks here with that same RVA as the key.

#include <cstdint>
#include <string>

namespace D2RL {
struct PluginContext;
}

namespace d2rsig {

// Finds every address in game_sigs.h: first where 3.3.93787 has it, then by a
// search of .text. Before any hook goes in - a hook rewrites the bytes the
// patterns look for. Call again now and then (twice a second is fine): what
// waits for its code to be decrypted is looked for again, cheaply.
// rescan: what is not found is searched for again now, whatever was found
// before stays (a running game cannot change its build) - the Scan button.
void Resolve(const D2RL::PluginContext* ctx, bool rescan = false);

// The RVA in this build of what 3.3.93787 has at `rva`; 0 when it was not found.
// An RVA that is not in the table comes back as it is.
uint64_t Rva(uint64_t rva);
// The game's base + Rva(rva), or 0 when not found.
uintptr_t Addr(uint64_t rva);

// The loader's expected-byte check, hook and patch, at the address this build
// has. Where it moved, bytes that move with the code (call targets,
// RIP-relative offsets) are not compared, and the hook and the patch are given
// the bytes that are there.
bool Check(uint64_t rva, const void* expected, uint32_t size);
bool Hook(uint64_t rva, const void* expected, uint32_t size, void* detour, void** original);
bool Patch(uint64_t rva, const void* expected, uint32_t expectedSize, const void* bytes, uint32_t size);

// For the log and the settings program: "game code: all 54 addresses found
// (3 moved)" or "game code: 52 of 54 addresses found - not found: X (what
// goes off), ... - waiting for the game to run that code: Y (...)".
std::string Summary();
bool AllFound();
// One line per address, for the settings program's Status tab (vrcam writes it
// to d2r_vr_game_code.txt): name, area, 3.3.93787 RVA, RVA now (0 = none),
// ok / moved / waiting / missing, what goes off without it - tab-separated.
std::string Report();
// Bumped whenever the report changes (and on every rescan).
uint32_t Generation();

// Test only ([debug] sig_shift_test): pretend 3.3.93787 had every pattern
// `delta` bytes away, so every pattern must be found by the search. Before Resolve.
void SetShiftTest(int delta);

}   // namespace d2rsig
