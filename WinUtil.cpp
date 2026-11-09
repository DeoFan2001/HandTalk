#define NOMINMAX
#include "WinUtil.h"
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace HandTracker {

    static bool isAscii(const std::wstring& w) {
        for (wchar_t c : w) if (c > 127) return false;
        return true;
    }

    static std::string toUtf8Path(const std::wstring& w) {
        if (w.empty()) return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
        return s;
    }

    std::string pickVideoFile(std::wstring& shownName) {
        wchar_t path[MAX_PATH] = L"";
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFilter = L"Видео\0*.mp4;*.avi;*.mkv;*.mov;*.wmv\0Все файлы\0*.*\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = L"Выберите видеофайл";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&ofn)) return std::string();

        std::wstring full = path;
        std::size_t slash = full.find_last_of(L"\\/");
        shownName = (slash == std::wstring::npos) ? full : full.substr(slash + 1);

        if (isAscii(full)) return toUtf8Path(full);
        wchar_t shortPath[MAX_PATH] = L"";
        DWORD n = GetShortPathNameW(full.c_str(), shortPath, MAX_PATH);
        if (n > 0 && n < MAX_PATH && isAscii(shortPath)) return toUtf8Path(shortPath);
        return toUtf8Path(full);        // короткие имена выключены: надеемся, что OpenCV справится
    }

    void minimizeWindow(const std::string& title) {
        HWND h = FindWindowA(nullptr, title.c_str());
        if (h) ShowWindow(h, SW_MINIMIZE);
    }

    void restoreWindow(const std::string& title) {
        HWND h = FindWindowA(nullptr, title.c_str());
        if (h) ShowWindow(h, SW_RESTORE);
    }

    bool excludeFromCapture(const std::string& title, bool exclude) {
        HWND h = FindWindowA(nullptr, title.c_str());
        if (!h) return false;
        return SetWindowDisplayAffinity(h, exclude ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE) != FALSE;
    }

} // namespace HandTracker
