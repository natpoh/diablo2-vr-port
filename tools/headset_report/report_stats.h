#pragma once
// report_stats.h - D2R VR Headset Report's arithmetic and wording (tools/headset_report/headset_report.cpp): the eyes' angles and gap over a few
// seconds, whether the pair is a mirror image, where a space or a hand was located, and the plain-English hints at the end of the report.
// Pure: no OpenXR, no Windows (tools/headset_report_test.cpp).
//
// Angles are OpenXR's (left and down negative), here in degrees. The fov line is the plugin's own ("eyes 68.1 mm apart, ... left fov -54.0
// 40.0 44.0 -55.0, right ...", vr/xr.cpp TellEyes), so a report and a player's d2r_vr.log read side by side.

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace headsetreport {

constexpr float kDeg = 57.2957795f;

// One number over the sampled frames.
struct Series {
    int n = 0;
    double sum = 0.0;
    float lo = 0.0f, hi = 0.0f;
    void Add(float v) {
        if (!std::isfinite(v)) return;
        if (n == 0) lo = hi = v;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
        sum += v;
        ++n;
    }
    float Mean() const { return n ? (float)(sum / n) : 0.0f; }
    float Spread() const { return n ? hi - lo : 0.0f; }
};

struct FovDeg { float l = 0.0f, r = 0.0f, u = 0.0f, d = 0.0f; };   // left right up down, degrees

// One eye's fov over the frames.
struct FovSeries {
    Series l, r, u, d;
    void Add(const FovDeg& f) { l.Add(f.l); r.Add(f.r); u.Add(f.u); d.Add(f.d); }
    FovDeg Mean() const { return {l.Mean(), r.Mean(), u.Mean(), d.Mean()}; }
    float Spread() const { return std::max({l.Spread(), r.Spread(), u.Spread(), d.Spread()}); }
};

constexpr float kSymTolDeg = 0.5f;   // two angles this close are the same (a runtime's rounding)

// Is the pair a mirror image (the left eye's outer side as wide as the right eye's outer side, the inner sides alike, the same up and down)?
// What every headset we know gives; the zzangu Quest 3 numbers (left -54 40 44 -55, right -40 54 44 -55) are one.
struct Symmetry {
    bool mirrored = false;   // horizontally a mirror pair
    bool sameVertical = false;
    float outerL = 0.0f, outerR = 0.0f, innerL = 0.0f, innerR = 0.0f;   // degrees, positive
    float wideL = 0.0f, wideR = 0.0f, tallL = 0.0f, tallR = 0.0f;       // each eye's whole width and height
};
inline Symmetry CheckSymmetry(const FovDeg& L, const FovDeg& R, float tol = kSymTolDeg) {
    Symmetry s;
    s.outerL = -L.l; s.innerL = L.r;
    s.outerR = R.r;  s.innerR = -R.l;
    s.wideL = L.r - L.l; s.wideR = R.r - R.l;
    s.tallL = L.u - L.d; s.tallR = R.u - R.d;
    s.mirrored = std::fabs(s.outerL - s.outerR) <= tol && std::fabs(s.innerL - s.innerR) <= tol;
    s.sameVertical = std::fabs(L.u - R.u) <= tol && std::fabs(L.d - R.d) <= tol;
    return s;
}

// XrSpaceLocationFlags as vr/xr.cpp's FlagsText writes them: P/p position valid/tracked, R/r orientation valid/tracked ("----" = nothing).
constexpr uint64_t kOriValid = 0x1, kPosValid = 0x2, kOriTracked = 0x4, kPosTracked = 0x8;
inline std::string FlagsText(uint64_t f) {
    std::string s = "----";
    if (f & kPosValid) s[0] = 'P';
    if (f & kPosTracked) s[1] = 'p';
    if (f & kOriValid) s[2] = 'R';
    if (f & kOriTracked) s[3] = 'r';
    return s;
}
inline bool Whole(uint64_t f) { return (f & kOriValid) && (f & kPosValid); }

// How often a space (the head, a hand) was located over the frames, and the flags seen.
struct Located {
    int frames = 0;      // frames it was asked for
    int calls = 0;       // the locate call itself succeeded
    int whole = 0;       // a turn and a place (both VALID bits)
    int tracked = 0;     // both TRACKED bits too
    uint64_t lastFlags = 0;
    std::vector<uint64_t> seen;   // each distinct flag set, in the order first seen
    void Add(bool callOk, uint64_t f) {
        ++frames;
        if (!callOk) return;
        ++calls;
        lastFlags = f;
        if (Whole(f)) ++whole;
        if ((f & kOriTracked) && (f & kPosTracked)) ++tracked;
        if (std::find(seen.begin(), seen.end(), f) == seen.end()) seen.push_back(f);
    }
    std::string Text() const {
        if (!frames) return "not looked at";
        char b[96];
        std::snprintf(b, sizeof b, "whole %d/%d, tracked %d/%d", whole, frames, tracked, frames);
        std::string s = b;
        if (calls < frames) {
            std::snprintf(b, sizeof b, ", the call failed %d times", frames - calls);
            s += b;
        }
        if (!seen.empty()) {
            s += ", flags";
            for (uint64_t f : seen) s += " " + FlagsText(f);
        }
        return s;
    }
};

inline std::string Fmt(const char* f, ...) {
    char b[1024];
    va_list a;
    va_start(a, f);
    std::vsnprintf(b, sizeof b, f, a);
    va_end(a);
    return b;
}

// The plugin's own fov line (vr/xr.cpp TellEyes): "eyes 68.1 mm apart, 0.00 deg between their views; left fov ..., right ... (left right up down, deg)".
inline std::string FovLine(float gapMm, float cantDeg, const FovDeg& L, const FovDeg& R) {
    return Fmt("eyes %.1f mm apart, %.2f deg between their views; left fov %.1f %.1f %.1f %.1f, right %.1f %.1f %.1f %.1f (left right up down, deg)",
               gapMm, cantDeg, L.l, L.r, L.u, L.d, R.l, R.r, R.u, R.d);
}

// What was measured, for the hints.
struct Facts {
    bool gameRunning = false;        // the game held the headset: no OpenXR call was made
    bool noHeadset = false;          // the runtime answered, but with no headset (XR_ERROR_FORM_FACTOR_UNAVAILABLE)
    bool session = false;            // a session ran and frames were sampled
    int frames = 0;                  // frames sampled
    float gapMm = 0.0f;              // the eyes' distance in VIEW, mean
    float gapSpreadMm = 0.0f;
    float cantDeg = 0.0f;            // the angle between the eyes' turns
    FovDeg fov[2];                   // VIEW, mean
    float fovSpreadDeg = 0.0f;       // the most any angle moved over the frames
    float fovSpaceDiffDeg = 0.0f;    // the most an angle differed between VIEW and LOCAL / STAGE
    unsigned recW[2] = {}, recH[2] = {};   // each eye's recommended picture
    bool stageListed = false;
    Located headStage, headLocal;    // VIEW located in STAGE / LOCAL
    Located grip[2][2], aim[2][2];   // [hand][0 LOCAL, 1 STAGE]
    bool handsLooked = false;        // the actions were synced (the session was focused)
    std::string profile[2];          // the interaction profile per hand ("" none)
    bool metaRuntime = false;        // Oculus / Meta: the plugin's room_by_head and hand_space act there by default
};

// The hints at the end of the report: what in the numbers is unusual, in plain words; "nothing unusual" when nothing.
inline std::vector<std::string> Hints(const Facts& f) {
    std::vector<std::string> h;
    if (f.gameRunning) {
        h.push_back("The game was running, so the runtime was not asked: close the game (and D2RLoader) and run this again with the headset on.");
        return h;
    }
    if (f.noHeadset) {
        h.push_back("The runtime found no headset: connect it (Link / Air Link, Virtual Desktop streaming, SteamVR showing it ready), put it on, and run this again.");
        return h;
    }
    if (!f.session) {
        h.push_back("No session ran: only what the runtime says without one is above (no eye poses, no spaces, no controllers).");
        return h;
    }
    if (f.frames == 0) h.push_back("The session ran but no frame could be sampled: the headset may have been asleep (put it on, then run this again).");
    if (f.frames > 0) {
        if (f.gapMm < 20.0f)
            h.push_back(Fmt("The runtime locates both eyes %.1f mm apart (in one place): the mod then uses [openxr] ipd_fallback_mm.", f.gapMm));
        else if (f.gapMm < 50.0f || f.gapMm > 80.0f)
            h.push_back(Fmt("Eye distance %.1f mm is outside the usual 50-80 mm.", f.gapMm));
        if (f.gapSpreadMm > 0.5f) h.push_back(Fmt("The eye distance moved by %.1f mm while sampling.", f.gapSpreadMm));
        const Symmetry s = CheckSymmetry(f.fov[0], f.fov[1]);
        if (!s.mirrored)
            h.push_back(Fmt("The two eyes' fov are not a mirror pair: outer left %.1f vs outer right %.1f, inner left %.1f vs inner right %.1f deg.",
                            s.outerL, s.outerR, s.innerL, s.innerR));
        if (!s.sameVertical)
            h.push_back(Fmt("The two eyes' fov differ up/down: left %.1f/%.1f, right %.1f/%.1f deg.", f.fov[0].u, f.fov[0].d, f.fov[1].u, f.fov[1].d));
        for (int e = 0; e < 2; ++e)
            if (f.fov[e].l >= 0.0f || f.fov[e].r <= 0.0f || f.fov[e].u <= 0.0f || f.fov[e].d >= 0.0f)
                h.push_back(Fmt("The %s eye's fov does not contain straight ahead (%.1f %.1f %.1f %.1f).", e ? "right" : "left", f.fov[e].l, f.fov[e].r,
                                f.fov[e].u, f.fov[e].d));
        if (f.cantDeg >= 0.25f)
            h.push_back(Fmt("The displays are canted: the eyes' views are turned %.2f deg apart ([openxr] canted_eyes handles it).", f.cantDeg));
        if (f.fovSpreadDeg > 0.5f) h.push_back(Fmt("The runtime's fov changed by up to %.1f deg while sampling.", f.fovSpreadDeg));
        if (f.fovSpaceDiffDeg > 0.1f) h.push_back(Fmt("The fov differs by up to %.2f deg between the spaces it was located in.", f.fovSpaceDiffDeg));
    }
    if (f.recW[0] != f.recW[1] || f.recH[0] != f.recH[1])
        h.push_back(Fmt("The eyes' recommended pictures differ: %ux%u and %ux%u.", f.recW[0], f.recH[0], f.recW[1], f.recH[1]));
    if (f.frames > 0) {
        if (!f.stageListed)
            h.push_back("The runtime offers no STAGE space: the mod uses LOCAL (no floor).");
        else if (f.headStage.whole == 0 && f.headLocal.whole > 0)
            h.push_back(Fmt("STAGE gives the head nothing while LOCAL does (the Quest-over-Link case)%s.",
                            f.metaRuntime ? ": the mod takes LOCAL as the room here ([openxr] room_by_head=2)" : ": [openxr] room_by_head=1 takes LOCAL as the room"));
        else if (f.headStage.whole < f.headStage.frames)
            h.push_back(Fmt("STAGE located the head in %d of %d frames only.", f.headStage.whole, f.headStage.frames));
        if (f.headLocal.whole == 0) h.push_back("LOCAL gives the head nothing either: the headset is not tracking (asleep, or tracking lost).");
    }
    if (f.frames > 0 && f.handsLooked) {
        for (int hd = 0; hd < 2; ++hd) {
            const char* side = hd ? "right" : "left";
            if (f.profile[hd].empty()) {
                h.push_back(Fmt("No controller profile for the %s hand: the controller was off, asleep or not seen.", side));
                continue;
            }
            const Located& gs = f.grip[hd][1];
            const Located& gl = f.grip[hd][0];
            const Located& as = f.aim[hd][1];
            if (f.stageListed && gs.whole == 0 && (gl.whole > 0 || as.whole > 0))
                h.push_back(Fmt("The %s grip has no whole pose in STAGE while %s does (the hands-at-the-waist case: [openxr] hand_space mends it%s).", side,
                                gl.whole > 0 ? "LOCAL" : "the aim", f.metaRuntime ? ", on by default here" : " with hand_space=1"));
            else if (gl.whole == 0 && f.grip[hd][0].frames > 0)
                h.push_back(Fmt("The %s grip was never located whole (the controller in view? awake?).", side));
        }
    } else if (f.frames > 0) {
        h.push_back("The controllers were not read: the session never had the focus (another VR app or the runtime's menu in front?).");
    }
    if (h.empty()) h.push_back("Nothing unusual in the numbers above.");
    return h;
}

}  // namespace headsetreport
