#pragma once
// Performance.h - адаптация к слабому компьютеру
// Измеряет, сколько времени занимает обработка кадра (сглаженно). Если компьютер не успевает,
// включается режим "экономия": нейросети запускаются на каждом 2-м кадре, между ними рука
// показывается по прошлому результату. Когда запас появляется, возвращается обычный режим.
#include <deque>

namespace HandTracker {

    class PerformanceGovernor {
        double ema_ = 0;
        bool init_ = false;
        bool eco_ = false;
        int frame_ = 0;
        double targetMs_;       // сколько мс на кадр допустимо (1000 / целевой FPS)
        double alpha_;
    public:
        explicit PerformanceGovernor(double targetFps = 20.0, double alpha = 0.1)
            : targetMs_(1000.0 / targetFps), alpha_(alpha) {}

        // ms - время обработки последнего кадра (без ожидания камеры)
        void report(double ms) {
            ema_ = init_ ? ema_ + alpha_ * (ms - ema_) : ms;
            init_ = true;
            if (!eco_ && ema_ > targetMs_ * 0.9) eco_ = true;        // не успеваем - экономим
            else if (eco_ && ema_ < targetMs_ * 0.45) eco_ = false;  // большой запас - выходим (гистерезис)
        }

        // Нужно ли запускать нейросети на этом кадре
        bool shouldProcess() { ++frame_; return !eco_ || (frame_ % 2 == 0); }

        bool eco() const { return eco_; }
        double avgMs() const { return ema_; }
        double fps() const { return ema_ > 0 ? 1000.0 / ema_ : 0; }
    };

} // namespace HandTracker
