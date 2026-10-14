#pragma once
// The in-headset settings panel's side of d2r_vr.ini: reading the keys it offers and writing the ones
// that changed (shared/ini_txn: a conflict with an outside edit reloads instead of overwriting, the
// original kept once as d2r_vr.ini.before-headset-menu). The file is read and written on a thread of
// its own, never on the draw thread; the panel only reads the latest Snapshot and hands in an Apply.
// After bearstonem/d2r-pcvr's vr/settings_menu.cpp (Poll / Submit), MIT.

#include <string>

#include "hset_model.h"

namespace hset::store {

// Synchronous (the thread, the tests): the keys as the file has them now / one Apply.
Snapshot LoadSnapshot(const wchar_t* path);
// Result::Saved, Conflict (nothing written) or Failed; `why` says in English.
Result SaveRequest(const wchar_t* path, const Request& r, std::string* why = nullptr);

// The panel opens: the first reading is made now (so the panel has values at once), then the thread
// polls the file twice a second and carries out Apply.
void Start(const wchar_t* iniPath);
// The panel closed: the thread ends once an Apply in flight is done. Does not wait (the draw thread calls it).
void Stop();
// Waits for the thread to end after Stop, up to `ms` (the session is torn down, the game closes, a test).
void Join(unsigned ms);
// Where the thread says what it did (an Apply saved, refused, failed); the caller's log, any thread.
void SetLogger(void (*log)(const char*));
Snapshot Read();
// False: another Apply is still in flight, or the request is not sane.
bool Submit(const Request& r);

}  // namespace hset::store
