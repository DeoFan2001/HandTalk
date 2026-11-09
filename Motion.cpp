#include "Motion.h"
#include "Alphabet.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace HandTracker {

    // === РАЗМЕР И СКОРОСТЬ ===
    float handSizeOf(const HandPoints& p) {
        float m = 0;
        for (const P3& q : p) {
            float dx = q.x - p[0].x, dy = q.y - p[0].y;
            m = std::max(m, std::sqrt(dx * dx + dy * dy));
        }
        return std::max(m, 1e-6f);
    }

    // Скорость считаем по запястью и кончикам пальцев: так заметно и движение всей руки,
    // и движение одних пальцев (у ладони точки почти не двигаются, среднее по всем точкам их "гасило" бы)
    float handMotion(const HandPoints& prev, const HandPoints& cur) {
        static const int idx[6] = { 0, 4, 8, 12, 16, 20 };
        float s = 0;
        for (int i : idx) {
            float dx = cur[i].x - prev[i].x, dy = cur[i].y - prev[i].y;
            s += std::sqrt(dx * dx + dy * dy);
        }
        return (s / 6.0f) / handSizeOf(cur);
    }

    // === ОТРЕЗОК -> ПРИЗНАКИ ===
    SeqFeatures makeSequence(const std::vector<SegFrame>& fr) {
        const int n = (int)fr.size();
        SeqFeatures out(SEQ_SIZE, 0.0f);
        if (n == 0) return out;

        int left = 0;
        float size = 0;
        for (const SegFrame& f : fr) { left += f.isLeft ? 1 : 0; size += handSizeOf(f.pts); }
        const bool mirror = left * 2 > n;          // левая рука зеркалится, как в обычных признаках
        size = std::max(size / n, 1e-6f);

        std::vector<float> rows((std::size_t)n * SEQ_FRAME_DIM, 0.0f);
        const P3 w0 = fr[0].pts[0];
        for (int i = 0; i < n; ++i) {
            Features f;
            if (!normalizeHand(fr[i].pts, mirror, f)) f.fill(0.0f);
            float* r = &rows[(std::size_t)i * SEQ_FRAME_DIM];
            for (int k = 0; k < FEATURE_SIZE; ++k) r[k] = f[k];
            float dx = (fr[i].pts[0].x - w0.x) / size;
            float dy = (fr[i].pts[0].y - w0.y) / size;
            if (mirror) dx = -dx;
            r[FEATURE_SIZE] = dx * TRAJ_WEIGHT;
            r[FEATURE_SIZE + 1] = dy * TRAJ_WEIGHT;
        }

        // приводим к SEQ_LEN шагам по времени
        for (int t = 0; t < SEQ_LEN; ++t) {
            float pos = (n == 1) ? 0.0f : (float)t * (n - 1) / (SEQ_LEN - 1);
            int lo = (int)std::floor(pos);
            int hi = std::min(lo + 1, n - 1);
            float a = pos - lo;
            for (int k = 0; k < SEQ_FRAME_DIM; ++k)
                out[(std::size_t)t * SEQ_FRAME_DIM + k] =
                    rows[(std::size_t)lo * SEQ_FRAME_DIM + k] * (1 - a) + rows[(std::size_t)hi * SEQ_FRAME_DIM + k] * a;
        }
        return out;
    }

    // === РАЗБИЕНИЕ НА ДВИЖЕНИЯ ===
    void MotionSegmenter::reset() {
        hasPrev_ = false; moving_ = false; fast_ = 0; still_ = 0;
        pre_.clear(); seg_.clear();
    }

    bool MotionSegmenter::finish(SeqFeatures& out, int trailing) {
        int n = (int)seg_.size() - trailing;        // хвост неподвижных кадров отбрасываем
        if (n < 1) n = (int)seg_.size();
        std::vector<SegFrame> s(seg_.begin(), seg_.begin() + n);
        moving_ = false; seg_.clear(); fast_ = 0; still_ = 0; pre_.clear();
        if ((int)s.size() < p_.minLen) return false;
        out = makeSequence(s);
        return true;
    }

    bool MotionSegmenter::update(const HandPoints* pts, bool isLeft, SeqFeatures& out) {
        if (!pts) {                                 // рука пропала: движение заканчивается
            bool ended = moving_ ? finish(out, 0) : false;
            hasPrev_ = false; fast_ = 0; pre_.clear();
            return ended;
        }
        float m = hasPrev_ ? handMotion(prev_, *pts) : 0.0f;
        prev_ = *pts;
        hasPrev_ = true;

        SegFrame fr;
        fr.pts = *pts;
        fr.isLeft = isLeft;

        if (!moving_) {
            pre_.push_back(fr);
            if (pre_.size() > 3) pre_.pop_front();
            fast_ = (m > p_.moveOn) ? fast_ + 1 : 0;
            if (fast_ >= p_.startFrames) {
                moving_ = true;
                seg_.assign(pre_.begin(), pre_.end());   // берём пару кадров до начала, чтобы не потерять старт
                still_ = 0;
            }
            return false;
        }

        seg_.push_back(fr);
        still_ = (m < p_.moveOff) ? still_ + 1 : 0;
        if (still_ >= p_.stopFrames || (int)seg_.size() >= p_.maxLen)
            return finish(out, still_);
        return false;
    }

    // === kNN ===
    static float seqDist(const SeqFeatures& a, const SeqFeatures& b) {
        float s = 0;
        for (std::size_t i = 0; i < a.size(); ++i) { float d = a[i] - b[i]; s += d * d; }
        return std::sqrt(s / (SEQ_LEN * 21.0f));       // та же шкала, что у обычных жестов (на точку и на шаг)
    }

    GestureResult SeqKnn::classify(const SeqFeatures& f) const {
        GestureResult r;
        if (samples_.empty() || f.size() != (std::size_t)SEQ_SIZE) return r;

        std::vector<std::pair<float, int>> d;
        d.reserve(samples_.size());
        for (std::size_t i = 0; i < samples_.size(); ++i)
            d.push_back({ seqDist(f, samples_[i].f), (int)i });
        int k = std::min<int>(k_, (int)d.size());
        std::partial_sort(d.begin(), d.begin() + k, d.end());

        if (d[0].first > maxDistance_) return r;

        std::map<std::string, float> votes;
        float total = 0;
        for (int i = 0; i < k; ++i) {
            float w = 1.0f / (d[i].first + 0.01f);
            votes[samples_[d[i].second].code] += w;
            total += w;
        }
        for (auto& v : votes) {
            float c = v.second / total;
            if (c > r.confidence) { r.confidence = c; r.code = v.first; }
        }
        r.valid = !r.code.empty();
        return r;
    }

    // === CSV ===
    std::vector<SeqSample> loadSeqSamples(const std::string& path, int* skipped) {
        std::vector<SeqSample> out;
        int bad = 0;
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            std::stringstream ss(line);
            std::string tok;
            SeqSample s;
            if (!std::getline(ss, tok, ';') || indexFromCode(tok) < 0) { ++bad; continue; }
            s.code = tok;
            s.f.reserve(SEQ_SIZE);
            bool ok = true;
            while (std::getline(ss, tok, ';')) {
                char* end = nullptr;
                float v = std::strtof(tok.c_str(), &end);
                if (end == tok.c_str() || !std::isfinite(v)) { ok = false; break; }
                s.f.push_back(v);
            }
            if (!ok || (int)s.f.size() != SEQ_SIZE) { ++bad; continue; }
            out.push_back(std::move(s));
        }
        if (skipped) *skipped = bad;
        return out;
    }

    bool appendSeqSample(const std::string& path, const SeqSample& s) {
        std::ofstream out(path, std::ios::app);
        if (!out) return false;
        out << s.code;
        char buf[32];
        for (float v : s.f) { std::snprintf(buf, sizeof(buf), ";%.4f", v); out << buf; }
        out << "\n";
        return (bool)out;
    }

} // namespace HandTracker
