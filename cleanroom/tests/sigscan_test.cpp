// The address resolver (cleanroom/sigscan) on a snapshot of the game's image,
// without the game: the snapshot (tools/dump_d2r.py, dump_loader/D2R_image.bin)
// is put into memory as the image and the loader's API is played by this test.
//
//   sigscan_test <D2R_image.bin>          every case, each in a process of its own
//   sigscan_test <D2R_image.bin> <case>   one case: same, shift, waiting, waitshift, broken, hooked
//
// same       nothing moved: every address where 3.3.93787 has it, none searched
// shift      every pattern expected 0x10 bytes off: each found by the search, at its own RVA
// waiting    the keyboard move's page does not read at first: it waits, and is found once it reads
// waitshift  the same with the search: not "not found" while a page does not read
// broken     SetBiome's first bytes changed: that one not found, the rest found
// hooked     a function we hooked still checks out against the bytes the hook proved (the engine unit's check after the potion hide's hook)

#include "game_layout.h"
#include "game_sigs.h"
#include "sigscan.h"

#include <D2RLPlugin/context.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

uintptr_t g_base = 0;
int       g_fails = 0;
int       g_hooks = 0;
uint64_t  g_lastHookRva = 0;

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

bool __cdecl InstallHook(const D2RL::PluginContext*, const D2RL::InlineHookRegistration* h) noexcept {
	// Not installed: only what the loader would be asked for is checked.
	g_lastHookRva = h->rva;
	if (!Same(h->rva, h->expected, h->expectedSize)) return false;
	++g_hooks;
	if (h->original) *h->original = reinterpret_cast<void*>(g_base + h->rva);
	return true;
}

bool __cdecl PatchBytes(const D2RL::PluginContext*, uint64_t rva, const void* expected, uint32_t en, const void* bytes, uint32_t n) noexcept {
	if (!Same(rva, expected, en)) return false;
	DWORD old = 0;
	VirtualProtect(reinterpret_cast<void*>(g_base + rva), n, PAGE_READWRITE, &old);
	std::memcpy(reinterpret_cast<void*>(g_base + rva), bytes, n);
	return true;
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
	g_api.installInlineHook = &InstallHook;
	g_api.patchBytes = &PatchBytes;
	g_ctx.contextSize = D2RL::PluginContextSize;
	g_ctx.api = &g_api;
	g_ctx.exeBase = g_base;
	return ok;
}

void Protect(uint64_t rva, DWORD how) {
	DWORD old = 0;
	VirtualProtect(reinterpret_cast<void*>(g_base + (rva & ~0xFFFull)), 0x1000, how, &old);
}

// Every entry at its own RVA (whatever way it was found).
void ExpectAllAtHome() {
	for (const d2rsig::Sig& s : d2rsig::kSigs) {
		EXPECT(d2rsig::Rva(s.rva) == s.rva, "%s at 0x%llX, wanted 0x%llX", s.name, static_cast<unsigned long long>(d2rsig::Rva(s.rva)),
		       static_cast<unsigned long long>(s.rva));
	}
}

// The expected-byte arrays the code passes keep working, moved or not.
void ExpectLayoutChecks() {
	using namespace d2rcam::layout;
	const Site sites[] = { CamRenderTick, RebuildView, GetProjection, ScreenToRay, ReleaseCamera, GetRenderer, GetGameCamera, GetActiveCamera,
		                   LevelSimTick, FindRooms, EnsureRoomTiled, RoomListPushBack, SetModelRadius, GetModelRadius, ModelRecalcCheck };
	for (const Site& s : sites) {
		EXPECT(d2rsig::Check(s.rva, s.bytes, s.size), "Check 0x%llX", static_cast<unsigned long long>(s.rva));
	}
	// A hook is asked for at the address this build has, with the bytes that are there.
	const int before = g_hooks;
	EXPECT(d2rsig::Hook(RebuildView.rva, RebuildView.bytes, RebuildView.size, nullptr, nullptr), "Hook RebuildView");
	EXPECT(g_hooks == before + 1 && g_lastHookRva == RebuildView.rva, "hook went to 0x%llX", static_cast<unsigned long long>(g_lastHookRva));
	// The background sleep's patch: mov ecx, 10 becomes mov ecx, 0, and the check tells the two apart.
	const uint8_t ten[5] = { 0xB9, 0x0A, 0, 0, 0 }, zero[5] = { 0xB9, 0, 0, 0, 0 };
	EXPECT(d2rsig::Check(0xB6C32, ten, 5) && !d2rsig::Check(0xB6C32, zero, 5), "background sleep before the patch");
	EXPECT(d2rsig::Patch(0xB6C32, ten, 5, zero, 5), "background sleep patch");
	EXPECT(d2rsig::Check(0xB6C32, zero, 5) && !d2rsig::Check(0xB6C32, ten, 5), "background sleep after the patch");
}

