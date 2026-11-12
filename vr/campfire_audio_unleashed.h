// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem, Kudlatytrue and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed: steamvr/campfire_audio.h at 71efaa3 (Kudlatytrue, 3599cf4 .. 71efaa3, in PR #518) taken as it is - the fire's
// own sound made here, no recording; below it the waveOut stream it plays on (steamvr/transport.cpp HeartStream, cc39009) and the game's
// volume (vr/combat.cpp GameCombatVolume: Settings.json's Master x the audio preset's Combat). Used by vr/campfire_unleashed.cpp
// ([campfire] fire_sound). The rest of D2R VR Authentic is MIT; this file stays MPL 2.0.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// The campfire's own sound (owner 2026-11-08: "dźwięk paleniska"), made here, no recording: a low
// roar of the flames with crackles over it while the character screen's fire burns, and on a click
// at the fire (vrcam CampfirePick) a whoosh with a flurry of crackles - the sparks' sound.
// Value-only: the transport streams it on its own waveOut stream (HeartStream), mono 16-bit.
namespace framevr::fireaudio {
constexpr int Rate=44100;

struct Synth {
    uint32_t rng=0x2545F491u;
    float roarLow=0,roarMid=0,roarMid2=0,flicker=.6f,flickerTo=.6f;int flickerLeft=0;
    // crackles: a few clicks in a row, each a short decaying burst of bright noise
    int clicksLeft=0,clickGap=0;float clickAmp=0,clickEnv=0,clickDecay=.9f,clickHp=0;
    float burst=0;double burstT=99;     // the whoosh, seconds since the click
    float whooshLp=0,whooshLp2=0,burstLevel=1;

    float Noise(){rng=rng*1664525u+1013904223u;return float(rng>>9)/float(1u<<23)*2.f-1.f;}
    float Uniform(){rng=rng*1664525u+1013904223u;return float(rng>>8)/float(1u<<24);}
    void Burst(){burstT=0;}

    // frames of mono samples; ambient and burst gains 0..1 (the game's volume already in them)
    void Render(std::vector<int16_t>& out,int frames,float ambient,float burstGain) {
        out.resize(size_t(frames));
        burstLevel=std::max(0.f,burstGain/std::max(ambient,1e-4f));   // fire_burst_volume against fire_volume: how big the flurry
        for(int i=0;i<frames;++i) {
            // the roar: brownish noise, its loudness wandering like the flames
            const float n=Noise();
            roarLow+=.012f*(n-roarLow);roarMid+=.09f*(n-roarMid);roarMid2+=.03f*(roarMid-roarMid2);
            if(--flickerLeft<=0){flickerLeft=int(Rate*(.05f+.25f*Uniform()));flickerTo=.35f+.65f*Uniform();}
            flicker+=.0004f*(flickerTo-flicker);
            float s=(roarLow*1.6f+(roarMid-roarMid2)*.35f)*flicker*.06f;   // only a breath of the flames: the crackle carries it

            // crackles: on average ~5 a second, many more just after a click
            const double t=burstT;
            // a pine cone in the fire: it catches over half a second, crackles hard, then settles over ~3 s
            const float boost=t<3.5?float((t/0.35)*std::exp(1.0-t/0.35))*30.f:0.f;
            if(clicksLeft<=0&&Uniform()<(7.f+boost*3.f*burstLevel)/Rate) {
                clicksLeft=1+int(Uniform()*4);clickGap=0;const float u=Uniform();clickAmp=(.05f+.45f*u*u*u)*(1.f+boost*.03f*burstLevel);
            }
            if(clicksLeft>0&&--clickGap<=0) {
                clickEnv=clickAmp*(.5f+.5f*Uniform());clickDecay=.80f+.15f*Uniform();
                clickGap=int(Rate*(.002f+.014f*Uniform()));--clicksLeft;
            }
            const float raw=Noise()*clickEnv;clickEnv*=clickDecay;
            clickHp+=.35f*(raw-clickHp);
            const float click=raw-clickHp;   // high-passed: a dry, sharp crack

            float amb=(s+click*.9f)*ambient;   // (no low pops: they thudded like a little drum)

            // a click: no whoosh of its own - only the fire's own crackle, a great deal of it at once (boost)
            const float wh=0.f;
            burstT+=1.0/Rate;
            const float v=amb+wh*burstGain;
            out[size_t(i)]=int16_t(std::lround(std::clamp(std::tanh(v*1.2f),-1.f,1.f)*32000.f));
        }
    }
};
}

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#include <cstdio>
#include <deque>
#include <memory>
#include <string>

