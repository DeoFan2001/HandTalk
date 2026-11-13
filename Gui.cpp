#include "Gui.h"
#include "Alphabet.h"
#include "HandArt.h"
#include "TextRender.h"
#include <algorithm>
#include <map>

namespace HandTracker {

    // === ЦВЕТА (BGR; rgb() позволяет писать привычный порядок R, G, B) ===
    static cv::Scalar rgb(int r, int g, int b) { return cv::Scalar(b, g, r); }

    static const cv::Scalar C_BG = rgb(11, 13, 18);
    static const cv::Scalar C_CARD = rgb(30, 34, 45);
    static const cv::Scalar C_EDGE = rgb(45, 52, 68);
    static const cv::Scalar C_VIDEO = rgb(8, 9, 13);
    static const cv::Scalar C_STRIP = rgb(15, 17, 23);
    static const cv::Scalar C_BTN = rgb(32, 37, 49);
    static const cv::Scalar C_BTN_HOT = rgb(43, 49, 65);
    static const cv::Scalar C_BTN_OFF = rgb(24, 27, 35);
    static const cv::Scalar C_BTN_OFF_EDGE = rgb(31, 35, 45);
    static const cv::Scalar C_TEXT = rgb(242, 244, 248);
    static const cv::Scalar C_DIM = rgb(164, 173, 196);
    static const cv::Scalar C_DIMMER = rgb(104, 112, 134);
    static const cv::Scalar C_LETTER = rgb(142, 147, 170);
    static const cv::Scalar C_BLUE = rgb(10, 110, 255);
    static const cv::Scalar C_GREEN = rgb(46, 204, 113);
    static const cv::Scalar C_RED = rgb(240, 82, 79);
    static const cv::Scalar C_AMBER = rgb(255, 196, 64);
    static const cv::Scalar C_POSE = rgb(30, 120, 230);
    static const cv::Scalar C_MOVE = rgb(235, 140, 30);

    // === ЗНАЧКИ ===
    enum Icon {
        IC_NONE = 0, IC_ARROW_L, IC_ARROW_R, IC_CHEVRON, IC_CAMERA, IC_MONITOR, IC_FILE, IC_PLAY, IC_STOP,
        IC_SPEAKER, IC_PENCIL, IC_SEND, IC_CHAT, IC_BOOK, IC_GEAR, IC_CHECK, IC_CROSS,
        IC_REFRESH, IC_REC, IC_TRAIN, IC_HAND
    };

    static int lineH(int heightPx) { return heightPx * 4 / 3; }

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

    static void roundBorder(cv::Mat& img, cv::Rect r, int rad, const cv::Scalar& fill, const cv::Scalar& edge) {
        roundRect(img, r, rad, edge);
        roundRect(img, cv::Rect(r.x + 1, r.y + 1, r.width - 2, r.height - 2), rad - 1, fill);
    }

    static void gradRoundRect(cv::Mat& img, cv::Rect r, int rad, const cv::Scalar& top, const cv::Scalar& bot) {
        if (r.width <= 0 || r.height <= 0) return;
        cv::Mat g(r.height, r.width, CV_8UC3);
        for (int y = 0; y < r.height; ++y) {
            double t = (double)y / std::max(1, r.height - 1);
            g.row(y).setTo(cv::Scalar(top[0] + (bot[0] - top[0]) * t, top[1] + (bot[1] - top[1]) * t,
                top[2] + (bot[2] - top[2]) * t));
        }
        cv::Mat mask(r.height, r.width, CV_8UC1, cv::Scalar(0));
        roundRect(mask, cv::Rect(0, 0, r.width, r.height), rad, cv::Scalar(255));
        g.copyTo(img(r), mask);
    }

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

    static void blitRounded(cv::Mat& dst, const cv::Mat& src, cv::Point pos, int rad) {
        cv::Rect r(pos.x, pos.y, src.cols, src.rows);
        if (r.x < 0 || r.y < 0 || r.x + r.width > dst.cols || r.y + r.height > dst.rows) return;
        cv::Mat mask(src.rows, src.cols, CV_8UC1, cv::Scalar(0));
        roundRect(mask, cv::Rect(0, 0, src.cols, src.rows), rad, cv::Scalar(255));
        src.copyTo(dst(r), mask);
    }

    static std::wstring fitText(std::wstring s, int maxW, int heightPx, bool bold) {
        if (textWidthW(s, heightPx, bold) <= maxW) return s;
        while (s.size() > 1 && textWidthW(s + L"...", heightPx, bold) > maxW) s.pop_back();
        return s + L"...";
    }

    static std::wstring fitTail(std::wstring s, int maxW, int heightPx, bool bold) {
        if (s.size() > 90) s = s.substr(s.size() - 90);
        if (textWidthW(s, heightPx, bold) <= maxW) return s;
        while (s.size() > 1 && textWidthW(L"..." + s, heightPx, bold) > maxW) s.erase(0, 1);
        return L"..." + s;
    }

    static void centerText(cv::Mat& img, const std::wstring& s, cv::Rect r, int h, const cv::Scalar& c, bool bold = false) {
        int w = textWidthW(s, h, bold);
        putTextW(img, s, { r.x + (r.width - w) / 2, r.y + (r.height - lineH(h)) / 2 }, h, c, bold);
    }

    static void centerTextX(cv::Mat& img, const std::wstring& s, int cx, int y, int h, const cv::Scalar& c, bool bold = false) {
        putTextW(img, s, { cx - textWidthW(s, h, bold) / 2, y }, h, c, bold);
    }

    static void leftText(cv::Mat& img, const std::wstring& s, int x, int cy, int h, const cv::Scalar& c, bool bold = false) {
        putTextW(img, s, { x, cy - lineH(h) / 2 }, h, c, bold);
    }

    // === ЗАГРУЗКА PNG С ПРОЗРАЧНОСТЬЮ (С ВЫРЕЗАНИЕМ БЕЛОГО ФОНА) ===
    static void drawImageWithAlpha(cv::Mat& dst, const cv::Mat& img, cv::Point pos, int size) {
        if (img.empty()) return;

        cv::Mat resized;
        cv::resize(img, resized, cv::Size(size, size), 0, 0, cv::INTER_AREA);

        cv::Mat gray, mask;
        cv::cvtColor(resized, gray, cv::COLOR_BGR2GRAY);
        cv::threshold(gray, mask, 240, 255, cv::THRESH_BINARY_INV);

        cv::Rect roi(pos.x, pos.y, size, size);
        roi &= cv::Rect(0, 0, dst.cols, dst.rows);
        if (roi.width <= 0 || roi.height <= 0) return;

        cv::Mat dstROI = dst(roi);
        cv::Mat srcROI = resized(cv::Rect(0, 0, roi.width, roi.height));
        cv::Mat maskROI = mask(cv::Rect(0, 0, roi.width, roi.height));

        srcROI.copyTo(dstROI, maskROI);
    }

    // === КЭШ ИКОНОК (для вставки PNG) ===
    static std::map<int, cv::Mat> g_iconCache;

