// Offline checks of vr/gait_lib.cpp ([multiplayer] pose_share_legs): the optional gait library d2r_vr_gait.dll found, refused when it speaks another interface
// version, absent without harm, a step through it - against stand-ins (cleanroom/tests/gait_lib_standin.cpp). Run: gait_lib_test.exe (exit 0 = all passed).
#include <cstdio>
#include <cstring>
#include <string>

#include "gait_lib.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static std::string g_said;
static void Log(const char* t) { g_said += t; g_said += "\n"; }

static std::wstring W(const char* s) { return std::wstring(s, s + std::strlen(s)); }

int main() {
    gaitlib::SetLog(&Log);
    D2rfLegsIn in{};
    in.size = sizeof in;
    in.unitsPerM = 10.0f;
    in.velocity = {10.0f, 0.0f, 0.0f};
    in.rest[0] = {-1.0f, 0.8f, 0.0f};
    in.rest[1] = {1.0f, 0.8f, 0.0f};
    D2rfState st{};
    D2rfLegsOut out{};
    out.size = sizeof out;

    // none loaded: a step does nothing
    CHECK(!gaitlib::Loaded());
    CHECK(!gaitlib::Step(in, &st, &out));

    // missing: told, not loaded
    gaitlib::SetPath(L"C:\\no\\such\\dir\\d2r_vr_gait.dll");
    CHECK(!gaitlib::Ensure());
    CHECK(gaitlib::GetStatus() == gaitlib::Status::Missing);
    CHECK(g_said.find("not there") != std::string::npos);
    CHECK(!gaitlib::Ensure());   // (asked again at once: not looked for again within 2 s)

    // another interface version: refused
    gaitlib::SetPath(W(GAIT_STANDIN_BAD).c_str());
    CHECK(!gaitlib::Ensure());
    CHECK(gaitlib::GetStatus() == gaitlib::Status::BadAbi);
    CHECK(!gaitlib::Loaded());

    // the right one: loaded, named, stepped
    gaitlib::SetPath(W(GAIT_STANDIN_GOOD).c_str());
    CHECK(gaitlib::Ensure());
    CHECK(gaitlib::Loaded() && gaitlib::GetStatus() == gaitlib::Status::Loaded);
    CHECK(std::string(gaitlib::Name()) == "gait stand-in");
    CHECK(g_said.find("loaded") != std::string::npos);
    CHECK(gaitlib::Step(in, &st, &out));
    CHECK(st.phase == 1.0f && out.drop == 1.0f && out.foot[0].x == 0.0f && out.foot[1].x == 2.0f);   // the rest moved by a tenth of a second of the travel
    D2rfLegsOut small{};
    small.size = 4;
    CHECK(!gaitlib::Step(in, &st, &small));   // the library's own refusal comes through
    CHECK(gaitlib::Ensure());                 // (loaded: stays)

    std::printf("gait_lib_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
