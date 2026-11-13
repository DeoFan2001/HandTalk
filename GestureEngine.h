#pragma once
// GestureEngine.h - объединяет распознавание позы и движения и набор текста
//
// Правило: рука почти стоит -> смотрим на позу (обычные буквы).
//          рука движется   -> собираем движение; когда оно закончилось, ищем среди букв с движением.
// После буквы с движением обычные буквы блокируются, пока рука не уйдёт из кадра или не начнётся новое движение:
// иначе конечная поза движения (например, у Й она похожа на И) сразу дала бы лишнюю букву.
// Модуль не зависит от OpenCV и Windows.
#include "Gesture.h"
#include "Motion.h"
#include "TextBuffer.h"
#include <string>

namespace HandTracker {

    struct EngineParams {
        float minConfidence = 0.6f;        // порог уверенности для позы
        float dynMinConfidence = 0.6f;     // порог уверенности для движения
        int stableFrames = 8;              // кадров держать позу до ввода
        int spaceFrames = 45;              // кадров без руки до пробела
        int unlockFrames = 9;              // кадров без руки, чтобы разрешить обычные буквы после движения
        MotionParams motion;
    };

    // Что произошло на кадре (для экрана и записи образцов)
    struct FrameOut {
        bool moving = false;               // рука сейчас движется
        std::string code;                  // поза: код буквы или "" (нет уверенного ответа)
        bool hasFeatures = false;          // рука стоит - признаки позы готовы (для записи образцов)
        Features feat;
        bool hasSeq = false;               // движение только что закончилось - признаки готовы
        SeqFeatures seq;
        std::string dynCode;               // буква, найденная по движению, или ""
        float dynConf = 0;
    };

    class GestureEngine {
        EngineParams p_;
        const IClassifier* stat_ = nullptr;
        const SeqKnn* dyn_ = nullptr;
        MotionSegmenter seg_;
        TextBuffer text_;
        bool dynLock_ = false;
        int noHand_ = 0;
        std::string lastCode_;
        bool lastHand_ = false;
    public:
        explicit GestureEngine(const EngineParams& p = EngineParams())
            : p_(p), seg_(p.motion), text_(p.stableFrames, p.spaceFrames) {}

        void setStatic(const IClassifier* c) { stat_ = c; }
        void setDynamic(const SeqKnn* d) { dyn_ = d; }
        TextBuffer& text() { return text_; }
        const TextBuffer& text() const { return text_; }

        // pts - точки руки (nullptr, если руки нет).
        // commit == false - режим записи образцов: текст не меняется.
        // fresh == false - кадр повторяет прошлые точки (режим экономии): только считаем время для текста.
        FrameOut update(const HandPoints* pts, bool isLeft, bool commit = true, bool fresh = true);

        // Сбросить состояние (при остановке и новом запуске)
        void reset();
    };

} // namespace HandTracker
