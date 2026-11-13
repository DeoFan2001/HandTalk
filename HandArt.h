#pragma once
// HandArt.h - рисунок руки по 21 точке
// Нужен для трёх вещей: эталон буквы на экране «Изучить алфавит», побуквенная анимация сообщения
// и логотип/значки. Картинок в программе нет: руку рисуем линиями и фигурами по точкам.
// Модуль зависит только от OpenCV (окна и Windows ему не нужны).
#include <opencv2/opencv.hpp>
#include <vector>
#include "Alphabet.h"
#include "Gesture.h"

namespace HandTracker {

    // Цвета рисунка (BGR)
    struct HandStyle {
        cv::Scalar skin;       // заливка руки
        cv::Scalar edge;       // контур (он же делает перекрытие пальцев видимым)
        cv::Scalar sleeve;     // рукав
        bool sleeveOn = true;
    };

    // === ЭТАЛОНЫ БУКВ ===
    // Для каждой буквы берём среднюю позу по её образцам из gestures.csv.
    // has[i] == false - образцов позы у буквы нет (например, буква только с движением).
    struct HandRefs {
        bool has[ALPHABET_SIZE] = {};
        Features f[ALPHABET_SIZE];
    };

    HandRefs buildHandRefs(const std::vector<Sample>& samples);

    // Раскрытая ладонь (для логотипа и значков)
    const Features& openHandPose();

    // Нарисовать руку так, чтобы она поместилась в прямоугольник box
    void drawHandArt(cv::Mat& img, cv::Rect box, const Features& f, const HandStyle& st);

} // namespace HandTracker
