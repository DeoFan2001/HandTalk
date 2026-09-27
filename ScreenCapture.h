#pragma once
#include <opencv2/opencv.hpp>
#include <windows.h>

namespace HandTracker {

    class ScreenCapture {
        int x, y, w, h;
        HDC hScreenDC, hMemDC;
        HBITMAP hBitmap;
        BITMAPINFOHEADER bi;
        cv::Mat bufferBGRA, bufferBGR;

    public:
        ScreenCapture(int x_, int y_, int w_, int h_);
        ~ScreenCapture();
        cv::Mat& grab();
    };

} // namespace HandTracker