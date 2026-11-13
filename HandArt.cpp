#include "HandArt.h"
#include <algorithm>
#include <cmath>

namespace HandTracker {

    // === СРЕДНЯЯ ПОЗА БУКВЫ ===
    HandRefs buildHandRefs(const std::vector<Sample>& samples) {
        HandRefs r;
        int n[ALPHABET_SIZE] = {};
        for (std::size_t i = 0; i < ALPHABET_SIZE; ++i) r.f[i].fill(0.0f);
        for (const Sample& s : samples) {
            int i = indexFromCode(s.code);
            if (i < 0) continue;
            for (int k = 0; k < FEATURE_SIZE; ++k) r.f[i][k] += s.f[k];
            ++n[i];
        }
        for (std::size_t i = 0; i < ALPHABET_SIZE; ++i) {
            if (n[i] == 0) continue;
            for (int k = 0; k < FEATURE_SIZE; ++k) r.f[i][k] /= (float)n[i];
            r.has[i] = true;
        }
        return r;
    }

    // === РАСКРЫТАЯ ЛАДОНЬ ===
    // Запястье в (0, 0), самая дальняя точка на расстоянии 1; y растёт вниз, поэтому пальцы смотрят в минус.
    const Features& openHandPose() {
        static Features f = [] {
            const float p[21][2] = {
                { 0.00f,  0.00f },
                { -0.22f, -0.10f }, { -0.40f, -0.26f }, { -0.52f, -0.42f }, { -0.62f, -0.55f },     // большой
                { -0.20f, -0.52f }, { -0.25f, -0.72f }, { -0.28f, -0.86f }, { -0.30f, -0.98f },     // указательный
                { -0.02f, -0.55f }, { -0.03f, -0.78f }, { -0.03f, -0.93f }, { -0.03f, -1.04f },     // средний
                {  0.16f, -0.52f }, {  0.20f, -0.72f }, {  0.22f, -0.85f }, {  0.24f, -0.95f },     // безымянный
                {  0.32f, -0.45f }, {  0.40f, -0.60f }, {  0.45f, -0.70f }, {  0.49f, -0.78f } };   // мизинец
            Features r;
            r.fill(0.0f);
            for (int i = 0; i < 21; ++i) { r[i * 3] = p[i][0]; r[i * 3 + 1] = p[i][1]; }
            return r;
        }();
        return f;
    }

    // === РИСУНОК РУКИ ===
    namespace {
        struct Finger { int a, b, c, d; float k; float z; };    // четыре точки пальца, k - толщина относительно общей
    }

