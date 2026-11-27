// depth_net.cpp - see depth_net.h.

#include "depth_net.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <cstring>

#include "onnxruntime_c_api.h"
#include "dml_provider_factory.h"

namespace video3d {

namespace {

void Anchor() {}

std::wstring ModulePath(HMODULE m) {
    wchar_t b[MAX_PATH * 2] = {};
    const DWORD n = m ? GetModuleFileNameW(m, b, (DWORD)(sizeof b / sizeof b[0])) : 0;
    return std::wstring(b, n);
}

bool StartsWith(const std::wstring& path, const std::wstring& dir) {
    return !dir.empty() && path.size() >= dir.size() && _wcsnicmp(path.c_str(), dir.c_str(), dir.size()) == 0;
}

std::string Narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s += (c > 0 && c < 128) ? (char)c : '?';
    return s;
}

}  // namespace

std::wstring RuntimeFolder() {
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&Anchor, &mod)) return L"";
    const std::wstring p = ModulePath(mod);
    const size_t slash = p.find_last_of(L"\\/");
    const std::wstring dir = slash == std::wstring::npos ? std::wstring() : p.substr(0, slash + 1);
    for (const std::wstring& d : {dir + L"..\\d2r_vr_libs\\", dir})
        if (GetFileAttributesW((d + L"onnxruntime.dll").c_str()) != INVALID_FILE_ATTRIBUTES) return d;
    return L"";
}

bool DepthNet::Load(const std::wstring& dir, const std::wstring& model, int adapter, LogFn log) {
    log_ = log;
    char line[700];
    auto say = [&](const char* s) { if (log_) log_(s); };
    if (session_) return true;
    if (GetFileAttributesW(model.c_str()) == INVALID_FILE_ATTRIBUTES) {
        snprintf(line, sizeof line, "vrcam: video3d - no depth network (%s): the videos stay flat", Narrow(model).c_str());
        say(line);
        return false;
    }
    // DirectML first, by its full path: ONNX Runtime asks for it by name, and finds ours already in the process instead of System32's.
    if (!GetModuleHandleW(L"DirectML.dll")) LoadLibraryExW((dir + L"DirectML.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    HMODULE ort = LoadLibraryExW((dir + L"onnxruntime.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!ort) {
        snprintf(line, sizeof line, "vrcam: video3d - onnxruntime.dll could not be loaded from %s (error %lu): the videos stay flat",
                 Narrow(dir).c_str(), GetLastError());
        say(line);
        return false;
    }
    using GetApiBaseFn = const OrtApiBase*(ORT_API_CALL*)(void);
    const auto getBase = (GetApiBaseFn)GetProcAddress(ort, "OrtGetApiBase");
    const OrtApiBase* base = getBase ? getBase() : nullptr;
    const OrtApi* api = base ? base->GetApi(ORT_API_VERSION) : nullptr;
    if (!api) {
        snprintf(line, sizeof line, "vrcam: video3d - %s (%s) does not speak ONNX Runtime API %d: the videos stay flat",
                 Narrow(ModulePath(ort)).c_str(), base ? base->GetVersionString() : "?", (int)ORT_API_VERSION);
        say(line);
        return false;
    }
    const std::wstring dml = ModulePath(GetModuleHandleW(L"DirectML.dll"));
    if (!StartsWith(dml, dir)) {
        snprintf(line, sizeof line, "vrcam: video3d - note: DirectML.dll is %s, not the one in %s", dml.empty() ? "(none yet)" : Narrow(dml).c_str(),
                 Narrow(dir).c_str());
        say(line);
    }
    api_ = (void*)api;

    // Each call's status: false and the reason in the log.
    auto ok = [&](OrtStatus* s, const char* what) {
        if (!s) return true;
        snprintf(line, sizeof line, "vrcam: video3d - %s failed: %s - the videos stay flat", what, api->GetErrorMessage(s));
        api->ReleaseStatus(s);
        say(line);
        return false;
    };
    OrtEnv* env = nullptr;
    OrtSessionOptions* so = nullptr;
    OrtSession* session = nullptr;
    OrtMemoryInfo* mem = nullptr;
    bool good = ok(api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "d2r_vr_video3d", &env), "CreateEnv") &&
                ok(api->CreateSessionOptions(&so), "CreateSessionOptions") &&
                ok(api->DisableMemPattern(so), "DisableMemPattern") &&
                ok(api->SetSessionExecutionMode(so, ORT_SEQUENTIAL), "SetSessionExecutionMode") &&
                ok(api->SetSessionGraphOptimizationLevel(so, ORT_ENABLE_ALL), "SetSessionGraphOptimizationLevel") &&
                ok(api->SetIntraOpNumThreads(so, 1), "SetIntraOpNumThreads");
    const OrtDmlApi* dmlApi = nullptr;
    good = good && ok(api->GetExecutionProviderApi("DML", ORT_API_VERSION, (const void**)&dmlApi), "GetExecutionProviderApi(DML)") && dmlApi &&
           ok(dmlApi->SessionOptionsAppendExecutionProvider_DML(so, adapter), "the DirectML provider") &&
           ok(api->CreateSession(env, model.c_str(), so, &session), "loading the depth network") &&
           ok(api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mem), "CreateCpuMemoryInfo");
    if (so) api->ReleaseSessionOptions(so);
    if (good) {
        OrtAllocator* al = nullptr;
        char* in = nullptr;
        char* out = nullptr;
        good = ok(api->GetAllocatorWithDefaultOptions(&al), "GetAllocatorWithDefaultOptions") &&
               ok(api->SessionGetInputName(session, 0, al, &in), "SessionGetInputName") &&
               ok(api->SessionGetOutputName(session, 0, al, &out), "SessionGetOutputName");
        if (in) { inName_ = in; al->Free(al, in); }
        if (out) { outName_ = out; al->Free(al, out); }
        OrtTypeInfo* ti = nullptr;
        const OrtTensorTypeAndShapeInfo* tti = nullptr;
        size_t n = 0;
        int64_t dims[8] = {};
        good = good && ok(api->SessionGetInputTypeInfo(session, 0, &ti), "SessionGetInputTypeInfo") &&
               ok(api->CastTypeInfoToTensorInfo(ti, &tti), "CastTypeInfoToTensorInfo") && ok(api->GetDimensionsCount(tti, &n), "GetDimensionsCount") &&
               n == 4 && ok(api->GetDimensions(tti, dims, 4), "GetDimensions");
        if (ti) api->ReleaseTypeInfo(ti);
        if (good && (dims[0] != 1 || dims[3] != 3 || dims[1] < 16 || dims[2] < 16)) {
            say("vrcam: video3d - the depth network does not take one RGB picture (NHWC): the videos stay flat");
            good = false;
        }
        h_ = (int)dims[1];
        w_ = (int)dims[2];
    }
    if (!good) {
        if (mem) api->ReleaseMemoryInfo(mem);
        if (session) api->ReleaseSession(session);
        if (env) api->ReleaseEnv(env);
        api_ = nullptr;
        return false;
    }
    env_ = env;
    session_ = session;
    memInfo_ = mem;
    // One run before the first film, so the first one does not wait for DirectML's compile.
    std::vector<float> grey((size_t)w_ * h_ * 3, 0.5f), map;
    if (!Run(grey, map)) { Unload(); return false; }
    snprintf(line, sizeof line, "vrcam: video3d - depth network ready: %dx%d on DirectML (adapter %d), ONNX Runtime %s from %s", w_, h_, adapter,
             base->GetVersionString(), Narrow(dir).c_str());
    say(line);
    return true;
}

