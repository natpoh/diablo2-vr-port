#pragma once
// auto_tune_text.h - the words the headset's Video page shows of an "Auto" run ([autotune], vr/auto_tune_run.h): its title, up to
// hset::kAutoLines lines (what changed, then the measured steps) and its status line, in the player's language (uitext). Pure.

#include <algorithm>
#include <string>
#include <vector>

#include "auto_tune.h"
#include "auto_tune_run.h"
#include "hset_model.h"
#include "ui_text.h"

namespace autotunetext {

inline std::string Fps(float v) { return uitext::Num("%.0f", v); }

inline std::string StepLine(const autotunerun::Row& r) {
    using autotune::Phase;
    const std::pair<const char*, std::string> what{"what", r.what}, from{"from", r.from}, to{"to", r.to}, a{"a", Fps(r.fpsBefore)}, b{"b", Fps(r.fpsAfter)};
    switch ((Phase)r.phase) {
        case Phase::Baseline: return uitext::TrF("vr.autotune.line.start", "Your settings: {b} frames/s", {b});
        case Phase::Probe: return uitext::TrF("vr.autotune.line.probe", "Tried {what} {from} -> {to}: {a} -> {b} frames/s", {what, from, to, a, b});
        case Phase::Down: return uitext::TrF("vr.autotune.line.down", "Turned down {what} {from} -> {to}: {a} -> {b} frames/s", {what, from, to, a, b});
        case Phase::Up:
            return r.kept ? uitext::TrF("vr.autotune.line.up", "Back up {what} {from} -> {to}: {b} frames/s holds", {what, from, to, b})
                          : uitext::TrF("vr.autotune.line.up_no", "Back up {what} {from} -> {to}: {b} frames/s, too slow - left down", {what, from, to, b});
        case Phase::Final: return uitext::TrF("vr.autotune.line.final", "Checked again: {b} frames/s", {b});
        default: return {};
    }
}

inline hset::AutoTune Make(const autotunerun::View& v, bool enabled) {
    hset::AutoTune a;
    a.on = enabled;
    a.state = (int)v.state;
    a.revision = v.revision;
    if (v.state == autotunerun::State::Idle) return a;
    std::vector<std::string> lines;
    if (v.state == autotunerun::State::Running) {
        a.title = uitext::TrF("vr.autotune.title.running", "Auto: measuring, stand still ({done} done, about {left} to go)",
                              {{"done", std::to_string(v.measured)}, {"left", std::to_string(v.left)}});
        a.status = uitext::Tr("vr.autotune.status.running", "Each setting is tried for a few seconds. Stop puts everything back as it was.");
    } else {
        a.title = v.reached ? uitext::TrF("vr.autotune.title.reached", "Auto: {fps} frames/s reached (headset {hz} Hz)", {{"fps", Fps(v.targetFps)}, {"hz", Fps(v.hz)}})
                            : uitext::TrF("vr.autotune.title.not_reached", "Auto: {fps} frames/s not reached - this is the best it found (now {now})",
                                          {{"fps", Fps(v.targetFps)}, {"now", Fps(v.nowFps)}});
        a.status = uitext::Tr("vr.autotune.status.waiting", "Keep writes this to d2r_vr.ini (the old one is copied first). Undo puts everything back.");
        if (v.changes.empty()) lines.push_back(uitext::Tr("vr.autotune.line.no_change", "Nothing to change: your settings stay as they are."));
        for (const auto& c : v.changes)
            lines.push_back(uitext::TrF("vr.autotune.line.change", "Changed: {what} {from} -> {to}", {{"what", c.what}, {"from", c.from}, {"to", c.to}}));
    }
    // the measured steps under the changes: the latest that fit
    const int room = std::max(0, hset::kAutoLines - (int)lines.size());
    const int first = std::max(0, (int)v.rows.size() - room);
    for (int i = first; i < (int)v.rows.size(); ++i) lines.push_back(StepLine(v.rows[i]));
    lines.resize(std::min<size_t>(lines.size(), hset::kAutoLines));
    a.lines = std::move(lines);
    return a;
}

}  // namespace autotunetext
