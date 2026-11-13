// main.cpp - HandTalk: переводчик дактильной азбуки РЖЯ (окно с кнопками)
//
// Порядок работы цикла:
//   кнопки -> кадр -> поиск и ведение рук (нейросети) -> распознавание позы и движения -> текст -> окно.
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include <opencv2/opencv.hpp>
#include <opencv2/core/utils/logger.hpp>
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <memory>
#include <vector>

#include "Alphabet.h"
#include "Gesture.h"
#include "GestureEngine.h"
#include "Gui.h"
#include "HandArt.h"
#include "HandTracker.h"
#include "Log.h"
#include "MlpClassifier.h"
#include "Motion.h"
#include "Settings.h"
#include "Speech.h"
#include "Text.h"
#include "Trainer.h"
#include "UI.h"
#include "VideoSource.h"
#include "WinUtil.h"

using namespace HandTracker;

// Функция, которая берет последнюю нажатую клавишу и превращает её в русскую букву.
// Защищена от дублирования: если с прошлого нажатия прошло меньше 150 мс, возвращает пустую строку.
static std::wstring getRussianKey() {
    static bool keys[256] = { false };
    static int lastKeyCode = 0;       // Код последней нажатой клавиши
    static DWORD lastKeyTime = 0;     // Время последнего нажатия (в миллисекундах)

    // === ОТДЕЛЬНАЯ ЛОГИКА ДЛЯ BACKSPACE ===
    static DWORD lastBackspaceTime = 0;
    bool backspacePressed = (GetAsyncKeyState(VK_BACK) & 0x8000) != 0;
    if (backspacePressed) {
        DWORD now = GetTickCount();
        // Если это первое нажатие ИЛИ прошло больше 80 мс с прошлого
        if (!keys[VK_BACK] || (now - lastBackspaceTime > 80)) {
            keys[VK_BACK] = true;
            lastBackspaceTime = now;
            return L"\b"; // Возвращаем бэкспейс
        }
        return L""; // Ждем, пока не пройдет 80 мс
    }
    else {
        keys[VK_BACK] = false;
    }

    // === ОБЫЧНЫЕ БУКВЫ (твоя логика) ===
    std::wstring result;

    for (int i = 8; i < 256; ++i) {
        if (i == VK_BACK) continue; // Бэкспейс уже обработан выше

        bool pressed = (GetAsyncKeyState(i) & 0x8000) != 0;

        // Если клавиша нажата СЕЙЧАС, но не была нажата РАНЬШЕ
        if (pressed && !keys[i]) {

            // Если это та же самая клавиша, что и в прошлый раз, и с момента нажатия прошло меньше 100 мс — 
            // значит, это "залипание", игнорируем.
            if (i == lastKeyCode && (GetTickCount() - lastKeyTime) < 100) {
                keys[i] = pressed;
                return L""; // Игнорируем дубликат той же клавиши
            }

            if (i == VK_RETURN) { result = L"\n"; break; }
            if (i == VK_SPACE) { result = L" "; break; }
            if (i == VK_ESCAPE) { result = L"\x1B"; break; }

            BYTE keyboardState[256];
            GetKeyboardState(keyboardState);
            wchar_t buf[5] = { 0 };
            if (ToUnicode(i, 0, keyboardState, buf, 4, 0) > 0) {
                result = buf;
                lastKeyCode = i;           // Запоминаем, какую клавишу нажали
                lastKeyTime = GetTickCount(); // Запоминаем время
            }
            break;
        }
    }

    // Обновляем состояния всех клавиш (кроме бэкспейса, он управляется выше)
    for (int i = 8; i < 256; ++i) {
        if (i != VK_BACK) {
            keys[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
        }
    }

    return result;
}

static HandPoints toPoints(const HandTrack& t) {
    HandPoints p;
    for (int i = 0; i < 21; ++i) p[i] = { t.points[i].x, t.points[i].y, t.points[i].z };
    return p;
}

// === СЧЁТЧИКИ ОБРАЗЦОВ ===
struct Counts {
    std::map<std::string, int> pose, move;
    unsigned long long mask(const std::map<std::string, int>& m) const {
        unsigned long long r = 0;
        for (std::size_t i = 0; i < ALPHABET_SIZE; ++i) {
            auto it = m.find(ALPHABET[i].code);
            if (it != m.end() && it->second > 0) r |= (1ULL << i);
        }
        return r;
    }
    int of(const std::map<std::string, int>& m, const std::string& code) const {
        auto it = m.find(code);
        return it == m.end() ? 0 : it->second;
    }
};

// Заглавная русская буква (фразы и текст из набора хранятся заглавными)
static wchar_t upperRu(wchar_t c) {
    if (c >= 0x0430 && c <= 0x044F) return (wchar_t)(c - 0x20);
    if (c == 0x0451) return (wchar_t)0x0401;
    return c;
}

// Номер буквы в алфавите по русской букве, -1 - если это не буква алфавита
static int indexFromLetter(wchar_t c) {
    for (std::size_t i = 0; i < ALPHABET_SIZE; ++i)
        if (ALPHABET[i].letter == c) return (int)i;
    return -1;
}

static std::wstring asciiToW(const char* s) {
    std::wstring w;
    while (s && *s) w += (wchar_t)(unsigned char)*s++;
    return w;
}

int main() {
    SetProcessDPIAware();
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_SILENT);
    SetConsoleOutputCP(CP_UTF8);

    logOpen("handtalk.log");
    logInfo("=== Запуск HandTalk ===");

    // === НАСТРОЙКИ ===
    Settings st;
    int errs = st.load("handtalk.ini");
    if (errs) logWarn("В handtalk.ini строк с ошибками: " + std::to_string(errs) + " (взяты значения по умолчанию)");

    // === НЕЙРОСЕТИ РУКИ ===
    PalmDetector palm;
    HandLandmarker lm;
    if (!palm.init("models\\palm_detection.onnx") || !lm.init("models\\hand_landmark.onnx")) {
        logError("Не удалось загрузить модели из папки models");
        MessageBoxW(nullptr, L"Не найдены файлы моделей в папке models:\npalm_detection.onnx и hand_landmark.onnx.",
            L"HandTalk", MB_ICONERROR);
        return -1;
    }

    // === КЛАССИФИКАТОРЫ ===
    KnnClassifier knn(st.knnK, st.knnMaxDistance);
    SeqKnn dynKnn(st.knnK, st.dynMaxDistance);
    MlpClassifier mlp;
    mlp.load(st.mlpModel);
    const IClassifier* clf = mlp.ready() ? static_cast<const IClassifier*>(&mlp) : static_cast<const IClassifier*>(&knn);

    Counts counts;
    HandRefs refs;
    auto applySamples = [&](std::vector<Sample> stat, std::vector<SeqSample> dyn) {
        counts.pose.clear(); counts.move.clear();
        refs = buildHandRefs(stat);
        for (const Sample& s : stat) ++counts.pose[s.code];
        for (const SeqSample& s : dyn) ++counts.move[s.code];
        knn.setSamples(std::move(stat));
        dynKnn.setSamples(std::move(dyn));
        };
    {
        int sk = 0, dk = 0;
        auto stat = loadSamples(st.samplesFile, &sk);
        auto dyn = loadSeqSamples(st.dynSamplesFile, &dk);
        if (sk) logWarn("В " + st.samplesFile + " пропущено повреждённых строк: " + std::to_string(sk));
        if (dk) logWarn("В " + st.dynSamplesFile + " пропущено повреждённых строк: " + std::to_string(dk));
        applySamples(std::move(stat), std::move(dyn));
    }
    logInfo("Классификатор: " + std::string(clf->name()) + ", поз: " + std::to_string(knn.size()) +
        ", движений: " + std::to_string(dynKnn.size()));

    // === ДВИЖОК ЖЕСТОВ И ТЕКСТА ===
    EngineParams ep;
    ep.minConfidence = st.minConfidence;
    ep.dynMinConfidence = st.dynMinConfidence;
    ep.stableFrames = st.stableFrames;
    ep.spaceFrames = st.spaceFrames;
    ep.motion.moveOn = st.moveOn;
    ep.motion.moveOff = st.moveOff;
    GestureEngine eng(ep);
    eng.setStatic(clf);
    eng.setDynamic(&dynKnn);

    GestureEngine learnEng(ep);
    learnEng.setStatic(clf);
    learnEng.setDynamic(&dynKnn);

    // === ОЗВУЧИВАНИЕ ===
    Speech speech;
    std::wstring speechNote;
    if (st.speakEnabled) {
        if (!speech.init(st.speechRate, st.speechVolume)) { speechNote = L"Озвучивание недоступно"; logWarn("SAPI недоступен"); }
        else if (!speech.hasRussianVoice()) { speechNote = L"Нет русского голоса: озвучивание выключено"; logWarn("Русский голос не найден"); }
    }
    bool canSpeak = st.speakEnabled && speech.available() && speech.hasRussianVoice();

    // === ОКНО ===
    const std::string WIN = "HandTalk";
    Gui gui(WIN);
    gui.init();

    GuiState gs;
    gs.canSpeak = canSpeak;
    gs.learnTotal = (int)ALPHABET_SIZE;
    gs.setVals.resize(SET_COUNT);
    for (int i = 0; i < SET_COUNT; ++i) gs.setVals[i] = st.valueText(i);
    std::string chosenFile;
    Region region = { 0, 0, 0, 0 };
    bool haveRegion = false;
    if (!st.videoFile.empty()) {
        chosenFile = st.videoFile;
        std::wstring w = fromUtf8(st.videoFile);
        std::size_t sl = w.find_last_of(L"\\/");
        gs.fileLabel = sl == std::wstring::npos ? w : w.substr(sl + 1);
        gs.source = 3;
    }
    else gs.source = 0;
    gs.camera = std::max(0, st.cameraIndex);                // последняя выбранная камера

    using clock = std::chrono::steady_clock;
    auto seconds = []() { return std::chrono::duration<double>(clock::now().time_since_epoch()).count(); };
    std::wstring message;
    double messageUntil = 0;
    auto setMessage = [&](const std::wstring& m, double sec = 7.0) { message = m; messageUntil = seconds() + sec; };
    if (!speechNote.empty()) setMessage(speechNote, 10);
    else if (knn.size() == 0 && dynKnn.size() == 0)
        setMessage(L"Образцов жестов нет: запишите их в «Настройки» - «Для разработчиков» и нажмите «Обучить модель»", 12);

    // Настройка изменена: обновляем значения в окне и сразу записываем handtalk.ini
    auto settingsChanged = [&]() {
        for (int i = 0; i < SET_COUNT; ++i) gs.setVals[i] = st.valueText(i);
        if (st.save("handtalk.ini")) setMessage(L"Сохранено в handtalk.ini. Заработает после перезапуска программы", 5);
        else { logWarn("Не удалось записать handtalk.ini"); setMessage(L"Не удалось записать handtalk.ini: нет доступа к папке программы", 8); }
        };

    auto refreshCounts = [&]() {
        const std::string code = ALPHABET[gs.letter].code;
        gs.poseMask = counts.mask(counts.pose);
        gs.moveMask = counts.mask(counts.move);
        gs.letterPose = counts.of(counts.pose, code);
        gs.letterMove = counts.of(counts.move, code);
        };
    refreshCounts();

    // === СОСТОЯНИЕ СЕАНСА ===
    std::unique_ptr<IFrameSource> src;
    std::vector<HandTrack> tracks;
    const std::vector<cv::Scalar> colors = {
        cv::Scalar(0, 255, 0), cv::Scalar(255, 128, 0), cv::Scalar(0, 255, 255), cv::Scalar(255, 0, 255) };
    auto prevTime = clock::now();
    auto statTime = prevTime;
    int nextHandId = 1, palmCountdown = 0, saveSkip = 0, lostFrames = 0, iter = 0;
    bool signalLost = false;

    // === ОБУЧЕНИЕ И АНИМАЦИЯ ===
    std::string learnCode;
    int learnFrames = 0, learnEmpty = 0;
    int sourceBeforeLearn = 0;
    const double ANIM_STEP = 1.0;
    std::vector<int> animLetters;
    double animStart = 0;

    auto removeInactive = [&]() {
        tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
            [](const HandTrack& t) { return !t.active; }), tracks.end());
        };

    // === НАЧАТЬ ===
    auto startSession = [&]() {
        std::unique_ptr<IFrameSource> s;
        if (gs.source == 0) {
            s = std::make_unique<CameraSource>(gs.camera);
            if (!s->open()) {
                logError("Камера " + std::to_string(gs.camera) + " недоступна");
                setMessage(L"Камера " + std::to_wstring(gs.camera + 1) + L" недоступна: закройте другие программы с камерой или выберите другую камеру в списке");
                return;
            }
        }
        else if (gs.source == 3) {
            if (chosenFile.empty()) { setMessage(L"Сначала выберите видеофайл (кнопка «Видео файл»)"); return; }
            s = std::make_unique<FileSource>(chosenFile);
            if (!s->open()) { logError("Не открывается видеофайл " + chosenFile); setMessage(L"Не удалось открыть видеофайл"); return; }
        }
        else {
            if (!haveRegion) { setMessage(L"Сначала выберите область экрана (кнопка «Запись экрана»)"); return; }
            s = std::make_unique<ScreenSource>(region.x, region.y, region.w, region.h);
            if (!s->open()) { logError("Не удалось начать захват экрана"); setMessage(L"Не удалось начать захват экрана"); return; }
        }
        cv::Mat first;
        for (int i = 0; i < 100 && !s->read(first); ++i) Sleep(30);
        if (first.empty()) { logError("Источник не отдаёт кадры"); setMessage(L"Источник не отдаёт кадры"); return; }

        src = std::move(s);
        tracks.clear();
        eng.reset();
        learnEng.reset();
        palmCountdown = 0; saveSkip = 0; lostFrames = 0; signalLost = false;
        gs.running = true;
        logInfo("Источник: " + src->name());
        if (gs.source == 4 && !excludeFromCapture(WIN, true))
            setMessage(L"Окно программы может попасть в кадр: поставьте его вне выбранной области");
        };

    // === ЗАКОНЧИТЬ ===
    auto stopSession = [&]() {
        if (gs.source == 4) excludeFromCapture(WIN, false);
        src.reset();
        tracks.clear();
        eng.reset();
        learnEng.reset();
        gs.running = false;
        gs.recording = false;
        logInfo("Остановлено пользователем");
        };

    // === ОБУЧИТЬ МОДЕЛЬ ===
    auto trainModel = [&]() {
        int sk = 0, dk = 0;
        auto stat = loadSamples(st.samplesFile, &sk);
        auto dyn = loadSeqSamples(st.dynSamplesFile, &dk);
        TrainParams tp;
        tp.k = st.knnK;
        tp.maxDistance = st.knnMaxDistance;
        tp.dynMaxDistance = st.dynMaxDistance;
        tp.minConfidence = st.minConfidence;
        tp.dynMinConfidence = st.dynMinConfidence;
        TrainReport rep = trainAndEvaluate(stat, dyn, tp);

        std::ofstream out("train_report.txt", std::ios::binary | std::ios::trunc);
        if (out) {
            out << "\xEF\xBB\xBF" << toUtf8(rep.full);
            if (sk || dk) out << "\r\nПропущено повреждённых строк: поз " << sk << ", движений " << dk << "\r\n";
        }
        logInfo(rep.brief);
        bool empty = stat.empty() && dyn.empty();
        if (!empty) applySamples(std::move(stat), std::move(dyn));
        refreshCounts();
        std::wstring m = rep.brief;
        if (!empty) m += L". Подробности: train_report.txt";
        if (!empty && clf == static_cast<const IClassifier*>(&mlp)) m += L" (позы сейчас распознаёт MLP)";
        setMessage(m, 15);
        };

    // === ОБРАБОТКА НАЖАТИЙ ===
    auto render = [&](const cv::Mat& video, const InfoState& is) { gui.render(gs, is, video); };

    // Поиск подключённых камер (для выпадающего списка): при первом открытии и по кнопке «Обновить список»
    bool camWasOpen = false;                       // список был открыт до этого нажатия
    bool camsScanned = false;
    auto scanCameras = [&]() {
        InfoState is;
        is.message = L"Поиск камер...";
        render(cv::Mat(), is);
        cv::waitKey(1);
        gs.cameras = findCameras();
        camsScanned = true;
        logInfo("Найдено камер: " + std::to_string(gs.cameras.size()));
    };

    // Озвучить текст; если голос не сработал - сообщение и запись в журнал
    auto speakText = [&](const std::wstring& t) {
        if (!speech.speak(t)) {
            logWarn("SAPI: не удалось озвучить текст");
            setMessage(L"Не удалось озвучить текст: проверьте русский голос Windows и звук");
        }
    };

    auto learnReset = [&]() {
        gs.learnResult = LR_WAIT;
        gs.learnShown = 0;
        gs.showRef = false;
        learnCode.clear();
        learnFrames = 0; learnEmpty = 0;
        learnEng.reset();
        };

    auto stopAnim = [&]() { gs.animOn = false; animLetters.clear(); };

    // Остановить запись, анимацию и видео перед уходом с экрана
    auto closeWork = [&]() {
        if (gs.recording) {
            gs.recording = false;
            setMessage(L"Запись остановлена. Нажмите «Обучить модель», чтобы новые образцы заработали");
        }
        stopAnim();
        if (gs.running) stopSession();
    };

    // «Назад»: из раздела для разработчиков - в настройки, с остальных экранов - в меню
    auto goBack = [&]() {
        closeWork();
        if (gs.screen == SCR_DEV || gs.screen == SCR_DEV_PARAMS) { gs.screen = SCR_SETTINGS; return; }
        if (gs.screen == SCR_LEARN) gs.source = sourceBeforeLearn;
        gs.screen = SCR_HOME;
        };

    auto onClick = [&](int id) {
        if (id >= ID_SRC_CAM0 && id <= ID_SRC_FILE) {
            if (gs.running) { setMessage(L"Источник можно менять только после кнопки «Закончить»"); return; }
            if (id == ID_SRC_FILE) {
                std::wstring name;
                std::string p = pickVideoFile(name);
                if (!p.empty()) { chosenFile = p; gs.fileLabel = name; gs.source = 3; }
            }
            else if (id == ID_SRC_SCREEN) {
                InfoState is;
                is.message = L"Окно свернётся. Через 3 секунды появится снимок экрана: выделите область мышью";
                render(cv::Mat(), is);
                cv::waitKey(1);
                minimizeWindow(WIN);
                Sleep(3000);
                auto r = selectRegion();
                restoreWindow(WIN);
                if (r) {
                    region = *r; haveRegion = true;
                    gs.screenLabel = std::to_wstring(r->w) + L"x" + std::to_wstring(r->h);
                    gs.source = 4;
                }
            }
            else {                                          // плитка «Камера»: открыть или закрыть список камер
                gs.source = 0;
                if (!camWasOpen) {
                    if (!camsScanned) scanCameras();
                    gs.camMenuOpen = true;
                }
            }
            return;
        }
        if (id >= ID_CAM_BASE && id < ID_CAM_BASE + 16) {   // выбрана камера из списка
            const std::size_t k = (std::size_t)(id - ID_CAM_BASE);
            if (k < gs.cameras.size()) {
                gs.camera = gs.cameras[k];
                gs.source = 0;
                st.cameraIndex = gs.camera;                 // запоминаем выбор в handtalk.ini
                st.save("handtalk.ini");
            }
            return;
        }
        if (id >= ID_SET_BASE) {                       // кнопки «-» и «+» у настроек
            int k = id - ID_SET_BASE;
            st.change(k / 2, (k % 2) ? +1 : -1);
            settingsChanged();
            return;
        }
        if (id >= ID_LETTER_BASE) {
            gs.letter = id - ID_LETTER_BASE;
            gs.recording = false;
            refreshCounts();
            return;
        }
        switch (id) {
        case ID_START: if (!gs.running) startSession(); break;
        case ID_STOP: if (gs.running) stopSession(); break;
        case ID_GO_TALK: gs.screen = SCR_TALK; break;
        case ID_GO_SETTINGS: gs.screen = SCR_SETTINGS; break;
        case ID_CAM_REFRESH: scanCameras(); gs.camMenuOpen = true; break;
        case ID_GO_DEV: gs.screen = SCR_DEV; break;
        case ID_TAB_SAMPLES: if (gs.screen != SCR_DEV) { closeWork(); gs.screen = SCR_DEV; } break;
        case ID_TAB_PARAMS: if (gs.screen != SCR_DEV_PARAMS) { closeWork(); gs.screen = SCR_DEV_PARAMS; } break;
        case ID_SET_RESET:                              // настройки по умолчанию: на вкладке параметров - для разработчиков
            st.reset(gs.screen == SCR_DEV_PARAMS);
            settingsChanged();
            break;
        case ID_GO_LEARN:
            gs.screen = SCR_LEARN;
            gs.learnTarget = 0;
            learnReset();
            sourceBeforeLearn = gs.source;
            gs.source = 0;                                  // изучение алфавита всегда по камере
            startSession();
            break;
        case ID_NAV_BACK: goBack(); break;
        case ID_ERASE: eng.text().backspace(); break;
        case ID_CLEAR: eng.text().clear(); break;
        case ID_SEND: // Озвучить написанный текст
            if (canSpeak && !gs.replyText.empty()) {
                speakText(gs.replyText);
                gs.replyText.clear();
                gs.replyActive = false;
            }
            break;
        case ID_SPEAK_TEXT: // Озвучить распознанный текст (он остаётся в поле)
            if (canSpeak && !eng.text().text().empty()) speakText(eng.text().text());
            break;

        case ID_ANIM:
            if (gs.animOn) {
                stopAnim();
            }
            else {
                // Анимация работает ТОЛЬКО с текстом из поля ввода (gs.replyText)
                if (gs.replyText.empty()) {
                    setMessage(L"Сначала напишите сообщение в поле ввода");
                }
                else {
                    animLetters.clear();
                    for (wchar_t ch : gs.replyText) {
                        int i = indexFromLetter(upperRu(ch));
                        if (i >= 0) animLetters.push_back(i);
                    }
                    if (animLetters.empty()) {
                        setMessage(L"В тексте нет букв алфавита для анимации");
                    }
                    else {
                        animStart = seconds();
                        gs.animOn = true;
                    }
                }
            }
            break;
        case ID_SHOW_GESTURE: gs.showRef = !gs.showRef; break;
        case ID_RETRY:
            learnReset();
            if (!gs.running) startSession();
            break;
        case ID_NEXT:
            gs.learnTarget = (gs.learnTarget + 1) % (int)ALPHABET_SIZE;
            learnReset();
            if (!gs.running) startSession();
            break;
        case ID_KIND_POSE: gs.kindMove = false; gs.recording = false; break;
        case ID_KIND_MOVE: gs.kindMove = true; gs.recording = false; break;
        case ID_REC:
            if (gs.recording) {
                gs.recording = false;
                setMessage(L"Запись остановлена. Нажмите «Обучить модель», чтобы новые образцы заработали");
            }
            else if (!gs.running) setMessage(L"Сначала нажмите «Начать», чтобы включить видео");
            else {
                gs.recording = true; gs.recCount = 0; saveSkip = 0;
                setMessage(gs.kindMove
                    ? L"Запись движения: покажите жест и остановите руку на секунду. Повторите 20-30 раз"
                    : L"Запись позы: держите жест неподвижно, меняйте наклон руки и расстояние до камеры", 12);
            }
            break;
        case ID_TRAIN: {
            gs.recording = false;
            InfoState is;
            is.message = L"Идёт обучение...";
            render(cv::Mat(), is);
            cv::waitKey(1);
            trainModel();
            break;
        }
        }
        };

    // ==================================================================
    // === ГЛАВНЫЙ ЦИКЛ ===
    // ==================================================================
    while (true) {
        ++iter;


        // --- ПРОВЕРКА ФОКУСА ОКНА ---
       // Если окно программы неактивно (свернуто или кликнули в другое окно) — снимаем фокус с поля
        HWND hwnd = FindWindowA(nullptr, WIN.c_str());
        if (hwnd && GetForegroundWindow() != hwnd) {
            gs.replyActive = false;
        }

        // --- ОБРАБОТКА КЛИКОВ МЫШИ ---
        if (gui.hadClickThisFrame()) {
            gs.replyActive = false;  // любой клик снимает фокус с поля
            camWasOpen = gs.camMenuOpen;
            gs.camMenuOpen = false;  // и закрывает список камер (если клик по строке списка - он уже учтён в pollClick)
        }
        for (int id; (id = gui.pollClick()) != ID_NONE; ) {
            if (id == ID_REPLY_BOX) {
                gs.replyActive = true;  // но если кликнули в поле — включаем обратно
            }
            else {
                onClick(id);
            }
        }

        // --- ВВОД ТЕКСТА (ОПРОС НА КАЖДОМ КАДРЕ) ---
        std::wstring pressedKey = getRussianKey();
        if (gs.replyActive) {
            if (pressedKey == L"\b") {
                if (!gs.replyText.empty()) gs.replyText.pop_back();
            }
            else if (pressedKey == L"\n") {
                gs.replyText.clear();
                gs.replyActive = false;
            }
            else if (pressedKey == L"\x1B") {
                gs.replyActive = false;
            }
            else if (!pressedKey.empty()) {
                gs.replyText += pressedKey;
            }
        }

        cv::Mat frame;
        InfoState is;
        FrameOut o;
        int handsCount = 0;
        GestureEngine& cur = (gs.screen == SCR_LEARN) ? learnEng : eng;

        if (src) {
            if (!src->read(frame)) {
                if (!signalLost) { signalLost = true; logWarn("Потерян сигнал видео"); }
                ++lostFrames;
                tracks.clear();
                cur.update(nullptr, false, !gs.recording, true);
                if (lostFrames % 60 == 0 && src->open()) logInfo("Сигнал восстановлен");
                frame.release();
            }
            else {
                if (signalLost) { signalLost = false; lostFrames = 0; logInfo("Сигнал восстановлен"); }

                auto t0 = clock::now();
                float dt = std::chrono::duration<float>(t0 - prevTime).count();
                prevTime = t0;
                dt = std::min(std::max(dt, 1.0f / 120.0f), 0.2f);
                bool process = true; // Всегда обрабатываем каждый кадр

                if (process) {
                    for (auto& t : tracks) {
                        Roi roi = roiFromLandmarks(t.raw);
                        auto res = lm.predict(frame, roi, TRACK_PRESENCE_THRESHOLD);
                        if (res.detected) {
                            t.updatePoints(res.points, dt);
                            t.handProb = 0.8f * t.handProb + 0.2f * res.handednessProb;
                            t.handedness = t.handProb > 0.5f ? 1 : 0;
                            t.score = res.presence;
                            t.missedFrames = 0;
                        }
                        else if (++t.missedFrames > TRACK_MAX_MISS) t.active = false;
                    }
                    removeInactive();

                    for (size_t i = 0; i < tracks.size(); ++i)
                        for (size_t j = i + 1; j < tracks.size(); ++j)
                            if (tracks[i].active && tracks[j].active &&
                                cv::norm(tracks[i].center() - tracks[j].center()) < 0.5f * tracks[i].size())
                                tracks[j].active = false;
                    removeInactive();

                    if ((int)tracks.size() < st.maxHands) {
                        if (tracks.empty()) palmCountdown = 0;
                        if (palmCountdown <= 0) {
                            auto palms = palm.detect(frame, PALM_THRESHOLD);
                            for (auto& p : palms) {
                                if ((int)tracks.size() >= st.maxHands) break;
                                bool known = false;
                                for (auto& t : tracks)
                                    if (cv::norm(cv::Point2f(p.cx, p.cy) - t.center()) < t.size()) { known = true; break; }
                                if (known) continue;
                                auto res = lm.predict(frame, roiFromPalm(p), PRESENCE_THRESHOLD);
                                if (!res.detected) continue;
                                HandTrack t;
                                t.id = nextHandId++;
                                t.handProb = res.handednessProb;
                                t.handedness = res.handedness;
                                t.score = res.presence;
                                t.updatePoints(res.points, dt);
                                tracks.push_back(t);
                            }
                            palmCountdown = PALM_SKIP;
                        }
                        else palmCountdown--;
                    }
                }

                const HandTrack* use = nullptr;
                for (auto& t : tracks)
                    if (t.missedFrames == 0 && (!use || t.score > use->score)) use = &t;
                HandPoints pts{};
                if (use) pts = toPoints(*use);
                o = cur.update(use ? &pts : nullptr, use && use->handedness == 1, !gs.recording, process);
                handsCount = use ? (int)tracks.size() : 0;

                if (gs.screen == SCR_LEARN && gs.learnResult != LR_OK && process) {
                    wchar_t seen = 0;
                    if (!o.dynCode.empty()) {
                        seen = letterFromCode(o.dynCode);
                        learnEmpty = 0;
                    }
                    else if (!o.code.empty()) {
                        learnEmpty = 0;
                        if (o.code == learnCode) ++learnFrames;
                        else { learnCode = o.code; learnFrames = 1; }
                        if (learnFrames >= st.stableFrames) seen = letterFromCode(learnCode);
                    }
                    else {
                        learnCode.clear(); learnFrames = 0;
                        if (++learnEmpty > 20 && gs.learnResult == LR_WRONG) {
                            gs.learnResult = LR_WAIT;
                            gs.learnShown = 0;
                        }
                    }
                    if (seen) {
                        gs.learnShown = seen;
                        gs.learnResult = (seen == ALPHABET[gs.learnTarget].letter) ? LR_OK : LR_WRONG;
                    }
                }

                if (gs.recording) {
                    const std::string code = ALPHABET[gs.letter].code;
                    if (!gs.kindMove && use && tracks.size() == 1 && o.hasFeatures) {
                        if (++saveSkip % 3 == 0) {
                            Sample s; s.code = code; s.f = o.feat;
                            if (appendSample(st.samplesFile, s)) { ++counts.pose[code]; ++gs.recCount; refreshCounts(); }
                        }
                        if (gs.recCount >= 600) {
                            gs.recording = false;
                            setMessage(L"Записано 600 образцов - достаточно. Запись остановлена");
                        }
                    }
                    if (gs.kindMove && o.hasSeq) {
                        SeqSample s; s.code = code; s.f = o.seq;
                        if (appendSeqSample(st.dynSamplesFile, s)) { ++counts.move[code]; ++gs.recCount; refreshCounts(); }
                    }
                }
                if (!o.dynCode.empty())
                    setMessage(L"Движение: " + std::wstring(1, letterFromCode(o.dynCode)) + L" (" +
                        std::to_wstring((int)(o.dynConf * 100)) + L"%)", 3);

                for (size_t i = 0; i < tracks.size(); ++i) {
                    cv::Scalar col = tracks[i].missedFrames > 0 ? cv::Scalar(128, 128, 128) : colors[i % colors.size()];
                    drawHand(frame, tracks[i], col, tracks[i].id);
                }

            
            }
        }

        is.text = eng.text().text();

        std::wstring status;
        if (gs.running) {
            status = L"Рук: " + std::to_wstring(handsCount) + L"  |  " + (o.moving ? L"движение" : L"рука стоит");
        }
        else status = L"Остановлено";
        status += L"  |  " + asciiToW(clf->name()) + L": поз " + std::to_wstring(knn.size()) +
            L", движений " + std::to_wstring(dynKnn.size());
        is.status = status;
        if (seconds() < messageUntil) is.message = message;
        else if (canSpeak && speech.paused()) is.message = L"Речь на паузе";

        if (gs.running) {
            if (gs.recording) {
                const wchar_t letter = ALPHABET[gs.letter].letter;
                is.videoNote = std::wstring(L"ЗАПИСЬ ") + letter + (gs.kindMove ? L" (движение): " : L" (поза): ") + std::to_wstring(gs.recCount);
            }
            else if (gs.screen != SCR_LEARN && !eng.text().candidate().empty()) {
                wchar_t ch = letterFromCode(eng.text().candidate());
                if (ch) { is.candidate = std::wstring(1, ch); is.progress = eng.text().progress(); }
            }
        }

        if (gs.screen == SCR_LEARN && gs.showRef) {
            const int t = gs.learnTarget;
            const std::string code = ALPHABET[t].code;
            const bool hasMove = counts.of(counts.move, code) > 0;
            is.art = true;
            is.artLetter = std::wstring(1, ALPHABET[t].letter);
            is.artCaption = L"Эталон: средняя поза по вашим образцам";
            if (refs.has[t]) {
                is.artHasPose = true;
                is.artFeat = refs.f[t];
                if (hasMove) is.artNote = L"У этой буквы есть движение: после позы двигайте рукой, как при записи";
            }
            else is.artNote = hasMove ? L"Жест с движением: эталона позы нет, повторите движение рукой"
                : L"Образцов этой буквы нет: запишите их в «Настройки» - «Для разработчиков»";
        }
        if (gs.animOn) {
            int k = (int)((seconds() - animStart) / ANIM_STEP);
            if (k >= (int)animLetters.size()) stopAnim();
            else {
                const int t = animLetters[k];
                is.art = true;
                is.artLetter = std::wstring(1, ALPHABET[t].letter);
                is.artCaption = L"Сообщение: буква " + std::to_wstring(k + 1) + L" из " + std::to_wstring(animLetters.size());
                if (refs.has[t]) { is.artHasPose = true; is.artFeat = refs.f[t]; }
                else is.artNote = L"Для этой буквы нет образца позы";
            }
        }

        render(frame, is);
        if (iter == 2) applyDarkTitleBar(WIN);

        cv::waitKey(1);

        if (iter > 10 && cv::getWindowProperty(WIN, cv::WND_PROP_VISIBLE) < 1) break;
    }

    if (gs.running) stopSession();
    speech.stop();
    logInfo("=== Завершение работы ===");
    logClose();
    return 0;
}