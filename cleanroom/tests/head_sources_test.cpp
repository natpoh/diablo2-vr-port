// Every head the views are built from passes link::KeepRealHead (vr/link.h, vr/head_hold.h) - checked on the source itself, so a new pose
// source cannot bypass the guard unseen. The spin "inside FlatVR the camera spins when there is no headset data" was fixed three times
// (2026-10-10 on BodyWalk's side, 2026-11-06 for NaN in link::ToShared, 2026-11-22 for blank / gone heads) - each time on one path or
// for one kind of bad head, while the others stayed open. This test reads vr/ as it is built:
//
//   head_sources_test      exit code 0 = every head source is guarded
//
// writers    every file that writes a head into a block (headYawDeg = ..., headValid = 1) is a known one - a new one fails here until it
//            is run through link::KeepRealHead and registered below
// bodywalk   link.cpp's ToShared ends in KeepRealHeadImpl(.., BodyWalk)
// direct     direct input runs its head guard on every block right after SharedFromXr, link::Init sets that guard to KeepRealHead(..,
//            DirectInput), and link::Head() hands out that very block; nobody else reads directinput::HeadShared()
// native     frame_order.cpp's XrIntoShared runs KeepRealHead(.., NativePair) on both its ways out; every pair head stored is link::Head()'s
//            copy or XrIntoShared's
// shared     Shared() (what every view reads) is the pair's block or link::Head(), nothing else

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#ifndef D2RVR_SRC
#error "D2RVR_SRC (the source tree) is set by CMakeLists.txt"
#endif

namespace fs = std::filesystem;

namespace {

int g_failed = 0, g_checks = 0;
void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) { ++g_failed; std::printf("FAIL: %s\n", what.c_str()); }
}

std::string Read(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    std::string t = s.str();
    std::string out;
    out.reserve(t.size());
    for (char c : t) if (c != '\r') out += c;
    return out;
}
std::string Rel(const fs::path& p, const fs::path& root) { return fs::relative(p, root).generic_string(); }

// The body of the function whose definition starts with `head` (to the first line that is a lone "}").
std::string Body(const std::string& text, const std::string& head) {
    const size_t a = text.find(head);
    if (a == std::string::npos) return {};
    const size_t b = text.find("\n}\n", a);
    return text.substr(a, b == std::string::npos ? std::string::npos : b - a);
}
size_t Count(const std::string& text, const std::string& what) {
    size_t n = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) ++n;
    return n;
}

}  // namespace

