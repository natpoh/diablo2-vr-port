// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed commit 843b3a464ab1bc5b8dc36eff797ab80620b85914 ("Fog: eased in over its first 40 units, no hard ring round the
// hero"), its reshade/D2R_DepthFog.fx FogAt, moved into a file of its own that D2R_DepthFog.fx (MIT) includes. Stays MPL 2.0.
//
// Under curve 1 the fog's curve leaves its start with no limit to its slope, and the clear ground round the hero ended in a hard ring.
// The fog's share times this: 0 at the start, 1 from 40 units past it ([fog] ease_in; on 0 = 1, the fog as before). The function is
// word for word shared/unleashed_fog.h kEaseHlsl (the before-the-effects step builds it from that string).
float UnleashedFogEase(float dist, float start, float on)
{
    return on > 0.5 ? smoothstep(start, start + 40.0, dist) : 1.0;
}
