// [cinema] video_3d: the 3D video library's loader (vr/video3d_lib.h) against stand-in DLLs (video3d_lib_standin.cpp): no file, a library of another interface
// version, the right one - and its calls passed through (Start once per device, Record before and after Submitted, Reset); with no library every call does nothing.

#include "video3d_lib.h"

#include <cstdio>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#ifndef STANDIN_GOOD
#error "STANDIN_GOOD / STANDIN_BAD: the stand-in DLLs' paths (CMake)"
#endif

namespace {

int g_fails = 0, g_checks = 0;
std::string g_log;

#define EXPECT(c, what)                                                      \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(c)) { ++g_fails; std::printf("  FAIL line %d: %s - %s\n", __LINE__, #c, what); } \
    } while (0)

void Log(const char* s) { g_log += s; g_log += "\n"; }

std::wstring Wide(const char* s) {
    std::wstring w;
    for (; *s; ++s) w += static_cast<wchar_t>(*s);
    return w;
}

}  // namespace

int main() {
    video3dlib::SetLog(&Log);
    int dev = 0, queue = 0, list = 0;
    D2rv3Frame f{};
    f.size = sizeof f;

    // No file: missing, told once, every call a no-op.
    video3dlib::SetPath(L"C:\\no\\such\\d2r_vr_video3d.dll");
    EXPECT(!video3dlib::Ensure(), "a missing file is no library");
    EXPECT(video3dlib::GetStatus() == video3dlib::Status::Missing, "status Missing");
    EXPECT(g_log.find("no d2r_vr_video3d.dll") != std::string::npos, "the log says it is missing");
    EXPECT(!video3dlib::Start(&dev, &queue), "no start without a library");
    EXPECT(video3dlib::Record(&list, &f) == -1, "no record without a library");
    video3dlib::Submitted();
    video3dlib::Reset();
    video3dlib::Stop();

    // Another interface version: not used.
    Sleep(2100);   // (Ensure looks again at most every 2 s)
    video3dlib::SetPath(Wide(STANDIN_BAD).c_str());
    EXPECT(!video3dlib::Ensure(), "another interface version is not used");
    EXPECT(video3dlib::GetStatus() == video3dlib::Status::BadAbi, "status BadAbi");

    // The right one.
    Sleep(2100);
    video3dlib::SetPath(Wide(STANDIN_GOOD).c_str());
    EXPECT(video3dlib::Ensure(), "the right library loads");
    EXPECT(video3dlib::GetStatus() == video3dlib::Status::Loaded, "status Loaded");
    EXPECT(std::string(video3dlib::Name()) == "video3d stand-in", "its name");
    EXPECT(video3dlib::Record(&list, &f) == -1, "no record before Start");
    EXPECT(!video3dlib::Start(nullptr, &queue), "no start without a device");
    EXPECT(video3dlib::Start(&dev, &queue), "starts on a device");
    EXPECT(g_log.find("stand-in started") != std::string::npos, "the library's own log reaches the mod's");
    EXPECT(video3dlib::Start(&dev, &queue), "the same device: still started");
    EXPECT(video3dlib::Record(&list, &f) == 0, "flat before any depth");
    video3dlib::Submitted();
    EXPECT(video3dlib::Record(&list, &f) == 1, "3D once a list went");
    video3dlib::Reset();
    EXPECT(video3dlib::Record(&list, &f) == 0, "a new film: flat again");
    D2rv3Frame tiny{};
    tiny.size = 4;
    EXPECT(video3dlib::Record(&list, &tiny) == -1, "a frame struct too small is refused");
    video3dlib::Stop();
    EXPECT(video3dlib::Record(&list, &f) == -1, "nothing after Stop");
    EXPECT(video3dlib::Start(&dev, &queue), "started again after Stop");

    std::printf("video3d_lib_test: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
