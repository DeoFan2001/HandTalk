#pragma once
// TextBuffer.h - накопление распознанных букв в текст
// Буква добавляется, когда её показывают стабильно несколько кадров подряд.
// Пока та же поза держится, буква повторно не добавляется. Чтобы написать "ТТ", надо убрать руку или сменить жест.
// Короткие сбои распознавания (один-два кадра) повтор не включают.
// Долгое отсутствие руки добавляет пробел.
#include <string>

namespace HandTracker {

    class TextBuffer {
        std::wstring text_;
        std::string cand_;         // кандидат на ввод (код буквы)
        int candFrames_ = 0;
        std::string lockedCode_;   // последняя введённая буква: пока её показывают, повторно не вводим
        int emptyFrames_ = 0;      // сколько кадров подряд жест не распознан
        int noHandFrames_ = 0;
        bool spaceDone_ = false;
        int stableFrames_;
        int spaceFrames_;
        std::size_t maxLen_;
    public:
        TextBuffer(int stableFrames = 8, int spaceFrames = 45, std::size_t maxLen = 500)
            : stableFrames_(stableFrames), spaceFrames_(spaceFrames), maxLen_(maxLen) {}

        // Вызывать на каждом кадре. code - код буквы или "" (жеста нет / не уверен).
        // handPresent - есть ли рука в кадре. Возвращает добавленный символ или 0.
        wchar_t update(const std::string& code, bool handPresent);

        // Добавить букву напрямую (для жестов с движением)
        void append(wchar_t ch);

        void backspace();
        void clear();
        const std::wstring& text() const { return text_; }
        // Прогресс подтверждения кандидата 0..1 (для индикатора на экране)
        float progress() const;
        const std::string& candidate() const { return cand_; }
    };

} // namespace HandTracker
