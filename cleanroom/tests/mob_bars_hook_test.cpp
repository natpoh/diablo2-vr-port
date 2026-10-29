// The ordinary-monster plate hook (vr/mob_bars.cpp) on a snapshot of the game's image, without the game:
// the snapshot (tools/dump_d2r.py, dump_loader/D2R_image.bin) is put into memory as the image and the
// loader's API is played by this test, as in sigscan_test. Checks what an offline test can:
//   - nothing is hooked while the setting is off;
//   - once on, UnitToScreen (0x76A7D0) is hooked with the bytes the image has there, and the plate's
//     caller (0xCE87C) is found;
//   - the detour, called from that return address with fake units, answers for an ordinary monster and
//     passes everything else - a champion, a boss, an NPC, an unreadable record, a call from anywhere
//     else, the setting off, the foot anchor - to the game's own function.
// It cannot say that the game then draws no plate: that needs the game (docs/ in the note).
//
//   mob_bars_hook_test <D2R_image.bin>          every case, each in a process of its own
//   mob_bars_hook_test <D2R_image.bin> <case>   one case: hook, heightonly, worldonly, trackonly, liftonly, broken

#include "sigscan.h"
#include "../../vr/mob_bars.h"
#include "../../vr/plate_world.h"
#include "../../vr/view_gate.h"

#include <D2RLPlugin/context.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

uintptr_t g_base = 0;
int g_fails = 0;
int g_hooks = 0;
uint64_t g_hookRva = 0;
uint32_t g_hookSize = 0;
void* g_detour = nullptr;

#define EXPECT(c, ...)                                         \
	do {                                                       \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
			std::printf(__VA_ARGS__);                          \
			std::printf("\n");                                 \
		}                                                      \
	} while (0)

void __cdecl LogInfo(const D2RL::PluginContext*, const char* m) noexcept { std::printf("  [info] %s\n", m); }
void __cdecl LogWarn(const D2RL::PluginContext*, const char* m) noexcept { std::printf("  [warn] %s\n", m); }

