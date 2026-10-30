// vr/render/stereo/replay.cpp - one CPU pass, two GPU frames: the left eye's command lists run again as the right eye's (the replay) (cut from vr/vrcam.cpp, docs/plan_modules.md M14).
// What it gives: vr/render/stereo/stereo.h. It sees vrcam.cpp's headers.
#include "../../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// One CPU pass, two GPU frames - prototype ([debug] replay_proto=1 when vrcam joins
// ReShade; [stereo] replay_right live: 1 replay, 3 no replay - the left picture twice).
// The left eye's pass runs as ever; its direct and compute command lists are noted
// as the game executes them (the copy queue's uploads are not: their data stays where
// they put it). Its Present is held back: in the flip model a presented back buffer
// belongs to DXGI - reading it, or drawing into it again, removed the device
// (0x887A002B, access denied). Instead the left picture is copied out of the back
// buffer, the noted lists are executed once more on the same queues (our own fence
// between them) - they draw into the same back buffer, still the current one - and
// that is presented as the right eye; then the left copy goes into the next back
// buffer and is presented as the left eye. Both through the game's swap chain
// (ReShade's proxy: the effects and FlatVR's add-on run on them). The camera's
// constant buffers are not patched yet: the right eye is the left one again - this
// proves the replay. DLSS must be off (its evaluation is recorded with one history).
namespace camfix { void RecordSlots(ID3D12GraphicsCommandList* l, int eye, int from = 0, int to = -1); }   // (declared here: its defaults are its definition's)

namespace replay {
using namespace reshade::api;
std::atomic<bool> g_on{false};
// This pair's right eye is the left's lists replayed (Wanted() at the pair's start), not a pass of its own: its interface layer is then laid out
// for the left eye (D2R_DepthFog.fx LabelAxisR). False while the replay waits for an area to settle (240 pairs after a change, a frame of over
// 150 ms) - the right pass lays its labels out for the right eye itself.
std::atomic<bool> g_rightReplayed{false};
std::atomic<int> g_capturing{0};   // 1 while the left pass runs
std::atomic<int> g_want{0};        // [stereo] replay_right, from LoadSettings
struct Sub { ID3D12CommandQueue* q; ID3D12CommandList* cl; uint32_t call = 0; };   // call: the number of the game's ExecuteCommandLists it was in (0 = not seen), see ObserveExec
std::vector<Sub> g_subs;           // the left pass's direct and compute submissions, in order
SRWLOCK g_lock = SRWLOCK_INIT;
IDXGISwapChain* g_proxy = nullptr; // the game's swap chain (ReShade's proxy): what it presents through
UINT g_presentFlags = 0;
IDXGISwapChain3* g_native = nullptr;
HANDLE g_latency = nullptr;   // the swap chain's frame latency waitable object, if it was made with one
bool g_latencyAsked = false;
bool g_leftHeld = false;           // the left pass's Present was held back and its picture copied out
uint8_t* g_wrap = nullptr;         // the game's swap chain wrapper (its present's r8); the game keeps the
                                   // back buffer index it draws into next at +0x98 (D2R 0x10FAE65: right after
                                   // its present it asks the swap chain and stores it there)
ID3D12Device* g_dev = nullptr;
ID3D12CommandQueue* g_direct = nullptr;
// A fence per queue: one fence signalled from two queues went back down when the
// second finished first, and a GPU wait on it never ended (the game hung).
struct Pt { ID3D12Fence* f = nullptr; UINT64 v = 0; };
// Where a replayed pair's time goes, us summed over the 10 s log period (Steps).
double g_stepUs[kSteps] = {};
uint32_t g_stepPairs = 0;
// The replay's submissions, summed over the log period: the loop of ExecuteCommandLists calls (queue switches included), the lists, the calls, the signals after them.
double g_eclUs = 0.0;
uint32_t g_eclLists = 0, g_eclCalls = 0, g_eclSignals = 0, g_eclUntagged = 0;   // untagged: lists no game call was seen for (modes 3, 4: each then a call of its own)
struct StepTimer { int k; double t0; StepTimer(int k_) : k(k_), t0(pairtime::UsNow()) {} ~StepTimer() { g_stepUs[k] += pairtime::UsNow() - t0; } };
// The frame log's Y line: this pair's steps, us (the sums since the last pair; they restart every 10 s).
void PairLine(bool replayed) {
    static double last[kSteps] = {};
    double d[kSteps];
    for (int i = 0; i < kSteps; ++i) { d[i] = g_stepUs[i] >= last[i] ? g_stepUs[i] - last[i] : g_stepUs[i]; last[i] = g_stepUs[i]; }
    flog::Line("Y,%u,%d,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f", flog::Pass(), replayed ? 1 : 0,
               d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8]);
}
struct QFence { ID3D12CommandQueue* q; ID3D12Fence* f; UINT64 v; };
std::vector<QFence> g_qf;
Pt g_lastReplay, g_prevReplay;
ID3D12Fence* g_fence = nullptr;   // (made with the first one: Setup's "ready")
std::atomic<bool> g_restorePending{false};
std::atomic<int> g_ahead{1};   // [stereo] replay_ahead: replays the CPU may run ahead of (1 or 2)   // the camera buffers to put back before the left pass reaches the GPU
HANDLE g_event = nullptr;
constexpr int kAllocs = 16;
ID3D12CommandAllocator* g_alloc[kAllocs] = {};
Pt g_allocDone[kAllocs] = {};
int g_allocAt = 0, g_openAlloc = 0;
ID3D12GraphicsCommandList* g_list = nullptr;
ID3D12Resource* g_leftImg = nullptr;   // the left eye's picture, out of the back buffer
// One present a pair ([stereo] replay_fx, on): at the hold the left eye's effects run on its
// back buffer with its own depth (render_effects) and FlatVR's add-on takes it from that
// effect pass; the replay draws the right eye into the same back buffer, presented once -
// as many presents as the game's waits (two a pair drifted and hung), the sky by each
// eye's own depth, no copy of the left picture.
reshade::api::effect_runtime* g_rt = nullptr;
std::atomic<bool> g_fxAtHold{true};
// ReShade renders its effects once between two presents (render_effects returns at once the
// second time): the right eye's, rendered after the left eye's at the hold, never ran - no sky
// and FlatVR's add-on, which takes the picture from the effect pass, had no right eyes (its
// stream stopped). The right eye gets only our technique (D2R_DepthFog_R, render_technique:
// no once-a-frame check, and the begin / finish effects events FlatVR's add-on takes it from).
reshade::api::effect_technique g_rightTech{};
// DLSS in the replay: the left pass's list carries both evaluations - the game's DLSS instance
// and the right eye's twin - each under D3D12 predication on this buffer: [0] (u64) the game's,
// [1] the twin's, 0 = skipped. Before the left pass {1, 0}, before the replay {0, 1}: each eye
// runs on its own instance and history (one instance for both smeared each eye into the other).
ID3D12Resource* g_predBuf = nullptr;   // DEFAULT, 16 bytes used, D3D12_RESOURCE_STATE_PREDICATION once written
ID3D12Resource* g_predSrc = nullptr;   // UPLOAD: {1, 0} at 0, {0, 1} at 16
bool g_predReady = false;
std::atomic<uint32_t> g_predEvals{0};
std::atomic<bool> g_dlssBoth{false};   // [stereo] replay_dlss: 1 both eyes' evaluations, predicated; 0 the game's alone (one instance)
// [stereo] replay_fx_sync (on): the left eye's effects wait for the last pair to be done on the
// GPU. ReShade writes an effect's constants in place, once per render: with the CPU a pair
// ahead, the next left eye's values (its frame stamps among them) were in the buffer before
// the GPU drew the last right eye's effects - its picture went to FlatVR with the next pair's
// head stamp, and the right eye juddered as the head turned.
std::atomic<bool> g_fxSync{true};
// Our depth copy is bound to the effects ONCE (scenedepth::Bind, 2026-10-16; Bear's d2r-pcvr scene_depth.cpp does it so):
// bound at every effect pass, ReShade's update_texture_bindings waited for the graphics queue to go idle first
// (runtime_api.cpp, D3D12) - twice a pair the game's thread waited for the GPU to finish all it had ("effects" 4-8 ms an
// eye, ~half the pair) and the GPU stood idle while the thread recorded the next frame; once a present in the modes of one
// picture (98 instead of ~180 fps at 1280x720). Only with own depth (VR + a square window or a render size): 0.164 had no
// square_window, 0.166 ships it. Now bound again only when the copy's view changes or the effects reload.
uint64_t g_depthBound = 0;            // the view last bound as D2RVR_DEPTH (0 after the effects reload or the runtime goes)
std::atomic<uint32_t> g_depthBinds{0}, g_depthKept{0};
// [render] fx_fence (off; Settings > Stereo 3D, F4 only): what the drain also did, by fences. ReShade writes an effect's
// constants into one buffer in place at each render, and both eyes' techniques are one effect: a pass could rewrite them
// before the GPU drew the last one. Within a pair both passes write the same values (each sets both eyes' arrays; the
// left eye's stamp does not move between them), so only exactness is at stake: on, the right eye's effects wait for the
// left eye's to be done on the GPU (the replay is queued behind them: the GPU stays busy), the left eye's for the last
// pair (replay_fx_sync, forced on), and a frame of one picture for the last frame's effects (its stamp, scenedepth::).
std::atomic<bool> g_fxFence{false};
thread_local bool t_pairPass = false; // inside a pair's effect pass (the replay fences it, not scenedepth::)
Pt g_leftFx;                          // the left eye's effects on the GPU (signalled after them)
bool g_pairFence = false;             // this pair's effects went the fenced way (set at the hold, read at the right eye)
std::atomic<uint32_t> g_fxFenceWaits{0};
uint64_t g_rtvRes[8] = {};
reshade::api::resource_view g_rtv[8] = {};
// The depth buffer ReShade's effects read (D2R_DepthFog.fx FogDepthTex's binding) and its
// state as the game's last barrier left it: the left eye is presented after the replay,
// when that buffer holds the right eye's depth - the sky and the fog of the left eye were
// drawn by the right eye's depth (a dark seam beside the head). Its depth goes back first.
std::atomic<uint64_t> g_depthRes{0};
std::atomic<uint32_t> g_depthState{0};   // 0: no barrier seen - the game keeps it as a depth target
// [stereo] replay_depth (off): the copy needs the buffer's true state - COMMON assumed lost the
// shadows, DEPTH_WRITE removed the device (0x887A0001); no barrier on it is ever seen.
std::atomic<bool> g_depthCopy{false};
// The state to transition the depth buffer from: the game's last barrier on it, else DEPTH_WRITE
// (COMMON assumed for a depth target the game never moves broke its compression: no shadows).
D3D12_RESOURCE_STATES DepthState() {
    const uint32_t st = g_depthState.load();
    return st ? (D3D12_RESOURCE_STATES)st : D3D12_RESOURCE_STATE_DEPTH_WRITE;
}
ID3D12Resource* g_leftDepth = nullptr;
// The right eye's depth for its effects (2026-10-11). In D3D12 ReShade's Generic Depth hands the
// effects a copy of the game's depth buffer (g_depthRes), made as it sees the game's lists go -
// through ReShade. The replay sends the left pass's lists again on the queue itself, past ReShade:
// the copy stayed the left eye's, and the right eye's fog, sky and ceiling were cut out by the left
// eye's depth - "ReShade's depth is in mono", players' doubled right eye in "one pass, two pictures"
// (right for the first seconds of an area, while two passes were still drawn). So the depth-stencil
// the pass binds most that is the copy's size and kind goes into the copy before the right eye's
// effects - the barriers as Generic Depth makes them for its own copies; the depth-stencil from the
// state the game's last barrier on it left (else as a depth target). [stereo] replay_right_depth (on).
std::atomic<bool> g_rightDepth{true};
struct DsSeen { uint64_t res; uint32_t binds; uint32_t state; };
DsSeen g_ds[8] = {};
SRWLOCK g_dsLock = SRWLOCK_INIT;
std::atomic<uint32_t> g_rightDepthCopies{0};
std::atomic<uint32_t> g_replays{0}, g_fails{0};
// The back buffer indices of the last pairs, for the watchdog: the one the left pass drew in
// (DXGI's, and the game's own at its wrapper +0x98), the right eye's, DXGI's after our present.
struct PairIdx { UINT hold, game, right, after, presents; HRESULT hr; };
PairIdx g_pairIdx[16] = {};
uint32_t g_pairIdxAt = 0;
UINT g_gameIdxNow = ~0u;

