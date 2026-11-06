// game_version.h - the game data's version as D2RLoader read it, and whether D2R VR runs on it. The plugin (vr/vrcam.cpp CheckGameVersion: a loud log
// line) and Settings (Home > The game and the mod: a Warning row) read D2RLoader's own log, <game>\d2rloader\logs\d2rloader.log, whose start says
// "[BUILD] D2R version: 3.3.94995." (from the game's build configuration - the data, not the exe). Pure, tested in tools/game_version_test.cpp.
//
// Group K of the 2026-11-06 crash batch (rows 272 and 275, two PCs, 0.205.0): D2RLoader said "D2R version: 2.7.4" (.build.info 1.6.81914) and warned it is
// below [3.0, 3.4), but the exe is a 3.3 one (our 230 of 231 code addresses found at 3.3's places): the game itself then looks up a 3.3 class's file
// (data/hd/character/player/warlock.json) in 2.7's data, RapidJSON asserts IsObject() three times, and D2RLoader+0xF9F680 reads the null it got - dead before
// the title screen. That read is the game's own code: the plugin cannot take it away. What helps is saying it loudly - a broken install (the data older than
// the game), the fix is the launcher's Scan and Repair.
#pragma once

#include <cstddef>
#include <cstring>

namespace gamever {

struct Version {
    int major = -1, minor = -1, patch = -1;
    bool known() const { return major >= 0; }
};

// "... [BUILD] D2R version: 2.7.4." -> 2.7.4. False when the line has no version.
inline bool ParseLine(const char* line, Version* out) {
    if (!line || !out) return false;
    const char* key = "D2R version:";
    const char* p = std::strstr(line, key);
    if (!p) return false;
    p += std::strlen(key);
    while (*p == ' ' || *p == '\t') ++p;
    int parts[3] = {-1, -1, -1};
    for (int i = 0; i < 3; ++i) {
        if (*p < '0' || *p > '9') break;
        int v = 0;
        while (*p >= '0' && *p <= '9') {
            if (v < 100000000) v = v * 10 + (*p - '0');
            ++p;
        }
        parts[i] = v;
        if (*p != '.') break;
        ++p;
    }
    if (parts[0] < 0 || parts[1] < 0) return false;
    out->major = parts[0];
    out->minor = parts[1];
    out->patch = parts[2];
    return true;
}

// The first version line in a block of the log (its start: the version comes in its first lines). n bytes, not 0-terminated.
inline Version FindInLog(const char* text, size_t n) {
    Version v;
    size_t i = 0;
    char line[512];
    while (i < n) {
        size_t len = 0;
        while (i < n && text[i] != '\n') {
            if (len + 1 < sizeof line) line[len++] = text[i];
            ++i;
        }
        ++i;
        line[len] = 0;
        if (ParseLine(line, &v)) return v;
    }
    return Version{};
}

// D2R VR runs on the game's 3.x (D2RLoader's own range is [3.0, 3.4)). Older data: the install is broken or old - said loudly.
enum class Support { Unknown, Supported, TooOld, Newer };
inline Support Judge(const Version& v) {
    if (!v.known()) return Support::Unknown;
    if (v.major < 3) return Support::TooOld;
    if (v.major > 3) return Support::Newer;
    return Support::Supported;
}

}  // namespace gamever
