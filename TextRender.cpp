#define NOMINMAX
#include "TextRender.h"
#include <windows.h>
#include <algorithm>

namespace HandTracker {

    static HFONT makeFont(int heightPx, bool bold) {
        return CreateFontW(-heightPx, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");   // если шрифта нет, Windows подставит другой
    }

    int textWidthW(const std::wstring& text, int heightPx, bool bold) {
        if (text.empty()) return 0;
        HDC dc = CreateCompatibleDC(nullptr);
        HFONT font = makeFont(heightPx, bold);
        HGDIOBJ old = SelectObject(dc, font);
        SIZE sz{ 0, 0 };
        GetTextExtentPoint32W(dc, text.c_str(), (int)text.size(), &sz);
        SelectObject(dc, old);
        DeleteObject(font);
        DeleteDC(dc);
        return sz.cx;
    }

    void putTextW(cv::Mat& img, const std::wstring& text, cv::Point org, int heightPx,
                  cv::Scalar color, bool bold) {
        if (text.empty() || img.empty() || img.type() != CV_8UC3) return;

        HDC dc = CreateCompatibleDC(nullptr);
        HFONT font = makeFont(heightPx, bold);
        HGDIOBJ oldFont = SelectObject(dc, font);
        SIZE sz{ 0, 0 };
        GetTextExtentPoint32W(dc, text.c_str(), (int)text.size(), &sz);
        int w = sz.cx + 2, h = sz.cy + 2;

        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;                   // сверху вниз
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bmp || !bits) { SelectObject(dc, oldFont); DeleteObject(font); DeleteDC(dc); return; }
        HGDIOBJ oldBmp = SelectObject(dc, bmp);

        std::fill((unsigned char*)bits, (unsigned char*)bits + (size_t)w * h * 4, 0);   // чёрный фон
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        TextOutW(dc, 1, 1, text.c_str(), (int)text.size());
        GdiFlush();

        // белый текст на чёрном: яркость пикселя = прозрачность буквы
        const unsigned char* src = (const unsigned char*)bits;
        for (int yy = 0; yy < h; ++yy) {
            int iy = org.y + yy;
            if (iy < 0 || iy >= img.rows) continue;
            cv::Vec3b* row = img.ptr<cv::Vec3b>(iy);
            for (int xx = 0; xx < w; ++xx) {
                int ix = org.x + xx;
                if (ix < 0 || ix >= img.cols) continue;
                float a = src[(yy * w + xx) * 4 + 1] / 255.0f;
                if (a <= 0) continue;
                for (int c = 0; c < 3; ++c)
                    row[ix][c] = (uchar)(row[ix][c] * (1 - a) + color[c] * a);
            }
        }

        SelectObject(dc, oldBmp);
        DeleteObject(bmp);
        SelectObject(dc, oldFont);
        DeleteObject(font);
        DeleteDC(dc);
    }

    std::vector<std::wstring> wrapTextW(const std::wstring& text, int heightPx, int maxWidth, int maxLines) {
        std::vector<std::wstring> lines;
        std::wstring cur;
        for (wchar_t ch : text) {
            std::wstring t = cur + ch;
            if (!cur.empty() && textWidthW(t, heightPx) > maxWidth) {
                lines.push_back(cur);
                cur = (ch == L' ') ? L"" : std::wstring(1, ch);
            }
            else cur = t;
        }
        lines.push_back(cur);
        if ((int)lines.size() > maxLines) lines.erase(lines.begin(), lines.end() - maxLines);
        return lines;
    }

} // namespace HandTracker
