#include "blindside/daemon.hpp"
#include "blindside/config.hpp"
#include "blindside/tray_icon.hpp"
#include "blindside/platform.hpp"
#include "blindside/platform_paths.hpp"
#include "blindside/logger.hpp"
#include <iostream>
#include <string>
#include <csignal>
#include <thread>
#include <chrono>

namespace {
    std::atomic<bool> g_stop_requested{false};

    void signal_handler(int signal) {
        if (signal == SIGINT || signal == SIGTERM) {
            std::cout << "\n[Blindside CLI] Shutdown signal received (" << signal << "). Terminating..." << std::endl;
            g_stop_requested.store(true);
        }
    }
}

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n"
              << "Options:\n"
              << "  --daemon               Run continuously in desktop background mode\n"
              << "  --mode <mode>          Set operational mode: graceful (targeted redaction), strict (full lock), pause (default: graceful)\n"
              << "  --calibrate            Calibrate primary user baseline face position on launch\n"
              << "  --synthetic            Run in synthetic headless test mode (no physical camera required)\n"
              << "  --trigger-mode <mode>  Set trigger action: soft, hard, both, log (default: both)\n"
              << "  --fps-active <fps>     Set active monitoring FPS (default: 30)\n"
              << "  --fps-idle <fps>       Set low-power idle FPS (default: 5)\n"
              << "  --hysteresis <sec>     Secondary gaze duration required for lock/redaction (default: 1.0)\n"
              << "  --model-path <path>    Explicit path to the YuNet ONNX model\n"
              << "  --log-path <path>      Explicit path to the security log file\n"
              << "  --diagnostics          Print platform capability diagnostics and exit\n"
              << "  --help                 Display this help message\n";
}

void print_diagnostics() {
    auto platform = blindside::PlatformManager::create();
    platform->initialize();
    auto diag = platform->get_diagnostics();
    
    std::cout << "Blindside diagnostics\n"
              << "──────────────────────\n"
              << "Platform: " << diag.os_name << "\n"
              << "Monitors: " << diag.monitor_count << "\n\n"
              << "Vision engine: ✓ (OpenCV YuNet + SolvePnP)\n"
              << "Threat engine: ✓\n\n"
              << "Privacy responses:\n"
              << "  Screen lock: " << (diag.supports_screen_lock ? "✓" : "✗") << "\n"
              << "  Redaction: " << (diag.supports_native_redaction ? "✓" : "✗") << "\n"
              << "  Notifications: " << (diag.supports_desktop_notifications ? "✓" : "✗") << "\n\n"
              << "Network communication: disabled\n";
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    blindside::Config config;
    bool run_daemon = false;
    bool run_calibration = false;
    bool synthetic_mode = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--daemon") {
            run_daemon = true;
        } else if (arg == "--diagnostics") {
            print_diagnostics();
            return 0;
        } else if (arg == "--mode" && i + 1 < argc) {
            std::string mode_str = argv[++i];
            if (mode_str == "strict") config.daemon_state = blindside::DaemonState::StrictFullLock;
            else if (mode_str == "pause") config.daemon_state = blindside::DaemonState::PauseDetection;
            else config.daemon_state = blindside::DaemonState::GracefulTargetedBlur;
        } else if (arg == "--calibrate") {
            run_calibration = true;
        } else if (arg == "--synthetic") {
            synthetic_mode = true;
        } else if (arg == "--trigger-mode" && i + 1 < argc) {
            std::string mode_str = argv[++i];
            if (mode_str == "soft") config.trigger_mode = blindside::TriggerMode::SoftAlertOnly;
            else if (mode_str == "hard") config.trigger_mode = blindside::TriggerMode::HardDefenseOnly;
            else if (mode_str == "log") config.trigger_mode = blindside::TriggerMode::LogOnly;
            else config.trigger_mode = blindside::TriggerMode::Both;
        } else if (arg == "--fps-active" && i + 1 < argc) {
            config.active_fps = std::stod(argv[++i]);
        } else if (arg == "--fps-idle" && i + 1 < argc) {
            config.idle_fps = std::stod(argv[++i]);
        } else if (arg == "--hysteresis" && i + 1 < argc) {
            config.hysteresis_sec = std::stod(argv[++i]);
        } else if (arg == "--model-path" && i + 1 < argc) {
            config.model_path = argv[++i];
        } else if (arg == "--log-path" && i + 1 < argc) {
            config.log_file_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
    }

    // Default model path resolution if not explicitly provided
    if (config.model_path.empty()) {
        auto exe_dir = blindside::get_executable_directory();
        config.model_path = (exe_dir / "models" / "face_detection_yunet_2023mar.onnx").string();
    }

    std::cout << "========================================================\n"
              << "  🛡️ BLINDSIDE: Physical Privacy & Visual Eavesdropper Daemon (v4)\n"
              << "========================================================\n"
              << "Mode: " << blindside::SystemTrayController::get_mode_name(config.daemon_state)
              << " | Active FPS: " << config.active_fps 
              << " | Idle FPS: " << config.idle_fps 
              << " | Hysteresis: " << config.hysteresis_sec << "s\n";

    blindside::Daemon daemon(config);
    if (synthetic_mode) {
        daemon.set_synthetic_mode(true);
    }

    if (!blindside::Logger::get_instance().initialize(config.log_file_path)) {
        std::cerr << "[Blindside CLI] Warning: Failed to initialize logger at " << config.log_file_path << std::endl;
    }

    blindside::Logger::get_instance().log_system(std::string("Blindside daemon starting. Mode: ") + blindside::SystemTrayController::get_mode_name(config.daemon_state));

    if (!daemon.initialize()) {
        std::cerr << "[Blindside CLI] Daemon initialization failed!" << std::endl;
        blindside::Logger::get_instance().log_system("Daemon initialization failed.");
        return 1;
    }

    if (run_calibration) {
        std::cout << "[Blindside CLI] Registering primary user face position..." << std::endl;
        if (daemon.calibrate_primary_user()) {
            std::cout << "[Blindside CLI] Primary user calibration successful!" << std::endl;
        } else {
            std::cerr << "[Blindside CLI] Primary user calibration failed! (No face detected?)" << std::endl;
        }
    }

    daemon.start();

    if (run_daemon) {
        std::cout << "[Blindside CLI] Daemon active in background. Press Ctrl+C to stop." << std::endl;
        while (!g_stop_requested.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    } else {
        // Run diagnostic verification cycle
        std::cout << "[Blindside CLI] Running diagnostic verification cycle..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));

        if (synthetic_mode) {
            std::cout << "\n[Blindside CLI] Injecting synthetic live shoulder surfer event..." << std::endl;
            daemon.inject_synthetic_eavesdropper(true, 0.0);
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        }
    }

    daemon.stop();
    std::cout << "[Blindside CLI] Process exited cleanly." << std::endl;
    return 0;
}
