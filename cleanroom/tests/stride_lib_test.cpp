// Offline checks of vr/stride_lib.cpp ([body] legs_side_ragdoll): the weight library's gait (D2rsGait in d2r_vr_weight.dll, interface 2) found, refused when it
// speaks another interface version or is an older weight library without it, absent without harm, a call through it - against stand-ins
// (cleanroom/tests/stride_lib_standin.cpp; the older library: the gait stand-in of d2r_vr_gait.dll, which has none of the D2rs* exports). Run: stride_lib_test.exe (exit 0 = all passed).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "stride_lib.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static std::string g_said;
static void Log(const char* t) { g_said += t; g_said += "\n"; }

static std::wstring W(const char* s) { return std::wstring(s, s + std::strlen(s)); }

int main() {
    stridelib::SetLog(&Log);
    D2rsGaitIn in{};
    in.size = sizeof in;
    in.speed = 3.0f;
    in.rest[D2RS_PELVIS][1] = 0.95f;
    D2rsGaitState st{};
    D2rsGaitOut out{};
    out.size = sizeof out;

    // none loaded: a call does nothing
    CHECK(!stridelib::Loaded());
    CHECK(!stridelib::Gait(in, &st, &out));

    // missing: told, not loaded
    stridelib::SetPath(L"C:\\no\\such\\dir\\d2r_vr_weight.dll");
    CHECK(!stridelib::Ensure());
    CHECK(stridelib::GetStatus() == stridelib::Status::Missing);
    CHECK(g_said.find("not there") != std::string::npos);
    CHECK(!stridelib::Ensure());   // (asked again at once: not looked for again within 2 s)

    // an older weight library (no D2rs*): refused, said so
    stridelib::SetPath(W(STRIDE_STANDIN_OLD).c_str());
    CHECK(!stridelib::Ensure());
    CHECK(stridelib::GetStatus() == stridelib::Status::BrokenExports);
    CHECK(g_said.find("older weight library") != std::string::npos);

    // another interface version: refused
    stridelib::SetPath(W(STRIDE_STANDIN_BAD).c_str());
    CHECK(!stridelib::Ensure());
    CHECK(stridelib::GetStatus() == stridelib::Status::BadAbi);
    CHECK(!stridelib::Loaded());

    // the right one: loaded, named, called
    stridelib::SetPath(W(STRIDE_STANDIN_GOOD).c_str());
    CHECK(stridelib::Ensure());
    CHECK(stridelib::Loaded() && stridelib::GetStatus() == stridelib::Status::Loaded);
    CHECK(std::string(stridelib::Name()) == "gait stand-in");
    CHECK(g_said.find("loaded") != std::string::npos);
    CHECK(stridelib::Gait(in, &st, &out));
    CHECK(st.magic == 1 && std::fabs(out.joint[D2RS_PELVIS][1] - 0.98f) < 1e-5f && out.stepM == 1.0f);
    D2rsGaitOut brief{};
    brief.size = 4;
    CHECK(!stridelib::Gait(in, &st, &brief));   // the library's own refusal comes through
    CHECK(stridelib::Ensure());                 // (loaded: stays)

    std::printf("stride_lib_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
