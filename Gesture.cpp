#include "Gesture.h"
#include "Alphabet.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

namespace HandTracker {

    // === ПРИЗНАКИ РУКИ ===
    bool normalizeHand(const HandPoints& pts, bool isLeft, Features& out) {
        const P3& w = pts[0];
        float maxD = 0;
        for (const P3& p : pts) {
            float dx = p.x - w.x, dy = p.y - w.y;
            maxD = std::max(maxD, std::sqrt(dx * dx + dy * dy));
        }
        if (maxD < 1e-6f) return false;
        for (int i = 0; i < 21; ++i) {
            float x = (pts[i].x - w.x) / maxD;
            float y = (pts[i].y - w.y) / maxD;
            float z = (pts[i].z - w.z) / maxD;
            if (isLeft) x = -x;
            out[i * 3] = x; out[i * 3 + 1] = y; out[i * 3 + 2] = z;
        }
        return true;
    }

    static float dist(const Features& a, const Features& b) {
        float s = 0;
        for (int i = 0; i < FEATURE_SIZE; ++i) { float d = a[i] - b[i]; s += d * d; }
        return std::sqrt(s / 21.0f);   // среднее по точкам, чтобы порог не зависел от числа признаков
    }

    // === kNN ===
    GestureResult KnnClassifier::classify(const Features& f) const {
        GestureResult r;
        if (samples_.empty()) return r;

        std::vector<std::pair<float, int>> d;
        d.reserve(samples_.size());
        for (std::size_t i = 0; i < samples_.size(); ++i)
            d.push_back({ dist(f, samples_[i].f), (int)i });
        int k = std::min<int>(k_, (int)d.size());
        std::partial_sort(d.begin(), d.begin() + k, d.end());

        if (d[0].first > maxDistance_) return r;      // слишком непохоже на всё, что знаем

        std::map<std::string, float> votes;           // голос = 1 / (расстояние + eps)
        float total = 0;
        for (int i = 0; i < k; ++i) {
            float wgt = 1.0f / (d[i].first + 0.01f);
            votes[samples_[d[i].second].code] += wgt;
            total += wgt;
        }
        for (auto& v : votes) {
            float c = v.second / total;
            if (c > r.confidence) { r.confidence = c; r.code = v.first; }
        }
        r.valid = !r.code.empty();
        return r;
    }

    // === CSV ===
    std::vector<Sample> loadSamples(const std::string& path, int* skipped) {
        std::vector<Sample> out;
        int bad = 0;
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            std::stringstream ss(line);
            std::string tok;
            Sample s;
            if (!std::getline(ss, tok, ';') || indexFromCode(tok) < 0) { ++bad; continue; }
            s.code = tok;
            int n = 0;
            bool ok = true;
            while (n < FEATURE_SIZE && std::getline(ss, tok, ';')) {
                char* end = nullptr;
                float v = std::strtof(tok.c_str(), &end);
                if (end == tok.c_str() || !std::isfinite(v)) { ok = false; break; }
                s.f[n++] = v;
            }
            if (!ok || n != FEATURE_SIZE) { ++bad; continue; }
            out.push_back(s);
        }
        if (skipped) *skipped = bad;
        return out;
    }

    bool appendSample(const std::string& path, const Sample& s) {
        std::ofstream out(path, std::ios::app);
        if (!out) return false;
        out << s.code;
        char buf[32];
        for (float v : s.f) { std::snprintf(buf, sizeof(buf), ";%.5f", v); out << buf; }
        out << "\n";
        return (bool)out;
    }

} // namespace HandTracker
