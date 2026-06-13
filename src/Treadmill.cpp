#include "Treadmill.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

inline float Deg2Rad(float deg) { return deg * (float)M_PI / 180.0f; }
inline float Rad2Deg(float rad) { return rad * 180.0f / (float)M_PI; }

Treadmill::Treadmill() {}

void Treadmill::Update(const TreadMillData& ws, const ObjectTransform& eye) {
    if (!ws.connected) {
        m_moving = false;
        m_stopFrames = 0;
        return;
    }

    double lastCalibrationTime = KATNativeSDK::GetLastCalibratedTimeEscaped();

    // Calibration: Button pressed OR recent calibration
    if (ws.deviceDatas[0].btnPressed || lastCalibrationTime < 0.08) {
        float hmdYaw = GetYaw(eye.rotation);
        float bodyYaw = GetYaw(ws.bodyRotationRaw);
        m_yawCorrection = bodyYaw - hmdYaw;
        
        m_cachedOutput = {0.0f, 0.0f};
        m_moving = true; // Block stick during calibration
    } else {
        // Update rotation with correction
        KATQuaternion correctionQ = EulerY(m_yawCorrection);
        KATQuaternion bodyRotation = Multiply(ws.bodyRotationRaw, Inverse(correctionQ));

        // Movement logic
        KATVector3 velocity = Multiply(bodyRotation, ws.moveSpeed);

        // Convert world-space velocity to HMD-relative stick input
        float hmdYaw = GetYaw(eye.rotation);
        // OpenXR HMD yaw and the KAT movement frame use opposite handedness.
        KATQuaternion invHmdQ = EulerY(hmdYaw);
        KATVector3 relativeVelocity = Multiply(invHmdQ, velocity);

        const float rawStickX = relativeVelocity.x;
        const float rawStickY = relativeVelocity.z;
        m_cachedOutput.x = rawStickX;
        m_cachedOutput.y = rawStickY;
        
        // Clamp radially to preserve direction when treadmill speed exceeds stick range.
        const float rawStickMagnitude = std::sqrt(rawStickX * rawStickX + rawStickY * rawStickY);
        if (rawStickMagnitude > 1.0f) {
            m_cachedOutput.x /= rawStickMagnitude;
            m_cachedOutput.y /= rawStickMagnitude;
        }

        bool currentlyMoving = (std::abs(m_cachedOutput.x) > 0.01f || std::abs(m_cachedOutput.y) > 0.01f);
        if (currentlyMoving) {
            m_moving = true;
            m_stopFrames = 30; // ~300ms trailing stop at 90fps
        } else {
            m_moving = false;
            if (m_stopFrames > 0) m_stopFrames--;
        }
    }
}

bool Treadmill::AlterStickInput(XrVector2f& input) const {
    if (!m_connected) {
        return false;
    }
    if (m_moving) {
        input = m_cachedOutput;
        return true;
    }
    // Treadmill is idle. Check trailing stop.
    if (m_stopFrames > 0) {
        // If stick is also idle, override with 0 and force change.
        if (std::abs(input.x) < 0.1f && std::abs(input.y) < 0.1f) {
            input = {0, 0};
            return true;
        } else {
            // Physical stick is active, cancel trailing stop.
            m_stopFrames = 0;
        }
    }
    return false;
}

KATQuaternion Treadmill::Inverse(KATQuaternion q) {
    return {-q.x, -q.y, -q.z, q.w};
}

KATQuaternion Treadmill::Multiply(KATQuaternion a, KATQuaternion b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    };
}

KATVector3 Treadmill::Multiply(KATQuaternion q, KATVector3 v) {
    float x = q.x * 2.0f; float y = q.y * 2.0f; float z = q.z * 2.0f;
    float xx = q.x * x; float yy = q.y * y; float zz = q.z * z;
    float xy = q.x * y; float xz = q.x * z; float yz = q.y * z;
    float wx = q.w * x; float wy = q.w * y; float wz = q.w * z;

    return {
        (1.0f - (yy + zz)) * v.x + (xy - wz) * v.y + (xz + wy) * v.z,
        (xy + wz) * v.x + (1.0f - (xx + zz)) * v.y + (yz - wx) * v.z,
        (xz - wy) * v.x + (yz + wx) * v.y + (1.0f - (xx + yy)) * v.z
    };
}

float Treadmill::GetYaw(KATQuaternion q) {
    return Rad2Deg(std::atan2(2.0f * (q.w * q.y + q.x * q.z), 1.0f - 2.0f * (q.y * q.y + q.x * q.x)));
}

KATQuaternion Treadmill::EulerY(float yaw) {
    float halfYaw = Deg2Rad(yaw) * 0.5f;
    return { 0, std::sin(halfYaw), 0, std::cos(halfYaw) };
}
