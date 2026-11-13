#include "Settings.h"
#include "Text.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <locale>
#include <sstream>

namespace HandTracker {

    static std::string trim(const std::string& s) {
        std::size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return "";
        std::size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    static bool toInt(const std::string& v, int& out) {
        char* end = nullptr;
        long x = std::strtol(v.c_str(), &end, 10);
        if (end == v.c_str() || *end != 0) return false;
        out = (int)x; return true;
    }
    static bool toDouble(const std::string& v, double& out) {
        char* end = nullptr;
        double x = std::strtod(v.c_str(), &end);
        if (end == v.c_str() || *end != 0) return false;
        out = x; return true;
    }

    int Settings::load(const std::string& path) {
        std::ifstream in(path);
        if (!in) return 0;
        std::string line;
        int errors = 0;
        bool first = true;
        while (std::getline(in, line)) {
            if (first && line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
            first = false;
            line = trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            std::size_t eq = line.find('=');
            if (eq == std::string::npos) { ++errors; continue; }
            std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
            int i; double d; bool ok = true;
            if (k == "cameraIndex") ok = toInt(v, cameraIndex);
            else if (k == "videoFile") videoFile = v;
            else if (k == "maxHands") ok = toInt(v, maxHands);
            else if (k == "stableFrames") ok = toInt(v, stableFrames);
            else if (k == "spaceFrames") ok = toInt(v, spaceFrames);
            else if (k == "knnK") ok = toInt(v, knnK);
            else if (k == "knnMaxDistance") { ok = toDouble(v, d); if (ok) knnMaxDistance = (float)d; }
            else if (k == "minConfidence") { ok = toDouble(v, d); if (ok) minConfidence = (float)d; }
            else if (k == "targetFps") ok = toDouble(v, targetFps);
            else if (k == "speakEnabled") { ok = toInt(v, i); if (ok) speakEnabled = (i != 0); }
            else if (k == "speechRate") ok = toInt(v, speechRate);
            else if (k == "speechVolume") ok = toInt(v, speechVolume);
            else if (k == "samplesFile") samplesFile = v;
            else if (k == "dynSamplesFile") dynSamplesFile = v;
            else if (k == "mlpModel") mlpModel = v;
            else if (k == "dynMaxDistance") { ok = toDouble(v, d); if (ok) dynMaxDistance = (float)d; }
            else if (k == "dynMinConfidence") { ok = toDouble(v, d); if (ok) dynMinConfidence = (float)d; }
            else if (k == "moveOn") { ok = toDouble(v, d); if (ok) moveOn = (float)d; }
            else if (k == "moveOff") { ok = toDouble(v, d); if (ok) moveOff = (float)d; }
            else ok = false;                    // неизвестный ключ
            if (!ok) ++errors;
        }
        clamp();
        return errors;
    }

    void Settings::clamp() {
        cameraIndex = std::max(0, cameraIndex);
        maxHands = std::min(2, std::max(1, maxHands));
        stableFrames = std::min(60, std::max(2, stableFrames));
        spaceFrames = std::min(300, std::max(10, spaceFrames));
        knnK = std::min(15, std::max(1, knnK));
        knnMaxDistance = std::min(5.0f, std::max(0.05f, knnMaxDistance));
        minConfidence = std::min(1.0f, std::max(0.0f, minConfidence));
        targetFps = std::min(60.0, std::max(5.0, targetFps));
        speechRate = std::min(10, std::max(-10, speechRate));
        speechVolume = std::min(100, std::max(0, speechVolume));
        dynMaxDistance = std::min(5.0f, std::max(0.05f, dynMaxDistance));
        dynMinConfidence = std::min(1.0f, std::max(0.0f, dynMinConfidence));
        moveOn = std::min(0.5f, std::max(0.005f, moveOn));
        moveOff = std::min(moveOn, std::min(0.5f, std::max(0.001f, moveOff)));   // "стоит" всегда медленнее "движется"
    }

    // === ТЕКСТЫ ДЛЯ ОКНА НАСТРОЕК ===
    std::wstring Settings::title(int id) {
        switch (id) {
        case SET_MAXHANDS: return L"Количество рук";
        case SET_STABLE: return L"Удержание жеста";
        case SET_SPACE: return L"Пауза до пробела";
        case SET_SPEAK: return L"Озвучивание";
        case SET_RATE: return L"Скорость речи";
        case SET_VOLUME: return L"Громкость речи";
        case SET_MINCONF: return L"Уверенность: поза";
        case SET_DYNCONF: return L"Уверенность: движение";
        case SET_KNNK: return L"kNN: соседи (k)";
        case SET_KNNDIST: return L"kNN: расстояние, поза";
        case SET_DYNDIST: return L"kNN: расстояние, движение";
        case SET_MOVEON: return L"Начало движения";
        case SET_MOVEOFF: return L"Остановка руки";
        case SET_FPS: return L"Целевой FPS";
        case SET_F_SAMPLES: return L"Образцы поз";
        case SET_F_DYN: return L"Образцы движений";
        case SET_F_MLP: return L"Модель MLP";
        case SET_F_VIDEO: return L"Видеофайл";
        }
        return L"";
    }

    std::wstring Settings::hint(int id) {
        switch (id) {
        case SET_MAXHANDS: return L"Сколько рук отслеживать: одну или две";
        case SET_STABLE: return L"Сколько кадров держать жест до ввода буквы. Больше - меньше случайных букв";
        case SET_SPACE: return L"Сколько кадров руки нет в кадре до ввода пробела";
        case SET_SPEAK: return L"Озвучивать написанный ответ вслух";
        case SET_RATE: return L"От -10 (медленно) до 10 (быстро)";
        case SET_VOLUME: return L"От 0 до 100 процентов";
        case SET_MINCONF: return L"Если уверенность ниже, буква по позе не вводится";
        case SET_DYNCONF: return L"То же для жестов с движением";
        case SET_KNNK: return L"Сколько ближайших образцов голосуют за букву";
        case SET_KNNDIST: return L"Дальше этого расстояния до образца ответ «не знаю»";
        case SET_DYNDIST: return L"То же для образцов движений";
        case SET_MOVEON: return L"Скорость руки, выше которой считаем, что движение началось";
        case SET_MOVEOFF: return L"Скорость руки, ниже которой считаем, что рука стоит";
        case SET_FPS: return L"Желаемая частота обработки кадров";
        }
        return L"";
    }

    // Слово «кадр» с правильным окончанием: 1 кадр, 2 кадра, 5 кадров
    static std::wstring framesText(int n) {
        int a = n % 10, b = n % 100;
        const wchar_t* w = L"кадров";
        if (a == 1 && b != 11) w = L"кадр";
        else if (a >= 2 && a <= 4 && (b < 12 || b > 14)) w = L"кадра";
        return std::to_wstring(n) + L" " + w;
    }

    // Число с заданным количеством знаков после точки
    static std::wstring numText(double v, int digits) {
        std::ostringstream os;
        os.imbue(std::locale::classic());
        os.setf(std::ios::fixed);
        os.precision(digits);
        os << v;
        return fromUtf8(os.str());
    }

    std::wstring Settings::valueText(int id) const {
        switch (id) {
        case SET_MAXHANDS: return std::to_wstring(maxHands);
        case SET_STABLE: return framesText(stableFrames);
        case SET_SPACE: return framesText(spaceFrames);
        case SET_SPEAK: return speakEnabled ? SET_ON_TEXT : SET_OFF_TEXT;
        case SET_RATE: return std::to_wstring(speechRate);
        case SET_VOLUME: return std::to_wstring(speechVolume) + L"%";
        case SET_MINCONF: return numText(minConfidence, 2);
        case SET_DYNCONF: return numText(dynMinConfidence, 2);
        case SET_KNNK: return std::to_wstring(knnK);
        case SET_KNNDIST: return numText(knnMaxDistance, 2);
        case SET_DYNDIST: return numText(dynMaxDistance, 2);
        case SET_MOVEON: return numText(moveOn, 3);
        case SET_MOVEOFF: return numText(moveOff, 3);
        case SET_FPS: return numText(targetFps, 0);
        case SET_F_SAMPLES: return fromUtf8(samplesFile);
        case SET_F_DYN: return fromUtf8(dynSamplesFile);
        case SET_F_MLP: return fromUtf8(mlpModel);
        case SET_F_VIDEO: return videoFile.empty() ? std::wstring(L"(не задан)") : fromUtf8(videoFile);
        }
        return L"";
    }

    // === ИЗМЕНЕНИЕ ЗНАЧЕНИЯ КНОПКАМИ «-» И «+» ===
    void Settings::change(int id, int dir) {
        dir = dir < 0 ? -1 : 1;
        auto stepInt = [&](int& v, int step, int lo, int hi) {
            v = std::min(hi, std::max(lo, v + dir * step));
        };
        // дробные значения привязываем к сетке шага, чтобы не копились ошибки вида 0.6000001
        auto stepFloat = [&](float& v, float step, float lo, float hi) {
            float x = std::round((v + dir * step) / step) * step;
            v = std::min(hi, std::max(lo, x));
        };
        switch (id) {
        case SET_MAXHANDS: stepInt(maxHands, 1, 1, 2); break;
        case SET_STABLE: stepInt(stableFrames, 1, 2, 60); break;
        case SET_SPACE: stepInt(spaceFrames, 5, 10, 300); break;
        case SET_SPEAK: speakEnabled = !speakEnabled; break;
        case SET_RATE: stepInt(speechRate, 1, -10, 10); break;
        case SET_VOLUME: stepInt(speechVolume, 10, 0, 100); break;
        case SET_MINCONF: stepFloat(minConfidence, 0.05f, 0.0f, 1.0f); break;
        case SET_DYNCONF: stepFloat(dynMinConfidence, 0.05f, 0.0f, 1.0f); break;
        case SET_KNNK: stepInt(knnK, 1, 1, 15); break;
        case SET_KNNDIST: stepFloat(knnMaxDistance, 0.05f, 0.05f, 5.0f); break;
        case SET_DYNDIST: stepFloat(dynMaxDistance, 0.05f, 0.05f, 5.0f); break;
        case SET_MOVEON: stepFloat(moveOn, 0.005f, 0.005f, 0.5f); break;
        case SET_MOVEOFF: stepFloat(moveOff, 0.001f, 0.001f, 0.5f); break;
        case SET_FPS: targetFps = std::min(60.0, std::max(5.0, targetFps + dir * 5.0)); break;
        default: return;
        }
        clamp();
    }

    // === ЗНАЧЕНИЯ ПО УМОЛЧАНИЮ ===
    void Settings::reset(bool dev) {
        const Settings d;
        if (!dev) {
            maxHands = d.maxHands;
            stableFrames = d.stableFrames; spaceFrames = d.spaceFrames;
            speakEnabled = d.speakEnabled; speechRate = d.speechRate; speechVolume = d.speechVolume;
        }
        else {
            minConfidence = d.minConfidence; dynMinConfidence = d.dynMinConfidence;
            knnK = d.knnK; knnMaxDistance = d.knnMaxDistance; dynMaxDistance = d.dynMaxDistance;
            moveOn = d.moveOn; moveOff = d.moveOff; targetFps = d.targetFps;
        }
    }

    // === ЗАПИСЬ ФАЙЛА handtalk.ini ===
    // Русский текст пишем через UTF-8 (так же, как загрузка читает файл)
    bool Settings::save(const std::string& path) const {
        std::ostringstream os;
        os.imbue(std::locale::classic());
        auto comment = [&](const std::wstring& t) { os << "# " << toUtf8(t) << "\n"; };
        auto line = [&](const char* key, const std::string& value) { os << key << "=" << value << "\n"; };
        auto num = [&](auto v) { std::ostringstream t; t.imbue(std::locale::classic()); t << v; return t.str(); };

        comment(L"handtalk.ini - настройки HandTalk (строки вида ключ=значение).");
        comment(L"Файл записывает сама программа при изменении настроек; строки с # - комментарии.");
        os << "\n";
        comment(L"--- Для пользователей ---");
        comment(L"Камера: программа запоминает последнюю выбранную в списке камер (с 0)"); line("cameraIndex", num(cameraIndex));
        comment(L"Видеофайл вместо камеры (пусто - не используется)");      line("videoFile", videoFile);
        comment(L"Сколько рук отслеживать (1 или 2)");                      line("maxHands", num(maxHands));
        comment(L"Сколько кадров держать жест до ввода буквы");             line("stableFrames", num(stableFrames));
        comment(L"Сколько кадров без руки до пробела");                     line("spaceFrames", num(spaceFrames));
        comment(L"Озвучивание: 1 - включено, 0 - выключено");               line("speakEnabled", speakEnabled ? "1" : "0");
        comment(L"Скорость речи (-10..10)");                                line("speechRate", num(speechRate));
        comment(L"Громкость речи (0..100)");                                line("speechVolume", num(speechVolume));
        os << "\n";
        comment(L"--- Для разработчиков ---");
        comment(L"Порог уверенности для позы (0..1)");                      line("minConfidence", num(minConfidence));
        comment(L"Порог уверенности для движения (0..1)");                  line("dynMinConfidence", num(dynMinConfidence));
        comment(L"kNN: число соседей (1..15)");                             line("knnK", num(knnK));
        comment(L"kNN: максимальное расстояние для позы");                  line("knnMaxDistance", num(knnMaxDistance));
        comment(L"kNN: максимальное расстояние для движения");              line("dynMaxDistance", num(dynMaxDistance));
        comment(L"Скорость руки (доли размера руки за кадр): выше - движение началось");  line("moveOn", num(moveOn));
        comment(L"Скорость руки: ниже - рука стоит");                       line("moveOff", num(moveOff));
        comment(L"Целевая частота кадров (5..60)");                         line("targetFps", num(targetFps));
        comment(L"Файлы образцов и модель");
        line("samplesFile", samplesFile);
        line("dynSamplesFile", dynSamplesFile);
        line("mlpModel", mlpModel);

        std::ofstream out(path, std::ios::trunc);
        if (!out) return false;
        out << os.str();
        return (bool)out;
    }

} // namespace HandTracker
