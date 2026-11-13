#pragma once
// WinUtil.h - небольшие функции Windows для окна программы
#include <string>

namespace HandTracker {

    // Диалог выбора видеофайла. Возвращает путь, пригодный для OpenCV (при русских буквах в пути
    // берётся короткое имя 8.3, чтобы OpenCV точно открыл файл). Пустая строка - пользователь отказался.
    // shownName - имя файла для показа на кнопке.
    std::string pickVideoFile(std::wstring& shownName);

    // Окно по заголовку: свернуть, вернуть
    void minimizeWindow(const std::string& title);
    void restoreWindow(const std::string& title);

    // Тёмная рамка и заголовок окна (Windows 10 версии 20H1 и новее; на старых системах ничего не меняется).
    // Нужна, чтобы заголовок не выбивался из тёмного оформления программы.
    void applyDarkTitleBar(const std::string& title);

    // Не попадать в захват экрана (Windows 10 версии 2004 и новее). Нужно в режиме "Область экрана",
    // чтобы окно программы само не оказалось в кадре. Возвращает false, если система не поддерживает.
    bool excludeFromCapture(const std::string& title, bool exclude);

} // namespace HandTracker
