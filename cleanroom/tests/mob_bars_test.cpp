// The ordinary-monster plate filter (vr/mob_bars.h) on a fake game: a unit, its monster data and its
// MonStats row laid out in a byte array, read through the same reader the hook gets. No game needed.
//
//   mob_bars_test        exit code 0 = every case passed
//
// The test file is shaped after d2r-pcvr's steamvr/monster_bars_test.cpp (MIT, bearstonem); the cases
// are ours - they check the reader's widths (neighbouring bytes are not zero), the plate-call test and
// the answer, and the checks do not vanish in a release build (no assert()).

#include "../../vr/mob_bars.h"

#include <array>
#include <cstdio>
#include <cmath>
#include <cstring>

namespace {

int g_fails = 0;
int g_checks = 0;
#define CHECK(c)                                                  \
	do {                                                          \
		++g_checks;                                               \
		if (!(c)) {                                               \
			++g_fails;                                            \
			std::printf("  FAIL line %d: %s\n", __LINE__, #c);    \
		}                                                         \
	} while (0)

struct Fixture {
	static constexpr uintptr_t base = 0x100000, unit = base, data = base + 0x100, row = base + 0x200;
	std::array<unsigned char, 0x300> bytes{};
	uintptr_t fail = 0;   // a read at this address fails
	unsigned reads = 0;

	template <class T> void Put(uintptr_t address, T value) { std::memcpy(bytes.data() + address - base, &value, sizeof value); }

	Fixture() {
		// Garbage next to every field: a read of the wrong width sees it.
		std::memset(bytes.data(), 0xA5, bytes.size());
		Put(unit, uint32_t(mobbars::kUnitMonster));
		Put(unit + 4, uint32_t(19));
		Put(unit + 0x10, data);
		Put(data, row);
		Put(data + 0x1A, uint8_t(0));   // the byte after it, +0x1B, stays 0xA5
		Put(row, uint16_t(19));         // +2 stays 0xA5A5
		Put(row + 0x3C, uint32_t(mobbars::kRowEnabled | mobbars::kRowKillable));
	}
	bool IsMonster(uintptr_t at = unit) {
		return mobbars::IsMonsterUnit(at, [&](uintptr_t address, auto& value) {
			++reads;
			if (address == fail || address < base || address - base + sizeof value > bytes.size()) return false;
			std::memcpy(&value, bytes.data() + address - base, sizeof value);
			return true;
		});
	}
	bool Hides(uintptr_t at = unit) {
		return mobbars::HidesPlate(at, [&](uintptr_t address, auto& value) {
			++reads;
			if (address == fail || address < base || address - base + sizeof value > bytes.size()) return false;
			std::memcpy(&value, bytes.data() + address - base, sizeof value);
			return true;
		});
	}
};

}  // namespace

int main() {
	using namespace mobbars;

	// An ordinary monster: the plate goes, on every call (both eyes, every frame) - nothing is cached.
	Fixture normal;
	CHECK(normal.Hides());
	CHECK(normal.Hides());

	// Any special-monster bit keeps the plate: champion, unique, super unique, their minions, whatever else.
	for (unsigned bit = 0; bit < 8; ++bit) {
		Fixture f;
		f.Put(Fixture::data + 0x1A, uint8_t(1u << bit));
		CHECK(!f.Hides());
	}
	// ... and no bit set hides it again (a later unit is not judged by an earlier one).
	normal.Put(Fixture::data + 0x1A, uint8_t(8));
	CHECK(!normal.Hides());
	normal.Put(Fixture::data + 0x1A, uint8_t(0));
	CHECK(normal.Hides());

	// Boss, prime evil, NPC, interact, in town keep it - each alone.
	for (unsigned bit : {6u, 7u, 8u, 9u, 10u}) {
		Fixture f;
		f.Put(Fixture::row + 0x3C, uint32_t(kRowEnabled | kRowKillable | (1u << bit)));
		CHECK(!f.Hides());
	}
	// The five act bosses (Andariel, Duriel, Mephisto, Diablo, Baal) with no unique flag: boss + prime evil.
	for (uint32_t boss : {156u, 211u, 242u, 243u, 544u}) {
		Fixture f;
		f.Put(Fixture::unit + 4, boss);
		f.Put(Fixture::row, uint16_t(boss));
		f.Put(Fixture::row + 0x3C, uint32_t(kRowEnabled | kRowKillable | (1u << 6) | (1u << 7)));
		CHECK(!f.Hides());
	}
	// Flags of other bits do not matter (the row has dozens).
	{
		Fixture f;
		f.Put(Fixture::row + 0x3C, uint32_t(kRowEnabled | kRowKillable | (1u << 0) | (1u << 11) | (1u << 20) | (1u << 31)));
		CHECK(f.Hides());
	}
	// Not enabled, not killable, or neither: not a monster this reads as ordinary.
	for (uint32_t flags : {0u, kRowEnabled, kRowKillable, 0xFFFFFFFFu & ~(kRowEnabled | kRowKillable)}) {
		Fixture f;
		f.Put(Fixture::row + 0x3C, flags);
		CHECK(!f.Hides());
	}

	// Not a monster: player, object, missile, item, tile.
	for (uint32_t type : {0u, 2u, 3u, 4u, 5u, 0xFFFFFFFFu}) {
		Fixture f;
		f.Put(Fixture::unit, type);
		CHECK(!f.Hides());
	}

	// Every read that can fail keeps the plate.
	for (uintptr_t at : {Fixture::unit, Fixture::unit + 4, Fixture::unit + 0x10, Fixture::data, Fixture::data + 0x1A, Fixture::row,
	                     Fixture::row + 0x3C}) {
		Fixture f;
		f.fail = at;
		CHECK(!f.Hides());
	}
	// Pointers that are not pointers, a row that is not this monster's, a class id that is not one.
	{ Fixture f; f.Put(Fixture::unit + 0x10, uintptr_t(0)); CHECK(!f.Hides()); }
	{ Fixture f; f.Put(Fixture::unit + 0x10, uintptr_t(0x8000)); CHECK(!f.Hides()); }
	{ Fixture f; f.Put(Fixture::data, uintptr_t(0)); CHECK(!f.Hides()); }
	{ Fixture f; f.Put(Fixture::data, uintptr_t(0xFFFF)); CHECK(!f.Hides()); }
	{ Fixture f; f.Put(Fixture::row, uint16_t(20)); CHECK(!f.Hides()); }
	{ Fixture f; f.Put(Fixture::unit + 4, uint32_t(0x10013)); f.Put(Fixture::row, uint16_t(0x13)); CHECK(!f.Hides()); }
	// A unit that is not a pointer, or one outside the readable memory: nothing is read as ordinary.
	{ Fixture f; CHECK(!f.Hides(0)); CHECK(!f.Hides(0x40)); CHECK(!f.Hides(0xFFFF)); CHECK(f.reads == 0); }
	{ Fixture f; CHECK(!f.Hides(Fixture::base + 0x2F8)); CHECK(!f.Hides(Fixture::base + 0x1000)); }

	// The widths: neighbours of the byte at +0x1A and of the row id are not zero in the fixture, and the
	// plate still goes (a u16 read of the special flags or a u32 read of the row id would see them).
	{
		Fixture f;
		CHECK(f.bytes[Fixture::data - Fixture::base + 0x1B] == 0xA5 && f.bytes[Fixture::row - Fixture::base + 2] == 0xA5);
		CHECK(f.Hides());
	}

	// The call it is for: the plate's anchor helper's, with the setting on, at the head, with an output.
	{
		float out[2] = {};
		const uintptr_t plate = 0x7FF612340000, other = 0x7FF612340100;
		CHECK(IsPlateCall(true, 1, out, plate, plate));
		CHECK(!IsPlateCall(false, 1, out, plate, plate));      // setting off
		CHECK(!IsPlateCall(true, 0, out, plate, plate));       // not the head anchor (a foot or a ground point)
		CHECK(!IsPlateCall(true, 1, nullptr, plate, plate));   // nothing to answer into
		CHECK(!IsPlateCall(true, 1, out, other, plate));       // any other caller: the item labels, the controller's target code...
		CHECK(!IsPlateCall(true, 1, out, 0, 0));               // the plate's address not found: no call is the plate's
		CHECK(!IsPlateCall(true, 1, out, plate, 0));
	}

	// The answer: "on the screen" far off by default; "not on the screen" with the output off the screen too in style 1.
	{
		float out[2] = {10.f, 20.f};
		CHECK(CullAnswer(0, out) && out[0] == kFarAway && out[1] == kFarAway);
		out[0] = out[1] = 5.f;
		CHECK(!CullAnswer(1, out) && out[0] == kFarAway && out[1] == kFarAway);
		out[0] = out[1] = 5.f;
		CHECK(CullAnswer(2, out) && out[0] < -1000.f && out[1] < -1000.f);   // any other value: the default
		CHECK(kFarAway < -4096.f * 2);   // beyond any screen the game draws on, in either direction of a plate's width
	}

	// [hud] monster_height: a monster or an NPC (unit type 1) only, the anchor's y up by a percent of the viewport.
	{
		Fixture f;
		CHECK(f.IsMonster());
		f.Put(Fixture::unit, uint32_t(2));   // an object
		CHECK(!f.IsMonster());
		f.Put(Fixture::unit, uint32_t(0));   // a player
		CHECK(!f.IsMonster());
		Fixture failed;
		failed.fail = Fixture::unit;
		CHECK(!failed.IsMonster());
		CHECK(!failed.IsMonster(0) && !failed.IsMonster(0x8000));
		// the height is not judged by the flags: a champion, a boss, an NPC row are monsters here
		Fixture champion;
		champion.Put(Fixture::data + 0x1A, uint8_t(4));
		CHECK(champion.IsMonster());
	}
	CHECK(ShiftedY(500.0f, 0.0f, 1000.0f) == 500.0f);
	CHECK(std::fabs(ShiftedY(500.0f, 10.0f, 1000.0f) - 400.0f) < 0.01f);      // up: y smaller
	CHECK(std::fabs(ShiftedY(500.0f, -20.0f, 1000.0f) - 700.0f) < 0.01f);     // down
	CHECK(std::fabs(ShiftedY(500.0f, 10.0f, 0.0f) - (500.0f - 108.0f)) < 0.01f);   // a viewport not known: 1080 high
	CHECK(std::fabs(ShiftedY(500.0f, 10.0f, 50.0f) - 392.0f) < 0.01f && std::fabs(ShiftedY(500.0f, 10.0f, 1e9f) - 392.0f) < 0.01f);   // ... nor one that is no size
	CHECK(ClampHeight(1000.0f) == kHeightMax && ClampHeight(-1000.0f) == kHeightMin && ClampHeight(12.5f) == 12.5f);
	CHECK(ClampHeight(std::nanf("")) == 0.0f);
	CHECK(std::fabs(ShiftedY(500.0f, 1000.0f, 1000.0f) - (500.0f - kHeightMax * 10.0f)) < 0.01f);   // clamped to the top of the range

	std::printf("mob_bars_test: %d checks, %d failed\n", g_checks, g_fails);
	return g_fails == 0 ? 0 : 1;
}
