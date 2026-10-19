// [hands] shield_stow_no_block: the hook on the game's block check (vr/stowed_block.h says what it does and does not change).
//
// 0x44B930 in 3.3.93787 (and 3.3.93847, same RVA): the server's (game, attacker, defender, avoid, block) -> 1 blocked. With block == 0 it rolls no block and goes
// straight on to the dodge check; otherwise it takes the defender's block chance, rolls it and returns 1 under it. Found by bearstonem/d2r-pcvr (MIT: vr/combat.cpp,
// "BlockRva"), which replaces the roll for its physical shield guard. Ours only passes block = 0 while the shield is put away: the game's own "cannot block" path.

#include "stowed_block.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>

#include "sigscan.h"
#include "shield_arm_wire.h"

namespace stowedblock {
namespace {

constexpr uint64_t RVA_BLOCK = 0x44B930;
const uint8_t kSigBlock[16] = {0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x7C,0x24,0x20,0x41,0x56,0x48,0x83,0xEC,0x30};
using BlockFn = int (*)(uintptr_t game, uintptr_t attacker, uintptr_t defender, int avoid, int block);
BlockFn OrigBlock = nullptr;

std::atomic<bool> g_on{false}, g_in{false};
std::atomic<uint32_t> g_refused{0};
LogFn g_log = nullptr;
void Log(const char* s) { if (g_log) g_log(s); }

// A unit's first dword is its type: 0 is a player. The game reads the defender itself right before; the guard is for a build that is not ours.
bool IsPlayerUnit(uintptr_t unit) noexcept {
    if (unit <= 0x10000) return false;
    uint32_t type = 1;
    __try {
        type = *reinterpret_cast<volatile uint32_t*>(unit);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return type == 0;
}

int HookBlock(uintptr_t game, uintptr_t attacker, uintptr_t defender, int avoid, int block) {
    if (Refuse(g_on.load(std::memory_order_relaxed), shieldarmwire::Stowed(), IsPlayerUnit(defender), block)) {
        if (g_refused.fetch_add(1, std::memory_order_relaxed) == 0)
            Log("vrcam: shield put away - the first block check refused ([hands] shield_stow_no_block): the game goes on to its dodge check as for a hero with no shield");
        block = 0;
    }
    return OrigBlock(game, attacker, defender, avoid, block);
}

}  // namespace

void SetLog(LogFn fn) { g_log = fn; }

void Configure(bool on) {
    if (g_on.exchange(on) != on && g_in.load())
        Log(on ? "vrcam: [hands] shield_stow_no_block on - a shield put away behind the left shoulder does not block"
               : "vrcam: [hands] shield_stow_no_block off - the game's block as ever, put away or not");
}

void Install() {
    static bool failed = false;
    static int tries = 0;
    if (g_in.load() || failed || !g_on.load()) return;
    if (!d2rsig::Check(RVA_BLOCK, kSigBlock, sizeof kSigBlock)) {
        if (++tries >= 120) {   // a minute: the page is long decrypted by then, so this build has other code there
            failed = true;
            Log("vrcam: [hands] shield_stow_no_block - the game's block check is not where 3.3.93787 has it (another build?): a shield put away still blocks");
        }
        return;
    }
    const bool in = d2rsig::Hook(RVA_BLOCK, kSigBlock, sizeof kSigBlock, (void*)&HookBlock, (void**)&OrigBlock);
    g_in.store(in);
    failed = !in;
    Log(in ? "vrcam: [hands] shield_stow_no_block - block check hook in: a shield put away behind the left shoulder does not block (the game's roll otherwise)"
           : "vrcam: [hands] shield_stow_no_block - block check hook FAILED to install: a shield put away still blocks");
}

}  // namespace stowedblock
