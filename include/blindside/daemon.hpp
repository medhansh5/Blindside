#ifndef BLINDSIDE_DAEMON_HPP
#define BLINDSIDE_DAEMON_HPP

#include "blindside/config.hpp"
#include "blindside/ring_buffer.hpp"
#include "blindside/camera.hpp"
#include "blindside/face_detector.hpp"
#include "blindside/pose_estimator.hpp"
#include "blindside/eavesdropper_detector.hpp"
#include "blindside/privacy_trigger.hpp"
#include "blindside/tray_icon.hpp"

#include <thread>
#include <atomic>
#include <memory>
#include <chrono>

namespace blindside {

class Daemon {
public:
    explicit Daemon(const Config& config);
    ~Daemon();

    /**
     * @brief Initializes daemon subsystems (camera, face detector, triggers).
     */
    bool initialize();

    /**
     * @brief Starts the background capture and vision worker threads using C++20 std::jthread.
     */
    void start();

    /**
     * @brief Gracefully stops the daemon threads and cleans up resources.
     */
    void stop();

    /**
     * @brief Runs calibration for primary user face position.
     */
    bool calibrate_primary_user();

    /**
     * @brief Lock-free update of daemon state (Strict, Graceful, Pause).
     */
    void set_daemon_state(DaemonState state) {
        atomic_state_.store(state, std::memory_order_release);
    }

    DaemonState get_daemon_state() const {
        return atomic_state_.load(std::memory_order_acquire);
    }

    bool is_running() const { return running_.load(); }
    double get_current_fps() const { return current_target_fps_.load(); }
    uint64_t get_processed_frame_count() const { return processed_frames_.load(); }

    void set_synthetic_mode(bool synthetic) {
        camera_.set_synthetic_mode(synthetic);
    }

    void inject_synthetic_eavesdropper(bool present, double yaw = 0.0) {
        camera_.set_synthetic_eavesdropper(present, yaw);
    }

private:
    void capture_loop(std::stop_token stop_token);
    void worker_loop(std::stop_token stop_token);

    Config config_;
    CameraCapture camera_;
    FaceDetector face_detector_;
    PoseEstimator pose_estimator_;
    EavesdropperDetector eavesdropper_detector_;
    PrivacyTriggerManager privacy_trigger_;

    RingBuffer<RawFrame, 4> frame_buffer_;

    std::atomic<bool> running_{false};
    std::atomic<double> current_target_fps_{30.0};
    std::atomic<uint64_t> processed_frames_{0};
    std::atomic<DaemonState> atomic_state_{DaemonState::GracefulTargetedBlur};

    std::chrono::system_clock::time_point last_secondary_activity_time_;

    std::jthread capture_thread_;
    std::jthread worker_thread_;
};

} // namespace blindside

#endif // BLINDSIDE_DAEMON_HPP
