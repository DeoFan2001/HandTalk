#include "VideoSource.h"
#include "ScreenCapture.h"

namespace HandTracker {

    // === КАМЕРА ===
    bool CameraSource::open() {
        cap_.release();
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
