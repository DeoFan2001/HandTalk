#pragma once
// Speech.h - озвучивание текста (Windows SAPI 5, работает без интернета)
// Если в Windows нет русского голоса, озвучивание отключается, остальная программа работает.
#include <string>

struct ISpVoice;

namespace HandTracker {

    class Speech {
        ISpVoice* voice_ = nullptr;
        bool comInit_ = false;
        bool russian_ = false;
        bool paused_ = false;
    public:
        ~Speech();
        bool init(int rate = 0, int volume = 100);   // false - озвучивание недоступно
        bool available() const { return voice_ != nullptr; }
        bool hasRussianVoice() const { return russian_; }
        bool speak(const std::wstring& text);        // прерывает предыдущую фразу, не блокирует программу; false - не удалось
        void pauseResume();                          // пауза / продолжение
        void stop();
        bool paused() const { return paused_; }
    };

} // namespace HandTracker
