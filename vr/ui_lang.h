#pragma once
// ui_lang.h - [hud] headset_language: the words the plugin draws in the headset (vr/ui_text.h) in the language D2R VR Settings is in -
// [settings] language of d2r_vr_settings.ini beside d2r_vr.ini ("auto" or nothing: Windows' language when there is a file for it), its
// file lang\<code>.txt from the mod's folder (Setup puts them in mods\D2R-VR\lang). Off (the default), "en", no file, a key the file lacks:
// English, as before. The font: Georgia and Segoe UI stay unless the language has letters they lack (Chinese: Microsoft YaHei).

#include <string>

namespace uilang {

// Read again when what it depends on changed (the key, the language picked, the file); `ini` is d2r_vr.ini's path. Returns a line for the
// log when something changed, else "".
std::string Load(const wchar_t* ini);

}  // namespace uilang
