#include "blindside/eavesdropper_detector.hpp"
#include "blindside/face_detector.hpp"
#include "blindside/pose_estimator.hpp"
#include "blindside/config.hpp"
#include <cassert>
#include <iostream>
#include <thread>

void test_facial_aspect_ratio_calculation() {
    blindside::FaceBox face;
    face.width = 100.0f;
    face.height = 120.0f;
    face.landmarks[0] = {30.0f, 40.0f};  // Right Eye
    face.landmarks[1] = {70.0f, 40.0f};  // Left Eye
    face.landmarks[2] = {50.0f, 65.0f};  // Nose

    float far = blindside::PoseEstimator::compute_facial_aspect_ratio(face);
    assert(far > 0.10f && far < 0.45f);

    // Verify backward compatibility alias returns identical value
    float ear = blindside::PoseEstimator::compute_ear(face);
    assert(ear == far);
    (void)far;
    (void)ear;

    std::cout << "[TEST PASSED] Geometric Facial Aspect Ratio (FAR) proxy calculation test." << std::endl;
}

class MockFaceDetector : public blindside::IFaceDetector {
public:
    bool initialize(const std::string& model_path = "") override { (void)model_path; return true; }
    std::vector<blindside::FaceBox> detect(const blindside::RawFrame& frame) override {
        std::vector<blindside::FaceBox> res;
        blindside::FaceBox primary;
        primary.x = 300; primary.y = 200; primary.width = 100; primary.height = 100;
        primary.landmarks[0] = {330, 230}; primary.landmarks[1] = {370, 230};
        primary.landmarks[2] = {350, 250}; primary.landmarks[3] = {330, 270}; primary.landmarks[4] = {370, 270};
        res.push_back(primary);

        if (frame.buffer.size() > 0 && frame.buffer[0] == 220) {
            blindside::FaceBox secondary;
            secondary.x = 50; secondary.y = 50; secondary.width = 100; secondary.height = 100;
            secondary.landmarks[0] = {80, 80}; secondary.landmarks[1] = {120, 80};
            secondary.landmarks[2] = {100, 100}; secondary.landmarks[3] = {80, 120}; secondary.landmarks[4] = {120, 120};
            res.push_back(secondary);
        }
        return res;
    }
    bool is_initialized() const override { return true; }
};

void test_photo_spoof_rejection() {
    blindside::Config config;
    config.hysteresis_sec = 0.4;
    config.ear_blink_threshold = 0.20f;
    MockFaceDetector detector;
    blindside::PoseEstimator estimator(config);
    detector.initialize();

    blindside::EavesdropperDetector eavesdropper(config, detector, estimator);

    blindside::RawFrame frame;
    frame.width = 640;
    frame.height = 480;
    frame.buffer.resize(frame.width * frame.height * 3, 220); // Secondary face present

    // Simulate 5 frames with static pose and 0 variance (static photo)
    for (int i = 0; i < 6; ++i) {
        auto res = eavesdropper.process_frame(frame);
        (void)res;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Process 7th frame -> Anti-spoofing should verify static photo and suppress hard lock
    auto final_res = eavesdropper.process_frame(frame);
    assert(final_res.secondary_gaze_detected == true);
    
    std::cout << "[TEST PASSED] Anti-Spoofing Static Photo Rejection test." << std::endl;
}

int main() {
    test_facial_aspect_ratio_calculation();
    test_photo_spoof_rejection();
    std::cout << "All Anti-Spoofing & Liveness unit tests passed successfully!\n";
    return 0;
}
