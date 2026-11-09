#include "Trainer.h"
#include "Alphabet.h"
#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <set>

namespace HandTracker {

    // число с форматом -> широкая строка (только ASCII)
    static std::wstring num(const char* fmt, double v) {
        char b[64];
        std::snprintf(b, sizeof(b), fmt, v);
        return std::wstring(b, b + std::char_traits<char>::length(b));
    }
    static std::wstring num(int v) { return std::to_wstring(v); }

    static std::wstring ru(const std::string& code) {
        wchar_t ch = letterFromCode(code);
        return ch ? std::wstring(1, ch) : std::wstring(L"?");
    }

    // === РАЗДЕЛЕНИЕ НА ОБУЧАЮЩИЕ И ПРОВЕРОЧНЫЕ ===
    template <class S>
    static void split(const std::vector<S>& all, const TrainParams& p, std::vector<S>& train, std::vector<S>& test) {
        std::map<std::string, std::vector<std::size_t>> by;
        for (std::size_t i = 0; i < all.size(); ++i) by[all[i].code].push_back(i);
        for (auto& kv : by) {
            const auto& idx = kv.second;
            std::size_t nTest = 0;
            if ((int)idx.size() >= p.minForTest) nTest = std::max<std::size_t>(1, (std::size_t)(idx.size() * p.testPart));
            for (std::size_t j = 0; j < idx.size(); ++j)
                (j + nTest >= idx.size() ? test : train).push_back(all[idx[j]]);
        }
    }

    struct Eval {
        int total = 0, ok = 0, rejected = 0;
        std::map<std::string, std::pair<int, int>> per;                      // буква -> (верно, всего)
        std::map<std::pair<std::string, std::string>, int> wrong;            // (настоящая, ответ) -> сколько раз
    };

    // classify - функция "образец -> результат" на основе обучающей части
    template <class S>
    static Eval evaluate(const std::vector<S>& test, float minConf,
                         const std::function<GestureResult(const S&)>& classify) {
        Eval e;
        for (const S& s : test) {
            GestureResult r = classify(s);
            bool accepted = r.valid && r.confidence >= minConf;
            ++e.total;
            ++e.per[s.code].second;
            if (accepted && r.code == s.code) { ++e.ok; ++e.per[s.code].first; }
            else if (!accepted) ++e.rejected;
            else ++e.wrong[{ s.code, r.code }];
        }
        return e;
    }

