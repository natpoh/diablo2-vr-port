// [weight] world_contact: D2R_VR_Settings' maker of the level pieces' solid parts (tools/settings/solids_assets.cpp) against the player's own game, the way
// props_assets_test runs the props' catalogue. Run: solids_assets_test.exe <game folder> <empty scratch folder> [<entity id> ...]
//
// Checks: the catalogue is made, reads back, has pieces and models with parts, leaves foliage out; the second run finds it made (no remake); each entity id
// given is looked for and its models' parts printed.

#include "solids_assets.h"
#include "world_solids.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int wmain(int argc, wchar_t** argv) {
	if (argc < 3) {
		std::printf("solids_assets_test <game folder> <empty scratch folder> [<entity id> ...]\n");
		return 0;
	}
	int fails = 0;
	const solidsassets::Result r = solidsassets::Prepare(argv[1], argv[2], true);
	std::wprintf(L"made: %ls\n", r.text.c_str());
	if (r.state != solidsassets::kMade || r.pieces < 1000 || r.withParts < 500 || r.foliage < 50) {
		std::printf("  FAIL: not made, or too few (%d pieces, %d models with parts, %d foliage)\n", r.pieces, r.withParts, r.foliage);
		++fails;
	}
	const solidsassets::Result again = solidsassets::Prepare(argv[1], argv[2], false);
	std::wprintf(L"again: %ls\n", again.text.c_str());
	if (again.state != solidsassets::kMade || again.wrote) {
		std::printf("  FAIL: made again on the same build\n");
		++fails;
	}
	// the archives not opening (the game starting at that moment, Serg 0.192.19: "error 2") with this make's catalogue there: kept, no red line
	const solidsassets::Result busy = solidsassets::Prepare(L"C:\\no_such_game_folder", argv[2], false);
	std::wprintf(L"archives not there: %ls\n", busy.text.c_str());
	if (busy.state != solidsassets::kMade || busy.text.find(L"kept") == std::wstring::npos) {
		std::printf("  FAIL: the catalogue made before is not kept when the archives do not open\n");
		++fails;
	}
	std::wstring bin = argv[2];
	if (!bin.empty() && bin.back() != L'\\') bin += L'\\';
	bin += L"d2r_vr_props\\solids.bin";
	HANDLE f = CreateFileW(bin.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	std::vector<uint8_t> data;
	if (f != INVALID_HANDLE_VALUE) {
		LARGE_INTEGER size{};
		GetFileSizeEx(f, &size);
		data.resize((size_t)size.QuadPart);
		DWORD got = 0;
		ReadFile(f, data.data(), (DWORD)data.size(), &got, nullptr);
		CloseHandle(f);
	}
	worldsolids::Catalog c;
	if (!c.Parse(data.data(), data.size())) {
		std::printf("  FAIL: solids.bin does not read back\n");
		++fails;
	} else {
		size_t parts = 0, most = 0;
		for (size_t i = 0; i < c.Models(); ++i) {
			parts += c.ModelAt((uint32_t)i)->parts.size();
			most = std::max(most, c.ModelAt((uint32_t)i)->parts.size());
		}
		std::printf("read back: %zu pieces, %zu models, %zu parts (at most %zu in one model)\n", c.Rows(), c.Models(), parts, most);
	}
	for (int i = 3; i < argc; ++i) {
		const uint32_t id = (uint32_t)wcstoul(argv[i], nullptr, 10);
		std::printf("entity %u: %s\n", id, c.Knows(id) ? "known" : "NOT known");
	}
	std::printf(fails ? "solids_assets_test: %d FAILED\n" : "solids_assets_test: all passed\n", fails);
	return fails ? 1 : 0;
}
