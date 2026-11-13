#include "VideoSource.h"
#include "ScreenCapture.h"
#include <objbase.h>

namespace HandTracker {

    // Озвучивание (SAPI) работает через COM на потоке программы. Закрывая камеру через DirectShow,
    // OpenCV может лишний раз вызвать CoUninitialize на этом же потоке, и после этого озвучивание молчит.
    // Поэтому перед каждой работой с камерой добавляем ссылку на COM заранее: лишняя ссылка ничему не мешает.
    static void keepComAlive() {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    }

    // === КАМЕРА ===
    CameraSource::~CameraSource() {
        keepComAlive();
        cap_.release();
    }

    bool CameraSource::open() {
        keepComAlive();
        cap_.release();
        keepComAlive();
        // DirectShow на Windows открывает камеру быстрее и стабильнее, чем MSMF
        if (!cap_.open(index_, cv::CAP_DSHOW)) cap_.open(index_);
        if (!cap_.isOpened()) return false;
        cap_.set(cv::CAP_PROP_FRAME_WIDTH, 1280);     // просим 1280x720; если камера не умеет - даст своё
        cap_.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
        cap_.set(cv::CAP_PROP_BUFFERSIZE, 1);         // меньше задержка
        return true;
    }

    bool CameraSource::read(cv::Mat& out) {
        if (!cap_.isOpened()) return false;
        cv::Mat f;
        if (!cap_.read(f) || f.empty()) return false;
        out = f;                                      // без зеркала: все источники дают одинаковую картинку,
                                                      // поэтому образцы жестов подходят к любому источнику
        return true;
    }

    std::vector<int> findCameras(int maxCount) {
        std::vector<int> found;
        for (int i = 0; i < maxCount; ++i) {
            keepComAlive();
            cv::VideoCapture c;
            if (c.open(i, cv::CAP_DSHOW) && c.isOpened()) found.push_back(i);
            c.release();
        }
        return found;
    }

    // === ФАЙЛ ===
    bool FileSource::open() {
        cap_.release();
        return cap_.open(path_) && cap_.isOpened();
    }

    bool FileSource::read(cv::Mat& out) {
        if (!cap_.isOpened()) return false;
        cv::Mat f;
        if (!cap_.read(f) || f.empty()) {
            cap_.set(cv::CAP_PROP_POS_FRAMES, 0);     // в начало
            if (!cap_.read(f) || f.empty()) return false;
        }
        out = f;
        return true;
    }

    // === ЭКРАН ===
    ScreenSource::ScreenSource(int x, int y, int w, int h) : x_(x), y_(y), w_(w), h_(h) {}
    ScreenSource::~ScreenSource() = default;

    bool ScreenSource::open() {
        if (w_ < 10 || h_ < 10) return false;
        cap_ = std::make_unique<ScreenCapture>(x_, y_, w_, h_);
        return true;
    }

    bool ScreenSource::read(cv::Mat& out) {
        if (!cap_) return false;
        out = cap_->grab();                            // общий буфер; main копирует при необходимости
        return !out.empty();
    }

} // namespace HandTracker
