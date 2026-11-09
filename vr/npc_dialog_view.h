// npc_dialog_view.h - [hud] npc_dialog_keep_view: talking to an NPC in VR F1, F2, F3 keeps the stereo view.
//
// Serg (2026-11-09, after martin039383's report from F3): "плашка диалога с нпс тоже не отдаляла вид". Natively (OpenXR) any menu open in F1 - F3
// puts the flat picture up instead of the pair (vr/frame/frame_order.cpp NativePairBegin; menu_3d is F4's): the game's whole window on a quad
// some way ahead - the diorama, the window or the head camera gone, the view "flown far back". An NPC's talk opens only its small box
// (NpcDialogPanel: Talk / Trade / Cancel) and, with the laser's panels, its speech scroll (ScrollSpeech...): nothing to point at that the pad does
// not pick, so with the key on those panels alone keep the pair - the box and the scroll drawn in the game's picture with the rest of its interface,
// as the toolbar is. Trade, the stash or any other screen opened from there is a menu as ever: the flat picture. F4 keeps its own way (menu_3d).
// Unticked: as before.
#pragma once

#include <string>

namespace npcview {

// A panel of an NPC's talk: its small box, its speech scroll.
inline bool IsTalkPanel(const std::string& name) { return name == "NpcDialogPanel" || name.find("ScrollSpeech") != std::string::npos; }

// The menus open (each panel the game has open and whether it counts as a menu): all of them an NPC's talk, and at least one.
template <class Names, class IsMenu>
bool TalkOnly(const Names& names, IsMenu isMenu) {
    bool talk = false;
    for (const std::string& n : names) {
        if (!isMenu(n)) continue;
        if (!IsTalkPanel(n)) return false;
        talk = true;
    }
    return talk;
}

struct Where {
    bool on = false;          // [hud] npc_dialog_keep_view
    bool vr = false;          // the headset (platform 1)
    bool firstPerson = false; // VR F4: its own way (menu_3d)
    bool nativeView = false;  // native OpenXR draws this view as a pair (F2; F1 / F3 with views_3d)
    bool inWorld = false;     // a game area
    bool talkOnly = false;    // TalkOnly
};
// The pair kept while an NPC's talk is the only menu open.
inline bool KeepsView(const Where& w) { return w.on && w.vr && !w.firstPerson && w.nativeView && w.inWorld && w.talkOnly; }

}  // namespace npcview
