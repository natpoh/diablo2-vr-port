// replay_groups.h - the left pass's command lists as the game submitted them, and the submissions the replay makes of them. Pure (tools/replay_groups_test.cpp).
//
// The stereo replay runs the lists of the left pass again for the right eye. 2026-10-18: one ExecuteCommandLists for a queue's whole run of lists (9438732, test-60..62)
// ran the direct queue into a list that never ended (hang_1033, hang_1040; the dump's pair: 15 lists on the copy queue and 16 on the direct queue, each in one call);
// one call a list with a signal after each (what the lists were recorded for, and test-57) never hung. The lists were recorded for the game's own calls: a resource
// promoted out of COMMON decays when an ExecuteCommandLists ends, and the driver flushes and drains there. So the replay can submit them as the game did - the same
// lists in the same calls - which costs the game's number of submissions and no more.
//
// ReShade fires its execute_command_list event once for each list of a call, in order, and then makes the call: the call itself is seen by a hook on the queue's
// ExecuteCommandLists (vr/ecl_hook.h), which gives each captured list the number of its call (Tag).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace replaygroups {

// [debug] replay_marks
enum Mode {
    kBatched = 0,        // a queue's whole run in ONE call, no signals (test-60..62: hangs)
    kPerListSignal = 1,  // a call a list, a signal after each (test-57; the default until the game's grouping is tested)
    kPerList = 2,        // a call a list, no signals
    kGame = 3,           // the game's calls, no signals of ours between them
    kGameSignal = 4,     // the game's calls, a signal after each
};

inline bool SignalAfterEach(int mode) { return mode == kPerListSignal || mode == kGameSignal; }

// The game made call `id` (non-zero) with lists[0..n) on queue `q`: the entries of `subs` captured for them get the number. The events came just before the call,
// so the entries are at the end (other threads' may be between). Entries not found (a list the capture left out) are not an error; ones already numbered are not touched.
// Returns how many were numbered. Sub needs: q, cl, call.
template <class Sub>
inline unsigned Tag(std::vector<Sub>& subs, const void* q, const void* const* lists, unsigned n, uint32_t id, size_t window = 128) {
    unsigned tagged = 0;
    const size_t floor = subs.size() > window ? subs.size() - window : 0;
    for (unsigned k = n; k-- > 0;) {
        for (size_t i = subs.size(); i-- > floor;) {
            if ((const void*)subs[i].cl == lists[k] && (const void*)subs[i].q == q && subs[i].call == 0) { subs[i].call = id; ++tagged; break; }
        }
    }
    return tagged;
}

struct Batch { size_t first; size_t count; };   // subs[first .. first + count): one ExecuteCommandLists

// One queue's run of lists, subs[from, to) (all on the one queue), as submissions. A list without a call number (the hook did not see its call) is a call of its own.
template <class Sub>
inline std::vector<Batch> PlanRun(const std::vector<Sub>& subs, size_t from, size_t to, int mode) {
    std::vector<Batch> out;
    if (to > subs.size()) to = subs.size();
    if (from >= to) return out;
    if (mode == kBatched) { out.push_back({from, to - from}); return out; }
    if (mode == kGame || mode == kGameSignal) {
        size_t i = from;
        while (i < to) {
            size_t j = i + 1;
            if (subs[i].call != 0) while (j < to && subs[j].call == subs[i].call) ++j;
            out.push_back({i, j - i});
            i = j;
        }
        return out;
    }
    for (size_t i = from; i < to; ++i) out.push_back({i, 1});
    return out;
}

}  // namespace replaygroups