// The game's end-of-frame signals held until the replay is on the queue (2026-10-09). The
// game signals its frame fences after its last lists, and on the GPU those came before our
// replay: the game took the frame for done and freed what it drew with while the replay
// still read it (the device hung at an area change, 0x887A0006). A direct or compute queue's
// signals are kept back while the left pass runs; the next submission on that queue lets
// them go first (they were mid-frame - compute waits on them), and the ones left at the
// game's present are issued after the replay.
using QSignalFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, ID3D12Fence*, UINT64);
// each queue type may have its own vtable (the command lists do): a trampoline per slot
QSignalFn OrigQSignalT[3] = {};
QSignalFn OrigQSignal = nullptr;   // any of them, for our own calls (they go through the queue's vtable anyway)
struct HeldSig { ID3D12CommandQueue* q; ID3D12Fence* f; UINT64 v; };
std::vector<HeldSig> g_held;
SRWLOCK g_heldLock = SRWLOCK_INIT;
std::atomic<bool> g_holdSignals{false};
// [stereo] replay_hold_signals (off): the counters showed none ever left for the replay (the
// game's own queue waits let them go), and a signal held while the game loads an area is a
// wait that never ends.
std::atomic<bool> g_holdSignalsOn{false};
std::atomic<uint32_t> g_heldMax{0};
thread_local bool t_ours = false;   // our own signals pass
std::atomic<uint32_t> g_sigCalls{0}, g_sigHeld{0}, g_sigWhileHolding{0};
// The game's own Signal / Wait / ExecuteCommandLists on its queues, the last 1024, for the
// watchdog: what a stuck queue waits for and who was to signal it (an area change hung the
// direct queue inside the replay, 2026-10-09).
struct GameOp { ID3D12CommandQueue* q; ID3D12Fence* f; UINT64 v; DWORD tid; char op; };
GameOp g_gameOps[1024];
std::atomic<uint32_t> g_gameOpAt{0};
UINT64 SafeDone(ID3D12Fence* f) { __try { return f->GetCompletedValue(); } __except (EXCEPTION_EXECUTE_HANDLER) { return ~0ull; } }
void NoteGame(ID3D12CommandQueue* q, char op, ID3D12Fence* f, UINT64 v) {
    if (op != 'w' && q->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_COPY) return;   // the streaming's flood: only its waits
    const uint32_t i = g_gameOpAt.fetch_add(1, std::memory_order_relaxed) & 1023;
    g_gameOps[i] = {q, f, v, GetCurrentThreadId(), op};
}
template <int S> HRESULT STDMETHODCALLTYPE HookQSignal(ID3D12CommandQueue* q, ID3D12Fence* f, UINT64 v) {
    g_sigCalls.fetch_add(1, std::memory_order_relaxed);
    if (!t_ours) NoteGame(q, 's', f, v);
    if (g_holdSignals.load(std::memory_order_relaxed)) g_sigWhileHolding.fetch_add(1, std::memory_order_relaxed);
    if (!t_ours && g_holdSignals.load(std::memory_order_relaxed) && q->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_COPY) {
        g_sigHeld.fetch_add(1, std::memory_order_relaxed);
        AcquireSRWLockExclusive(&g_heldLock);
        g_held.push_back({q, f, v});
        ReleaseSRWLockExclusive(&g_heldLock);
        return S_OK;
    }
    return OrigQSignalT[S](q, f, v);
}
// Let a queue's held signals go (before its next submission, or after the replay).
void ReleaseHeld(ID3D12CommandQueue* q) {
    AcquireSRWLockExclusive(&g_heldLock);
    std::vector<HeldSig> keep;
    for (const HeldSig& h : g_held) {
        if (!q || h.q == q) { t_ours = true; h.q->Signal(h.f, h.v); t_ours = false; }
        else keep.push_back(h);
    }
    g_held.swap(keep);
    ReleaseSRWLockExclusive(&g_heldLock);
}
// A wait the game puts on its direct or compute queue lets every held signal go first: the
// other queue may be waiting for one of them (direct signals, compute waits and signals,
// direct waits for that - held, it was a deadlock). The copy queue's waits may wait on.
using QWaitFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, ID3D12Fence*, UINT64);
QWaitFn OrigQWaitT[3] = {};
template <int S> HRESULT STDMETHODCALLTYPE HookQWait(ID3D12CommandQueue* q, ID3D12Fence* f, UINT64 v) {
    if (!t_ours) NoteGame(q, 'w', f, v);
    if (!t_ours && g_holdSignals.load(std::memory_order_relaxed) && q->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_COPY) ReleaseHeld(nullptr);
    return OrigQWaitT[S](q, f, v);
}
std::atomic<bool> g_sigHooked{false};   // the direct queue's (the one that matters) is in
std::vector<void*> g_qHookedFns;
bool g_qSlotDone[3] = {};
template <int S> bool HookQTable(void** vt) {
    bool ok = true;
    if (std::find(g_qHookedFns.begin(), g_qHookedFns.end(), vt[14]) == g_qHookedFns.end()) {
        ok &= MH_CreateHook(vt[14], (void*)&HookQSignal<S>, (void**)&OrigQSignalT[S]) == MH_OK && MH_EnableHook(vt[14]) == MH_OK;
        g_qHookedFns.push_back(vt[14]);
    }
    if (std::find(g_qHookedFns.begin(), g_qHookedFns.end(), vt[15]) == g_qHookedFns.end()) {
        ok &= MH_CreateHook(vt[15], (void*)&HookQWait<S>, (void**)&OrigQWaitT[S]) == MH_OK && MH_EnableHook(vt[15]) == MH_OK;
        g_qHookedFns.push_back(vt[15]);
    }
    return ok;
}
// The game's ExecuteCommandLists, seen at the real call (vr/ecl_hook.h): the lists captured for it just before (ReShade's event for each) get the call's number
// (vr/replay_groups.h), so that [debug] replay_marks 3 and 4 can submit them as the game did. Only while the left pass is captured; not our own calls.
std::atomic<uint32_t> g_eclSeq{0};
void ObserveExec(ID3D12CommandQueue* q, unsigned n, ID3D12CommandList* const* lists) {
    if (t_ours || !n || !g_capturing.load(std::memory_order_relaxed)) return;
    AcquireSRWLockExclusive(&g_lock);
    replaygroups::Tag(g_subs, q, (const void* const*)lists, n, g_eclSeq.fetch_add(1, std::memory_order_relaxed) + 1);
    ReleaseSRWLockExclusive(&g_lock);
}
void HookQueueSignal(ID3D12CommandQueue* q) {
    const D3D12_COMMAND_LIST_TYPE t = q->GetDesc().Type;
    const int slot = t == D3D12_COMMAND_LIST_TYPE_DIRECT ? 0 : t == D3D12_COMMAND_LIST_TYPE_COMPUTE ? 1 : 2;
    if (g_qSlotDone[slot]) return;
    g_qSlotDone[slot] = true;
    void** vt = *(void***)q;
    const bool ok = slot == 0 ? HookQTable<0>(vt) : slot == 1 ? HookQTable<1>(vt) : HookQTable<2>(vt);
    if (slot == 0) g_sigHooked.store(ok);
    LogF("vrcam: replay - the %s queue's Signal/Wait hooked %s, vtable %p", slot == 0 ? "direct" : slot == 1 ? "compute" : "copy",
         ok ? "in" : "FAILED", (void*)vt);
}

void WaitFor(const Pt& p);
bool Setup(ID3D12CommandQueue* q);
void CopySlots(int eye);
void CopyLateSlots(ID3D12CommandQueue* nq);
bool MakePred();
void RecordPred(ID3D12GraphicsCommandList* l, int eye);
int g_leftCopied = 0;
std::atomic<uint32_t> g_lateSlots{0};
// The game's lists on its direct and compute queues outside the left pass (not replayed: what
// they draw is the left eye's in both - the right eye's shadows stayed where the left eye had
// them), by thread: the draw thread's own, and others'.
std::atomic<uint32_t> g_outsideDraw{0}, g_outsideOther{0}, g_insideLists{0};
std::atomic<DWORD> g_captureThread{0};
std::atomic<bool> g_replayCopy{true};   // [stereo] replay_copy (see OnExecute)
std::atomic<uint32_t> g_copyLists{0};
// who sends them: the last few threads, queue types and the moment (us since the pair's left pass began)
struct Outside { DWORD tid; int type; double atUs; };
Outside g_outside[8] = {};
std::atomic<uint32_t> g_outsideAt{0};
std::atomic<double> g_pairStartUs{0.0};
void OnExecute(command_queue* q, command_list* cl) {
    HookQueueSignal((ID3D12CommandQueue*)q->get_native());
    if (!t_ours) NoteGame((ID3D12CommandQueue*)q->get_native(), 'x', nullptr, 0);
    if (!t_ours && g_on.load(std::memory_order_relaxed) && g_want.load(std::memory_order_relaxed) == 1) {
        const command_queue_type t = q->get_type();
        if ((t & command_queue_type::graphics) != 0 || (t & command_queue_type::compute) != 0) {
            if (g_capturing.load(std::memory_order_relaxed)) g_insideLists.fetch_add(1, std::memory_order_relaxed);
            else if (GetCurrentThreadId() == g_captureThread.load(std::memory_order_relaxed)) g_outsideDraw.fetch_add(1, std::memory_order_relaxed);
            else {
                g_outsideOther.fetch_add(1, std::memory_order_relaxed);
                const uint32_t i = g_outsideAt.fetch_add(1) & 7;
                g_outside[i] = {GetCurrentThreadId(), (t & command_queue_type::graphics) != 0 ? 0 : 2, pairtime::UsNow() - g_pairStartUs.load()};
            }
        }
    }
    if (!g_capturing.load(std::memory_order_relaxed)) return;
    const command_queue_type t = q->get_type();
    // The copy queue: its lists from the draw thread inside the left pass are part of the frame -
    // mid-frame the direct queue signals, the copy queue waits for it, copies, and the direct
    // queue waits for that ([stereo] replay_copy, on: replayed too; left out, the right eye read
    // the left eye's copy - its character sat in shadows of its own, 2026-10-10). The streaming's
    // uploads come from other threads and stay out.
    if ((t & command_queue_type::graphics) == 0 && (t & command_queue_type::compute) == 0 &&
        !(g_replayCopy.load(std::memory_order_relaxed) && GetCurrentThreadId() == g_captureThread.load(std::memory_order_relaxed))) return;
    ReleaseHeld((ID3D12CommandQueue*)q->get_native());   // signals mid-frame: before this queue's next lists
    if (g_restorePending.exchange(false)) {   // the left pass's first list: the left camera into the slots first
        StepTimer st(kWaitRestore);
        if (camfix::g_redirect.load()) {
            if (!g_direct) {   // (the first pair: the direct queue is this one)
                D3D12_COMMAND_QUEUE_DESC d = ((ID3D12CommandQueue*)q->get_native())->GetDesc();
                if (d.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) g_direct = (ID3D12CommandQueue*)q->get_native();
            }
            if (g_direct && Setup(g_direct)) CopySlots(0);
        } else {
            WaitFor(g_lastReplay);
            camfix::Restore();
        }
    }
    else CopyLateSlots((ID3D12CommandQueue*)q->get_native());
    AcquireSRWLockExclusive(&g_lock);
    g_subs.push_back({(ID3D12CommandQueue*)q->get_native(), (ID3D12CommandList*)cl->get_native()});
    ReleaseSRWLockExclusive(&g_lock);
}

void OnBarrier(command_list*, uint32_t count, const resource* res, const resource_usage*, const resource_usage* to) {
    const uint64_t d = g_depthRes.load(std::memory_order_relaxed);
    if (!d) return;
    for (uint32_t i = 0; i < count; ++i)
        if (res[i].handle == d) g_depthState.store((uint32_t)to[i], std::memory_order_relaxed);
    if (!g_rightDepth.load(std::memory_order_relaxed) || fx::OwnDepthNow()) return;
    AcquireSRWLockExclusive(&g_dsLock);
    for (uint32_t i = 0; i < count; ++i)
        for (DsSeen& s : g_ds)
            if (s.res && s.res == res[i].handle) s.state = (uint32_t)to[i];
    ReleaseSRWLockExclusive(&g_dsLock);
}

// A depth-stencil bound while the left pass records: counted (the right eye's depth comes from the
// one bound most that fits the effects' copy).
void OnBindDs(command_list* cl, uint32_t, const resource_view*, resource_view dsv) {
    if (!dsv.handle || !g_capturing.load(std::memory_order_relaxed) || !g_rightDepth.load(std::memory_order_relaxed)) return;
    if (fx::OwnDepthNow()) return;   // own depth: RightDepthIntoCopy has nothing to do, nothing to count for it
    const uint64_t r = cl->get_device()->get_resource_from_view(dsv).handle;
    if (!r) return;
    AcquireSRWLockExclusive(&g_dsLock);
    DsSeen* slot = nullptr;
    for (DsSeen& s : g_ds) {
        if (s.res == r) { slot = &s; break; }
        if (!s.res && !slot) slot = &s;
    }
    if (slot) {
        if (!slot->res) *slot = DsSeen{r, 0, 0};
        ++slot->binds;
    }
    ReleaseSRWLockExclusive(&g_dsLock);
}

