// [weight] monster_ragdoll: the doll library's loader (vr/doll_lib.h) against stand-in DLLs (doll_lib_standin.cpp): no file, a DLL that is not one, a library of
// another interface version, the right one - and its calls passed through (a push of a humanoid's body, none of another's, the bodies moved, the mass, the
// balance); with no library every call does nothing.

#include "doll_lib.h"

#include <cstdio>
#include <cstring>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#ifndef STANDIN_GOOD
#error "STANDIN_GOOD / STANDIN_BAD: the stand-in DLLs' paths (CMake)"
#endif

namespace {

int g_fails = 0;
std::string g_log;

#define EXPECT(c, ...)                                         \
	do {                                                       \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
			std::printf(__VA_ARGS__);                          \
			std::printf("\n");                                 \
		}                                                      \
	} while (0)

void Log(const char* s) { g_log += s; g_log += "\n"; }

std::wstring Wide(const char* s) {
	std::wstring w;
	for (; *s; ++s) w += static_cast<wchar_t>(*s);
	return w;
}

void Bodies(D2rdBody* b) {
	for (int i = 0; i < 3; ++i) {
		b[i] = D2rdBody{};
		b[i].id = i < 2 ? 5u : 6u;
		b[i].bone = i;
		b[i].rig = 1;
		b[i].doll = i < 2 ? 1 : 0;
		b[i].radius = 0.1f;
		b[i].b[1] = 1.0f;
	}
}

}  // namespace

int main() {
	dolllib::SetLog(&Log);
	D2rdState st{};
	D2rdParams p{};
	p.size = sizeof p;
	D2rdBody b[3];
	Bodies(b);
	const float imp[3] = {0.0f, 0.0f, -2.0f}, at[3] = {0.0f, 1.0f, 0.0f};

	dolllib::SetPath(L"C:\\no\\such\\folder\\d2r_vr_weight.dll");
	EXPECT(!dolllib::Probe() && !dolllib::Ensure() && dolllib::GetStatus() == dolllib::Status::Missing, "no file: missing");
	EXPECT(g_log.find("there is no d2r_vr_weight.dll") != std::string::npos, "the log says so");
	EXPECT(!dolllib::Push(&st, &p, b, 3, 0, imp, at) && dolllib::Apply(&st, &p, b, 3) == 0 && dolllib::Mass(&p, b, 3, 5) == 0.0f, "no library: nothing done");

	wchar_t sys[MAX_PATH];
	GetSystemDirectoryW(sys, MAX_PATH);
	dolllib::SetPath((std::wstring(sys) + L"\\version.dll").c_str());
	EXPECT(!dolllib::Ensure() && dolllib::GetStatus() == dolllib::Status::BrokenExports, "a DLL without the exports (a weight library older than 1.2): not used");
	EXPECT(g_log.find("no monsters' weight") != std::string::npos, "the log says so");
	dolllib::SetPath(Wide(STANDIN_BAD).c_str());
	EXPECT(!dolllib::Ensure() && dolllib::GetStatus() == dolllib::Status::BadAbi, "another interface version: not used");
	EXPECT(g_log.find("interface version") != std::string::npos, "the log says why");

	dolllib::SetPath(Wide(STANDIN_GOOD).c_str());
	EXPECT(dolllib::Probe(), "the file is there");
	EXPECT(dolllib::Ensure() && dolllib::Loaded(), "loaded");
	EXPECT(std::strcmp(dolllib::Name(), "doll stand-in") == 0, "its name: %s", dolllib::Name());
	EXPECT(!dolllib::Push(&st, &p, b, 3, 2, imp, at), "not a humanoid's body: not pushed");
	EXPECT(dolllib::Push(&st, &p, b, 3, 0, imp, at) && st.nMonsters == 1 && st.monsters[0].id == 5u && st.monsters[0].s.vel[2] == -2.0f, "a humanoid's: pushed");
	EXPECT(dolllib::Step(&st, &p, 0.01f), "a step");
	EXPECT(dolllib::Apply(&st, &p, b, 3) == 2 && b[0].a[0] > 0.0f && b[2].a[0] == 0.0f, "its bodies moved, the other monster's not");
	EXPECT(dolllib::Mass(&p, b, 3, 5) == 20.0f, "the mass: %.1f", dolllib::Mass(&p, b, 3, 5));
	EXPECT(dolllib::Balance(&st, &p, 5, nullptr) == 0.5f && dolllib::Balance(&st, &p, 6, nullptr) == 0.0f, "the balance");

	std::printf("doll_lib_test: %s (%d failed)\n", g_fails ? "FAIL" : "ok", g_fails);
	return g_fails ? 1 : 0;
}
