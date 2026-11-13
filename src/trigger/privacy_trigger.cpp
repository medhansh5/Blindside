#include "blindside/privacy_trigger.hpp"
#include "blindside/platform.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>

namespace blindside {

PrivacyTriggerManager::PrivacyTriggerManager(const Config& config)
    : config_(config) {
    platform_manager_ = PlatformManager::create();
}

PrivacyTriggerManager::~PrivacyTriggerManager() {
    clear_alerts();
    if (log_stream_.is_open()) {
        log_stream_.close();
    }
}

bool PrivacyTriggerManager::initialize() {
    log_stream_.open(config_.log_file_path, std::ios::app);
    if (log_stream_.is_open()) {
        std::cout << "[PrivacyTrigger] Threat log opened at " << config_.log_file_path << std::endl;
    }

    if (!platform_manager_->initialize()) {
        std::cerr << "[PrivacyTrigger] Failed to initialize platform manager." << std::endl;
        return false;
    }

    return true;
}

WindowRect PrivacyTriggerManager::get_active_window_geometry() {
    return platform_manager_->get_active_window_geometry();
}

void PrivacyTriggerManager::trigger_targeted_blur(const WindowRect& rect) {
    if (!targeted_blur_active_) {
        targeted_blur_active_ = true;
        auto diag = platform_manager_->get_diagnostics();
        if (diag.supports_native_redaction) {
            std::cout << "\033[1;36m[TARGETED PRIVACY OVERLAY] Redacting Active Workspace Window ["
                      << rect.x << ", " << rect.y << ", " << rect.width << "x" << rect.height << "]\033[0m" << std::endl;
        } else {
            std::cout << "\033[1;33m[TARGETED PRIVACY OVERLAY] Requested, but unsupported on this platform. Falling back.\033[0m" << std::endl;
        }
    }

    platform_manager_->trigger_targeted_blur(rect);
}

void PrivacyTriggerManager::trigger_soft_alert(const std::string& message) {
    if (soft_alert_active_) return;
    soft_alert_active_ = true;

    std::cout << "\033[1;33m[SOFT ALERT] " << message << "\033[0m" << std::endl;
    platform_manager_->trigger_soft_alert(message);
}

void PrivacyTriggerManager::trigger_hard_defense(const std::string& message) {
    if (hard_defense_active_) return;
    hard_defense_active_ = true;

    std::cout << "\033[1;31m[HARD DEFENSE TRIGGERED] " << message << "\033[0m" << std::endl;

    if (config_.enable_screen_lock) {
        platform_manager_->trigger_hard_defense(message);
    }
}

void PrivacyTriggerManager::clear_alerts() {
    if (soft_alert_active_ || hard_defense_active_ || targeted_blur_active_) {
        std::cout << "[PrivacyTrigger] Clearing active privacy alerts and overlays." << std::endl;
        soft_alert_active_ = false;
        hard_defense_active_ = false;
        targeted_blur_active_ = false;
        platform_manager_->clear_alerts();
    }
    if (current_threat_state_ != "CLEARED") {
        log_threat_event(current_threat_state_ + "_DEACTIVATED", 0.0, 0, false);
        current_threat_state_ = "CLEARED";
    }
}

void PrivacyTriggerManager::log_threat_event(const std::string& threat_type, double gaze_duration_sec, size_t face_count, bool live_verified) {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    
    std::cout << "[AUDIT LOG] Threat: " << threat_type 
              << " | Gaze Duration: " << std::fixed << std::setprecision(2) << gaze_duration_sec << "s"
              << " | Faces Present: " << face_count
              << " | Liveness Verified: " << (live_verified ? "YES" : "NO") << std::endl;

    if (log_stream_.is_open()) {
        log_stream_ << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S")
                    << " [AUDIT_ALERT] Threat=" << threat_type
                    << " GazeDurationSec=" << std::fixed << std::setprecision(2) << gaze_duration_sec
                    << " FacesDetected=" << face_count
                    << " LivenessVerified=" << (live_verified ? "1" : "0")
                    << " Control=NIST_SP_800_53_PE_3" << std::endl;
        log_stream_.flush();
    }
}

void PrivacyTriggerManager::execute_triggers(const FrameResult& result) {
    std::string new_threat_state = "CLEARED";
    std::string threat_name = "";
    double dur = 0.0;

    if (result.trigger_hard_defense) {
        threat_name = result.primary_absence_triggered ? "PRIMARY_USER_ABSENT_LOCK" : "HARD_WORKSTATION_LOCK";
        dur = result.primary_absence_triggered ? result.primary_absence_duration_sec : result.secondary_gaze_duration_sec;
        trigger_hard_defense(result.primary_absence_triggered ? "Primary user absent - workstation locked" : "Shoulder surfer detected");
        new_threat_state = threat_name;
    } else if (result.trigger_targeted_blur) {
        WindowRect active_rect = get_active_window_geometry();
        trigger_targeted_blur(active_rect);
        threat_name = result.primary_absence_triggered ? "PRIMARY_USER_ABSENT_BLUR" : "TARGETED_WORKSPACE_BLUR";
        dur = result.primary_absence_triggered ? result.primary_absence_duration_sec : result.secondary_gaze_duration_sec;
        new_threat_state = threat_name;
    } else if (result.trigger_soft_alert) {
        trigger_soft_alert();
        threat_name = "SOFT_SECONDARY_FACE_DETECTED";
        dur = result.secondary_gaze_duration_sec;
        new_threat_state = threat_name;
    } else {
        clear_alerts();
        new_threat_state = "CLEARED";
    }

    // Deduplicate audit logging: Log only on state transitions (activation, escalation, deactivation)
    if (new_threat_state != current_threat_state_) {
        if (new_threat_state != "CLEARED") {
            std::string transition = (current_threat_state_ == "CLEARED") ? "_ACTIVATED" : "_ESCALATED";
            log_threat_event(new_threat_state + transition, dur, result.faces.size(), result.secondary_liveness_verified);
        } else {
            log_threat_event(current_threat_state_ + "_DEACTIVATED", 0.0, result.faces.size(), false);
        }
        current_threat_state_ = new_threat_state;
    }

    pump_events();
}

PlatformDiagnostics PrivacyTriggerManager::get_diagnostics() const {
    return platform_manager_->get_diagnostics();
}

PlatformCapabilities PrivacyTriggerManager::get_capabilities() const {
    return platform_manager_->get_capabilities();
}

void PrivacyTriggerManager::pump_events() {
    platform_manager_->pump_events();
}

} // namespace blindside
