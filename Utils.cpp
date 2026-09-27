#include "Utils.h"
#include <algorithm>

namespace HandTracker {

    // === ANCHORS ===
    std::vector<cv::Point2f> generateAnchors() {
        std::vector<cv::Point2f> anchors;
        anchors.reserve(PALM_NUM_ANCHORS);

        int cells0 = PALM_INPUT_SIZE / 8;
        for (int y = 0; y < cells0; ++y)
            for (int x = 0; x < cells0; ++x) {
                float cx = (x + 0.5f) / cells0;
                float cy = (y + 0.5f) / cells0;
                anchors.push_back({ cx, cy });
                anchors.push_back({ cx, cy });
            }

        int cells1 = PALM_INPUT_SIZE / 16;
        for (int y = 0; y < cells1; ++y)
            for (int x = 0; x < cells1; ++x) {
                float cx = (x + 0.5f) / cells1;
                float cy = (y + 0.5f) / cells1;
                for (int k = 0; k < 6; ++k) anchors.push_back({ cx, cy });
            }

        return anchors;
    }

    // === IoU ===
    float rectIoU(const cv::Rect2f& a, const cv::Rect2f& b) {
        float x1 = std::max(a.x, b.x);
        float y1 = std::max(a.y, b.y);
        float x2 = std::min(a.x + a.width, b.x + b.width);
        float y2 = std::min(a.y + a.height, b.y + b.height);
        float iw = std::max(0.0f, x2 - x1);
        float ih = std::max(0.0f, y2 - y1);
        float inter = iw * ih;
        float uni = a.width * a.height + b.width * b.height - inter;
        if (uni < 1e-6f) return 0.0f;
        return inter / uni;
    }

    // === KALMAN ===
    PointKalman::PointKalman() {
        kf.init(4, 2, 0, CV_32F);
        kf.transitionMatrix = (cv::Mat_<float>(4, 4) <<
            1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1);
        cv::setIdentity(kf.measurementMatrix);
        cv::setIdentity(kf.processNoiseCov, cv::Scalar::all(KF_PROCESS_NOISE));
        cv::setIdentity(kf.measurementNoiseCov, cv::Scalar::all(KF_MEASUREMENT_NOISE));
        cv::setIdentity(kf.errorCovPost, cv::Scalar::all(1.0f));
    }

    cv::Point2f PointKalman::update(cv::Point2f m) {
        if (!initialized) {
            kf.statePost.at<float>(0) = m.x;
            kf.statePost.at<float>(1) = m.y;
            kf.statePost.at<float>(2) = 0;
            kf.statePost.at<float>(3) = 0;
            initialized = true;
            return m;
        }
        kf.predict();
        cv::Mat meas = (cv::Mat_<float>(2, 1) << m.x, m.y);
        cv::Mat corrected = kf.correct(meas);
        return cv::Point2f(corrected.at<float>(0), corrected.at<float>(1));
    }

    void PointKalman::reset() { initialized = false; }

    // === СКЕЛЕТ ===
    const std::vector<std::pair<int, int>> CONNECTIONS = {
        {0,1},{1,2},{2,3},{3,4},
        {0,5},{5,6},{6,7},{7,8},
        {5,9},{9,10},{10,11},{11,12},
        {9,13},{13,14},{14,15},{15,16},
        {13,17},{17,18},{18,19},{19,20},
        {0,17}
    };

    const std::vector<int> FINGERTIPS = { 4,8,12,16,20 };

} // namespace HandTracker