int main() {
    const fs::path root = fs::path(D2RVR_SRC);
    const fs::path vr = root / "vr";
    if (!fs::exists(vr)) { std::printf("FAIL: no source tree at %s\n", vr.generic_string().c_str()); return 1; }

    // writers: a head written into a block. Registered: the three sources, and readers that copy a head into a structure of their own.
    const std::map<std::string, const char*> known = {
        {"vr/link.cpp", "BodyWalk's Game Link (ToShared) and KeepRealHead itself"},
        {"vr/direct_input.cpp", "the plugin's direct input (SharedFromXr) - guarded in its own block (SetHeadGuard, set by link::Init)"},
        {"vr/frame/frame_order.cpp", "the native pair (XrIntoShared)"},
        {"vr/body/arms.cpp", "a reader: Shared()'s head into the skeleton's own input"},
    };
    const std::regex write(R"((->|\.)headYawDeg\s*=[^=]|headValid\s*=\s*1\b)");
    std::set<std::string> found;
    for (const auto& e : fs::recursive_directory_iterator(vr)) {
        if (!e.is_regular_file()) continue;
        const std::string ext = e.path().extension().string();
        if (ext != ".cpp" && ext != ".h") continue;
        const std::string rel = "vr/" + Rel(e.path(), vr);
        if (rel == "vr/head_hold.h") continue;   // (the guard's own struct)
        if (std::regex_search(Read(e.path()), write)) found.insert(rel);
    }
    for (const std::string& f : found)
        Check(known.count(f) != 0, "writers: " + f + " writes a head - a new head source: run it through link::KeepRealHead and register it here");
    for (const auto& k : known) Check(found.count(k.first) != 0, std::string("writers: ") + k.first + " registered but writes no head now - update the list");

    // bodywalk
    const std::string link = Read(vr / "link.cpp");
    Check(Body(link, "bool ToShared(").find("return KeepRealHeadImpl(o, (int)HeadSource::BodyWalk)") != std::string::npos,
          "bodywalk: link.cpp ToShared does not end in KeepRealHeadImpl(.., BodyWalk)");
    Check(Body(link, "void KeepRealHead(").size() || link.find("void KeepRealHead(D2RVR_Shared* o, HeadSource src)") != std::string::npos,
          "bodywalk: link::KeepRealHead is not defined in link.cpp");

    // direct
    const std::string head = Body(link, "const D2RVR_Shared* Head() {");
    Check(!head.empty(), "direct: link::Head() not found");
    Check(head.find("return directinput::HeadShared();") != std::string::npos, "direct: link::Head() does not hand out direct input's block");
    Check(Body(link, "void Init(").find("directinput::SetHeadGuard(") != std::string::npos &&
              Body(link, "void Init(").find("HeadSource::DirectInput") != std::string::npos,
          "direct: link::Init does not set direct input's head guard to KeepRealHead(.., DirectInput)");
    {
        const std::string din = Read(vr / "direct_input.cpp");
        const size_t made = din.find("SharedFromXr(*in, s.sharedCounter, sh);");
        const size_t guarded = din.find("guard(sh);", made == std::string::npos ? 0 : made);
        const size_t out = din.find("g_head.store(sh);", made == std::string::npos ? 0 : made);
        Check(made != std::string::npos && guarded != std::string::npos && out != std::string::npos && guarded < out,
              "direct: direct_input.cpp hands out its head (g_head.store) without the head guard after SharedFromXr");
    }
    for (const auto& e : fs::recursive_directory_iterator(vr)) {
        if (!e.is_regular_file() || e.path().extension() != ".cpp") continue;
        const std::string rel = "vr/" + Rel(e.path(), vr);
        if (rel == "vr/link.cpp" || rel == "vr/direct_input.cpp") continue;
        Check(Read(e.path()).find("HeadShared()") == std::string::npos, "direct: " + rel + " reads directinput::HeadShared() raw - use link::Head()");
    }

    // native
    const std::string order = Read(vr / "frame" / "frame_order.cpp");
    const std::string xr = Body(order, "bool XrIntoShared(");
    Check(!xr.empty(), "native: XrIntoShared not found");
    Check(Count(xr, "link::KeepRealHead(out, link::HeadSource::NativePair)") >= 2, "native: XrIntoShared does not guard both its ways out");
    {
        std::istringstream lines(order);
        std::string l;
        int stores = 0;
        while (std::getline(lines, l)) {
            const size_t at = l.find("g_pairShared.store(&");
            if (at == std::string::npos) continue;
            ++stores;
            const bool fromHead = l.find("memcpy(&held, live") != std::string::npos;   // live = link::Head()
            const bool fromXr = l.find("XrIntoShared(") != std::string::npos;
            Check(fromHead || fromXr, "native: a pair head stored that is neither link::Head()'s copy nor XrIntoShared's: " + l);
        }
        Check(stores >= 2, "native: no pair head stores found (the check reads nothing)");
        Check(Count(order, "const D2RVR_Shared* live = link::Head();") >= 2, "native: the pair's live head is not link::Head()");
    }

    // shared
    const std::string view = Read(vr / "view" / "head.cpp");
    Check(Body(view, "const D2RVR_Shared* Shared() {").find("return p ? p : link::Head();") != std::string::npos,
          "shared: Shared() reads something other than the pair's block or link::Head()");

    std::printf("head_sources_test: %d of %d checks passed\n", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