bool SkipWhileStuck(const char* what);   // (the stall guard, below)
// After the replay, before the right eye's effects: its depth into the effects' copy (see g_rightDepth).
void RightDepthIntoCopy() {
    using namespace reshade::api;
    static int told = -1;   // the last thing logged, once each
    auto tell = [&](int what, const char* text) { if (told != what) { told = what; Log(text); } };
    if (!g_rightDepth.load() || !g_rt) return;
    if (SkipWhileStuck("the right eye's depth copy")) return;
    // Own depth (scenedepth::): the effects read OUR copy, and scenedepth::Bind copies the right eye's depth-stencil into it
    // at the right eye's begin-effects anyway (the replay left it there) - a second copy here, a depth decompress with it,
    // was overwritten before anything read it (2026-10-16).
    if (fx::OwnDepthNow()) return tell(4, "vrcam: replay - right eye's depth: own depth - scenedepth copies it at the right eye's effects, nothing to do here");
    const uint64_t copy = g_depthRes.load();
    if (!copy) return tell(1, "vrcam: replay - right eye's depth: the effects' depth buffer not known yet");
    device* dev = g_rt->get_device();
    const resource_desc cd = dev->get_resource_desc({copy});
    if ((cd.usage & resource_usage::depth_stencil) != 0)
        return tell(2, "vrcam: replay - right eye's depth: the effects read the game's depth buffer itself - the replay wrote it, nothing to copy");
    DsSeen best{};
    AcquireSRWLockShared(&g_dsLock);
    for (const DsSeen& s : g_ds) {
        if (!s.res || s.res == copy || s.binds <= best.binds) continue;
        const resource_desc d = dev->get_resource_desc({s.res});
        if ((d.usage & resource_usage::depth_stencil) == 0 || d.texture.width != cd.texture.width || d.texture.height != cd.texture.height ||
            d.texture.samples != cd.texture.samples || format_to_typeless(d.texture.format) != format_to_typeless(cd.texture.format))
            continue;
        best = s;
    }
    ReleaseSRWLockShared(&g_dsLock);
    if (!best.res) return tell(3, "vrcam: replay - right eye's depth: no depth-stencil of the pass fits the effects' copy - left as it is");
    const resource src{best.res}, dst{copy};
    const resource_usage from = best.state ? (resource_usage)best.state : resource_usage::depth_stencil_write;
    command_queue* q = g_rt->get_command_queue();
    command_list* cl = q->get_immediate_command_list();
    t_ours = true;
    cl->barrier(src, from, resource_usage::copy_source);
    cl->barrier(dst, resource_usage::shader_resource, resource_usage::copy_dest);
    cl->copy_resource(src, dst);
    cl->barrier(src, resource_usage::copy_source, from);
    cl->barrier(dst, resource_usage::copy_dest, resource_usage::shader_resource);
    q->flush_immediate_command_list();
    t_ours = false;
    if (g_rightDepthCopies.fetch_add(1) == 0)
        LogF("vrcam: replay - right eye's depth: the pass's depth-stencil %p (%ux%u, bound %u times, from state %X) into the effects' copy %p "
             "before the right eye's effects - its fog and sky by its own depth ([stereo] replay_right_depth=0 leaves the left one's)",
             (void*)best.res, cd.texture.width, cd.texture.height, best.binds, (unsigned)from, (void*)copy);
    told = 0;
}

void OnPresent(command_queue*, swapchain* sc, const rect*, const rect*, uint32_t, const rect*) {
    if (!g_native) {
        IDXGISwapChain* n = (IDXGISwapChain*)sc->get_native();
        if (n) n->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&g_native);
    }
}

bool Setup(ID3D12CommandQueue* q) {
    if (g_list) return true;
    if (FAILED(q->GetDevice(__uuidof(ID3D12Device), (void**)&g_dev))) return false;
    if (FAILED(g_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&g_fence))) return false;
    g_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    for (int i = 0; i < kAllocs; ++i)
        if (FAILED(g_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&g_alloc[i]))) return false;
    if (FAILED(g_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_alloc[0], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&g_list))) return false;
    g_list->Close();
    g_list->SetName(L"d2rvr replay copy list");   // (the names DRED prints for what the GPU was running)
    camfix::MakeSlots(g_dev);
    Log("vrcam: replay - fence, allocators and the copy list made");
    return true;
}

// The last 64 signals and GPU waits we put on the queues, for the watchdog.
struct Hist { ID3D12CommandQueue* q; char op; ID3D12Fence* f; UINT64 v; const char* why; };
Hist g_hist[256];
int g_histAt = 0;
const char* g_why = "";
void Note(ID3D12CommandQueue* q, char op, ID3D12Fence* f, UINT64 v) { g_hist[g_histAt++ & 255] = {q, op, f, v, g_why}; }
// [debug] replay_marks (Settings > Debug, live), how the replayed lists are submitted:
//   1 (the default): one ExecuteCommandLists a list and a signal after each, and around the right eye's effects and present - the watchdog's "done" then says which list
//     the queue stopped at. Since the hang hunt of 2026-10-09 ("no hangs since"); ~35 submissions with a signal each a pair (the right pass's "submit" 0.57 ms).
//   0: each queue's run of lists in ONE ExecuteCommandLists, no signals (test-60 .. test-62's default, 9438732): the direct queue ran a list that never ended 30 s to
//     3 min in, with a bow drawn (hang_1033, hang_1040, 2026-10-18; no hang with 1, none with replay_right=0).
//   2: one ExecuteCommandLists a list, no signals: the batch's difference without the signals' - which of the two it is that the hang needs (a boundary the lists were
//     recorded for - a resource promoted out of COMMON decays when an ExecuteCommandLists ends - or the pipeline drain a signal makes) is what a run with 2 tells.
//   3: the game's own calls - the same lists in the same ExecuteCommandLists calls as the game made them (vr/replay_groups.h; the calls are seen by a hook on the queue,
//     vr/ecl_hook.h, put in at the first pair), no signals of ours but the queue switches'. The game's number of submissions and no more. 4: the same, a signal after each.
std::atomic<int> g_marksOn{1};
int g_marks = 1;   // this pair's (read once at its start)
const char* const kListMark[32] = {
    "replay: list 0", "replay: list 1", "replay: list 2", "replay: list 3", "replay: list 4", "replay: list 5", "replay: list 6", "replay: list 7",
    "replay: list 8", "replay: list 9", "replay: list 10", "replay: list 11", "replay: list 12", "replay: list 13", "replay: list 14", "replay: list 15",
    "replay: list 16", "replay: list 17", "replay: list 18", "replay: list 19", "replay: list 20", "replay: list 21", "replay: list 22", "replay: list 23",
    "replay: list 24", "replay: list 25", "replay: list 26", "replay: list 27", "replay: list 28", "replay: list 29", "replay: list 30", "replay: list 31+"};
void GpuWait(ID3D12CommandQueue* q, const Pt& p) { if (p.f) { t_ours = true; q->Wait(p.f, p.v); t_ours = false; Note(q, 'W', p.f, p.v); } }
Pt SignalOn(ID3D12CommandQueue* q) {
    for (QFence& x : g_qf)
        if (x.q == q) {
            t_ours = true; q->Signal(x.f, ++x.v); t_ours = false;
            Note(q, 'S', x.f, x.v); return {x.f, x.v};
        }
    QFence x{q, nullptr, 0};
    if (FAILED(g_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&x.f))) return {};
    g_qf.push_back(x);
    return SignalOn(q);
}
void WaitFor(const Pt& p) {
    if (!p.f || p.f->GetCompletedValue() >= p.v) return;
    p.f->SetEventOnCompletion(p.v, g_event);
    WaitForSingleObject(g_event, 100);
}
// The allocator of a list we reuse: its last list must be done before it is reset - a reset under a list the GPU still runs removes the
// device (2026-10-17). Waited for up to 2 s, not 100 ms: a GPU that is not done in that long is hung and the device is lost anyway.
void WaitForAlloc(const Pt& p) {
    if (!p.f || p.f->GetCompletedValue() >= p.v) return;
    p.f->SetEventOnCompletion(p.v, g_event);
    if (WaitForSingleObject(g_event, 2000) == WAIT_TIMEOUT) {
        static int told = 0;
        if (told++ < 3) Log("vrcam: replay - the GPU has not finished a list of ours in 2 s: its allocator is reset anyway (the device is likely hung)");
    }
}