    static void describe(const std::wstring& title, int samples, int letters, const std::vector<std::pair<std::string, int>>& few,
                         int minForTest, bool tested, const Eval& e, PartStats& st, std::wstring& out) {
        out += title + L": образцов " + num(samples) + L", букв " + num(letters) + L"\n";
        if (!few.empty()) {
            out += L"  мало образцов (меньше " + num(minForTest) + L", для них проверки нет): ";
            for (std::size_t i = 0; i < few.size(); ++i)
                out += (i ? L", " : L"") + ru(few[i].first) + L" - " + num(few[i].second);
            out += L"\n";
        }
        st.samples = samples;
        st.letters = letters;
        if (!tested || e.total == 0) {
            out += L"  проверить точность нельзя: слишком мало образцов\n";
            return;
        }
        st.tested = e.total;
        st.accuracy = (float)e.ok / (float)e.total;
        out += L"  точность на проверочной части: " + num("%.1f", st.accuracy * 100) + L"% (" + num(e.ok) + L" из " + num(e.total) + L")\n";
        if (e.rejected) out += L"  не распознано (ниже порогов): " + num(e.rejected) + L"\n";

        std::vector<std::pair<float, std::string>> worst;
        for (auto& kv : e.per) {
            float a = (float)kv.second.first / (float)kv.second.second;
            if (a < 1.0f) worst.push_back({ a, kv.first });
        }
        std::sort(worst.begin(), worst.end());
        if (!worst.empty()) {
            out += L"  хуже всего: ";
            for (std::size_t i = 0; i < worst.size() && i < 5; ++i)
                out += (i ? L", " : L"") + ru(worst[i].second) + L" " + num("%.0f", worst[i].first * 100) + L"%";
            out += L"\n";
        }
        std::vector<std::pair<int, std::pair<std::string, std::string>>> conf;
        for (auto& kv : e.wrong) conf.push_back({ kv.second, kv.first });
        std::sort(conf.begin(), conf.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        if (!conf.empty()) {
            out += L"  чаще всего путаются: ";
            for (std::size_t i = 0; i < conf.size() && i < 5; ++i)
                out += (i ? L", " : L"") + ru(conf[i].second.first) + L"->" + ru(conf[i].second.second) + L" (" + num(conf[i].first) + L")";
            out += L"\n";
        }
    }

    template <class S>
    static void fewList(const std::vector<S>& all, int minForTest, std::vector<std::pair<std::string, int>>& few, int& letters) {
        std::map<std::string, int> cnt;
        for (const S& s : all) ++cnt[s.code];
        letters = (int)cnt.size();
        for (auto& kv : cnt) if (kv.second < minForTest) few.push_back(kv);
    }

    TrainReport trainAndEvaluate(const std::vector<Sample>& stat, const std::vector<SeqSample>& dyn, const TrainParams& p) {
        TrainReport rep;
        std::wstring& out = rep.full;
        out = L"==== ОБУЧЕНИЕ ====\n";

        if (stat.empty() && dyn.empty()) {
            out += L"Образцов нет. Сначала запишите жесты (раздел для администраторов, кнопка «Запись»).\n";
            rep.brief = L"Образцов нет: сначала запишите жесты";
            return rep;
        }

        // --- позы ---
        if (!stat.empty()) {
            std::vector<Sample> tr, te;
            split(stat, p, tr, te);
            Eval e;
            if (!tr.empty() && !te.empty()) {
                KnnClassifier knn(p.k, p.maxDistance);
                knn.setSamples(tr);
                e = evaluate<Sample>(te, p.minConfidence, [&](const Sample& s) { return knn.classify(s.f); });
            }
            std::vector<std::pair<std::string, int>> few; int letters = 0;
            fewList(stat, p.minForTest, few, letters);
            describe(L"Позы (статические жесты)", (int)stat.size(), letters, few, p.minForTest, !te.empty(), e, rep.stat, out);
        }
        else out += L"Образцов поз нет.\n";

        // --- движения ---
        if (!dyn.empty()) {
            std::vector<SeqSample> tr, te;
            split(dyn, p, tr, te);
            Eval e;
            if (!tr.empty() && !te.empty()) {
                SeqKnn knn(p.k, p.dynMaxDistance);
                knn.setSamples(tr);
                e = evaluate<SeqSample>(te, p.dynMinConfidence, [&](const SeqSample& s) { return knn.classify(s.f); });
            }
            std::vector<std::pair<std::string, int>> few; int letters = 0;
            fewList(dyn, p.minForTest, few, letters);
            describe(L"Движения (динамические буквы)", (int)dyn.size(), letters, few, p.minForTest, !te.empty(), e, rep.dyn, out);
        }
        else out += L"Образцов движений нет.\n";

        // буква записана и как поза, и как движение - скорее всего, ошибка при записи
        std::set<std::string> a, b;
        for (auto& s : stat) a.insert(s.code);
        for (auto& s : dyn) b.insert(s.code);
        std::wstring both;
        for (auto& c : a) if (b.count(c)) both += (both.empty() ? L"" : L", ") + ru(c);
        if (!both.empty()) out += L"Внимание: эти буквы записаны и как поза, и как движение: " + both + L"\n";

        out += L"Образцы загружены в программу, распознавание уже работает с ними.\n";

        // --- строка для экрана ---
        auto part = [&](const wchar_t* name, const PartStats& s) {
            std::wstring t = std::wstring(name) + L" ";
            if (s.samples == 0) return t + L"-";
            if (s.accuracy < 0) return t + num(s.letters) + L" букв (без проверки)";
            return t + num("%.0f", s.accuracy * 100) + L"% (" + num(s.letters) + L" букв)";
        };
        rep.brief = L"Готово. " + part(L"Позы:", rep.stat) + L"; " + part(L"движения:", rep.dyn);
        return rep;
    }

} // namespace HandTracker
