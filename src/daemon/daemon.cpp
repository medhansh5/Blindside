#include "blindside/daemon.hpp"
#include "blindside/logger.hpp"
#include <iostream>
#include <chrono>

namespace blindside {

Daemon::Daemon(const Config& config)
    : config_(config),
      camera_(config),
      face_detector_(config),
      pose_estimator_(config),
      eavesdropper_detector_(config, face_detector_, pose_estimator_),
      privacy_trigger_(config),
      atomic_state_(config.daemon_state) {
}

Daemon::~Daemon() {
    stop();
}

bool Daemon::initialize() {
    if (!camera_.open()) {
        std::cerr << "[Daemon] Failed to open camera stream." << std::endl;
        return false;
    }

    std::string model = config_.model_path.empty() ? "models/face_detection_yunet_2023mar.onnx" : config_.model_path;
    if (!face_detector_.initialize(model)) {
        if (!camera_.is_synthetic_mode()) {
            std::cerr << "[Daemon] Failed to initialize face detector engine." << std::endl;
            return false;
        } else {
            std::cout << "[Daemon] Model unavailable for YuNet; face detector running in synthetic simulation mode." << std::endl;
        }
    }

    if (!privacy_trigger_.initialize()) {
        std::cerr << "[Daemon] Failed to initialize privacy triggers." << std::endl;
        return false;
    }

    last_secondary_activity_time_ = std::chrono::system_clock::now();
    return true;
}

bool Daemon::calibrate_primary_user() {
    RawFrame sample_frame;
    if (camera_.grab_frame(sample_frame)) {
        return eavesdropper_detector_.calibrate(sample_frame);
    }
    return false;
}

void Daemon::start() {
    if (running_.load()) return;
    running_.store(true);

    std::cout << "[Daemon] Starting Blindside Phase 2 background daemon (std::jthread)..." << std::endl;

    capture_thread_ = std::jthread([this](std::stop_token st) { capture_loop(st); });
    worker_thread_  = std::jthread([this](std::stop_token st) { worker_loop(st); });
}

void Daemon::stop() {
    if (!running_.load()) return;
    std::cout << "[Daemon] Stopping Blindside background daemon..." << std::endl;

    running_.store(false);
    frame_buffer_.stop();

    if (capture_thread_.joinable()) capture_thread_.request_stop();
    if (worker_thread_.joinable()) worker_thread_.request_stop();

    camera_.close();
    std::cout << "[Daemon] Daemon stopped cleanly." << std::endl;
}

void Daemon::capture_loop(std::stop_token stop_token) {
    int backoff_ms = 1000;
    while (!stop_token.stop_requested() && running_.load()) {
        RawFrame frame;
        if (camera_.grab_frame(frame)) {
            backoff_ms = 1000; // Reset backoff
            if (!frame.buffer.empty()) {
                frame_buffer_.push(std::move(frame));
            }
        } else {
            // Camera failure (or unavailable). Wait and attempt recovery.
            // Sleep in small increments to remain responsive to stop signals
            int waited = 0;
            while (waited < backoff_ms && !stop_token.stop_requested() && running_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                waited += 100;
            }

            if (!stop_token.stop_requested() && running_.load()) {
                std::cout << "[Daemon] Attempting camera recovery..." << std::endl;
                if (camera_.open()) {
                    RawFrame test_frame;
                    if (camera_.grab_frame(test_frame)) {
                        std::cout << "[Daemon] Camera recovered successfully." << std::endl;
                        backoff_ms = 1000;
                        if (!test_frame.buffer.empty()) {
                            frame_buffer_.push(std::move(test_frame));
                        }
                    } else {
                        std::cout << "[Daemon] Camera opened but failed to capture. Backing off..." << std::endl;
                        backoff_ms = std::min(backoff_ms * 2, 16000); // Max 16s
                    }
                } else {
                    backoff_ms = std::min(backoff_ms * 2, 16000);
                }
            }
        }
    }
}

void Daemon::worker_loop(std::stop_token stop_token) {
    while (!stop_token.stop_requested() && running_.load()) {
        auto opt_frame = frame_buffer_.wait_pop_for(std::chrono::milliseconds(200));
        if (!opt_frame.has_value()) continue;

        DaemonState current_state = atomic_state_.load(std::memory_order_acquire);
        if (current_state == DaemonState::PauseDetection) {
            privacy_trigger_.clear_alerts();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        const RawFrame& frame = opt_frame.value();
        
        FrameResult result;
        try {
            result = eavesdropper_detector_.process_frame(frame, current_state);
        } catch (const std::exception& e) {
            std::cerr << "[Daemon] Exception during frame processing: " << e.what() << std::endl;
            continue; // Skip triggering and rate changes on bad frame
        }

        result.daemon_state = current_state;
        processed_frames_++;

        // Execute OS Privacy Triggers (Targeted Blur or Full Workstation Lock)
        if (result.trigger_soft_alert || result.trigger_hard_defense || result.trigger_targeted_blur) {
            std::string threat_class = result.trigger_hard_defense ? "HARD_DEFENSE" : (result.trigger_targeted_blur ? "BLUR_ACTIVE" : "SOFT_ALERT");
            blindside::Logger::get_instance().log_threat("EAVESDROPPER_DETECTED", threat_class, result.secondary_gaze_duration_sec, result.faces.size());
        }
        privacy_trigger_.execute_triggers(result);

        // Adaptive Sampling Rate State Machine (30 FPS active -> 5 FPS idle)
        auto now = std::chrono::system_clock::now();
        if (result.secondary_gaze_detected || result.faces.size() > 1) {
            last_secondary_activity_time_ = now;
            if (current_target_fps_.load() != config_.active_fps) {
                current_target_fps_.store(config_.active_fps);
                camera_.set_fps(config_.active_fps);
                std::cout << "[Daemon] Motion/Eavesdropper active! Elevating frame rate to " 
                          << config_.active_fps << " FPS." << std::endl;
            }
        } else {
            std::chrono::duration<double> idle_elapsed = now - last_secondary_activity_time_;
            if (idle_elapsed.count() >= config_.idle_timeout_sec) {
                if (current_target_fps_.load() != config_.idle_fps) {
                    current_target_fps_.store(config_.idle_fps);
                    camera_.set_fps(config_.idle_fps);
                    std::cout << "[Daemon] Workstation space calm. Dropping to low-power idle rate of " 
                              << config_.idle_fps << " FPS (<2% CPU)." << std::endl;
                }
            }
        }
    }
}

} // namespace blindside
