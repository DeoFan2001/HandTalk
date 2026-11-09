#pragma once
// Gesture.h - распознавание жеста (буквы) по 21 точке руки
// Модуль не зависит от OpenCV: точки передаются простым массивом, поэтому его можно тестировать отдельно.
#include <array>
#include <string>
#include <vector>

namespace HandTracker {

    struct P3 { float x = 0, y = 0, z = 0; };
    using HandPoints = std::array<P3, 21>;

    constexpr int FEATURE_SIZE = 63;             // 21 точка * (x, y, z)
    using Features = std::array<float, FEATURE_SIZE>;

    // === ПРИЗНАКИ РУКИ ===
    // Запястье переносится в начало координат, всё делится на размер кисти,
    // левая рука зеркалится: так признаки не зависят от положения, расстояния до камеры и руки.
    // Возвращает false, если рука вырожденная (все точки в одном месте).
    bool normalizeHand(const HandPoints& pts, bool isLeft, Features& out);

    // === ОДИН ОБУЧАЮЩИЙ ПРИМЕР ===
    struct Sample {
        std::string code;     // код буквы (см. Alphabet.h)
        Features f;
    };

    // === РЕЗУЛЬТАТ РАСПОЗНАВАНИЯ ===
    struct GestureResult {
        bool valid = false;       // false - уверенного ответа нет
        std::string code;         // код буквы
        float confidence = 0;     // уверенность 0..1
    };

    // === ОБЩИЙ ИНТЕРФЕЙС КЛАССИФИКАТОРА ===
    // Сейчас работает kNN по образцам; MLP (нейросеть) будет вторым классификатором с тем же интерфейсом.
    class IClassifier {
    public:
        virtual ~IClassifier() = default;
        virtual bool ready() const = 0;
        virtual GestureResult classify(const Features& f) const = 0;
        virtual const char* name() const = 0;
    };

    // === kNN ПО ОБРАЗЦАМ ===
    class KnnClassifier : public IClassifier {
        std::vector<Sample> samples_;
        int k_;
        float maxDistance_;
    public:
        // maxDistance - если ближайший пример дальше, ответ считается "не знаю"
        KnnClassifier(int k = 5, float maxDistance = 0.6f) : k_(k), maxDistance_(maxDistance) {}
        void setSamples(std::vector<Sample> s) { samples_ = std::move(s); }
        std::size_t size() const { return samples_.size(); }
        bool ready() const override { return !samples_.empty(); }
        GestureResult classify(const Features& f) const override;
        const char* name() const override { return "kNN"; }
    };

    // === ФАЙЛ ОБРАЗЦОВ (CSV: код;x0;y0;z0;...;z20) ===
    // Загрузка: плохие строки пропускаются, их число возвращается в skipped
    std::vector<Sample> loadSamples(const std::string& path, int* skipped = nullptr);
    bool appendSample(const std::string& path, const Sample& s);

} // namespace HandTracker