bool Same(uint64_t rva, const void* expected, uint32_t n) {
	__try {
		return std::memcmp(reinterpret_cast<const void*>(g_base + rva), expected, n) == 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool __cdecl CheckExpected(const D2RL::PluginContext*, uint64_t rva, const void* expected, uint32_t n) noexcept { return Same(rva, expected, n); }

// The game's UnitToScreen, played: it answers "on the screen" at 111, 222 and counts its calls.
int g_originalCalls = 0;
bool g_originalShows = true;   // the game's answer: the unit is on the screen
bool OriginalUnitToScreen(void*, void*, float* out, uint8_t) {
	++g_originalCalls;
	if (!g_originalShows) return false;
	if (out) { out[0] = 111.0f; out[1] = 222.0f; }
	return true;
}

bool __cdecl InstallHook(const D2RL::PluginContext*, const D2RL::InlineHookRegistration* h) noexcept {
	if (!Same(h->rva, h->expected, h->expectedSize)) return false;   // the loader refuses a hook whose bytes are not there
	++g_hooks;
	g_hookRva = h->rva;
	g_hookSize = h->expectedSize;
	g_detour = h->target;
	if (h->original) *h->original = reinterpret_cast<void*>(&OriginalUnitToScreen);
	return true;
}

bool __cdecl PatchBytes(const D2RL::PluginContext*, uint64_t, const void*, uint32_t, const void*, uint32_t) noexcept { return false; }

D2RL::PluginApi g_api {};
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
	g_api.installInlineHook = &InstallHook;
	g_api.patchBytes = &PatchBytes;
	g_ctx.contextSize = D2RL::PluginContextSize;
	g_ctx.api = &g_api;
	g_ctx.exeBase = g_base;
	return ok;
}

// A fake monster in this process's memory, laid out as the game's (vr/mob_bars.h).
struct FakeMonster {
	alignas(16) unsigned char unit[0x40] {};
	alignas(16) unsigned char data[0x40] {};
	alignas(16) unsigned char row[0x40] {};
	alignas(16) unsigned char path[0x10] {};   // the unit's path (+0x38): the grid place, 16.16 - x 100.0, z 50.0 cells, i.e. 200 and 100 game units
	template <class T> static void Put(unsigned char* base, size_t at, T v) { std::memcpy(base + at, &v, sizeof v); }
	FakeMonster(uint32_t classId = 19, uint8_t special = 0, uint32_t flags = mobbars::kRowEnabled | mobbars::kRowKillable, uint32_t type = 1) {
		Put(unit, 0, type);
		Put(unit, 4, classId);
		Put(unit, 0x10, reinterpret_cast<uintptr_t>(data));
		Put(unit, 0x38, reinterpret_cast<uintptr_t>(path));
		Put(path, 2, static_cast<uint16_t>(100));
		Put(path, 6, static_cast<uint16_t>(50));
		Put(data, 0, reinterpret_cast<uintptr_t>(row));
		Put(data, 0x1A, special);
		Put(row, 0, static_cast<uint16_t>(classId));
		Put(row, 0x3C, flags);
	}
};

using UnitToScreenFn = bool (*)(void* context, void* unit, float* out, uint8_t above);

bool Near(float a, float b) { return a - b < 0.01f && b - a < 0.01f; }   // (a percent of a size is not exact in a float)

// What the plate's anchor helper does, in machine code at the return address the hook looks for: a call
// into the detour whose return address is exactly 0xCE87C. The page is the image's, written after the hook
// went in (the hook's check of the bytes there is over).
//   sub rsp, 28h; mov rax, detour; call rax   <- returns to 0xCE87C
//   add rsp, 28h; ret
constexpr uint64_t kPlateRet = 0xCE87C;
UnitToScreenFn PlateCaller(void* detour) {
	DWORD old = 0;
	VirtualProtect(reinterpret_cast<void*>(g_base + (kPlateRet & ~0xFFFull)), 0x1000, PAGE_EXECUTE_READWRITE, &old);
	unsigned char* at = reinterpret_cast<unsigned char*>(g_base + kPlateRet - 16);
	const unsigned char head[] = {0x48, 0x83, 0xEC, 0x28, 0x48, 0xB8};
	std::memcpy(at, head, sizeof head);
	std::memcpy(at + sizeof head, &detour, sizeof detour);
	const unsigned char tail[] = {0xFF, 0xD0, 0x48, 0x83, 0xC4, 0x28, 0xC3};
	std::memcpy(at + sizeof head + sizeof detour, tail, sizeof tail);   // the call ends at at + 16 = 0xCE87C
	return reinterpret_cast<UnitToScreenFn>(at);
}

void CaseHook() {
	d2rsig::Resolve(&g_ctx);
	EXPECT(d2rsig::AllFound(), "%s", d2rsig::Summary().c_str());
	EXPECT(d2rsig::Addr(kPlateRet) == g_base + kPlateRet, "the plate's caller found at its own address");

	// Off: nothing hooked, however often it is asked.
	mobbars::SetEnabled(false);
	mobbars::Install(&g_ctx);
	mobbars::Install(&g_ctx);
	EXPECT(g_hooks == 0 && !mobbars::Hooked(), "hooked with the setting off");

	// On: UnitToScreen, with the 19 bytes the image has there; once.
	mobbars::SetEnabled(true);
	mobbars::Install(&g_ctx);
	mobbars::Install(&g_ctx);
	EXPECT(g_hooks == 1 && mobbars::Hooked(), "hooks: %d", g_hooks);
	EXPECT(g_hookRva == 0x76A7D0 && g_hookSize == 19, "hooked 0x%llX, %u bytes", static_cast<unsigned long long>(g_hookRva), g_hookSize);
	if (g_hooks != 1 || !g_detour) return;

	UnitToScreenFn fromPlate = PlateCaller(g_detour);
	UnitToScreenFn direct = reinterpret_cast<UnitToScreenFn>(g_detour);   // a call from here: not the plate's
	float out[2];
	auto reset = [&] { out[0] = out[1] = 5.0f; g_originalCalls = 0; };

	// An ordinary monster, called from the plate's helper: the game's function is not asked, the answer is
	// "on the screen" far off.
	FakeMonster ordinary;
	reset();
	EXPECT(fromPlate(nullptr, ordinary.unit, out, 1) && out[0] == mobbars::kFarAway && out[1] == mobbars::kFarAway && g_originalCalls == 0,
	       "ordinary: out %g %g, original %d", out[0], out[1], g_originalCalls);

	// Everything that is not an ordinary monster gets the game's own answer, untouched.
	FakeMonster champion(19, 0x04), boss(156, 0, mobbars::kRowEnabled | mobbars::kRowKillable | (1u << 6) | (1u << 7)),
	            npc(150, 0, mobbars::kRowEnabled | mobbars::kRowKillable | (1u << 8) | (1u << 9)), player(19, 0, 0, 0), crooked(19);
	FakeMonster* kept[] = {&champion, &boss, &npc, &player, &crooked};
	FakeMonster::Put(crooked.row, 0, uint16_t(20));   // a row that is not this class's
	for (FakeMonster* m : kept) {
		reset();
		EXPECT(fromPlate(nullptr, m->unit, out, 1) && out[0] == 111.0f && out[1] == 222.0f && g_originalCalls == 1,
		       "kept: out %g %g, original %d", out[0], out[1], g_originalCalls);
	}
	reset();
	EXPECT(fromPlate(nullptr, nullptr, out, 1) && g_originalCalls == 1, "no unit");
	reset();
	EXPECT(fromPlate(nullptr, reinterpret_cast<void*>(0x1234), out, 1) && g_originalCalls == 1, "a unit that is not a pointer");
	reset();
	EXPECT(fromPlate(nullptr, reinterpret_cast<void*>(0x30000000000ull), out, 1) && g_originalCalls == 1, "a unit that is not memory");
	reset();
	EXPECT(fromPlate(nullptr, ordinary.unit, out, 0) && g_originalCalls == 1 && out[0] == 111.0f, "not the head anchor");
	reset();
	EXPECT(fromPlate(nullptr, ordinary.unit, nullptr, 1) && g_originalCalls == 1, "no output");

	// The same ordinary monster asked by anyone else (the item labels, the controller's target code...): the game's.
	reset();
	EXPECT(direct(nullptr, ordinary.unit, out, 1) && out[0] == 111.0f && g_originalCalls == 1, "a call from elsewhere");

	// The setting off again, live: the plate is the game's at once.
	mobbars::SetEnabled(false);
	reset();
	EXPECT(fromPlate(nullptr, ordinary.unit, out, 1) && out[0] == 111.0f && g_originalCalls == 1, "setting off");

	// [debug] mob_bars_style 1, d2r-pcvr's answer: "not on the screen", the output far off too.
	mobbars::SetEnabled(true, 1);
	reset();
	EXPECT(!fromPlate(nullptr, ordinary.unit, out, 1) && out[0] == mobbars::kFarAway && out[1] == mobbars::kFarAway && g_originalCalls == 0,
	       "style 1: out %g %g, original %d", out[0], out[1], g_originalCalls);
	reset();
	EXPECT(fromPlate(nullptr, boss.unit, out, 1) && g_originalCalls == 1, "style 1, a boss");

	// And a monster that was ordinary and is now a champion (the next target): nothing is remembered.
	mobbars::SetEnabled(true, 0);
	FakeMonster::Put(ordinary.data, 0x1A, uint8_t(8));
	reset();
	EXPECT(fromPlate(nullptr, ordinary.unit, out, 1) && g_originalCalls == 1, "the next target");

	// F4 only (vr/view_gate.h): the setting on, an ordinary monster, another view - the game's own plate, at once; back in F4 it is hidden again.
	static bool f4 = true;
	viewgate::Set([] { return f4; });
	FakeMonster ordinary2;
	reset();
	EXPECT(fromPlate(nullptr, ordinary2.unit, out, 1) && out[0] == mobbars::kFarAway && g_originalCalls == 0, "F4: the plate is taken off");
	f4 = false;
	reset();
	EXPECT(fromPlate(nullptr, ordinary2.unit, out, 1) && out[0] == 111.0f && out[1] == 222.0f && g_originalCalls == 1, "another view: the game's own plate (out %g %g, original %d)", out[0], out[1], g_originalCalls);
	f4 = true;
	reset();
	EXPECT(fromPlate(nullptr, ordinary2.unit, out, 1) && out[0] == mobbars::kFarAway && g_originalCalls == 0, "back in F4: the plate is taken off again");

	// [hud] monster_height: the game's own answer, its y moved by a percent of the viewport (+ up), a monster's only, in F4 only.
	mobbars::SetEnabled(false);
	mobbars::NoteViewport(1000.0f, 1000.0f);
	mobbars::SetHeight(-20.0f);   // 20 % down: 200 px
	FakeMonster champion2(19, 0x04), object(19, 0, mobbars::kRowEnabled | mobbars::kRowKillable, 2);
	reset();
	EXPECT(fromPlate(nullptr, champion2.unit, out, 1) && out[0] == 111.0f && Near(out[1], 422.0f) && g_originalCalls == 1,
	       "height -20: out %g %g, original %d", out[0], out[1], g_originalCalls);
	reset();
	EXPECT(fromPlate(nullptr, ordinary2.unit, out, 1) && out[0] == 111.0f && Near(out[1], 422.0f) && g_originalCalls == 1, "height: an ordinary monster too (hiding is off)");
	reset();
	EXPECT(fromPlate(nullptr, object.unit, out, 1) && out[1] == 222.0f && g_originalCalls == 1, "height: not a monster (a chest's name): not moved");
	reset();
	EXPECT(fromPlate(nullptr, nullptr, out, 1) && out[1] == 222.0f && g_originalCalls == 1, "height: no unit");
	reset();
	EXPECT(direct(nullptr, champion2.unit, out, 1) && out[1] == 222.0f && g_originalCalls == 1, "height: a call from elsewhere is not moved");
	reset();
	EXPECT(fromPlate(nullptr, champion2.unit, out, 0) && out[1] == 222.0f && g_originalCalls == 1, "height: not the head anchor");
	f4 = false;
	reset();
	EXPECT(fromPlate(nullptr, champion2.unit, out, 1) && out[1] == 222.0f && g_originalCalls == 1, "height: another view is not moved");
	f4 = true;
	mobbars::SetHeight(25.0f);   // up: y smaller
	reset();
	EXPECT(fromPlate(nullptr, champion2.unit, out, 1) && Near(out[1], 222.0f - 250.0f), "height +25: y %g", out[1]);
	mobbars::SetHeight(500.0f);   // clamped to +40
	reset();
	EXPECT(fromPlate(nullptr, champion2.unit, out, 1) && Near(out[1], 222.0f - 400.0f), "height clamped: y %g", out[1]);
	mobbars::NoteViewport(0.0f, 0.0f);   // not known: counts as 1080
	mobbars::SetHeight(10.0f);
	reset();
	EXPECT(fromPlate(nullptr, champion2.unit, out, 1) && Near(out[1], 222.0f - 108.0f), "viewport unknown: y %g", out[1]);
	mobbars::SetHeight(0.0f);
	reset();
	EXPECT(fromPlate(nullptr, champion2.unit, out, 1) && out[1] == 222.0f && g_originalCalls == 1, "height 0: the game's own");
	// Hiding is first: an ordinary monster with both settings on is off the screen, not moved.
	mobbars::SetEnabled(true);
	mobbars::SetHeight(-20.0f);
	mobbars::NoteViewport(1000.0f, 1000.0f);
	reset();
	EXPECT(fromPlate(nullptr, ordinary2.unit, out, 1) && out[0] == mobbars::kFarAway && g_originalCalls == 0, "both settings: an ordinary monster is hidden");
	reset();
	EXPECT(fromPlate(nullptr, champion2.unit, out, 1) && Near(out[1], 422.0f) && g_originalCalls == 1, "both settings: a champion is moved");

	// [hud] monster_bar_room (vr/plate_world.h): the game's plate is drawn at a FIXED point, the monster's place and the ray are recorded.
	mobbars::SetEnabled(false);
	mobbars::SetHeight(0.0f);
	mobbars::NoteViewport(2000.0f, 1000.0f);
	static int rays = 0;
	static bool rayOk = true;
	plateworld::RaySlot().store([](float sx, float sy, float o[3], float d[3]) {
		++rays;
		o[0] = 1.0f; o[1] = 2.0f; o[2] = 3.0f;
		d[0] = sx; d[1] = sy; d[2] = 1.0f;   // (the point it was asked for, so the test can read it back)
		return rayOk;
	});
	plateworld::Capture cap;
	const uint64_t soon = GetTickCount64();
	// not in force: the game's own answer, nothing recorded
	plateworld::Clear();
	plateworld::SetActive(false);
	reset();
	plateworld::Seen sn;
	EXPECT(fromPlate(nullptr, champion2.unit, out, 1) && out[0] == 111.0f && out[1] == 222.0f && !plateworld::GetCapture(&cap, soon) && rays == 0, "world not active: untouched");
	EXPECT(!plateworld::GetSeen(&sn, GetTickCount64()), "world not active: nothing for the log either");
	// in force: the point is fixed, the record has the monster and the game's own point
	plateworld::SetActive(true);
	reset();
	EXPECT(fromPlate(nullptr, champion2.unit, out, 1) && Near(out[0], 1000.0f) && Near(out[1], 300.0f) && g_originalCalls == 1, "world: the fixed point (out %g %g)", out[0], out[1]);
	EXPECT(plateworld::GetCapture(&cap, GetTickCount64()) && cap.valid && Near(cap.foot[0], 200.0f) && Near(cap.foot[1], 100.0f) && cap.cls == 19 &&
	       Near(cap.px[0], 111.0f) && Near(cap.px[1], 222.0f) && Near(cap.rayD[0], 111.0f) && Near(cap.rayO[1], 2.0f) && rays == 1,
	       "the record: foot %g %g, class %u, game's point %g %g", cap.foot[0], cap.foot[1], cap.cls, cap.px[0], cap.px[1]);
	// the record carries what the tick checks the plate against (the point the game was given, the viewport it is a point of) and the monster it is of
	EXPECT(cap.unit == reinterpret_cast<uintptr_t>(champion2.unit) && Near(cap.fixed[0], 1000.0f) && Near(cap.fixed[1], 300.0f) && Near(cap.view[0], 2000.0f) && Near(cap.view[1], 1000.0f),
	       "the record's fixed point %g %g of a %g x %g viewport", cap.fixed[0], cap.fixed[1], cap.view[0], cap.view[1]);
	// ... and the hook says how far it got, for the line in the log: Hung, the monster, its class and where the game had put its head
	EXPECT(plateworld::GetSeen(&sn, GetTickCount64()) && sn.why == plateworld::Why::Hung && sn.unit == cap.unit && sn.cls == 19 && Near(sn.px[0], 111.0f) && Near(sn.px[1], 222.0f),
	       "seen: why %d, class %u, point %g %g", static_cast<int>(sn.why), sn.cls, sn.px[0], sn.px[1]);
	// an ordinary monster with hiding on is hidden, not hung (hiding first), and nothing is recorded
	plateworld::Clear();
	mobbars::SetEnabled(true);
	reset();
	EXPECT(fromPlate(nullptr, ordinary2.unit, out, 1) && out[0] == mobbars::kFarAway && !plateworld::GetCapture(&cap, GetTickCount64()), "world: an ordinary monster is hidden, not hung");
	EXPECT(plateworld::GetSeen(&sn, GetTickCount64()) && sn.why == plateworld::Why::Hidden && sn.unit == reinterpret_cast<uintptr_t>(ordinary2.unit) && sn.cls == 19,
	       "world: the log is told it was hidden (ordinary_mob_bars), why %d", static_cast<int>(sn.why));
	mobbars::SetEnabled(false);
	// not a monster, no unit, no ray, no viewport, the game's own answer "off screen", another caller, another view: untouched / nothing recorded
	plateworld::Clear();
	reset();
	EXPECT(fromPlate(nullptr, object.unit, out, 1) && out[0] == 111.0f && !plateworld::GetCapture(&cap, GetTickCount64()), "world: a chest's name stays the game's");
	EXPECT(!plateworld::GetSeen(&sn, GetTickCount64()), "world: a chest's name is not a target for the log");
	rayOk = false;
	reset();
	EXPECT(fromPlate(nullptr, champion2.unit, out, 1) && out[0] == 111.0f && out[1] == 222.0f && !plateworld::GetCapture(&cap, GetTickCount64()), "world: no ray, the game's plate stays over the monster");
	EXPECT(plateworld::GetSeen(&sn, GetTickCount64()) && sn.why == plateworld::Why::NoRay && sn.unit == reinterpret_cast<uintptr_t>(champion2.unit), "world: the log is told: no ray (why %d)", static_cast<int>(sn.why));
	rayOk = true;
	mobbars::NoteViewport(0.0f, 0.0f);
	reset();
	EXPECT(fromPlate(nullptr, champion2.unit, out, 1) && out[0] == 111.0f && !plateworld::GetCapture(&cap, GetTickCount64()), "world: viewport not known, the game's plate stays");
	EXPECT(plateworld::GetSeen(&sn, GetTickCount64()) && sn.why == plateworld::Why::NoViewport, "world: the log is told: no viewport (why %d)", static_cast<int>(sn.why));
	mobbars::NoteViewport(2000.0f, 1000.0f);
	g_originalShows = false;
	reset();
	EXPECT(!fromPlate(nullptr, champion2.unit, out, 1) && !plateworld::GetCapture(&cap, GetTickCount64()), "world: a monster off the screen is the game's answer (not shown)");
	EXPECT(plateworld::GetSeen(&sn, GetTickCount64()) && sn.why == plateworld::Why::NotShown, "world: the log is told: not on the screen (why %d)", static_cast<int>(sn.why));
	g_originalShows = true;
	reset();
	EXPECT(direct(nullptr, champion2.unit, out, 1) && out[0] == 111.0f && !plateworld::GetCapture(&cap, GetTickCount64()), "world: a call from elsewhere is not the plate's");
	f4 = false;
	reset();
	plateworld::Clear();
	EXPECT(fromPlate(nullptr, champion2.unit, out, 1) && out[0] == 111.0f && !plateworld::GetCapture(&cap, GetTickCount64()) && !plateworld::GetSeen(&sn, GetTickCount64()), "world: another view, the game's plate, nothing for the log");
	f4 = true;
	FakeMonster lost(19);
	FakeMonster::Put(lost.unit, 0x38, uintptr_t(0));   // a path that cannot be read: no foot, no record
	reset();
	EXPECT(fromPlate(nullptr, lost.unit, out, 1) && out[0] == 111.0f && !plateworld::GetCapture(&cap, GetTickCount64()), "world: no foot, the game's plate stays");
	EXPECT(plateworld::GetSeen(&sn, GetTickCount64()) && sn.why == plateworld::Why::NoFoot && sn.unit == reinterpret_cast<uintptr_t>(lost.unit), "world: the log is told: no foot (why %d)", static_cast<int>(sn.why));
	// [hud] monster_far / monster_depth_body (no bar in the room): the hook notes where the targeted monster stands and leaves the plate where the game puts it
	mobbars::SetEnabled(false);
	mobbars::SetHeight(0.0f);
	plateworld::SetActive(false);
	plateworld::Clear();
	plateworld::SetTrack(true);
	FakeMonster tracked(19, 0x04);
	reset();
	EXPECT(fromPlate(nullptr, tracked.unit, out, 1) && out[0] == 111.0f && out[1] == 222.0f && g_originalCalls == 1, "tracking: the game's own plate (out %g %g)", out[0], out[1]);
	EXPECT(plateworld::GetSeen(&sn, GetTickCount64()) && sn.footOk && Near(sn.foot[0], 200.0f) && Near(sn.foot[1], 100.0f) && sn.unit == reinterpret_cast<uintptr_t>(tracked.unit) &&
	       sn.cls == 19, "tracking: the monster's place %g %g (foot ok %d)", sn.foot[0], sn.foot[1], sn.footOk ? 1 : 0);
	EXPECT(!plateworld::GetCapture(&cap, GetTickCount64()), "tracking: nothing recorded to hang a bar from");
	plateworld::Clear();
	reset();
	EXPECT(fromPlate(nullptr, object.unit, out, 1) && out[1] == 222.0f && !plateworld::GetSeen(&sn, GetTickCount64()), "tracking: a chest's name is no monster");
	reset();
	EXPECT(direct(nullptr, tracked.unit, out, 1) && out[1] == 222.0f && !plateworld::GetSeen(&sn, GetTickCount64()), "tracking: a call from elsewhere is not the plate's");
	f4 = false;
	reset();
	EXPECT(fromPlate(nullptr, tracked.unit, out, 1) && out[1] == 222.0f && !plateworld::GetSeen(&sn, GetTickCount64()), "tracking: another view, nothing noted");
	f4 = true;
	g_originalShows = false;
	reset();
	EXPECT(!fromPlate(nullptr, tracked.unit, out, 1) && !plateworld::GetSeen(&sn, GetTickCount64()), "tracking: a monster off the screen has no plate to note");
	g_originalShows = true;
	mobbars::NoteViewport(1000.0f, 1000.0f);
	mobbars::SetHeight(-20.0f);   // both: the plate moved as ever, and the monster noted
	reset();
	EXPECT(fromPlate(nullptr, tracked.unit, out, 1) && Near(out[1], 422.0f) && plateworld::GetSeen(&sn, GetTickCount64()) && sn.footOk, "tracking with monster_height: moved and noted");
	mobbars::SetHeight(0.0f);
	plateworld::Clear();
	plateworld::SetTrack(false);
	reset();
	EXPECT(fromPlate(nullptr, tracked.unit, out, 1) && out[1] == 222.0f && !plateworld::GetSeen(&sn, GetTickCount64()), "tracking off: nothing noted, the game's plate");

	// [hud] monster_over_head: the game's own answer given another y (x kept), so the plate's lower edge hangs the gap over the head
	// a pinhole camera 60 deg across a 1000 px viewport, at (60, 1.7, 25) metres, upm 3, looking along +z: the fake monster (grid 100, 50 cells = 66.7, 33.3 m) is some 11 m off
	plateworld::Clear();
	mobbars::NoteViewport(1000.0f, 1000.0f);
	plateworld::SetWorld(0.0f, 3.0f);
	plateworld::SetGameGap(19.0f);
	plateworld::Configure(false, 100.0f, 100.0f);   // the gap: 100 cm
	plateworld::RaySlot().store([](float sx, float sy, float o[3], float d[3]) {
		o[0] = 60.0f * 3.0f; o[1] = 1.7f * 3.0f; o[2] = 25.0f * 3.0f;
		d[0] = (sx - 500.0f) / 866.0f; d[1] = -(sy - 500.0f) / 866.0f; d[2] = 1.0f;
		return true;
	});
	plateworld::SetLift(true);
	FakeMonster over(19, 0x04);
	reset();
	EXPECT(fromPlate(nullptr, over.unit, out, 1) && out[0] == 111.0f && g_originalCalls == 1 && out[1] < 222.0f - 30.0f, "lift 100 cm: x the game's, y higher (out %g %g)", out[0], out[1]);
	EXPECT(plateworld::GetSeen(&sn, GetTickCount64()) && sn.footOk && sn.unit == reinterpret_cast<uintptr_t>(over.unit), "lift: the monster noted");
	EXPECT(!plateworld::GetCapture(&cap, GetTickCount64()), "lift: nothing cut out, nothing recorded for the room");
	const float lifted100 = out[1];
	plateworld::Configure(false, 0.0f, 100.0f);   // the lower edge at the head: the anchor the game's own gap (19 px) under the head's point
	reset();
	EXPECT(fromPlate(nullptr, over.unit, out, 1) && out[0] == 111.0f && Near(out[1], 222.0f + 19.0f), "lift 0 cm: the game's own gap (19 px) under the head's point: %g", out[1]);
	plateworld::Configure(false, 100.0f, 100.0f);
	plateworld::SetGameGap(40.0f);   // a bigger gap of the game's own: the anchor goes lower by as much (the plate's lower edge stays where the gap puts it)
	reset();
	EXPECT(fromPlate(nullptr, over.unit, out, 1) && Near(out[1] - lifted100, 40.0f - 19.0f), "the game's own gap: %g vs %g", out[1], lifted100);
	plateworld::SetGameGap(19.0f);
	// not a monster, a call from elsewhere, another view, the game says off screen: untouched
	reset();
	EXPECT(fromPlate(nullptr, object.unit, out, 1) && out[1] == 222.0f, "lift: a chest's name stays the game's");
	reset();
	EXPECT(direct(nullptr, over.unit, out, 1) && out[1] == 222.0f, "lift: a call from elsewhere is not the plate's");
	f4 = false;
	reset();
	EXPECT(fromPlate(nullptr, over.unit, out, 1) && out[1] == 222.0f, "lift: another view, the game's plate");
	f4 = true;
	g_originalShows = false;
	reset();
	EXPECT(!fromPlate(nullptr, over.unit, out, 1), "lift: a monster off the screen is the game's answer");
	g_originalShows = true;
	// ordinary monsters: hiding comes first
	mobbars::SetEnabled(true);
	FakeMonster plain;
	reset();
	EXPECT(fromPlate(nullptr, plain.unit, out, 1) && out[0] == mobbars::kFarAway && g_originalCalls == 0, "lift: an ordinary monster is hidden, not lifted");
	reset();
	EXPECT(fromPlate(nullptr, over.unit, out, 1) && out[1] < 222.0f - 30.0f, "lift: a champion is lifted with hiding on");
	mobbars::SetEnabled(false);
	// monster_height does not add to it (the lift replaces it while on)
	mobbars::SetHeight(-20.0f);
	reset();
	EXPECT(fromPlate(nullptr, over.unit, out, 1) && Near(out[1], lifted100), "lift with monster_height: the lift only (%g vs %g)", out[1], lifted100);
	mobbars::SetHeight(0.0f);
	// what it cannot work out leaves the game's own place
	plateworld::RaySlot().store([](float, float, float[3], float[3]) { return false; });
	reset();
	EXPECT(fromPlate(nullptr, over.unit, out, 1) && out[1] == 222.0f, "lift: no ray, the game's place");
	plateworld::RaySlot().store([](float sx, float sy, float o[3], float d[3]) {
		o[0] = 60.0f * 3.0f; o[1] = 1.7f * 3.0f; o[2] = 25.0f * 3.0f;
		d[0] = (sx - 500.0f) / 866.0f; d[1] = -(sy - 500.0f) / 866.0f; d[2] = 1.0f;
		return true;
	});
	plateworld::SetWorld(0.0f, 0.0f);
	reset();
	EXPECT(fromPlate(nullptr, over.unit, out, 1) && out[1] == 222.0f, "lift: the world not known, the game's place");
	plateworld::SetWorld(0.0f, 3.0f);
	mobbars::NoteViewport(0.0f, 0.0f);
	reset();
	EXPECT(fromPlate(nullptr, over.unit, out, 1) && out[1] == 222.0f, "lift: the viewport not known, the game's place");
	mobbars::NoteViewport(1000.0f, 1000.0f);
	// the plate cut out and hung in the room, when asked for, has its own way and the lift stands aside
	plateworld::Configure(true, 15.0f, 100.0f);
	plateworld::SetActive(true);
	reset();
	EXPECT(fromPlate(nullptr, over.unit, out, 1) && Near(out[0], 500.0f) && Near(out[1], 300.0f), "the room path: the fixed point, not the lift (out %g %g)", out[0], out[1]);
	plateworld::SetActive(false);
	plateworld::Configure(false, 15.0f, 100.0f);
	// off: the game's own plate
	plateworld::SetLift(false);
	reset();
	EXPECT(fromPlate(nullptr, over.unit, out, 1) && out[1] == 222.0f && g_originalCalls == 1, "lift off: the game's plate");
	plateworld::SetActive(false);
	plateworld::RaySlot().store(nullptr);
	viewgate::Set(nullptr);
}

void CaseLiftOnly() {
	// monster_over_head alone puts the hook in (every other setting off)
	d2rsig::Resolve(&g_ctx);
	mobbars::SetEnabled(false);
	mobbars::SetHeight(0.0f);
	plateworld::Configure(false, 15.0f, 100.0f);
	plateworld::SetTrack(false);
	plateworld::SetLift(false);
	mobbars::Install(&g_ctx);
	EXPECT(g_hooks == 0 && !mobbars::Hooked(), "hooked with every setting off");
	plateworld::SetLift(true);
	mobbars::Install(&g_ctx);
	mobbars::Install(&g_ctx);
	EXPECT(g_hooks == 1 && mobbars::Hooked() && g_hookRva == 0x76A7D0, "hooks: %d", g_hooks);
}

void CaseTrackOnly() {
	// monster_far / monster_depth_body alone put the hook in (every other setting off)
	d2rsig::Resolve(&g_ctx);
	mobbars::SetEnabled(false);
	mobbars::SetHeight(0.0f);
	plateworld::Configure(false, 15.0f, 100.0f);
	plateworld::SetTrack(false);
	mobbars::Install(&g_ctx);
	EXPECT(g_hooks == 0 && !mobbars::Hooked(), "hooked with every setting off");
	plateworld::SetTrack(true);
	mobbars::Install(&g_ctx);
	mobbars::Install(&g_ctx);
	EXPECT(g_hooks == 1 && mobbars::Hooked() && g_hookRva == 0x76A7D0, "hooks: %d", g_hooks);
}

void CaseWorldOnly() {
	// monster_bar_room alone puts the hook in (the other two settings off), and the hook stays out while nothing asks for it.
	d2rsig::Resolve(&g_ctx);
	mobbars::SetEnabled(false);
	mobbars::SetHeight(0.0f);
	plateworld::Configure(false, 15.0f, 100.0f);
	mobbars::Install(&g_ctx);
	EXPECT(g_hooks == 0 && !mobbars::Hooked(), "hooked with every setting off");
	plateworld::Configure(true, 15.0f, 100.0f);
	mobbars::Install(&g_ctx);
	mobbars::Install(&g_ctx);
	EXPECT(g_hooks == 1 && mobbars::Hooked() && g_hookRva == 0x76A7D0, "hooks: %d", g_hooks);
}

void CaseHeightOnly() {
	// monster_height alone (ordinary_mob_bars off) puts the hook in, and nothing else is asked of it.
	d2rsig::Resolve(&g_ctx);
	mobbars::SetEnabled(false);
	mobbars::SetHeight(0.0f);
	mobbars::Install(&g_ctx);
	EXPECT(g_hooks == 0 && !mobbars::Hooked(), "hooked with both settings off");
	mobbars::SetHeight(-10.0f);
	mobbars::Install(&g_ctx);
	mobbars::Install(&g_ctx);
	EXPECT(g_hooks == 1 && mobbars::Hooked() && g_hookRva == 0x76A7D0, "hooks: %d", g_hooks);
	if (g_hooks != 1 || !g_detour) return;
	UnitToScreenFn fromPlate = PlateCaller(g_detour);
	float out[2] = {};
	FakeMonster ordinary, champion(19, 0x04);
	mobbars::NoteViewport(2000.0f, 2000.0f);
	g_originalCalls = 0;
	EXPECT(fromPlate(nullptr, ordinary.unit, out, 1) && Near(out[1], 222.0f + 200.0f) && g_originalCalls == 1, "height only: an ordinary monster's plate stays, moved (y %g)", out[1]);
	EXPECT(fromPlate(nullptr, champion.unit, out, 1) && Near(out[1], 222.0f + 200.0f) && g_originalCalls == 2, "height only: a champion's, moved (y %g)", out[1]);
}

void CaseBroken() {
	// UnitToScreen's first bytes are not the game's: not in this build, nothing hooked, nothing more asked.
	DWORD old = 0;
	VirtualProtect(reinterpret_cast<void*>(g_base + 0x76A7D0), 8, PAGE_READWRITE, &old);
	std::memset(reinterpret_cast<void*>(g_base + 0x76A7D0), 0xCC, 8);
	d2rsig::Resolve(&g_ctx);
	EXPECT(d2rsig::Rva(0x76A7D0) == 0, "UnitToScreen must not be found");
	mobbars::SetEnabled(true);
	mobbars::Install(&g_ctx);
	mobbars::Install(&g_ctx);
	EXPECT(g_hooks == 0 && !mobbars::Hooked(), "hooked a build it does not know");
}

}   // namespace

