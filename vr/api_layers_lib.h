// api_layers_lib.h - [openxr] block_listed_layers (on): the implicit OpenXR layers on [openxr] blocked_layers kept out of the game's process while the
// eyes are drawn our own way (shared/api_layers.h has the what and why). vr/xr.cpp calls Apply on its session thread before the OpenXR loader is
// first used for a session; the registry is only read.
#pragma once

#include <string>
#include <vector>

#include "api_layers.h"

namespace apilayerslib {

using LogFn = void (*)(const char*);

// The implicit layers the loader would load in this process now (HKLM, then HKCU unless the process runs elevated - as the loader reads them),
// each manifest read. Nothing is changed.
std::vector<apilayers::Found> Scan();

// Before the session's xrCreateInstance: Scan, decide (`block` = [openxr] block_listed_layers, `ownCrop` = not [openxr] runtime_fov, `list` =
// [openxr] blocked_layers), and set the disable_environment variable of each listed one to turn off, in this process only; a variable set by an
// earlier Apply is cleared again when its layer is no longer to be turned off. Logs every implicit layer found (once per change). Returns them.
std::vector<apilayers::Found> Apply(bool block, bool ownCrop, const std::string& list, LogFn log);

}  // namespace apilayerslib
