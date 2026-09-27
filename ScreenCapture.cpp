#include "ScreenCapture.h"

namespace HandTracker {

    ScreenCapture::ScreenCapture(int x_, int y_, int w_, int h_)
        : x(x_), y(y_), w(w_), h(h_) {
        hScreenDC = GetDC(nullptr);
        hMemDC = CreateCompatibleDC(hScreenDC);
        hBitmap = CreateCompatibleBitmap(hScreenDC, w, h);
        SelectObject(hMemDC, hBitmap);
        bi = {};
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = w; bi.biHeight = -h;
        bi.biPlanes = 1; bi.biBitCount = 32; bi.biCompression = BI_RGB;
        bufferBGRA = cv::Mat(h, w, CV_8UC4);
        bufferBGR = cv::Mat(h, w, CV_8UC3);
    }

    ScreenCapture::~ScreenCapture() {
        DeleteObject(hBitmap);
        DeleteDC(hMemDC);
        ReleaseDC(nullptr, hScreenDC);
    }

    cv::Mat& ScreenCapture::grab() {
        BitBlt(hMemDC, 0, 0, w, h, hScreenDC, x, y, SRCCOPY);
        GetDIBits(hMemDC, hBitmap, 0, h, bufferBGRA.data,
            (BITMAPINFO*)&bi, DIB_RGB_COLORS);
        int fromTo[] = { 0,0,1,1,2,2 };
        cv::mixChannels(&bufferBGRA, 1, &bufferBGR, 1, fromTo, 3);
        return bufferBGR;
    }

} // namespace HandTracker