bool DepthNet::Run(const std::vector<float>& nhwc, std::vector<float>& out) {
    if (!session_ || nhwc.size() != (size_t)w_ * h_ * 3) return false;
    const OrtApi* api = (const OrtApi*)api_;
    const int64_t shape[4] = {1, h_, w_, 3};
    OrtValue* x = nullptr;
    OrtValue* y = nullptr;
    OrtStatus* s = api->CreateTensorWithDataAsOrtValue((const OrtMemoryInfo*)memInfo_, (void*)nhwc.data(), nhwc.size() * sizeof(float), shape, 4,
                                                       ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &x);
    if (!s) {
        const char* ins[] = {inName_.c_str()};
        const char* outs[] = {outName_.c_str()};
        s = api->Run((OrtSession*)session_, nullptr, ins, (const OrtValue* const*)&x, 1, outs, 1, &y);
    }
    float* p = nullptr;
    if (!s) s = api->GetTensorMutableData(y, (void**)&p);
    if (!s && p) out.assign(p, p + (size_t)w_ * h_);
    if (x) api->ReleaseValue(x);
    if (y) api->ReleaseValue(y);
    if (s) {
        if (log_ && failures_++ % 300 == 0) {
            char line[400];
            snprintf(line, sizeof line, "vrcam: video3d - the depth network failed: %s", api->GetErrorMessage(s));
            log_(line);
        }
        api->ReleaseStatus(s);
        return false;
    }
    return true;
}

void DepthNet::Unload() {
    const OrtApi* api = (const OrtApi*)api_;
    if (!api) return;
    if (memInfo_) api->ReleaseMemoryInfo((OrtMemoryInfo*)memInfo_);
    if (session_) api->ReleaseSession((OrtSession*)session_);
    if (env_) api->ReleaseEnv((OrtEnv*)env_);
    memInfo_ = session_ = env_ = nullptr;
    api_ = nullptr;   // (the runtime itself stays loaded: freeing it under a worker it may have started is not safe)
}

}  // namespace video3d
