#ifndef BLINDSIDE_FACE_DETECTOR_HPP
#define BLINDSIDE_FACE_DETECTOR_HPP

#include "blindside/config.hpp"
#include "blindside/types.hpp"
#include "blindside/camera.hpp"
#include <vector>
#include <string>
#include <mutex>

namespace cv {
    class FaceDetectorYN;
}

namespace blindside {

class IFaceDetector {
public:
    virtual ~IFaceDetector() = default;
    virtual bool initialize(const std::string& model_path = "") = 0;
    virtual DetectionResult detect(const RawFrame& frame) = 0;
    virtual bool is_initialized() const = 0;
};

class FaceDetector : public IFaceDetector {
public:
    explicit FaceDetector(const Config& config);
    ~FaceDetector() override;

    bool initialize(const std::string& model_path = "models/face_detection_yunet_2023mar.onnx") override;
    
    DetectionResult detect(const RawFrame& frame) override;

    bool is_initialized() const override { return initialized_; }
    std::string last_error() const {
        std::lock_guard<std::mutex> lock(error_mutex_);
        return last_error_;
    }

    void set_error(std::string err) {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_ = std::move(err);
    }

    void clear_error() {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_.clear();
    }

private:
    Config config_;
    bool initialized_ = false;
    mutable std::mutex error_mutex_;
    std::string last_error_;

    void* detector_ptr_ = nullptr;
};

} // namespace blindside

#endif // BLINDSIDE_FACE_DETECTOR_HPP
