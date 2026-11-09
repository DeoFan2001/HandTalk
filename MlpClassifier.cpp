#include "MlpClassifier.h"
#include <fstream>

namespace HandTracker {

    bool MlpClassifier::load(const std::string& onnxPath) {
        // TODO(участник 4): здесь создаётся Ort::Session. Пока только проверяем, что файл есть,
        // но загруженной модель не считаем - заглушка не умеет её исполнять.
        std::ifstream f(onnxPath, std::ios::binary);
        loaded_ = false;
        return f.good() && loaded_;
    }

    GestureResult MlpClassifier::classify(const Features&) const {
        return GestureResult{};   // заглушка: "не знаю"
    }

} // namespace HandTracker
