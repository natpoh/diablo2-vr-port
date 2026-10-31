// stride_lib_standin.cpp - a stand-in for the weight library's gait (shared/d2r_vr_stride.h, interface 2), so the loader (vr/stride_lib.cpp) can be tested
// without the real one (the private d2r_vr_weight.dll). Not a gait anyone walks with: the legs as he stands, the pelvis up by the speed's hundredth, and the
// state counts the calls. STANDIN_BAD_ABI builds one that speaks another interface version.

#define D2RS_BUILD_DLL 1
#include "d2r_vr_stride.h"

extern "C" {

D2RS_API int32_t D2rsAbi(void) {
#ifdef STANDIN_BAD_ABI
    return D2RS_ABI_VERSION + 7;
#else
    return D2RS_ABI_VERSION;
#endif
}

D2RS_API const char* D2rsName(void) { return "gait stand-in"; }

D2RS_API int32_t D2rsGait(const D2rsGaitIn* in, D2rsGaitState* state, D2rsGaitOut* out) {
    if (!in || !state || !out || in->size < sizeof(D2rsGaitIn) || out->size < sizeof(D2rsGaitOut)) return 0;
    ++state->magic;
    for (int j = 0; j < D2RS_GAIT_JOINTS; ++j)
        for (int k = 0; k < 3; ++k) out->joint[j][k] = in->rest[j][k];
    out->joint[D2RS_PELVIS][1] += 0.01f * in->speed;
    out->stepM = 1.0f;
    out->cycleS = 0.5f;
    return 1;
}

}  // extern "C"
