// [hud] labels_whole_eye: the label layout's height check given the whole picture (vr/label_area.h LabelArea).
#include <cstdint>
#include <cstdio>

#include "label_area.h"

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}
static uint64_t Pack(uint32_t w, uint32_t h) { return uint64_t(h) << 32 | w; }

int main() {
    using labelarea::LabelArea;
    // a 2048 square's widescreen band (2048 x 1152): the height becomes the picture's, the width stays
    Check(LabelArea(Pack(2048, 1152), 2048) == Pack(2048, 2048), "band: the whole picture's height");
    // the interface as tall as the picture or taller: unchanged
    Check(LabelArea(Pack(1920, 1080), 1080) == Pack(1920, 1080), "same height: unchanged");
    Check(LabelArea(Pack(1920, 1080), 900) == Pack(1920, 1080), "picture shorter: unchanged");
    // the picture not known
    Check(LabelArea(Pack(1920, 1080), 0) == Pack(1920, 1080), "no picture size: unchanged");
    std::printf(g_fail ? "label_area_test: %d FAILED\n" : "label_area_test: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
