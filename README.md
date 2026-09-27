# HandTalk

Переводчик русского жестового языка (РЖЯ) в текст и голос.

## Требования

- Visual Studio 2022 (версия 17.10+)
- OpenCV 4.10
- ONNX Runtime 1.20

## Установка

1. Клонируй репозиторий:

   git clone https://github.com/USERNAME/HandTalk.git
   cd HandTalk

2. Скачай OpenCV 4.10 с https://opencv.org/releases/ 
   Распакуй в `HandTalk\HandTalk\opencv\`

3. Скачай ONNX Runtime 1.20 с https://github.com/microsoft/onnxruntime/releases 
   Распакуй в `HandTalk\HandTalk\onnxruntime\`

4. Открой `HandTalk.sln` в Visual Studio
5. Выбери Release | x64
6. Build → Build Solution (Ctrl+Shift+B)
7. Debug → Start Without Debugging (Ctrl+F5)

## Использование

- **G** — выбрать область экрана
- **Q** — выход

## Структура

- `ScreenCapture.cpp` — захват экрана
- `HandTracker.cpp` — palm + landmark + Kalman
- `UI.cpp` — выбор области + оверлей
- `Utils.cpp` — константы, якоря, математика
- `main.cpp` — точка входа

## Авторы

- [Имя] — трекинг рук
- [Имя] — UI
- [Имя] — голос + API
- [Имя] — классификатор