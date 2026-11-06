#pragma once
// The Stats page's numbers from the plugin's strike statistics (vr/strike_stats_wire.h) - a copy taken only when they changed (the wire's
// revision), while the page is up: vr/skill_wheel.cpp (the skill panel's Settings tab) and vr/xr_headset_settings.h (the panel of its own).

#include "hset_stats.h"
#include "strike_stats_wire.h"

namespace hset {
namespace stats {

inline void Feed(View& v) {
    const uint64_t rev = strikestatswire::Revision();
    const bool on = strikestatswire::On();
    if (v.known && v.revision == rev && v.on == on) return;
    strikestats::Store s;
    int64_t started = 0;
    v.known = strikestatswire::Snapshot(&s, &started);
    v.on = on;
    v.revision = rev;
    v.started = started;
    v.heroes = s.Heroes();
    v.current = s.CurrentIndex();
}

}  // namespace stats
}  // namespace hset
