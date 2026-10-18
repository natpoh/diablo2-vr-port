// A picture the CPU paints, hung in the room as a quad layer - what the settings panel (vr/xr_headset_settings.h) uses and
// what any other panel in the headset (the skill wheel) can use the same way.
//
// NOT a header of its own: xr.cpp includes it once, inside namespace xr { namespace {, after HangLaser, because it uses what
// is in xr.cpp's unnamed namespace (the session, the copy list, the swapchain helpers). The pure side - the ray against a
// panel, the pad values and the release gate - is vr/hset_ray.h.
//
// What a panel needs from here, in the order it is used:
//   PoseIn(space, &pose)                     a located pose in the layers' space, as far as the runtime vouches for it
//   QuadAtlasUpload(A, w, h, pixels, "name") the next picture (premultiplied RGBA, R in the low byte, sRGB) into A's swapchain
//   QuadLayerOf(layer, A, pose, size)        the quad layer that shows it (A.shown first)
//   HangBeam(BeamView, ...)                  (xr.cpp) the beam and the dot from a hand to the panel
//   QuadAtlasDestroy(A)                      with the session (Teardown)
// Hand it a frame of input through Publish (xr.cpp): take the pads you own out of `in` there, as HsStep does.

struct QuadAtlas {
    XrSwapchain sc = XR_NULL_HANDLE;
    std::vector<XrSwapchainImageD3D12KHR> img;
    ID3D12Resource* upload[3] = {};   // three buffers in turn: one is written again only after its copy is done
    UINT64 done[3] = {};
    int at = 0;
    uint32_t w = 0, h = 0;
    bool shown = false;               // an image was released: the layer may name the swapchain
    ULONGLONG retryAt = 0;            // a failed make waits a moment
};