    void drawHandArt(cv::Mat& img, cv::Rect box, const Features& f, const HandStyle& st) {
        if (img.empty() || box.width < 8 || box.height < 8) return;

        auto X = [&](int i) { return f[i * 3]; };
        auto Y = [&](int i) { return f[i * 3 + 1]; };
        const float thick = 0.11f;                    // толщина пальца в долях размера кисти

        // направление от центра ладони к запястью: туда «смотрит» рукав
        float px = (X(0) + X(5) + X(9) + X(13) + X(17)) / 5.0f;
        float py = (Y(0) + Y(5) + Y(9) + Y(13) + Y(17)) / 5.0f;
        float dx = X(0) - px, dy = Y(0) - py;
        float dl = std::sqrt(dx * dx + dy * dy);
        if (dl < 1e-4f) { dx = 0; dy = 1; dl = 1; }
        dx /= dl; dy /= dl;
        const float sleeveLen = st.sleeveOn ? 0.55f : 0.0f;

        // границы рисунка (точки руки, рукав и запас на толщину пальцев)
        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
        for (int i = 0; i < 21; ++i) {
            minX = std::min(minX, X(i)); maxX = std::max(maxX, X(i));
            minY = std::min(minY, Y(i)); maxY = std::max(maxY, Y(i));
        }
        minX = std::min(minX, X(0) + dx * sleeveLen - 0.3f); maxX = std::max(maxX, X(0) + dx * sleeveLen + 0.3f);
        minY = std::min(minY, Y(0) + dy * sleeveLen);        maxY = std::max(maxY, Y(0) + dy * sleeveLen);
        const float pad = thick * 0.9f;
        float w = (maxX - minX) + 2 * pad, h = (maxY - minY) + 2 * pad;
        float s = std::min(box.width / w, box.height / h);                 // пикселей на единицу
        float ox = box.x + box.width / 2.0f - (minX + maxX) / 2.0f * s;
        float oy = box.y + box.height / 2.0f - (minY + maxY) / 2.0f * s;
        auto P = [&](float x, float y) { return cv::Point(cvRound(ox + x * s), cvRound(oy + y * s)); };

        const int tp = std::max(2, cvRound(thick * s));                    // толщина пальца в пикселях
        const int edge = std::max(2, cvRound(0.03f * s));                  // толщина контура

        // --- рукав ---
        if (st.sleeveOn) {
            float nx = -dy, ny = dx;                                       // перпендикуляр
            float x0 = X(0) + dx * 0.02f, y0 = Y(0) + dy * 0.02f;
            float x1 = X(0) + dx * sleeveLen, y1 = Y(0) + dy * sleeveLen;
            cv::Point q[4] = { P(x0 + nx * 0.24f, y0 + ny * 0.24f), P(x0 - nx * 0.24f, y0 - ny * 0.24f),
                               P(x1 - nx * 0.34f, y1 - ny * 0.34f), P(x1 + nx * 0.34f, y1 + ny * 0.34f) };
            cv::fillConvexPoly(img, q, 4, st.sleeve, cv::LINE_AA);
        }

        // --- ладонь ---
        const int palmIdx[6] = { 0, 1, 5, 9, 13, 17 };
        std::vector<cv::Point> palm;
        for (int i : palmIdx) palm.push_back(P(X(i), Y(i)));
        for (int pass = 0; pass < 2; ++pass) {
            const cv::Scalar& col = pass == 0 ? st.edge : st.skin;
            int th = pass == 0 ? tp * 6 / 10 + 2 * edge : tp * 6 / 10;
            cv::fillPoly(img, std::vector<std::vector<cv::Point>>{ palm }, col, cv::LINE_AA);
            cv::polylines(img, palm, true, col, th, cv::LINE_AA);
        }

        // --- пальцы: сначала дальние от камеры (больший z), чтобы ближние их перекрывали ---
        std::vector<Finger> fingers = {
            { 1, 2, 3, 4, 1.15f, 0 }, { 5, 6, 7, 8, 1.0f, 0 }, { 9, 10, 11, 12, 1.0f, 0 },
            { 13, 14, 15, 16, 0.95f, 0 }, { 17, 18, 19, 20, 0.85f, 0 } };
        for (Finger& fg : fingers) fg.z = (f[fg.a * 3 + 2] + f[fg.b * 3 + 2] + f[fg.c * 3 + 2] + f[fg.d * 3 + 2]) / 4.0f;
        std::sort(fingers.begin(), fingers.end(), [](const Finger& a, const Finger& b) { return a.z > b.z; });

        for (const Finger& fg : fingers) {
            const int idx[4] = { fg.a, fg.b, fg.c, fg.d };
            for (int pass = 0; pass < 2; ++pass) {
                const cv::Scalar& col = pass == 0 ? st.edge : st.skin;
                int th = cvRound(tp * fg.k) + (pass == 0 ? 2 * edge : 0);
                for (int j = 0; j < 3; ++j)
                    cv::line(img, P(X(idx[j]), Y(idx[j])), P(X(idx[j + 1]), Y(idx[j + 1])), col, th, cv::LINE_AA);
                for (int j = 0; j < 4; ++j)                                // круглые суставы и кончик
                    cv::circle(img, P(X(idx[j]), Y(idx[j])), th / 2, col, cv::FILLED, cv::LINE_AA);
            }
        }
    }

} // namespace HandTracker
