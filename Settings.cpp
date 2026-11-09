#include "Settings.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace HandTracker {

    static std::string trim(const std::string& s) {
        std::size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return "";
        std::size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    static bool toInt(const std::string& v, int& out) {
        char* end = nullptr;
        long x = std::strtol(v.c_str(), &end, 10);
        if (end == v.c_str() || *end != 0) return false;
        out = (int)x; return true;
    }
    static bool toDouble(const std::string& v, double& out) {
        char* end = nullptr;
        double x = std::strtod(v.c_str(), &end);
        if (end == v.c_str() || *end != 0) return false;
        out = x; return true;
    }

    int Settings::load(const std::string& path) {
        std::ifstream in(path);
        if (!in) return 0;
        std::string line;
        int errors = 0;
        bool first = true;
        while (std::getline(in, line)) {
            if (first && line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
            first = false;
            line = trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            std::size_t eq = line.find('=');
            if (eq == std::string::npos) { ++errors; continue; }
            std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
            int i; double d; bool ok = true;
            if (k == "cameraIndex") ok = toInt(v, cameraIndex);
            else if (k == "videoFile") videoFile = v;
            else if (k == "maxHands") ok = toInt(v, maxHands);
            else if (k == "stableFrames") ok = toInt(v, stableFrames);
            else if (k == "spaceFrames") ok = toInt(v, spaceFrames);
            else if (k == "knnK") ok = toInt(v, knnK);
            else if (k == "knnMaxDistance") { ok = toDouble(v, d); if (ok) knnMaxDistance = (float)d; }
            else if (k == "minConfidence") { ok = toDouble(v, d); if (ok) minConfidence = (float)d; }
            else if (k == "targetFps") ok = toDouble(v, targetFps);
            else if (k == "speakEnabled") { ok = toInt(v, i); if (ok) speakEnabled = (i != 0); }
            else if (k == "speechRate") ok = toInt(v, speechRate);
            else if (k == "speechVolume") ok = toInt(v, speechVolume);
            else if (k == "samplesFile") samplesFile = v;
            else if (k == "dynSamplesFile") dynSamplesFile = v;
            else if (k == "mlpModel") mlpModel = v;
            else if (k == "dynMaxDistance") { ok = toDouble(v, d); if (ok) dynMaxDistance = (float)d; }
            else if (k == "dynMinConfidence") { ok = toDouble(v, d); if (ok) dynMinConfidence = (float)d; }
            else if (k == "moveOn") { ok = toDouble(v, d); if (ok) moveOn = (float)d; }
            else if (k == "moveOff") { ok = toDouble(v, d); if (ok) moveOff = (float)d; }
            else ok = false;                    // неизвестный ключ
            if (!ok) ++errors;
        }
        clamp();
        return errors;
    }

    bool Settings::save(const std::string& path) const {
        std::ofstream out(path);
        if (!out) return false;
        out << "# Настройки HandTalk\n";
        out << "cameraIndex=" << cameraIndex << "\n";
        out << "videoFile=" << videoFile << "\n";
        out << "maxHands=" << maxHands << "\n";
        out << "stableFrames=" << stableFrames << "\n";
        out << "spaceFrames=" << spaceFrames << "\n";
        out << "knnK=" << knnK << "\n";
        out << "knnMaxDistance=" << knnMaxDistance << "\n";
        out << "minConfidence=" << minConfidence << "\n";
        out << "targetFps=" << targetFps << "\n";
        out << "speakEnabled=" << (speakEnabled ? 1 : 0) << "\n";
        out << "speechRate=" << speechRate << "\n";
        out << "speechVolume=" << speechVolume << "\n";
        out << "samplesFile=" << samplesFile << "\n";
        out << "dynSamplesFile=" << dynSamplesFile << "\n";
        out << "mlpModel=" << mlpModel << "\n";
        out << "dynMaxDistance=" << dynMaxDistance << "\n";
        out << "dynMinConfidence=" << dynMinConfidence << "\n";
        out << "moveOn=" << moveOn << "\n";
        out << "moveOff=" << moveOff << "\n";
        return (bool)out;
    }

    void Settings::clamp() {
        cameraIndex = std::max(0, cameraIndex);
        maxHands = std::min(2, std::max(1, maxHands));
        stableFrames = std::min(60, std::max(2, stableFrames));
        spaceFrames = std::min(300, std::max(10, spaceFrames));
        knnK = std::min(15, std::max(1, knnK));
        knnMaxDistance = std::min(5.0f, std::max(0.05f, knnMaxDistance));
        minConfidence = std::min(1.0f, std::max(0.0f, minConfidence));
        targetFps = std::min(60.0, std::max(5.0, targetFps));
        speechRate = std::min(10, std::max(-10, speechRate));
        speechVolume = std::min(100, std::max(0, speechVolume));
        dynMaxDistance = std::min(5.0f, std::max(0.05f, dynMaxDistance));
        dynMinConfidence = std::min(1.0f, std::max(0.0f, dynMinConfidence));
        moveOn = std::min(0.5f, std::max(0.005f, moveOn));
        moveOff = std::min(moveOn, std::min(0.5f, std::max(0.001f, moveOff)));   // "стоит" всегда медленнее "движется"
    }

} // namespace HandTracker
