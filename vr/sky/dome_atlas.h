// dome_atlas.h - [sky] dome_in_game_colour: the game draws the dome in our sky's colours itself. Serg on 0.192.8 / 0.192.12 (92.webp,
// 98.png: "почему до сих пор контур"): our effect recoloured the dome's pixels after the game had blended the trees' and the palisade's
// edges into the bright, uncorrected dome - a light outline round every branch at night, which no pass over the finished picture can take
// back. So the game samples a texture of ours in the dome's own draw: the game makes one view of the domes' picture (shared/sky_atlas.h, the
// atlas - 4096 x 6144 BC6H, 9 mips on PC1, 0.192.9's probe), and that view is made on our texture instead (StandIn, from the plugin's
// CreateShaderResourceView hook). Ours is baked on the graphics card from the game's own atlas, texel for texel:
//     the act's day picture (the atlas) mixed with its night picture (the effect's SkyNBand / SkyNCap, laid round as make_sky_dome.py lays the
//     day one, in the same scene units) by the night's share, times the brightness ([sky] day_night's dimming and bright_<period>), times
//     the gain our effect measures (DomeSeen: what the game shows of the dome against what our sky would be there)
// - the same numbers the effect's DomeLookOf used on the finished picture, now before the trees are drawn over it. The gain the bake used
// goes back to the effect (BakedGain, D2RVR_DOME_GAIN): it measures the dome as baked, so its next gain is the baked one times what is
// still off.
//
// Only the bands the game shows are backed by memory (a reserved texture, ~22 MiB at 4096 wide: the act's and the cave's band, every mip),
// where the card reads unbacked tiles as zero (tiled resources tier 2 or more); else the whole texture (~134 MiB). R11G11B10_FLOAT.
// Without the plugin's own effects (ReShade's path, [render] native_effects=0) nothing stands in: the game's own view, as before.
#pragma once

#include <d3d12.h>

#include <cstdint>

namespace domeatlas {

using LogFn = void (*)(const char*);
void SetLog(LogFn log);

// The original CreateShaderResourceView (the hook's own: no recursion)
using CreateSrvFn = void(STDMETHODCALLTYPE*)(ID3D12Device*, ID3D12Resource*, const D3D12_SHADER_RESOURCE_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);

// The game makes a view of the atlas (the hook: the game's thread). While the key is on and our bake runs (Present seen lately on this
// device, its pipeline made), our texture's view is made there instead - true: done, the caller makes none. Only in this call: nothing of
// ours is written into the game's slots later (one may hold another view by then, or its heap be gone) - the key ticked live counts from the
// game's next view of the picture.
bool StandIn(ID3D12Device* dev, ID3D12Resource* atlas, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE slot,
             CreateSrvFn create);

struct BakeIn {
    bool on = false;                  // [sky] dome_in_game_colour (unticked live: ours baked as the game's own picture, texel for texel)
    int band = -1;                    // the act's band in the atlas (0..4, shared/sky_atlas.h kBands), -1: no act's dome shown (left as baked)
    float night = 0.0f;               // the night picture's share (the effect's DomeNightMix)
    float mul = 1.0f;                 // the brightness (DomeDayMul: day_night's dimming without a night picture, times bright_<period>)
    bool capOn = true;                // the act has its zenith picture (SkyCapOn); else the zenith colour
    float zenith[3] = {0.10f, 0.13f, 0.18f};   // (SkyZenith, as the picture has it: sRGB)
    ID3D12Resource* nightBand = nullptr;   // the act's night pictures as the effect has them (RGBA8, PIXEL_SHADER_RESOURCE); null: no night
    ID3D12Resource* nightCap = nullptr;
    ID3D12Resource* gain = nullptr;   // the effect's measured gain (DomeSeenTex: 1 x 1 RGBA16F, a = measured); null: 1
    // [sky] dome_gain_once: the gain as measured once for this area and period, not the effect's texture (gain then null)
    bool gainFixed = false;
    float gainRgb[3] = {1.0f, 1.0f, 1.0f};
    // [sky] dome_fog_baked: the dome's low part laid into the game's fog colour (scene units) as our old sky's is (D2R_DepthFog.fx
    // SkyFogHorizon: 1 - smoothstep(-0.03, 0.2, the direction's height)), times fogBand (0: none)
    float fog[3] = {0.0f, 0.0f, 0.0f};
    float fogBand = 0.0f;
};
// Each present, on our list on the game's queue `q`, before it is submitted: the tiles of the shown bands mapped (on q, so before the list),
// then baked when anything changed (or every kGainEvery presents while the gain is measured). Recorded only on `l`.
void Present(ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12GraphicsCommandList* l, const BakeIn& in);

// The game samples our texture now and it has been baked (the effect leaves the dome's colours to it: DomeInGame)
bool Live();
// The game's last view of the picture was made on ours (baked or not yet): a list is wanted for Present each present
bool Ours();
// The gain the last bake used (1 x 1 RGBA16F, a = 1 once baked; PIXEL_SHADER_RESOURCE): the effect's D2RVR_DOME_GAIN. Null before.
ID3D12Resource* BakedGain();
// [sky] dome_gain_once: the measured gain a bake used, read back a few presents after it (each bake with the effect's texture is copied out);
// false before any. `reads` counts them: a new one when it grows.
bool MeasuredGain(float rgb[3], uint32_t* reads);
// Ours (the test reads it back; nullptr before the game's first view of the picture)
ID3D12Resource* OurTexture();

// ---- pure (tested in tools/dome_atlas_test.cpp)
// Band `band`'s texel rows at mip `mip` of an atlas `bandRows` rows a band at mip 0: [*from, *to)
inline void BandRows(int band, uint32_t bandRows, uint32_t mip, uint32_t* from, uint32_t* to) {
    const uint32_t r = bandRows >> mip;
    *from = (uint32_t)band * r;
    *to = *from + r;
}
// Baked again for the effect's measured gain (it eases slowly): every kGainEvery presents, only while an act's sky is shown (`shownBand`
// 0..4, BakeIn::band) - Serg on 0.192.18: "в пещерах не надо цвет измерять", "только там, где есть небо", "можно не 12 кадров, а в 120
// раз". In caves and dungeons nothing is measured (the effect's DomeSeen keeps its last), the cave dome's band is black, cleared once. A
// change of the period's numbers (night, brightness, the act) bakes at once, as before.
constexpr int kGainEvery = 120;
inline bool RebakeForGain(bool gainOn, int shownBand, int sinceBake) { return gainOn && shownBand >= 0 && shownBand < 5 && sinceBake >= kGainEvery; }
// ... and the tile rows they touch, tiles `tileRows` texels high: [*from, *to)
inline void BandTileRows(int band, uint32_t bandRows, uint32_t mip, uint32_t tileRows, uint32_t* from, uint32_t* to) {
    uint32_t a = 0, b = 0;
    BandRows(band, bandRows, mip, &a, &b);
    *from = a / tileRows;
    *to = b > a ? (b + tileRows - 1) / tileRows : *from;
}

}  // namespace domeatlas
