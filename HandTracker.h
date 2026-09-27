#pragma once
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>
#include <memory>
#include <array>
#include <string>
#include <vector>
#include "Utils.h"

namespace HandTracker {

    // === ТРЕК ОДНОЙ РУКИ ===
    struct HandTrack {
        int id;
        int handedness;
        std::array<cv::Point3f, 21> points;
        float score;
        int missedFrames;
        bool active;
        std::array<PointKalman, 21> kalmans;

        cv::Point2f center() const;
        void updatePoints(const std::array<cv::Point3f, 21>& newPts);
    };

    // === PALM DETECTOR ===
    class PalmDetector {
        std::unique_ptr<Ort::Env> env;
        std::unique_ptr<Ort::SessionOptions> sessionOptions;
        std::unique_ptr<Ort::Session> session;
        Ort::AllocatorWithDefaultOptions allocator;

        std::string inputName;
        std::string outRegressors;
        std::string outClassificators;
        std::vector<cv::Point2f> anchors;

    public:
        struct Detection {
            float score;
            float cx, cy, width;
            cv::Point2f wrist;
            cv::Point2f mcp;
        };

        bool init(const std::string& modelPath);
        std::vector<Detection> detect(const cv::Mat& frame, float threshold = 0.5f);
    };

    // === HAND LANDMARKER ===
    class HandLandmarker {
        std::unique_ptr<Ort::Env> env;
        std::unique_ptr<Ort::SessionOptions> sessionOptions;
        std::unique_ptr<Ort::Session> session;
        Ort::AllocatorWithDefaultOptions allocator;

        std::string inputName;
        std::string outScreen, outPresence, outHandedness, outWorld;

    public:
        struct Result {
            bool detected = false;
            float presence = 0;
            int handedness = 0;
            std::array<cv::Point3f, 21> points;
        };

        bool init(const std::string& modelPath);
        Result predict(const cv::Mat& frame, const PalmDetector::Detection& palm);
    };

    // === ОТРИСОВКА РУКИ ===
    void drawHand(cv::Mat& frame, const HandTrack& hand, cv::Scalar color, int id);

} // namespace HandTracker