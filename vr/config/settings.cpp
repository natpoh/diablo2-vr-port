// vr/config/settings.cpp - the settings' one copy, the ini's path and its helpers (cut from vr/vrcam.cpp, docs/plan_modules.md M2)
#include "settings.h"
#include "../vrcam_internal.h"

#include <cmath>
#include <cwchar>

namespace vrc {

Settings g_set;

wchar_t g_iniPath[MAX_PATH];

float IniF(const wchar_t* sec, const wchar_t* key, float def) {
    wchar_t buf[64], d[64];
    swprintf_s(d, L"%g", def);
    GetPrivateProfileStringW(sec, key, d, buf, 64, g_iniPath);
    wchar_t* end = nullptr;
    const float v = wcstof(buf, &end);
    return end != buf && std::isfinite(v) ? v : def;
}
bool IniB(const wchar_t* sec, const wchar_t* key, bool def) { return IniF(sec, key, def ? 1.0f : 0.0f) != 0.0f; }

}  // namespace vrc
