#include "Log.h"
#include "Text.h"
#include <ctime>
#include <fstream>

namespace HandTracker {

    static std::ofstream g_log;

    // === ВРЕМЕННАЯ МЕТКА ===
    static std::string timestamp() {
        std::time_t t = std::time(nullptr);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
        return buf;
    }

    // === ОТКРЫТИЕ / ЗАКРЫТИЕ ===
    void logOpen(const std::string& path) {
        if (g_log.is_open()) g_log.close();

        // размер уже существующего файла
        long long size = 0;
        {
            std::ifstream in(path, std::ios::binary | std::ios::ate);
            if (in) size = (long long)in.tellg();
        }

        if (size == 0 || size > 1024 * 1024) {
            // новый файл: пишем метку UTF-8, чтобы Блокнот правильно показал русский текст
            g_log.open(path, std::ios::binary | std::ios::trunc);
            if (g_log) g_log << "\xEF\xBB\xBF";
        }
        else {
            g_log.open(path, std::ios::binary | std::ios::app);
        }
    }

    void logClose() {
        if (g_log.is_open()) g_log.close();
    }

    // === ЗАПИСЬ СТРОКИ ===
    static void write(const char* level, const std::string& msg) {
        if (!g_log.is_open()) return;
        g_log << timestamp() << " [" << level << "] " << msg << "\r\n";
        g_log.flush();
    }

    void logInfo(const std::string& msg) { write("INFO", msg); }
    void logWarn(const std::string& msg) { write("WARN", msg); }
    void logError(const std::string& msg) { write("ERROR", msg); }

    void logInfo(const std::wstring& msg) { write("INFO", toUtf8(msg)); }

} // namespace HandTracker
