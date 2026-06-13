#include "KATNativeSDK.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

namespace {
    HMODULE g_hModule = nullptr;

    HMODULE GetSDKModule() {
        if (!g_hModule) {
            HMODULE layerModule = nullptr;
            char sdkPath[MAX_PATH];
            if (GetModuleHandleExA(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCSTR>(&GetSDKModule),
                    &layerModule)) {
                const DWORD pathLength = GetModuleFileNameA(layerModule, sdkPath, sizeof(sdkPath));
                if (pathLength > 0 && pathLength < sizeof(sdkPath)) {
                    char* filename = std::strrchr(sdkPath, '\\');
                    if (!filename) return nullptr;
                    const size_t remaining = sizeof(sdkPath) - static_cast<size_t>(filename + 1 - sdkPath);
                    const int filenameLength = std::snprintf(filename + 1, remaining, "%s", "KATNativeSDK.dll");
                    if (filenameLength > 0 && static_cast<size_t>(filenameLength) < remaining) {
                        g_hModule = LoadLibraryA(sdkPath);
                    }
                }
            }
        }
        return g_hModule;
    }

    template<typename T>
    T GetFunc(const char* name) {
        HMODULE h = GetSDKModule();
        if (!h) return nullptr;
        return reinterpret_cast<T>(GetProcAddress(h, name));
    }
}

// Function Pointer Typedefs
typedef double (WINAPI *PGetLastCalibratedTimeEscaped)();
typedef TreadMillData (WINAPI *PGetWalkStatus)(const char*);
typedef char* (WINAPI *PGetVRPath)(char*);
typedef float (WINAPI *PGetSDKInterfaceVersion)();

typedef void (WINAPI *PEnableKATInput)();
typedef void (WINAPI *PDisableKATInput)();
typedef void (WINAPI *PUnloadSDKLibrary)();
typedef void (WINAPI *PVibrate)(const char*, float);
typedef void (WINAPI *PLED)(const char*, float);

double KATNativeSDK::GetLastCalibratedTimeEscaped() {
    static auto func = GetFunc<PGetLastCalibratedTimeEscaped>("GetLastCalibratedTimeEscaped");
    return func ? func() : 0.0;
}

TreadMillData KATNativeSDK::GetWalkStatus(const std::string& sn) {
    static auto func = GetFunc<PGetWalkStatus>("GetWalkStatus");
    if (func) return func(sn.empty() ? nullptr : sn.c_str());
    return {};
}

std::string KATNativeSDK::GetVRPath() {
    static auto func = GetFunc<PGetVRPath>("GetVRPath");
    if (func) {
        char buffer[3072];
        char* path = func(buffer);
        return path ? std::string(path) : "";
    }
    return "";
}

std::string KATNativeSDK::GetSDKInterfaceVersion() {
    static auto func = GetFunc<PGetSDKInterfaceVersion>("GetSDKInterfaceVersion");
    if (func) {
        float version = func();
        // Convert float to string to match the header signature
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", version);
        return std::string(buf);
    }
    return "";
}

void KATNativeSDK::EnableKATInput() {
    static auto func = GetFunc<PEnableKATInput>("EnableKATInput");
    if (func) func();
}

void KATNativeSDK::DisableKATInput() {
    static auto func = GetFunc<PDisableKATInput>("DisableKATInput");
    if (func) func();
}

void KATNativeSDK::UnloadSDKLibrary() {
    static auto func = GetFunc<PUnloadSDKLibrary>("UnloadSDKLibrary");
    if (func) func();
}

void KATNativeSDK::KATExtension::Vibrate(float amplitude, const std::string& sn) {
    static auto func = GetFunc<PVibrate>("Vibrate");
    if (func) func(sn.empty() ? nullptr : sn.c_str(), amplitude);
}

void KATNativeSDK::KATExtension::LED(float amplitude, const std::string& sn) {
    static auto func = GetFunc<PLED>("LED");
    if (func) func(sn.empty() ? nullptr : sn.c_str(), amplitude);
}
