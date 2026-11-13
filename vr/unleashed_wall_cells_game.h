// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed commit b0f8eb1dfab71ec1cbc7e9535d24d72f11be5d6b ("Weapons hit walls"), its vr/wall_cells.h, for [weight] wall_contact_mode 2 (Bear's).
// The rest of D2R VR Authentic is MIT; this file stays MPL 2.0.
//
// The game's wall cells around the hero (vr/unleashed_wall_cells.cpp). The game's room and collision calls run only on the game thread (single
// player or the host: a joined game has no server here); other threads see copies.
#pragma once

#include <cstdint>

#include "unleashed_wall_cells.h"

namespace D2RL {
struct PluginContext;
}

namespace unleashedwalls {

void Init(const D2RL::PluginContext* ctx);
// Any thread: the cells wanted around this cell (the hero's) for this session (gamestate::LocalPlayer). A sample is queued when the hero moved
// several cells from the last one or it has aged (SampleDue).
void Want(uint64_t session, Cell centre, uint64_t now);
bool Latest(Patch& out);   // the newest sample, if any
void Forget();             // off: the last sample dropped

// For the ten-second log line, since the last call.
struct SampleStats {
    unsigned samples = 0, refused = 0, blocked = 0;
    int state = 0;          // 0 not asked yet, 1 sampling, 2 the game's functions not found, 3 no server here (a joined game)
    bool faulted = false;
    double us = 0.0;        // on the game thread
};
SampleStats TakeStats();

}  // namespace unleashedwalls
