#pragma once

#include <string>
#include <cstdint>

#pragma pack(push, 1)

struct KATVector3 {
    float x, y, z;
};

struct KATQuaternion {
    float x, y, z, w;
};

struct DeviceData {
    uint8_t btnPressed;
    uint8_t isBatteryCharging;
    float batteryLevel;
    uint8_t firmwareVersion;
};

struct TreadMillData {
    char deviceName[64];
    uint8_t connected;
    double lastUpdateTimePoint;
    KATQuaternion bodyRotationRaw;
    KATVector3 moveSpeed;
    DeviceData deviceDatas[3];
    uint8_t extraData[128];
};

#pragma pack(pop)

class KATNativeSDK {
public:
    static double GetLastCalibratedTimeEscaped();
    static TreadMillData GetWalkStatus(const std::string& sn = "");
    static std::string GetVRPath();
    static std::string GetSDKInterfaceVersion();

    static void EnableKATInput();
    static void DisableKATInput();
    static void UnloadSDKLibrary();

    class KATExtension {
    public:
        static void Vibrate(float amplitude, const std::string& sn = "");
        static void LED(float amplitude, const std::string& sn = "");
    };
};
