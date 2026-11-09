#include "Gui.h"
#include "Alphabet.h"
#include "TextRender.h"
#include <algorithm>

namespace HandTracker {

    // === ЦВЕТА (BGR) ===
    static const cv::Scalar C_BG(34, 32, 30);          // фон панели
    static const cv::Scalar C_INFO(24, 24, 24);        // фон под видео
    static const cv::Scalar C_STRIP(30, 29, 28);       // полоса состояния внизу
    static const cv::Scalar C_LINE(62, 60, 58);        // тонкие разделители
    static const cv::Scalar C_BORDER(92, 90, 88);      // рамка кнопок без заливки
    static const cv::Scalar C_BTN(78, 74, 70);         // кнопка
    static const cv::Scalar C_BTN_OFF(48, 46, 44);     // недоступная кнопка
    static const cv::Scalar C_TEXT(240, 240, 240);
    static const cv::Scalar C_DIM(150, 150, 150);
    static const cv::Scalar C_GREEN(70, 150, 70);
    static const cv::Scalar C_RED(60, 60, 190);
    static const cv::Scalar C_ACCENT(190, 120, 40);    // выбранный пункт (синий)
    static const cv::Scalar C_POSE(190, 120, 40);      // буква: есть образцы поз (синий)
    static const cv::Scalar C_MOVE(40, 130, 220);      // буква: есть образцы движений (оранжевый)

    // Прямоугольник со скруглёнными углами (заливка)
    static void roundRect(cv::Mat& img, cv::Rect r, int rad, const cv::Scalar& c) {
        if (r.width <= 0 || r.height <= 0) return;
        rad = std::max(0, std::min(rad, std::min(r.width, r.height) / 2));
        cv::rectangle(img, cv::Rect(r.x + rad, r.y, r.width - 2 * rad, r.height), c, cv::FILLED);
        cv::rectangle(img, cv::Rect(r.x, r.y + rad, r.width, r.height - 2 * rad), c, cv::FILLED);
        const int x1 = r.x + r.width - 1 - rad, y1 = r.y + r.height - 1 - rad;
        cv::circle(img, { r.x + rad, r.y + rad }, rad, c, cv::FILLED, cv::LINE_AA);
        cv::circle(img, { x1, r.y + rad }, rad, c, cv::FILLED, cv::LINE_AA);
        cv::circle(img, { r.x + rad, y1 }, rad, c, cv::FILLED, cv::LINE_AA);
        cv::circle(img, { x1, y1 }, rad, c, cv::FILLED, cv::LINE_AA);
    }

    // Скруглённый прямоугольник, разделённый по диагонали: сверху слева цвет a, снизу справа цвет b
    static void splitRoundRect(cv::Mat& img, cv::Rect r, int rad, const cv::Scalar& a, const cv::Scalar& b) {
        if (r.width <= 0 || r.height <= 0) return;
        roundRect(img, r, rad, a);
        cv::Mat shape(r.height, r.width, CV_8UC1, cv::Scalar(0));
        roundRect(shape, cv::Rect(0, 0, r.width, r.height), rad, cv::Scalar(255));
        cv::Mat half(r.height, r.width, CV_8UC1, cv::Scalar(0));
        cv::Point tri[3] = { { r.width - 1, 0 }, { r.width - 1, r.height - 1 }, { 0, r.height - 1 } };
        cv::fillConvexPoly(half, tri, 3, cv::Scalar(255), cv::LINE_AA);
        cv::bitwise_and(shape, half, shape);
        img(r).setTo(b, shape);
    }

    // Обрезать строку с многоточием, чтобы влезла в maxW пикселей
    static std::wstring fitText(std::wstring s, int maxW, int heightPx, bool bold) {
        if (textWidthW(s, heightPx, bold) <= maxW) return s;
        while (s.size() > 1 && textWidthW(s + L"...", heightPx, bold) > maxW) s.pop_back();
        return s + L"...";
    }