// -- the stall guard (vr/stall_guard.h) --------------------------------------------------------------------------------------------
// [debug] stall_guard (on): two hangs of 2026-10-18 (crash_1009, hang_1033) left the game's direct queue standing for good; the draw thread of the second sat inside
// ReShade's flush, an infinite wait on a fence of a submission eight flushes old. Nothing of ours waits without a limit - ReShade's wait and the game's are what
// stand. So: (1) our optional work that would wait on the queue (SkipWhileStuck) is not started while the queue stands (250 ms, [debug] stall_skip_ms); (2) after
// 8 s ([debug] stall_unstick_ms) with the presents stopped and the queue still, the watchdog signals our own cross-queue fences from the CPU, every 5 s while it
// stands - and, only with [debug] stall_unstick_game, the game's fences the queues wait for that nothing queued will reach (it tells the game a thing done that is not;
// off: the dump of hang_1033 showed the queue not waiting at all but running a list that never ends, which no signal frees); (3) [debug] stall_remove_ms (0 = never,
// [debug] stall_remove = 20000): the device is taken away on purpose, so that DRED says which list the queue was in (gpu_fault.cpp) - for the tests.
std::atomic<bool> g_guardGameFences{false};   // [debug] stall_unstick_game
std::atomic<bool> g_guardOn{true};
stallguard::Limits g_guardLimits;
stallguard::Progress g_guardProg;   // the present thread's looks (g_guardLock)
SRWLOCK g_guardLock = SRWLOCK_INIT;
double GuardMs() { return pairtime::UsNow() / 1000.0; }
// How long the game's direct queue has stood still with our work on it, ms; 0 = it moves, is idle, or the guard is off.
double QueueStuckMs() {
    if (!g_guardOn.load(std::memory_order_relaxed) || !g_direct) return 0.0;
    for (const QFence& x : g_qf) {
        if (x.q != g_direct || !x.f) continue;
        AcquireSRWLockExclusive(&g_guardLock);
        const double ms = stallguard::Feed(g_guardProg, x.v, SafeDone(x.f), GuardMs());
        ReleaseSRWLockExclusive(&g_guardLock);
        return ms;
    }
    return 0.0;
}
// Optional work that would wait on the queue: true = do not start it now. Tells each time for the first few, then now and then.
bool SkipWhileStuck(const char* what) {
    const double ms = QueueStuckMs();
    if (stallguard::Decide(ms, g_guardLimits) == stallguard::Level::None) return false;
    static std::atomic<uint32_t> n{0};
    const uint32_t k = n.fetch_add(1, std::memory_order_relaxed);
    if (k < 12 || k % 500 == 0) LogF("vrcam: stall guard - the direct queue has not moved for %.0f ms: %s skipped (%u so far)", ms, what, k + 1);
    return true;
}
bool SafeSignal(ID3D12Fence* f, UINT64 v) { __try { return SUCCEEDED(f->Signal(v)); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
// The watchdog thread: the queue has stood still for good. Signals from the CPU what it may wait for (stall_guard.h, PickForUnstick), and ours.
void GuardUnstick(double stuckMs) {
    stallguard::Wait w[200];
    stallguard::Signal sg[300];
    size_t nw = 0, ns = 0;
    const uint32_t end = g_gameOpAt.load(std::memory_order_relaxed);
    const uint32_t n = std::min<uint32_t>(end, 300);
    for (uint32_t i = end - n; i != end; ++i) {
        const GameOp& o = g_gameOps[i & 1023];
        if (!o.q || !o.f) continue;
        if (o.op == 'w' && nw < 200) {
            const UINT64 done = SafeDone(o.f);
            if (done != ~0ull) w[nw++] = {o.f, o.v, done};
        } else if (o.op == 's' && ns < 300) {
            sg[ns++] = {o.f, o.v};
        }
    }
    for (int i = 256 - 96; i < 256; ++i) {   // ours: a signal of ours is a signal
        const Hist& h = g_hist[(g_histAt + i) & 255];
        if (!h.q || !h.f) continue;
        if (h.op == 'S' && ns < 300) sg[ns++] = {h.f, h.v};
    }
    stallguard::Pick pk[48];
    const size_t np = g_guardGameFences.load() ? stallguard::PickForUnstick(w, nw, sg, ns, pk, 48) : 0;
    unsigned done = 0;
    for (size_t i = 0; i < np; ++i) {
        ID3D12Fence* f = (ID3D12Fence*)pk[i].fence;
        const UINT64 was = SafeDone(f);
        if (was == ~0ull) continue;
        const bool ok = SafeSignal(f, pk[i].value);
        ++done;
        LogF("vrcam: stall guard - fence %p: completed %llu, signalled from the CPU to %llu (%s)", (void*)f, (unsigned long long)was, (unsigned long long)pk[i].value, ok ? "ok" : "failed");
    }
    for (const QFence& x : g_qf) {
        if (!x.f) continue;
        const UINT64 was = SafeDone(x.f);
        if (was == ~0ull || was >= x.v) continue;
        const bool ok = SafeSignal(x.f, x.v);
        ++done;
        LogF("vrcam: stall guard - our fence %p (queue %p): completed %llu, signalled from the CPU to %llu (%s)", (void*)x.f, (void*)x.q, (unsigned long long)was,
             (unsigned long long)x.v, ok ? "ok" : "failed");
    }
    LogF("vrcam: stall guard - the presents and the direct queue have stood still for %.1f s: %u fence(s) signalled from the CPU", stuckMs / 1000.0, done);
}
// Level 3, only when asked for ([debug] stall_remove_ms): the device taken away so that DRED tells where each queue stood.
void GuardRemove(double stuckMs) {
    ID3D12Device5* d5 = nullptr;
    if (!g_dev || FAILED(g_dev->QueryInterface(__uuidof(ID3D12Device5), (void**)&d5)) || !d5) return;
    LogF("vrcam: stall guard - %.1f s without a present and a moving queue: the device is removed on purpose ([debug] stall_remove_ms) - the report that follows says where each queue stood", stuckMs / 1000.0);
    d5->RemoveDevice();
    d5->Release();
}
// The watchdog thread, every half second while the presents stand. Looks at the queue with its own Progress (the draw thread's is another).
void GuardWatch(double presentsStoodMs) {
    if (!g_guardOn.load(std::memory_order_relaxed) || !g_direct) return;
    static stallguard::Progress prog;
    static double lastUnstick = -1e12;
    static bool removed = false;
    double qms = 0.0;
    for (const QFence& x : g_qf)
        if (x.q == g_direct && x.f) { qms = stallguard::Feed(prog, x.v, SafeDone(x.f), GuardMs()); break; }
    const double stuck = std::min(qms, presentsStoodMs);   // both: a draw thread that waits for the runtime with an idle GPU is not this
    const stallguard::Level lv = stallguard::Decide(stuck, g_guardLimits);
    if (lv == stallguard::Level::Remove && !removed) { removed = true; GuardRemove(stuck); return; }
    if (lv >= stallguard::Level::Unstick && GuardMs() - lastUnstick >= 5000.0) {
        lastUnstick = GuardMs();
        GuardUnstick(stuck);
    }
}

D3D12_RESOURCE_BARRIER Tr(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    return b;
}

// Native OpenXR (vr/xr.cpp) on the game's queue: the runtime's own signals pass our queue
// hooks as ours do.
bool XrBegin() {
    t_ours = true;
    const bool on = xr::BeginPair(g_dev, g_direct);
    t_ours = false;
    return on;
}
void XrCopy(int eye, ID3D12Resource* bb) { t_ours = true; xr::CopyEye(eye, bb); t_ours = false; }
void XrEnd() { t_ours = true; xr::EndPair(); t_ours = false; }
// Eyes by turns with the other eye made on the GPU (xr::TurnMake, [stereo] turn_warp): what D2R_DepthFog.fx draws without the scene's
// depth - the game's labels laid back, the wrist medallions, the bow's orbs, the hitboxes, the low-life vignette - is left out of the
// picture the warp moves (OverlaysApart, set at the effects' begin for this present) and drawn after it for each eye on its own picture
// (D2R_Overlay / D2R_Overlay_R on the eye drawn, D2R_OverlayMade / D2R_OverlayMade_R on the eye made): moved with the depth behind them
// they showed twice in the eye made (the author, 2026-10-20). Which depth hides them in an eye is its technique's, not a uniform: the
// eye made is the other one every frame, and ReShade writes the effect's uniforms over its one buffer at the next frame's effects -
// GPU-bound, before this frame's overlays have run - so flags per eye were read the next frame's way round and the eye made's
// medallions were hidden by the other eye's hand: doubled, always (the labels, which read no depth, not).
reshade::api::effect_technique g_ovTech[2][2]{};   // [made][eye], found with the effect's techniques (fx OnFinishEffects)
std::atomic<bool> g_ovApart{false};             // OverlaysApart as this present's effects had it
thread_local bool t_overlay = false;            // inside our own overlay render (the effects' events, if they come, are not a present's)
bool OverlayTechs() { return g_ovTech[0][0].handle && g_ovTech[0][1].handle && g_ovTech[1][0].handle && g_ovTech[1][1].handle; }
bool OverlaysWanted() { return NativeByTurns() && xr::TurnWarpOn() && OverlayTechs(); }   // the mode (any thread)
bool OverlaysNow() { return OverlaysWanted() && xr::TurnsNow(); }   // ... and this pair is made so (the draw thread, inside the pair)
// What the overlays by turns did, told every 10 s (TellOverlays), so a medallion seen twice says by which way it came in: drawn into
// the picture the warp moves (the effect did not leave them out this present: the warp then carries them into the eye made, with the
// depth behind them), the eye the effects took as drawn not the present's (the eye made then hidden by the wrong depth), or drawn from
// a camera off the one its picture shows (checked against the warp's cameras, fx: the overlays' cameras). The draw thread (the present
// and its effects' events are one thread).
OverlayTell g_ovTell;
int g_ovDrawnEye = -1;   // the eye the effects of this present took as drawn (OnBeginEffects)
const char* OverlaysWhyNot() {
    if (!OverlayTechs()) return "the effect has no D2R_Overlay(_R) / D2R_OverlayMade(_R) - an older D2R_DepthFog.fx";
    if (!NativeByTurns()) return "not native OpenXR by turns";
    if (!xr::TurnWarpOn()) return "[stereo] turn_warp off";
    return "the pair was not made by turns when the effects began";
}
void TellOverlays(float upm, float gapCm) {
    OverlayTell& t = g_ovTell;
    const ULONGLONG now = GetTickCount64();
    if (!t.since) t.since = now;
    if (now - t.since < 10000) return;
    const float cm = upm > 0.0f ? 100.0f / upm : 0.0f;
    char camLine[256] = "";
    if (t.cams)
        snprintf(camLine, sizeof camLine, "; their cameras off the pictures' by up to %.1f / %.1f cm and %.3f / %.3f rad (left / right; 0 = on the picture, "
                 "the eyes' gap %.1f cm = the other eye's place, shown twice)%s", t.gapAt[0] * cm, t.gapAt[1] * cm, t.gapTurn[0], t.gapTurn[1], gapCm,
                 t.camFallback ? " - an eye's view not there, the left one's taken" : "");
    LogF("vrcam: eyes by turns - overlays, 10 s: %u presents; the effect left them out of the picture the warp moves %u, drew them into it %u%s%s; "
         "drawn on the eye drawn %u, on the eye made %u of %u made%s; nothing to draw (no HUD shift, toolbar, labels, orbs, lines or vignette on: "
         "both skipped) %u; the eye the effects took as drawn not the present's %u%s",
         t.presents, t.apart, t.inPicture, t.inPicture ? " - " : "", t.inPicture ? t.why : "", t.drawn, t.madeDrawn, t.made,
         t.madeSkipped ? " (none there: [stereo] turn_overlays_made=0)" : "", t.empty, t.eyeOff, camLine);
    t = OverlayTell{};
    t.since = now;
}
// [render] no_reshade with native_effects: the plugin's own runtime (fx::noreshade, set at its Install) - the overlays drawn apart by it.
fxnative::Runtime* g_nativeFx = nullptr;
// Whether the overlay techniques have anything to draw this present (vr/overlay_work.h): the effect's own switches their passes branch on,
// read back as OnBeginEffects left them. With all off neither eye's is rendered. RT: ReShade's runtime or the plugin's own.
template <class RT> bool OverlaysHaveWork(RT* rt) {
    return overlaywork::Any(
        [rt](const char* n, bool* v) {
            const reshade::api::effect_uniform_variable u = rt->find_uniform_variable("D2R_DepthFog.fx", n);
            if (u.handle) rt->get_uniform_value_bool(u, v, 1);
            return u.handle != 0;
        },
        [rt](const char* n, float* v) {
            const reshade::api::effect_uniform_variable u = rt->find_uniform_variable("D2R_DepthFog.fx", n);
            if (u.handle) rt->get_uniform_value_float(u, v, 1);
            return u.handle != 0;
        });
}
// `eye`'s overlays over `target`: the back buffer (PRESENT, the eye drawn) or our picture of the other eye (RENDER_TARGET, made), viewed as `fmt`.
bool DrawOverlays(reshade::api::effect_runtime* rt, ID3D12Resource* target, int eye, DXGI_FORMAT fmt, bool backBuffer) {
    using namespace reshade::api;
    const effect_technique tech = g_ovTech[backBuffer ? 0 : 1][eye & 1];
    if (!rt || !target || !tech.handle) return false;
    device* dev = rt->get_device();
    const resource res{(uint64_t)target};
    resource_view rtv{0};   // made and let go each time: our picture may be made again at the same address
    if (!dev->create_resource_view(res, resource_usage::render_target, resource_view_desc((format)fmt), &rtv)) return false;
    command_queue* q = rt->get_command_queue();
    command_list* cl = q->get_immediate_command_list();
    if (backBuffer) cl->barrier(res, resource_usage::present, resource_usage::render_target);
    t_overlay = true;
    rt->render_technique(tech, cl, rtv, rtv);
    t_overlay = false;
    if (backBuffer) cl->barrier(res, resource_usage::render_target, resource_usage::present);
    q->flush_immediate_command_list();
    dev->destroy_resource_view(rtv);   // (its descriptor was copied into the list when it was recorded)
    return true;
}
// The same without ReShade: the plugin's own runtime renders the technique on our list on the game's queue (after the eye's copies before it).
bool DrawOverlaysNative(ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12Resource* target, int eye, bool backBuffer) {
    const reshade::api::effect_technique tech = g_ovTech[backBuffer ? 0 : 1][eye & 1];
    if (!g_nativeFx || !dev || !q || !target || !tech.handle) return false;
    ID3D12GraphicsCommandList* l = natived3d::OpenList(dev);
    if (!l) return false;
    const bool drew = g_nativeFx->RenderTechnique(tech, l, target, backBuffer);
    natived3d::Submit(q);
    return drew;
}

// rt: ReShade's runtime of this present, or nullptr - [render] no_reshade, the plugin's own present hook (no effects, no overlays).
void XrPresent(reshade::api::effect_runtime* rt, ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12Resource* bb) {
    if (SkipWhileStuck("the picture's copy to the headset (ReShade's flush waits on the queue)")) return;
    t_ours = true;
    // ReShade's effects of this present (the sky, the fog) are still in its immediate list here:
    // sent first, or our copy ran on the GPU before them - the headset without the sky while the
    // window had it, in the eyes by turns (2026-10-11). The replay flushes it in EffectsAtHold.
    if (rt) rt->get_command_queue()->flush_immediate_command_list();
    const int pairEye = g_pairNow.load() ? (g_eye.load() & 1) : -1;   // a pass of two: its eye
    // eyes by turns, the other eye made ([stereo] turn_warp): this eye kept as it is (no overlays yet), then its overlays, its swapchain,
    // the other eye made, its overlays, its swapchain (xr.h TurnKeep)
    const bool turns = pairEye >= 0 && xr::TurnsNow();
    const bool native = !rt && g_nativeFx;   // [render] no_reshade: the plugin's own runtime draws them
    const bool apart = turns && (rt || native) && g_ovApart.load();
    const bool work = apart && (rt ? OverlaysHaveWork(rt) : OverlaysHaveWork(g_nativeFx));   // (nothing on: both eyes' overlays skipped, counted in the 10 s line)
    const DXGI_FORMAT fmt = bb ? bb->GetDesc().Format : DXGI_FORMAT_UNKNOWN;
    if (turns) xr::TurnKeep(pairEye, bb);
    OverlayTell& tell = g_ovTell;
    if (turns && (rt || native)) {   // (no ReShade and no effects of its own: no overlays at all, nothing to tell)
        ++tell.presents;
        if (apart) ++tell.apart;
        else { ++tell.inPicture; tell.why = OverlaysWhyNot(); }
        if (apart && !work) ++tell.empty;
        if (g_ovDrawnEye != pairEye) ++tell.eyeOff;
    }
    if (work && (rt ? DrawOverlays(rt, bb, pairEye, fmt, true) : DrawOverlaysNative(dev, q, bb, pairEye, true))) ++tell.drawn;
    xr::OnPresent(dev, q, bb, pairEye);
    if (turns)
        if (ID3D12Resource* made = xr::TurnMake()) {
            ++tell.made;
            // [stereo] turn_overlays_made=0 (the author's idea, a test): none on the eye made - the eye drawn's alone
            if (apart && !xr::TurnOverlaysMade()) ++tell.madeSkipped;
            else if (work && (rt ? DrawOverlays(rt, made, pairEye ^ 1, fmt, false) : DrawOverlaysNative(dev, q, made, pairEye ^ 1, false))) ++tell.madeDrawn;
            xr::TurnFinish();
            monitoreye::Made(pairEye ^ 1, made);   // [render] monitor_eye: this frame's picture of that eye, for the window (MonitorPresent)
        }
    if (turns && (rt || native)) {
        const float upm = std::max(1e-3f, xr::TurnViewNow().upm);
        TellOverlays(upm, 2.0f * g_eyeHalf.load() * 100.0f / upm);
    }
    t_ours = false;
}

// [render] monitor_eye (vr/monitor_eye.h): the left eye's picture at the hold, kept for the window while the right eye's present follows (one pass,
// two pictures) - on the queue its effects and its copy to the headset went on.
void MonitorHeld(ID3D12Resource* bb) {
    if (!monitoreye::On() || !bb) return;
    ID3D12CommandQueue* q = g_rt ? (ID3D12CommandQueue*)g_rt->get_command_queue()->get_native() : g_direct;
    if (!q) return;
    const bool was = t_ours;
    t_ours = true;   // our submission and signal, not the game's
    monitoreye::KeepHeld(q, 0, bb);
    t_ours = was;
}
// Every present, after its copies to the headset (XrPresent): the eye this present carries in a stereo pair (g_pairNow: the passes of a pair,
// the replay's right eye, the eyes by turns) - the window gets the eye wanted instead of the other. ReShade's work of this present on the queue first.
void MonitorPresent(reshade::api::effect_runtime* rt, HWND wnd, ID3D12CommandQueue* q, ID3D12Resource* bb) {
    if (!monitoreye::On() || !q || !bb) return;
    if (SkipWhileStuck("the monitor's eye")) return;
    const int eye = g_pairNow.load() ? (g_eye.load() & 1) : -1;
    const bool was = t_ours;
    t_ours = true;
    if (rt) rt->get_command_queue()->flush_immediate_command_list();
    monitoreye::Present(q, (HWND__*)wnd, bb, eye);
    t_ours = was;
}

// Our own list on the direct queue: the next allocator of the ring, reset and opened.
ID3D12GraphicsCommandList* Open() {
    const int a = g_allocAt;
    g_allocAt = (g_allocAt + 1) % kAllocs;
    WaitForAlloc(g_allocDone[a]);
    g_alloc[a]->Reset();
    g_list->Reset(g_alloc[a], nullptr);
    g_openAlloc = a;
    return g_list;
}
void Submit(ID3D12CommandQueue* q) {
    g_list->Close();
    ID3D12CommandList* l = g_list;
    q->ExecuteCommandLists(1, &l);
    g_allocDone[g_openAlloc] = SignalOn(q);
}
// One eye's camera values into the slots, on the direct queue.
// The left values' copies this pass reached (slots made later - by lists recorded after the
// pass's first submission - had last pair's right values in the left eye: they get theirs
// before the next submission).
void CopySlots(int eye) {
    if (eye == 0) g_leftCopied = 0;   // a new left pass: none of its slots copied yet
    if (!g_direct) return;
    MakePred();
    if (!camfix::g_nSlots.load() && !g_predBuf) return;
    g_why = eye ? "slots: right" : "slots: left";
    const int n = camfix::g_nSlots.load();
    ID3D12GraphicsCommandList* l = Open();
    camfix::RecordSlots(l, eye, 0, n);
    RecordPred(l, eye);
    if (eye == 0) g_leftCopied = n;
    Submit(g_direct);
}

// Slots made since the left values went in: theirs now, on the direct queue; a compute
// submission waits for that copy.
void CopyLateSlots(ID3D12CommandQueue* nq) {
    if (!camfix::g_redirect.load() || !g_direct || camfix::g_nSlots.load() <= g_leftCopied) return;
    const int n = camfix::g_nSlots.load();
    g_lateSlots.fetch_add((uint32_t)(n - g_leftCopied), std::memory_order_relaxed);
    g_why = "slots: left, late";
    camfix::RecordSlots(Open(), 0, g_leftCopied, n);
    g_leftCopied = n;
    Submit(g_direct);
    if (nq != g_direct) { g_why = "slots: left, late (compute waits)"; GpuWait(nq, g_allocDone[g_openAlloc]); }
}

// One copy on the direct queue, with our own list (a ring of allocators).
void Copy(ID3D12Resource* dst, D3D12_RESOURCE_STATES dstState, ID3D12Resource* src, D3D12_RESOURCE_STATES srcState) {
    g_why = "picture copy";
    const int a = g_allocAt;
    g_allocAt = (g_allocAt + 1) % kAllocs;
    WaitForAlloc(g_allocDone[a]);
    g_alloc[a]->Reset();
    g_list->Reset(g_alloc[a], nullptr);
    D3D12_RESOURCE_BARRIER b[2] = {Tr(src, srcState, D3D12_RESOURCE_STATE_COPY_SOURCE), Tr(dst, dstState, D3D12_RESOURCE_STATE_COPY_DEST)};
    g_list->ResourceBarrier(2, b);
    g_list->CopyResource(dst, src);
    D3D12_RESOURCE_BARRIER c[2] = {Tr(src, D3D12_RESOURCE_STATE_COPY_SOURCE, srcState), Tr(dst, D3D12_RESOURCE_STATE_COPY_DEST, dstState)};
    g_list->ResourceBarrier(2, c);
    g_list->Close();
    ID3D12CommandList* l = g_list;
    g_direct->ExecuteCommandLists(1, &l);
    g_allocDone[a] = SignalOn(g_direct);
}

// [debug] replay_peek_go: one pair's left picture (after its effects, at the hold) and right
// picture (after its effects, before the present) read back and compared - whether the right
// eye is a picture of its own (the headset showed "mono" with the replay).
std::atomic<int> g_peekWant{0};
int g_peekStage = 0;   // 0 idle, 1 this pair, 2 both copies queued
ID3D12Resource* g_peekBuf[2] = {};
D3D12_PLACED_SUBRESOURCE_FOOTPRINT g_peekFp{};
UINT g_peekW = 0, g_peekH = 0;
Pt g_peekDone{};
void Peek(int eye, ID3D12Resource* bb) {
    const D3D12_RESOURCE_DESC d = bb->GetDesc();
    UINT64 total = 0;
    g_dev->GetCopyableFootprints(&d, 0, 1, 0, &g_peekFp, nullptr, nullptr, &total);
    g_peekW = (UINT)d.Width; g_peekH = d.Height;
    if (!g_peekBuf[eye]) {
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = total; rd.Height = 1;
        rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(g_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                  __uuidof(ID3D12Resource), (void**)&g_peekBuf[eye]))) { g_peekStage = 0; return; }
    }
    g_why = "peek";
    const int a = g_allocAt;
    g_allocAt = (g_allocAt + 1) % kAllocs;
    WaitForAlloc(g_allocDone[a]);
    g_alloc[a]->Reset();
    g_list->Reset(g_alloc[a], nullptr);
    D3D12_RESOURCE_BARRIER b = Tr(bb, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    g_list->ResourceBarrier(1, &b);
    D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = g_peekBuf[eye]; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = g_peekFp;
    D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = bb; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = 0;
    g_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_BARRIER c = Tr(bb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    g_list->ResourceBarrier(1, &c);
    g_list->Close();
    ID3D12CommandList* l = g_list;
    g_direct->ExecuteCommandLists(1, &l);
    g_allocDone[a] = g_peekDone = SignalOn(g_direct);
}
void PeekReport() {
    for (int i = 0; i < 30 && g_peekDone.f && g_peekDone.f->GetCompletedValue() < g_peekDone.v; ++i) WaitFor(g_peekDone);
    const uint8_t* m[2] = {};
    D3D12_RANGE r{0, 0};
    for (int e = 0; e < 2; ++e) if (g_peekBuf[e]) g_peekBuf[e]->Map(0, nullptr, (void**)&m[e]);
    if (m[0] && m[1]) {
        const UINT pitch = g_peekFp.Footprint.RowPitch, w = g_peekW, h = g_peekH;
        const UINT bpp = std::max<UINT>(1, std::min<UINT>(pitch / std::max<UINT>(1, w), 8));
        uint64_t diff = 0, all = 0;
        for (UINT y = 0; y < h; ++y)
            for (UINT x = 0; x < w; ++x, ++all)
                if (memcmp(m[0] + (size_t)y * pitch + x * bpp, m[1] + (size_t)y * pitch + x * bpp, bpp) != 0) ++diff;
        // the right picture against the left moved sideways: the shift that fits best (4-byte pixels,
        // the green byte, the middle half of the rows)
        int best = 0; double bestErr = 1e300, err0 = 0.0;
        if (bpp == 4)
            for (int sft = -80; sft <= 80; ++sft) {
                double e = 0.0; uint64_t n = 0;
                for (UINT y = h / 4; y < h * 3 / 4; y += 4)
                    for (UINT x = 100; x + 100 < w; x += 2) {
                        const int a = m[0][(size_t)y * pitch + x * 4 + 1], b = m[1][(size_t)y * pitch + (x + sft) * 4 + 1];
                        e += std::abs(a - b); ++n;
                    }
                e /= std::max<uint64_t>(1, n);
                if (sft == 0) err0 = e;
                if (e < bestErr) { bestErr = e; best = sft; }
            }
        LogF("vrcam: replay peek - %ux%u (%u bytes a pixel): left and right differ in %.1f%% of pixels; best sideways fit %d px (mean |diff| %.2f, at 0 px %.2f)",
             w, h, bpp, 100.0 * diff / std::max<uint64_t>(1, all), best, bestErr, err0);
    } else {
        Log("vrcam: replay peek - could not map the read-back pictures");
    }
    for (int e = 0; e < 2; ++e) if (m[e]) g_peekBuf[e]->Unmap(0, &r);
}

// Prototype diagnostics: the device's health after each step, the first failure logged.
bool MakePred() {
    if (g_predBuf) return true;
    D3D12_HEAP_PROPERTIES hp{};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = 256; rd.Height = 1; rd.DepthOrArraySize = 1;
    rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    if (FAILED(g_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                              __uuidof(ID3D12Resource), (void**)&g_predSrc))) return false;
    uint64_t* v = nullptr;
    D3D12_RANGE none{0, 0};
    if (FAILED(g_predSrc->Map(0, &none, (void**)&v))) return false;
    v[0] = 1; v[1] = 0; v[2] = 0; v[3] = 1;
    g_predSrc->Unmap(0, nullptr);
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (FAILED(g_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                              __uuidof(ID3D12Resource), (void**)&g_predBuf))) return false;
    Log("vrcam: replay - DLSS predicate made (each eye its own DLSS instance in the replay)");
    return true;
}
void RecordPred(ID3D12GraphicsCommandList* l, int eye) {
    if (!g_predBuf) return;
    if (g_predReady) { D3D12_RESOURCE_BARRIER b = Tr(g_predBuf, D3D12_RESOURCE_STATE_PREDICATION, D3D12_RESOURCE_STATE_COPY_DEST); l->ResourceBarrier(1, &b); }
    l->CopyBufferRegion(g_predBuf, 0, g_predSrc, eye ? 16 : 0, 16);
    D3D12_RESOURCE_BARRIER c = Tr(g_predBuf, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PREDICATION);
    l->ResourceBarrier(1, &c);
    g_predReady = true;
}
// The left pass being recorded for a replay, with the predicate in place: both evaluations.
bool DlssBoth() { return g_dlssBoth.load() && g_on.load() && g_want.load() == 1 && g_capturing.load() == 1 && g_predReady && camfix::g_redirect.load(); }

