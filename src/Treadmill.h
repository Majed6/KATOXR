#pragma once

#include <openxr/openxr.h>
#include "KATNativeSDK.h"

struct ObjectTransform {
    KATQuaternion rotation = {0, 0, 0, 1};
};

class Treadmill {
public:
    Treadmill();

    void Update(const TreadMillData& ws, const ObjectTransform& eye);
    bool AlterStickInput(XrVector2f& input) const;
    
    bool IsConnected() const { return m_connected; }
    void SetConnected(bool connected) { m_connected = connected; }
    
    bool IsMoving() const { return m_moving; }

private:
    bool m_connected = false;
    float m_yawCorrection = 0.0f;
    XrVector2f m_cachedOutput = {0.0f, 0.0f};
    bool m_moving = false;
    mutable int m_stopFrames = 0;

    // Math utilities
    static KATQuaternion Inverse(KATQuaternion q);
    static KATQuaternion Multiply(KATQuaternion a, KATQuaternion b);
    static KATVector3 Multiply(KATQuaternion q, KATVector3 v);
    static float GetYaw(KATQuaternion q);
    static KATQuaternion EulerY(float yaw);
};
