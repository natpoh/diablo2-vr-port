// Stab: see stab.h. Only the seam's registration is here; the stab itself is the weight-physics library's (vr/weight_lib.cpp).

#include "stab.h"

#include <atomic>

namespace heft::stab {

namespace {
std::atomic<const Engine*> g_engine { nullptr };
}   // namespace

void SetEngine(const Engine* e) { g_engine.store(e); }
const Engine* GetEngine() { return g_engine.load(); }

}   // namespace heft::stab
