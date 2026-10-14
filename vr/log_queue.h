#pragma once
// log_queue.h - [debug] log_async: the plugin's log lines written from a thread of its own, so the thread that logs (the game's, in the middle of
// a frame) pays for a copy into a ring and not for the host's file write. The queue and the pump are pure (no game, no host); the host's
// LogInfo is the pump's sink (vr/log_async.h).
//
// Why: in a swing every press writes about seven lines from the pad poll alone (pad: buttons, input: pad A down/up, pad A only attacks, ...), the
// first 400 of them "input:" lines, the "pad:" lines without a limit - 20 to 52 lines in a second with five presses against 9 in an average
// second - and on the log of the machine this was made on a line costs about a millisecond (the lines of one block of LogF calls, with nothing
// between them, are 1 ms apart): seven milliseconds of a frame at each press, more where the runtime's file is slow (an antivirus on the log,
// a disk that waits). None of it changes what the game does; it is the cost of telling.
//
// What it keeps: every line, in the order it was posted (one FIFO, one writer); a line that finds the ring full is written by its own thread, as
// before (so nothing is lost and the memory is bounded); a line that waited more than kLateMs says so at its end (the host stamps the time it
// WRITES the line); stopping drains what waits.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace logqueue {

constexpr size_t kRingBytes = 1u << 20;     // 1 MB of lines waiting at most
constexpr size_t kMaxText = 3000;           // a longer line is cut (the longest the plugin writes is ~1.5 KB)
constexpr int kPollMs = 4;                  // the writer looks every 4 ms
constexpr int kStopWaitMs = 2000;            // Stop waits this long for a writer stuck in the sink
constexpr double kLateMs = 50.0;            // a line written this long after it was posted says so

inline int64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Lines in a ring of bytes: [int64 posted ns][uint32 length][text...] one after the other, wrapping at the end.
class Queue {
public:
    explicit Queue(size_t bytes = kRingBytes) : buf_(bytes) {}

    // false: no room (the caller writes the line itself).
    bool Push(const char* text, int64_t nowNs) {
        if (!text) return false;
        size_t n = std::strlen(text);
        if (n > kMaxText) n = kMaxText;
        const size_t rec = kHeader + n;
        std::lock_guard<std::mutex> lock(m_);
        if (rec > buf_.size() || used_ + rec > buf_.size()) return false;
        size_t at = (head_ + used_) % buf_.size();
        const uint32_t len = (uint32_t)n;
        Put(&at, &nowNs, sizeof nowNs);
        Put(&at, &len, sizeof len);
        Put(&at, text, n);
        used_ += rec;
        return true;
    }
    // The oldest line; false with none.
    // `wait` false: a queue whose lock is held (by the thread that crashed in Push, say) is left alone - false.
    bool Pop(int64_t* postedNs, std::string* text, bool wait = true) {
        std::unique_lock<std::mutex> lock(m_, std::defer_lock);
        if (wait) lock.lock();
        else if (!lock.try_lock()) return false;
        if (!used_) return false;
        size_t at = head_;
        uint32_t len = 0;
        Get(&at, postedNs, sizeof *postedNs);
        Get(&at, &len, sizeof len);
        text->resize(len);
        if (len) Get(&at, &(*text)[0], len);
        head_ = at;
        used_ -= kHeader + len;
        return true;
    }
    bool Empty() const {
        std::lock_guard<std::mutex> lock(m_);
        return used_ == 0;
    }
    size_t Bytes() const {
        std::lock_guard<std::mutex> lock(m_);
        return used_;
    }

private:
    static constexpr size_t kHeader = sizeof(int64_t) + sizeof(uint32_t);
    void Put(size_t* at, const void* src, size_t n) {
        const char* s = static_cast<const char*>(src);
        const size_t first = std::min(n, buf_.size() - *at);
        std::memcpy(&buf_[*at], s, first);
        if (n > first) std::memcpy(&buf_[0], s + first, n - first);
        *at = (*at + n) % buf_.size();
    }
    void Get(size_t* at, void* dst, size_t n) {
        char* d = static_cast<char*>(dst);
        const size_t first = std::min(n, buf_.size() - *at);
        std::memcpy(d, &buf_[*at], first);
        if (n > first) std::memcpy(d + first, &buf_[0], n - first);
        *at = (*at + n) % buf_.size();
    }

