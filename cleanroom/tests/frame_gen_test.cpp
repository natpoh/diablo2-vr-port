// [openxr] frame_generation: the pure parts (shared/frame_gen.h). Checks: the ini's numbers read (out of range: the defaults); what the layer is asked for (off,
// by turns: stand aside; NVIDIA's options); the control ABI's sizes; the manifest names the layer and its DLL by a relative path; the default ini has the log and
// the overlay off; the state lines for every start and snapshot.

#include <cstdio>
#include <cstring>
#include <string>

#include "frame_gen.h"

namespace {

int g_fails = 0;
#define EXPECT(c, ...)                                         \
    do {                                                       \
        if (!(c)) {                                            \
            ++g_fails;                                         \
            std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
            std::printf(__VA_ARGS__);                          \
            std::printf("\n");                                 \
        }                                                      \
    } while (0)

using namespace framegen;

void TestRead() {
    EXPECT(!Read(0, 1, 2).On(), "0 is off");
    EXPECT(Read(1, 1, 2).mode == Mode::FidelityFx && Read(2, 0, 1).mode == Mode::Nvidia, "1 FidelityFX, 2 NVIDIA");
    const Options bad = Read(7, -1, 9);
    EXPECT(bad.mode == Mode::Off && bad.preset == 1 && bad.scale == 2, "out of range: off, Medium, 50%%");
}

void TestControl() {
    const ControlSettingsV1 off = Control(Read(0, 1, 2), true);
    EXPECT(off.enabled == 0, "off: stand aside");
    const ControlSettingsV1 turns = Control(Read(1, 1, 2), false);
    EXPECT(turns.enabled == 0, "by turns: stand aside");
    const ControlSettingsV1 ffx = Control(Read(1, 1, 2), true);
    EXPECT(ffx.enabled == 1 && ffx.backend == 0, "FidelityFX runs");
    const ControlSettingsV1 nv = Control(Read(2, 0, 1), true);
    EXPECT(nv.enabled == 1 && nv.backend == 1 && nv.preset == 0 && nv.scale == 1, "NVIDIA Slow 75%%");
    EXPECT(sizeof(ControlSettingsV1) == 28 && sizeof(ControlSnapshotV1) == 72, "the V1 ABI");
    EXPECT(ffx.structSize == 28, "its size in it");
    EXPECT(Describe(1, 1, 2) == "NVIDIA Medium, 50% scale" && Describe(0, 0, 0) == "FidelityFX", "the names");
}

void TestFiles() {
    const std::string m = Manifest();
    EXPECT(m.find("\"name\": \"XR_APILAYER_XRFrameBridge_diagnostic\"") != std::string::npos, "the manifest names the layer");
    EXPECT(m.find("\"library_path\": \"./XR_APILAYER_XRFrameBridge_diagnostic.dll\"") != std::string::npos, "its DLL by a relative path");
    EXPECT(m.find("\"implementation_version\": \"116\"") != std::string::npos, "its build");
    const std::string ini = DefaultIni();
    EXPECT(ini.find("logging_enabled=0") != std::string::npos && ini.find("position=off") != std::string::npos, "its log and overlay off");
    EXPECT(std::strlen(kSha256) == 64, "a SHA-256");
}

void TestStates() {
    const Options on = Read(1, 1, 2), off = Read(0, 1, 2);
    EXPECT(Classify(Start::NotAsked, false, off, nullptr, true).state == State::Off, "off");
    EXPECT(Classify(Start::Missing, false, on, nullptr, true).state == State::NotInstalled, "on, not installed");
    EXPECT(Classify(Start::NotAsked, true, on, nullptr, true).state == State::RestartToStart, "switched on while running: restart");
    EXPECT(Classify(Start::Failed, true, on, nullptr, true).state == State::Failed, "failed");
    EXPECT(Classify(Start::NotAsked, true, on, nullptr, false).state == State::NeedsBothEyes, "by turns: needs both eyes");
    EXPECT(Classify(Start::Loaded, true, off, nullptr, true).state == State::Paused, "loaded, switched off: paused till the restart");
    ControlSnapshotV1 s{};
    EXPECT(Classify(Start::Loaded, true, on, &s, true).state == State::Starting, "no session yet");
    s.sessions = 1;
    s.pending = 1;
    EXPECT(Classify(Start::Loaded, true, on, &s, true).state == State::Applying, "applying");
    s.pending = 0;
    s.ready = 1;
    EXPECT(Classify(Start::Loaded, true, on, &s, true).state == State::Active, "active");
    s.ready = 0;
    s.bypass = 1;
    EXPECT(Classify(Start::Loaded, true, on, &s, true).state == State::Bypassed, "bypassed");
    s.errors = 1;
    s.lastError = -2;
    const Readout e = Classify(Start::Loaded, true, on, &s, true);
    EXPECT(e.state == State::Error && Line(e).find("0xFFFFFFFE") != std::string::npos, "an error with its code (%s)", Line(e).c_str());
    EXPECT(Classify(Start::NoControl, true, on, nullptr, true).state == State::NoControl, "no control API");
    for (int i = 0; i <= static_cast<int>(State::NeedsBothEyes); ++i) {
        Readout r;
        r.state = static_cast<State>(i);
        EXPECT(!Line(r).empty() && Line(r).size() < 160, "a line for state %d", i);
    }
}

// After bearstonem v0.3.27: a new session when switched on, once after a failed live request; Virtual Desktop's own halving.
void TestNewSessionAndHalving() {
    const Options on = Read(1, 1, 2);
    EXPECT(LoadsWithNewSession(Classify(Start::NotAsked, true, on, nullptr, true)), "switched on while running, installed: a new session loads it");
    EXPECT(!LoadsWithNewSession(Classify(Start::NotAsked, true, on, nullptr, false)), "not by turns: it would only wait");
    EXPECT(!LoadsWithNewSession(Classify(Start::Failed, true, on, nullptr, true)), "not after it failed");
    EXPECT(!LoadsWithNewSession(Classify(Start::Missing, false, on, nullptr, true)), "not while it is not installed");
    ControlSnapshotV1 s{};
    s.sessions = 1;
    s.errors = 1;
    s.lastError = static_cast<int32_t>(0x80070057);
    EXPECT(RetriesWithNewSession(Classify(Start::Loaded, true, on, &s, true)), "a failed request: a new session");
    s.errors = 0;
    s.ready = 1;
    EXPECT(!RetriesWithNewSession(Classify(Start::Loaded, true, on, &s, true)), "running: none");
    HalvingWatch w;
    for (int i = 0; i < 5; ++i) w.Note(90.0f);
    EXPECT(!w.halved, "the headset's rate");
    w.Note(45.0f);
    w.Note(45.0f);
    EXPECT(!w.halved, "two seconds halved: not yet");
    w.Note(45.0f);
    EXPECT(w.halved, "three seconds halved: SSW");
    w.Note(90.0f); w.Note(90.0f); w.Note(90.0f);
    EXPECT(!w.halved, "three seconds back: cleared");
    w.Note(0.0f);
    EXPECT(!w.halved, "an unknown rate changes nothing");
    Readout a = Classify(Start::Loaded, true, on, &s, true);
    a.runtimeHalves = true;
    EXPECT(a.state == State::Active && Line(a).find("SSW") != std::string::npos, "the line says SSW (%s)", Line(a).c_str());
}

}  // namespace

int main() {
    TestNewSessionAndHalving();
    TestRead();
    TestControl();
    TestFiles();
    TestStates();
    if (g_fails) {
        std::printf("frame_gen_test: %d check(s) failed\n", g_fails);
        return 1;
    }
    std::printf("frame_gen_test: all checks passed\n");
    return 0;
}