void CaseSame() {
	d2rsig::Resolve(&g_ctx);
	EXPECT(d2rsig::AllFound(), "%s", d2rsig::Summary().c_str());
	EXPECT(d2rsig::Summary().find("moved") == std::string::npos, "%s", d2rsig::Summary().c_str());
	ExpectAllAtHome();
	ExpectLayoutChecks();
	EXPECT(d2rsig::Rva(0x123456) == 0x123456, "an RVA not in the table comes back as it is");
}

void CaseShift() {
	d2rsig::SetShiftTest(0x10);
	d2rsig::Resolve(&g_ctx);
	EXPECT(d2rsig::AllFound(), "%s", d2rsig::Summary().c_str());
	EXPECT(d2rsig::Summary().find("moved") != std::string::npos, "%s", d2rsig::Summary().c_str());
	ExpectAllAtHome();
	ExpectLayoutChecks();
}

constexpr uint64_t kKeyMove = 0x8A960;
constexpr uint64_t kRunLock = 0x2A2375C;   // a data address found through a reference in the same page of code as the keyboard move

void CaseWaiting() {
	Protect(kKeyMove, PAGE_NOACCESS);
	d2rsig::Resolve(&g_ctx);
	EXPECT(!d2rsig::AllFound(), "%s", d2rsig::Summary().c_str());
	EXPECT(d2rsig::Rva(kKeyMove) == 0, "the keyboard move before its page reads");
	EXPECT(d2rsig::Rva(kRunLock) == 0, "the run flag before the page of the code that names it reads");
	EXPECT(d2rsig::Summary().find("waiting") != std::string::npos && d2rsig::Summary().find("not found") == std::string::npos, "%s",
	       d2rsig::Summary().c_str());
	Protect(kKeyMove, PAGE_EXECUTE_READ);
	Sleep(300);
	EXPECT(d2rsig::Rva(kKeyMove) == kKeyMove, "the keyboard move once its page reads");
	EXPECT(d2rsig::Rva(kRunLock) == kRunLock, "the run flag once its page reads (an entry is looked at again when its address is asked for)");
	EXPECT(d2rsig::AllFound(), "%s", d2rsig::Summary().c_str());
}

void CaseWaitShift() {
	d2rsig::SetShiftTest(0x10);
	Protect(kKeyMove, PAGE_NOACCESS);
	d2rsig::Resolve(&g_ctx);
	EXPECT(d2rsig::Rva(kKeyMove) == 0, "the keyboard move before its page reads");
	EXPECT(d2rsig::Summary().find("waiting") != std::string::npos && d2rsig::Summary().find("not found") == std::string::npos, "%s",
	       d2rsig::Summary().c_str());
	Protect(kKeyMove, PAGE_EXECUTE_READ);
	Sleep(5200);   // the search again: 5 s after the last, more pages read now
	d2rsig::Resolve(&g_ctx);
	EXPECT(d2rsig::Rva(kKeyMove) == kKeyMove, "the keyboard move once its page reads");
	EXPECT(d2rsig::AllFound(), "%s", d2rsig::Summary().c_str());
}

void CaseBroken() {
	constexpr uint64_t kSetBiome = 0xE68C20;
	DWORD old = 0;
	VirtualProtect(reinterpret_cast<void*>(g_base + kSetBiome), 8, PAGE_READWRITE, &old);
	std::memset(reinterpret_cast<void*>(g_base + kSetBiome), 0xCC, 8);
	d2rsig::Resolve(&g_ctx);
	EXPECT(d2rsig::Rva(kSetBiome) == 0, "SetBiome must not be found");
	const std::string s = d2rsig::Summary();
	EXPECT(s.find("53 of 54") != std::string::npos || s.find("not found: SetBiome") != std::string::npos, "%s", s.c_str());
	for (const d2rsig::Sig& sig : d2rsig::kSigs) {
		if (sig.rva != kSetBiome) EXPECT(d2rsig::Rva(sig.rva) == sig.rva, "%s", sig.name);
	}
}

