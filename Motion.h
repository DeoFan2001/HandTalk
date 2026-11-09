#pragma once
// Motion.h - жесты с движением (динамические буквы)
// Идея простая: пока рука почти неподвижна, работает обычное распознавание позы.
// Когда рука заметно движется, кадры движения собираются в отрезок, отрезок приводится к 16 шагам по времени
// (быстрое и медленное движение становятся похожи) и сравнивается с образцами движений методом kNN.
// Модуль не зависит от OpenCV и Windows.
#include "Gesture.h"
#include <deque>
#include <string>
#include <vector>

namespace HandTracker {

    constexpr int SEQ_LEN = 16;                               // шагов по времени
    constexpr int SEQ_FRAME_DIM = FEATURE_SIZE + 2;           // 63 числа формы руки + сдвиг запястья (x, y)
    constexpr int SEQ_SIZE = SEQ_LEN * SEQ_FRAME_DIM;         // 1040 чисел на одно движение
    constexpr float TRAJ_WEIGHT = 4.0f;                       // вес сдвига запястья среди признаков (их всего 2 из 65 чисел кадра)

    using SeqFeatures = std::vector<float>;

    struct SeqSample {
        std::string code;      // код буквы
        SeqFeatures f;
    };

    struct MotionParams {
        float moveOn = 0.025f;     // скорость (доли размера руки за кадр) выше этого - движение началось
        float moveOff = 0.012f;    // ниже этого - рука почти стоит
        int startFrames = 2;       // кадров подряд быстрее moveOn
        int stopFrames = 4;        // кадров подряд медленнее moveOff
        int minLen = 6;            // отрезок короче - не жест
        int maxLen = 90;           // отрезок длиннее - обрываем
    };

    struct SegFrame {
        HandPoints pts;
        bool isLeft = false;
    };

    // Размер руки (самая дальняя точка от запястья) и скорость между кадрами
    float handSizeOf(const HandPoints& p);
    float handMotion(const HandPoints& prev, const HandPoints& cur);

    // Отрезок кадров -> SEQ_SIZE чисел
    SeqFeatures makeSequence(const std::vector<SegFrame>& frames);

    // === РАЗБИЕНИЕ ПОТОКА НА ДВИЖЕНИЯ ===
    class MotionSegmenter {
        MotionParams p_;
        bool hasPrev_ = false;
        HandPoints prev_;
        bool moving_ = false;
        int fast_ = 0, still_ = 0;
        std::deque<SegFrame> pre_;       // последние кадры до начала движения
        std::vector<SegFrame> seg_;
        bool finish(SeqFeatures& out, int trailing);
    public:
        explicit MotionSegmenter(MotionParams p = MotionParams()) : p_(p) {}
        void setParams(const MotionParams& p) { p_ = p; }
        // Подавать на каждом кадре (pts == nullptr, если руки нет).
        // Возвращает true, когда движение только что закончилось; признаки - в out.
        bool update(const HandPoints* pts, bool isLeft, SeqFeatures& out);
        bool moving() const { return moving_; }
        void reset();
    };

    // === kNN ПО ДВИЖЕНИЯМ ===
    class SeqKnn {
        std::vector<SeqSample> samples_;
        int k_;
        float maxDistance_;
    public:
        SeqKnn(int k = 5, float maxDistance = 0.5f) : k_(k), maxDistance_(maxDistance) {}
        void setSamples(std::vector<SeqSample> s) { samples_ = std::move(s); }
        std::size_t size() const { return samples_.size(); }
        bool ready() const { return !samples_.empty(); }
        GestureResult classify(const SeqFeatures& f) const;
    };

    // === ФАЙЛ ОБРАЗЦОВ ДВИЖЕНИЙ (CSV: код;1040 чисел) ===
    std::vector<SeqSample> loadSeqSamples(const std::string& path, int* skipped = nullptr);
    bool appendSeqSample(const std::string& path, const SeqSample& s);

} // namespace HandTracker
