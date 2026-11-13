#pragma once
// Gui.h - окно программы (рисуется средствами OpenCV, отдельных библиотек не нужно)
//
// Окно 1200x740, тёмная тема. Шесть экранов:
//   SCR_HOME       - меню: «Общение», «Изучить алфавит», «Настройки»;
//   SCR_TALK       - общение: видео, источник, «Начать/Закончить», поле текста, «Варианты ответа»;
//   SCR_LEARN      - изучение алфавита: эталон буквы, «Ваш жест», «Повторить», «Далее»;
//   SCR_SETTINGS   - настройки для пользователей (из handtalk.ini) и кнопка «Для разработчиков»;
//   SCR_DEV        - для разработчиков, вкладка «Образцы жестов»: буквы, запись образцов, обучение модели;
//   SCR_DEV_PARAMS - для разработчиков, вкладка «Параметры»: параметры распознавания из handtalk.ini.
// Нажатия мыши собираются в очередь, главный цикл забирает их через pollClick().
// Всё, что не меняется от кадра к кадру (карточки, кнопки, подписи), рисуется в отдельную картинку
// и перерисовывается только при изменении состояния (русский текст через GDI не быстрый).
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include "Gesture.h"
#include "Settings.h"

namespace HandTracker {

    // === ЭКРАНЫ ===
    enum Screen { SCR_HOME = 0, SCR_TALK, SCR_LEARN, SCR_SETTINGS, SCR_DEV, SCR_DEV_PARAMS };

    // === КНОПКИ ===
    enum GuiId {
        ID_NONE = -1,
        ID_GO_TALK = 0, ID_GO_LEARN, ID_GO_SETTINGS,    // карточки меню
        ID_NAV_BACK,                                    // «Назад» в меню
        ID_SRC_CAM0, ID_SRC_SCREEN, ID_SRC_FILE,        // источник: камера, область экрана, видеофайл
        ID_START, ID_STOP,
        ID_ERASE, ID_CLEAR,                             // поле текста: стереть букву, очистить
        ID_ANIM, ID_SEND,                               // «Анимация жестов», «Отправить»
        ID_SHOW_GESTURE, ID_RETRY, ID_NEXT,             // экран обучения
        ID_KIND_POSE, ID_KIND_MOVE, ID_REC, ID_TRAIN,   // настройки: образцы
        ID_REPLY_BOX,
        ID_GO_DEV,                                      // «Для разработчиков» в настройках
        ID_TAB_SAMPLES, ID_TAB_PARAMS,                  // вкладки раздела для разработчиков
        ID_SET_RESET,                                   // «Сбросить» настройки (на экране настроек и на вкладке параметров)
        ID_CAM_REFRESH,                                 // «Обновить список» в выпадающем списке камер
        ID_SPEAK_TEXT,                                  // озвучить распознанный текст (в поле над «Варианты ответа»)
        ID_LETTER_BASE = 100,                           // 100 + номер буквы в алфавите
        ID_CAM_BASE = 200,                              // 200 + номер строки в списке камер
        ID_SET_BASE = 1000                              // 1000 + 2 * номер настройки (+0 - кнопка «-», +1 - кнопка «+»)
    };

    // Результат проверки жеста на экране обучения
    enum LearnResult { LR_WAIT = 0, LR_OK, LR_WRONG };

    // Что показывать в окне (меняется редко: перерисовка при изменении)
    struct GuiState {
        Screen screen = SCR_HOME;
        int source = 0;                           // 0 - камера (номер в camera), 3 - файл, 4 - область экрана
        int camera = 0;                           // номер выбранной камеры
        std::vector<int> cameras;                 // найденные камеры (для выпадающего списка)
        bool camMenuOpen = false;                 // выпадающий список камер открыт
        std::wstring fileLabel;                   // имя выбранного файла
        std::wstring screenLabel;                 // размер выбранной области
        bool running = false;
        bool canSpeak = false;
        bool animOn = false;                      // идёт побуквенная анимация сообщения
        // --- настройки: образцы ---
        int letter = 0;                           // выбранная буква (номер в алфавите)
        bool kindMove = false;                    // записываем движение (иначе позу)
        bool recording = false;
        int recCount = 0;                         // записано в этом сеансе
        int letterPose = 0, letterMove = 0;       // образцов у выбранной буквы
        unsigned long long poseMask = 0;          // у каких букв есть образцы поз (бит = номер буквы)
        unsigned long long moveMask = 0;          // ... образцы движений
        // --- обучение ---
        int learnTarget = 0;                      // какую букву просим показать
        int learnTotal = 0;                       // сколько букв в курсе (для «3 из 33»)
        LearnResult learnResult = LR_WAIT;
        wchar_t learnShown = 0;                   // какую букву сейчас видит программа (0 - никакую)
        bool showRef = false;                     // в окне эталон, а не камера
        std::vector<std::wstring> setVals;        // значения настроек для показа (по номерам SettingId)
        std::wstring replyText;   // текст, вводимый с клавиатуры
        bool replyActive = false; // фокус на поле ввода
        bool operator==(const GuiState& o) const {
            return screen == o.screen && source == o.source && camera == o.camera && cameras == o.cameras &&
                camMenuOpen == o.camMenuOpen && fileLabel == o.fileLabel && screenLabel == o.screenLabel &&
                running == o.running && canSpeak == o.canSpeak && animOn == o.animOn &&
                letter == o.letter && kindMove == o.kindMove &&
                recording == o.recording && recCount == o.recCount && letterPose == o.letterPose &&
                letterMove == o.letterMove && poseMask == o.poseMask && moveMask == o.moveMask &&
                learnTarget == o.learnTarget && learnTotal == o.learnTotal && learnResult == o.learnResult &&
                learnShown == o.learnShown && showRef == o.showRef && setVals == o.setVals &&
                replyText == o.replyText && replyActive == o.replyActive;
        }
        bool operator!=(const GuiState& o) const { return !(*this == o); }
    };

