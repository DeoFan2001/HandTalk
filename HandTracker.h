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
        int id = 0;
        int handedness = 0;          // 0/1, получено сглаживанием handProb
        float handProb = 0.5f;       // сглаженная вероятность "рука 1" (убирает мигание L/R)
        std::array<cv::Point3f, 21> points;   // СГЛАЖЕННЫЕ точки (для рисования и распознавания жестов)
        std::array<cv::Point3f, 21> raw;      // СЫРЫЕ точки (из них считаем ROI на следующий кадр)
        float score = 0.0f;
        int missedFrames = 0;
        bool active = true;
        std::array<PointFilter, 21> filters;

        cv::Point2f center() const;
        float size() const;          // размер руки в пикселях (запястье -> самая дальняя точка)
        void updatePoints(const std::array<cv::Point3f, 21>& newPts, float dt);
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
        std::vector<float> inputData;     // буфер входного тензора (создаём один раз)

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

    // === ROI (область, которую вырезаем для модели ключевых точек) ===
    struct Roi {
        float cx = 0, cy = 0;   // центр
        float side = 0;         // сторона квадрата
        float angleDeg = 0;     // поворот, чтобы рука стояла пальцами вверх
    };

    // ROI по результату детектора ладони (когда руку только нашли)
    Roi roiFromPalm(const PalmDetector::Detection& palm);
    // ROI по ключевым точкам прошлого кадра (когда рука уже отслеживается)
    Roi roiFromLandmarks(const std::array<cv::Point3f, 21>& pts);

    // === HAND LANDMARKER ===
    class HandLandmarker {
        std::unique_ptr<Ort::Env> env;
        std::unique_ptr<Ort::SessionOptions> sessionOptions;
        std::unique_ptr<Ort::Session> session;
        Ort::AllocatorWithDefaultOptions allocator;

        std::string inputName;
        std::string outScreen, outPresence, outHandedness, outWorld;
        std::vector<float> inputData;

    public:
        struct Result {
            bool detected = false;
            float presence = 0;
            int handedness = 0;
            float handednessProb = 0.5f;
            std::array<cv::Point3f, 21> points;
        };

        bool init(const std::string& modelPath);
        Result predict(const cv::Mat& frame, const Roi& roi, float presenceThreshold = PRESENCE_THRESHOLD);
    };

    // === ОТРИСОВКА РУКИ ===
    void drawHand(cv::Mat& frame, const HandTrack& hand, cv::Scalar color, int id);

} // namespace HandTracker
