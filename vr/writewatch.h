// writewatch.h - who writes a given address: hardware data breakpoints for a few seconds.
#pragma once
#include <cstdint>

namespace ww {

// What the caller knows at a hit (on the writing thread): packed into 64 bits, written out as hex.
using TagFn = uint64_t (*)();
using LogFn = void (*)(const char*);

// Arms write breakpoints on addrA and addrB (4 bytes each, 4-aligned; 0 = unused) in every thread
// of the process for `ms`, then disarms and writes each hit (time, thread, tag, the writing code and
// its callers as module+offset) to outPath, and a summary per writing place to log. false if one runs.
bool Start(uintptr_t addrA, uintptr_t addrB, unsigned ms, const wchar_t* outPath, TagFn tag, LogFn log);

}  // namespace ww
