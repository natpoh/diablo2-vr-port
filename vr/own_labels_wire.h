#pragma once
// own_labels_wire.h - [hud] labels_own: the atlas of our item labels and the worker that paints it (vr/own_labels.h has why).
//
// The effect's frame (vr/render/effects/effects.cpp LabelDepthUniforms) asks for this frame's labels (Resolve): a label already painted gets
// its cell; one that is not is queued for the worker and gets none this frame (the effect then shows the game's own pixels for it). The
// worker paints the queue (GDI, vr/own_labels_paint.cpp) into the atlas off the frame's thread; the next Resolve hands the changed atlas to
// the effect's upload under the same lock as the cells, so a cell is never shown from an older picture. Off (the key unticked): nothing runs,
// no thread is made.

#include <cstdint>

#include "own_labels.h"

namespace ownlabels {

using LogFn = void (*)(const char*);
void SetLog(LogFn fn);

// [hud] labels_own, live (vr/config/load_settings.cpp).
void SetWanted(bool on);
bool Wanted();

// The atlas changed: its pixels (kAtlasW x kAtlasH, 0xAABBGGRR premultiplied) to the effect's texture. False: it could not take them (no
// such texture: an older effect) - then no cell is handed out.
using UploadFn = bool (*)(void* ctx, const uint32_t* px, int w, int h);

// This frame's labels: cells[i] for wants[i] (w 0: not painted yet - queued). `*rev` is the atlas the caller's texture holds (0 at first, or
// after its texture was made again); when the atlas is newer, `upload` gets it first. Returns how many labels got a cell.
int Resolve(const Want* wants, int n, Cell* cells, uint32_t* rev, UploadFn upload, void* ctx);

// The game's Formal436 for the names (taken from D2R VR Settings' game-art cache, vr/game_ui.h) or ours: the worker asks once.
void SetFaceSource(const wchar_t* (*fn)());
// ... and the game's Exocet, for a text whose style names it (Want::font kFontExocet).
void SetTitleFaceSource(const wchar_t* (*fn)());

// [debug] own_labels_dump: the atlas as it is now to a .bmp (32-bit, its alpha kept) - our names and labels as painted, to hold against the game's.
bool DumpAtlas(const wchar_t* path, int* err = nullptr);   // err: errno of the failed open/write, -1 the atlas empty

// Tests: wait until the worker has painted everything queued (true) or `ms` passed.
bool Drain(int ms);

}  // namespace ownlabels
