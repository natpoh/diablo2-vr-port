#pragma once
// ui_text_tables.h - the headset's words that live in tables (the settings panel's rows and tabs, the first-start guide, the key bindings' picks, the
// toolbar types), each with the key its table makes for it (vr/ui_text.h). D2R_VR_Settings.exe --lang-template writes them into lang\en.txt's
// "In the headset" part (tools/lang_sync.py en); the words the code writes (uitext::Tr calls with a literal key) tools/lang_sync.py finds itself.
// Pure. Tested: cleanroom/tests/ui_text_test.cpp (every key here is in lang\en.txt with this English, no key twice with two Englishes).

#include <map>
#include <string>

#include "first_start_pages.h"
#include "hset_defs.h"
#include "hset_keys.h"
#include "hset_names.h"
#include "ui_text.h"

namespace uitext {

// fn(key, English) once a key, in the tables' order. A key two places make with the same English (a switch's "On") is given once.
template <class Fn>
void ForEachTableText(Fn fn) {
    std::map<std::string, std::string> seen;
    auto give = [&](const std::string& key, const char* en) {
        if (!en || !*en) return;
        const auto at = seen.find(key);
        if (at != seen.end() && at->second == en) return;
        seen[key] = en;
        fn(key, en);
    };
    // the settings in the headset (vr/hset_defs.h): the tabs, then each row's name, the line under it and a switch's two faces
    for (int p = 0; p < hset::kPageCount; ++p) give(hset::PageKey(p), hset::kPageNames[p]);
    for (int i = 0; i < hset::kCount; ++i) {
        const hset::Def& d = hset::kDefs[i];
        give(hset::DefKey(i, "label"), d.label);
        if (d.type != hset::DefType::Choice) give(hset::DefKey(i, "detail"), d.detail);   // (a Choice row's line is its ini text)
        if (*d.off) give(hset::ValueKey(d.off), d.off);
        if (*d.on) give(hset::ValueKey(d.on), d.on);
    }
    for (int t = 0; t < toolbartype::kCount; ++t) give(hset::ToolbarKey(t), toolbartype::kShort[t]);
    for (const hset::keys::Short& s : hset::keys::kShort) give(hset::keys::ShortKey(s.ini), s.name);
    // the first-start guide (vr/first_start_pages.h)
    for (int p = 0; p < firststart::kPageCount; ++p) {
        give(firststart::GuideKey(p, "title"), firststart::kPages[p].title);
        for (int r = 0; r < firststart::kRowsMax; ++r) {
            give(firststart::RowKey(p, r, "control"), firststart::kPages[p].rows[r].control);
            give(firststart::RowKey(p, r, "does"), firststart::kPages[p].rows[r].does);
        }
        give(firststart::NavKey(p), firststart::kPages[p].nav);
    }
}

}  // namespace uitext
