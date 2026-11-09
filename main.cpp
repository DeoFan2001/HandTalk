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
#include "HandTracker.h"
#include "Log.h"
#include "MlpClassifier.h"
#include "Motion.h"
#include "Performance.h"
#include "Settings.h"
#include "Speech.h"
#include "Text.h"
#include "TextRender.h"
#include "Trainer.h"
#include "UI.h"
#include "VideoSource.h"
#include "WinUtil.h"

using namespace HandTracker;

// === ГОРЯЧИЕ КЛАВИШИ ===
// Глобальные (работают, даже когда окно не в фокусе, - нужно для режима экрана), всегда вместе с Ctrl+Alt.
static bool keyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
static bool modDown() { return keyDown(VK_CONTROL) && keyDown(VK_MENU); }

struct Hotkey {
    int vk; bool prev = false;
    explicit Hotkey(int v) : vk(v) {}
    bool pressed() {
        bool now = modDown() && keyDown(vk);
        bool fire = now && !prev;
        prev = now;
        return fire;
    }
};

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
    // Подставить образцы в классификаторы и пересчитать счётчики
    auto applySamples = [&](std::vector<Sample> stat, std::vector<SeqSample> dyn) {
        counts.pose.clear(); counts.move.clear();
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
    else gs.source = std::min(2, std::max(0, st.cameraIndex));

    using clock = std::chrono::steady_clock;
    auto seconds = []() { return std::chrono::duration<double>(clock::now().time_since_epoch()).count(); };
    std::wstring message;
    double messageUntil = 0;
    auto setMessage = [&](const std::wstring& m, double sec = 7.0) { message = m; messageUntil = seconds() + sec; };
    if (!speechNote.empty()) setMessage(speechNote, 10);
    else if (knn.size() == 0 && dynKnn.size() == 0)
        setMessage(L"Образцов жестов нет: запишите их в разделе «Для администраторов» и нажмите «Обучить модель»", 12);

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
    PerformanceGovernor gov(st.targetFps);
    const std::vector<cv::Scalar> colors = {
        cv::Scalar(0, 255, 0), cv::Scalar(255, 128, 0), cv::Scalar(0, 255, 255), cv::Scalar(255, 0, 255) };
    auto prevTime = clock::now();
    auto statTime = prevTime;
    int nextHandId = 1, palmCountdown = 0, saveSkip = 0, lostFrames = 0, iter = 0;
    bool signalLost = false;
    std::wstring wrapKey;                       // для какого текста посчитан перенос строк
    std::vector<std::wstring> wrapped;

    auto removeInactive = [&]() {
        tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
            [](const HandTrack& t) { return !t.active; }), tracks.end());
    };

    // === НАЧАТЬ ===
    auto startSession = [&]() {
        std::unique_ptr<IFrameSource> s;
        if (gs.source <= 2) {
            s = std::make_unique<CameraSource>(gs.source);
            if (!s->open()) {
                logError("Камера " + std::to_string(gs.source) + " недоступна");
                setMessage(L"Камера " + std::to_wstring(gs.source) + L" недоступна: закройте другие программы с камерой или выберите другой источник");
                return;
            }
        }
        else if (gs.source == 3) {
            if (chosenFile.empty()) { setMessage(L"Сначала выберите видеофайл (кнопка «Видеофайл...»)"); return; }
            s = std::make_unique<FileSource>(chosenFile);
            if (!s->open()) { logError("Не открывается видеофайл " + chosenFile); setMessage(L"Не удалось открыть видеофайл"); return; }
        }
        else {
            if (!haveRegion) { setMessage(L"Сначала выберите область экрана (кнопка «Область экрана...»)"); return; }
            s = std::make_unique<ScreenSource>(region.x, region.y, region.w, region.h);
            if (!s->open()) { logError("Не удалось начать захват экрана"); setMessage(L"Не удалось начать захват экрана"); return; }
        }
        cv::Mat first;
        for (int i = 0; i < 100 && !s->read(first); ++i) Sleep(30);
        if (first.empty()) { logError("Источник не отдаёт кадры"); setMessage(L"Источник не отдаёт кадры"); return; }

        src = std::move(s);
        tracks.clear();
        eng.reset();
        gov = PerformanceGovernor(st.targetFps);
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
    bool quit = false;
    auto render = [&](const cv::Mat& video, const InfoState& is) { gui.render(gs, is, video); };

    auto onClick = [&](int id) {
        if (id >= ID_SRC_CAM0 && id <= ID_SRC_SCREEN) {
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
            else gs.source = id - ID_SRC_CAM0;
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
        case ID_BACK: eng.text().backspace(); break;
        case ID_CLEAR: eng.text().clear(); break;
        case ID_SPEAK: if (canSpeak) speech.speak(eng.text().text()); break;
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

    Hotkey kQ('Q'), kB(VK_BACK), kC('C'), kS('S'), kA('A'), kP('P');

    // ==================================================================
    // === ГЛАВНЫЙ ЦИКЛ ===
    // ==================================================================
    while (!quit) {
        ++iter;

        // --- управление ---
        if (kQ.pressed()) break;
        if (kB.pressed()) eng.text().backspace();
        if (kC.pressed()) eng.text().clear();
        if (kS.pressed() && canSpeak) speech.speak(eng.text().lastWord());
        if (kA.pressed() && canSpeak) speech.speak(eng.text().text());
        if (kP.pressed() && canSpeak) speech.pauseResume();
        for (int id; (id = gui.pollClick()) != ID_NONE; ) onClick(id);

        cv::Mat frame;
        InfoState is;
        std::wstring statusExtra;
        FrameOut o;
        int handsCount = 0;

        if (src) {
            // === 1. КАДР ===
            if (!src->read(frame)) {
                if (!signalLost) { signalLost = true; logWarn("Потерян сигнал видео"); }
                ++lostFrames;
                tracks.clear();
                eng.update(nullptr, false, !gs.recording, true);     // для текста: руки нет
                is.placeholder = L"Нет сигнала видео. Проверяю подключение...";
                if (lostFrames % 60 == 0 && src->open()) logInfo("Сигнал восстановлен");
                frame.release();
            }
            else {
                if (signalLost) { signalLost = false; lostFrames = 0; logInfo("Сигнал восстановлен"); }

                auto t0 = clock::now();
                float dt = std::chrono::duration<float>(t0 - prevTime).count();
                prevTime = t0;
                dt = std::min(std::max(dt, 1.0f / 120.0f), 0.2f);
                bool process = gov.shouldProcess();      // на слабом компьютере нейросети работают через кадр

                if (process) {
                    // === 2. ВЕДЁМ НАЙДЕННЫЕ РУКИ ===
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

                    for (size_t i = 0; i < tracks.size(); ++i)          // дубликаты (два трека на одной руке)
                        for (size_t j = i + 1; j < tracks.size(); ++j)
                            if (tracks[i].active && tracks[j].active &&
                                cv::norm(tracks[i].center() - tracks[j].center()) < 0.5f * tracks[i].size())
                                tracks[j].active = false;
                    removeInactive();

                    // === 3. ИЩЕМ НОВЫЕ РУКИ ===
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

                // === 4. РАСПОЗНАВАНИЕ: поза, движение, текст ===
                // Работаем с одной рукой - самой уверенной из найденных
                const HandTrack* use = nullptr;
                for (auto& t : tracks)
                    if (t.missedFrames == 0 && (!use || t.score > use->score)) use = &t;
                HandPoints pts{};
                if (use) pts = toPoints(*use);
                o = eng.update(use ? &pts : nullptr, use && use->handedness == 1, !gs.recording, process);
                handsCount = use ? (int)tracks.size() : 0;

                // --- запись образцов ---
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

                // === 5. РИСУЕМ РУКИ ===
                for (size_t i = 0; i < tracks.size(); ++i) {
                    cv::Scalar col = tracks[i].missedFrames > 0 ? cv::Scalar(128, 128, 128) : colors[i % colors.size()];
                    drawHand(frame, tracks[i], col, tracks[i].id);
                }

                // === 6. ВРЕМЯ ОБРАБОТКИ ===
                double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
                if (process) gov.report(ms);
                auto now = clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - statTime).count() >= 10000) {
                    logInfo("Среднее время кадра " + std::to_string((int)gov.avgMs()) + " мс, режим " + (gov.eco() ? "экономия" : "обычный"));
                    statTime = now;
                }
            }
        }

        // === ТЕКСТ И СТРОКА СОСТОЯНИЯ ===
        const std::wstring& txt = eng.text().text();
        if (wrapKey != txt) {                       // перенос строк считаем только при изменении текста
            wrapKey = txt;
            wrapped = wrapTextW(txt + L"_", 32, Gui::VIDEO_W - 30, 3);
        }
        is.lines = wrapped;

        std::wstring status;
        if (gs.running) {
            status = L"Рук: " + std::to_wstring(handsCount) + L"  |  " + (o.moving ? L"движение" : L"рука стоит") +
                     L"  |  " + std::to_wstring((int)(gov.fps() + 0.5)) + L" к/с";
            if (gov.eco()) status += L"  |  ЭКОНОМИЯ";
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
            else if (!eng.text().candidate().empty()) {
                wchar_t ch = letterFromCode(eng.text().candidate());
                if (ch) { is.candidate = std::wstring(1, ch); is.progress = eng.text().progress(); }
            }
        }
        else if (is.placeholder.empty()) is.placeholder = L"Выберите источник видео и нажмите «Начать»";

        render(frame, is);
        cv::waitKey(gs.running ? 1 : 15);
        if (iter > 10 && cv::getWindowProperty(WIN, cv::WND_PROP_VISIBLE) < 1) break;     // окно закрыто крестиком
    }

    if (gs.running) stopSession();
    speech.stop();
    logInfo("=== Завершение работы ===");
    logClose();
    return 0;
}
