// gait_lib_standin.cpp - a stand-in for the gait library (shared/d2r_vr_gait.h), so the loader (vr/gait_lib.cpp) can be tested without the real one (the private
// d2r_vr_gait.dll). Not a gait anyone walks with: each foot goes to its rest place moved along the travel by a tenth of a second of it, the hips down by 1, and the
// state counts the steps. STANDIN_BAD_ABI builds one that speaks another interface version; STANDIN_FAULT one whose step faults.

#define D2RF_BUILD_DLL 1
#include "d2r_vr_gait.h"

extern "C" {

D2RF_API int32_t D2rfAbi(void) {
#ifdef STANDIN_BAD_ABI
    return D2RF_ABI_VERSION + 7;
#else
    return D2RF_ABI_VERSION;
#endif
}

D2RF_API const char* D2rfName(void) { return "gait stand-in"; }

D2RF_API int32_t D2rfLegsStep(const D2rfLegsIn* in, D2rfState* state, D2rfLegsOut* out) {
#ifdef STANDIN_FAULT
    volatile int* nowhere = nullptr;
    *nowhere = 1;
#endif
    if (!in || !state || !out || in->size < sizeof(D2rfLegsIn) || out->size < sizeof(D2rfLegsOut)) return 0;
    state->phase += 1.0f;
    for (int i = 0; i < 2; ++i) {
        out->foot[i] = in->rest[i];
        out->foot[i].x += in->velocity.x * 0.1f;
        out->foot[i].z += in->velocity.z * 0.1f;
        out->lift[i] = 0.0f;
    }
    out->drop = 1.0f;
    out->move = 1.0f;
    out->strideM = 0.5f;
    out->speedM = 0.0f;
    return 1;
}

}
