#pragma once
// app_thread.h - the plugin's way onto the game's APP thread for the network SDK's client calls (see vr/app_thread_pump.h for why).
//
// A joined TCP/IP client's connectToHost / getChannelInfo / sendToHost must come from the app thread (the UI thread's jobs are on it); the
// plugin's 1 ms timer thread and the game's own hooks are not safe places. Post the whole step of the channel's state machine here.

#include <functional>

#include "app_thread_pump.h"

namespace D2RL { struct PluginContext; }

namespace appthread {

// vrcam's load (needs the thread service) and unload.
void Init(const D2RL::PluginContext* ctx);
void Shutdown();

// Any thread: fn runs on the app thread, later (never inline); the latest fn of a slot wins. false: no thread service or unloading.
bool Post(Slot slot, std::function<void()> fn);

// A job that is already on the UI thread (pose share's, melee direct's): so that NoteCall knows the UI thread's id.
void MarkUiJob();

// The first call of each (module, call) pair is written to the log, naming the thread: the UI thread (the app thread) or not.
void NoteCall(const char* module, const char* call);

}  // namespace appthread
