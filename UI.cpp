#include "UI.h"
#include <windows.h>

namespace HandTracker {

    static bool g_dragging = false, g_done = false;
    static cv::Point g_startPt{ 0,0 }, g_endPt{ 0,0 };

    static void onMouse(int event, int x, int y, int, void*) {
        if (event == cv::EVENT_LBUTTONDOWN) {
            g_dragging = true; g_startPt = { x,y }; g_endPt = { x,y };
        }
        else if (event == cv::EVENT_MOUSEMOVE && g_dragging) {
            g_endPt = { x,y };
        }
        else if (event == cv::EVENT_LBUTTONUP) {
            g_dragging = false; g_endPt = { x,y }; g_done = true;
        }
    }

    std::optional<Region> selectRegion() {
        int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        HDC sdc = GetDC(nullptr), mdc = CreateCompatibleDC(sdc);
        HBITMAP bmp = CreateCompatibleBitmap(sdc, sw, sh);
        SelectObject(mdc, bmp);
        BitBlt(mdc, 0, 0, sw, sh, sdc, 0, 0, SRCCOPY);
        cv::Mat screen(sh, sw, CV_8UC4);
        BITMAPINFOHEADER bi = {};
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = sw; bi.biHeight = -sh;
        bi.biPlanes = 1; bi.biBitCount = 32; bi.biCompression = BI_RGB;
        GetDIBits(mdc, bmp, 0, sh, screen.data, (BITMAPINFO*)&bi, DIB_RGB_COLORS);
        DeleteObject(bmp); DeleteDC(mdc); ReleaseDC(nullptr, sdc);

        cv::Mat bgr; cv::cvtColor(screen, bgr, cv::COLOR_BGRA2BGR);
        cv::Mat darkened; bgr.convertTo(darkened, -1, 0.4, 0);

        std::string win = "Select region";
        cv::namedWindow(win, cv::WINDOW_NORMAL);
        cv::imshow(win, cv::Mat(1, 1, CV_8UC3, cv::Scalar(0, 0, 0)));
        cv::waitKey(100);
        cv::resizeWindow(win, sw, sh);
        cv::moveWindow(win, 0, 0);
        cv::setWindowProperty(win, cv::WND_PROP_FULLSCREEN, cv::WINDOW_FULLSCREEN);
        cv::waitKey(100);

        g_dragging = false; g_done = false;
        g_startPt = { 0,0 }; g_endPt = { 0,0 };
        cv::setMouseCallback(win, onMouse);

        while (!g_done) {
            cv::Mat c = darkened.clone();
            if (g_startPt.x || g_startPt.y || g_endPt.x || g_endPt.y) {
                cv::Rect r((std::min)(g_startPt.x, g_endPt.x), (std::min)(g_startPt.y, g_endPt.y),
                    std::abs(g_endPt.x - g_startPt.x), std::abs(g_endPt.y - g_startPt.y));
                r &= cv::Rect(0, 0, sw, sh);
                if (r.width > 0 && r.height > 0) {
                    bgr(r).copyTo(c(r));
                    cv::rectangle(c, r, cv::Scalar(0, 255, 0), 2);
                }
            }
            cv::putText(c, "Drag LMB. ESC = cancel", { 20,40 },
                cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(255, 255, 255), 2);
            cv::imshow(win, c);
            if (cv::waitKey(10) == 27) { cv::destroyWindow(win); return std::nullopt; }
        }
        cv::destroyWindow(win);

        Region reg;
        reg.x = (std::min)(g_startPt.x, g_endPt.x);
        reg.y = (std::min)(g_startPt.y, g_endPt.y);
        reg.w = std::abs(g_endPt.x - g_startPt.x);
        reg.h = std::abs(g_endPt.y - g_startPt.y);
        if (reg.w < 10 || reg.h < 10) return std::nullopt;
        return reg;
    }

    bool setupOverlayWindow(const std::string& winName, int x, int y, int w, int h, int alpha) {
        HWND hwnd = FindWindowA(nullptr, winName.c_str());
        if (!hwnd) return false;

        LONG style = GetWindowLong(hwnd, GWL_STYLE);
        style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
        style |= WS_POPUP;
        SetWindowLong(hwnd, GWL_STYLE, style);

        LONG ex = GetWindowLong(hwnd, GWL_EXSTYLE);
        ex |= WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT;
        SetWindowLong(hwnd, GWL_EXSTYLE, ex);

        SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
        SetLayeredWindowAttributes(hwnd, 0, (BYTE)alpha, LWA_ALPHA);
        SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
        return true;
    }

} // namespace HandTracker