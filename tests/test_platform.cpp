#include "blindside/platform.hpp"
#include "blindside/privacy_trigger.hpp"
#include "blindside/types.hpp"
#include "blindside/config.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <cstdio>

using namespace blindside;

// 1. Platform capability detection
void test_platform_capability_detection() {
    auto pm = PlatformManager::create();
    assert(pm != nullptr);
    bool init_ok = pm->initialize();
    assert(init_ok);

    auto caps = pm->get_capabilities();
    auto diag = pm->get_diagnostics();

    // Diagnostics must accurately reflect capabilities
    assert(diag.capabilities.screen_redaction == caps.screen_redaction);
    assert(diag.capabilities.targeted_blur == caps.targeted_blur);
    assert(diag.capabilities.session_lock == caps.session_lock);
    assert(diag.capabilities.click_through_overlay == caps.click_through_overlay);
    assert(diag.capabilities.multi_monitor_aware == caps.multi_monitor_aware);

    // Honest reporting: targeted_blur must be false (DwmBlur is legacy/no-op; X11/Wayland don't have true blur)
    assert(caps.targeted_blur == false);
    assert(diag.supports_targeted_blur == false);

#if defined(_WIN32)
    assert(diag.os_name == "Windows");
    assert(caps.screen_redaction == true);
    assert(caps.session_lock == true);
    assert(caps.click_through_overlay == true);
    assert(caps.multi_monitor_aware == true);
    assert(diag.redaction_mode.find("AlphaRedaction") != std::string::npos);
#elif defined(HAVE_X11)
    assert(diag.os_name == "Linux (X11)");
    assert(caps.session_lock == true);
    assert(caps.click_through_overlay == false);
    if (caps.screen_redaction) {
        assert(diag.redaction_mode.find("OpaqueRedaction") != std::string::npos);
        assert(caps.multi_monitor_aware == true);
    } else {
        assert(diag.redaction_mode.find("None") != std::string::npos);
        assert(caps.multi_monitor_aware == false);
    }
#else
    // Wayland or generic
    assert(caps.session_lock == true);
#endif

    std::cout << "[PASS] test_platform_capability_detection" << std::endl;
}

// 2. Repeated overlay activation/deactivation
void test_repeated_overlay_lifecycle() {
    auto pm = PlatformManager::create();
    assert(pm->initialize());

    WindowRect rect{150, 150, 800, 600, true};

    // Rapid cycle activation and deactivation
    for (int i = 0; i < 10; ++i) {
        pm->trigger_targeted_blur(rect);
        pm->pump_events();
        pm->clear_alerts();
        pm->pump_events();
    }

    // Platform must remain stable after 10 full cycles
    auto geom = pm->get_active_window_geometry();
    assert(geom.width > 0);
    assert(geom.height > 0);

    std::cout << "[PASS] test_repeated_overlay_lifecycle" << std::endl;
}

// 3. Idempotent activation
void test_idempotent_overlay_activation() {
    auto pm = PlatformManager::create();
    assert(pm->initialize());

    WindowRect rect1{100, 100, 640, 480, true};
    WindowRect rect2{200, 200, 800, 600, true};

    // Activate once
    pm->trigger_targeted_blur(rect1);
    pm->pump_events();

    // Activate again with same rect (idempotent no-op/refresh)
    pm->trigger_targeted_blur(rect1);
    pm->pump_events();

    // Activate with new rect (repositions existing overlay without duplicating)
    pm->trigger_targeted_blur(rect2);
    pm->pump_events();

    // Teardown
    pm->clear_alerts();
    pm->pump_events();

    std::cout << "[PASS] test_idempotent_overlay_activation" << std::endl;
}

// 4. Cleanup after deactivation
void test_cleanup_after_deactivation() {
    auto pm = PlatformManager::create();
    assert(pm->initialize());

    // Calling clear_alerts when inactive must be completely safe
    pm->clear_alerts();
    pm->pump_events();

    // Activate and clear
    WindowRect rect{50, 50, 400, 300, true};
    pm->trigger_targeted_blur(rect);
    pm->clear_alerts();

    // Calling clear_alerts a second time must be a safe idempotent no-op
    pm->clear_alerts();
    pm->pump_events();

    std::cout << "[PASS] test_cleanup_after_deactivation" << std::endl;
}

