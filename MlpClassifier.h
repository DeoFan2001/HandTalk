#pragma once
// MlpClassifier.h - ЗАГЛУШКА классификатора на нейросети (MLP, ONNX)
// Задача участника 4: обучить MLP (PyTorch), экспортировать в models/gesture_mlp.onnx
// (вход 1x63 - те же признаки, что даёт normalizeHand; выход 1x33 - по числу букв Alphabet.h)
// и реализовать classify() через ONNX Runtime. Пока модели нет, ready() = false,
// и программа работает на kNN.
#include "Gesture.h"
#include <string>

namespace HandTracker {

    class MlpClassifier : public IClassifier {
        bool loaded_ = false;
    public:
        bool load(const std::string& onnxPath);                 // TODO: создать сессию ONNX Runtime
        bool ready() const override { return loaded_; }
        GestureResult classify(const Features& f) const override;   // TODO: прогнать модель, взять argmax
        const char* name() const override { return "MLP"; }
    };

} // namespace HandTracker