void Check(const char* step, HRESULT hr = S_OK) {
    static bool told = false;
    if (told || !g_dev) return;
    const HRESULT dr = g_dev->GetDeviceRemovedReason();
    if (dr != S_OK || FAILED(hr)) {
        told = true;
        LogF("vrcam: replay - after %s: hr %08lX, device removed reason %08lX", step, (unsigned long)hr, (unsigned long)dr);
    }
}

ID3D12Resource* BackBuffer(UINT i) {
    ID3D12Resource* r = nullptr;
    g_native->GetBuffer(i, __uuidof(ID3D12Resource), (void**)&r);
    return r;
}

// The left eye's effects on its back buffer now (the depth is still its own); right: the
// right eye's technique alone (see g_rightTech).
bool EffectsAtHold(ID3D12Resource* bb, UINT index, bool right = false, bool fence = false) {
    using namespace reshade::api;
    if (!g_rt || index >= 8) return false;
    if (SkipWhileStuck("an eye's ReShade effects")) return false;
    device* dev = g_rt->get_device();
    const resource res{(uint64_t)bb};
    if (g_rtvRes[index] != res.handle) {
        if (g_rtv[index].handle) dev->destroy_resource_view(g_rtv[index]);
        g_rtv[index] = {0};
        const D3D12_RESOURCE_DESC d = bb->GetDesc();
        if (!dev->create_resource_view(res, resource_usage::render_target, resource_view_desc((format)d.Format), &g_rtv[index])) return false;
        g_rtvRes[index] = res.handle;
    }
    command_queue* q = g_rt->get_command_queue();
    command_list* cl = q->get_immediate_command_list();
    t_ours = true;   // ReShade's and FlatVR's own signals in here are not the game's: they pass
    cl->barrier(res, resource_usage::present, resource_usage::render_target);
    t_pairPass = true;
    if (right) g_rt->render_technique(g_rightTech, cl, g_rtv[index], g_rtv[index]);
    else g_rt->render_effects(cl, g_rtv[index], g_rtv[index]);
    t_pairPass = false;
    cl->barrier(res, resource_usage::render_target, resource_usage::present);
    q->flush_immediate_command_list();
    t_ours = false;
    if (fence && !right) {   // the right eye's effects rewrite the constants these read: they wait for this point
        g_why = "after the left eye's effects";
        g_leftFx = SignalOn((ID3D12CommandQueue*)q->get_native());
    }
    return true;
}
// The fences instead of ReShade's drain, this pair (see g_fxFence).
bool FxFence() { return g_fxFence.load() && viewgate::F4(); }

