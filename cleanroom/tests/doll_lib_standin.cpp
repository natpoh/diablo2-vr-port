// A stand-in for the doll library (shared/d2r_vr_doll.h), for doll_lib_test: the loader and its calls, not the physics (that is the library's own,
// in its own repository). Push puts the pushed monster into the state with the impulse as its velocity; Apply moves every body of a pushed monster by
// 1 cm; Mass is 10 kg a body; Balance is 0.5 for a pushed monster. Built twice: STANDIN_BAD_ABI=1 speaks another interface version.

#define D2RD_BUILD_DLL 1
#include "d2r_vr_doll.h"

extern "C" {

int32_t D2rdAbi(void) {
#if defined(STANDIN_BAD_ABI)
    return D2RD_ABI_VERSION + 98;
#else
    return D2RD_ABI_VERSION;
#endif
}

const char* D2rdName(void) { return "doll stand-in"; }

int32_t D2rdPush(D2rdState* state, const D2rdParams* params, const D2rdBody* bodies, int32_t n, int32_t i, const float impulse[3], const float point[3]) {
    (void)params;
    (void)point;
    if (!state || !bodies || i < 0 || i >= n) return -1;
    if (!bodies[i].doll) return 0;
    state->size = sizeof(D2rdState);
    D2rdMonster& m = state->monsters[0];
    m.id = bodies[i].id;
    for (int k = 0; k < 3; ++k) m.s.vel[k] = impulse[k];
    state->nMonsters = 1;
    return 1;
}

int32_t D2rdStep(D2rdState* state, const D2rdParams* params, float dt) {
    (void)params;
    (void)dt;
    return state ? 0 : -1;
}

int32_t D2rdApply(const D2rdState* state, const D2rdParams* params, D2rdBody* bodies, int32_t n) {
    (void)params;
    if (!state || !bodies) return -1;
    int32_t moved = 0;
    for (int32_t i = 0; i < n; ++i)
        if (state->nMonsters > 0 && bodies[i].id == state->monsters[0].id) {
            bodies[i].a[0] += 0.01f;
            bodies[i].b[0] += 0.01f;
            ++moved;
        }
    return moved;
}

float D2rdMass(const D2rdParams* params, const D2rdBody* bodies, int32_t n, uint32_t id) {
    (void)params;
    float kg = 0.0f;
    for (int32_t i = 0; i < n; ++i)
        if (bodies[i].id == id) kg += 10.0f;
    return kg;
}

float D2rdBalance(const D2rdState* state, const D2rdParams* params, uint32_t id, float off[3]) {
    (void)params;
    const bool pushed = state && state->nMonsters > 0 && state->monsters[0].id == id;
    if (off) off[0] = off[1] = off[2] = 0.0f;
    return pushed ? 0.5f : 0.0f;
}

}  // extern "C"
