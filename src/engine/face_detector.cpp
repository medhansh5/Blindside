#include "blindside/face_detector.hpp"
#include <iostream>
#include <fstream>
#include <opencv2/opencv.hpp>
#include <opencv2/objdetect.hpp>

namespace blindside {

FaceDetector::FaceDetector(const Config& config)
    : config_(config) {
}

FaceDetector::~FaceDetector() {
    if (detector_ptr_) {
        delete static_cast<cv::Ptr<cv::FaceDetectorYN>*>(detector_ptr_);
    }
}

bool FaceDetector::initialize(const std::string& model_path) {
    std::ifstream f(model_path.c_str());
    if (!f.good()) {
        std::cerr << "[FaceDetector] Model file not found at: " << model_path << std::endl;
        return false;
    }
    try {
        auto yunet = cv::FaceDetectorYN::create(model_path, "", cv::Size(320, 320), 0.6f, 0.3f, 5000);
        detector_ptr_ = new cv::Ptr<cv::FaceDetectorYN>(yunet);
        initialized_ = true;
        std::cout << "[FaceDetector] Successfully loaded YuNet model: " << model_path << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[FaceDetector] Failed to load YuNet model: " << e.what() << std::endl;
        return false;
    }
}

std::vector<FaceBox> FaceDetector::detect(const RawFrame& frame) {
    if (frame.is_synthetic) {
        return frame.synthetic_faces;
    }

    std::vector<FaceBox> results;
    if (!initialized_ || !detector_ptr_ || frame.buffer.empty()) return results;

    auto* yunet_ptr = static_cast<cv::Ptr<cv::FaceDetectorYN>*>(detector_ptr_);
    if (!*yunet_ptr) return results;

    // Convert raw frame to cv::Mat
    cv::Mat cv_frame(frame.height, frame.width, CV_8UC3, (void*)frame.buffer.data());
    
    // YuNet needs the input size to match the frame size
    (*yunet_ptr)->setInputSize(cv_frame.size());

    cv::Mat faces;
    (*yunet_ptr)->detect(cv_frame, faces);

    for (int i = 0; i < faces.rows; ++i) {
        FaceBox box;
        box.x = faces.at<float>(i, 0);
        box.y = faces.at<float>(i, 1);
        box.width = faces.at<float>(i, 2);
        box.height = faces.at<float>(i, 3);
        
        box.landmarks[0] = {faces.at<float>(i, 4), faces.at<float>(i, 5)};   // Right eye
        box.landmarks[1] = {faces.at<float>(i, 6), faces.at<float>(i, 7)};   // Left eye
        box.landmarks[2] = {faces.at<float>(i, 8), faces.at<float>(i, 9)};   // Nose
        box.landmarks[3] = {faces.at<float>(i, 10), faces.at<float>(i, 11)}; // Right mouth
        box.landmarks[4] = {faces.at<float>(i, 12), faces.at<float>(i, 13)}; // Left mouth
        
        box.confidence = faces.at<float>(i, 14);

        if (box.confidence > 0.6f) {
            results.push_back(box);
        }
    }

    return results;
}

} // namespace blindside
