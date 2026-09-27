#pragma once
#include <opencv2/opencv.hpp>
#include <vector>

namespace HandTracker {

    // === КОНСТАНТЫ ===
    constexpr int   MAX_HANDS = 2;
    constexpr float PALM_THRESHOLD = 0.5f;
    constexpr float PRESENCE_THRESHOLD = 0.5f;
    constexpr float ROI_SCALE = 2.6f;
    constexpr float ROI_SHIFT = 0.5f;
    constexpr int   PALM_INPUT_SIZE = 192;
    constexpr int   HAND_INPUT_SIZE = 224;
    constexpr int   PALM_NUM_ANCHORS = 2016;
    constexpr float KF_PROCESS_NOISE = 1e-4f;
    constexpr float KF_MEASUREMENT_NOISE = 1e-2f;
    constexpr float TRACK_MAX_DIST = 250.0f;
    constexpr int   TRACK_MAX_MISS = 15;
    constexpr int   PALM_SKIP = 3;

    // === ANCHORS ===
    std::vector<cv::Point2f> generateAnchors();

    // === MATH ===
    float rectIoU(const cv::Rect2f& a, const cv::Rect2f& b);

    // === KALMAN ===
    class PointKalman {
        cv::KalmanFilter kf;
        bool initialized = false;
    public:
        PointKalman();
        cv::Point2f update(cv::Point2f m);
        void reset();
    };

    // === СКЕЛЕТ ===
    extern const std::vector<std::pair<int, int>> CONNECTIONS;
    extern const std::vector<int> FINGERTIPS;

} // namespace HandTracker