    // Что показывать на видео, под ним и в строке состояния (может меняться каждый кадр)
    struct InfoState {
        std::wstring text;                        // набранный текст
        std::wstring status;                      // строка состояния
        std::wstring message;                     // сообщение пользователю
        std::wstring candidate;                   // буква, которая сейчас вводится
        float progress = 0;                       // готовность ввода 0..1
        std::wstring videoNote;                   // надпись на видео (например, "ЗАПИСЬ")
        // --- рисунок руки в окне (эталон буквы на экране обучения или анимация сообщения) ---
        bool art = false;                         // показывать рисунок вместо видео
        bool artHasPose = false;                  // есть поза для рисунка (иначе только буква и подпись)
        Features artFeat;                         // поза буквы
        std::wstring artLetter;                   // буква крупно
        std::wstring artCaption;                  // подпись сверху («Эталон», «Сообщение: 2 из 7»)
        std::wstring artNote;                     // пояснение снизу
    };

    class Gui {
    public:
        static constexpr int W = 1200, H = 740;
        static constexpr int STRIP_Y = 676;       // с этой высоты - полоса состояния
        bool hadClickThisFrame() const { return clicked_; }
        explicit Gui(const std::string& winName);
        void init();                                              // создать окно, подключить мышь
        int pollClick();                                          // номер нажатой кнопки или ID_NONE
        void render(const GuiState& gs, const InfoState& is, const cv::Mat& video);   // нарисовать и показать

    private:
        struct Btn { Screen screen; int id; cv::Rect r; };
        std::string win_;
        std::vector<Btn> btns_;
        cv::Mat canvas_, base_;                                   // canvas_ - кадр целиком, base_ - его неподвижная часть
        GuiState lastState_;
        std::wstring lastText_;
        int lastHover_ = -2;
        bool baseValid_ = false;
        Screen screen_ = SCR_HOME;                                // какой экран сейчас на картинке
        bool enabledStart_ = true, enabledStop_ = false, enabledSpeak_ = false;
        bool enabledSpeakText_ = false;                           // есть распознанный текст и голос
        bool camMenuOpen_ = false;                                // список камер открыт (для проверки нажатий)
        int camCount_ = 0;                                        // сколько камер в списке

        bool clicked_ = false;
        int clickX_ = 0, clickY_ = 0;
        int mouseX_ = -1, mouseY_ = -1;
        int hover_ = ID_NONE;                                     // кнопка под курсором

        void layout();
        int hit(int x, int y) const;
        cv::Rect rectOf(int id) const;
        cv::Rect videoRect() const;

        void drawBase(const GuiState& gs, const InfoState& is);
        void drawHome();
        void drawTalk(const GuiState& gs, const InfoState& is);
        void drawLearn(const GuiState& gs);
        void drawSettings(const GuiState& gs);
        void drawDev(const GuiState& gs);
        void drawDevParams(const GuiState& gs);
        void drawSettingRow(const GuiState& gs, int id);
        cv::Rect camPanel() const;                                // рамка выпадающего списка камер
        cv::Rect camRow(int k) const;                             // строка списка: камеры, затем «Обновить список»
        void drawCamMenu(const GuiState& gs);
        void drawDevTabs(bool samples);
        void drawHeader(const wchar_t* title, const std::wstring& subtitle, int icon);
        void drawSourceCard(const GuiState& gs, cv::Rect card);
        void drawRunCard(const GuiState& gs, cv::Rect card);
        void drawVideo(const GuiState& gs, const InfoState& is, const cv::Mat& video);
        void drawStrip(const GuiState& gs, const InfoState& is);

        // кнопка: style 0 - обычная, 1 - главная (синяя), 2 - красная, 3 - зелёная
        void button(int id, const std::wstring& label, int style, bool enabled, int icon, int fontPx = 18);
        void tile(int id, const std::wstring& label, bool active, int icon);
        static void onMouse(int event, int x, int y, int flags, void* self);
    };

} // namespace HandTracker