    mutable std::mutex m_;
    std::vector<char> buf_;
    size_t head_ = 0, used_ = 0;
};

// "<text> [logged N ms after it was posted]" for a line that waited kLateMs or more; else the text itself.
inline std::string Dated(const std::string& text, int64_t postedNs, int64_t writtenNs) {
    const double ms = (double)(writtenNs - postedNs) * 1e-6;
    if (!(ms >= kLateMs)) return text;
    char tail[64];
    std::snprintf(tail, sizeof tail, " [logged %.0f ms after it was posted]", ms);
    return text + tail;
}

// The queue and the thread that empties it into a sink. Post() is any thread; the sink is called from the pump's thread (or from Drain's).
class Pump {
public:
    using Sink = void (*)(void* user, const char* text);

    Pump() : q_(new Queue()) {}
    ~Pump() { Stop(); delete q_; }
    Pump(const Pump&) = delete;
    Pump& operator=(const Pump&) = delete;

    // The thread begins (once); lines posted before are kept, none are lost.
    bool Start(Sink sink, void* user) {
        std::lock_guard<std::mutex> lock(life_);
        if (running_.load()) return true;
        sink_ = sink;
        user_ = user;
        stop_.store(false);
        done_.store(false);
        try { th_ = std::thread([this] { Run(); }); } catch (...) { return false; }
        running_.store(true);
        return true;
    }
    // Everything that waits is written, the thread joined. A sink that hangs (the host's file) is waited for 2 s, then the thread is left and the lines
    // that wait are not written: an unload must not hang on the log.
    void Stop() {
        std::lock_guard<std::mutex> lock(life_);
        if (!running_.load()) return;
        stop_.store(true);
        for (const int64_t until = NowNs() + kStopWaitMs * 1000000LL; !done_.load() && NowNs() < until;) std::this_thread::sleep_for(std::chrono::milliseconds(1));   // (by the clock: a sleep of 1 ms may be 15)
        const bool finished = done_.load();
        if (th_.joinable()) { if (finished) th_.join(); else th_.detach(); }
        running_.store(false);
        if (finished) DrainLocked();
    }
    bool Running() const { return running_.load(); }

    // false: the pump is not running, or the ring is full - the caller writes the line itself.
    bool Post(const char* text) {
        if (!running_.load(std::memory_order_acquire) || stop_.load(std::memory_order_relaxed)) return false;
        return q_->Push(text, NowNs());
    }

    // Writes what waits on the caller's thread, in order, behind the writer's batch in progress. For a crash: the lines before it should be in
    // the log. tryMs: how long to wait for that batch (the host's file may be what is stuck).
    bool Drain(int tryMs) {
        const int64_t until = NowNs() + (int64_t)tryMs * 1000000;
        while (!sinkLock_.try_lock()) {
            if (NowNs() > until) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::lock_guard<std::mutex> own(sinkLock_, std::adopt_lock);
        WriteAll(false);   // (a queue whose own lock is held is left alone: the thread that crashed may be inside it)
        return true;
    }

private:
    void Run() {
        while (!stop_.load()) {
            {
                std::lock_guard<std::mutex> lock(sinkLock_);
                WriteAll();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        }
        done_.store(true);
    }
    void DrainLocked() {
        std::lock_guard<std::mutex> lock(sinkLock_);
        WriteAll();
    }
    void WriteAll(bool wait = true) {   // (sinkLock_ held)
        int64_t posted = 0;
        std::string text;
        while (q_->Pop(&posted, &text, wait)) {
            const std::string line = Dated(text, posted, NowNs());
            if (sink_) sink_(user_, line.c_str());
        }
    }

    Queue* q_;
    Sink sink_ = nullptr;
    void* user_ = nullptr;
    std::mutex life_, sinkLock_;
    std::thread th_;
    std::atomic<bool> running_{false}, stop_{false}, done_{false};
};

}   // namespace logqueue
