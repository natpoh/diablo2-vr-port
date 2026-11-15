#pragma once
// skill_tables_game.h - the running game's own skill tables for the skill panel, the skill under the map and the wrist's medallion: each skill's
// class, tree page, icon frame and the string id of its name, the classes' trees' name ids - read from the loader's data tables (Skills,
// SkillDesc, CharStats, PlayerClass; read only) and handed to vr/wheel_icons.h SetGameTables. A player has no icon cache (that is a developer's
// extra): this is where the names, the categories and which icon to cut come from.

namespace D2RL { struct PluginContext; }

namespace skilltables {

// On the game's UI thread (the data tables answer nowhere else). Cheap when nothing changed: the tables are parsed again only when the loader
// loads them anew (their revision) or the icon side forgot them (an ini reload); a layout this build does not know is told in the log once
// and tried again only at the next load.
void Refresh(const D2RL::PluginContext* ctx);

}  // namespace skilltables