    // Иконка примерно 18x18 пикселей с центром в c
    static void drawIcon(cv::Mat& img, GuiIcon ic, cv::Point c, const cv::Scalar& col) {
        const int aa = cv::LINE_AA;
        auto P = [&](int dx, int dy) { return cv::Point(c.x + dx, c.y + dy); };
        switch (ic) {
        case GI_CAMERA:
            cv::rectangle(img, P(-9, -5), P(9, 7), col, 2, aa);                 // корпус
            cv::rectangle(img, P(-4, -8), P(3, -5), col, cv::FILLED, aa);       // выступ видоискателя
            cv::circle(img, P(0, 1), 3, col, 2, aa);                            // объектив
            break;
        case GI_FILM: {
            cv::rectangle(img, P(-9, -7), P(9, 7), col, 2, aa);                 // кадр
            cv::Point tri[3] = { P(-2, -3), P(-2, 3), P(3, 0) };
            cv::fillConvexPoly(img, tri, 3, col, aa);                           // треугольник воспроизведения
            break;
        }
        case GI_CROP: {                                                         // уголки рамки выделения
            const int a = 8, l = 5;
            cv::line(img, P(-a, -a), P(-a + l, -a), col, 2, aa);  cv::line(img, P(-a, -a), P(-a, -a + l), col, 2, aa);
            cv::line(img, P(a, -a), P(a - l, -a), col, 2, aa);    cv::line(img, P(a, -a), P(a, -a + l), col, 2, aa);
            cv::line(img, P(-a, a), P(-a + l, a), col, 2, aa);    cv::line(img, P(-a, a), P(-a, a - l), col, 2, aa);
            cv::line(img, P(a, a), P(a - l, a), col, 2, aa);      cv::line(img, P(a, a), P(a, a - l), col, 2, aa);
            break;
        }
        case GI_PLAY: {
            cv::Point tri[3] = { P(-5, -7), P(-5, 7), P(7, 0) };
            cv::fillConvexPoly(img, tri, 3, col, aa);
            break;
        }
        case GI_STOP:
            cv::rectangle(img, P(-6, -6), P(6, 6), col, cv::FILLED, aa);
            break;
        case GI_LOCK:
            cv::rectangle(img, P(-7, -1), P(7, 8), col, cv::FILLED, aa);        // корпус замка
            cv::ellipse(img, P(0, -1), cv::Size(5, 6), 0, 180, 360, col, 2, aa);   // дужка
            break;
        case GI_ARROW_LEFT:
            cv::line(img, P(7, 0), P(-7, 0), col, 2, aa);
            cv::line(img, P(-7, 0), P(-1, -6), col, 2, aa);
            cv::line(img, P(-7, 0), P(-1, 6), col, 2, aa);
            break;
        case GI_REC:
            cv::circle(img, c, 6, col, cv::FILLED, aa);
            break;
        case GI_TRAIN:                                                          // растущие столбики
            cv::rectangle(img, P(-7, 1), P(-3, 7), col, cv::FILLED, aa);
            cv::rectangle(img, P(-2, -3), P(2, 7), col, cv::FILLED, aa);
            cv::rectangle(img, P(3, -7), P(7, 7), col, cv::FILLED, aa);
            break;
        default: break;
        }
    }

    Gui::Gui(const std::string& winName) : win_(winName) {
        layout();
    }

    // === РАСПОЛОЖЕНИЕ КНОПОК ===
    void Gui::layout() {
        const int x0 = PANEL_X + 12, w = PANEL_W - 24;       // 276 пикселей
        btns_.clear();
        auto add = [&](int id, int x, int y, int bw, int bh, bool admin) {
            btns_.push_back({ id, cv::Rect(x, y, bw, bh), admin });
            };

        // --- основной вид ---
        const int tw = (w - 12) / 3;                         // три плитки источников в ряд
        add(ID_SRC_CAM0, x0, 42, tw, 58, false);
        add(ID_SRC_FILE, x0 + (tw + 6), 42, tw, 58, false);
        add(ID_SRC_SCREEN, x0 + 2 * (tw + 6), 42, tw, 58, false);
        add(ID_START, x0, 134, 134, 46, false);
        add(ID_STOP, x0 + 142, 134, 134, 46, false);
        add(ID_BACK, x0, 228, 86, 36, false);
        add(ID_CLEAR, x0 + 95, 228, 86, 36, false);
        add(ID_SPEAK, x0 + 190, 228, 86, 36, false);
        add(ID_ADMIN, x0, H - 54, w, 42, false);

        // --- раздел для администраторов ---
        add(ID_ADMIN_BACK, x0, 12, w, 34, true);

        // сетка букв: 7 столбцов x 5 строк
        const int cw = 39, ch = 36, gy = 82;
        for (std::size_t i = 0; i < ALPHABET_SIZE; ++i)
            add(ID_LETTER_BASE + (int)i, x0 + (int)(i % 7) * cw, gy + (int)(i / 7) * ch, cw - 2, ch - 2, true);

        add(ID_KIND_POSE, x0, 296, 134, 36, true);
        add(ID_KIND_MOVE, x0 + 142, 296, 134, 36, true);
        add(ID_REC, x0, 530, w, 42, true);
        add(ID_TRAIN, x0, 580, w, 42, true);
    }

    cv::Rect Gui::rectOf(int id) const {
        for (const Btn& b : btns_) if (b.id == id) return b.r;
        return cv::Rect();
    }

