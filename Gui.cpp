#include "Gui.h"
#include "Alphabet.h"
#include "TextRender.h"
#include <algorithm>

namespace HandTracker {

    // === ЦВЕТА (BGR) ===
    static const cv::Scalar C_BG(36, 34, 32);          // фон панели
    static const cv::Scalar C_INFO(24, 24, 24);        // фон под видео
    static const cv::Scalar C_BTN(78, 74, 70);         // кнопка
    static const cv::Scalar C_BTN_OFF(52, 50, 48);     // недоступная кнопка
    static const cv::Scalar C_TEXT(240, 240, 240);
    static const cv::Scalar C_DIM(150, 150, 150);
    static const cv::Scalar C_GREEN(70, 150, 70);
    static const cv::Scalar C_RED(60, 60, 190);
    static const cv::Scalar C_ACCENT(190, 120, 40);    // выбранный пункт (синий)
    static const cv::Scalar C_MOVE(40, 130, 220);      // оранжевый: есть образцы движений

    Gui::Gui(const std::string& winName) : win_(winName) {
        layout();
    }

    // === РАСПОЛОЖЕНИЕ КНОПОК ===
    void Gui::layout() {
        const int x0 = PANEL_X + 12, w = PANEL_W - 24;       // 276 пикселей
        btns_.clear();
        auto add = [&](int id, int x, int y, int bw, int bh) { btns_.push_back({ id, cv::Rect(x, y, bw, bh) }); };

        for (int i = 0; i < 5; ++i) add(ID_SRC_CAM0 + i, x0, 34 + i * 32, w, 28);       // источники
        add(ID_START, x0, 200, 134, 36);
        add(ID_STOP, x0 + 142, 200, 134, 36);
        add(ID_BACK, x0, 244, 86, 30);
        add(ID_CLEAR, x0 + 95, 244, 86, 30);
        add(ID_SPEAK, x0 + 190, 244, 86, 30);

        // сетка букв: 8 столбцов x 5 строк
        const int cw = 34, ch = 28, gy = 316;
        for (std::size_t i = 0; i < ALPHABET_SIZE; ++i)
            add(ID_LETTER_BASE + (int)i, x0 + (int)(i % 8) * cw, gy + (int)(i / 8) * ch, cw - 2, ch - 2);

        add(ID_KIND_POSE, x0, 482, 134, 28);
        add(ID_KIND_MOVE, x0 + 142, 482, 134, 28);
        add(ID_REC, x0, 540, w, 34);
        add(ID_TRAIN, x0, 582, w, 36);
    }

    cv::Rect Gui::rectOf(int id) const {
        for (const Btn& b : btns_) if (b.id == id) return b.r;
        return cv::Rect();
    }

    int Gui::hit(int x, int y) const {
        for (const Btn& b : btns_) {
            if (!b.r.contains({ x, y })) continue;
            if (b.id == ID_START && !enabledStart_) return ID_NONE;
            if (b.id == ID_STOP && !enabledStop_) return ID_NONE;
            if (b.id == ID_SPEAK && !enabledSpeak_) return ID_NONE;
            return b.id;
        }
        return ID_NONE;
    }

    // === МЫШЬ ===
    void Gui::onMouse(int event, int x, int y, int, void* self) {
        if (event != cv::EVENT_LBUTTONDOWN) return;
        Gui* g = static_cast<Gui*>(self);
        g->clicked_ = true;
        g->clickX_ = x;
        g->clickY_ = y;
    }

    void Gui::init() {
        cv::namedWindow(win_, cv::WINDOW_AUTOSIZE);        // размер окна равен размеру картинки: координаты мыши точные
        cv::setMouseCallback(win_, &Gui::onMouse, this);
        canvas_ = cv::Mat(H, W, CV_8UC3, C_INFO);
    }

    int Gui::pollClick() {
        if (!clicked_) return ID_NONE;
        clicked_ = false;
        return hit(clickX_, clickY_);
    }

    // === КНОПКА НА ПАНЕЛИ ===
    void Gui::drawButton(int id, const std::wstring& label, bool active, cv::Scalar activeColor, bool enabled) {
        cv::Rect r = rectOf(id);
        r.x -= PANEL_X;                                    // панель рисуется в своей картинке
        cv::Scalar fill = !enabled ? C_BTN_OFF : (active ? activeColor : C_BTN);
        cv::rectangle(panel_, r, fill, cv::FILLED);
        const int fh = 16;
        int tw = textWidthW(label, fh, active);
        putTextW(panel_, label, { r.x + std::max(4, (r.width - tw) / 2), r.y + (r.height - fh) / 2 - 1 }, fh,
                 enabled ? C_TEXT : C_DIM, active);
    }