    static void initIcons() {
        if (!g_iconCache.empty()) return;

        g_iconCache[IC_CHAT] = cv::imread("assets/ic_chat.png");
        g_iconCache[IC_BOOK] = cv::imread("assets/ic_book.png");
        g_iconCache[IC_GEAR] = cv::imread("assets/ic_gear.png");
        g_iconCache[IC_CAMERA] = cv::imread("assets/ic_camera.png");
        g_iconCache[IC_MONITOR] = cv::imread("assets/ic_monitor.png");
        g_iconCache[IC_FILE] = cv::imread("assets/ic_file.png");
        g_iconCache[IC_PLAY] = cv::imread("assets/ic_play.png");
        g_iconCache[IC_STOP] = cv::imread("assets/ic_stop.png");
        g_iconCache[IC_SPEAKER] = cv::imread("assets/ic_speaker.png");
        g_iconCache[IC_SEND] = cv::imread("assets/ic_send.png");
        g_iconCache[IC_ARROW_L] = cv::imread("assets/ic_arrow_l.png");
        g_iconCache[IC_ARROW_R] = cv::imread("assets/ic_arrow_r.png");
        g_iconCache[IC_REFRESH] = cv::imread("assets/ic_refresh.png");
        g_iconCache[IC_PENCIL] = cv::imread("assets/ic_pencil.png");
        g_iconCache[IC_CHECK] = cv::imread("assets/ic_check.png");
        g_iconCache[IC_CROSS] = cv::imread("assets/ic_cross.png");
        g_iconCache[IC_HAND] = cv::imread("assets/ic_hand.png");
    }

    static void drawIcon(cv::Mat& img, int ic, cv::Point c, int size, const cv::Scalar& col, const cv::Scalar& bg) {
        auto it = g_iconCache.find(ic);
        if (it != g_iconCache.end() && !it->second.empty()) {
            cv::Point pos(c.x - size / 2, c.y - size / 2);
            drawImageWithAlpha(img, it->second, pos, size);
            return;
        }

        const double k = size / 24.0;
        const int aa = cv::LINE_AA;
        const int th = std::max(1, cvRound(2.0 * k));
        auto P = [&](double x, double y) { return cv::Point(c.x + cvRound(x * k), c.y + cvRound(y * k)); };
        switch (ic) {
        case IC_ARROW_L:
            cv::line(img, P(9, 0), P(-9, 0), col, th, aa);
            cv::line(img, P(-9, 0), P(-2, -7), col, th, aa);
            cv::line(img, P(-9, 0), P(-2, 7), col, th, aa);
            break;
        case IC_ARROW_R:
            cv::line(img, P(-9, 0), P(9, 0), col, th, aa);
            cv::line(img, P(9, 0), P(2, -7), col, th, aa);
            cv::line(img, P(9, 0), P(2, 7), col, th, aa);
            break;
        case IC_CHEVRON:
            cv::line(img, P(-3, -8), P(4, 0), col, th, aa);
            cv::line(img, P(4, 0), P(-3, 8), col, th, aa);
            break;
        case IC_CAMERA: {
            roundRect(img, cv::Rect(P(-11, -6), P(3, 6)), cvRound(3 * k), col);
            roundRect(img, cv::Rect(P(-9, -4), P(1, 4)), cvRound(2 * k), bg);
            cv::Point tri[4] = { P(5, -2), P(11, -6), P(11, 6), P(5, 2) };
            cv::fillConvexPoly(img, tri, 4, col, aa);
            break;
        }
        case IC_MONITOR:
            roundRect(img, cv::Rect(P(-11, -9), P(11, 5)), cvRound(2 * k), col);
            roundRect(img, cv::Rect(P(-9, -7), P(9, 3)), cvRound(1 * k), bg);
            cv::line(img, P(0, 5), P(0, 9), col, th, aa);
            cv::line(img, P(-5, 9), P(5, 9), col, th, aa);
            break;
        case IC_FILE: {
            std::vector<cv::Point> doc = { P(-7, -10), P(2, -10), P(8, -4), P(8, 10), P(-7, 10) };
            cv::polylines(img, doc, true, col, th, aa);
            cv::line(img, P(2, -10), P(2, -4), col, th, aa);
            cv::line(img, P(2, -4), P(8, -4), col, th, aa);
            cv::Point tri[3] = { P(-2, 0), P(-2, 7), P(4, 3.5) };
            cv::fillConvexPoly(img, tri, 3, col, aa);
            break;
        }
        case IC_PLAY: {
            cv::Point tri[3] = { P(-5, -8), P(-5, 8), P(8, 0) };
            cv::fillConvexPoly(img, tri, 3, col, aa);
            break;
        }
        case IC_STOP:
            roundRect(img, cv::Rect(P(-6, -6), P(6, 6)), cvRound(1.5 * k), col);
            break;
        case IC_SPEAKER: {
            std::vector<cv::Point> body = { P(-10, -3.5), P(-6, -3.5), P(-1, -8), P(-1, 8), P(-6, 3.5), P(-10, 3.5) };
            cv::fillPoly(img, std::vector<std::vector<cv::Point>>{ body }, col, aa);
            cv::ellipse(img, P(0, 0), cv::Size(cvRound(5 * k), cvRound(5 * k)), 0, -50, 50, col, th, aa);
            cv::ellipse(img, P(0, 0), cv::Size(cvRound(9 * k), cvRound(9 * k)), 0, -50, 50, col, th, aa);
            break;
        }
        case IC_PENCIL: {
            cv::line(img, P(-2, 2), P(8, -8), col, std::max(2, cvRound(6 * k)), aa);
            std::vector<cv::Point> tip = { P(-10, 10), P(-8, 3), P(-3, 8) };
            cv::fillPoly(img, std::vector<std::vector<cv::Point>>{ tip }, col, aa);
            break;
        }
        case IC_SEND: {
            std::vector<cv::Point> plane = { P(-11, -8), P(11, 0), P(-11, 8), P(-7, 0) };
            cv::fillPoly(img, std::vector<std::vector<cv::Point>>{ plane }, col, aa);
            cv::line(img, P(-7, 0), P(3, 0), bg, th, aa);
            break;
        }
        case IC_CHAT: {
            roundRect(img, cv::Rect(P(-11, -9), P(11, 5)), cvRound(4 * k), col);
            cv::Point tail[3] = { P(-7, 3), P(-9, 11), P(0, 4) };
            cv::fillConvexPoly(img, tail, 3, col, aa);
            cv::line(img, P(-6, -4.5), P(6, -4.5), bg, th, aa);
            cv::line(img, P(-6, -0.5), P(2, -0.5), bg, th, aa);
            break;
        }
        case IC_BOOK: {
            std::vector<cv::Point> l = { P(-11, -8), P(-1, -5), P(-1, 9), P(-11, 6) };
            std::vector<cv::Point> r = { P(11, -8), P(1, -5), P(1, 9), P(11, 6) };
            cv::polylines(img, l, true, col, th, aa);
            cv::polylines(img, r, true, col, th, aa);
            break;
        }
        case IC_GEAR: {
            std::vector<cv::Point> poly;
            for (int t = 0; t < 8; ++t) {
                double a = t * 45.0;
                const double ang[4] = { a - 9, a + 9, a + 22.5 - 11, a + 22.5 + 11 };
                const double rad[4] = { 11.5, 11.5, 8.5, 8.5 };
                for (int j = 0; j < 4; ++j) {
                    double rr = ang[j] * 3.14159265 / 180.0;
                    poly.push_back(P(rad[j] * std::cos(rr), rad[j] * std::sin(rr)));
                }
            }
            cv::fillPoly(img, std::vector<std::vector<cv::Point>>{ poly }, col, aa);
            cv::circle(img, c, cvRound(8.5 * k), col, cv::FILLED, aa);
            cv::circle(img, c, cvRound(3.6 * k), bg, cv::FILLED, aa);
            break;
        }
        case IC_CHECK:
            cv::line(img, P(-7, 0), P(-2, 5), col, std::max(2, cvRound(2.6 * k)), aa);
            cv::line(img, P(-2, 5), P(8, -6), col, std::max(2, cvRound(2.6 * k)), aa);
            break;
        case IC_CROSS:
            cv::line(img, P(-6, -6), P(6, 6), col, std::max(2, cvRound(2.4 * k)), aa);
            cv::line(img, P(-6, 6), P(6, -6), col, std::max(2, cvRound(2.4 * k)), aa);
            break;
        case IC_REFRESH: {
            cv::ellipse(img, c, cv::Size(cvRound(8 * k), cvRound(8 * k)), 0, 30, 320, col, th, aa);
            cv::Point head[3] = { P(8.7, -2.0), P(8.8, -7.4), P(3.4, -2.85) };
            cv::fillConvexPoly(img, head, 3, col, aa);
            break;
        }
        case IC_REC:
            cv::circle(img, c, cvRound(6 * k), col, cv::FILLED, aa);
            break;
        case IC_TRAIN:
            roundRect(img, cv::Rect(P(-9, 1), P(-4, 9)), 1, col);
            roundRect(img, cv::Rect(P(-2.5, -4), P(2.5, 9)), 1, col);
            roundRect(img, cv::Rect(P(4, -9), P(9, 9)), 1, col);
            break;
        case IC_HAND: {
            HandStyle st;
            st.skin = bg; st.edge = col; st.sleeve = col; st.sleeveOn = false;
            drawHandArt(img, cv::Rect(c.x - size / 2, c.y - size / 2, size, size), openHandPose(), st);
            break;
        }
        default: break;
        }
    }

