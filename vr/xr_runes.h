// BodyWalk's gesture runes in the headset ([openxr] runes_draw) - the OpenXR side of vr/rune_geom.h and vr/rune_shader.h.
//
// NOT a header of its own: xr.cpp includes this file once, inside namespace xr { namespace {, after xr_skill_wheel.h, because it uses what
// is in xr.cpp's unnamed namespace (the session, the copy list, the swapchain helpers, MakeHudPipeline's descriptor heaps). What a rune
// looks like, where its picture and its trail go and how many layers they may take is in vr/rune_*.h (pure, tested); this only draws.
//
// The engine outside the game (BodyWalk) recognises the shape drawn in the air and writes, per rune on show, its picture on a panel in
// the room and the hand's trail (link::VrRunes). Per rune: ONE quad layer for the picture, at the block's place; ONE head-facing
// billboard for the trail, the room polyline projected into it from the head. One atlas swapchain holds all of them (the pictures on
// its top row, the trails below), a cell each, drawn by one pixel shader (vr/rune_shader.h) from a segment list in a constant buffer
// - no CPU painting: a glowing line at the headset's rate costs the GPU a few thousand pixels. Up to 4 runes, 8 layers. The layers
// are in the stage space the engine placed them in (g_roomSpace), as the gesture zones' are.
//
// F4 only, off unless [openxr] runes_draw=1; nothing at all is read or drawn otherwise. A block that is missing is nothing: no error.

static_assert(kRuneLayers == runes::kLayers, "xr.cpp's layer room for the runes is what the geometry may ask for");

constexpr uint32_t kRuneRing = 4;   // frames of constant buffers in flight: one slot per cell, kRuneRing x 8 x kCbSlot bytes
ID3D12RootSignature* g_runeRs = nullptr;
ID3D12PipelineState* g_runePso = nullptr;
ID3D12Resource* g_runeCb = nullptr;      // the constant buffers, an upload heap kept mapped
uint8_t* g_runeCbMap = nullptr;
UINT64 g_runeCbDone[kRuneRing] = {};     // the fence value after which a ring slot is free again
uint32_t g_runeRing = 0;
bool g_runeFailed = false;
HudOut g_runeOut;                        // the atlas
XrCompositionLayerQuad g_runeLayer[kRuneLayers];   // pictures 0..3, trails 4..7 (by the rune's place in the list)
runes::Ease g_runeEase;
bool g_runeTold = false;

// The pipeline: the descriptor heaps MakeHudPipeline makes for the zones' atlas, a root signature of one constant buffer view, the
// rune shader. Made once; kept for the life of the game, as the HUD's.
bool MakeRunePipeline() {
    if (g_runePso && g_runeCb) return true;
    if (g_runeFailed) return false;
    if (!MakeHudPipeline()) return false;   // (its heaps; a game with no shader compiler has none of this)
    g_runeFailed = true;                    // until it is all made
    HMODULE dc = LoadLibraryW(L"d3dcompiler_47.dll");
    const pD3DCompile compile = dc ? (pD3DCompile)GetProcAddress(dc, "D3DCompile") : nullptr;
    using SerializeFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
    HMODULE d12 = GetModuleHandleW(L"d3d12.dll");
    const SerializeFn serialize = d12 ? (SerializeFn)GetProcAddress(d12, "D3D12SerializeRootSignature") : nullptr;
    if (!compile || !serialize) { Log("vrcam: openxr - gesture runes: no shader compiler (d3dcompiler_47.dll) - no runes in the headset"); return false; }
    const char* const src = runes::Hlsl();
    ID3DBlob* vs = nullptr; ID3DBlob* ps = nullptr;
    const char* const entry[2] = {"VSMain", "PSRune"};
    ID3DBlob** const blob[2] = {&vs, &ps};
    for (int i = 0; i < 2; ++i) {
        ID3DBlob* err = nullptr;
        if (FAILED(compile(src, strlen(src), "d2rvr_runes", nullptr, nullptr, entry[i], i ? "ps_5_0" : "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob[i], &err))) {
            LogF("vrcam: openxr - gesture runes: %s did not compile: %s", entry[i], err ? (const char*)err->GetBufferPointer() : "?");
            if (err) err->Release();
            if (vs) vs->Release();
            if (ps) ps->Release();
            return false;
        }
        if (err) err->Release();
    }
    D3D12_ROOT_PARAMETER rp{};
    rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rp.Descriptor = {0, 0};
    rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    const D3D12_ROOT_SIGNATURE_DESC rsd{1, &rp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ID3DBlob* rsBlob = nullptr; ID3DBlob* rsErr = nullptr;
    bool ok = SUCCEEDED(serialize(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr)) &&
              SUCCEEDED(g_dev->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)&g_runeRs));
    if (rsBlob) rsBlob->Release();
    if (rsErr) rsErr->Release();
    if (ok) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
        d.pRootSignature = g_runeRs;
        d.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        d.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;   // no blending: the cell is written whole
        d.SampleMask = UINT_MAX;
        d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        d.RasterizerState.DepthClipEnable = TRUE;
        d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        d.NumRenderTargets = 1;
        d.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        d.SampleDesc.Count = 1;
        ok = SUCCEEDED(g_dev->CreateGraphicsPipelineState(&d, __uuidof(ID3D12PipelineState), (void**)&g_runePso));
    }
    vs->Release();
    ps->Release();
    if (ok) {   // the constant buffers: one upload buffer, written by the CPU, read by the shader through a root CBV
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = (UINT64)kRuneRing * kRuneLayers * runes::kCbSlot;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_RANGE none{0, 0};
        void* mapped = nullptr;
        ok = SUCCEEDED(g_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                      __uuidof(ID3D12Resource), (void**)&g_runeCb)) &&
             SUCCEEDED(g_runeCb->Map(0, &none, &mapped));
        g_runeCbMap = (uint8_t*)mapped;
    }
    if (!ok) { Log("vrcam: openxr - gesture runes: the pipeline could not be made - no runes in the headset"); return false; }
    g_runeFailed = false;
    Log("vrcam: openxr - gesture runes: the shader compiled (BodyWalk's runes and trails, [openxr] runes_draw)");
    return true;
}

