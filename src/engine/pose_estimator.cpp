#include "blindside/pose_estimator.hpp"

#include <opencv2/opencv.hpp>
#include <opencv2/calib3d.hpp>

namespace blindside {

constexpr double M_PI_VAL = 3.14159265358979323846;

PoseEstimator::PoseEstimator(const Config& config) : config_(config) {}

float PoseEstimator::compute_facial_aspect_ratio(const FaceBox& face) {
    const auto& eye_r = face.landmarks[0];
    const auto& eye_l = face.landmarks[1];
    const auto& nose  = face.landmarks[2];

    float dx = eye_l.x - eye_r.x;
    float dy = eye_l.y - eye_r.y;
    float interocular_dist = std::sqrt(dx * dx + dy * dy);

    if (interocular_dist < 0.001f) return 0.30f;

    float eye_center_y = (eye_r.y + eye_l.y) * 0.5f;
    float nose_dist_v = std::abs(nose.y - eye_center_y);

    // 5-point geometric facial aspect ratio proxy: vertical eye-to-nose vs interocular distance
    float far = (nose_dist_v > 0.001f) ? (nose_dist_v / (1.8f * interocular_dist)) : 0.30f;
    return std::clamp(far, 0.05f, 0.45f);
}

float PoseEstimator::compute_ear(const FaceBox& face) {
    return compute_facial_aspect_ratio(face);
}

Point3f PoseEstimator::compute_gaze_vector(double pitch_deg, double yaw_deg) {
    double pitch_rad = pitch_deg * M_PI_VAL / 180.0;
    double yaw_rad   = yaw_deg   * M_PI_VAL / 180.0;

    Point3f gaze;
    gaze.x = static_cast<float>(std::sin(yaw_rad) * std::cos(pitch_rad));
    gaze.y = static_cast<float>(-std::sin(pitch_rad));
    gaze.z = static_cast<float>(std::cos(yaw_rad) * std::cos(pitch_rad));
    return gaze;
}

HeadPose PoseEstimator::estimate_pose(const FaceBox& face, const RawFrame& frame) {
    HeadPose pose;
    if (frame.width <= 0 || frame.height <= 0) return pose;

    // 1. Generic 3D facial model (5 points corresponding to YuNet output)
    std::vector<cv::Point3d> model_points;
    model_points.push_back(cv::Point3d(-225.0f, 170.0f, -135.0f)); // Right eye
    model_points.push_back(cv::Point3d( 225.0f, 170.0f, -135.0f)); // Left eye
    model_points.push_back(cv::Point3d(   0.0f,   0.0f,    0.0f)); // Nose
    model_points.push_back(cv::Point3d(-150.0f, -150.0f, -125.0f)); // Right mouth corner
    model_points.push_back(cv::Point3d( 150.0f, -150.0f, -125.0f)); // Left mouth corner

    // 2. 2D image points from detected face
    std::vector<cv::Point2d> image_points;
    for (int i = 0; i < 5; ++i) {
        image_points.push_back(cv::Point2d(face.landmarks[i].x, face.landmarks[i].y));
    }

    // 3. Approximate Camera Calibration
    double focal_length = frame.width; 
    cv::Point2d center = cv::Point2d(frame.width / 2.0, frame.height / 2.0);
    cv::Mat camera_matrix = (cv::Mat_<double>(3, 3) << focal_length, 0, center.x,
                                                       0, focal_length, center.y,
                                                       0, 0, 1);
    cv::Mat dist_coeffs = cv::Mat::zeros(4, 1, cv::DataType<double>::type); 

    try {
        // 4. SolvePnP
        cv::Mat rotation_vector; 
        cv::Mat translation_vector;
        bool success = cv::solvePnP(model_points, image_points, camera_matrix, dist_coeffs, rotation_vector, translation_vector, false, cv::SOLVEPNP_EPNP);

        if (!success || rotation_vector.empty()) {
            pose.valid = false;
            return pose;
        }

        // 5. Convert rotation vector to Euler angles (pitch, yaw, roll)
        cv::Mat rmat;
        cv::Rodrigues(rotation_vector, rmat);

        // From rotation matrix to Euler angles
        double m00 = rmat.at<double>(0, 0);
        double m10 = rmat.at<double>(1, 0);
        double m20 = rmat.at<double>(2, 0);
        double m21 = rmat.at<double>(2, 1);
        double m22 = rmat.at<double>(2, 2);

        double sy = std::sqrt(m00 * m00 + m10 * m10);
        bool singular = sy < 1e-6;

        double x, y, z;
        if (!singular) {
            x = std::atan2(m21, m22);
            y = std::atan2(-m20, sy);
            z = std::atan2(m10, m00);
        } else {
            x = std::atan2(-rmat.at<double>(1, 2), rmat.at<double>(1, 1));
            y = std::atan2(-m20, sy);
            z = 0;
        }

        // Convert to degrees
        pose.pitch_deg = x * 180.0 / M_PI_VAL;
        pose.yaw_deg = y * 180.0 / M_PI_VAL;
        pose.roll_deg = z * 180.0 / M_PI_VAL;

        // Normalize angles if model Z is flipped (giving angles near 180 or -180)
        if (pose.pitch_deg > 90.0) pose.pitch_deg -= 180.0;
        else if (pose.pitch_deg < -90.0) pose.pitch_deg += 180.0;

        // Adjust signs/orientation to match coordinate system convention
        pose.yaw_deg = -pose.yaw_deg;

        // Compute 3D gaze vector
        pose.gaze_vector = compute_gaze_vector(pose.pitch_deg, pose.yaw_deg);

        // Check if gaze points at screen
        pose.is_looking_at_screen = is_gaze_directed_at_screen(pose);

        pose.valid = true;

    } catch (const cv::Exception& e) {
        pose.valid = false;
    }

    return pose;
}

bool PoseEstimator::is_gaze_directed_at_screen(const HeadPose& pose) const {
    return (std::abs(pose.yaw_deg) <= config_.max_allowed_yaw_deg) &&
           (std::abs(pose.pitch_deg) <= config_.max_allowed_pitch_deg);
}

} // namespace blindside
