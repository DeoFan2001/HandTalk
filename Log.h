#pragma once
// Log.h - журнал работы (handtalk.log)
// Пишет ошибки и статистику быстродействия. По журналу можно проверить работу на слабом компьютере.
#include <string>

namespace HandTracker {

    // Открыть журнал. Строки дописываются в конец; если файл больше 1 МБ, он начинается заново.
    void logOpen(const std::string& path);
    void logClose();

    void logInfo(const std::string& msg);
    void logWarn(const std::string& msg);
    void logError(const std::string& msg);

    // Для широких строк (сообщения на русском) нужен только logInfo
    void logInfo(const std::wstring& msg);

} // namespace HandTracker
