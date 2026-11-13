#ifndef BLINDSIDE_CAMERA_HPP
#define BLINDSIDE_CAMERA_HPP

#include "blindside/config.hpp"
#include "blindside/types.hpp"
#include <memory>
#include <vector>
#include <cstdint>
#include <chrono>
#include <atomic>

namespace blindside {

struct RawFrame {
    uint64_t frame_id = 0;
    int width = 0;
    int height = 0;
    int channels = 3;
    std::vector<uint8_t> buffer; // BGR format
    std::chrono::system_clock::time_point timestamp;
    bool is_synthetic = false;
    std::vector<FaceBox> synthetic_faces{};
};

class CameraCapture {
public:
    explicit CameraCapture(const Config& config);
    ~CameraCapture();

    bool open();
    void close();
    bool is_opened() const;

    // Capture frame with adaptive sampling interval
    bool grab_frame(RawFrame& frame);

    void set_fps(double target_fps);
    double get_current_fps() const { return current_fps_; }

    void set_synthetic_mode(bool enabled) { synthetic_mode_ = enabled; }
    bool is_synthetic_mode() const { return synthetic_mode_; }

    // Inject a synthetic test frame containing simulated face bounding boxes
    void set_synthetic_eavesdropper(bool present, double gaze_yaw = 0.0);

private:
    Config config_;
    std::atomic<bool> is_open_ = false;
    std::atomic<double> current_fps_ = 30.0;
    uint64_t frame_counter_ = 0;
    bool synthetic_mode_ = false;
    bool synthetic_eavesdropper_present_ = false;
    double synthetic_eavesdropper_yaw_ = 0.0;
    std::chrono::steady_clock::time_point last_frame_time_;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace blindside

#endif // BLINDSIDE_CAMERA_HPP
