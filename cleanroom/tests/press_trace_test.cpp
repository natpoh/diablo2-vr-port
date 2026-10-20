// [debug] melee_press_trace (vr/press_trace.h): one line per attack press of hit by touch - the pad the game polled, its slot reads, its target lookups, the hero's mode,
// the hit - and the first step that did not happen (the Amazon's touches that pressed A but never struck, the author 2026-10-20).
//
//   press_trace_test        exit code 0 = all passed

#include "press_trace.h"

#include <cstdio>
#include <cstring>

using namespace presstrace;

namespace {
int g_failed = 0, g_checks = 0;
void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failed; std::printf("FAIL: %s\n", what); }
}
bool Has(const char* s, const char* part) { return std::strstr(s, part) != nullptr; }

Facts Amazon() {
    Facts f;
    f.weaponSet = 2;
    f.weaponClass = 2;
    f.weaponType = 4;
    f.codeR = 's' | 's' << 8 | 'd' << 16 | ' ' << 24;
    f.slotA = 0;
    f.bank = 16;
    f.armedMelee = 0;
    return f;
}
}  // namespace

int main() {
    char line[2048];
    {   // a press that the game struck with: the pad, the slot, the lookups answered ours, the attack mode, the hit
        Recorder rec;
        Trace prev;
        Check(!rec.Begin(15, 0, 1000, Amazon(), 1, &prev), "the first press hands nothing back");
        rec.Poll(1004, 0, true, 1);                          // Stand Still held back A at first
        rec.Poll(1020, Recorder::kPadA, true, 1);
        rec.Slot(kSelect, 0, 0, 0);
        rec.Slot(kSetter, 0, 0, 0);
        rec.Lookup(kDispatch, 1021, 0, false);
        rec.Cast(0, false);
        rec.Lookup(kResolver, 1021, 15, true);
        rec.Lookup(kClick, 1022, 15, true);
        rec.Poll(1060, Recorder::kPadA, true, 7);            // the attack
        Check(rec.WantsAnim(), "an attack mode seen: the row is wanted");
        rec.Anim("AMA11HS", 16, 1);
        rec.Poll(1200, 0, false, 7);
        rec.Outcome(true, 92.4);
        Trace t;
        Check(!rec.Due(1300, &t), "not told before the hit has had its 400 ms");
        Check(rec.Due(1500, &t), "told once the hit is in and 400 ms on");
        Check(!rec.Active() && !rec.Due(1600, &t), "told once");
        Check(t.pollsA == 2 && t.aDownMs == 20 && t.aUpMs == 200 && t.stillAtA == 1, "A: two polls, down at 20 ms, up at 200, Stand Still down");
        Check(t.actMode == 7 && t.actMs == 60 && t.hitMs == 92, "the attack mode at 60 ms, the hit at 92");
        Check(t.ask[kResolver].ours == 1 && t.ask[kClick].lastId == 15 && t.ask[kDispatch].lastId == 0, "the lookups as given");
        Format(t, line, sizeof line);
        Check(Has(line, "the hero attacked and the hit came"), "the verdict: struck");
        Check(Has(line, "R 'ssd ' L '-'"), "the hands' items by code");
        Check(Has(line, "row AMA11HS 16 frames, the action frame 1"), "the animation row");
        Check(Has(line, "HIT 92 ms"), "the hit");
        Check(Has(line, "panel A 0, bank 16"), "the A slot and the bank");
        Check(Has(line, "select [0] 0->0 (x1)"), "the slot read");
    }
    {   // the press never reached the game
        Recorder rec;
        rec.Begin(15, 0, 0, Amazon(), 1, nullptr);
        rec.Poll(10, 0, true, 1);
        rec.Poll(30, 0, true, 1);
        rec.Outcome(false, 0.0);
        Trace t;
        Check(!rec.Due(1000, &t), "lost: told only at the end of the window");
        Check(rec.Due(1300, &t), "... at 1.3 s");
        Format(t, line, sizeof line);
        Check(Has(line, "A NEVER reached"), "the verdict: no A at the game");
        Check(Has(line, "LOST (no hit)") && Has(line, "Stand Still at A -"), "lost, no A");
    }
    {   // A reached the game, the slot answered a skill
        Recorder rec;
        rec.Begin(15, 0, 0, Amazon(), 1, nullptr);
        rec.Poll(20, Recorder::kPadA, false, 1);
        rec.Slot(kSelect, 0, 12, 12);
        Trace t;
        Check(rec.Due(1300, &t), "told");
        Format(t, line, sizeof line);
        Check(Has(line, "answered a SKILL") && Has(line, "Stand Still at A NOT down"), "the verdict: a skill on A");
    }
    {   // the lookups were asked but given another unit or none
        Recorder rec;
        rec.Begin(15, 0, 0, Amazon(), 1, nullptr);
        rec.Poll(20, Recorder::kPadA, true, 1);
        rec.Slot(kSelect, 0, 0, 0);
        rec.Cast(0, false);
        rec.Lookup(kResolver, 21, 0, false);
        rec.Lookup(kClick, 22, 0, false);
        Trace t;
        rec.Due(2000, &t);
        Format(t, line, sizeof line);
        Check(Has(line, "NOT given the touched monster"), "the verdict: the target not ours");
        Check(Has(line, "resolver x1 at 21 ms (ours 0, last none)"), "the resolver's ask");
    }
    {   // directed cast, and no attack mode
        Recorder rec;
        rec.Begin(15, 0, 0, Amazon(), 1, nullptr);
        rec.Poll(20, Recorder::kPadA, true, 1);
        rec.Slot(kSelect, 0, 0, 0);
        rec.Cast(0, true);
        Trace t;
        rec.Due(2000, &t);
        Format(t, line, sizeof line);
        Check(Has(line, "ALONG THE HAND"), "the verdict: directed");
        Recorder rec2;
        rec2.Begin(15, 0, 0, Amazon(), 1, nullptr);
        rec2.Poll(20, Recorder::kPadA, true, 1);
        rec2.Slot(kSelect, 0, 0, 0);
        rec2.Cast(0, false);
        rec2.Lookup(kClick, 22, 15, true);
        rec2.Due(2000, &t);
        Format(t, line, sizeof line);
        Check(Has(line, "never went into an attack mode"), "the verdict: no attack mode");
    }
    {   // a new press before the old one was told: the old one is handed back, cut short; outside a press nothing is noted
        Recorder rec;
        rec.Poll(5, Recorder::kPadA, true, 7);
        rec.Slot(kSelect, 0, 0, 0);
        Check(!rec.Active(), "nothing noted without a press");
        rec.Begin(15, 0, 0, Amazon(), 1, nullptr);
        rec.Poll(20, Recorder::kPadA, true, 1);
        Trace prev;
        Check(rec.Begin(16, 1, 500, Amazon(), 1, &prev), "the next press hands the old one back");
        Check(prev.cut && prev.monster == 15 && prev.pollsA == 1, "... cut short, as it was");
        Check(rec.Current().monster == 16 && rec.Current().pollsA == 0 && rec.Current().seq == 2, "the new one starts clean");
        Format(prev, line, sizeof line);
        Check(Has(line, "cut short"), "the old line says it was cut short");
    }
    Check(Recorder::IsAttackMode(7) && Recorder::IsAttackMode(8) && Recorder::IsAttackMode(10) && !Recorder::IsAttackMode(12) && !Recorder::IsAttackMode(1) && !Recorder::IsAttackMode(0),
          "the attack modes");
    std::printf("press_trace_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