    // === КАРТОЧКИ НАСТРОЕК ===
    // Две колонки по 4 строки. Для пользователей: слева «Распознавание» (0..2), справа «Озвучивание» (3..5).
    // Для разработчиков: 8 настроек подряд, по 4 в колонке.
    static cv::Rect settingCard(int id) {
        int col, row, y0;
        if (id < SET_USER_END) { col = id < 3 ? 0 : 1; row = id < 3 ? id : id - 3; y0 = 124; }
        else { col = (id - SET_USER_END) / 4; row = (id - SET_USER_END) % 4; y0 = 92; }
        return cv::Rect(24 + col * 584, y0 + row * 102, 568, 88);
    }

    // Кнопки справа в карточке: «-» и «+» (у настройки «вкл/выкл» одна кнопка на всю ширину)
    static void settingButtons(int id, cv::Rect& minus, cv::Rect& plus) {
        cv::Rect c = settingCard(id);
        plus = cv::Rect(c.x + c.width - 16 - 52, c.y + 18, 52, 52);
        minus = cv::Rect(plus.x - 8 - 120 - 8 - 52, c.y + 18, 52, 52);
        if (Settings::isToggle(id)) minus = cv::Rect(minus.x, minus.y, plus.x + plus.width - minus.x, 52);
    }

    Gui::Gui(const std::string& winName) : win_(winName) {
        layout();
    }

    // === РАСПОЛОЖЕНИЕ КНОПОК ===
    void Gui::layout() {
        btns_.clear();
        auto add = [&](Screen s, int id, cv::Rect r) { btns_.push_back({ s, id, r }); };

        {
            const int x = 620, w = 556, h = 120, gap = 22;
            const int y0 = (STRIP_Y - (3 * h + 2 * gap)) / 2;
            add(SCR_HOME, ID_GO_TALK, cv::Rect(x, y0, w, h));
            add(SCR_HOME, ID_GO_LEARN, cv::Rect(x, y0 + h + gap, w, h));
            add(SCR_HOME, ID_GO_SETTINGS, cv::Rect(x, y0 + 2 * (h + gap), w, h));
        }

        for (Screen s : { SCR_TALK, SCR_LEARN, SCR_SETTINGS, SCR_DEV, SCR_DEV_PARAMS }) add(s, ID_NAV_BACK, cv::Rect(24, 20, 148, 52));

        auto sourceCard = [&](Screen s, int x, int w) {
            const int tw = (w - 32 - 16) / 3, ty = 92 + 46, th = 76;
            add(s, ID_SRC_CAM0, cv::Rect(x + 16, ty, tw, th));
            add(s, ID_SRC_SCREEN, cv::Rect(x + 16 + (tw + 8), ty, tw, th));
            add(s, ID_SRC_FILE, cv::Rect(x + 16 + 2 * (tw + 8), ty, tw, th));
            add(s, ID_START, cv::Rect(x + 16, 260 + 12, w - 32, 44));
            add(s, ID_STOP, cv::Rect(x + 16, 260 + 66, w - 32, 44));
            };

        {
            sourceCard(SCR_TALK, 828, 348);
            add(SCR_TALK, ID_ERASE, cv::Rect(732, 580, 72, 64));
            add(SCR_TALK, ID_CLEAR, cv::Rect(24 + 696 - 55, 590, 44, 44));
            add(SCR_TALK, ID_SPEAK_TEXT, cv::Rect(24 + 696 - 55 - 52, 590, 44, 44));
            add(SCR_TALK, ID_ANIM, cv::Rect(844, 394 + 44, 316, 76));

            add(SCR_TALK, ID_REPLY_BOX, cv::Rect(844, 394 + 130, 316, 56));
            add(SCR_TALK, ID_SEND, cv::Rect(844, 394 + 196, 316, 52));
        }

        add(SCR_LEARN, ID_SHOW_GESTURE, cv::Rect(24, 576, 632, 64));
        add(SCR_LEARN, ID_RETRY, cv::Rect(704, 552, 218, 64));
        add(SCR_LEARN, ID_NEXT, cv::Rect(934, 552, 218, 64));

        // --- «Для разработчиков», вкладка «Образцы жестов» (то, что раньше было в «Настройках») ---
        {
            sourceCard(SCR_DEV, 688, 488);
            const int gx = 24 + 17, gy = 464 + 46;
            for (std::size_t i = 0; i < ALPHABET_SIZE; ++i)
                add(SCR_DEV, ID_LETTER_BASE + (int)i, cv::Rect(gx + (int)(i % 11) * 55, gy + (int)(i / 11) * 45, 51, 41));
            add(SCR_DEV, ID_KIND_POSE, cv::Rect(704, 394 + 80, 222, 44));
            add(SCR_DEV, ID_KIND_MOVE, cv::Rect(938, 394 + 80, 222, 44));
            add(SCR_DEV, ID_REC, cv::Rect(704, 394 + 134, 456, 52));
            add(SCR_DEV, ID_TRAIN, cv::Rect(704, 394 + 194, 456, 52));
        }

        // --- вкладки раздела для разработчиков ---
        for (Screen s : { SCR_DEV, SCR_DEV_PARAMS }) {
            add(s, ID_TAB_SAMPLES, cv::Rect(700, 22, 232, 48));
            add(s, ID_TAB_PARAMS, cv::Rect(944, 22, 232, 48));
        }

        // --- настройки: кнопки «-» и «+» в карточках ---
        auto settingBtns = [&](Screen s, int id) {
            cv::Rect minus, plus;
            settingButtons(id, minus, plus);
            add(s, ID_SET_BASE + 2 * id, minus);
            if (!Settings::isToggle(id)) add(s, ID_SET_BASE + 2 * id + 1, plus);
            };
        for (int id = 0; id < SET_USER_END; ++id) settingBtns(SCR_SETTINGS, id);
        for (int id = SET_USER_END; id < SET_DEV_END; ++id) settingBtns(SCR_DEV_PARAMS, id);

        add(SCR_SETTINGS, ID_GO_DEV, cv::Rect(24, 596, 360, 56));
        add(SCR_SETTINGS, ID_SET_RESET, cv::Rect(816, 596, 360, 56));
        add(SCR_DEV_PARAMS, ID_SET_RESET, cv::Rect(880, 616, 280, 48));
    }

