#include "TextBuffer.h"
#include "Alphabet.h"
#include <algorithm>

namespace HandTracker {

    void TextBuffer::append(wchar_t ch) {
        if (text_.size() >= maxLen_) text_.erase(0, text_.size() - maxLen_ + 1);
        text_ += ch;
        noHandFrames_ = 0;
        spaceDone_ = false;
    }

    wchar_t TextBuffer::update(const std::string& code, bool handPresent) {
        if (!handPresent) {
            cand_.clear(); candFrames_ = 0; lockedCode_.clear(); emptyFrames_ = 0;
            ++noHandFrames_;
            if (noHandFrames_ >= spaceFrames_ && !spaceDone_ && !text_.empty() && text_.back() != L' ') {
                text_ += L' ';
                spaceDone_ = true;
                return L' ';
            }
            return 0;
        }
        noHandFrames_ = 0;
        spaceDone_ = false;

        if (code.empty()) {                 // рука есть, жест не распознан
            cand_.clear(); candFrames_ = 0;
            // повтор той же буквы разрешаем, только если жеста не было заметное время
            if (++emptyFrames_ >= stableFrames_) lockedCode_.clear();
            return 0;
        }
        emptyFrames_ = 0;
        if (code != cand_) { cand_ = code; candFrames_ = 0; }
        if (code == lockedCode_) return 0;  // эта буква уже введена, ждём смены жеста
        if (++candFrames_ >= stableFrames_) {
            wchar_t ch = letterFromCode(code);
            if (ch == 0) return 0;
            lockedCode_ = code;
            append(ch);
            return ch;
        }
        return 0;
    }

    void TextBuffer::backspace() { if (!text_.empty()) text_.pop_back(); }
    void TextBuffer::clear() {
        text_.clear(); cand_.clear(); candFrames_ = 0; lockedCode_.clear(); emptyFrames_ = 0;
    }

    float TextBuffer::progress() const {
        if (cand_.empty()) return 0;
        if (cand_ == lockedCode_) return 1;
        return std::min(1.0f, (float)candFrames_ / (float)stableFrames_);
    }

} // namespace HandTracker
