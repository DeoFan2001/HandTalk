#include "Utils.h"
#include <algorithm>
#include <cmath>

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

    // === ONE EURO FILTER ===
    // alpha = коэффициент low-pass фильтра для заданной частоты среза
    static float lowPassAlpha(float cutoff, float dt) {
        float tau = 1.0f / (2.0f * (float)CV_PI * cutoff);
        return 1.0f / (1.0f + tau / dt);
    }

    float OneEuroFilter::filter(float x, float dt) {
        if (!initialized) {
            xPrev = x;
            dxPrev = 0.0f;
            initialized = true;
            return x;
        }

        // 1. скорость изменения (тоже слегка сглаживаем)
        float dx = (x - xPrev) / dt;
        float aD = lowPassAlpha(OE_D_CUTOFF, dt);
        dxPrev = aD * dx + (1.0f - aD) * dxPrev;

        // 2. чем быстрее движение, тем выше частота среза -> меньше сглаживание
        float cutoff = OE_MIN_CUTOFF + OE_BETA * std::abs(dxPrev);
        float a = lowPassAlpha(cutoff, dt);

        xPrev = a * x + (1.0f - a) * xPrev;
        return xPrev;
    }

    cv::Point3f PointFilter::update(const cv::Point3f& p, float dt) {
        return cv::Point3f(fx.filter(p.x, dt), fy.filter(p.y, dt), fz.filter(p.z, dt));
    }

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
