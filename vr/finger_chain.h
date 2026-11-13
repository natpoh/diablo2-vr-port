// finger_chain.h - where a finger's curl starts on its chain of bones. Pure: no game, no skeleton; skeletons.cpp asks it once per chain when it learns a hero's rig, and
// cleanroom/tests/fist_knuckle_test.cpp checks it against the shapes of the game's eight hero rigs.
//
// What was wrong (Serg, 0.215.13, the Amazon's free left hand): half closed, her fingers folded out of the MIDDLE OF THE PALM - the knuckles twisted, the four fingers leaning
// into each other, the thumb lying across the index. [hands] fist (Fist, skeletons.cpp) bends each finger at three joints from the chain's first bending bone out, and took that
// bone to be the chain's first one unless its name says "metacarpal". The game's hero rigs are of three makes (read off data/hd/character/player/<class>/skeleton):
//   - Barbarian, Sorceress, Necromancer, Warlock (R_finger_A_A ...): three bones a finger - the knuckle, the middle joint, the last joint;
//   - Assassin (hand_index_01_R_JNT ...): three bones for the index and the middle finger, FOUR for the ring and the little finger, the first of them named
//     hand_ring_metacarpal_R_JNT - a bone inside the palm, a third of the way from the wrist to the knuckle;
//   - Amazon, Paladin, Druid (R_ind_finger_01_bind_jnt ...): FOUR bones for every finger, and the first (_01) is that same bone inside the palm - halfway from the wrist to the
//     knuckle (_02), with no "metacarpal" in its name. The game's own animations say so too: in the Amazon's bow draw _01 does not turn and _02 .. _04 curl (vr/bow_hook.h).
// So on those three rigs the fist bent the palm bone as if it were the knuckle (80 degrees of a full fist), the knuckle as the middle joint, and the last joint not at all: the
// palm folded in half. The paladin's had been toned down by hand ([fist] paladin_close 65 - "the full fist rolled the paladin's fingers into his palm", 2026-10-06), the Amazon's
// the same way in the shipped settings; the rig was never asked.
//
// The rule, from the rig itself: a finger bends at three joints - the knuckle, the middle one, the last one. A finger chain of exactly four bones has one bone more than its
// joints: a bone in the palm before the knuckle, which the curl leaves alone (the Assassin's rig names it so, and has it in exactly its four-bone chains). A thumb's chain is three
// bones on every rig and bends from its first. A chain of any other length is taken at its name's word, as before.
#pragma once

namespace fingerchain {

constexpr int kFingerJoints = 3;   // the knuckle, the middle joint, the last joint

// The chain's bone the curl starts at (0 = its first bone).
//   bones            how many bones the chain has
//   thumb            the chain is a thumb's
//   namedMetacarpal  its first bone's name says "metacarpal"
//   fromKnuckle      [fist] from_knuckle: the rig's own count of bones is asked too; off = the name only (as it was up to 0.215)
inline int FirstBend(int bones, bool thumb, bool namedMetacarpal, bool fromKnuckle) {
    const int named = namedMetacarpal ? 1 : 0;
    if (!fromKnuckle || thumb) return named;
    return bones == kFingerJoints + 1 ? 1 : named;
}

}  // namespace fingerchain
