# reshadefx

ReShade's effect compiler (the ReShade FX preprocessor, parser and HLSL / DXBC code generator), copied unchanged from
crosire/reshade at commit 18deaa5 ("Fix parsing of floating-point literals with an exponent", 2026-08-02), `source/`:
effect_*.cpp / .hpp / .inl. The GLSL, SPIR-V, DXBC and DXIL generators are left out (D3DCompile is called by vr/fx_native.cpp). Licence: BSD-3-Clause,
LICENSE.md beside this file (Patrick Mours).

Used by D2R VR's `[render] no_reshade` path (vr/fx_native.cpp): reshade/D2R_DepthFog.fx is compiled to HLSL per entry point
and assembled with D3DCompile (d3dcompiler_47.dll, part of Windows), then run on the plugin's own D3D12 passes - with no
ReShade64.dll in the game.
