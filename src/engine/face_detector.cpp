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
    clear_error();
    std::ifstream f(model_path.c_str());
    if (!f.good()) {
        std::string err = "Model file not found at: " + model_path;
        set_error(err);
        std::cerr << "[FaceDetector] " << err << std::endl;
        return false;
    }
    try {
        auto yunet = cv::FaceDetectorYN::create(model_path, "", cv::Size(320, 320), 0.6f, 0.3f, 5000);
        if (!yunet) {
            std::string err = "cv::FaceDetectorYN::create returned null";
            set_error(err);
            std::cerr << "[FaceDetector] " << err << std::endl;
            return false;
        }
        if (detector_ptr_) {
            delete static_cast<cv::Ptr<cv::FaceDetectorYN>*>(detector_ptr_);
        }
        detector_ptr_ = new cv::Ptr<cv::FaceDetectorYN>(yunet);
        initialized_ = true;
        std::cout << "[FaceDetector] Successfully loaded YuNet model: " << model_path << std::endl;
        return true;
    } catch (const cv::Exception& e) {
        std::string err = std::string("OpenCV exception: ") + e.what();
        set_error(err);
        std::cerr << "[FaceDetector] Failed to load YuNet model: " << err << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::string err = e.what();
        set_error(err);
        std::cerr << "[FaceDetector] Failed to load YuNet model: " << err << std::endl;
        return false;
    }
}

DetectionResult FaceDetector::detect(const RawFrame& frame) {
    if (frame.is_synthetic) {
        return DetectionResult::successful(frame.synthetic_faces);
    }

    if (!initialized_) {
        set_error("FaceDetector is not initialized");
        return DetectionResult::failure("FaceDetector is not initialized");
    }

    if (!detector_ptr_) {
        set_error("Detector instance pointer is null");
        return DetectionResult::failure("Detector instance pointer is null");
    }

    if (frame.width <= 0 || frame.height <= 0) {
        set_error("Invalid frame dimensions");
        return DetectionResult::failure("Invalid frame dimensions");
    }

    if (frame.buffer.empty()) {
        set_error("Frame buffer is empty");
        return DetectionResult::failure("Frame buffer is empty");
    }

    auto* yunet_ptr = static_cast<cv::Ptr<cv::FaceDetectorYN>*>(detector_ptr_);
    if (!*yunet_ptr) {
        set_error("Underlying FaceDetectorYN pointer is null");
        return DetectionResult::failure("Underlying FaceDetectorYN pointer is null");
    }

    size_t expected_bytes = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height) * 3;
    if (frame.buffer.size() < expected_bytes) {
        std::string err = "Buffer size mismatch: expected at least " + std::to_string(expected_bytes) +
                          " bytes, got " + std::to_string(frame.buffer.size());
        set_error(err);
        std::cerr << "[FaceDetector] " << err << std::endl;
        return DetectionResult::failure(err);
    }

    try {
        // Convert raw frame to cv::Mat
        cv::Mat cv_frame(frame.height, frame.width, CV_8UC3, const_cast<uint8_t*>(frame.buffer.data()));

        // YuNet needs the input size to match the frame size
        (*yunet_ptr)->setInputSize(cv_frame.size());

        cv::Mat faces;
        (*yunet_ptr)->detect(cv_frame, faces);

        std::vector<FaceBox> results;
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
        clear_error();
        return DetectionResult::successful(std::move(results));
    } catch (const cv::Exception& e) {
        std::string err = std::string("OpenCV inference exception: ") + e.what();
        set_error(err);
        std::cerr << "[FaceDetector] " << err << std::endl;
        return DetectionResult::failure(err);
    } catch (const std::exception& e) {
        std::string err = std::string("Inference exception: ") + e.what();
        set_error(err);
        std::cerr << "[FaceDetector] " << err << std::endl;
        return DetectionResult::failure(err);
    }
}

} // namespace blindside
