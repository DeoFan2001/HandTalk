#pragma once
// Settings.h - настройки программы (файл handtalk.ini, строки вида ключ=значение)
// Если файла нет или значение испорчено, берётся значение по умолчанию: программа запускается всегда.
#include <string>

namespace HandTracker {

    struct Settings {
        int cameraIndex = 0;            // номер камеры
        std::string videoFile;          // путь к видеофайлу (если не пусто - источник "файл")
        int maxHands = 2;               // 1 или 2
        int stableFrames = 8;           // сколько кадров держать жест до ввода буквы
        int spaceFrames = 45;           // сколько кадров без руки до пробела
        int knnK = 5;
        float knnMaxDistance = 0.6f;
        float minConfidence = 0.6f;     // ниже - буква не вводится
        double targetFps = 20.0;
        bool speakEnabled = true;
        int speechRate = 0;             // -10..10 (SAPI)
        int speechVolume = 100;         // 0..100
        std::string samplesFile = "gestures.csv";              // образцы поз
        std::string dynSamplesFile = "gestures_dynamic.csv";   // образцы движений
        std::string mlpModel = "models/gesture_mlp.onnx";

        // Жесты с движением
        float dynMaxDistance = 0.5f;    // дальше этого образцы движения считаются "не знаю"
        float dynMinConfidence = 0.6f;
        float moveOn = 0.025f;          // скорость руки (доли размера руки за кадр): выше - движение началось
        float moveOff = 0.012f;         // ниже - рука стоит

        // Загрузка: возвращает число строк с ошибками (0 - всё хорошо). Нет файла - не ошибка.
        int load(const std::string& path);
        bool save(const std::string& path) const;
        // Приведение значений к допустимым пределам
        void clamp();
    };

} // namespace HandTracker
