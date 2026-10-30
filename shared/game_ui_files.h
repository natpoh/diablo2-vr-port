#pragma once
// game_ui_files.h - the game's own interface art the skill panel's game look draws with ([openxr] skill_panel_game_look): which files of
// the player's game, and under which names they wait in the cache beside the plugin. D2R_VR_Settings copies them out of the game's
// archives (tools/settings/menu_assets.cpp, CascLib, read only); the plugin only reads the copies (vr/game_ui.cpp). Blizzard's files on
// the player's own machine: never in the repository, an installer or a release.
//
// They are the pieces D2RLoader's Multiplayer window and the game's own Options and lobby panels are built from (the layouts
// data\global\ui\Layouts\CreateGamePanelHD.json, JoinGamePanelHD.json, TCPIPPanelHD.json, SoundOptionsPanelHD.json, _profileHD.json).

namespace gameuifiles {

constexpr const wchar_t* kFolder = L"d2r_vr_game_ui";   // beside d2rl-vrcam.dll (and d2r_vr.ini)

struct File {
    const char* archive;   // in the game's archives (CascLib, after "data:")
    const wchar_t* leaf;   // the copy's name in kFolder
};

// The order is vr/game_ui.h's Sprite, then the two fonts.
constexpr File kFiles[] = {
    {"data\\hd\\global\\ui\\PANEL\\Modals\\Modal_No_Title_BG.sprite", L"Modal_No_Title_BG.sprite"},          // the stone slab
    {"data\\hd\\global\\ui\\PANEL\\TooltipBorder\\TooltipBorder.sprite", L"TooltipBorder.sprite"},           // the gold corners, 9 pieces
    {"data\\hd\\global\\ui\\Controller\\Panel\\Stash\\V2\\StashTabs.sprite", L"StashTabs.sprite"},           // the Options menu's tabs
    {"data\\hd\\global\\ui\\Lobby\\JoinGame\\JoinGame_Tabs.sprite", L"JoinGame_Tabs.sprite"},                // the lobby's small tabs
    {"data\\hd\\global\\ui\\FrontEnd\\HD\\Final\\FrontEnd_ButtonMed.sprite", L"FrontEnd_ButtonMed.sprite"},  // the button with the blue gems
    {"data\\hd\\global\\ui\\Lobby\\CreateGame\\CreateGame_AdvancedCheckBox.sprite", L"CreateGame_AdvancedCheckBox.sprite"},
    {"data\\hd\\global\\ui\\PauseMenu\\LeftArrow.sprite", L"LeftArrow.sprite"},                              // the slider's arrows
    {"data\\hd\\global\\ui\\PauseMenu\\RightArrow.sprite", L"RightArrow.sprite"},
    {"data\\hd\\global\\ui\\PauseMenu\\HorizontalScroll.sprite", L"HorizontalScroll.sprite"},                // the slider's track
    {"data\\hd\\global\\ui\\PauseMenu\\HorizontalIndicator.sprite", L"HorizontalIndicator.sprite"},          // and its knob
    {"data\\hd\\global\\ui\\PANEL\\TCP_IP\\TCP_IP_AddressBox.sprite", L"TCP_IP_AddressBox.sprite"},          // a dark inset field
    {"data\\hd\\global\\ui\\PauseMenu\\Divider_Medium.sprite", L"Divider_Medium.sprite"},                    // a thin gold rule
    {"data\\hd\\global\\ui\\Lobby\\CreateGame\\CreateGame_DifficultyBTN.sprite", L"CreateGame_DifficultyBTN.sprite"},
    {"data\\hd\\global\\ui\\Lobby\\CreateGame\\CreateGame_DifficultyBTN_Selected.sprite", L"CreateGame_DifficultyBTN_Selected.sprite"},
    {"data\\hd\\ui\\fonts\\ExocetBlizzardOT-Medium.otf", L"ExocetBlizzardOT-Medium.otf"},                   // the titles' font
    {"data\\hd\\ui\\fonts\\Formal436BT.ttf", L"Formal436BT.ttf"},                                            // the text's
};
constexpr int kFileCount = (int)(sizeof kFiles / sizeof kFiles[0]);
constexpr int kSpriteCount = kFileCount - 2;

}  // namespace gameuifiles
