#pragma once
// TextRender.h - вывод русского текста на кадр OpenCV (cv::putText кириллицу не умеет)
// Использует шрифт Windows через GDI.
#include <opencv2/opencv.hpp>
#include <string>

namespace HandTracker {

    // Нарисовать строку; org - левый верхний угол текста, heightPx - высота шрифта в пикселях
    void putTextW(cv::Mat& img, const std::wstring& text, cv::Point org, int heightPx,
                  cv::Scalar color, bool bold = false);

    // Ширина строки в пикселях (для выравнивания и переноса)
    int textWidthW(const std::wstring& text, int heightPx, bool bold = false);

    // Перенос по ширине maxWidth: возвращает строки; берутся последние maxLines строк
    std::vector<std::wstring> wrapTextW(const std::wstring& text, int heightPx, int maxWidth, int maxLines);

} // namespace HandTracker
