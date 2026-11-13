#include "blindside/camera.hpp"
#include <iostream>
#include <thread>
#include <cmath>

#if __has_include(<opencv2/opencv.hpp>)
#include <opencv2/opencv.hpp>
#define BLINDSIDE_HAS_OPENCV 1
#else
#define BLINDSIDE_HAS_OPENCV 0
#endif

namespace blindside {

struct CameraCapture::Impl {
#if BLINDSIDE_HAS_OPENCV
    cv::VideoCapture cap;
#endif
};

CameraCapture::CameraCapture(const Config& config)
    : config_(config), current_fps_(config.active_fps), impl_(std::make_unique<Impl>()) {
}

CameraCapture::~CameraCapture() {
    close();
}

bool CameraCapture::open() {
    if (synthetic_mode_) {
        is_open_ = true;
        std::cout << "[CameraCapture] Initialized in Synthetic / Emulation Mode." << std::endl;
        return true;
    }

#if BLINDSIDE_HAS_OPENCV
    if (impl_->cap.open(config_.camera_index)) {
        impl_->cap.set(cv::CAP_PROP_FRAME_WIDTH, config_.capture_width);
        impl_->cap.set(cv::CAP_PROP_FRAME_HEIGHT, config_.capture_height);
        impl_->cap.set(cv::CAP_PROP_FPS, current_fps_);
        is_open_ = true;
        std::cout << "[CameraCapture] Hardware camera index " << config_.camera_index 
                  << " opened successfully." << std::endl;
        return true;
    }
#endif

    std::cout << "[CameraCapture] Hardware camera unavailable. Falling back to synthetic stream." << std::endl;
    synthetic_mode_ = true;
    is_open_ = true;
    return true;
}

void CameraCapture::close() {
#if BLINDSIDE_HAS_OPENCV
    if (impl_->cap.isOpened()) {
        impl_->cap.release();
    }
#endif
    is_open_ = false;
}

bool CameraCapture::is_opened() const {
    return is_open_;
}

void CameraCapture::set_fps(double target_fps) {
    if (current_fps_.load() != target_fps) {
        current_fps_.store(target_fps);
    }
}

void CameraCapture::set_synthetic_eavesdropper(bool present, double gaze_yaw) {
    synthetic_eavesdropper_present_ = present;
    synthetic_eavesdropper_yaw_ = gaze_yaw;
}

bool CameraCapture::grab_frame(RawFrame& frame) {
    if (!is_open_) return false;

    frame.frame_id = ++frame_counter_;
    frame.timestamp = std::chrono::system_clock::now();
    frame.width = config_.capture_width;
    frame.height = config_.capture_height;
    frame.channels = 3;

    if (synthetic_mode_) {
        frame.is_synthetic = true;
        // Generate synthetic frame buffer (BGR gradient image)
        frame.buffer.resize(frame.width * frame.height * 3);
        uint8_t base_color = static_cast<uint8_t>((frame_counter_ * 5) % 256);
        
        for (int i = 0; i < frame.width * frame.height * 3; i += 3) {
            frame.buffer[i]     = base_color;       // Blue
            frame.buffer[i + 1] = 120;              // Green
            frame.buffer[i + 2] = 200 - base_color; // Red
        }

        // Generate synthetic primary user face (centered in frame with micro-movements)
        frame.synthetic_faces.clear();
        double t = static_cast<double>(frame_counter_) * 0.15;
        float jitter_x = static_cast<float>(std::sin(t) * 1.5);
        float jitter_y = static_cast<float>(std::cos(t * 0.7) * 1.2);

        float fw = static_cast<float>(frame.width);
        float fh = static_cast<float>(frame.height);

        FaceBox primary_box;
        primary_box.width = fw * 0.28f;
        primary_box.height = fh * 0.38f;
        primary_box.x = (fw - primary_box.width) * 0.5f + jitter_x;
        primary_box.y = (fh - primary_box.height) * 0.5f + jitter_y;
        primary_box.confidence = 0.95f;

        float pcx = primary_box.center_x();
        float pcy = primary_box.center_y();
        primary_box.landmarks[0] = {pcx - fw * 0.06f, pcy - fh * 0.05f}; // Right eye
        primary_box.landmarks[1] = {pcx + fw * 0.06f, pcy - fh * 0.05f}; // Left eye
        primary_box.landmarks[2] = {pcx, pcy};                            // Nose
        primary_box.landmarks[3] = {pcx - fw * 0.04f, pcy + fh * 0.07f}; // Right mouth
        primary_box.landmarks[4] = {pcx + fw * 0.04f, pcy + fh * 0.07f}; // Left mouth
        frame.synthetic_faces.push_back(primary_box);

        // If synthetic eavesdropper is injected, generate secondary face
        if (synthetic_eavesdropper_present_) {
            FaceBox eavesdropper_box;
            eavesdropper_box.width = fw * 0.18f;
            eavesdropper_box.height = fh * 0.24f;
            float ejitter_x = static_cast<float>(std::sin(t * 1.3) * 1.0);
            float ejitter_y = static_cast<float>(std::cos(t * 0.9) * 0.8);
            eavesdropper_box.x = fw * 0.08f + ejitter_x;
            eavesdropper_box.y = fh * 0.12f + ejitter_y;
            eavesdropper_box.confidence = 0.92f;

            float ecx = eavesdropper_box.center_x();
            float ecy = eavesdropper_box.center_y();
            float yaw_shift = static_cast<float>(synthetic_eavesdropper_yaw_ * 0.35);

            eavesdropper_box.landmarks[0] = {ecx - fw * 0.04f, ecy - fh * 0.035f};
            eavesdropper_box.landmarks[1] = {ecx + fw * 0.04f, ecy - fh * 0.035f};
            eavesdropper_box.landmarks[2] = {ecx + yaw_shift, ecy};
            eavesdropper_box.landmarks[3] = {ecx - fw * 0.03f, ecy + fh * 0.045f};
            eavesdropper_box.landmarks[4] = {ecx + fw * 0.03f, ecy + fh * 0.045f};
            frame.synthetic_faces.push_back(eavesdropper_box);
        }

        // Simulate frame timing based on current target FPS
        double frame_delay_ms = 1000.0 / std::max(1.0, current_fps_.load());
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(frame_delay_ms)));
        return true;
    }

