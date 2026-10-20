// label_area.h - [hud] labels_whole_eye: item labels kept down to the bottom of the game's picture, not only of its interface.
//
// The game's label layout (LabelLayout 0x1FA9F0) drops a label whose bottom passes the interface's origin y plus its height (InterfaceSize 0x8462E0, asked at
// 0x1FAC2A). Where the interface is laid out in a smaller band of the picture than the labels are placed over - bearstonem/d2r-pcvr's native VR (MIT, v0.3.23,
// steamvr/dropped_item_labels.h LabelArea) sizes it as a widescreen band of a square or taller eye - items low in the view lose their labels, Show Items held
// too. For that one call only, the whole picture's height. Off by default here: to be tried with and without in the headset.
//
// Pure (cleanroom/tests/label_area_test.cpp); the hook is vr/vrcam.cpp InstallLabelArea.
#pragma once

#include <cstdint>

namespace labelarea {

// The interface's size as the query returns it: width in the low 32 bits, height in the high ones. pictureHeight 0 (not known) or not taller: as it was.
inline uint64_t LabelArea(uint64_t interfaceSize, uint32_t pictureHeight) {
    const uint32_t height = uint32_t(interfaceSize >> 32);
    return pictureHeight > height ? (interfaceSize & 0xffffffffull) | (uint64_t(pictureHeight) << 32) : interfaceSize;
}

}  // namespace labelarea