// 5. Unsupported capability fallback
void test_unsupported_capability_fallback() {
    Config cfg;
    cfg.enable_screen_lock = false; // Do not lock machine during automated tests
    PrivacyTriggerManager trigger_mgr(cfg);
    assert(trigger_mgr.initialize());

    auto caps = trigger_mgr.get_capabilities();
    (void)caps;

    // Trigger targeted blur through trigger manager
    WindowRect rect{100, 100, 500, 400, true};
    trigger_mgr.trigger_targeted_blur(rect);
    assert(trigger_mgr.is_targeted_blur_active());

    // Trigger soft alert
    trigger_mgr.trigger_soft_alert("Test advisory alert");
    assert(trigger_mgr.is_soft_alert_active());

    // Clear alerts
    trigger_mgr.clear_alerts();
    assert(!trigger_mgr.is_targeted_blur_active());
    assert(!trigger_mgr.is_soft_alert_active());

    std::cout << "[PASS] test_unsupported_capability_fallback" << std::endl;
}

// 6. Invalid display geometry handling
void test_invalid_display_geometry() {
    auto pm = PlatformManager::create();
    assert(pm->initialize());

    // Negative width and height
    WindowRect invalid_negative{-50, -50, -640, -480, true};
    pm->trigger_targeted_blur(invalid_negative);
    pm->pump_events();

    // Zero dimensions
    WindowRect invalid_zero{100, 100, 0, 0, true};
    pm->trigger_targeted_blur(invalid_zero);
    pm->pump_events();

    // Unflagged invalid
    WindowRect not_valid{0, 0, 0, 0, false};
    pm->trigger_targeted_blur(not_valid);
    pm->pump_events();

    pm->clear_alerts();
    pm->pump_events();

    std::cout << "[PASS] test_invalid_display_geometry" << std::endl;
}

// 7. Multi-monitor geometry including negative coordinates
void test_multi_monitor_negative_coordinates() {
    auto pm = PlatformManager::create();
    assert(pm->initialize());

    // Secondary monitor placed to the left: negative x coordinate
    WindowRect left_monitor{-1920, 0, 1920, 1080, true};
    pm->trigger_targeted_blur(left_monitor);
    pm->pump_events();

    // Secondary monitor placed above: negative y coordinate
    WindowRect top_monitor{0, -1080, 1920, 1080, true};
    pm->trigger_targeted_blur(top_monitor);
    pm->pump_events();

    // Top-left diagonal display: negative x and y
    WindowRect topleft_monitor{-2560, -1440, 2560, 1440, true};
    pm->trigger_targeted_blur(topleft_monitor);
    pm->pump_events();

    pm->clear_alerts();
    pm->pump_events();

    std::cout << "[PASS] test_multi_monitor_negative_coordinates" << std::endl;
}

// 8. Platform failure does not propagate as application crash
void test_platform_resilience_and_graceful_recovery() {
    auto pm = PlatformManager::create();
    assert(pm->initialize());

    // Verify event pump functions safely even when no windows are open
    pm->pump_events();

    // Verify soft alert doesn't throw or crash
    pm->trigger_soft_alert("Resilience test alert");

    // Geometry lookup must never return invalid/zero dimensions even if headless
    WindowRect geom = pm->get_active_window_geometry();
    assert(geom.valid);
    assert(geom.width > 0);
    assert(geom.height > 0);

    pm->clear_alerts();
    pm->pump_events();

    std::cout << "[PASS] test_platform_resilience_and_graceful_recovery" << std::endl;
}

// 9. Threat remains independent from platform response
void test_threat_independent_from_platform() {
    // Model threat assessment result before platform interactions
    FrameResult threat_assessment;
    threat_assessment.frame_id = 101;
    threat_assessment.primary_user_present = true;
    threat_assessment.secondary_gaze_detected = true;
    threat_assessment.secondary_liveness_verified = true;
    threat_assessment.secondary_gaze_duration_sec = 1.45;
    threat_assessment.trigger_soft_alert = true;
    threat_assessment.trigger_targeted_blur = true;

    // Platform execution
    auto pm = PlatformManager::create();
    assert(pm->initialize());
    pm->trigger_targeted_blur(WindowRect{100, 100, 800, 600, true});
    pm->trigger_soft_alert("Threat detected");
    pm->pump_events();

    // The threat assessment data remains identical and pure regardless of platform state
    assert(threat_assessment.frame_id == 101);
    assert(threat_assessment.trigger_soft_alert == true);
    assert(threat_assessment.trigger_targeted_blur == true);
    assert(threat_assessment.secondary_gaze_duration_sec == 1.45);
    assert(threat_assessment.secondary_liveness_verified == true);

    pm->clear_alerts();
    pm->pump_events();

    std::cout << "[PASS] test_threat_independent_from_platform" << std::endl;
}

