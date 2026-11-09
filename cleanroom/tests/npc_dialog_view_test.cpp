// [hud] npc_dialog_keep_view (vr/npc_dialog_view.h): talking to an NPC in VR F1, F2, F3 keeps the stereo view (Serg, 2026-11-09:
// "плашка диалога с нпс тоже не отдаляла вид"); everything else that is a menu stays the flat picture, and F4 keeps its own way.
//
//   npc_dialog_view_test   exit code 0 = every case passed

#include "../../vr/npc_dialog_view.h"

#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace {
int g_fails = 0;
int g_checks = 0;
#define CHECK(c)                                               \
	do {                                                       \
		++g_checks;                                            \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s\n", __LINE__, #c); \
		}                                                      \
	} while (0)

// as vr/gamestate.cpp IsMenuPanel, the part that matters here (the laser's panels on: the speech scroll counts)
bool IsMenu(const std::string& n) {
	if (n.rfind("HUD", 0) == 0) return false;
	for (const char* k : {"Vendor", "Npc", "NPC", "Stash", "PlayerInventory", "ScrollSpeech", "Waypoint"})
		if (n.find(k) != std::string::npos) return true;
	return false;
}
bool TalkOnly(std::initializer_list<const char*> open) {
	std::set<std::string> names;
	for (const char* n : open) names.insert(n);
	return npcview::TalkOnly(names, IsMenu);
}
npcview::Where Native(bool talkOnly) {
	npcview::Where w;
	w.on = true; w.vr = true; w.firstPerson = false; w.nativeView = true; w.inWorld = true; w.talkOnly = talkOnly;
	return w;
}
}  // namespace

int main() {
	// which panels are an NPC's talk
	CHECK(npcview::IsTalkPanel("NpcDialogPanel"));
	CHECK(npcview::IsTalkPanel("ScrollSpeechPanel") && npcview::IsTalkPanel("ScrollSpeech"));
	CHECK(!npcview::IsTalkPanel("VendorPanelLayout") && !npcview::IsTalkPanel("NpcDialog") && !npcview::IsTalkPanel("HUDPanel"));

	// only the talk open (the HUD's own panels are not menus and do not count)
	CHECK(TalkOnly({"NpcDialogPanel", "HUDPanel", "MiniMenuPanel"}));
	CHECK(TalkOnly({"NpcDialogPanel", "ScrollSpeechPanel"}));
	CHECK(TalkOnly({"ScrollSpeechPanel"}));
	// Trade (the vendor's screen), the stash, a waypoint: the flat picture as ever
	CHECK(!TalkOnly({"NpcDialogPanel", "VendorPanelLayout"}));
	CHECK(!TalkOnly({"VendorPanelLayout"}));
	CHECK(!TalkOnly({"BankExpansionLayout_Stash"}));
	CHECK(!TalkOnly({"WaypointPanel"}));
	// no menu at all: nothing to keep
	CHECK(!TalkOnly({"HUDPanel"}));
	CHECK(!TalkOnly({}));

	// where the pair is kept: native VR F1 - F3, a game area, the key on, the talk alone
	CHECK(npcview::KeepsView(Native(true)));
	CHECK(!npcview::KeepsView(Native(false)));
	npcview::Where w = Native(true);
	w.on = false;   // unticked: as before
	CHECK(!npcview::KeepsView(w));
	w = Native(true);
	w.firstPerson = true;   // F4: menu_3d's own way
	CHECK(!npcview::KeepsView(w));
	w = Native(true);
	w.vr = false;   // the flat game: never the headset's flat picture
	CHECK(!npcview::KeepsView(w));
	w = Native(true);
	w.nativeView = false;   // FlatVR, or F1 / F3 as a flat picture anyway (views_3d off)
	CHECK(!npcview::KeepsView(w));
	w = Native(true);
	w.inWorld = false;   // the title, the character screens
	CHECK(!npcview::KeepsView(w));

	std::printf("npc_dialog_view_test: %d checks, %d failed\n", g_checks, g_fails);
	return g_fails == 0 ? 0 : 1;
}