namespace framevr::fireaudio {

// A waveOut stream fed short buffers (Unleashed's HeartStream): winmm loaded at the first use, never linked.
class Stream {
    using OpenFn = MMRESULT(WINAPI*)(LPHWAVEOUT, UINT, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR, DWORD);
    using HeaderFn = MMRESULT(WINAPI*)(HWAVEOUT, LPWAVEHDR, UINT);
    using PositionFn = MMRESULT(WINAPI*)(HWAVEOUT, LPMMTIME, UINT);
    using HandleFn = MMRESULT(WINAPI*)(HWAVEOUT);
    OpenFn open = nullptr; HeaderFn prepare = nullptr, unprepare = nullptr, write = nullptr; PositionFn position = nullptr; HandleFn reset = nullptr, close = nullptr;
    bool loaded = false;
    HWAVEOUT out = nullptr; int rate = 0, channels = 0;
    uint64_t queuedEnd = 0;   // frames written since open
    struct Buffer { WAVEHDR header{}; std::vector<int16_t> pcm; };
    std::deque<std::unique_ptr<Buffer>> buffers;
    bool Load() {
        if (loaded) return open != nullptr;
        loaded = true;
        if (HMODULE m = LoadLibraryW(L"winmm.dll")) {
            open = reinterpret_cast<OpenFn>(GetProcAddress(m, "waveOutOpen")); prepare = reinterpret_cast<HeaderFn>(GetProcAddress(m, "waveOutPrepareHeader"));
            unprepare = reinterpret_cast<HeaderFn>(GetProcAddress(m, "waveOutUnprepareHeader")); write = reinterpret_cast<HeaderFn>(GetProcAddress(m, "waveOutWrite"));
            position = reinterpret_cast<PositionFn>(GetProcAddress(m, "waveOutGetPosition"));
            reset = reinterpret_cast<HandleFn>(GetProcAddress(m, "waveOutReset")); close = reinterpret_cast<HandleFn>(GetProcAddress(m, "waveOutClose"));
        }
        if (!(open && prepare && unprepare && write && position && reset && close)) open = nullptr;
        return open != nullptr;
    }
    uint64_t Played() {
        MMTIME t{}; t.wType = TIME_SAMPLES;
        if (!out || position(out, &t, sizeof t) != MMSYSERR_NOERROR || t.wType != TIME_SAMPLES) return queuedEnd;
        return t.u.sample;
    }
public:
    void Reap() {
        while (!buffers.empty() && (buffers.front()->header.dwFlags & WHDR_DONE)) {
            unprepare(out, &buffers.front()->header, sizeof(WAVEHDR)); buffers.pop_front();
        }
    }
    uint64_t Remaining() { if (!out || buffers.empty()) return 0; const auto p = Played(); return queuedEnd > p ? queuedEnd - p : 0; }
    bool Queue(int sourceRate, int sourceChannels, std::vector<int16_t>&& pcm) {
        if (pcm.empty() || !Load()) return false;
        if (out && (sourceRate != rate || sourceChannels != channels)) Stop();
        if (!out) {
            WAVEFORMATEX f{}; f.wFormatTag = WAVE_FORMAT_PCM; f.nChannels = WORD(sourceChannels); f.nSamplesPerSec = DWORD(sourceRate);
            f.wBitsPerSample = 16; f.nBlockAlign = WORD(sourceChannels * 2); f.nAvgBytesPerSec = f.nSamplesPerSec * f.nBlockAlign;
            if (open(&out, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { out = nullptr; return false; }
            rate = sourceRate; channels = sourceChannels; queuedEnd = 0;
        }
        auto b = std::make_unique<Buffer>(); b->pcm = std::move(pcm);
        b->header.lpData = reinterpret_cast<LPSTR>(b->pcm.data()); b->header.dwBufferLength = DWORD(b->pcm.size() * sizeof(int16_t));
        if (prepare(out, &b->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) return false;
        if (write(out, &b->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) { unprepare(out, &b->header, sizeof(WAVEHDR)); return false; }
        queuedEnd += b->pcm.size() / size_t(channels); buffers.push_back(std::move(b));
        return true;
    }
    bool Open() const { return out != nullptr; }
    void Stop() {
        if (!out) return;
        reset(out);   // marks every queued buffer done
        for (auto& b : buffers) unprepare(out, &b->header, sizeof(WAVEHDR));
        buffers.clear(); close(out); out = nullptr; rate = channels = 0; queuedEnd = 0;
    }
    ~Stream() { Stop(); }
};

// D2R's Settings.json: "Master Volume" x the active "Advanced Audio Preset"'s Combat slider (0..1). Read again every 2 s at most.
inline float GameVolume() {
    static ULONGLONG checkAt = 0; static float volume = -1.0f;
    const ULONGLONG now = GetTickCount64();
    if (volume >= 0.0f && now < checkAt) return volume;
    checkAt = now + 2000;
    wchar_t home[MAX_PATH];
    if (!GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH)) return volume = 1.0f;
    const std::wstring path = std::wstring(home) + L"\Saved Games\Diablo II Resurrected\Settings.json";
    std::string text;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") == 0 && f) { char buf[4096]; size_t n; while ((n = fread(buf, 1, sizeof buf, f)) > 0 && text.size() < 256 * 1024) text.append(buf, n); fclose(f); }
    auto number = [&](const std::string& key, float fallback) {
        const auto at = text.find("\"" + key + "\""); if (at == std::string::npos) return fallback;
        const auto colon = text.find(':', at); if (colon == std::string::npos) return fallback;
        return float(atof(text.c_str() + colon + 1));
    };
    const int preset = int(number("Advanced Audio Preset", 0));
    const float master = number("Master Volume", 100), combat = number("Audio AdvancedCombat Preset " + std::to_string(preset), 100);
    return volume = std::clamp(master / 100.f, 0.f, 1.f) * std::clamp(combat / 100.f, 0.f, 1.f);
}

}  // namespace framevr::fireaudio
