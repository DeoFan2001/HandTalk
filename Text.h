#pragma once
// Text.h - преобразование строк wchar_t <-> UTF-8
// Нужно, чтобы русский текст одинаково писался в файлы (журнал, настройки)
// независимо от кодовой страницы Windows.
#include <string>

namespace HandTracker {

    // === ШИРОКАЯ СТРОКА -> UTF-8 ===
    inline std::string toUtf8(const std::wstring& w) {
        std::string out;
        for (std::size_t i = 0; i < w.size(); ++i) {
            unsigned long c = (unsigned long)w[i];
            // на Windows wchar_t 16-битный: символы вне BMP приходят парой суррогатов
            if (sizeof(wchar_t) == 2 && c >= 0xD800 && c <= 0xDBFF && i + 1 < w.size()) {
                unsigned long c2 = (unsigned long)w[i + 1];
                if (c2 >= 0xDC00 && c2 <= 0xDFFF) {
                    c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00);
                    ++i;
                }
            }
            if (c < 0x80) {
                out += (char)c;
            }
            else if (c < 0x800) {
                out += (char)(0xC0 | (c >> 6));
                out += (char)(0x80 | (c & 0x3F));
            }
            else if (c < 0x10000) {
                out += (char)(0xE0 | (c >> 12));
                out += (char)(0x80 | ((c >> 6) & 0x3F));
                out += (char)(0x80 | (c & 0x3F));
            }
            else {
                out += (char)(0xF0 | (c >> 18));
                out += (char)(0x80 | ((c >> 12) & 0x3F));
                out += (char)(0x80 | ((c >> 6) & 0x3F));
                out += (char)(0x80 | (c & 0x3F));
            }
        }
        return out;
    }

    // === UTF-8 -> ШИРОКАЯ СТРОКА ===
    // Некорректные байты заменяются на U+FFFD, программа из-за них не падает
    inline std::wstring fromUtf8(const std::string& s) {
        std::wstring out;
        std::size_t i = 0;
        while (i < s.size()) {
            unsigned char b = (unsigned char)s[i];
            unsigned long cp = 0xFFFD;
            int len = 1;
            if (b < 0x80) { cp = b; }
            else if ((b & 0xE0) == 0xC0) { len = 2; cp = b & 0x1F; }
            else if ((b & 0xF0) == 0xE0) { len = 3; cp = b & 0x0F; }
            else if ((b & 0xF8) == 0xF0) { len = 4; cp = b & 0x07; }
            if (len > 1) {
                if (i + len > s.size()) { cp = 0xFFFD; len = 1; }
                else {
                    for (int k = 1; k < len; ++k) {
                        unsigned char cb = (unsigned char)s[i + k];
                        if ((cb & 0xC0) != 0x80) { cp = 0xFFFD; len = 1; break; }
                        cp = (cp << 6) | (cb & 0x3F);
                    }
                }
            }
            i += len;
            if (sizeof(wchar_t) == 2 && cp >= 0x10000) {
                cp -= 0x10000;
                out += (wchar_t)(0xD800 + (cp >> 10));
                out += (wchar_t)(0xDC00 + (cp & 0x3FF));
            }
            else {
                out += (wchar_t)cp;
            }
        }
        return out;
    }

} // namespace HandTracker
