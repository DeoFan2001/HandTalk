#pragma once
// Settings.h - настройки программы (файл handtalk.ini, строки вида ключ=значение)
// Если файла нет или значение испорчено, берётся значение по умолчанию: программа запускается всегда.
#include <string>

namespace HandTracker {

    // === НОМЕРА НАСТРОЕК (для окон «Настройки» и «Для разработчиков») ===
    // 0..SET_USER_END-1  - для пользователей (экран «Настройки»)
    // SET_USER_END..SET_DEV_END-1 - для разработчиков (кнопки «-» и «+»)
    // дальше - файлы: в окне только показываются, меняются вручную в handtalk.ini
    enum SettingId {
        SET_MAXHANDS = 0, SET_STABLE, SET_SPACE, SET_SPEAK, SET_RATE, SET_VOLUME,        // пользователи
        SET_MINCONF, SET_DYNCONF, SET_KNNK, SET_KNNDIST, SET_DYNDIST, SET_MOVEON, SET_MOVEOFF, SET_FPS,   // разработчики
        SET_F_SAMPLES, SET_F_DYN, SET_F_MLP, SET_F_VIDEO,                                            // файлы
        SET_COUNT
    };
    constexpr int SET_USER_END = SET_MINCONF;      // 6 настроек для пользователей
    constexpr int SET_DEV_END = SET_F_SAMPLES;     // ещё 8 для разработчиков
    constexpr const wchar_t* SET_ON_TEXT = L"Включено";
    constexpr const wchar_t* SET_OFF_TEXT = L"Выключено";

    struct Settings {
        int cameraIndex = 0;            // номер камеры (программа запоминает последнюю выбранную в списке камер)
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
        // Приведение значений к допустимым пределам
        void clamp();

        // --- для окна настроек ---
        static std::wstring title(int id);             // название настройки
        static std::wstring hint(int id);              // короткое пояснение
        static bool isToggle(int id) { return id == SET_SPEAK; }   // «вкл/выкл» вместо «-» и «+»
        std::wstring valueText(int id) const;          // значение для показа в окне
        void change(int id, int dir);                  // dir = -1 («-») или +1 («+»); значение остаётся в пределах
        void reset(bool dev);                          // вернуть значения по умолчанию: dev - для разработчиков, иначе для пользователей
        bool save(const std::string& path) const;      // записать файл handtalk.ini; false - не удалось записать
    };

} // namespace HandTracker