    cv::Rect Gui::rectOf(int id) const {
        for (const Btn& b : btns_) if (b.screen == screen_ && b.id == id) return b.r;
        return cv::Rect();
    }

    cv::Rect Gui::videoRect() const {
        switch (screen_) {
        case SCR_TALK: return cv::Rect(24, 92, 780, 468);
        case SCR_LEARN: return cv::Rect(24, 92, 632, 468);
        case SCR_DEV: return cv::Rect(24, 92, 640, 360);
        default: return cv::Rect();
        }
    }

    // === ВЫПАДАЮЩИЙ СПИСОК КАМЕР ===
    // Появляется под плиткой «Камера». Строки: найденные камеры, затем «Обновить список».
    // Если камер нет - первая строка «Камеры не найдены» (не нажимается).
    cv::Rect Gui::camPanel() const {
        const bool talk = screen_ == SCR_TALK;
        const int x = talk ? 828 : 688, w = talk ? 348 : 488;
        const int rows = std::max(1, camCount_) + 1;
        return cv::Rect(x + 16, 218, w - 32, 8 + rows * 38);
    }

    cv::Rect Gui::camRow(int k) const {
        cv::Rect p = camPanel();
        return cv::Rect(p.x + 6, p.y + 6 + k * 38, p.width - 12, 34);
    }

    int Gui::hit(int x, int y) const {
        if (camMenuOpen_ && (screen_ == SCR_TALK || screen_ == SCR_DEV)) {
            const int n = camCount_, rows = std::max(1, n) + 1;
            for (int k = 0; k < rows; ++k) {
                if (!camRow(k).contains({ x, y })) continue;
                if (k == rows - 1) return ID_CAM_REFRESH;
                return k < n ? ID_CAM_BASE + k : ID_NONE;
            }
            if (camPanel().contains({ x, y })) return ID_NONE;      // щель между строками
        }
        for (const Btn& b : btns_) {
            if (b.screen != screen_) continue;
            if (!b.r.contains({ x, y })) continue;
            if (b.id == ID_START && !enabledStart_) return ID_NONE;
            if (b.id == ID_STOP && !enabledStop_) return ID_NONE;
            if (b.id == ID_SEND && !enabledSpeak_) return ID_NONE;
            if (b.id == ID_SPEAK_TEXT && !enabledSpeakText_) return ID_NONE;
            return b.id;
        }
        return ID_NONE;
    }

    void Gui::onMouse(int event, int x, int y, int, void* self) {
        Gui* g = static_cast<Gui*>(self);
        g->mouseX_ = x;
        g->mouseY_ = y;
        if (event != cv::EVENT_LBUTTONDOWN) return;
        g->clicked_ = true;
        g->clickX_ = x;
        g->clickY_ = y;
    }

    void Gui::init() {
        cv::namedWindow(win_, cv::WINDOW_AUTOSIZE);
        cv::setMouseCallback(win_, &Gui::onMouse, this);
        canvas_ = cv::Mat(H, W, CV_8UC3, C_BG);
        base_ = cv::Mat(H, W, CV_8UC3, C_BG);

        initIcons();
    }

    int Gui::pollClick() {
        if (!clicked_) return ID_NONE;
        clicked_ = false;
        return hit(clickX_, clickY_);
    }

    void Gui::button(int id, const std::wstring& label, int style, bool enabled, int icon, int fontPx) {
        cv::Rect r = rectOf(id);
        const bool hot = enabled && hover_ == id;
        const int rad = 12;
        cv::Scalar bg = C_BTN;
        if (!enabled) {
            roundBorder(base_, r, rad, C_BTN_OFF, C_BTN_OFF_EDGE);
            bg = C_BTN_OFF;
        }
        else if (style == 1) {
            gradRoundRect(base_, r, rad, hot ? rgb(74, 152, 255) : rgb(44, 132, 255), hot ? rgb(24, 116, 250) : rgb(0, 96, 240));
            bg = C_BLUE;
        }
        else if (style == 2) {
            gradRoundRect(base_, r, rad, hot ? rgb(250, 120, 116) : rgb(245, 98, 95), hot ? rgb(226, 78, 76) : rgb(212, 60, 58));
            bg = C_RED;
        }
        else if (style == 3) {
            gradRoundRect(base_, r, rad, hot ? rgb(84, 224, 142) : rgb(60, 214, 124), hot ? rgb(46, 190, 104) : rgb(34, 176, 92));
            bg = C_GREEN;
        }
        else {
            bg = hot ? C_BTN_HOT : C_BTN;
            roundBorder(base_, r, rad, bg, C_EDGE);
        }

        cv::Scalar tc = enabled ? C_TEXT : C_DIMMER;
        const int isz = fontPx + 6;
        const int iw = icon != IC_NONE ? isz : 0;
        const int gap = (icon != IC_NONE && !label.empty()) ? 10 : 0;
        std::wstring text = fitText(label, r.width - 24 - iw - gap, fontPx, true);
        int tw = textWidthW(text, fontPx, true);
        int x = r.x + (r.width - (iw + gap + tw)) / 2;
        int cy = r.y + r.height / 2;
        if (icon != IC_NONE) drawIcon(base_, icon, { x + iw / 2, cy }, isz, tc, bg);
        if (!text.empty()) leftText(base_, text, x + iw + gap, cy, fontPx, tc, true);
    }

    void Gui::tile(int id, const std::wstring& label, bool active, int icon) {
        cv::Rect r = rectOf(id);
        const bool hot = hover_ == id;
        cv::Scalar bg;
        if (active) {
            gradRoundRect(base_, r, 12, hot ? rgb(74, 152, 255) : rgb(44, 132, 255), hot ? rgb(24, 116, 250) : rgb(0, 96, 240));
            bg = C_BLUE;
        }
        else {
            bg = hot ? C_BTN_HOT : C_BTN;
            roundBorder(base_, r, 12, bg, C_EDGE);
        }
        drawIcon(base_, icon, { r.x + r.width / 2, r.y + 27 }, 28, active ? cv::Scalar(255, 255, 255) : C_DIM, bg);
        std::wstring text = fitText(label, r.width - 8, 13, active);
        centerTextX(base_, text, r.x + r.width / 2, r.y + 50, 13, active ? cv::Scalar(255, 255, 255) : C_TEXT, active);
    }

