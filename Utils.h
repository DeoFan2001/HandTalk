#pragma once
#include <opencv2/opencv.hpp>
#include <vector>

namespace HandTracker {

    // === КОНСТАНТЫ ===
    constexpr int   MAX_HANDS = 2;
    constexpr float PALM_THRESHOLD = 0.5f;
    constexpr float PRESENCE_THRESHOLD = 0.5f;         // порог для НОВОЙ руки
    constexpr float TRACK_PRESENCE_THRESHOLD = 0.3f;   // порог для руки, которую уже ведём (ниже -> кулак не теряется)
    constexpr float ROI_SCALE = 2.6f;                  // ROI по ладони (детектор)
    constexpr float ROI_SHIFT = 0.5f;
    constexpr float ROI_LM_SCALE = 2.0f;               // ROI по ключевым точкам прошлого кадра
    constexpr float ROI_LM_SHIFT_Y = -0.1f;            // сдвиг ROI в сторону пальцев
    constexpr int   PALM_INPUT_SIZE = 192;
    constexpr int   HAND_INPUT_SIZE = 224;
    constexpr int   PALM_NUM_ANCHORS = 2016;
    constexpr int   TRACK_MAX_MISS = 3;                // сколько кадров держим руку без подтверждения
    constexpr int   PALM_SKIP = 3;                     // детектор ладони раз в N кадров (если есть свободное место)

    // Параметры сглаживания One Euro
    constexpr float OE_MIN_CUTOFF = 1.5f;   // меньше -> плавнее, когда рука стоит (но больше дрожание убирается)
    constexpr float OE_BETA = 0.02f;        // больше -> меньше запаздывание, когда рука движется
    constexpr float OE_D_CUTOFF = 1.0f;     // сглаживание оценки скорости

    // === ANCHORS ===
    std::vector<cv::Point2f> generateAnchors();

    // === СГЛАЖИВАНИЕ (One Euro Filter) ===
    // Идея: простой low-pass фильтр x = a*новое + (1-a)*старое,
    // но коэффициент a зависит от скорости:
    //   рука стоит    -> a маленький -> сильное сглаживание, дрожание уходит
    //   рука движется -> a большой   -> почти нет запаздывания
    class OneEuroFilter {
        bool initialized = false;
        float xPrev = 0.0f, dxPrev = 0.0f;
    public:
        float filter(float x, float dt);
    };

    // Фильтр для одной точки (x, y, z)
    class PointFilter {
        OneEuroFilter fx, fy, fz;
    public:
        cv::Point3f update(const cv::Point3f& p, float dt);
    };

    // === СКЕЛЕТ ===
    extern const std::vector<std::pair<int, int>> CONNECTIONS;
    extern const std::vector<int> FINGERTIPS;

} // namespace HandTracker
