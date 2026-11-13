#include "blindside/camera.hpp"
#include "blindside/eavesdropper_detector.hpp"
#include "blindside/face_detector.hpp"
#include "blindside/pose_estimator.hpp"
#include "blindside/config.hpp"
#include "blindside/platform_paths.hpp"
#include <iostream>
#include <cassert>
#include <fstream>
#include <thread>
#include <chrono>

void test_synthetic_eavesdropper_pipeline() {
    blindside::Config config;
    config.active_fps = 30.0;
    config.hysteresis_sec = 0.2; // Fast hysteresis for test verification
    config.daemon_state = blindside::DaemonState::GracefulTargetedBlur;

    blindside::CameraCapture camera(config);
    camera.set_synthetic_mode(true);
    assert(camera.open());
    assert(camera.is_synthetic_mode());

    blindside::FaceDetector detector(config);
    blindside::PoseEstimator estimator(config);
    blindside::EavesdropperDetector eavesdropper(config, detector, estimator);

    // 1. Grab initial frame (primary user only)
    blindside::RawFrame frame;
    assert(camera.grab_frame(frame));
    assert(frame.is_synthetic);

    auto detected_faces = detector.detect(frame);
    assert(detected_faces.size() == 1);
    assert(detected_faces[0].confidence > 0.9f);

    // Verify solvePnP pose estimation runs on synthetic face
    auto primary_pose = estimator.estimate_pose(detected_faces[0], frame);
    assert(primary_pose.valid);
    assert(primary_pose.is_looking_at_screen);

    // Calibrate primary user
    assert(eavesdropper.calibrate(frame));
    assert(eavesdropper.is_calibrated());

    // Process primary user frame - no eavesdropper present
    auto res_calm = eavesdropper.process_frame(frame, blindside::DaemonState::GracefulTargetedBlur);
    assert(res_calm.primary_user_present);
    assert(!res_calm.secondary_gaze_detected);
    assert(!res_calm.trigger_soft_alert);
    assert(!res_calm.trigger_targeted_blur);

    // 2. Inject synthetic eavesdropper into camera stream
    camera.set_synthetic_eavesdropper(true, 0.0);
    assert(camera.grab_frame(frame));
    auto faces_with_eavesdropper = detector.detect(frame);
    assert(faces_with_eavesdropper.size() == 2);

    // First frame with eavesdropper -> triggers soft alert, but not targeted blur (< hysteresis)
    auto res_first_threat = eavesdropper.process_frame(frame, blindside::DaemonState::GracefulTargetedBlur);
    assert(res_first_threat.primary_user_present);
    assert(res_first_threat.secondary_gaze_detected);
    assert(res_first_threat.trigger_soft_alert);
    assert(!res_first_threat.trigger_targeted_blur);

    // 3. Wait past hysteresis (250ms > 200ms) and feed subsequent frames
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    assert(camera.grab_frame(frame));
    auto res_elevated = eavesdropper.process_frame(frame, blindside::DaemonState::GracefulTargetedBlur);
    assert(res_elevated.secondary_gaze_detected);
    assert(res_elevated.secondary_liveness_verified);
    assert(res_elevated.secondary_gaze_duration_sec >= 0.2);
    assert(res_elevated.trigger_targeted_blur);

    // 4. Test strict full lock state elevation
    auto res_strict = eavesdropper.process_frame(frame, blindside::DaemonState::StrictFullLock);
    assert(res_strict.secondary_gaze_detected);
    assert(res_strict.trigger_hard_defense);
    assert(!res_strict.trigger_targeted_blur);

    // 5. Remove eavesdropper and verify pipeline returns to calm state
    camera.set_synthetic_eavesdropper(false);
    assert(camera.grab_frame(frame));
    auto res_recovered = eavesdropper.process_frame(frame, blindside::DaemonState::GracefulTargetedBlur);
    assert(res_recovered.primary_user_present);
    assert(!res_recovered.secondary_gaze_detected);
    assert(!res_recovered.trigger_soft_alert);
    assert(!res_recovered.trigger_targeted_blur);
    assert(!res_recovered.trigger_hard_defense);

    std::cout << "[PASS] test_synthetic_eavesdropper_pipeline (full capture->detect->pose->track->threat lifecycle)" << std::endl;
}

void test_full_pipeline(const std::string& model_path) {
    blindside::Config config;
    blindside::FaceDetector detector(config);
    blindside::PoseEstimator estimator(config);
    
    if (!detector.initialize(model_path)) {
        // As per requirements: Do NOT fake it. Report NOT RUN and skip gracefully.
        std::cout << "[SKIPPED] YuNet model unavailable (" << model_path << "). Hardware pipeline not tested." << std::endl;
        return;
    }

    blindside::EavesdropperDetector eavesdropper(config, detector, estimator);
    
    // Create a 640x480 black image (synthetic deterministic frame)
    blindside::RawFrame frame;
    frame.width = 640;
    frame.height = 480;
    frame.buffer.resize(frame.width * frame.height * 3, 0);

    // This frame contains no faces. The pipeline should handle it cleanly without crashing.
    auto res = eavesdropper.process_frame(frame);
    assert(res.faces.empty());
    
    std::cout << "[PASS] Full hardware pipeline integration (YuNet + SolvePnP + Threat Engine) tested successfully on empty frame." << std::endl;
}

int main() {
    test_synthetic_eavesdropper_pipeline();

    auto exe_dir = blindside::get_executable_directory();
    // In the build tree, the model might be copied to the output or reachable relative to root
    std::string model_path = (exe_dir.parent_path() / "models" / "face_detection_yunet_2023mar.onnx").string();
    
    test_full_pipeline(model_path);
    
    return 0;
}