// The left pass's Present: its picture copied out, the Present held back (told the game it went).
bool HoldLeft() {
    if (!g_native) return false;
    ID3D12CommandQueue* direct = nullptr;
    AcquireSRWLockExclusive(&g_lock);
    for (const Sub& s : g_subs)
        if (s.q->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) { direct = s.q; break; }
    ReleaseSRWLockExclusive(&g_lock);
    if (!direct) return false;
    g_direct = direct;
    if (!Setup(direct)) return false;
    const UINT holdIndex = g_native->GetCurrentBackBufferIndex();
    g_pairIdx[g_pairIdxAt & 15] = {holdIndex, g_gameIdxNow, ~0u, ~0u, 0u, S_OK};
    ID3D12Resource* bb = BackBuffer(holdIndex);
    if (!bb) return false;
    if (g_fxAtHold.load() && g_rt) {   // one present a pair: the left eye goes out from this effect pass
        g_pairFence = FxFence();   // decided once a pair: both eyes the same way
        if ((g_fxSync.load() || g_pairFence) && g_lastReplay.f) { StepTimer st(kWaitLeft); WaitFor(g_lastReplay); }
        const bool ok = EffectsAtHold(bb, holdIndex, false, g_pairFence);
        if (!ok) g_pairFence = false;
        if (ok && g_peekStage == 1) Peek(0, bb);
        if (ok) XrCopy(0, bb);   // native OpenXR: the left eye, as FlatVR's add-on takes it from that effect pass
        if (ok) MonitorHeld(bb);   // [render] monitor_eye: kept for the window (the right eye's present follows)
        bb->Release();
        return ok;
    }
    const D3D12_RESOURCE_DESC d = bb->GetDesc();
    if (g_leftImg) {
        const D3D12_RESOURCE_DESC h = g_leftImg->GetDesc();
        if (h.Width != d.Width || h.Height != d.Height || h.Format != d.Format) { g_leftImg->Release(); g_leftImg = nullptr; }
    }
    if (!g_leftImg) {
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd = d;
        rd.Flags = D3D12_RESOURCE_FLAG_NONE;
        if (FAILED(g_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                  __uuidof(ID3D12Resource), (void**)&g_leftImg))) { bb->Release(); return false; }
        LogF("vrcam: replay - the left picture's texture made, %llux%u", (unsigned long long)d.Width, d.Height);
    }
    Check("before holding the left picture");
    Copy(g_leftImg, D3D12_RESOURCE_STATE_COMMON, bb, D3D12_RESOURCE_STATE_PRESENT);
    Check("copying the left picture out");
    XrCopy(0, bb);
    MonitorHeld(bb);   // [render] monitor_eye: kept for the window (the right eye's present comes first)
    if (ID3D12Resource* depth = g_depthCopy.load() ? (ID3D12Resource*)g_depthRes.load() : nullptr) {
        const D3D12_RESOURCE_DESC dd = depth->GetDesc();
        if (g_leftDepth) {
            const D3D12_RESOURCE_DESC h = g_leftDepth->GetDesc();
            if (h.Width != dd.Width || h.Height != dd.Height || h.Format != dd.Format) { g_leftDepth->Release(); g_leftDepth = nullptr; }
        }
        if (!g_leftDepth) {
            D3D12_HEAP_PROPERTIES hp{};
            hp.Type = D3D12_HEAP_TYPE_DEFAULT;
            if (SUCCEEDED(g_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &dd, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                         __uuidof(ID3D12Resource), (void**)&g_leftDepth)))
                LogF("vrcam: replay - the left depth's copy made, %llux%u format %d, state %X",
                     (unsigned long long)dd.Width, dd.Height, (int)dd.Format, g_depthState.load());
        }
        if (g_leftDepth) Copy(g_leftDepth, D3D12_RESOURCE_STATE_COMMON, depth, DepthState());
    }
    bb->Release();
    return true;
}

// The game's present (D2R 0x109F4C0: rcx its device, rdx a result, r8 its swap chain
// wrapper - [0] the IDXGISwapChain it presents through, +0x50 the sync interval).
constexpr uint64_t RVA_GAME_PRESENT = 0x109F4C0;
const uint8_t kSigGamePresent[16] = {0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x57,0x48,0x83,0xEC,0x40,0x4D};
using GamePresentFn = void* (*)(void*, void*, void*);
GamePresentFn OrigGamePresent = nullptr;
void* HookGamePresent(void* dev, void* result, void* wrap) {
    if (wrap) { __try { g_gameIdxNow = *(const uint32_t*)((const uint8_t*)wrap + 0x98); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    if (wrap) {
        __try {   // and the flags it presents with: tearing allowed (0x200) as the game decides it
            const uint8_t* w = (const uint8_t*)wrap;
            g_proxy = *(IDXGISwapChain**)wrap;
            g_presentFlags = *((const uint8_t*)dev + 0x80) && *(const uint32_t*)(w + 0x50) == 0 && !w[0x38] && w[0x39] ? 0x200u : 0u;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    double holdT0 = pairtime::UsNow();
    if (g_capturing.load() && !g_leftHeld && HoldLeft()) {
        g_stepUs[kHold] += pairtime::UsNow() - holdT0;
        g_leftHeld = true;
        g_wrap = (uint8_t*)wrap;
        *(uint16_t*)result = 0;                     // the game's own success: code 0, HRESULT 0
        *(uint32_t*)((uint8_t*)result + 4) = 0;
        return result;
    }
    return OrigGamePresent(dev, result, wrap);
}

// A watchdog for the prototype: no present for 3 s while replaying - the fences' state
// (signalled vs done) and where the draw thread waits, to the log, once.
void LogStack(DWORD tid) {
    HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!th) return;
    static uint8_t copy[32768];
    CONTEXT c{}; c.ContextFlags = CONTEXT_FULL;
    size_t copied = 0;
    if (SuspendThread(th) != (DWORD)-1) {
        if (GetThreadContext(th, &c) && c.Rsp) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery((void*)c.Rsp, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT) {
                copied = (size_t)std::min<DWORD64>(sizeof copy, (DWORD64)mbi.BaseAddress + mbi.RegionSize - c.Rsp);
                memcpy(copy, (void*)c.Rsp, copied);
            }
        }
        ResumeThread(th);
    }
    CloseHandle(th);
    if (!copied) return;
    DWORD64 pcs[24];
    const int n = drawprof::UnwindCopy(c, copy, copied, pcs);
    for (int i = 0; i < n; ++i) {
        DWORD64 base = 0;
        const std::string m = drawprof::ModuleOf(pcs[i], &base);
        LogF("vrcam: watchdog -   %s+0x%llX", m.c_str(), (unsigned long long)(base ? pcs[i] - base : pcs[i]));
    }
}
// Every thread's stack to d2rloader\logs\d2r_vr_hang.txt, written straight to the file
// (the log itself may be what is stuck) with the modules looked up from a list taken
// beforehand (GetModuleHandleEx takes the loader lock a stuck thread may hold).
struct Mod { uint64_t base, end; char name[64]; };
std::vector<Mod> g_mods;
void ListModules() {   // psapi reads the module list in place: no thread made, no loader lock
    g_mods.clear();
    HMODULE mods[1024];
    DWORD need = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &need)) return;
    for (DWORD i = 0; i < need / sizeof(HMODULE) && i < 1024; ++i) {
        MODULEINFO mi{};
        if (!K32GetModuleInformation(GetCurrentProcess(), mods[i], &mi, sizeof mi)) continue;
        Mod m{(uint64_t)mi.lpBaseOfDll, (uint64_t)mi.lpBaseOfDll + mi.SizeOfImage, {}};
        K32GetModuleBaseNameA(GetCurrentProcess(), mods[i], m.name, sizeof m.name);
        g_mods.push_back(m);
    }
}
void DumpAllStacks() {
    ListModules();
    wchar_t path[MAX_PATH];
    wcscpy_s(path, g_iniPath);
    if (wchar_t* slash = wcsrchr(path, L'\\')) *slash = 0;
    wcscat_s(path, L"\\..\\logs\\d2r_vr_hang.txt");
    FILE* f = _wfopen(path, L"w");
    if (!f) return;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te{sizeof te};
    const DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
    static uint8_t copy[65536];
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
        HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!th) continue;
        CONTEXT c{}; c.ContextFlags = CONTEXT_FULL;
        size_t copied = 0;
        if (SuspendThread(th) != (DWORD)-1) {
            if (GetThreadContext(th, &c) && c.Rsp) {
                MEMORY_BASIC_INFORMATION mbi{};
                if (VirtualQuery((void*)c.Rsp, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT) {
                    copied = (size_t)std::min<DWORD64>(sizeof copy, (DWORD64)mbi.BaseAddress + mbi.RegionSize - c.Rsp);
                    memcpy(copy, (void*)c.Rsp, copied);
                }
            }
            ResumeThread(th);
        }
        CloseHandle(th);
        fprintf(f, "thread %lu%s\n", te.th32ThreadID, te.th32ThreadID == pairtime::g_drawThread.load() ? " (draw)" : "");
        if (!copied) continue;
        DWORD64 pcs[24];
        const int n = drawprof::UnwindCopy(c, copy, copied, pcs);
        for (int i = 0; i < n; ++i) {
            const Mod* m = nullptr;
            for (const Mod& x : g_mods) if (pcs[i] >= x.base && pcs[i] < x.end) { m = &x; break; }
            fprintf(f, "    %s+0x%llX\n", m ? m->name : "?", (unsigned long long)(m ? pcs[i] - m->base : pcs[i]));
        }
    }
    CloseHandle(snap);
    fclose(f);
}
DWORD WINAPI Watchdog(void*) {
    bool told = false;
    Log("vrcam: replay - watchdog running (no present for 3 s while replaying: every thread's stack to d2r_vr_hang.txt)");
    // By the presents' count: g_presentAt, cleared at every pair's start, stayed 0 when the
    // game hung inside a pair with its left present held - no dump (2026-10-09, an area change).
    uint32_t seen = 0;
    double since = 0.0;
    for (;;) {
        Sleep(500);
        const uint32_t now = pairtime::g_presents.load();
        if (now != seen || !g_replays.load()) { seen = now; since = pairtime::UsNow(); told = false; continue; }
        if (pairtime::UsNow() - since < 3e6) continue;
        GuardWatch((pairtime::UsNow() - since) / 1000.0);   // [debug] stall_guard: the queue freed, or the device removed for the report
        if (told) continue;
        told = true;
        DumpAllStacks();   // first, to its own file: the log may be the stuck thing
        Log("vrcam: watchdog - no present for 3 s while replaying:");
        for (const QFence& x : g_qf)
            LogF("vrcam: watchdog -   queue %p (type %d): our fence signalled %llu, done %llu", (void*)x.q, (int)x.q->GetDesc().Type,
                 (unsigned long long)x.v, (unsigned long long)x.f->GetCompletedValue());
        if (g_native) {
            UINT last = 0; g_native->GetLastPresentCount(&last);
            DXGI_FRAME_STATISTICS fs{};
            const HRESULT hs = g_native->GetFrameStatistics(&fs);
            LogF("vrcam: watchdog -   DXGI: presents issued %u, frame statistics hr %08lX: present count %u, present refresh %u, sync refresh %u; back buffer now %u, the game's %u",
                 last, (unsigned long)hs, fs.PresentCount, fs.PresentRefreshCount, fs.SyncRefreshCount, g_native->GetCurrentBackBufferIndex(), g_gameIdxNow);
            for (uint32_t i = g_pairIdxAt - std::min<uint32_t>(g_pairIdxAt + 1, 16); i != g_pairIdxAt + 1; ++i) {
                const PairIdx& pi = g_pairIdx[i & 15];
                LogF("vrcam: watchdog -   pair: left drew in %u (game's %u), right %u, after %u, hr %08lX, presents %u",
                     pi.hold, pi.game, pi.right, pi.after, (unsigned long)pi.hr, pi.presents);
            }
        }
        {
            uint64_t vu = 0, vb = 0;
            if (gpufault::VideoMemory(&vu, &vb)) LogF("vrcam: watchdog -   video memory (this process, local segment): %llu of %llu MiB", (unsigned long long)(vu >> 20), (unsigned long long)(vb >> 20));
        }
        LogF("vrcam: watchdog -   last replay point %llu, two back %llu; slots %d", (unsigned long long)g_lastReplay.v,
             (unsigned long long)g_prevReplay.v, camfix::g_nSlots.load());
        for (int i = 256 - 96; i < 256; ++i) {
            const Hist& h = g_hist[(g_histAt + i) & 255];
            if (!h.q) continue;
            LogF("vrcam: watchdog -   %c q %p f %p %llu (%s)", h.op, (void*)h.q, (void*)h.f, (unsigned long long)h.v, h.why);
        }
        {   // the game's own, oldest first; a wait shows its fence's completed value now
            const uint32_t end = g_gameOpAt.load();
            const uint32_t n = std::min<uint32_t>(end, 160);
            Log("vrcam: watchdog -   the game's queue ops (s signal, w wait [done now], x submit), last 160:");
            for (uint32_t i = end - n; i != end; ++i) {
                const GameOp& o = g_gameOps[i & 1023];
                if (!o.q) continue;
                if (o.op == 'x') LogF("vrcam: watchdog -   x q %p (type %d) thread %lu", (void*)o.q, (int)o.q->GetDesc().Type, o.tid);
                else LogF("vrcam: watchdog -   %c q %p (type %d) f %p %llu [done %llu] thread %lu", o.op, (void*)o.q, (int)o.q->GetDesc().Type,
                          (void*)o.f, (unsigned long long)o.v, (unsigned long long)SafeDone(o.f), o.tid);
            }
        }
        LogF("vrcam: watchdog -   the draw thread %lu waits in:", pairtime::g_drawThread.load());
        LogStack(pairtime::g_drawThread.load());
    }
}