    void Gui::drawHeader(const wchar_t* title, const std::wstring& subtitle, int icon) {
        button(ID_NAV_BACK, L"Назад", 0, true, IC_ARROW_L, 18);
        drawIcon(base_, icon, { 218 + 20, 46 }, 40, C_TEXT, C_BG);
        putTextW(base_, title, { 276, 16 }, 28, C_TEXT, true);
        putTextW(base_, fitText(subtitle, 800, 16, false), { 278, 54 }, 16, C_DIM);
    }

    void Gui::drawSourceCard(const GuiState& gs, cv::Rect card) {
        roundBorder(base_, card, 16, C_CARD, C_EDGE);
        putTextW(base_, L"Источник трансляции", { card.x + 16, card.y + 11 }, 17, C_TEXT, true);

        tile(ID_SRC_CAM0, L"Камера " + std::to_wstring(gs.camera + 1), gs.source == 0, IC_CAMERA);
        {   // маленький треугольник «вниз»: у плитки есть выпадающий список
            cv::Rect cr = rectOf(ID_SRC_CAM0);
            cv::Point tri[3] = { cv::Point(cr.x + cr.width - 20, cr.y + 10), cv::Point(cr.x + cr.width - 10, cr.y + 10),
                cv::Point(cr.x + cr.width - 15, cr.y + 16) };
            cv::fillConvexPoly(base_, tri, 3, gs.source == 0 ? cv::Scalar(255, 255, 255) : C_DIM, cv::LINE_AA);
        }
        tile(ID_SRC_SCREEN, L"Запись экрана", gs.source == 4, IC_MONITOR);
        tile(ID_SRC_FILE, L"Видео файл", gs.source == 3, IC_FILE);

        std::wstring detail;
        if (gs.source == 3 && !gs.fileLabel.empty()) detail = L"Файл: " + gs.fileLabel;
        else if (gs.source == 4 && !gs.screenLabel.empty()) detail = L"Область: " + gs.screenLabel;
        if (!detail.empty()) putTextW(base_, fitText(detail, card.width - 32, 13, false), { card.x + 16, card.y + 130 }, 13, C_DIM);
    }

    void Gui::drawRunCard(const GuiState& gs, cv::Rect card) {
        roundBorder(base_, card, 16, C_CARD, C_EDGE);
        button(ID_START, L"Начать", 1, !gs.running, IC_PLAY, 18);
        button(ID_STOP, L"Закончить", 2, gs.running, IC_STOP, 18);
    }

    void Gui::drawHome() {
        static cv::Mat bg = cv::imread("assets/1111.png");

        if (!bg.empty()) {
            cv::resize(bg, base_, cv::Size(W, H));
        }
        else {
            base_.setTo(C_BG);
        }

        putTextW(base_, L"HandTalk", { 262, 150 }, 70, C_TEXT, true);
        putTextW(base_, L"Общение без барьеров", { 266, 250 }, 26, C_DIM);
        putTextW(base_, L"Распознавайте жесты, изучайте", { 70, 372 }, 26, rgb(190, 196, 222));
        putTextW(base_, L"алфавит и общайтесь", { 70, 408 }, 26, rgb(190, 196, 222));
        putTextW(base_, L"с помощью РЖЯ.", { 70, 444 }, 26, rgb(190, 196, 222));

        struct Item { int id; int icon; const wchar_t* title; const wchar_t* sub; };
        const Item items[3] = {
            { ID_GO_TALK, IC_CHAT, L"Общение", L"Распознавать жесты и писать сообщения" },
            { ID_GO_LEARN, IC_BOOK, L"Изучить алфавит", L"Узнать жесты букв" },
            { ID_GO_SETTINGS, IC_GEAR, L"Настройки", L"Параметры приложения" } };
        for (const Item& it : items) {
            cv::Rect r = rectOf(it.id);
            const bool hot = hover_ == it.id;
            roundBorder(base_, r, 20, hot ? rgb(38, 43, 56) : C_CARD, hot ? rgb(58, 66, 86) : C_EDGE);

            drawIcon(base_, it.icon, { r.x + 66, r.y + r.height / 2 }, 50, C_TEXT, hot ? rgb(30, 35, 47) : C_CARD);

            putTextW(base_, it.title, { r.x + 124, r.y + 24 }, 28, C_TEXT, true);
            putTextW(base_, fitText(it.sub, r.width - 124 - 70, 18, false), { r.x + 124, r.y + 68 }, 18, C_DIM);
            drawIcon(base_, IC_CHEVRON, { r.x + r.width - 40, r.y + r.height / 2 }, 28, C_DIM, C_CARD);
        }
    }

    void Gui::drawTalk(const GuiState& gs, const InfoState& is) {
        drawHeader(L"Общение", L"Общайтесь с помощью текста и жестов", IC_CHAT);

        roundBorder(base_, videoRect(), 16, C_VIDEO, C_EDGE);

        cv::Rect field(24, 580, 696, 64);
        roundBorder(base_, field, 16, C_CARD, C_EDGE);
        const int textW = field.width - 22 - 120;
        if (is.text.empty()) leftText(base_, L"Распознанный текст появится здесь", field.x + 22, field.y + field.height / 2, 20, C_DIMMER);
        else leftText(base_, fitTail(is.text, textW, 24, false), field.x + 22, field.y + field.height / 2, 24, C_TEXT);
        button(ID_CLEAR, L"", 0, true, IC_CROSS, 14);
        button(ID_SPEAK_TEXT, L"", 0, enabledSpeakText_, IC_SPEAKER, 20);
        button(ID_ERASE, L"", 0, true, IC_PENCIL, 20);

        drawSourceCard(gs, cv::Rect(828, 92, 348, 158));
        drawRunCard(gs, cv::Rect(828, 260, 348, 124));

        cv::Rect card(828, 394, 348, 250);
        roundBorder(base_, card, 16, C_CARD, C_EDGE);
        putTextW(base_, L"Варианты ответа", { card.x + 16, card.y + 11 }, 17, C_TEXT, true);

        {
            cv::Rect r = rectOf(ID_ANIM);
            const bool hot = hover_ == ID_ANIM;
            cv::Scalar bg = gs.animOn ? rgb(18, 62, 128) : (hot ? C_BTN_HOT : C_BTN);
            roundBorder(base_, r, 12, bg, gs.animOn ? C_BLUE : C_EDGE);

            drawIcon(base_, IC_HAND, { r.x + 36, r.y + r.height / 2 }, 40, C_TEXT, bg);
            putTextW(base_, gs.animOn ? L"Остановить анимацию" : L"Анимация жестов", { r.x + 72, r.y + 12 }, 17, C_TEXT, true);
            putTextW(base_, L"Показать сообщение", { r.x + 72, r.y + 36 }, 13, C_DIM);
            putTextW(base_, L"побуквенной анимацией", { r.x + 72, r.y + 54 }, 13, C_DIM);
            drawIcon(base_, IC_CHEVRON, { r.x + r.width - 22, r.y + r.height / 2 }, 20, C_DIM, bg);
        }

        cv::Rect phraseBox = rectOf(ID_REPLY_BOX);

        cv::Scalar boxBg = gs.replyActive ? rgb(40, 46, 60) : C_BTN;
        cv::Scalar boxEdge = gs.replyActive ? C_BLUE : C_EDGE;
        roundBorder(base_, phraseBox, 12, boxBg, boxEdge);

        if (gs.replyText.empty()) {
            leftText(base_, L"Напишите ответ...", phraseBox.x + 16, phraseBox.y + phraseBox.height / 2, 18, C_DIMMER);
        }
        else {
            leftText(base_, fitTail(gs.replyText, phraseBox.width - 32, 18, false),
                phraseBox.x + 16, phraseBox.y + phraseBox.height / 2, 18, C_TEXT);

            static int blinkCounter = 0;
            if (gs.replyActive && (blinkCounter++ / 15) % 2 == 0) {
                int tw = textWidthW(fitTail(gs.replyText, phraseBox.width - 32, 18, false), 18, false);
                cv::line(base_,
                    { phraseBox.x + 16 + tw + 2, phraseBox.y + 12 },
                    { phraseBox.x + 16 + tw + 2, phraseBox.y + phraseBox.height - 12 },
                    C_TEXT, 2);
            }
        }

        button(ID_SEND, L"Озвучить", 1, enabledSpeak_, IC_SPEAKER, 18);
    }

