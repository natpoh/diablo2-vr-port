#pragma once
// gpu_timeline.h - [debug] gpu_timeline: does the graphics card sit idle within a pair, and while the processor does what? (2026-11-10, Serg:
// "может у нас стоят какие то пост обработки которые заставляют карту ждать")
//
// While on, every ExecuteCommandLists on a direct queue (the game's own, the replay's, ours - all go through vr/ecl_hook.cpp) is put between two
// tiny lists of our own, each one timestamp written and copied out: the card's time when it reached the batch and when it finished it. Lists
// in one queue run one after another, so a batch that starts later than the last one ended means the card had nothing to do in between - an
// idle gap. Each gap is put down to the part of the pair the processor was in when it submitted the batch that ended it (vr/frame/frame_order.cpp
// SetPhase): the game's frame between pairs, the XR begin (xrWaitFrame), the left pass, the right eye (the replay: its holds, camera, submit,
// present), the pair's end. A gap in the right eye = the card waited on our replay; in the left pass = on the game's recording (CPU-bound).
// A fence the queue waits on (ID3D12CommandQueue::Wait) shows as a gap too, in the part that submitted the next batch.
//
// Every 10 s, beside the pair lines: `gpu timeline, 10 s: queue ... busy B ms a pair, idle I ms a pair - before batches of: ...`.
// Costs two small ExecuteCommandLists a batch (~30-50 a pair) on the processor while on; nothing while off. The lists are recorded once (1024
// of them, each with its own timestamp) and submitted again and again; a slot is reused only after its stamp came back. View only.

#include <cstdint>
#include <string>

struct ID3D12CommandQueue;

namespace gputimeline {

enum Phase : int { kGame = 0, kXrBegin, kLeft, kRight, kPairEnd, kPhases };

void SetLogger(void (*log)(const char*));
// From LoadSettings ([debug] gpu_timeline)
void Configure(bool on);
// frame_order: which part of the pair the draw thread is in now
void SetPhase(int phase);
// The draw thread after each pair, with the game's direct queue (natived3d::GameQueue): the hook put in when first wanted, the stamps that came
// back read; the 10 s line (empty: none, or not on)
void OnPair(ID3D12CommandQueue* gameQueue);
std::string Line();

}  // namespace gputimeline
