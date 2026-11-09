#pragma once
// Gui.h - окно программы с кнопками (рисуется средствами OpenCV, отдельных библиотек не нужно)
//
// Окно 1180x660: слева видео и текст, справа панель кнопок.
// Нажатия мыши собираются в очередь, главный цикл забирает их через pollClick().
// Панель с кнопками перерисовывается только при изменении состояния (русский текст через GDI не быстрый).
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace HandTracker {

    // === КНОПКИ ===
    enum GuiId {
        ID_NONE = -1,
        ID_SRC_CAM0 = 0, ID_SRC_CAM1, ID_SRC_CAM2, ID_SRC_FILE, ID_SRC_SCREEN,   // источник видео
        ID_START, ID_STOP,
        ID_BACK, ID_CLEAR, ID_SPEAK,
        ID_KIND_POSE, ID_KIND_MOVE,
        ID_REC, ID_TRAIN,
        ID_LETTER_BASE = 100                      // 100 + номер буквы в алфавите
    };

    // Что показывать на панели
    struct GuiState {
        int source = 0;                           // 0..2 - камера, 3 - файл, 4 - область экрана
        std::wstring fileLabel;                   // имя выбранного файла
        std::wstring screenLabel;                 // размер выбранной области
        bool running = false;
        bool canSpeak = false;
        int letter = 0;                           // выбранная буква (номер в алфавите)
        bool kindMove = false;                    // записываем движение (иначе позу)
        bool recording = false;
        int recCount = 0;                         // записано в этом сеансе
        int letterPose = 0, letterMove = 0;       // образцов у выбранной буквы
        unsigned long long poseMask = 0;          // у каких букв есть образцы поз (бит = номер буквы)
        unsigned long long moveMask = 0;          // ... образцы движений
        bool operator==(const GuiState& o) const {
            return source == o.source && fileLabel == o.fileLabel && screenLabel == o.screenLabel &&
                   running == o.running && canSpeak == o.canSpeak && letter == o.letter && kindMove == o.kindMove &&
                   recording == o.recording && recCount == o.recCount && letterPose == o.letterPose &&
                   letterMove == o.letterMove && poseMask == o.poseMask && moveMask == o.moveMask;
        }
        bool operator!=(const GuiState& o) const { return !(*this == o); }
    };

    // Что показывать на видео и под ним
    struct InfoState {
        std::vector<std::wstring> lines;          // последние строки текста (до 3)
        std::wstring status;                      // строка состояния
        std::wstring message;                     // сообщение пользователю
        std::wstring candidate;                   // буква, которая сейчас вводится
        float progress = 0;                       // готовность ввода 0..1
        std::wstring videoNote;                   // надпись на видео (например, "ЗАПИСЬ")
        std::wstring placeholder;                 // что писать на пустом экране
    };

    class Gui {
    public:
        static constexpr int W = 1180, H = 660;
        static constexpr int VIDEO_W = 880, VIDEO_H = 495;
        static constexpr int PANEL_X = 880, PANEL_W = W - PANEL_X;

        explicit Gui(const std::string& winName);
        void init();                                              // создать окно, подключить мышь
        int pollClick();                                          // номер нажатой кнопки или ID_NONE
        void render(const GuiState& gs, const InfoState& is, const cv::Mat& video);   // нарисовать и показать

    private:
        struct Btn { int id; cv::Rect r; };
        std::string win_;
        std::vector<Btn> btns_;
        cv::Mat canvas_, panel_;
        GuiState lastState_;
        bool panelValid_ = false;
        bool enabledStart_ = true, enabledStop_ = false, enabledSpeak_ = false;

        bool clicked_ = false;
        int clickX_ = 0, clickY_ = 0;

        void layout();
        int hit(int x, int y) const;
        void drawPanel(const GuiState& gs);
        void drawButton(int id, const std::wstring& label, bool active, cv::Scalar activeColor, bool enabled = true);
        cv::Rect rectOf(int id) const;
        static void onMouse(int event, int x, int y, int flags, void* self);
    };

} // namespace HandTracker