    void Gui::drawLearn(const GuiState& gs) {
        std::wstring letter(1, ALPHABET[gs.learnTarget].letter);
        drawHeader(L"Изучить алфавит",
            L"Покажите букву «" + letter + L"»  ·  " + std::to_wstring(gs.learnTarget + 1) + L" из " + std::to_wstring(gs.learnTotal),
            IC_BOOK);

        roundBorder(base_, videoRect(), 16, C_VIDEO, C_EDGE);
        button(ID_SHOW_GESTURE, gs.showRef ? L"Показать камеру" : L"Показать жест", 0, true, gs.showRef ? IC_CAMERA : IC_PLAY, 20);

        cv::Rect card(680, 92, 496, 548);
        roundBorder(base_, card, 20, C_CARD, C_EDGE);
        const int cx = card.x + card.width / 2;
        centerTextX(base_, L"Ваш жест:", cx, card.y + 26, 28, C_TEXT, true);

        std::wstring big = gs.learnShown ? std::wstring(1, gs.learnShown) : std::wstring(L"?");
        centerTextX(base_, big, cx, card.y + 82, 150, gs.learnShown ? C_LETTER : rgb(56, 62, 80), true);

        const int vy = card.y + 318;
        std::wstring verdict, note;
        cv::Scalar vcol = C_DIM;
        if (gs.learnResult == LR_OK) {
            verdict = L"Верно!"; vcol = C_GREEN;
            note = L"Отлично, вы правильно показали букву";
            const int w = textWidthW(verdict, 42, true), gx = cx - (60 + w) / 2;
            cv::circle(base_, { gx + 28, vy + 28 }, 28, C_GREEN, cv::FILLED, cv::LINE_AA);
            drawIcon(base_, IC_CHECK, { gx + 28, vy + 28 }, 34, cv::Scalar(255, 255, 255), C_GREEN);
            putTextW(base_, verdict, { gx + 60, vy + 28 - lineH(42) / 2 }, 42, vcol, true);
        }
        else if (gs.learnResult == LR_WRONG) {
            verdict = L"Не та буква"; vcol = C_RED;
            note = L"Вы показали «" + std::wstring(1, gs.learnShown) + L"», а нужна «" + letter + L"». Попробуйте ещё раз";
            const int w = textWidthW(verdict, 36, true), gx = cx - (60 + w) / 2;
            cv::circle(base_, { gx + 28, vy + 28 }, 28, C_RED, cv::FILLED, cv::LINE_AA);
            drawIcon(base_, IC_CROSS, { gx + 28, vy + 28 }, 30, cv::Scalar(255, 255, 255), C_RED);
            putTextW(base_, verdict, { gx + 60, vy + 28 - lineH(36) / 2 }, 36, vcol, true);
        }
        else {
            verdict = L"Жду жест";
            note = gs.running ? L"Покажите жест перед камерой" : L"Камера выключена. Нажмите «Повторить»";
            const int w = textWidthW(verdict, 34, true), gx = cx - (60 + w) / 2;
            cv::circle(base_, { gx + 28, vy + 28 }, 28, C_BTN, cv::FILLED, cv::LINE_AA);
            drawIcon(base_, IC_HAND, { gx + 28, vy + 28 }, 34, C_DIM, C_BTN);
            putTextW(base_, verdict, { gx + 60, vy + 28 - lineH(34) / 2 }, 34, C_DIM, true);
        }
        std::vector<std::wstring> lines = wrapTextW(note, 21, card.width - 80, 2);
        for (std::size_t i = 0; i < lines.size(); ++i)
            centerTextX(base_, lines[i], cx, vy + 82 + (int)i * 30, 21, C_DIM);

        button(ID_RETRY, L"Повторить", 1, true, IC_REFRESH, 20);
        button(ID_NEXT, L"Далее", 0, true, IC_ARROW_R, 20);
    }

    // === ЭКРАН «НАСТРОЙКИ» (для пользователей) ===
    void Gui::drawSettings(const GuiState& gs) {
        drawHeader(L"Настройки", L"Параметры приложения (сохраняются в файле handtalk.ini)", IC_GEAR);

        putTextW(base_, L"Распознавание", { 24, 94 }, 17, C_DIM, true);
        putTextW(base_, L"Озвучивание", { 608, 94 }, 17, C_DIM, true);
        for (int id = 0; id < SET_USER_END; ++id) drawSettingRow(gs, id);

        putTextW(base_, L"Изменения сохраняются сразу. Часть настроек заработает после перезапуска программы.",
            { 28, 440 }, 14, C_DIMMER);
        putTextW(base_, L"Камеру выбирают в разделе «Общение»: кнопка «Камера» открывает список подключённых камер.",
            { 28, 466 }, 14, C_DIMMER);

        button(ID_GO_DEV, L"Для разработчиков", 0, true, IC_GEAR, 18);
        button(ID_SET_RESET, L"Сбросить настройки", 0, true, IC_REFRESH, 18);
    }

    // Одна настройка: карточка с названием, пояснением и кнопками «-» / «+» (или одной кнопкой «вкл/выкл»)
    void Gui::drawSettingRow(const GuiState& gs, int id) {
        cv::Rect c = settingCard(id);
        roundBorder(base_, c, 16, C_CARD, C_EDGE);
        cv::Rect minus, plus;
        settingButtons(id, minus, plus);
        const std::wstring val = id < (int)gs.setVals.size() ? gs.setVals[id] : std::wstring();

        const int textW = minus.x - 12 - (c.x + 18);                  // слева от кнопок
        putTextW(base_, fitText(Settings::title(id), textW, 17, true), { c.x + 18, c.y + 14 }, 17, C_TEXT, true);
        std::vector<std::wstring> lines = wrapTextW(Settings::hint(id), 13, textW, 2);
        for (std::size_t i = 0; i < lines.size(); ++i)
            putTextW(base_, lines[i], { c.x + 18, c.y + 42 + (int)i * 18 }, 13, C_DIM);

        if (Settings::isToggle(id)) {
            button(ID_SET_BASE + 2 * id, val, val == SET_ON_TEXT ? 3 : 0, true, IC_NONE, 18);
        }
        else {
            button(ID_SET_BASE + 2 * id, L"\u2013", 0, true, IC_NONE, 26);
            button(ID_SET_BASE + 2 * id + 1, L"+", 0, true, IC_NONE, 26);
            cv::Rect vr(minus.x + minus.width + 8, minus.y, 120, 52);
            centerText(base_, fitText(val, vr.width - 4, 20, true), vr, 20, C_TEXT, true);
        }
    }

