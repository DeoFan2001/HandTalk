#pragma once
// Trainer.h - "обучение модели" и проверка точности
// Модель kNN - это сами образцы, поэтому обучение состоит из трёх шагов:
//   1) читаем файлы образцов (позы и движения);
//   2) проверяем точность: для каждой буквы последние 20% образцов файла берутся на проверку,
//      остальные работают как обучающие (последние, а не случайные: соседние кадры почти одинаковы,
//      случайная выборка дала бы завышенную точность);
//   3) возвращаем отчёт, а вызывающий код подставляет образцы в классификаторы.
// Позже сюда же встанет обучение MLP (тот же вход и выход).
#include "Gesture.h"
#include "Motion.h"
#include <string>
#include <vector>

namespace HandTracker {

    struct TrainParams {
        int k = 5;
        float maxDistance = 0.6f;
        float dynMaxDistance = 0.5f;
        float minConfidence = 0.6f;
        float dynMinConfidence = 0.6f;
        float testPart = 0.2f;     // доля проверочных образцов
        int minForTest = 5;        // у буквы меньше образцов - проверка для неё пропускается
    };

    struct PartStats {
        int samples = 0;           // образцов
        int letters = 0;           // букв
        int tested = 0;            // проверено образцов
        float accuracy = -1;       // точность 0..1 (-1 - проверить нельзя)
    };

    struct TrainReport {
        PartStats stat, dyn;
        std::wstring full;         // подробный отчёт (для файла)
        std::wstring brief;        // одна строка для экрана
    };

    TrainReport trainAndEvaluate(const std::vector<Sample>& stat, const std::vector<SeqSample>& dyn,
                                 const TrainParams& p);

} // namespace HandTracker
