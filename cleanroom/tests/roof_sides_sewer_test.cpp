// [render] roofs_two_sided (vr/roof_sides.h): the Kurast sewer entrances' materials are chosen, and nothing near them.
#include "roof_sides.h"

#include <cstdio>

static int g_fail = 0;
static void Expect(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}

int main() {
    using roofsides::Selected;
    const char* entrance = "data/hd/env/model/act3/kurast/act3_kurast_architecture/sewer_entrance02.model";
    const char* stairs = "data/hd/env/model/act3/kurast/act3_kurast_stairs/stairs_sewers01_lod3.model";
    Expect(Selected(entrance, "roof_lip_corner01"), "the entrance's lip");
    Expect(Selected(entrance, "elevation_walls_a"), "the entrance's walls");
    Expect(!Selected(entrance, "kurast_stairs_a"), "a stairs material on the entrance: no");
    Expect(Selected(stairs, "kurast_stairs_b"), "the stairs, a LOD");
    Expect(Selected(stairs, "pasted__sewer_wall05"), "the stairs' wall");
    Expect(Selected("data/hd/env/model/act3/kurast/act3_kurast_stairs/stairs_sewers01.model", "pasted__sewer_column_corner"), "the stairs' column");
    Expect(!Selected("data/hd/env/model/act3/kurast/act3_kurast_stairs/stairs_sewers01_lod5.model", "kurast_stairs_a"), "no LOD 5");
    Expect(!Selected("data/hd/env/model/act3/kurast/act3_kurast_stairs/stairs_sewers010.model", "kurast_stairs_a"), "another family");
    Expect(!Selected("data/hd/env/model/act3/kurast/act3_kurast_stairs/stairs_other01.model", "kurast_stairs_a"), "other stairs");
    Expect(!Selected("data/hd/env/model/act2/sewer/act3_kurast_stairs/stairs_sewers01.model", "kurast_stairs_a"), "another act");
    Expect(Selected("data/hd/env/model/act1/town/act1_town_roof01.model", "ext_roof_tile01"), "the roofs as before");
    Expect(!Selected("data/hd/env/model/act1/town/act1_town_wall01.model", "ext_roof_tile01"), "a wall's roof trim as before");
    if (g_fail) return 1;
    std::printf("roof_sides_sewer_test: ok\n");
    return 0;
}