    // Вкладки раздела «Для разработчиков»
    void Gui::drawDevTabs(bool samples) {
        button(ID_TAB_SAMPLES, L"Образцы жестов", samples ? 1 : 0, true, IC_NONE, 17);
        button(ID_TAB_PARAMS, L"Параметры", samples ? 0 : 1, true, IC_NONE, 17);
    }

    // === «ДЛЯ РАЗРАБОТЧИКОВ» - ВКЛАДКА «ПАРАМЕТРЫ» ===
    void Gui::drawDevParams(const GuiState& gs) {
        drawHeader(L"Для разработчиков", L"Параметры распознавания", IC_GEAR);
        drawDevTabs(false);
        for (int id = SET_USER_END; id < SET_DEV_END; ++id) drawSettingRow(gs, id);

        cv::Rect fc(24, 504, 1152, 160);
        roundBorder(base_, fc, 16, C_CARD, C_EDGE);
        putTextW(base_, L"Файлы (меняются вручную в handtalk.ini)", { fc.x + 16, fc.y + 11 }, 17, C_TEXT, true);
        for (int k = 0; k < 4; ++k) {
            const int id = SET_F_SAMPLES + k, y = fc.y + 44 + k * 26;
            putTextW(base_, Settings::title(id) + L":", { fc.x + 16, y }, 15, C_DIM);
            const std::wstring val = id < (int)gs.setVals.size() ? gs.setVals[id] : std::wstring();
            putTextW(base_, fitText(val, 560, 15, false), { fc.x + 216, y }, 15, C_TEXT);
        }
        button(ID_SET_RESET, L"Сбросить параметры", 0, true, IC_REFRESH, 17);
    }

    // === «ДЛЯ РАЗРАБОТЧИКОВ» - ВКЛАДКА «ОБРАЗЦЫ ЖЕСТОВ» ===
    void Gui::drawDev(const GuiState& gs) {
        drawHeader(L"Для разработчиков", L"Образцы жестов и обучение модели", IC_GEAR);
        drawDevTabs(true);
        roundBorder(base_, videoRect(), 16, C_VIDEO, C_EDGE);

        cv::Rect lc(24, 464, 640, 200);
        roundBorder(base_, lc, 16, C_CARD, C_EDGE);
        putTextW(base_, L"Буква", { lc.x + 17, lc.y + 12 }, 17, C_TEXT, true);
        {
            int x = lc.x + lc.width - 17;
            auto item = [&](const cv::Scalar& c, const std::wstring& t) {
                x -= textWidthW(t, 13);
                putTextW(base_, t, { x, lc.y + 14 }, 13, C_DIM);
                x -= 16;
                roundRect(base_, cv::Rect(x, lc.y + 18, 11, 11), 3, c);
                x -= 18;
                };
            item(C_MOVE, L"движение");
            item(C_POSE, L"поза");
        }
        for (std::size_t i = 0; i < ALPHABET_SIZE; ++i) {
            int id = ID_LETTER_BASE + (int)i;
            bool pose = (gs.poseMask >> i) & 1, move = (gs.moveMask >> i) & 1;
            bool sel = (int)i == gs.letter;
            cv::Rect r = rectOf(id);
            if (sel) {
                roundRect(base_, r, 9, cv::Scalar(255, 255, 255));
                r = cv::Rect(r.x + 2, r.y + 2, r.width - 4, r.height - 4);
            }
            cv::Scalar fill = pose ? C_POSE : (move ? C_MOVE : (hover_ == id ? C_BTN_HOT : C_BTN));
            if (pose && move) splitRoundRect(base_, r, 7, C_POSE, C_MOVE);
            else roundRect(base_, r, 7, fill);
            std::wstring s(1, ALPHABET[i].letter);
            centerText(base_, s, r, 18, C_TEXT, sel);
        }

        drawSourceCard(gs, cv::Rect(688, 92, 488, 158));
        drawRunCard(gs, cv::Rect(688, 260, 488, 124));

        cv::Rect sc(688, 394, 488, 270);
        roundBorder(base_, sc, 16, C_CARD, C_EDGE);
        putTextW(base_, L"Образцы жестов", { sc.x + 16, sc.y + 11 }, 17, C_TEXT, true);
        std::wstring info = std::wstring(1, ALPHABET[gs.letter].letter) + L":  поз " + std::to_wstring(gs.letterPose) +
            L",  движений " + std::to_wstring(gs.letterMove);
        putTextW(base_, info, { sc.x + 16, sc.y + 44 }, 18, C_TEXT);
        button(ID_KIND_POSE, L"Поза", !gs.kindMove ? 1 : 0, true, IC_NONE, 18);
        button(ID_KIND_MOVE, L"Движение", gs.kindMove ? 1 : 0, true, IC_NONE, 18);
        std::wstring rec = gs.recording ? L"Остановить запись (" + std::to_wstring(gs.recCount) + L")" : L"Запись образцов";
        button(ID_REC, rec, gs.recording ? 2 : 0, true, gs.recording ? IC_STOP : IC_REC, 18);
        button(ID_TRAIN, L"Обучить модель", 0, true, IC_TRAIN, 18);
        putTextW(base_, L"Образцы: gestures.csv, gestures_dynamic.csv", { sc.x + 16, sc.y + 248 }, 13, C_DIMMER);
    }

    void Gui::drawCamMenu(const GuiState& gs) {
        const int n = (int)gs.cameras.size(), rows = std::max(1, n) + 1;
        roundBorder(base_, camPanel(), 12, C_CARD, C_BLUE);
        for (int k = 0; k < rows; ++k) {
            cv::Rect r = camRow(k);
            const int cy = r.y + r.height / 2;
            if (k == rows - 1) {
                const bool hot = hover_ == ID_CAM_REFRESH;
                roundBorder(base_, r, 8, hot ? C_BTN_HOT : C_BTN, hot ? C_BTN_HOT : C_BTN);
                drawIcon(base_, IC_REFRESH, { r.x + 22, cy }, 20, C_DIM, hot ? C_BTN_HOT : C_BTN);
                leftText(base_, L"Обновить список", r.x + 42, cy, 15, C_DIM);
            }
            else if (n == 0) {
                leftText(base_, L"Камеры не найдены", r.x + 14, cy, 15, C_DIMMER);
            }
            else {
                const bool sel = gs.cameras[k] == gs.camera, hot = hover_ == ID_CAM_BASE + k;
                cv::Scalar fill = sel ? rgb(18, 62, 128) : (hot ? C_BTN_HOT : C_BTN);
                roundBorder(base_, r, 8, fill, sel ? C_BLUE : fill);
                leftText(base_, L"Камера " + std::to_wstring(gs.cameras[k] + 1), r.x + 14, cy, 16, C_TEXT, sel);
                if (sel) drawIcon(base_, IC_CHECK, { r.x + r.width - 24, cy }, 20, cv::Scalar(255, 255, 255), fill);
            }
        }
    }