// A pose in the layers' space (g_space), only as far as the runtime vouches for it: both the turn and the place valid, finite,
// the turn made a unit quaternion.
bool PoseIn(XrSpace sp, hset::Pose* out) {
    *out = hset::Pose{};
    if (!sp) return false;
    XrSpaceLocation l{XR_TYPE_SPACE_LOCATION};
    const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if (XR_FAILED(xrLocateSpace(sp, g_space, g_fs.predictedDisplayTime, &l)) || (l.locationFlags & need) != need) return false;
    const XrQuaternionf& q = l.pose.orientation;
    const XrVector3f& p = l.pose.position;
    const float n2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (!std::isfinite(n2) || n2 < 0.5f || n2 > 2.0f || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
    const float k = 1.0f / sqrtf(n2);
    out->p = {p.x, p.y, p.z};
    out->q = {q.x * k, q.y * k, q.z * k, q.w * k};
    out->valid = true;
    return true;
}

// The picture into the next image of A's swapchain, on the game's queue. The swapchain is made at the first call (and again
// when the size changes). False: not this time (its wait ran out, a copy is still running, no format) - ask again later.
bool QuadAtlasUpload(QuadAtlas& A, uint32_t w, uint32_t h, const std::vector<uint32_t>& px, const char* what) {
    if (GetTickCount64() < A.retryAt || !MakeCopyList() || px.size() < (size_t)w * h) return false;
    char b[96];
    if (A.sc && (A.w != w || A.h != h)) {   // another size: made anew
        ForgetPending(A.sc);
        SettleGpu();
        gpufault::Note("xr: quad atlas swapchain destroyed", (uint64_t)(uintptr_t)A.sc, ((uint64_t)A.w << 32) | A.h);
        xrDestroySwapchain(A.sc);
        A.sc = XR_NULL_HANDLE;
        A.shown = false;
        for (int i = 0; i < 3; ++i) if (A.upload[i]) { Retire(A.upload[i]); A.done[i] = 0; }   // (not released under a copy that may not have run)
    }
    if (!A.sc) {
        int64_t fmt = 0;
        for (int64_t f : g_formats) if (f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) fmt = f;
        if (!fmt) {
            A.retryAt = GetTickCount64() + 5000;
            LogF("vrcam: openxr - %s: the runtime has no R8G8B8A8 sRGB swapchain format - no picture", what);
            return false;
        }
        XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        ci.format = fmt;
        ci.sampleCount = 1;
        ci.width = w;
        ci.height = h;
        ci.faceCount = 1;
        ci.arraySize = 1;
        ci.mipCount = 1;
        snprintf(b, sizeof b, "xrCreateSwapchain(%s)", what);
        if (!Ok(xrCreateSwapchain(g_session, &ci, &A.sc), b)) { A.sc = XR_NULL_HANDLE; A.retryAt = GetTickCount64() + 1000; return false; }
        uint32_t n = 0;
        xrEnumerateSwapchainImages(A.sc, 0, &n, nullptr);
        A.img.assign(n, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
        xrEnumerateSwapchainImages(A.sc, n, &n, (XrSwapchainImageBaseHeader*)A.img.data());
        A.w = w;
        A.h = h;
    }
    const int k = A.at;
    if (g_fence && g_fence->GetCompletedValue() < A.done[k]) return false;   // its last copy is still running: next time
    const UINT pitch = (w * 4 + 255) & ~255u;
    if (!A.upload[k]) {
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = (UINT64)pitch * h;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(g_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                  __uuidof(ID3D12Resource), (void**)&A.upload[k]))) {
            A.upload[k] = nullptr;
            A.retryAt = GetTickCount64() + 1000;
            return false;
        }
        A.upload[k]->SetName(L"d2rvr quad atlas upload");
    }
    uint8_t* dst = nullptr;
    D3D12_RANGE none{0, 0};
    if (FAILED(A.upload[k]->Map(0, &none, (void**)&dst))) return false;
    for (UINT y = 0; y < h; ++y) memcpy(dst + (size_t)y * pitch, px.data() + (size_t)y * w, (size_t)w * 4);
    A.upload[k]->Unmap(0, nullptr);
    uint32_t idx = 0;
    if (NextImage(A.sc, &idx, what) <= 0) return false;   // (its wait ran out: again next time)
    A.at = (k + 1) % 3;
    bool copied = false;
    if (ID3D12GraphicsCommandList* l = OpenList()) {
        ID3D12Resource* t = A.img[idx].texture;
        D3D12_RESOURCE_BARRIER rb = Tr(t, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
        l->ResourceBarrier(1, &rb);
        D3D12_TEXTURE_COPY_LOCATION to{}, from{};
        to.pResource = t;
        to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        from.pResource = A.upload[k];
        from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        from.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, w, h, 1, pitch};
        l->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        rb = Tr(t, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
        l->ResourceBarrier(1, &rb);
        SubmitList();
        A.done[k] = g_fenceV;
        copied = true;
    }
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    snprintf(b, sizeof b, "xrReleaseSwapchainImage(%s)", what);
    if (!Ok(xrReleaseSwapchainImage(A.sc, &ri), b)) return false;
    if (!copied) return false;   // no list could be opened (the GPU is behind): the picture is not on the image - painted again next time, not taken for shown
    A.shown = true;
    return true;
}

// The quad layer that shows A, standing at `pose` (in g_space), `size` metres.
void QuadLayerOf(XrCompositionLayerQuad& q, const QuadAtlas& A, const hset::Pose& pose, float widthM, float heightM) {
    q = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    q.space = g_space;
    q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    q.subImage.swapchain = A.sc;
    q.subImage.imageRect = {{0, 0}, {(int32_t)A.w, (int32_t)A.h}};
    q.pose.orientation = {pose.q.x, pose.q.y, pose.q.z, pose.q.w};
    q.pose.position = {pose.p.x, pose.p.y, pose.p.z};
    q.size = {widthM, heightM};
}

// With the session (Teardown, before the swapchains' space goes). The game's queue was waited for: the buffers are free.
void QuadAtlasDestroy(QuadAtlas& A) {
    if (A.sc) { ForgetPending(A.sc); xrDestroySwapchain(A.sc); }
    for (ID3D12Resource*& u : A.upload) if (u) { u->Release(); u = nullptr; }
    A = QuadAtlas{};
}