int main(int argc, char** argv) {
	if (argc < 2) {
		std::printf("mob_bars_hook_test <D2R_image.bin> [case]\n");
		return 2;
	}
	if (argc == 2) {   // every case in a process of its own (the resolver and the hook keep their state)
		int bad = 0;
		for (const char* c : { "hook", "heightonly", "worldonly", "trackonly", "liftonly", "broken" }) {
			char exe[MAX_PATH];
			GetModuleFileNameA(nullptr, exe, MAX_PATH);
			std::string cmd = std::string("\"") + exe + "\" \"" + argv[1] + "\" " + c;
			STARTUPINFOA si { sizeof(si) };
			PROCESS_INFORMATION pi {};
			if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return 2;
			WaitForSingleObject(pi.hProcess, INFINITE);
			DWORD code = 1;
			GetExitCodeProcess(pi.hProcess, &code);
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
			bad += code != 0;
		}
		std::printf(bad ? "%d case(s) FAILED\n" : "all cases passed\n", bad);
		return bad ? 1 : 0;
	}
	if (!Load(argv[1])) {
		std::printf("cannot read %s\n", argv[1]);
		return 2;
	}
	const std::string c = argv[2];
	std::printf("== %s\n", c.c_str());
	if (c == "hook") CaseHook();
	else if (c == "heightonly") CaseHeightOnly();
	else if (c == "worldonly") CaseWorldOnly();
	else if (c == "trackonly") CaseTrackOnly();
	else if (c == "liftonly") CaseLiftOnly();
	else if (c == "broken") CaseBroken();
	else return 2;
	std::printf(g_fails ? "== %s: %d FAILED\n" : "== %s: ok\n", c.c_str(), g_fails);
	return g_fails ? 1 : 0;
}
