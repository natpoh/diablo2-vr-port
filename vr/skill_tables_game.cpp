// skill_tables_game.cpp - the running game's skill tables into the icon side (vr/skill_tables_game.h). Only read: the loader's data-table views
// are the game's compiled rows; nothing is written. The layout is checked row by row in wheelicons::ParseGameTables (pure, tested).

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <memory>

#include <D2RLPlugin/api.h>

#include "log_async.h"
#include "skill_tables_game.h"
#include "wheel_icons.h"

using namespace D2RL;

namespace skilltables {
namespace {

const PluginContext* g_ctx = nullptr;
const DataTableService* g_tables = nullptr;
bool g_asked = false;
uint64_t g_usedRev = 0;        // the Skills table's revision the tables in use were made from
uint64_t g_refusedRev = ~0ull; // a revision whose rows did not read: not tried again until the next load
int g_toldRefused = 0;

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

bool View(DataTables::Bank bank, DataTables::TableId id, DataTables::TableView* v) {
    *v = DataTables::TableView{};
    v->structSize = DataTables::TableViewSize;
    return g_tables->getTable(g_ctx, bank, id, v) == DataTables::Result::Success && v->rows && v->rowSize && v->rowCount;
}

wheelicons::RawTable Raw(const DataTables::TableView& v) {
    wheelicons::RawTable r;
    r.rows = static_cast<const uint8_t*>(v.rows);
    r.count = v.rowCount;
    r.size = v.rowSize;
    return r;
}

bool ParseGuarded(const wheelicons::RawTable (&t)[4], wheelicons::GameTables* out, const char** why) noexcept {
    __try {
        return wheelicons::ParseGameTables(t[0], t[1], t[2], t[3], out, why);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *why = "reading the rows faulted";
        return false;
    }
}

}  // namespace

void Refresh(const PluginContext* ctx) {
    if (!ctx) return;
    if (!g_asked) {
        g_asked = true;
        g_ctx = ctx;
        const DataTableService* s = nullptr;
        g_tables = ctx->QueryService(&s) == ServiceQueryResult::Success ? s : nullptr;
        if (!g_tables || !g_tables->getTable) Log("vrcam: skill tables - no data-table service: skill names, trees and icons only from an icon cache");
    }
    if (!g_tables || !g_tables->getTable) return;
    // the newest bank whose rows read (RotW holds every class; the others the same seven)
    for (const DataTables::Bank bank : {DataTables::Bank::Rotw, DataTables::Bank::Lod, DataTables::Bank::Classic}) {
        DataTables::TableView v[4];
        if (!View(bank, DataTables::TableId::Skills, &v[0])) continue;
        if (v[0].revision == g_usedRev && wheelicons::HasGameTables()) return;   // in use, as it was
        if (v[0].revision == g_refusedRev) return;
        if (!View(bank, DataTables::TableId::SkillDesc, &v[1]) || !View(bank, DataTables::TableId::CharStats, &v[2]) ||
            !View(bank, DataTables::TableId::PlayerClass, &v[3]))
            continue;
        // (only a summary is kept - a few KB - never the views: they expire at the next load)
        const wheelicons::RawTable raw[4] = {Raw(v[0]), Raw(v[1]), Raw(v[2]), Raw(v[3])};
        auto t = std::make_shared<wheelicons::GameTables>();
        const char* why = "";
        char line[300];
        if (!ParseGuarded(raw, t.get(), &why)) {
            g_refusedRev = v[0].revision;
            if (g_toldRefused++ < 3) {
                std::snprintf(line, sizeof line, "vrcam: skill tables - bank %u (skills %u rows of %u bytes, descriptions %u of %u, classes %u of %u / %u) not read: %s - "
                              "skill names, trees and icons only from an icon cache", (unsigned)bank, v[0].rowCount, v[0].rowSize, v[1].rowCount, v[1].rowSize,
                              v[2].rowCount, v[2].rowSize, v[3].rowSize, why);
                Log(line);
            }
            return;
        }
        int onTree = 0, withIcon = 0;
        for (const auto& s : t->skill) {
            onTree += s.page ? 1 : 0;
            withIcon += s.iconCel >= 0 ? 1 : 0;
        }
        const int classes = t->classes;
        g_usedRev = v[0].revision;
        wheelicons::SetGameTables(std::move(t));
        std::snprintf(line, sizeof line, "vrcam: skill tables - from the game (bank %u): %u skills, %d on a class's tree, %d with an icon frame, %d classes%s",
                      (unsigned)bank, v[0].rowCount, onTree, withIcon, classes,
                      wheelicons::Available() ? "" : "; no icon sheets beside the plugin yet (D2R VR Settings copies them from the game): names on plain plates");
        Log(line);
        return;
    }
}

}  // namespace skilltables
