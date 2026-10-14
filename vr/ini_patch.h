// ini_patch.h - set keys in the text of an ini file without touching anything else in it: comments, order, spacing and the
// line endings stay as they are. Pure (no file, no Windows call), so cleanroom/tests/body_calibration_test.cpp can run it.
//
// Why not WritePrivateProfileStringW: it rewrites the file in place, and the settings program and the plugin both write
// d2r_vr.ini - the measurement writes three keys at once through a copy of the file and a replace (vr/calibration.cpp),
// so a reader never sees half of them and a crash never leaves half a file.
#pragma once

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

namespace inipatch {

namespace detail {
inline bool IEq(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}
inline std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}
}  // namespace detail

// key=value in [section] (names compared without case, as GetPrivateProfileString does): the first such key has its value
// replaced (an inline "; comment" after it is kept); none, and it is added at the end of the section's keys; no such
// section, and it is added at the end of the file. False for text with NUL bytes (a UTF-16 file): left alone.
inline bool Set(std::string& text, std::string_view section, std::string_view key, std::string_view value) {
    if (text.find('\0') != std::string::npos) return false;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    bool inSection = false, sectionFound = false;
    size_t insertAt = std::string::npos;   // just after the section's header or its last key line
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t nl = text.find('\n', pos);
        const size_t next = nl == std::string::npos ? text.size() : nl + 1;
        const std::string_view line(text.data() + pos, next - pos);
        const std::string_view body = detail::Trim(line);
        if (!body.empty() && body.front() == '[') {
            if (inSection) break;   // the section is over
            const size_t close = body.find(']');
            if (close != std::string_view::npos && detail::IEq(detail::Trim(body.substr(1, close - 1)), section)) {
                inSection = sectionFound = true;
                insertAt = next;
            }
        } else if (inSection && !body.empty() && body.front() != ';' && body.front() != '#') {
            const size_t eq = line.find('=');
            if (eq != std::string_view::npos) {
                if (detail::IEq(detail::Trim(line.substr(0, eq)), key)) {
                    // the value runs to a ';' that follows blanks (a comment) or to the end of the line
                    size_t end = line.size();
                    while (end > eq + 1 && (line[end - 1] == '\n' || line[end - 1] == '\r')) --end;
                    size_t cut = end;
                    for (size_t i = eq + 1; i < end; ++i)
                        if (line[i] == ';' && i > eq + 1 && (line[i - 1] == ' ' || line[i - 1] == '\t')) {
                            cut = i;
                            while (cut > eq + 1 && (line[cut - 1] == ' ' || line[cut - 1] == '\t')) --cut;
                            break;
                        }
                    text.replace(pos + eq + 1, cut - (eq + 1), value);
                    return true;
                }
                insertAt = next;
            }
        }
        pos = next;
    }
    std::string add;
    add.append(key).append("=").append(value).append(eol);
    if (sectionFound) {
        if (insertAt > 0 && text[insertAt - 1] != '\n') add.insert(0, eol);   // the last line had no ending
        text.insert(insertAt, add);
        return true;
    }
    if (!text.empty() && text.back() != '\n') text += eol;
    if (!text.empty()) text += eol;
    text.append("[").append(section).append("]").append(eol).append(add);
    return true;
}

}  // namespace inipatch