#if BLINDSIDE_HAS_OPENCV
    if (impl_->cap.isOpened()) {
        auto now = std::chrono::steady_clock::now();
        double target_interval = 1.0 / current_fps_.load();
        std::chrono::duration<double> elapsed = now - last_frame_time_;
        
        if (elapsed.count() < target_interval) {
            // Buffer drain to prevent stale frames, without decoding/processing
            if (!impl_->cap.grab()) {
                std::cerr << "[CameraCapture] Hardware frame read failed or pipeline halted. Closing camera to recover..." << std::endl;
                impl_->cap.release();
                is_open_.store(false);
                return false;
            }
            frame.buffer.clear();
            return true; // Successfully skipped
        }

        cv::Mat cv_frame;
        if (impl_->cap.read(cv_frame) && !cv_frame.empty()) {
            last_frame_time_ = std::chrono::steady_clock::now();
            frame.width = cv_frame.cols;
            frame.height = cv_frame.rows;
            size_t data_size = cv_frame.total() * cv_frame.elemSize();
            frame.buffer.assign(cv_frame.data, cv_frame.data + data_size);
            return true;
        } else {
            std::cerr << "[CameraCapture] Hardware frame read failed or pipeline halted. Closing camera to recover..." << std::endl;
            impl_->cap.release();
            is_open_.store(false);
            return false;
        }
    }
#endif

    // Fallback if not opened or synthetic mode
    frame.buffer.resize(frame.width * frame.height * 3, 0);
    return false;
}

} // namespace blindside
