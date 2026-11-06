#pragma once
// own_labels_paint.h - [hud] labels_own: one item label painted by us (vr/own_labels.h has why and the sizes). Our own drawing, through GDI:
// the game's box colour behind, the name in its colour with a dark edge round its letters, the pointed item's rim. Painted on the labels'
// worker (vr/own_labels_wire.cpp), never on a frame's thread.

#include <cstdint>
#include <vector>

#include "own_labels.h"

namespace ownlabels {

// One cell for `w`: premultiplied pixels 0xAABBGGRR (R in the low byte, as D2R_DepthFog.fx's OwnLabelTex takes them), w.heightPx tall and
// as wide as the name needs (at most the atlas's width). `face`: a font family for the name (the game's Formal436 when it is in and has
// every letter of it), nullptr: ours (the language's: uitext::Face). False: nothing to paint (no name) or GDI failed.
bool Paint(const Want& w, const wchar_t* face, std::vector<uint32_t>* px, int* outW, int* outH);

}  // namespace ownlabels