// On unless [debug] replay_proto=0: the hooks cost next to nothing while [stereo] replay_right is
// off, and the Settings' "Stereo" mode switches it on live.
void Register() {
    if (!IniB(L"debug", L"replay_proto", true)) return;
    g_guardOn.store(IniB(L"debug", L"stall_guard", true));   // [debug] stall_guard: the queue that stands for good (vr/stall_guard.h)
    g_guardLimits.skipMs = (double)GetPrivateProfileIntW(L"debug", L"stall_skip_ms", 250, g_iniPath);
    g_guardLimits.unstickMs = (double)GetPrivateProfileIntW(L"debug", L"stall_unstick_ms", 8000, g_iniPath);
    g_guardLimits.removeMs = (double)GetPrivateProfileIntW(L"debug", L"stall_remove_ms", 0, g_iniPath);
    if (g_guardLimits.removeMs <= 0.0 && IniB(L"debug", L"stall_remove", false)) g_guardLimits.removeMs = 20000.0;   // the tick: 20 s
    g_guardGameFences.store(IniB(L"debug", L"stall_unstick_game", false));
    if (HANDLE h = CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr)) CloseHandle(h);
    reshade::register_event<reshade::addon_event::execute_command_list>(&d2rvr::D3D12Only<&OnExecute>::Call);
    reshade::register_event<reshade::addon_event::present>(&d2rvr::D3D12Only<&OnPresent>::Call);
    reshade::register_event<reshade::addon_event::barrier>(&d2rvr::D3D12Only<&OnBarrier>::Call);
    reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(&d2rvr::D3D12Only<&OnBindDs>::Call);
    camfix::Register();
    g_on.store(true);
    Log("vrcam: replay prototype on - [stereo] replay_right=1 draws the right eye from the left eye's lists");
}

// The game's present is hooked when first wanted, in the world: at ReShade's start the
// game's code may not be decrypted yet.
// Only in a loaded area and after 2 s of it: a menu or a loading screen has nothing to gain,
// and the game frees and makes resources then (it hung at an area change, 2026-10-09).
// When the replay may run: vr/replay_pause.h (240 settled pairs in a loaded area). An area change the world flags do not show
// (2026-10-18, act2_arcane -> act2_palace_cells through a portal, 0.170): the area loaded inside one 421 ms game frame and InWorld
// never went false - no pause, the replay ran on through the first frames of the new area while the game freed and made
// resources each frame (its frame fences pass before our replay on the queue), and the GPU hung ~40 ms later (DEVICE_HUNG, the
// direct queue stopped at the next left pass's "Depth Pass"). Now the settle starts again also when the game sets a biome
// (HookSetBiome -> AreaChanged, which first lets the last replay finish on the GPU, before the game can free what it drew with)
// and after a game frame of over 150 ms between two pairs.
replaypause::Settle g_settle;
std::atomic<uint32_t> g_areaChanges{0};
std::atomic<uint32_t> g_areaWaits{0};
void AreaChanged() {
    g_areaChanges.fetch_add(1);
    // the last replay finished on the GPU: its own event (this may be another thread than the pairs'), 200 ms at most
    const Pt last = g_lastReplay;
    if (!last.f || last.f->GetCompletedValue() >= last.v) return;
    if (HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr)) {
        if (SUCCEEDED(last.f->SetEventOnCompletion(last.v, e))) WaitForSingleObject(e, 200);
        CloseHandle(e);
        g_areaWaits.fetch_add(1);
    }
}
bool Wanted() {
    if (!g_on.load() || !g_want.load()) return false;
    const bool wasSettled = g_settle.Settled();
    replaypause::Why stopped;
    const bool settled = g_settle.Step(g_areaChanges.load(), pairtime::UsNow(), g_inWorld.load() && d2rcam::InWorld(), &stopped);
    if (stopped == replaypause::Why::kNoWorld) Log("vrcam: replay - paused (no world: a menu or an area change)");
    else if (stopped == replaypause::Why::kArea) Log("vrcam: replay - paused (the game set a biome: an area loads) - two passes until it has settled");
    else if (stopped == replaypause::Why::kLongFrame) Log("vrcam: replay - paused (a game frame of over 150 ms: a load) - two passes until the area has settled");
    if (!settled) return false;
    if (!wasSettled) {
        uint32_t noWorld = 0, area = 0, longFrame = 0;
        g_settle.TakeRestarts(&noWorld, &area, &longFrame);
        LogF("vrcam: replay - on (the area settled; the settle started again meanwhile: no world %u, another biome %u, a frame over 150 ms %u; "
             "the same biome set again %u, not counted)", noWorld, area, longFrame, ::g_sameBiomeSets.exchange(0));
    }
    static int hooked = -1;
    if (hooked < 0) {
        hooked = d2rsig::Hook(RVA_GAME_PRESENT, kSigGamePresent, sizeof kSigGamePresent, (void*)&HookGamePresent, (void**)&OrigGamePresent) ? 1 : 0;
        LogF("vrcam: replay - the game's present %s", hooked ? "hooked" : "NOT where expected - no replay");
    }
    return hooked == 1 && g_proxy && g_native;   // both known after a present through the hook
}

// Before a pair's left pass. The previous replay must be done on the GPU first: the game
// resets the left eye's command allocators once its own frame fence passes, which comes
// before our replay of them.
void BeginLeft() {
    // the replay two pairs back: the game's allocators of that left pass come round again
    // (3 back buffers, 2 presents a pair); the last one may still run while this pass records
    {
        StepTimer st(kWaitPrev);
        if (g_fence) WaitFor(g_ahead.load() >= 2 ? g_prevReplay : g_lastReplay);
        // The skipped right pass's wait on the swap chain's frame latency object: the game waits
        // once a pass, we present twice a pair - with one wait a pair the count drifted, DXGI
        // handed out the back buffer still on screen and the GPU waited for a newer present
        // queued behind it (the game hung at replay_ahead 2). Here, where the pass would wait.
        if (g_ahead.load() == 2 && g_latency && !(g_fxAtHold.load() && g_rt)) WaitForSingleObject(g_latency, 100);
    }
    if (g_peekStage == 2) { PeekReport(); g_peekStage = 0; }
    if (g_peekStage == 1) g_peekStage = 0;   // armed but the pair went out otherwise: again
    if (g_peekWant.exchange(0)) g_peekStage = 1;
    g_prevReplay = g_lastReplay;
    g_restorePending.store(true);
    AcquireSRWLockExclusive(&g_lock);
    g_subs.clear();
    ReleaseSRWLockExclusive(&g_lock);
    g_leftHeld = false;
    AcquireSRWLockExclusive(&g_dsLock);
    for (DsSeen& d : g_ds) d.binds = 0;   // this pair's binds; a buffer's last barrier state is kept
    ReleaseSRWLockExclusive(&g_dsLock);
    g_captureThread.store(GetCurrentThreadId());
    g_pairStartUs.store(pairtime::UsNow());
    g_capturing.store(1);
    if (g_sigHooked.load() && g_holdSignalsOn.load()) g_holdSignals.store(true);
    if (g_want.load() == 1 || g_want.load() == 4) camfix::BeginLeft(g_want.load() == 1);
}

// Our presents' results: a present that is not S_OK, or that leaves the back buffer index
// where it was (DXGI_STATUS_OCCLUDED: the window covered - it is dropped), told once a second.
void NotePresent(HRESULT hr, UINT before, UINT after) {
    static uint32_t n = 0, odd = 0, stuck = 0;
    static HRESULT lastOdd = S_OK;
    static ULONGLONG since = GetTickCount64();
    ++n;
    if (hr != S_OK) { ++odd; lastOdd = hr; }
    if (before == after) ++stuck;
    if (GetTickCount64() - since >= 1000) {
        if (odd || stuck) LogF("vrcam: replay - presents: %u, not S_OK %u (last %08lX), back buffer index unchanged %u", n, odd, (unsigned long)lastOdd, stuck);
        n = odd = stuck = 0; since = GetTickCount64();
    }
}