    // === ПАНЕЛЬ ===
    void Gui::drawPanel(const GuiState& gs) {
        panel_ = cv::Mat(H, PANEL_W, CV_8UC3, C_BG);
        const int lx = 12;

        putTextW(panel_, L"Источник видео", { lx, 8 }, 17, C_DIM, true);
        std::wstring names[5] = { L"Камера 0", L"Камера 1", L"Камера 2",
            gs.fileLabel.empty() ? std::wstring(L"Видеофайл...") : L"Файл: " + gs.fileLabel,
            gs.screenLabel.empty() ? std::wstring(L"Область экрана...") : L"Экран: " + gs.screenLabel };
        for (int i = 0; i < 5; ++i)
            drawButton(ID_SRC_CAM0 + i, names[i], gs.source == i, C_ACCENT);

        // доступная кнопка яркая, недоступная серая
        drawButton(ID_START, L"Начать", !gs.running, C_GREEN, !gs.running);
        drawButton(ID_STOP, L"Закончить", gs.running, C_RED, gs.running);

        drawButton(ID_BACK, L"Стереть", false, C_BTN);
        drawButton(ID_CLEAR, L"Очистить", false, C_BTN);
        drawButton(ID_SPEAK, L"Озвучить", false, C_BTN, gs.canSpeak);

        // --- раздел для администраторов ---
        cv::line(panel_, { lx, 284 }, { PANEL_W - lx, 284 }, cv::Scalar(90, 90, 90), 1);
        putTextW(panel_, L"Для администраторов", { lx, 292 }, 17, C_DIM, true);

        for (std::size_t i = 0; i < ALPHABET_SIZE; ++i) {
            int id = ID_LETTER_BASE + (int)i;
            bool pose = (gs.poseMask >> i) & 1, move = (gs.moveMask >> i) & 1;
            cv::Scalar fill = C_BTN;
            if (pose && move) fill = cv::Scalar(90, 140, 90);
            else if (pose) fill = cv::Scalar(130, 90, 50);
            else if (move) fill = cv::Scalar(50, 100, 170);
            bool sel = (int)i == gs.letter;
            cv::Rect r = rectOf(id);
            r.x -= PANEL_X;
            cv::rectangle(panel_, r, fill, cv::FILLED);
            if (sel) cv::rectangle(panel_, r, cv::Scalar(255, 255, 255), 2);
            std::wstring s(1, ALPHABET[i].letter);
            int tw = textWidthW(s, 16, sel);
            putTextW(panel_, s, { r.x + (r.width - tw) / 2, r.y + 4 }, 16, C_TEXT, sel);
        }
        putTextW(panel_, L"синий - есть позы, оранжевый - есть движения", { lx, 458 }, 13, C_DIM);

        drawButton(ID_KIND_POSE, L"Поза", !gs.kindMove, C_ACCENT);
        drawButton(ID_KIND_MOVE, L"Движение", gs.kindMove, C_ACCENT);

        std::wstring sel = std::wstring(1, ALPHABET[gs.letter].letter) + L": поз " + std::to_wstring(gs.letterPose) +
                           L", движений " + std::to_wstring(gs.letterMove);
        putTextW(panel_, sel, { lx, 516 }, 16, C_TEXT);

        std::wstring rec = gs.recording ? L"Остановить запись (" + std::to_wstring(gs.recCount) + L")" : L"Запись образцов";
        drawButton(ID_REC, rec, gs.recording, C_RED);
        drawButton(ID_TRAIN, L"Обучить модель", false, C_BTN);
        putTextW(panel_, L"Образцы: gestures.csv, gestures_dynamic.csv", { lx, 626 }, 13, C_DIM);
    }

    // === ПОЛНАЯ ОТРИСОВКА КАДРА ===
    void Gui::render(const GuiState& gs, const InfoState& is, const cv::Mat& video) {
        enabledStart_ = !gs.running;
        enabledStop_ = gs.running;
        enabledSpeak_ = gs.canSpeak;

        if (!panelValid_ || gs != lastState_) {
            drawPanel(gs);
            lastState_ = gs;
            panelValid_ = true;
        }
        panel_.copyTo(canvas_(cv::Rect(PANEL_X, 0, PANEL_W, H)));

        // --- видео по центру области, с сохранением пропорций ---
        cv::Rect vr(0, 0, VIDEO_W, VIDEO_H);
        cv::rectangle(canvas_, vr, cv::Scalar(16, 16, 16), cv::FILLED);
        if (!video.empty()) {
            double sc = std::min((double)VIDEO_W / video.cols, (double)VIDEO_H / video.rows);
            int w = std::max(1, (int)(video.cols * sc)), h = std::max(1, (int)(video.rows * sc));
            cv::Mat small;
            cv::resize(video, small, cv::Size(w, h), 0, 0, cv::INTER_AREA);
            small.copyTo(canvas_(cv::Rect((VIDEO_W - w) / 2, (VIDEO_H - h) / 2, w, h)));
        }
        else if (!is.placeholder.empty()) {
            int tw = textWidthW(is.placeholder, 22);
            putTextW(canvas_, is.placeholder, { std::max(10, (VIDEO_W - tw) / 2), VIDEO_H / 2 - 12 }, 22, C_DIM);
        }
        if (!is.videoNote.empty()) putTextW(canvas_, is.videoNote, { 12, 10 }, 22, cv::Scalar(0, 200, 255), true);
        if (!is.candidate.empty()) {
            putTextW(canvas_, is.candidate, { VIDEO_W - 90, 8 }, 64, cv::Scalar(0, 255, 255), true);
            cv::rectangle(canvas_, { VIDEO_W - 90, 82 }, { VIDEO_W - 20, 88 }, cv::Scalar(90, 90, 90), cv::FILLED);
            cv::rectangle(canvas_, { VIDEO_W - 90, 82 }, { VIDEO_W - 90 + (int)(70 * is.progress), 88 }, cv::Scalar(0, 255, 0), cv::FILLED);
        }

        // --- текст и строка состояния под видео ---
        cv::rectangle(canvas_, cv::Rect(0, VIDEO_H, VIDEO_W, H - VIDEO_H), C_INFO, cv::FILLED);
        for (std::size_t i = 0; i < is.lines.size() && i < 3; ++i)
            putTextW(canvas_, is.lines[i], { 12, VIDEO_H + 6 + (int)i * 36 }, 28, cv::Scalar(255, 255, 255), true);
        putTextW(canvas_, is.status, { 12, H - 50 }, 16, C_DIM);
        putTextW(canvas_, is.message, { 12, H - 26 }, 16, cv::Scalar(0, 210, 255));

        cv::imshow(win_, canvas_);
    }

} // namespace HandTracker