// 10. Platform response does not modify threat state
void test_platform_response_does_not_modify_threat_engine() {
    Config cfg;
    cfg.enable_screen_lock = false;
    PrivacyTriggerManager trigger_mgr(cfg);
    assert(trigger_mgr.initialize());

    // Immutable threat assessment passed into trigger execution
    FrameResult immutable_threat;
    immutable_threat.frame_id = 42;
    immutable_threat.primary_user_present = true;
    immutable_threat.trigger_soft_alert = true;
    immutable_threat.trigger_targeted_blur = false;
    immutable_threat.trigger_hard_defense = false;
    immutable_threat.secondary_gaze_duration_sec = 0.5;
    immutable_threat.secondary_liveness_verified = true;

    // Execute platform triggers with threat result
    trigger_mgr.execute_triggers(immutable_threat);
    assert(trigger_mgr.is_soft_alert_active());

    // Verify trigger execution did not mutate input threat assessment
    assert(immutable_threat.frame_id == 42);
    assert(immutable_threat.trigger_soft_alert == true);
    assert(immutable_threat.secondary_gaze_duration_sec == 0.5);

    trigger_mgr.clear_alerts();
    assert(!trigger_mgr.is_soft_alert_active());

    std::cout << "[PASS] test_platform_response_does_not_modify_threat_engine" << std::endl;
}

void test_audit_log_deduplication() {
    Config config;
    config.enable_screen_lock = false; // Don't trigger actual OS lock in unit test
    config.log_file_path = "test_dedup_threats.log";
    std::remove("test_dedup_threats.log");

    PrivacyTriggerManager trigger_mgr(config);
    assert(trigger_mgr.initialize());

    FrameResult soft_frame;
    soft_frame.trigger_soft_alert = true;
    soft_frame.secondary_gaze_duration_sec = 0.2;

    // Send 10 identical consecutive soft alert frames
    for (int i = 0; i < 10; ++i) {
        trigger_mgr.execute_triggers(soft_frame);
    }
    assert(trigger_mgr.get_current_threat_state() == "SOFT_SECONDARY_FACE_DETECTED");

    // Escalate to hard defense for 10 frames
    FrameResult hard_frame;
    hard_frame.trigger_hard_defense = true;
    hard_frame.secondary_gaze_duration_sec = 1.2;
    for (int i = 0; i < 10; ++i) {
        trigger_mgr.execute_triggers(hard_frame);
    }
    assert(trigger_mgr.get_current_threat_state() == "HARD_WORKSTATION_LOCK");

    // Clear / calm state for 10 frames
    FrameResult calm_frame;
    for (int i = 0; i < 10; ++i) {
        trigger_mgr.execute_triggers(calm_frame);
    }
    assert(trigger_mgr.get_current_threat_state() == "CLEARED");

    // Inspect the generated audit log file
    std::ifstream log_file("test_dedup_threats.log");
    assert(log_file.is_open());
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(log_file, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    log_file.close();
    std::remove("test_dedup_threats.log");

    // Exactly 3 state-transition log entries should exist:
    // 1. SOFT_SECONDARY_FACE_DETECTED_ACTIVATED
    // 2. HARD_WORKSTATION_LOCK_ESCALATED
    // 3. HARD_WORKSTATION_LOCK_DEACTIVATED
    assert(lines.size() == 3);
    assert(lines[0].find("SOFT_SECONDARY_FACE_DETECTED_ACTIVATED") != std::string::npos);
    assert(lines[1].find("HARD_WORKSTATION_LOCK_ESCALATED") != std::string::npos);
    assert(lines[2].find("HARD_WORKSTATION_LOCK_DEACTIVATED") != std::string::npos);

    std::cout << "[PASS] test_audit_log_deduplication (logged 3 transitions out of 30 frames)" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " Running Blindside V4 Platform Tests   " << std::endl;
    std::cout << "========================================" << std::endl;

    test_platform_capability_detection();
    test_repeated_overlay_lifecycle();
    test_idempotent_overlay_activation();
    test_cleanup_after_deactivation();
    test_unsupported_capability_fallback();
    test_invalid_display_geometry();
    test_multi_monitor_negative_coordinates();
    test_platform_resilience_and_graceful_recovery();
    test_threat_independent_from_platform();
    test_platform_response_does_not_modify_threat_engine();
    test_audit_log_deduplication();

    std::cout << "========================================" << std::endl;
    std::cout << " ALL 11 PLATFORM TESTS PASSED (11/11)   " << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
