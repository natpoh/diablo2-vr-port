// vr/diag/save_backups.h - [support] save_backups: the game's saves copied into Saved Games\Diablo II Resurrected\D2R VR Backups\<time>
// at the start and whenever one changes, only good ones, kept save_backups_days (vr/diag/save_backups.cpp; its rules save_backups_rule.h)
#pragma once

namespace vrc {
namespace savebackups {
void Start();   // after g_iniPath is set: the backups' thread (its first backup before the game reads the saves)
void Stop();    // one last look at the saves, the thread joined
}  // namespace savebackups
}  // namespace vrc
