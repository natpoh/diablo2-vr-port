// A stand-in for the 3D video library (shared/d2r_vr_video3d.h), for video3d_lib_test: the loader and its calls, not the drawing (that is the library's own,
// in its own repository). Start succeeds for any non-null device; Record returns 1 after a Submitted, 0 before (as "no depth yet"); Reset makes it 0 again.
// Built twice: STANDIN_BAD_ABI=1 speaks another interface version.

#define D2RV3_BUILD_DLL 1
#include "d2r_vr_video3d.h"

namespace {
int g_started = 0, g_submitted = 0, g_stops = 0;
}

extern "C" {

int32_t D2rv3Abi(void) {
#if defined(STANDIN_BAD_ABI)
    return D2RV3_ABI_VERSION + 98;
#else
    return D2RV3_ABI_VERSION;
#endif
}

const char* D2rv3Name(void) { return "video3d stand-in"; }

int32_t D2rv3Start(void* device, void* queue, D2rv3LogFn log) {
    if (!device || !queue) return 0;
    if (log) log("stand-in started");
    ++g_started;
    return 1;
}

int32_t D2rv3Record(void* list, const D2rv3Frame* frame) {
    if (!list || !frame || frame->size < sizeof(D2rv3Frame)) return -1;
    return g_submitted ? 1 : 0;
}

void D2rv3Submitted(void) { ++g_submitted; }
void D2rv3Reset(void) { g_submitted = 0; }
void D2rv3Stop(void) { ++g_stops; g_submitted = 0; }

}
