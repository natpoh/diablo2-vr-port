// memdiff - find a flag in the game's globals by flipping it (Ctrl+F9).
//
// Press Ctrl+F9, change ONE thing in the game (the map on/off, or its side
// with V), press Ctrl+F9 again, change it back, press again, and so on. Each
// press copies the writable sections of the game's own image (its globals:
// .data and the like - not the heap) and keeps only the bytes that changed on
// every press and went back and forth between the same two values. After 4-5
// presses a handful are left; they are logged with their RVAs (loader layout)
// and written in full to d2r_vr_memdiff.txt beside the ini. Ctrl+Shift+F9
// starts over. Read only: nothing in the game is written.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace memdiff {
namespace {

struct Range { uintptr_t at; size_t size; };
struct Cand { uint32_t off; uint8_t v1, v2; };

std::vector<Range> g_ranges;
std::vector<uint8_t> g_prev;
std::vector<Cand> g_cands;
int g_step = 0;   // presses so far

// The image's writable, non-code sections.
std::vector<Range> WritableSections(uintptr_t base) {
    std::vector<Range> out;
    const auto* dos = (const IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return out;
    const auto* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return out;
    const IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s) {
        const DWORD c = s->Characteristics;
        if (!(c & IMAGE_SCN_MEM_WRITE) || (c & IMAGE_SCN_MEM_EXECUTE)) continue;
        const size_t size = s->Misc.VirtualSize ? s->Misc.VirtualSize : s->SizeOfRawData;
        if (size) out.push_back({base + s->VirtualAddress, size});
    }
    return out;
}

bool Readable(const void* p) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT) return false;
    const DWORD pr = mi.Protect & 0xFF;
    return !(mi.Protect & PAGE_GUARD) && pr != PAGE_NOACCESS && pr != PAGE_EXECUTE;
}

// One page at a time, unreadable pages as zeros: one fault must not lose the rest.
void CopyPage(uint8_t* dst, const uint8_t* src, size_t n) {
    __try { memcpy(dst, src, n); } __except (EXCEPTION_EXECUTE_HANDLER) { memset(dst, 0, n); }
}

std::vector<uint8_t> Snapshot() {
    size_t total = 0;
    for (const Range& r : g_ranges) total += r.size;
    std::vector<uint8_t> out(total, 0);
    size_t o = 0;
    for (const Range& r : g_ranges) {
        for (size_t p = 0; p < r.size; p += 4096) {
            const size_t n = (r.size - p) < 4096 ? (r.size - p) : 4096;
            const uint8_t* src = (const uint8_t*)(r.at + p);
            if (Readable(src)) CopyPage(out.data() + o + p, src, n);
        }
        o += r.size;
    }
    return out;
}

// Offset in the snapshot -> RVA in the image.
uint64_t RvaOf(uint32_t off, uintptr_t base) {
    for (const Range& r : g_ranges) {
        if (off < r.size) return (uint64_t)(r.at + off - base);
        off -= (uint32_t)r.size;
    }
    return 0;
}

}  // namespace

void Reset(void (*log)(const char*)) {
    g_ranges.clear(); g_prev.clear(); g_cands.clear(); g_step = 0;
    log("memdiff: started over - Ctrl+F9, change one thing, Ctrl+F9, change it back, Ctrl+F9...");
}

void Step(uintptr_t base, const std::wstring& outPath, void (*log)(const char*)) {
    char line[256];
    if (g_step == 0) {
        g_ranges = WritableSections(base);
        size_t total = 0;
        for (const Range& r : g_ranges) total += r.size;
        if (g_ranges.empty() || total > (512u << 20)) {
            log("memdiff: no writable sections found in the game's image (or too big) - nothing to compare");
            return;
        }
        g_prev = Snapshot();
        g_step = 1;
        snprintf(line, sizeof line, "memdiff 1: %zu sections, %.1f MB copied - now change ONE thing (the map), then Ctrl+F9",
                 g_ranges.size(), total / 1048576.0);
        log(line);
        return;
    }
    const std::vector<uint8_t> cur = Snapshot();
    if (cur.size() != g_prev.size()) { log("memdiff: the image changed size - Ctrl+Shift+F9 to start over"); return; }
    ++g_step;
    if (g_step == 2) {
        g_cands.clear();
        for (uint32_t i = 0; i < (uint32_t)cur.size(); ++i)
            if (cur[i] != g_prev[i]) g_cands.push_back({i, g_prev[i], cur[i]});
    } else {
        // odd presses are back at the first state, even ones at the second
        const bool second = (g_step % 2) == 0;
        size_t keep = 0;
        for (const Cand& c : g_cands)
            if (cur[c.off] == (second ? c.v2 : c.v1)) g_cands[keep++] = c;
        g_cands.resize(keep);
    }
    g_prev = cur;
    snprintf(line, sizeof line, "memdiff %d: %zu bytes still flip with it - change it %s, then Ctrl+F9",
             g_step, g_cands.size(), (g_step % 2) == 0 ? "back" : "again");
    log(line);
    if (g_cands.size() <= 24) {
        for (const Cand& c : g_cands) {
            snprintf(line, sizeof line, "memdiff   RVA 0x%llX: %u <-> %u", (unsigned long long)RvaOf(c.off, base), c.v1, c.v2);
            log(line);
        }
    }
    if (g_cands.size() <= 5000) {
        FILE* f = nullptr;
        if (_wfopen_s(&f, outPath.c_str(), L"w") == 0 && f) {
            fprintf(f, "# memdiff after %d presses: RVA (loader layout), first state, second state\n", g_step);
            for (const Cand& c : g_cands)
                fprintf(f, "0x%llX %u %u\n", (unsigned long long)RvaOf(c.off, base), c.v1, c.v2);
            fclose(f);
        }
    }
}

}  // namespace memdiff