    void Gui::drawBase(const GuiState& gs, const InfoState& is) {
        base_.setTo(C_BG);
        switch (gs.screen) {
        case SCR_HOME: drawHome(); break;
        case SCR_TALK: drawTalk(gs, is); break;
        case SCR_LEARN: drawLearn(gs); break;
        case SCR_SETTINGS: drawSettings(gs); break;
        case SCR_DEV: drawDev(gs); break;
        case SCR_DEV_PARAMS: drawDevParams(gs); break;
        }
        if (gs.camMenuOpen && (gs.screen == SCR_TALK || gs.screen == SCR_DEV)) drawCamMenu(gs);   // поверх всего
    }

    void Gui::drawVideo(const GuiState& gs, const InfoState& is, const cv::Mat& video) {
        const cv::Rect vr = videoRect();
        if (vr.width <= 0) return;
        cv::Mat tmp(vr.height - 2, vr.width - 2, CV_8UC3, C_VIDEO);
        const int W2 = tmp.cols, H2 = tmp.rows;

        auto fitVideo = [&](int boxW, int boxH, cv::Mat& out, cv::Point& pos) {
            double sc = std::min((double)boxW / video.cols, (double)boxH / video.rows);
            int w = std::max(1, (int)(video.cols * sc)), h = std::max(1, (int)(video.rows * sc));
            cv::resize(video, out, cv::Size(w, h), 0, 0, cv::INTER_AREA);
            pos = cv::Point((boxW - w) / 2, (boxH - h) / 2);
            };

        if (is.art) {
            tmp.setTo(rgb(14, 17, 23));
            if (is.artHasPose) {
                HandStyle st;
                st.skin = rgb(240, 180, 142); st.edge = rgb(206, 132, 100); st.sleeve = rgb(38, 41, 52); st.sleeveOn = true;
                drawHandArt(tmp, cv::Rect(60, 40, W2 - 120, H2 - 110), is.artFeat, st);
            }
            else if (!is.artLetter.empty()) {
                centerTextX(tmp, is.artLetter, W2 / 2, H2 / 2 - lineH(170) / 2 - 20, 170, C_LETTER, true);
            }
            if (!is.artCaption.empty()) putTextW(tmp, is.artCaption, { 22, 16 }, 18, C_DIM);
            if (is.artHasPose && !is.artLetter.empty()) putTextW(tmp, is.artLetter, { 24, H2 - 120 }, 84, C_TEXT, true);
            if (!is.artNote.empty()) centerTextX(tmp, fitText(is.artNote, W2 - 60, 18, false), W2 / 2, H2 - 44, 18, C_DIM);

            if (!video.empty() && gs.screen == SCR_LEARN) {
                cv::Mat small;
                cv::Point p;
                fitVideo(220, 124, small, p);
                cv::Point pos(W2 - small.cols - 16, H2 - small.rows - 16);
                roundRect(tmp, cv::Rect(pos.x - 2, pos.y - 2, small.cols + 4, small.rows + 4), 12, rgb(70, 78, 98));
                blitRounded(tmp, small, pos, 10);
            }
        }
        else if (!video.empty()) {
            cv::Mat small;
            cv::Point p;
            fitVideo(W2, H2, small, p);
            small.copyTo(tmp(cv::Rect(p.x, p.y, small.cols, small.rows)));
        }
        else {
            static cv::Mat emptyCamImg = cv::imread("assets/2222.png", cv::IMREAD_UNCHANGED);

            if (!emptyCamImg.empty()) {
                double sc = std::max((double)W2 / emptyCamImg.cols, (double)H2 / emptyCamImg.rows);
                int w = (int)(emptyCamImg.cols * sc);
                int h = (int)(emptyCamImg.rows * sc);

                cv::Mat resized;
                cv::resize(emptyCamImg, resized, cv::Size(w, h), 0, 0, cv::INTER_AREA);

                int startX = (w - W2) / 2;
                int startY = (h - H2) / 2;
                cv::Rect cropRect(startX, startY, W2, H2);
                cv::Mat cropped = resized(cropRect);

                if (cropped.channels() == 4) {
                    std::vector<cv::Mat> ch;
                    cv::split(cropped, ch);
                    cv::Mat bgr;
                    cv::cvtColor(cropped, bgr, cv::COLOR_BGRA2BGR);
                    bgr.copyTo(tmp, ch[3]);
                }
                else {
                    cropped.copyTo(tmp);
                }
            }
        }

        if (!is.videoNote.empty()) {
            int w = textWidthW(is.videoNote, 17, true) + 28;
            roundRect(tmp, cv::Rect(16, 16, w, 36), 18, rgb(200, 52, 52));
            leftText(tmp, is.videoNote, 30, 34, 17, cv::Scalar(255, 255, 255), true);
        }
        if (!is.candidate.empty() && !is.art) {
            cv::Rect b(W2 - 16 - 84, 16, 84, 100);
            roundBorder(tmp, b, 16, rgb(18, 21, 29), rgb(58, 66, 86));
            centerTextX(tmp, is.candidate, b.x + b.width / 2, b.y + 8, 52, C_TEXT, true);
            cv::Rect track(b.x + 14, b.y + 82, b.width - 28, 6);
            roundRect(tmp, track, 3, rgb(52, 58, 74));
            int fw = (int)(track.width * std::max(0.0f, std::min(1.0f, is.progress)));
            if (fw >= 6) roundRect(tmp, cv::Rect(track.x, track.y, fw, track.height), 3, C_GREEN);
        }

        blitRounded(canvas_, tmp, vr.tl() + cv::Point(1, 1), 15);
    }

    void Gui::drawStrip(const GuiState& gs, const InfoState& is) {
        cv::rectangle(canvas_, cv::Rect(0, STRIP_Y, W, H - STRIP_Y), C_STRIP, cv::FILLED);
        cv::line(canvas_, { 0, STRIP_Y }, { W - 1, STRIP_Y }, C_EDGE, 1);
        cv::Scalar dot = gs.recording ? C_RED : (gs.running ? C_GREEN : C_DIMMER);
        cv::circle(canvas_, { 34, STRIP_Y + 22 }, 5, dot, cv::FILLED, cv::LINE_AA);
        leftText(canvas_, fitText(is.status, W - 80, 15, false), 50, STRIP_Y + 22, 15, C_DIM);
        if (!is.message.empty())
            leftText(canvas_, fitText(is.message, W - 80, 16, false), 34, STRIP_Y + 48, 16, C_AMBER);
    }

    void Gui::render(const GuiState& gs, const InfoState& is, const cv::Mat& video) {
        screen_ = gs.screen;
        enabledStart_ = !gs.running;
        enabledStop_ = gs.running;
        enabledSpeak_ = gs.canSpeak && !gs.replyText.empty();
        enabledSpeakText_ = gs.canSpeak && !is.text.empty();
        camMenuOpen_ = gs.camMenuOpen;
        camCount_ = (int)gs.cameras.size();
        hover_ = hit(mouseX_, mouseY_);

        if (!baseValid_ || gs != lastState_ || is.text != lastText_ || hover_ != lastHover_) {
            drawBase(gs, is);
            lastState_ = gs;
            lastText_ = is.text;
            lastHover_ = hover_;
            baseValid_ = true;
        }
        base_.copyTo(canvas_);
        drawVideo(gs, is, video);
        drawStrip(gs, is);
        cv::imshow(win_, canvas_);
    }

} // namespace HandTracker