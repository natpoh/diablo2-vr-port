// depth_net.h - [cinema] video_3d: the depth network on ONNX Runtime + DirectML, loaded at run time.
//
// Nothing links against ONNX Runtime: onnxruntime.dll and DirectML.dll are loaded by their full paths from the folder given (the
// installer puts them in d2rloader\d2r_vr_libs with the network), and the API is asked for by version. Windows 11 carries older copies
// of both in System32; a missing, older or foreign runtime leaves the videos flat and is said once in the log - it never stops the plugin.
// The C API only (third_party/onnxruntime/include, MIT), so no C++ wrapper reaches for an import either.
// Our own code (D2R VR, MIT).
#pragma once

#include <string>
#include <vector>

namespace video3d {

using LogFn = void (*)(const char*);

class DepthNet {
public:
    // Loads the runtime from `dir` (ending in a backslash) and the network `model` (a full path) on the DXGI adapter `adapter`.
    // The network must take one RGB picture 0..1 as NHWC [1, h, w, 3] and give one map [1, h, w] (larger = nearer). True when ready.
    bool Load(const std::wstring& dir, const std::wstring& model, int adapter, LogFn log);
    bool Ready() const { return session_ != nullptr; }
    int Width() const { return w_; }
    int Height() const { return h_; }
    // One picture (w*h*3 floats, NHWC) in, the map (w*h) out. False on a failure (said in the log, once in a while).
    bool Run(const std::vector<float>& nhwc, std::vector<float>& out);
    void Unload();
    ~DepthNet() { Unload(); }

private:
    void* api_ = nullptr;        // const OrtApi*
    void* env_ = nullptr;        // OrtEnv*
    void* session_ = nullptr;    // OrtSession*
    void* memInfo_ = nullptr;    // OrtMemoryInfo*
    std::string inName_, outName_;
    int w_ = 0, h_ = 0;
    LogFn log_ = nullptr;
    int failures_ = 0;
};

// The runtime's folder: d2rloader\d2r_vr_libs beside the plugin's own folder, or the plugin's folder itself; "" when neither has
// onnxruntime.dll. Ends in a backslash.
std::wstring RuntimeFolder();

}  // namespace video3d
