#ifndef BLINDSIDE_PRIVACY_TRIGGER_HPP
#define BLINDSIDE_PRIVACY_TRIGGER_HPP

#include "blindside/config.hpp"
#include "blindside/types.hpp"
#include "blindside/platform.hpp"
#include <string>
#include <memory>
#include <fstream>

namespace blindside {

class PrivacyTriggerManager {
public:
    explicit PrivacyTriggerManager(const Config& config);
    ~PrivacyTriggerManager();

    bool initialize();

    /**
     * @brief Evaluates frame detection results and executes soft alert, targeted blur, or hard defense if needed.
     */
    void execute_triggers(const FrameResult& result);

    /**
     * @brief Retrieves exact active focused window rectangle via native OS APIs.
     */
    WindowRect get_active_window_geometry();

    /**
     * @brief Spawns a targeted transparent/blurred privacy overlay strictly covering active window bounds.
     */
    void trigger_targeted_blur(const WindowRect& rect);

    /**
     * @brief Triggers a soft alert (e.g. screen edge glow, tray notification).
     */
    void trigger_soft_alert(const std::string& message = "Visual eavesdropper detected nearby!");

    /**
     * @brief Triggers a hard defense (e.g. OS workstation lock).
     */
    void trigger_hard_defense(const std::string& message = "Eavesdropper gaze verified > 1.0s. Locking workstation!");

    /**
     * @brief Removes/deactivates active alerts or overlays when space is clear.
     */
    void clear_alerts();

    /**
     * @brief Writes threat event to audit log for NIST SP 800-53 / ISO 27001 compliance.
     */
    void log_threat_event(const std::string& threat_type, double gaze_duration_sec, size_t face_count, bool live_verified);

    /**
     * @brief Gets current platform diagnostics and capability info.
     */
    PlatformDiagnostics get_diagnostics() const;

    /**
     * @brief Gets discrete platform capabilities.
     */
    PlatformCapabilities get_capabilities() const;

    /**
     * @brief Pushes platform event loop forward without blocking.
     */
    void pump_events();

    bool is_hard_defense_active() const { return hard_defense_active_; }
    bool is_soft_alert_active() const { return soft_alert_active_; }
    bool is_targeted_blur_active() const { return targeted_blur_active_; }
    std::string get_current_threat_state() const { return current_threat_state_; }

private:
    Config config_;
    bool soft_alert_active_ = false;
    bool hard_defense_active_ = false;
    bool targeted_blur_active_ = false;
    std::string current_threat_state_ = "CLEARED";
    std::ofstream log_stream_;

    std::unique_ptr<PlatformManager> platform_manager_;
};

} // namespace blindside

#endif // BLINDSIDE_PRIVACY_TRIGGER_HPP
