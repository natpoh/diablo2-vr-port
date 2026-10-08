#pragma once
// Motion vectors for DLSS with real stereo: against the same eye's previous
// frame instead of the other eye's (dlss_mv.cpp).

namespace dlssmv {
void SetLogger(void (*log)(const char*));
// 0 off, 1 check only (logs how well vrcam's matrices explain the game's vectors), 2 fix
void SetMode(int mode);
// The camera's part of the game's vectors: -1 (where the point is - where it was) or +1 the other way
void SetSign(float sign);
// The picture's view: this many DLSS evaluations back from the latest vrcam handed over (0-2)
void SetLag(int lag);
// How much of the camera's turn the far part's vectors get (1 all, 0 none: the game's own)
void SetFarTurn(float k);
// The near part redone against the same eye; the jitter's step made this eye's
void SetFixes(bool nearPart, bool jitter);
// Once a game frame: the view and projection vrcam handed the game, and its eye.
void RecordFrame(const float view[16], const float proj[16], int eye);
// Every view vrcam hands over: the frame being built now.
void SetPending(const float view[16], const float proj[16], int eye);
// Just before the game's DLSS evaluation on the D3D12 command list: may hand DLSS
// our vectors; returns what to put back afterwards (nullptr = nothing).
void* BeforeEvaluate(void* cmdList, const void* ngxParams, int eye);
void AfterEvaluate(const void* ngxParams, void* restore);
}  // namespace dlssmv