void SetGameBackBuffer(uint8_t* wrap, UINT index) {
    __try { *(uint32_t*)(wrap + 0x98) = index; } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// After the left pass, instead of the right one. False: the left eye went out as usual
// (its Present was not held), the caller draws the right eye the usual way.
bool RightFromLeft() {
    g_marks = g_marksOn.load(std::memory_order_relaxed);
    g_capturing.store(0);
    g_holdSignals.store(false);
    std::vector<Sub> subs;
    AcquireSRWLockExclusive(&g_lock);
    subs.swap(g_subs);
    ReleaseSRWLockExclusive(&g_lock);
    if (!g_leftHeld) { ReleaseHeld(nullptr); g_fails.fetch_add(1); return false; }
    g_leftHeld = false;
    ::CommitRightSky();   // the right eye's sky view and stamp: the left ones moved across, only now (vr/sky_view_eyes.h)
    const int mode = g_want.load();
    camfix::g_capture.store(false);
    if (camfix::g_scanWant.exchange(0)) camfix::ScanCopies();
    if (mode == 1) {   // the right eye's camera into the slots, on the direct queue before the replay
        StepTimer st(kPatch);
        CopySlots(1);
    }
    if (mode == 4) {   // the left pass done on the GPU (every queue it used), then its camera rewritten
        std::vector<ID3D12CommandQueue*> qs;
        for (const Sub& s : subs) if (std::find(qs.begin(), qs.end(), s.q) == qs.end()) qs.push_back(s.q);
        std::vector<Pt> pts;
        for (ID3D12CommandQueue* q : qs) pts.push_back(SignalOn(q));
        {
            StepTimer st(kWaitLeft);
            for (const Pt& pt : pts) WaitFor(pt);
        }
        StepTimer st(kPatch);
        camfix::RightEye();
    }
    double t = pairtime::UsNow();
    if (mode != 3) {   // the lists again, a batch per run of one queue; a fence between queues
        // (the first batch may be the compute queue's: it must see the right eye's slots too)
        if (mode == 1 && !subs.empty() && subs[0].q != g_direct) { g_why = "replay: first batch not direct"; GpuWait(subs[0].q, SignalOn(g_direct)); }
        ID3D12CommandQueue* prev = nullptr;
        size_t i = 0;
        if (g_marks >= replaygroups::kGame) {   // the game's calls need the hook on the queues the pass used (put in once for each; the first pair after the switch is numbered by none)
            std::vector<ID3D12CommandQueue*> seen;
            for (const Sub& s : subs)
                if (std::find(seen.begin(), seen.end(), s.q) == seen.end()) {
                    seen.push_back(s.q);
                    static std::vector<ID3D12CommandQueue*> hooked;
                    if (std::find(hooked.begin(), hooked.end(), s.q) == hooked.end()) {
                        hooked.push_back(s.q);
                        LogF("vrcam: replay - the game's ExecuteCommandLists on queue %p (type %d) watched %s (replay_marks %d: its calls are replayed as they were)", (void*)s.q,
                             (int)s.q->GetDesc().Type, eclhook::Install(s.q, &ObserveExec) ? "- hook in" : "- hook FAILED", g_marks);
                    }
                }
        }
        if (g_marks >= replaygroups::kGame)
            for (const Sub& s : subs)
                if (!s.call) ++g_eclUntagged;
        const double ecl0 = pairtime::UsNow();
        unsigned eclCalls = 0, eclSignals = 0;
        while (i < subs.size()) {
            ID3D12CommandQueue* q = subs[i].q;
            const size_t runStart = i;
            while (i < subs.size() && subs[i].q == q) ++i;
            if (prev && prev != q) { g_why = "replay: queue switch"; GpuWait(q, SignalOn(prev)); }
            for (const replaygroups::Batch& b : replaygroups::PlanRun(subs, runStart, i, g_marks)) {
                ID3D12CommandList* ls[128];
                for (size_t off = 0; off < b.count; off += 128) {   // (a call is never longer than the pass; the cap is for a pass that is)
                    const size_t c = std::min<size_t>(128, b.count - off);
                    for (size_t k = 0; k < c; ++k) ls[k] = subs[b.first + off + k].cl;
                    q->ExecuteCommandLists((UINT)c, ls);
                    ++eclCalls;
                }
                if (replaygroups::SignalAfterEach(g_marks)) {
                    g_why = kListMark[std::min<size_t>(b.first, 31)];   // (b.first: the batch's place in the pass)
                    SignalOn(q);
                    ++eclSignals;
                }
            }
            prev = q;
        }
        g_eclUs += pairtime::UsNow() - ecl0;
        g_eclLists += (uint32_t)subs.size(); g_eclCalls += eclCalls; g_eclSignals += eclSignals;
        if (prev && prev != g_direct) { g_why = "replay: back to direct"; GpuWait(g_direct, SignalOn(prev)); }
    }
    // One wait on the swap chain's frame latency object a present, as the game does before its
    // frame: it waits once a game frame, we present twice - unthrottled, the GPU ran into a back
    // buffer still queued behind presents waiting after it (the game hung, replay_ahead 2).
    if (!g_latencyAsked) {
        g_latencyAsked = true;
        DXGI_SWAP_CHAIN_DESC1 d{};
        IDXGISwapChain2* s2 = nullptr;
        if (SUCCEEDED(g_native->GetDesc1(&d)) && (d.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) &&
            SUCCEEDED(g_native->QueryInterface(__uuidof(IDXGISwapChain2), (void**)&s2))) {
            g_latency = s2->GetFrameLatencyWaitableObject();
            // Each wait let through only when every earlier present was done (latency 1): the CPU
            // waited for the GPU's replay. [stereo] replay_latency frames may queue instead.
            UINT was = 0;
            s2->GetMaximumFrameLatency(&was);
            const UINT want = (UINT)std::clamp((int)IniF(L"stereo", L"replay_latency", 3.0f), 1, 8);
            const bool change = g_ahead.load() >= 2 && !(g_fxAtHold.load() && g_rt) && want != was;
            if (change) s2->SetMaximumFrameLatency(want);
            LogF("vrcam: replay - frame latency %u -> %u", was, change ? want : was);
            s2->Release();
        }
        LogF("vrcam: replay - the swap chain %s a frame latency waitable object (flags %X, %u buffers)",
             g_latency ? "has" : "has NO", d.Flags, d.BufferCount);
    }
    // (only with [stereo] replay_ahead=2: that wait keeps presents to the monitor's refresh -
    // 65 pairs/s - so by default the CPU runs only one replay ahead and needs no throttle)
    // [stereo] replay_ahead 2: the presents kept bounded by the GPU instead - the last pair
    // done before this one's go out (the replay of this one runs on while the CPU records
    // the next left pass). 3: the latency object as above (follows the monitor).
    if (g_latency && g_ahead.load() >= 3) { StepTimer st(kWaitPrev); WaitForSingleObject(g_latency, 100); }
    {   // the game's end-of-frame signals, now behind the replay on their queues
        AcquireSRWLockShared(&g_heldLock);
        const uint32_t n = (uint32_t)g_held.size();
        ReleaseSRWLockShared(&g_heldLock);
        if (n > g_heldMax.load()) { g_heldMax.store(n); LogF("vrcam: replay - %u end-of-frame signal(s) of the game held for the replay", n); }
        static ULONGLONG told = 0;
        if (GetTickCount64() - told > 10000) {
            told = GetTickCount64();
            LogF("vrcam: replay - queue signals: %u in all, %u while holding, %u held; at this replay %u still held",
                 g_sigCalls.load(), g_sigWhileHolding.load(), g_sigHeld.load(), n);
        }
        ReleaseHeld(nullptr);
    }
    // the right eye: the back buffer as the lists left it (mode 3: the left picture again)
    const UINT bbRight = g_native->GetCurrentBackBufferIndex();
    Check("the replay");
    SetEye(1);
    // ReShade renders no effects at a present once an add-on has rendered them in that frame:
    // with the left eye's at the hold, the right eye's went without (no sky, and FlatVR's
    // add-on, which takes the picture from the effect pass, had only left eyes - its stream
    // stopped). The right eye's are rendered here too, on the replayed picture and depth.
    if (replaygroups::SignalAfterEach(g_marks)) { g_why = "before the right eye's effects"; SignalOn(g_direct); }
    if (g_fxAtHold.load() && g_rt && g_rightTech.handle)
        if (ID3D12Resource* bb = BackBuffer(bbRight)) {
            RightDepthIntoCopy();   // the effects' depth copy was still the left eye's
            if (g_pairFence && g_leftFx.f) {   // the left eye's effects read the constants these overwrite
                StepTimer st(kWaitLeft);
                WaitFor(g_leftFx);
                g_fxFenceWaits.fetch_add(1, std::memory_order_relaxed);
            }
            EffectsAtHold(bb, bbRight, true, g_pairFence);
            if (g_peekStage == 1) { Peek(1, bb); g_peekStage = 2; }
            bb->Release();
        }
    if (xr::On())   // native OpenXR: the right eye
        if (ID3D12Resource* bb = BackBuffer(bbRight)) { XrCopy(1, bb); bb->Release(); }
    if (replaygroups::SignalAfterEach(g_marks)) { g_why = "after the right eye's effects, before its present"; SignalOn(g_direct); }
    double t1 = pairtime::UsNow(); g_stepUs[kSubmit] += t1 - t; t = t1;
    const HRESULT hrR = g_proxy->Present(0, g_presentFlags);
    Check("the right eye's present", hrR);
    NotePresent(hrR, bbRight, g_native->GetCurrentBackBufferIndex());
    {
        PairIdx& pi = g_pairIdx[g_pairIdxAt++ & 15];
        pi.right = bbRight; pi.after = g_native->GetCurrentBackBufferIndex(); pi.hr = hrR;
        UINT c = 0; g_native->GetLastPresentCount(&c); pi.presents = c;
    }
    t1 = pairtime::UsNow(); g_stepUs[kPresentR] += t1 - t; t = t1;
    // the left eye: its copy into the next back buffer (not with its effects run at the hold:
    // it went out from that effect pass)
    const UINT bbLeft = g_native->GetCurrentBackBufferIndex();
    if (g_fxAtHold.load() && g_rt) {
        g_why = "end of pair";
        g_lastReplay = SignalOn(g_direct);
        if (g_wrap) SetGameBackBuffer(g_wrap, g_native->GetCurrentBackBufferIndex());
        if (g_replays.fetch_add(1) % 900 == 0)
        {
            LogF("vrcam: replay - a pair from one pass, one present: %zu lists again (mode %d), back buffer %u; %d camera slots (%u overflowed)",
                 subs.size(), mode, bbRight, camfix::g_nSlots.load(), camfix::g_overflow.load());
            const uint32_t pairs = std::max<uint32_t>(1, g_replays.load());
            LogF("vrcam: replay - slots made after the left values went in: %.2f a pair (copied late)", g_lateSlots.load() / (double)pairs);
            LogF("vrcam: replay - our depth bound %u times, %u passes kept the binding; [render] fx_fence %d: %u right-eye waits",
                 g_depthBinds.load(), g_depthKept.load(), g_fxFence.load() ? 1 : 0, g_fxFenceWaits.load());
            LogF("vrcam: replay - the game's lists a pair: %.1f in the left pass (replayed), %.1f outside it on the draw thread, %.1f from other threads (neither replayed)",
                 g_insideLists.load() / (double)pairs, g_outsideDraw.load() / (double)pairs, g_outsideOther.load() / (double)pairs);
            for (int i = 0; i < 8; ++i)
                if (g_outside[i].tid) LogF("vrcam: replay -   outside: thread %lu (draw thread %lu), queue type %d, %.2f ms after the left pass began",
                                           g_outside[i].tid, g_captureThread.load(), g_outside[i].type, g_outside[i].atUs / 1000.0);
            LogF("vrcam: replay - the right eye's camera, a pair: %.1f folded into the projection, %.1f views moved, %.1f positions; shift %.4f (eye half %.4f x2 x%.2f), off-axis %.4f, x scale %.4f",
                 g_nFold.load() / (double)pairs, g_nView.load() / (double)pairs, g_nPos.load() / (double)pairs,
                 2.0 * g_eyeHalf.load() * g_shiftMul.load(), g_eyeHalf.load(), g_shiftMul.load(), g_projShift.load(), g_projSx.load());
        }
        ++g_stepPairs;
        PairLine(true);
        return true;
    }
    if (ID3D12Resource* bb = BackBuffer(bbLeft)) {
        Copy(bb, D3D12_RESOURCE_STATE_PRESENT, g_leftImg, D3D12_RESOURCE_STATE_COMMON);
        bb->Release();
    }
    Check("copying the left picture in");
    if (ID3D12Resource* depth = g_depthCopy.load() ? (ID3D12Resource*)g_depthRes.load() : nullptr; depth && g_leftDepth)
        Copy(depth, DepthState(), g_leftDepth, D3D12_RESOURCE_STATE_COMMON);
    g_why = "end of pair";
    g_lastReplay = SignalOn(g_direct);
    SetEye(0);
    t1 = pairtime::UsNow(); g_stepUs[kCopyL] += t1 - t; t = t1;
    const HRESULT hrL = g_proxy->Present(0, g_presentFlags);
    Check("the left eye's present", hrL);
    NotePresent(hrL, bbLeft, g_native->GetCurrentBackBufferIndex());
    g_stepUs[kPresentL] += pairtime::UsNow() - t;
    ++g_stepPairs;
    PairLine(true);
    // the game asked for the back buffer index right after its (held) present, before our two
    // went out: it would draw the next frame into one already presented (device removed, 0x887A002B)
    if (g_wrap) SetGameBackBuffer(g_wrap, g_native->GetCurrentBackBufferIndex());
    if (g_replays.fetch_add(1) % 900 == 0)
        LogF("vrcam: replay - a pair from one pass: %zu lists again (mode %d), right in back buffer %u, left in %u; %d camera slots (%u overflowed)",
             subs.size(), mode, bbRight, bbLeft, camfix::g_nSlots.load(), camfix::g_overflow.load());
    return true;
}
}  // namespace replay

}  // namespace vrc
