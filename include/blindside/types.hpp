#ifndef BLINDSIDE_TYPES_HPP
#define BLINDSIDE_TYPES_HPP

#include "blindside/config.hpp"
#include <vector>
#include <array>
#include <chrono>
#include <cmath>

namespace blindside {

struct Point2f {
    float x = 0.0f;
    float y = 0.0f;
};

struct Point3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct WindowRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool valid = false;
};

struct FaceBox {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float confidence = 0.0f;
    
    // 5 facial landmarks: right eye, left eye, nose tip, right mouth, left mouth
    std::array<Point2f, 5> landmarks{};

    float center_x() const { return x + width * 0.5f; }
    float center_y() const { return y + height * 0.5f; }
};

struct HeadPose {
    double pitch_deg = 0.0; // Rotation around X-axis (looking up/down)
    double yaw_deg = 0.0;   // Rotation around Y-axis (looking left/right)
    double roll_deg = 0.0;  // Rotation around Z-axis (head tilt)
    
    Point3f gaze_vector{0.0f, 0.0f, 1.0f}; // Unit gaze vector pointing out from face
    bool is_looking_at_screen = false;
    bool valid = false;
};

struct FaceDetectionResult {
    FaceBox box;
    HeadPose pose;
    float facial_aspect_ratio = 0.30f; // 5-point geometric proxy (vertical eye-to-nose ratio)
    float ear = 0.30f;                 // Backward-compatible alias
    bool is_primary_user = false;
    bool is_eavesdropper = false;
    bool is_live_threat = false;     // Passes Liveness micro-motion check
    bool is_spoof_static = false;    // Static photo / 0 variance detected
};

struct FrameResult {
    uint64_t frame_id = 0;
    std::chrono::system_clock::time_point timestamp;
    DaemonState daemon_state = DaemonState::GracefulTargetedBlur;
    bool primary_user_present = false;
    bool primary_absence_triggered = false;
    double primary_absence_duration_sec = 0.0;
    std::vector<FaceDetectionResult> faces;
    bool secondary_gaze_detected = false;
    bool secondary_liveness_verified = false;
    double secondary_gaze_duration_sec = 0.0;
    bool trigger_soft_alert = false;
    bool trigger_hard_defense = false;
    bool trigger_targeted_blur = false;
    WindowRect active_window_rect;
};

} // namespace blindside

#endif // BLINDSIDE_TYPES_HPP