    int Gui::hit(int x, int y) const {
        for (const Btn& b : btns_) {
            if (b.admin != admin_) continue;                 // кнопки другого вида панели не нажимаются
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
    void Gui::drawButton(int id, const std::wstring& label, bool active, cv::Scalar activeColor,
        bool enabled, GuiIcon icon, bool outline, bool left) {
        cv::Rect r = rectOf(id);
        r.x -= PANEL_X;                                    // панель рисуется в своей картинке
        if (outline) {
            roundRect(panel_, r, 6, C_BORDER);             // рамка в 1 пиксель, внутри цвет фона панели
            roundRect(panel_, cv::Rect(r.x + 1, r.y + 1, r.width - 2, r.height - 2), 5, C_BG);
        }
        else roundRect(panel_, r, 6, !enabled ? C_BTN_OFF : (active ? activeColor : C_BTN));

        const int fh = 16;
        const int iw = icon != GI_NONE ? 18 : 0, gap = icon != GI_NONE ? 8 : 0;
        std::wstring text = fitText(label, r.width - 24 - iw - gap, fh, active);
        int tw = textWidthW(text, fh, active);
        int x = left ? r.x + 12 : r.x + std::max(6, (r.width - (iw + gap + tw)) / 2);   // значок и подпись центрируются вместе
        cv::Scalar tc = enabled ? C_TEXT : C_DIM;
        if (icon != GI_NONE) drawIcon(panel_, icon, { x + iw / 2, r.y + r.height / 2 }, tc);
        putTextW(panel_, text, { x + iw + gap, r.y + (r.height - fh) / 2 - 1 }, fh, tc, active);
    }

    // === ПЛИТКА: ЗНАЧОК СВЕРХУ, ПОДПИСЬ СНИЗУ ===
    void Gui::drawTile(int id, const std::wstring& label, bool active, GuiIcon icon) {
        cv::Rect r = rectOf(id);
        r.x -= PANEL_X;
        roundRect(panel_, r, 6, active ? C_ACCENT : C_BTN);
        drawIcon(panel_, icon, { r.x + r.width / 2, r.y + 18 }, C_TEXT);
        const int fh = 14;
        std::wstring text = fitText(label, r.width - 8, fh, active);
        int tw = textWidthW(text, fh, active);
        putTextW(panel_, text, { r.x + (r.width - tw) / 2, r.y + 34 }, fh, C_TEXT, active);
    }

    // === ОСНОВНОЙ ВИД ПАНЕЛИ ===
    void Gui::drawMainPanel(const GuiState& gs) {
        const int lx = 12;

        putTextW(panel_, L"Источник видео", { lx, 14 }, 17, C_DIM, true);
        std::wstring cam = (gs.source == 1 || gs.source == 2) ? L"Камера " + std::to_wstring(gs.source) : L"Камера";
        drawTile(ID_SRC_CAM0, cam, gs.source <= 2, GI_CAMERA);
        drawTile(ID_SRC_FILE, L"Видеофайл", gs.source == 3, GI_FILM);
        drawTile(ID_SRC_SCREEN, L"Область", gs.source == 4, GI_CROP);

        // что именно выбрано (на плитках места для имени нет)
        std::wstring detail;
        if (gs.source == 3 && !gs.fileLabel.empty()) detail = L"Файл: " + gs.fileLabel;
        else if (gs.source == 4 && !gs.screenLabel.empty()) detail = L"Область: " + gs.screenLabel;
        if (!detail.empty()) putTextW(panel_, fitText(detail, PANEL_W - 2 * lx, 13, false), { lx, 108 }, 13, C_DIM);

        // доступная кнопка яркая, недоступная серая
        drawButton(ID_START, L"Начать", !gs.running, C_GREEN, !gs.running, GI_PLAY);
        drawButton(ID_STOP, L"Закончить", gs.running, C_RED, gs.running, GI_STOP);

        putTextW(panel_, L"Текст", { lx, 204 }, 17, C_DIM, true);
        drawButton(ID_BACK, L"Стереть", false, C_BTN);
        drawButton(ID_CLEAR, L"Очистить", false, C_BTN);
        drawButton(ID_SPEAK, L"Озвучить", false, C_BTN, gs.canSpeak);

        drawButton(ID_ADMIN, L"Для администраторов", false, C_BTN, true, GI_LOCK, true);
    }

    // === ВИД ПАНЕЛИ «ДЛЯ АДМИНИСТРАТОРОВ» ===
    void Gui::drawAdminPanel(const GuiState& gs) {
        const int lx = 12;

        drawButton(ID_ADMIN_BACK, L"Назад", false, C_BTN, true, GI_ARROW_LEFT, true, true);
        putTextW(panel_, L"Буква", { lx, 58 }, 15, C_DIM, true);

        // --- буквы ---
        for (std::size_t i = 0; i < ALPHABET_SIZE; ++i) {
            int id = ID_LETTER_BASE + (int)i;
            bool pose = (gs.poseMask >> i) & 1, move = (gs.moveMask >> i) & 1;
            bool sel = (int)i == gs.letter;
            cv::Rect r = rectOf(id);
            r.x -= PANEL_X;
            if (sel) {
                roundRect(panel_, r, 6, cv::Scalar(255, 255, 255));            // белая рамка выбранной буквы
                r = cv::Rect(r.x + 2, r.y + 2, r.width - 4, r.height - 4);
            }
            if (pose && move) splitRoundRect(panel_, r, 5, C_POSE, C_MOVE);    // есть и позы, и движения: диагональ
            else roundRect(panel_, r, 5, pose ? C_POSE : (move ? C_MOVE : C_BTN));
            std::wstring s(1, ALPHABET[i].letter);
            int tw = textWidthW(s, 16, sel);
            putTextW(panel_, s, { r.x + (r.width - tw) / 2, r.y + (r.height - 16) / 2 - 1 }, 16, C_TEXT, sel);
        }

        // --- легенда цветов букв ---
        {
            const int ly = 270;
            int x = lx;
            auto item = [&](const cv::Scalar& c, const std::wstring& t) {
                roundRect(panel_, cv::Rect(x, ly + 2, 11, 11), 3, c);
                putTextW(panel_, t, { x + 16, ly }, 13, C_DIM);
                x += 16 + textWidthW(t, 13) + 14;
                };
            item(C_POSE, L"поза");
            item(C_MOVE, L"движение");
        }

        // --- тип образца ---
        drawButton(ID_KIND_POSE, L"Поза", !gs.kindMove, C_ACCENT);
        drawButton(ID_KIND_MOVE, L"Движение", gs.kindMove, C_ACCENT);

        std::wstring sel = std::wstring(1, ALPHABET[gs.letter].letter) + L": поз " + std::to_wstring(gs.letterPose) +
            L", движений " + std::to_wstring(gs.letterMove);
        putTextW(panel_, sel, { lx, 344 }, 16, C_TEXT);

        // --- запись и обучение ---
        std::wstring rec = gs.recording ? L"Остановить запись (" + std::to_wstring(gs.recCount) + L")" : L"Запись образцов";
        drawButton(ID_REC, rec, gs.recording, C_RED, true, gs.recording ? GI_STOP : GI_REC);
        drawButton(ID_TRAIN, L"Обучить модель", false, C_BTN, true, GI_TRAIN);
        putTextW(panel_, L"Образцы: gestures.csv, gestures_dynamic.csv", { lx, 632 }, 13, C_DIM);
    }

    void Gui::drawPanel(const GuiState& gs) {
        panel_ = cv::Mat(H, PANEL_W, CV_8UC3, C_BG);
        cv::line(panel_, { 0, 0 }, { 0, H - 1 }, C_LINE, 1);       // граница между видео и панелью
        if (gs.admin) drawAdminPanel(gs);
        else drawMainPanel(gs);
    }

    // === ПОЛНАЯ ОТРИСОВКА КАДРА ===
    void Gui::render(const GuiState& gs, const InfoState& is, const cv::Mat& video) {
        enabledStart_ = !gs.running;
        enabledStop_ = gs.running;
        enabledSpeak_ = gs.canSpeak;
        admin_ = gs.admin;

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

        // --- текст под видео ---
        cv::rectangle(canvas_, cv::Rect(0, VIDEO_H, VIDEO_W, H - VIDEO_H), C_INFO, cv::FILLED);
        cv::line(canvas_, { 0, VIDEO_H }, { VIDEO_W - 1, VIDEO_H }, C_LINE, 1);
        if (is.lines.empty())
            putTextW(canvas_, L"Распознанный текст появится здесь", { 14, VIDEO_H + 16 }, 18, cv::Scalar(110, 110, 110));
        for (std::size_t i = 0; i < is.lines.size() && i < 3; ++i)
            putTextW(canvas_, is.lines[i], { 12, VIDEO_H + 6 + (int)i * 36 }, 28, cv::Scalar(255, 255, 255), true);

        // --- полоса состояния и сообщений внизу ---
        const int sy = H - 54;
        cv::rectangle(canvas_, cv::Rect(0, sy, VIDEO_W, H - sy), C_STRIP, cv::FILLED);
        cv::line(canvas_, { 0, sy }, { VIDEO_W - 1, sy }, C_LINE, 1);
        cv::Scalar dot = gs.recording ? C_RED : (gs.running ? C_GREEN : cv::Scalar(120, 120, 120));
        cv::circle(canvas_, { 20, H - 40 }, 5, dot, cv::FILLED, cv::LINE_AA);       // точка: идёт ли работа
        putTextW(canvas_, is.status, { 34, H - 48 }, 16, C_DIM);
        putTextW(canvas_, is.message, { 34, H - 26 }, 16, cv::Scalar(0, 210, 255));

        cv::imshow(win_, canvas_);
    }

} // namespace HandTracker
