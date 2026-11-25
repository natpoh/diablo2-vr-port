#pragma once
// build_backup.h - D2R VR Settings' backups of the installed build (shared/build_backup_rule.h says what a build is): make one, list them,
// restore one, delete one. Used by the Home tab's buttons and by D2R VR Setup (D2R_VR_Settings.exe --backup-build, before an update).
// No window here: the caller checks that the game and BodyWalk are closed and shows the outcome.
#include "../../shared/build_backup_rule.h"

#include <functional>
#include <string>
#include <vector>

namespace buildbackup {

struct Places {
    std::wstring game;       // the game's folder (D2R.exe), with a trailing backslash
    std::wstring bodywalk;   // %LOCALAPPDATA%\BodyWalkVR\ ("" = leave BodyWalk's files out)
};
Places PlacesFor(const std::wstring& gameDir);   // the game's folder made full, BodyWalk's from %LOCALAPPDATA%
std::wstring BackupsDir(const std::wstring& gameDir);   // <game>\mods\D2R-VR\build_backups\ (trailing backslash)
bool BuildInstalled(const std::wstring& gameDir);       // the plugin is there
std::wstring InstalledVersion(const std::wstring& gameDir);   // the plugin's version ("0.223.1"), else the settings program's, else "unknown"
Stamp Now();                                            // local time

std::vector<Entry> Scan(const Places& p, int scope = kBuild);   // the build (or its settings) as it is now (backup-relative paths, sizes)

enum Code { kDone = 0, kFailed = 1, kNothing = 2, kNoSpace = 3, kBadBackup = 5 };
struct Outcome {
    int code = kFailed;
    std::wstring folder;       // the backup made (Make), or the one made of what was there (Restore)
    uint64_t files = 0, bytes = 0;
    std::wstring error;        // English, for the log: what failed and Windows' error
    bool settingsReplaced = false;   // Restore put another D2R_VR_Settings.exe in place of the running one (restart it)
    // The running D2R_VR_Settings.exe could not even be moved aside (a FAT / exFAT drive, another program holding it): the restored one
    // waits beside it as <exe>.restore_new, and FinishPending puts it in place once this program has exited (a helper process does that).
    std::wstring settingsPending;
};
using Progress = std::function<void(uint64_t done, uint64_t total)>;

// A backup of the build (scope kBuild) or of its settings only (kSettings) as it is now into BackupsDir\FolderName; then the old ones Prune
// names go (never `keep`).
Outcome Make(const Places& p, int kind, const std::wstring& version, int keepAuto, const Stamp& now, const Progress& progress = nullptr,
             const std::vector<std::wstring>& keep = {}, int scope = kBuild);
// The second half of a restore whose settings program could not be swapped while it ran: waits (at most timeoutMs) for process `pid` to
// end, then moves `pending` over `target` (a few tries: the file may stay held a moment after the exit). true: done.
bool FinishPending(unsigned long pid, const std::wstring& pending, const std::wstring& target, unsigned long timeoutMs);
// The backups there, newest first (incomplete ones too, complete = false).
std::vector<Info> List(const std::wstring& gameDir);
bool ReadBackup(const std::wstring& gameDir, const std::wstring& folder, Info* info, std::vector<Entry>* entries);
// Puts a backup back: first a backup of what is there now (kind kRestore, the same scope; not when nothing is installed), then every file
// in RestoreOrder, each through a .restore_tmp moved over the old one (a program in use - the running settings program - is moved aside
// into build_backups\_replaced first; if even that fails, it waits as settingsPending), then, for a whole build, the made data's extra files
// go (MirrorExtras). A settings backup puts back the settings only. A failure stops it: what was there is in out.folder.
Outcome Restore(const Places& p, const std::wstring& folder, int keepAuto, const Stamp& now, const Progress& progress = nullptr);
// Removes one backup folder (only a name of ours, directly under build_backups).
bool Remove(const std::wstring& gameDir, const std::wstring& folder);
// The programs a restore moved aside (build_backups\_replaced): removed when no longer in use.
void CleanReplaced(const std::wstring& gameDir);

}  // namespace buildbackup