// Nothing of the runes is up (switched off, another view, the session over): the ease-out's memory goes too.
void RunesIdle() { g_runeEase.Reset(); }

// The runes' pictures and trails, hung in the room. Appended to layers[] from nl on, at most max.
uint32_t HangRunes(const XrCompositionLayerBaseHeader** layers, uint32_t nl, uint32_t max) {
    if (!g_cfg.runesDraw || !viewgate::F4()) { RunesIdle(); return nl; }   // F4 only: the other views draw nothing, as before
    BW_LinkVrRune got[runes::kMax];
    const uint32_t n = link::VrRunes(got, runes::kMax);
    BW_LinkVrRune show[runes::kMax];
    const uint32_t m = g_runeEase.Step(got, n, GetTickCount64(), show);   // a rune that vanished mid-flash eases out in 0.2 s
    if (!m || nl >= max || !g_inNow.head.valid) return nl;
    const runes::V3 head = runes::From3(g_inNow.head.pos);
    const XrVector3f look = Rot(Q4(g_inNow.head.rot), {0.0f, 0.0f, -1.0f});   // the way the head looks (stage space)
    const runes::V3 forward{look.x, look.y, look.z};

    struct Piece { bool picture = false, trail = false; runes::Look look; runes::Panel panel; float picDist = 0, trailDist = 0; };
    Piece piece[runes::kMax];
    static runes::TrailBoard board[runes::kMax];   // (1.5 KB each: not on the stack)
    uint32_t pictures = 0, trails = 0;
    for (uint32_t k = 0; k < m; ++k) {
        Piece& p = piece[k];
        p.look = runes::LookOf(show[k]);
        if (!p.look.shown) continue;
        if (p.look.rune.a > 0.003f || p.look.lit.a > 0.003f)
            if (runes::PicturePanel(show[k], p.look, &p.panel)) { p.picture = true; p.picDist = runes::Len(p.panel.centre - head); ++pictures; }
        if (p.look.trail.a > 0.003f && runes::ProjectTrail(show[k], p.look, head, forward, &board[k])) {
            p.trail = true;
            p.trailDist = runes::Len(board[k].centre - head);
            ++trails;
        }
    }
    if (!pictures && !trails) return nl;
    uint32_t keepPictures = 0, keepTrails = 0;
    runes::Budget(pictures, trails, max - nl, &keepPictures, &keepTrails);   // short of layers, the trails go first
    if (!keepPictures && !keepTrails) return nl;
    // which ones: the first of each in the engine's order; and laid farthest first, the pictures, then the trails over them
    uint32_t pic[runes::kMax], trl[runes::kMax], np = 0, nt = 0;
    for (uint32_t k = 0; k < m; ++k) {
        if (piece[k].picture && np < keepPictures) pic[np++] = k;
        if (piece[k].trail && nt < keepTrails) trl[nt++] = k;
    }
    std::sort(pic, pic + np, [&](uint32_t a, uint32_t b) { return piece[a].picDist > piece[b].picDist; });
    std::sort(trl, trl + nt, [&](uint32_t a, uint32_t b) { return piece[a].trailDist > piece[b].trailDist; });

    if (!MakeRunePipeline() || !g_fence) return nl;
    const uint32_t ring = g_runeRing % kRuneRing;
    if (g_fence->GetCompletedValue() < g_runeCbDone[ring]) return nl;   // the GPU is still reading this slot: not this frame
    HudOut& o = g_runeOut;
    if (!EnsureHudOut(o, runes::kAtlasW, runes::kAtlasH, "gesture runes")) return nl;
    uint32_t idx = 0;
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if (NextImage(o.sc, &idx, "gesture runes") <= 0) return nl;
    ID3D12GraphicsCommandList* l = OpenList();
    if (!l || idx >= o.img.size()) { xrReleaseSwapchainImage(o.sc, &ri); return nl; }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += (SIZE_T)(o.rtv0 + (int)idx) * g_rtvInc;
    l->SetGraphicsRootSignature(g_runeRs);
    l->SetPipelineState(g_runePso);
    l->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_GPU_VIRTUAL_ADDRESS cbBase = g_runeCb->GetGPUVirtualAddress();
    uint32_t made = 0;
    const XrSpace space = g_roomSpace ? g_roomSpace : g_space;
    // one cell: its constants into the ring slot, the whole cell drawn, the quad layer for it
    auto drawCell = [&](const runes::CellCb& cb, uint32_t cellCol, uint32_t cellRow, uint32_t layerAt, const runes::V3& centre, const runes::Quat& turn, float side) {
        const uint32_t slot = (ring * kRuneLayers + layerAt) * runes::kCbSlot;
        memcpy(g_runeCbMap + slot, &cb, sizeof cb);
        const float x = (float)(cellCol * runes::kCell), y = (float)(cellRow * runes::kCell), s = (float)runes::kCell;
        const D3D12_VIEWPORT vp{x, y, s, s, 0.0f, 1.0f};
        const D3D12_RECT sc{(LONG)x, (LONG)y, (LONG)(x + s), (LONG)(y + s)};
        l->RSSetViewports(1, &vp);
        l->RSSetScissorRects(1, &sc);
        l->SetGraphicsRootConstantBufferView(0, cbBase + slot);
        l->DrawInstanced(3, 1, 0, 0);
        XrCompositionLayerQuad& q = g_runeLayer[layerAt];
        q = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;   // premultiplied
        q.space = space;
        q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        q.subImage.swapchain = o.sc;
        q.subImage.imageRect = {{(int32_t)x, (int32_t)y}, {(int32_t)runes::kCell, (int32_t)runes::kCell}};
        q.pose.orientation = {turn.x, turn.y, turn.z, turn.w};
        q.pose.position = {centre.x, centre.y, centre.z};
        q.size = {side, side};
        layers[nl + made++] = (const XrCompositionLayerBaseHeader*)&q;
    };
    runes::CellCb cb;
    for (uint32_t i = 0; i < np; ++i) {
        const uint32_t k = pic[i];
        runes::FillPicture(&cb, show[k], piece[k].look, piece[k].panel, k);
        drawCell(cb, k, 0, k, piece[k].panel.centre, piece[k].panel.q, piece[k].panel.side);
    }
    for (uint32_t i = 0; i < nt; ++i) {
        const uint32_t k = trl[i];
        runes::FillTrail(&cb, piece[k].look, board[k], k);
        drawCell(cb, k, 1, runes::kMax + k, board[k].centre, board[k].q, board[k].side);
    }
    SubmitList();
    g_runeCbDone[ring] = g_fenceV;
    ++g_runeRing;
    if (!Ok(xrReleaseSwapchainImage(o.sc, &ri), "xrReleaseSwapchainImage(runes)")) return nl;
    if (!g_runeTold) { g_runeTold = true; LogF("vrcam: openxr - BodyWalk's gesture runes in the room: %u picture(s), %u trail(s)", np, nt); }
    return nl + made;
}

void RunesTeardown() {   // under g_lock or before the session exists: the atlas goes with the session, the pipeline stays like the HUD's
    if (g_runeOut.sc) { ForgetPending(g_runeOut.sc); xrDestroySwapchain(g_runeOut.sc); }
    g_runeOut = HudOut{};
    g_runeEase.Reset();
    for (UINT64& d : g_runeCbDone) d = 0;
}
