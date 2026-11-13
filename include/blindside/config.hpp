#ifndef BLINDSIDE_CONFIG_HPP
#define BLINDSIDE_CONFIG_HPP

#include <string>
#include <cstdint>

namespace blindside {

enum class TriggerMode {
    SoftAlertOnly,
    HardDefenseOnly,
    Both,
    LogOnly
};

enum class DaemonState {
    StrictFullLock,        // Immediate full OS screen lock on secondary gaze > 1s
    GracefulTargetedBlur,  // Targeted dynamic blur/overlay over active workspace window
    PauseDetection         // Monitoring paused by user
};

struct Config {
    // Frame sampling rates (Hz)
    double active_fps = 30.0;
    double idle_fps = 5.0;
    double idle_timeout_sec = 3.0; // Time in seconds of no secondary gaze before dropping to idle_fps

    // Camera settings
    int camera_index = 0;
    int capture_width = 640;
    int capture_height = 480;

    // Detection & Gaze Estimation Parameters
    float face_confidence_threshold = 0.55f;
    double max_allowed_yaw_deg = 30.0;   // Yaw angle relative to screen center
    double max_allowed_pitch_deg = 25.0; // Pitch angle relative to screen center
    double hysteresis_sec = 1.0;         // Secondary gaze must persist > 1s for Hard Defense

    // Liveness & Anti-Spoofing Parameters (5-Point Geometric Proxy)
    // YuNet outputs 5 landmark points (eyes, nose, mouth corners) without eyelid contours.
    // This is a geometric facial aspect ratio (FAR) proxy (interocular vs eye-to-nose vertical distance),
    // not anatomical eyelid aperture EAR. Anti-spoofing relies primarily on 3D head pose micro-motion.
    float facial_aspect_ratio_threshold = 0.20f;
    float ear_blink_threshold = 0.20f;               // Backward-compatible alias
    double liveness_window_sec = 3.0;                // Time window to verify micro-motion variance
    float micro_pose_variance_threshold = 0.80f;     // Pitch/Yaw variance threshold for live face vs photo

    // Primary User Calibration Box (normalized [0, 1])
    float primary_x_center = 0.5f;
    float primary_y_center = 0.5f;
    float primary_box_tolerance = 0.35f; // Distance threshold for primary vs eavesdropper

    // Primary User Absence Defense (THREAT-03)
    bool enable_primary_absence_defense = true;
    double primary_absence_timeout_sec = 5.0; // Sustained absence duration before triggering
    double primary_recovery_dwell_sec = 0.5;  // Sustained return duration before clearing

    // Trigger behavior
    TriggerMode trigger_mode = TriggerMode::Both;
    DaemonState daemon_state = DaemonState::GracefulTargetedBlur;
    bool enable_screen_lock = true;
    bool enable_blur_overlay = true;
    bool enable_sound_alert = false;
    std::string log_file_path = "blindside_threats.log";
    std::string model_path = "";
};

} // namespace blindside

#endif // BLINDSIDE_CONFIG_HPP
