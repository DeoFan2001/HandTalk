#pragma once
// VideoSource.h - источники видео: камера, видеофайл, область экрана
// Всё остальное в программе работает с общим интерфейсом IFrameSource и не знает, откуда пришёл кадр.
#include <memory>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace HandTracker {

    class ScreenCapture;

    class IFrameSource {
    public:
        virtual ~IFrameSource() = default;
        virtual bool open() = 0;                  // false - источник недоступен
        virtual bool read(cv::Mat& out) = 0;      // false - кадра нет (сигнал потерян / файл кончился)
        virtual std::string name() const = 0;     // название для экрана и журнала
    };

    // === КАМЕРА ===
    class CameraSource : public IFrameSource {
        int index_;
        cv::VideoCapture cap_;
    public:
        explicit CameraSource(int index) : index_(index) {}
        ~CameraSource() override;
        bool open() override;
        bool read(cv::Mat& out) override;
        std::string name() const override { return "Камера " + std::to_string(index_); }
    };

    // Номера камер, которые удалось открыть (проверяются номера 0..maxCount-1).
    // Занятая другой программой камера в список не попадёт.
    std::vector<int> findCameras(int maxCount = 6);

    // === ВИДЕОФАЙЛ (по окончании начинается сначала) ===
    class FileSource : public IFrameSource {
        std::string path_;
        cv::VideoCapture cap_;
    public:
        explicit FileSource(std::string path) : path_(std::move(path)) {}
        bool open() override;
        bool read(cv::Mat& out) override;
        std::string name() const override { return "Файл " + path_; }
    };

    // === ОБЛАСТЬ ЭКРАНА (режим демонстрации) ===
    class ScreenSource : public IFrameSource {
        int x_, y_, w_, h_;
        std::unique_ptr<ScreenCapture> cap_;
    public:
        ScreenSource(int x, int y, int w, int h);
        ~ScreenSource() override;
        bool open() override;
        bool read(cv::Mat& out) override;
        std::string name() const override { return "Экран"; }
    };

} // namespace HandTracker