// A function we hooked is still "the code it was written for" to a check made after the hook: the engine unit asks about the renderer's world-matrix function (0xE3A8C0)
// 7 s after the potion hide has hooked it - the hook's jump is what is there, and the bytes asked about are the ones the hook proved before it patched.
void CaseHooked() {
	constexpr uint64_t kXform = 0xE3A8C0, kInit = 0x771A10;
	static const uint8_t xform[16] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56 };
	static const uint8_t init[19] = { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x90, 0xFD, 0xFF, 0xFF };
	d2rsig::Resolve(&g_ctx);
	EXPECT(d2rsig::AllFound(), "%s", d2rsig::Summary().c_str());
	EXPECT(d2rsig::Check(kXform, xform, sizeof xform) && d2rsig::Check(kInit, init, sizeof init), "before any hook: both check out");
	void* orig = nullptr;
	EXPECT(d2rsig::Hook(kXform, xform, sizeof xform, reinterpret_cast<void*>(0x1000), &orig), "the hook goes in");
	// what the loader's hook does to the entry: a jump
	DWORD old = 0;
	VirtualProtect(reinterpret_cast<void*>(g_base + kXform), 16, PAGE_READWRITE, &old);
	const uint8_t jmp[16] = { 0xE9, 0x10, 0x20, 0x30, 0x40, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC };
	std::memcpy(reinterpret_cast<void*>(g_base + kXform), jmp, sizeof jmp);
	EXPECT(!Same(kXform, xform, sizeof xform), "the entry is patched now: the loader's own check would fail");
	EXPECT(d2rsig::Check(kXform, xform, sizeof xform), "the check after our hook still says: the code it was written for");
	EXPECT(d2rsig::Check(kXform, xform, 8), "a shorter prefix of the same bytes too");
	uint8_t other[16];
	std::memcpy(other, xform, sizeof other);
	other[5] ^= 0xFF;
	EXPECT(!d2rsig::Check(kXform, other, sizeof other), "other bytes are not the proven ones");
	uint8_t longer[24] = {};
	std::memcpy(longer, xform, sizeof xform);
	EXPECT(!d2rsig::Check(kXform, longer, sizeof longer), "bytes beyond what the hook proved are not vouched for");
	EXPECT(!d2rsig::Hook(kXform, xform, sizeof xform, reinterpret_cast<void*>(0x2000), &orig), "a second hook on the same function is not put on top of the first");
	// a function nobody hooked, patched by someone else: still not the expected code
	EXPECT(d2rsig::Check(kInit, init, sizeof init), "the other function is untouched and checks out");
	VirtualProtect(reinterpret_cast<void*>(g_base + kInit), 16, PAGE_READWRITE, &old);
	std::memset(reinterpret_cast<void*>(g_base + kInit), 0xCC, 8);
	EXPECT(!d2rsig::Check(kInit, init, sizeof init), "a function patched by someone else fails its check as ever");
	EXPECT(!d2rsig::Check(kXform + 0x1000000, xform, sizeof xform), "an RVA that is nowhere near is not vouched for");
}

}   // namespace

int main(int argc, char** argv) {
	if (argc < 2) {
		std::printf("sigscan_test <D2R_image.bin> [case]\n");
		return 2;
	}
	if (argc == 2) {   // every case in a process of its own (the resolver keeps its state)
		int bad = 0;
		for (const char* c : { "same", "shift", "waiting", "waitshift", "broken", "hooked" }) {
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
	if (c == "same") CaseSame();
	else if (c == "shift") CaseShift();
	else if (c == "waiting") CaseWaiting();
	else if (c == "waitshift") CaseWaitShift();
	else if (c == "broken") CaseBroken();
	else if (c == "hooked") CaseHooked();
	else return 2;
	std::printf(g_fails ? "== %s: %d FAILED\n" : "== %s: ok\n", c.c_str(), g_fails);
	return g_fails ? 1 : 0;
}
