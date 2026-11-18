#include "SentenceGrammarEngine.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace HandTracker {

    static std::unordered_set<std::wstring> wordSet(const wchar_t* s) {
        std::unordered_set<std::wstring> out;
        while (*s) {
            while (*s == L' ' || *s == L'\n' || *s == L'\r' || *s == L'\t') ++s;
            const wchar_t* st = s;
            while (*s && *s != L' ' && *s != L'\n' && *s != L'\r' && *s != L'\t') ++s;
            if (s > st) out.emplace(st, s);
        }
        return out;
    }

    static std::unordered_map<std::wstring, std::wstring> pairMap(const wchar_t* s) {
        std::unordered_map<std::wstring, std::wstring> out;
        for (const auto& tok : wordSet(s)) {
            size_t eq = tok.find(L'=');
            if (eq != std::wstring::npos) out.emplace(tok.substr(0, eq), tok.substr(eq + 1));
        }
        return out;
    }

    static std::unordered_map<std::wstring, std::vector<std::wstring>> listMap(const wchar_t* s) {
        std::unordered_map<std::wstring, std::vector<std::wstring>> out;
        for (const auto& tok : wordSet(s)) {
            size_t eq = tok.find(L'=');
            if (eq == std::wstring::npos) continue;
            std::vector<std::wstring> vals;
            size_t p = eq + 1;
            while (p <= tok.size()) {
                size_t c = tok.find(L',', p);
                vals.push_back(c == std::wstring::npos ? tok.substr(p) : tok.substr(p, c - p));
                if (c == std::wstring::npos) break;
                p = c + 1;
            }
            out.emplace(tok.substr(0, eq), std::move(vals));
        }
        return out;
    }

    static std::filesystem::path pathFromUtf8(const std::string& s) {
        if (s.empty()) return {};
        return std::filesystem::path(
            std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
    }

    static uint32_t popcount32(uint32_t x) {
        return (uint32_t)std::popcount(x);
    }

    static wchar_t toLowerRu(wchar_t c) {
        if (c >= L'А' && c <= L'Я') return c + (L'а' - L'А');
        if (c == L'Ё') return L'ё';
        if (c >= L'A' && c <= L'Z') return c + (L'a' - L'A');
        return c;
    }

    static std::wstring utf8SpanToWide(const char* data, size_t n, bool toLower = false) {
        std::wstring out;
        out.reserve(n);
        size_t i = 0;
        if (n >= 3 && (unsigned char)data[0] == 0xEF &&
            (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
            i = 3;
        }
        while (i < n) {
            unsigned char c = (unsigned char)data[i];
            uint32_t cp = 0;
            size_t len = 1;
            if (c < 0x80) {
                cp = c;
            } else if ((c & 0xE0) == 0xC0 && i + 1 < n && ((unsigned char)data[i + 1] & 0xC0) == 0x80) {
                cp = ((uint32_t)(c & 0x1F) << 6) | (uint32_t)((unsigned char)data[i + 1] & 0x3F);
                len = 2;
            } else if ((c & 0xF0) == 0xE0 && i + 2 < n &&
                       ((unsigned char)data[i + 1] & 0xC0) == 0x80 && ((unsigned char)data[i + 2] & 0xC0) == 0x80) {
                cp = ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)((unsigned char)data[i + 1] & 0x3F) << 6) |
                     (uint32_t)((unsigned char)data[i + 2] & 0x3F);
                len = 3;
            } else if ((c & 0xF8) == 0xF0 && i + 3 < n &&
                       ((unsigned char)data[i + 1] & 0xC0) == 0x80 &&
                       ((unsigned char)data[i + 2] & 0xC0) == 0x80 &&
                       ((unsigned char)data[i + 3] & 0xC0) == 0x80) {
                cp = ((uint32_t)(c & 0x07) << 18) | ((uint32_t)((unsigned char)data[i + 1] & 0x3F) << 12) |
                     ((uint32_t)((unsigned char)data[i + 2] & 0x3F) << 6) | (uint32_t)((unsigned char)data[i + 3] & 0x3F);
                len = 4;
            } else {
                ++i;
                continue;
            }
            i += len;
            if (cp == 0xFEFF) continue;
            if constexpr (sizeof(wchar_t) == 2) {
                if (cp <= 0xFFFF) {
                    wchar_t wc = (wchar_t)cp;
                    out.push_back(toLower ? toLowerRu(wc) : wc);
                } else if (cp <= 0x10FFFF) {
                    cp -= 0x10000;
                    out.push_back((wchar_t)(0xD800 + (cp >> 10)));
                    out.push_back((wchar_t)(0xDC00 + (cp & 0x3FF)));
                }
            } else {
                wchar_t wc = (wchar_t)cp;
                out.push_back(toLower ? toLowerRu(wc) : wc);
            }
        }
        return out;
    }

    static std::wstring utf8ToWide(const std::string& utf8) {
        return utf8SpanToWide(utf8.data(), utf8.size(), false);
    }

    static wchar_t toUpperRu(wchar_t c) {
        if (c >= L'а' && c <= L'я') return c - (L'а' - L'А');
        if (c == L'ё') return L'Ё';
        if (c >= L'a' && c <= L'z') return c - (L'a' - L'A');
        return c;
    }

    static wchar_t foldYoChar(wchar_t c) {
        if (c == L'ё') return L'е';
        if (c == L'Ё') return L'Е';
        return c;
    }

    static std::wstring foldYoStr(const std::wstring& s) {
        std::wstring res = s;
        for (wchar_t& c : res) c = foldYoChar(c);
        return res;
    }

    static std::wstring toLowerStr(const std::wstring& s) {
        std::wstring res = s;
        for (wchar_t& c : res) c = toLowerRu(c);
        return res;
    }

    static bool isDigitChar(wchar_t c) {
        return c >= L'0' && c <= L'9';
    }

    static bool isWordLetter(wchar_t c) {
        if ((c >= L'А' && c <= L'Я') || (c >= L'а' && c <= L'я') || c == L'Ё' || c == L'ё') return true;
        if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z')) return true;
        if (isDigitChar(c)) return true;
        return false;
    }

    static bool endsWithAny(const std::wstring& s, std::initializer_list<const wchar_t*> sufs) {
        for (const wchar_t* suf : sufs) {
            size_t sl = 0;
            while (suf[sl] != L'\0') ++sl;
            if (s.size() > sl && s.compare(s.size() - sl, sl, suf) == 0) return true;
        }
        return false;
    }

    static uint32_t computeCharMask(const std::wstring& w) {
        uint32_t mask = 0;
        for (wchar_t ch : w) {
            wchar_t c = foldYoChar(toLowerRu(ch));
            if (c >= L'а' && c <= L'я') {
                mask |= (1u << (uint32_t)(c - L'а'));
            }
        }
        return mask;
    }

    // Автоконвертация латинской раскладки QWERTY -> ЙЦУКЕН (если пользователь тестирует с клавиатуры)
    static wchar_t mapQwertyToRu(wchar_t c) {
        static const wchar_t* src = L"qwertyuiop[]asdfghjkl;'zxcvbnm,.`QWERTYUIOP{}ASDFGHJKL:\"ZXCVBNM<>~";
        static const wchar_t* dst = L"йцукенгшщзхъфывапролджэячсмитьбюёЙЦУКЕНГШЩЗХЪФЫВАПРОЛДЖЭЯЧСМИТЬБЮЁ";
        for (size_t i = 0; src[i]; ++i) {
            if (src[i] == c) return toLowerRu(dst[i]);
        }
        return toLowerRu(c);
    }

    // Проверка фонетической/клавиатурной/дактильной близости букв
    static bool areSimilarChars(wchar_t a, wchar_t b) {
        a = foldYoChar(a);
        b = foldYoChar(b);
        if (a == b) return true;
        static const wchar_t* pairs[] = {
            L"ао", L"еи", L"еэ", L"иы", L"ую", L"ая", L"ое",
            L"дт", L"бп", L"вф", L"гк", L"зс", L"жш", L"шщ", L"чщ", L"цс",
            // Соседние клавиши ЙЦУКЕН и похожие дактилемы РЖЯ
            L"чя", L"см", L"ми", L"ит", L"ть", L"бь", L"бю", L"жэ", L"хъ",
            L"фы", L"ыв", L"ва", L"ап", L"пр", L"ро", L"ол", L"лд", L"дж",
            L"йц", L"цу", L"ук", L"ке", L"ен", L"нг", L"гш", L"щз", L"зх",
            L"лм", L"нм", L"рл", L"вм", L"кх", L"сч", L"ос", L"пн", L"тм"
        };
        for (const wchar_t* p : pairs) {
            if ((a == p[0] && b == p[1]) || (a == p[1] && b == p[0])) return true;
        }
        return false;
    }

    SentenceGrammarEngine::SentenceGrammarEngine() {
        loadSupplementalLexicon();
        loadExactFixes();
        rebuildIndices();
    }

    static std::ifstream openDataFile(const std::string& dataDir, const char* filename) {
        if (!dataDir.empty()) {
            std::ifstream f(pathFromUtf8(dataDir) / filename, std::ios::binary);
            if (f.is_open()) return f;
        }
        return std::ifstream(pathFromUtf8(filename), std::ios::binary);
    }

    void SentenceGrammarEngine::loadSupplementalLexicon() {
        std::ifstream file = openDataFile(dataDir_, "lexicon_extra_ru.txt");
        if (!file.is_open()) return;
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            std::wstring w = utf8SpanToWide(line.data(), tab, true);
            int freq = 10000;
            try { freq = std::stoi(line.substr(tab + 1)); } catch (...) {}
            if (!w.empty()) {
                auto it = dictionary_.find(w);
                if (it == dictionary_.end() || freq > it->second) dictionary_[std::move(w)] = freq;
            }
        }
    }

    void SentenceGrammarEngine::loadExactFixes() {
        std::ifstream file = openDataFile(dataDir_, "fixes_ru.txt");
        if (!file.is_open()) return;
        exactFixes_.clear();
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            std::wstring key = utf8SpanToWide(line.data(), tab, true);
            std::wstring val = utf8SpanToWide(line.data() + tab + 1, line.size() - tab - 1, true);
            if (key.empty() || val.empty()) continue;
            std::vector<std::wstring> parts;
            size_t p = 0;
            while (p <= val.size()) {
                size_t c = val.find(L',', p);
                std::wstring tok = (c == std::wstring::npos) ? val.substr(p) : val.substr(p, c - p);
                if (!tok.empty()) parts.push_back(std::move(tok));
                if (c == std::wstring::npos) break;
                p = c + 1;
            }
            if (!parts.empty()) exactFixes_[std::move(key)] = std::move(parts);
        }
    }

    static int ruLetterIndex(wchar_t c) {
        c = foldYoChar(c);
        if (c >= L'а' && c <= L'я') return (int)(c - L'а');
        return -1;
    }

    void SentenceGrammarEngine::rebuildIndices() {
        dictEntries_.clear();
        bucketsByFirstLen_.assign(kNumRuLetters * kMaxBucketLen, {});
        bucketsBySecondLen_.assign(kNumRuLetters * kMaxBucketLen, {});
        dictEntries_.reserve(dictionary_.size());

        const int minBucketFreq = (dictionary_.size() >= 5000) ? 450 : 1;
        for (const auto& kv : dictionary_) {
            uint32_t idx = (uint32_t)dictEntries_.size();
            dictEntries_.push_back({kv.first, kv.second, computeCharMask(kv.first)});
            if (kv.first.size() >= 2 && kv.first.size() < kMaxBucketLen && kv.second >= minBucketFreq) {
                int fIdx = ruLetterIndex(kv.first[0]);
                int sIdx = ruLetterIndex(kv.first[1]);
                if (fIdx >= 0) {
                    bucketsByFirstLen_[(size_t)fIdx * kMaxBucketLen + kv.first.size()].push_back(idx);
                }
                if (sIdx >= 0) {
                    bucketsBySecondLen_[(size_t)sIdx * kMaxBucketLen + kv.first.size()].push_back(idx);
                }
            }
        }
    }

    bool SentenceGrammarEngine::loadDictionary(const std::string& dictPath) {
        std::ifstream file(pathFromUtf8(dictPath), std::ios::binary | std::ios::ate);
        if (!file.is_open()) return false;

        std::streamsize fileSize = file.tellg();
        if (fileSize <= 0) {
            dictionary_.clear();
            loadSupplementalLexicon();
            rebuildIndices();
            return false;
        }
        file.seekg(0, std::ios::beg);
        std::string buf((size_t)fileSize, '\0');
        if (!file.read(buf.data(), fileSize)) {
            return false;
        }

        dictionary_.clear();
        dictionary_.reserve(350000);

        size_t pos = 0;
        const size_t total = buf.size();
        if (total >= 3 && (unsigned char)buf[0] == 0xEF &&
            (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
            pos = 3;
        }

        while (pos < total) {
            size_t nl = buf.find('\n', pos);
            size_t lineEnd = (nl == std::string::npos) ? total : nl;
            size_t nextPos = (nl == std::string::npos) ? total : (nl + 1);

            if (lineEnd > pos && buf[lineEnd - 1] == '\r') {
                --lineEnd;
            }
            if (lineEnd > pos && buf[pos] != '#') {
                const char* linePtr = buf.data() + pos;
                const size_t lineLen = lineEnd - pos;
                const void* tabVoid = std::memchr(linePtr, '\t', lineLen);
                size_t wordLen = tabVoid ? (size_t)(static_cast<const char*>(tabVoid) - linePtr) : lineLen;

                size_t wsLeft = 0;
                while (wsLeft < wordLen && (linePtr[wsLeft] == ' ' || linePtr[wsLeft] == '\t')) ++wsLeft;
                while (wordLen > wsLeft && (linePtr[wordLen - 1] == ' ' || linePtr[wordLen - 1] == '\t')) --wordLen;

                if (wordLen > wsLeft) {
                    int freq = 1000;
                    if (tabVoid) {
                        size_t fPos = (size_t)(static_cast<const char*>(tabVoid) - linePtr) + 1;
                        int parsed = 0;
                        bool hasDigit = false;
                        while (fPos < lineLen) {
                            unsigned char dc = (unsigned char)linePtr[fPos++];
                            if (dc >= '0' && dc <= '9') {
                                hasDigit = true;
                                if (parsed < 100000000) {
                                    parsed = parsed * 10 + (int)(dc - '0');
                                }
                            } else if (hasDigit) {
                                break;
                            }
                        }
                        if (hasDigit) freq = parsed;
                    }

                    std::wstring word = utf8SpanToWide(linePtr + wsLeft, wordLen - wsLeft, true);
                    if (!word.empty()) {
                        auto it = dictionary_.find(word);
                        if (it == dictionary_.end() || freq > it->second) {
                            dictionary_[std::move(word)] = freq;
                        }
                    }
                }
            }
            pos = nextPos;
        }

        if (dictionary_.empty()) {
            loadSupplementalLexicon();
            rebuildIndices();
            return false;
        }

        dataDir_ = pathFromUtf8(dictPath).parent_path().string();
        loadSupplementalLexicon();
        loadExactFixes();
        rebuildIndices();
        return true;
    }

    bool SentenceGrammarEngine::loadBigrams(const std::string& bigramPath) {
        std::ifstream file(pathFromUtf8(bigramPath));
        if (!file.is_open()) return false;

        bigrams_.clear();
        bigramCount_ = 0;

        std::string line;
        bool firstLine = true;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (firstLine) {
                firstLine = false;
                if (line.size() >= 3 && (unsigned char)line[0] == 0xEF &&
                    (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) {
                    line.erase(0, 3);
                }
            }
            if (line.empty() || line[0] == '#') continue;

            std::wstring wline = utf8ToWide(line);
            size_t t1 = wline.find(L'\t');
            if (t1 == std::wstring::npos) continue;
            size_t t2 = wline.find(L'\t', t1 + 1);

            std::wstring w1 = toLowerStr(wline.substr(0, t1));
            std::wstring w2 = (t2 == std::wstring::npos)
                ? toLowerStr(wline.substr(t1 + 1))
                : toLowerStr(wline.substr(t1 + 1, t2 - t1 - 1));
            int weight = 50;
            if (t2 != std::wstring::npos) {
                int parsed = 0;
                bool hasDigit = false;
                for (size_t i = t2 + 1; i < wline.size(); ++i) {
                    if (wline[i] >= L'0' && wline[i] <= L'9') {
                        hasDigit = true;
                        if (parsed < 100000000) parsed = parsed * 10 + (int)(wline[i] - L'0');
                    } else if (hasDigit) {
                        break;
                    }
                }
                if (hasDigit) weight = parsed;
            }
            auto trimWs = [](std::wstring& s) {
                while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) s.erase(s.begin());
                while (!s.empty() && (s.back() == L' ' || s.back() == L'\t')) s.pop_back();
            };
            trimWs(w1);
            trimWs(w2);
            if (!w1.empty() && !w2.empty()) {
                auto& inner = bigrams_[w1];
                auto it = inner.find(w2);
                if (it == inner.end()) {
                    inner[w2] = weight;
                    ++bigramCount_;
                } else if (weight > it->second) {
                    it->second = weight;
                }
            }
        }
        return bigramCount_ > 0;
    }

    int SentenceGrammarEngine::getBigramWeight(const std::wstring& prev, const std::wstring& cur) const {
        if (prev.empty() || cur.empty() || bigrams_.empty()) return 0;
        auto it = bigrams_.find(prev);
        if (it == bigrams_.end()) {
            std::wstring fp = foldYoStr(prev);
            if (fp != prev) it = bigrams_.find(fp);
        }
        if (it == bigrams_.end()) return 0;
        auto it2 = it->second.find(cur);
        if (it2 != it->second.end()) return it2->second;
        std::wstring fc = foldYoStr(cur);
        if (fc != cur) {
            it2 = it->second.find(fc);
            if (it2 != it->second.end()) return it2->second;
        }
        return 0;
    }

    int SentenceGrammarEngine::editDistanceBounded(const std::wstring& s1, const std::wstring& s2, int maxDist) const {
        const size_t m = s1.size();
        const size_t n = s2.size();
        if (m == 0) return (int)n;
        if (n == 0) return (int)m;
        if (std::abs((int)m - (int)n) > maxDist) return maxDist + 1;

        int stackBuf[3 * 65];
        std::vector<int> heapBuf;
        int* buf = stackBuf;
        const size_t rowSize = n + 1;
        if (rowSize > 65) {
            heapBuf.resize(3 * rowSize);
            buf = heapBuf.data();
        }

        int* r0 = buf;
        int* r1 = buf + rowSize;
        int* r2 = buf + 2 * rowSize;

        for (size_t j = 0; j <= n; ++j) r1[j] = (int)j;

        for (size_t i = 1; i <= m; ++i) {
            r2[0] = (int)i;
            int rowMin = r2[0];
            const wchar_t c1 = s1[i - 1];
            for (size_t j = 1; j <= n; ++j) {
                const wchar_t c2 = s2[j - 1];
                int cost = (c1 == c2) ? 0 : 1;
                int v = r1[j] + 1;
                int ins = r2[j - 1] + 1;
                if (ins < v) v = ins;
                int sub = r1[j - 1] + cost;
                if (sub < v) v = sub;
                if (i > 1 && j > 1 && c1 == s2[j - 2] && s1[i - 2] == c2) {
                    int tr = r0[j - 2] + 1;
                    if (tr < v) v = tr;
                }
                r2[j] = v;
                if (v < rowMin) rowMin = v;
            }
            if (rowMin > maxDist) return maxDist + 1;
            int* tmp = r0;
            r0 = r1;
            r1 = r2;
            r2 = tmp;
        }
        return r1[n];
    }

    int SentenceGrammarEngine::editDistance(const std::wstring& s1, const std::wstring& s2) const {
        return editDistanceBounded(s1, s2, (int)(s1.size() + s2.size()));
    }

    static std::pair<std::wstring, int> tryCollapseDoubles(
        const std::wstring& w,
        const std::unordered_map<std::wstring, int>& dictionary,
        int minFreq)
    {
        std::vector<size_t> dupIndices;
        for (size_t i = 1; i < w.size(); ++i) {
            if (w[i] == w[i - 1]) {
                dupIndices.push_back(i);
            }
        }
        if (dupIndices.empty()) return {L"", 0};
        if (dupIndices.size() > 5) {
            std::wstring greedy;
            greedy.reserve(w.size());
            for (size_t i = 0; i < w.size(); ++i) {
                if (i > 0 && w[i] == w[i - 1]) continue;
                greedy.push_back(w[i]);
            }
            auto it = dictionary.find(greedy);
            if (it == dictionary.end() && greedy.find(L'ё') != std::wstring::npos) {
                it = dictionary.find(foldYoStr(greedy));
            }
            if (it != dictionary.end() && it->second >= minFreq) {
                return {greedy, it->second};
            }
            return {L"", 0};
        }

        std::wstring bestWord;
        int bestFreq = 0;
        const size_t totalMasks = (size_t)1 << dupIndices.size();
        for (size_t mask = 1; mask < totalMasks; ++mask) {
            std::wstring cand;
            cand.reserve(w.size());
            size_t dupPtr = 0;
            for (size_t i = 0; i < w.size(); ++i) {
                if (dupPtr < dupIndices.size() && i == dupIndices[dupPtr]) {
                    bool drop = ((mask >> dupPtr) & 1u) != 0;
                    ++dupPtr;
                    if (drop) continue;
                }
                cand.push_back(w[i]);
            }
            auto it = dictionary.find(cand);
            if (it == dictionary.end() && cand.find(L'ё') != std::wstring::npos) {
                it = dictionary.find(foldYoStr(cand));
            }
            if (it != dictionary.end() && it->second >= minFreq && it->second > bestFreq) {
                bestFreq = it->second;
                bestWord = cand;
            }
        }
        return {bestWord, bestFreq};
    }

    std::pair<std::wstring, double> SentenceGrammarEngine::findBestTypoCorrection(
        const std::wstring& target,
        const std::wstring& prevWord,
        int* outDist,
        int* outFreq) const
    {
        if (outDist) *outDist = 99;
        if (outFreq) *outFreq = 0;
        if (target.size() < 3) return {target, -1e9};

        auto ef = exactFixes_.find(target);
        if (ef != exactFixes_.end() && ef->second.size() == 1) {
            auto dIt = dictionary_.find(ef->second[0]);
            if (outDist) *outDist = 1;
            if (outFreq) *outFreq = (dIt != dictionary_.end()) ? dIt->second : 20000;
            return {ef->second[0], 45.0};
        }

        const int minTypoFreq = (dictionary_.size() >= 5000) ? 450 : 1;

        auto dbl = tryCollapseDoubles(target, dictionary_, minTypoFreq);
        if (!dbl.first.empty()) {
            if (outDist) *outDist = 1;
            if (outFreq) *outFreq = dbl.second;
            return {dbl.first, 40.0};
        }

        if (target.size() >= 4 && target.back() == L'ш') {
            std::wstring soft = target + L'ь';
            auto sIt = dictionary_.find(soft);
            if (sIt != dictionary_.end()) {
                if (outDist) *outDist = 1;
                if (outFreq) *outFreq = sIt->second;
                return {soft, 42.0};
            }
        }

        const int maxAllowedDist = (target.size() <= 4) ? 1 : 2;
        const size_t minLen = (target.size() > (size_t)maxAllowedDist + 1) ? (target.size() - maxAllowedDist) : 2;
        const size_t maxLen = target.size() + maxAllowedDist;
        const uint32_t targetMask = computeCharMask(target);
        const std::wstring foldedTarget = foldYoStr(target);

        double bestScore = -1e9;
        std::wstring bestWord = target;
        int bestDist = 99;
        int bestFreq = 0;

        auto evaluateEntry = [&](const DictEntry& cand) {
            const std::wstring& cw = cand.word;
            int cfreq = cand.freq;
            if (cfreq < minTypoFreq) return;

            int effMaxDist = (bestDist == 1 && bestScore > 32.0) ? 1 : maxAllowedDist;
            int lenDiff = std::abs((int)cw.size() - (int)target.size());
            if (lenDiff > effMaxDist) return;
            if (lenDiff == 2 && (target.size() < 6 || cfreq < 1800)) return;

            uint32_t d1 = targetMask & ~cand.charMask;
            uint32_t d2 = cand.charMask & ~targetMask;
            if ((int)popcount32(d1) > effMaxDist || (int)popcount32(d2) > effMaxDist) return;

            std::wstring foldedBuf;
            const std::wstring& foldedCand = (cw.find(L'ё') != std::wstring::npos) ? (foldedBuf = foldYoStr(cw)) : cw;
            int dist = editDistanceBounded(foldedTarget, foldedCand, effMaxDist);
            if (dist == 0 || dist > effMaxDist) return;
            if (dist == 2 && (target.size() < 6 || cfreq < 1800)) return;

            double score = std::log10((double)cfreq + 10.0) * 12.0 - dist * 28.0;

            if (foldYoChar(cw[0]) == foldYoChar(target[0])) score += 6.0;
            if (cw[0] == target[0]) score += 0.2;
            if (foldYoChar(cw.back()) == foldYoChar(target.back())) score += 5.0;
            if (cw.size() == target.size()) score += 3.0;

            if (dist == 1 && cw.size() == target.size()) {
                for (size_t k = 0; k < target.size(); ++k) {
                    if (foldedTarget[k] != foldedCand[k]) {
                        if (areSimilarChars(target[k], cw[k])) score += 8.0;
                        break;
                    }
                }
            } else if (dist == 1 && cw.size() == target.size() - 1) {
                score += 4.0;
            } else if (dist == 1 && cw.size() == target.size() + 1) {
                if (foldedCand.substr(0, foldedTarget.size()) == foldedTarget ||
                    foldedCand.substr(1) == foldedTarget) {
                    score += 6.0;
                }
            }

            if (!prevWord.empty()) {
                int bw = getBigramWeight(prevWord, cw);
                if (bw > 0) {
                    score += std::log10((double)bw + 1.0) * 6.0;
                }
            }

            if (score > bestScore) {
                bestScore = score;
                bestWord = cw;
                bestDist = dist;
                bestFreq = cfreq;
            }
        };

        std::vector<wchar_t> firstCharsToCheck;
        wchar_t fc0 = foldYoChar(target[0]);
        firstCharsToCheck.push_back(fc0);
        if (target.size() >= 2) {
            wchar_t fc1 = foldYoChar(target[1]);
            if (fc1 != fc0) firstCharsToCheck.push_back(fc1);
        }
        static const wchar_t alphabet[] = L"абвгдежзийклмнопрстуфхцчшщъыьэюя";
        for (wchar_t c : alphabet) {
            if (c != L'\0' && c != fc0 && areSimilarChars(fc0, c)) {
                if (std::find(firstCharsToCheck.begin(), firstCharsToCheck.end(), c) == firstCharsToCheck.end()) {
                    firstCharsToCheck.push_back(c);
                }
            }
        }

        uint32_t scannedFirstMask = 0;
        for (wchar_t fc : firstCharsToCheck) {
            int fIdx = ruLetterIndex(fc);
            if (fIdx < 0) continue;
            scannedFirstMask |= (1u << (uint32_t)fIdx);
            const size_t base = (size_t)fIdx * kMaxBucketLen;
            const size_t hiLen = std::min(maxLen, kMaxBucketLen - 1);
            for (size_t len = minLen; len <= hiLen; ++len) {
                const auto& bucket = bucketsByFirstLen_[base + len];
                for (uint32_t idx : bucket) {
                    evaluateEntry(dictEntries_[idx]);
                }
            }
        }

        // Поиск по 2-й букве слова: мгновенно находит пропущенную, переставленную или ошибочную 1-ю букву
        // (например: "ривет" -> "привет", "пасибо" -> "спасибо", "хривет" -> "привет")
        auto scanSecondBucket = [&](wchar_t sc, size_t len) {
            if (len >= kMaxBucketLen) return;
            int sIdx = ruLetterIndex(sc);
            if (sIdx < 0) return;
            const auto& bucket = bucketsBySecondLen_[(size_t)sIdx * kMaxBucketLen + len];
            for (uint32_t idx : bucket) {
                const DictEntry& cand = dictEntries_[idx];
                int candFIdx = ruLetterIndex(cand.word[0]);
                if (candFIdx >= 0 && (scannedFirstMask & (1u << (uint32_t)candFIdx))) {
                    continue; // уже проверено в bucketsByFirstLen_
                }
                evaluateEntry(cand);
            }
        };

        // 1-я буква пропущена (len == target.size() + 1) или первые две переставлены (len == target.size())
        scanSecondBucket(fc0, target.size() + 1);
        scanSecondBucket(fc0, target.size());
        // 1-я буква заменена на любую другую (len == target.size(), а при maxAllowedDist == 2 также +-1)
        if (target.size() >= 2) {
            wchar_t sc1 = foldYoChar(target[1]);
            if (sc1 != fc0) {
                scanSecondBucket(sc1, target.size());
                if (maxAllowedDist >= 2) {
                    if (target.size() > 2) scanSecondBucket(sc1, target.size() - 1);
                    scanSecondBucket(sc1, target.size() + 1);
                }
            }
        }

        if (outDist) *outDist = bestDist;
        if (outFreq) *outFreq = bestFreq;
        return {bestWord, bestScore};
    }

    std::vector<std::wstring> SentenceGrammarEngine::fixWordTokens(const std::wstring& rawWord, const std::wstring& prevWord) {
        std::wstring w = toLowerStr(rawWord);
        while (!w.empty() && w.front() == L'-') w.erase(w.begin());
        while (!w.empty() && w.back() == L'-') w.pop_back();
        if (w.empty()) return {};

        // Числа и смешанные цифро-буквенные токены не искажаем орфокоррекцией
        for (wchar_t c : w) {
            if (isDigitChar(c)) return {w};
        }

        // 0. Сначала схлопнем 3+ повтора одной буквы до 2 ("привееет" -> "привеет", "пппривеет" -> "ппривеет", "классс" -> "класс")
        std::wstring dedup;
        dedup.reserve(w.size());
        for (size_t i = 0; i < w.size(); ++i) {
            if (i >= 2 && w[i] == w[i - 1] && w[i] == w[i - 2]) continue;
            dedup.push_back(w[i]);
        }
        w = dedup;

        // 1. Прямой словарь частых разговорных склеек и опечаток
        auto exIt = exactFixes_.find(w);
        if (exIt != exactFixes_.end()) {
            return exIt->second;
        }

        // Если слово уже введено с дефисом (например "какие-нибудь")
        if (w.find(L'-') != std::wstring::npos) {
            auto directIt = dictionary_.find(w);
            if (directIt == dictionary_.end()) {
                std::vector<std::wstring> parts;
                size_t start = 0;
                bool hasExactFixPart = false;
                bool allNonEmpty = true;
                while (start <= w.size()) {
                    size_t dash = w.find(L'-', start);
                    std::wstring part = (dash == std::wstring::npos) ? w.substr(start) : w.substr(start, dash - start);
                    if (part.empty()) {
                        allNonEmpty = false;
                        break;
                    }
                    auto partEf = exactFixes_.find(part);
                    if (partEf != exactFixes_.end() && partEf->second.size() == 1) {
                        hasExactFixPart = true;
                        parts.push_back(partEf->second[0]);
                    } else {
                        parts.push_back(part);
                    }
                    if (dash == std::wstring::npos) break;
                    start = dash + 1;
                }
                if (allNonEmpty && hasExactFixPart && parts.size() >= 2) {
                    return parts;
                }
            }
            return {w};
        }

        auto lookupFreq = [&](const std::wstring& cand) -> int {
            auto it = dictionary_.find(cand);
            if (it != dictionary_.end()) return it->second;
            if (cand.find(L'ё') != std::wstring::npos) {
                auto itYo = dictionary_.find(foldYoStr(cand));
                if (itYo != dictionary_.end()) return itYo->second;
            }
            return 0;
        };

        int selfFreq = lookupFreq(w);

        // 2. Случайное удвоение одной или нескольких букв при задержке дактилемы
        // ("привеет" -> "привет", "сспасибо" -> "спасибо", "пппривеет" -> "привет")
        // Проверяем ДО разбиения склеек, но не трогаем уже существующие слова вроде "класс"/"анна"
        if (selfFreq == 0 && w.size() >= 4) {
            const int minDblFreq = (dictionary_.size() >= 5000) ? 450 : 1;
            auto dbl = tryCollapseDoubles(w, dictionary_, minDblFreq);
            if (!dbl.first.empty()) {
                return {dbl.first};
            }
        }

        // 3. Пропущенный мягкий знак в конце глагола 2-го лица ("гуляеш" -> "гуляешь")
        if (selfFreq == 0 && w.size() >= 4 && w.back() == L'ш') {
            std::wstring withSoft = w + L'ь';
            if (lookupFreq(withSoft) > 0) {
                return {withSoft};
            }
        }

        // 4. Если слово уже есть в словаре с достаточной частотой
        if (selfFreq >= 900) {
            return {w};
        }

        // 5. Динамическое программирование (Витерби) для разбиения слитных строк из N слов
        static const auto validLen1 = wordSet(
            L"а и в с к у о я");
        static const auto validLen2 = wordSet(
            L"не на но по из за от до ты мы вы он то же бы да ну ни во со ко ли ей им их её ее ух ой ай");
        static const auto validShortLeft = wordSet(
            L"а и в с к у о я не на но по из за от до ты мы вы он то же бы да ну ни во со ко что как так все всё они она "
            L"оно кто где тут там вот уже еще ещё нет или при про без под над для мой моя мои наш ваш");
        static const auto validShortRight = wordSet(
            L"я ты мы вы он же бы ли то да это как так все всё они она оно кто где тут там вот нет мне нам вам них ним "
            L"ней ему его мой моя дом сон год раз час");
        static const auto prefixPrepositions = wordSet(
            L"при по про за на от до из вы со во об под над без у о с в к ни не");
        auto looksLikeVerbAfterPrep = [&](const std::wstring& s) -> bool {
            static const auto commonVerbForms = wordSet(
                L"могу может можешь можем можете могут иду идет идёт иди хочу хочет хочешь знаю знает вижу видит делаю делает "
                L"скажу скажет позвоню позвонит приду придет уйду уйдет изошло пошло пошел пошёл пошла пойдет");
            if (commonVerbForms.count(s)) return true;
            return endsWithAny(s, {
                L"ться", L"тся", L"ешь", L"ёшь", L"ишь", L"ете", L"ёте", L"ите",
                L"ала", L"али", L"ало", L"ила", L"или", L"ило",
                L"ела", L"ели", L"ело", L"шло", L"шла", L"шли", L"шел", L"шёл"
            });
        };
        auto canFollowPrep = [&](const std::wstring& left, const std::wstring& right, int rightFreq) -> bool {
            if (!prefixPrepositions.count(left)) return true;
            int minRight = (dictionary_.size() >= 5000) ? 4500 : 1;
            if (rightFreq < minRight) return false;
            return left == L"не" || left == L"ни" || getBigramWeight(left, right) > 0 || !looksLikeVerbAfterPrep(right);
        };
        const int minSegFreq4 = (dictionary_.size() >= 5000) ? 500 : 1;

        const size_t n = w.size();
        struct SegCandidate {
            bool valid = false;
            int freq = 0;
            double score = -1e18;
            std::vector<std::wstring> tokens;
        };

        const size_t segCacheStride = 25;
        std::vector<uint8_t> segCached(n * segCacheStride, 0);
        std::vector<SegCandidate> segCache(n * segCacheStride);

        auto evalSegment = [&](size_t i, size_t len) -> const SegCandidate& {
            const size_t cacheIdx = i * segCacheStride + len;
            if (segCached[cacheIdx]) return segCache[cacheIdx];
            segCached[cacheIdx] = 1;
            SegCandidate& res = segCache[cacheIdx];
            if (len > 24) return res;
            std::wstring sub = w.substr(i, len);

            if (len >= 3 || i == 0) {
                auto ef = exactFixes_.find(sub);
                if (ef != exactFixes_.end()) {
                    if (len >= 3 || (i == 0 && ef->second.size() >= 2)) {
                        res.valid = true;
                        res.freq = 25000;
                        res.score = (double)len * (4.8 + 2.5) - 7.5;
                        res.tokens = ef->second;
                        return res;
                    }
                }
            }

            if (len >= 4 && sub.back() == L'ш' && lookupFreq(sub) == 0) {
                std::wstring withSoft = sub + L'ь';
                int sf = lookupFreq(withSoft);
                if (sf >= 300) {
                    res.valid = true;
                    res.freq = sf;
                    res.score = (double)len * (std::log10((double)std::max(sf, 10)) + 2.5) - 9.0;
                    res.tokens = {withSoft};
                    return res;
                }
            }

            if (len >= 5 && lookupFreq(sub) == 0) {
                auto dblSeg = tryCollapseDoubles(sub, dictionary_, std::max(800, minSegFreq4));
                if (!dblSeg.first.empty()) {
                    res.valid = true;
                    res.freq = dblSeg.second;
                    res.score = (double)dblSeg.first.size() * (std::log10((double)std::max(dblSeg.second, 10)) + 2.5) - 9.0;
                    res.tokens = {dblSeg.first};
                    return res;
                }
            }

            int freq = lookupFreq(sub);

            if (len == 1) {
                if (validLen1.find(sub) == validLen1.end()) return res;
                if (sub != L"я" && i + len == n) return res;
                freq = std::max(freq, 25000);
            } else if (len == 2) {
                if (validLen2.find(sub) == validLen2.end()) return res;
                static const auto forbidEndLen2 = wordSet(
                    L"не ни на по из за от до во со ко");
                if (i + len == n && forbidEndLen2.count(sub)) return res;
                freq = std::max(freq, 15000);
            } else if (len == 3) {
                bool inShortSet = (validShortLeft.find(sub) != validShortLeft.end()) ||
                                  (validShortRight.find(sub) != validShortRight.end());
                int minLen3 = (dictionary_.size() >= 5000) ? 2500 : 1;
                if (!inShortSet && freq < minLen3) return res;
                if (inShortSet) freq = std::max(freq, 10000);
            } else {
                if (freq < minSegFreq4) return res;
            }

            res.valid = true;
            res.freq = freq;
            res.score = (double)len * (std::log10((double)std::max(freq, 10)) + 2.5) - 8.5;
            res.tokens = {sub};
            return res;
        };

        struct DpNode {
            bool reached = false;
            double score = -1e18;
            std::vector<std::wstring> tokens;
        };

        std::vector<DpNode> dp(n + 1);
        dp[0].reached = true;
        dp[0].score = 0.0;

        if (n >= 3) {
            for (size_t i = 0; i < n; ++i) {
                if (!dp[i].reached) continue;
                const size_t maxSegLen = std::min<size_t>(24, n - i);
                for (size_t len = 1; len <= maxSegLen; ++len) {
                    if (i == 0 && len == n) continue;
                    if (len == 1 && !dp[i].tokens.empty() && dp[i].tokens.back().size() == 1) {
                        const std::wstring& prev1 = dp[i].tokens.back();
                        std::wstring cur1 = w.substr(i, 1);
                        if (prev1 != L"а" && prev1 != L"и") continue;
                        if (cur1 != L"я" && cur1 != L"у" && cur1 != L"в" && cur1 != L"с" && cur1 != L"к") continue;
                    }

                    const SegCandidate& seg = evalSegment(i, len);
                    if (!seg.valid) continue;

                    if (!dp[i].tokens.empty() && !seg.tokens.empty() &&
                        !canFollowPrep(dp[i].tokens.back(), seg.tokens.front(), seg.freq)) {
                        continue;
                    }

                    double bigramBonus = 0.0;
                    const std::wstring& pTok = dp[i].tokens.empty() ? prevWord : dp[i].tokens.back();
                    if (!pTok.empty() && !seg.tokens.empty()) {
                        int bw = getBigramWeight(pTok, seg.tokens.front());
                        if (bw > 0) bigramBonus = std::log10((double)bw + 1.0) * 2.5;
                    }

                    double candScore = dp[i].score + seg.score + bigramBonus;
                    if (!dp[i + len].reached || candScore > dp[i + len].score) {
                        dp[i + len].reached = true;
                        dp[i + len].score = candScore;
                        dp[i + len].tokens = dp[i].tokens;
                        dp[i + len].tokens.insert(dp[i + len].tokens.end(), seg.tokens.begin(), seg.tokens.end());
                    }
                }
            }
        }

        // 6. Оценка исправления всего слова целиком как одного слова с опечаткой
        int wholeDist = 99;
        int wholeFreq = 0;
        std::pair<std::wstring, double> wholeFix = {w, -1e9};
        if (selfFreq == 0) {
            wholeFix = findBestTypoCorrection(w, prevWord, &wholeDist, &wholeFreq);
        }
        bool hasWholeFix = (selfFreq == 0 && wholeFix.second > -1e8 && wholeFix.first != w);

        if (dp[n].reached && dp[n].tokens.size() >= 2) {
            bool unsafePrefixChop = (selfFreq >= 100 &&
                                     prefixPrepositions.count(dp[n].tokens.front()) &&
                                     dp[n].tokens.front() != L"не" && dp[n].tokens.front() != L"ни" &&
                                     getBigramWeight(dp[n].tokens[0], dp[n].tokens[1]) == 0);
            double selfScore = (selfFreq > 0)
                ? ((double)n * (std::log10((double)std::max(selfFreq, 10)) + 2.5) - 8.5)
                : -1e18;
            if (!unsafePrefixChop && (selfFreq == 0 || dp[n].score > selfScore + 6.0)) {
                // Защита от ложного разбиения одиночной опечатки на два случайных коротких слова
                // (например, если нет явной связки местоимения/предлога или биграммы, а wholeFix — уверенный 1-edit)
                bool dubiousShortSplit = (dp[n].tokens.size() == 2 &&
                                          dp[n].tokens[0].size() <= 3 &&
                                          dp[n].tokens[1].size() <= 3 &&
                                          !validLen1.count(dp[n].tokens[0]) &&
                                          !validLen2.count(dp[n].tokens[0]) &&
                                          !validShortLeft.count(dp[n].tokens[0]) &&
                                          !validShortRight.count(dp[n].tokens[1]) &&
                                          getBigramWeight(dp[n].tokens[0], dp[n].tokens[1]) == 0);
                bool dubiousPrepInterjSplit = (dp[n].tokens.size() == 2 &&
                                               dp[n].tokens[0].size() == 1 &&
                                               prefixPrepositions.count(dp[n].tokens[0]) &&
                                               dp[n].tokens[0] != L"я" &&
                                               (dp[n].tokens[1] == L"привет" || dp[n].tokens[1] == L"здравствуйте" ||
                                                dp[n].tokens[1] == L"спасибо" || dp[n].tokens[1] == L"пожалуйста"));
                if ((dubiousShortSplit || dubiousPrepInterjSplit) && hasWholeFix && wholeDist == 1 && wholeFreq >= 10000) {
                    return {wholeFix.first};
                }
                return dp[n].tokens;
            }
        }

        if (selfFreq > 0) {
            return {w};
        }

        // 7. Гибридное разбиение: [точный DP-префикс] + [слово с опечаткой] + [точный DP-суффикс]
        if (n >= 4 && n <= 64) {
            std::vector<DpNode> dpBack(n + 1);
            dpBack[n].reached = true;
            dpBack[n].score = 0.0;

            for (size_t j = n; j > 0; --j) {
                if (!dpBack[j].reached) continue;
                const size_t maxSegLen = std::min<size_t>(24, j);
                for (size_t len = 1; len <= maxSegLen; ++len) {
                    size_t i = j - len;
                    if (i == 0 && len == n) continue;
                    const SegCandidate& seg = evalSegment(i, len);
                    if (!seg.valid) continue;

                    if (!seg.tokens.empty() && !dpBack[j].tokens.empty() &&
                        !canFollowPrep(seg.tokens.back(), dpBack[j].tokens.front(), lookupFreq(dpBack[j].tokens.front()))) {
                        continue;
                    }

                    double bigramBonus = 0.0;
                    if (!seg.tokens.empty() && !dpBack[j].tokens.empty()) {
                        int bw = getBigramWeight(seg.tokens.back(), dpBack[j].tokens.front());
                        if (bw > 0) bigramBonus = std::log10((double)bw + 1.0) * 2.5;
                    }

                    double candScore = seg.score + dpBack[j].score + bigramBonus;
                    if (!dpBack[i].reached || candScore > dpBack[i].score) {
                        dpBack[i].reached = true;
                        dpBack[i].score = candScore;
                        dpBack[i].tokens = seg.tokens;
                        dpBack[i].tokens.insert(dpBack[i].tokens.end(), dpBack[j].tokens.begin(), dpBack[j].tokens.end());
                    }
                }
            }

            double bestHybridScore = -1e18;
            std::vector<std::wstring> bestHybridTokens;

            for (size_t i = 0; i < n; ++i) {
                if (!dp[i].reached) continue;
                const size_t maxEnd = std::min<size_t>(n, i + 20);
                for (size_t j = i + 3; j <= maxEnd; ++j) {
                    if (i == 0 && j == n) continue;
                    if (!dpBack[j].reached) continue;
                    const double optimisticMaxMid = 65.0 + (double)(j - i) * 4.5 - 8.5;
                    if (dp[i].score + optimisticMaxMid + dpBack[j].score <= bestHybridScore) continue;

                    std::wstring mid = w.substr(i, j - i);
                    const std::wstring& midPrev = dp[i].tokens.empty() ? prevWord : dp[i].tokens.back();
                    auto midFix = findBestTypoCorrection(mid, midPrev);
                    double minMidScore = (mid.size() == 3) ? 26.0 : 18.0;
                    if (midFix.second <= minMidScore || midFix.first == mid) continue;

                    if (!dp[i].tokens.empty() && prefixPrepositions.count(dp[i].tokens.back())) {
                        int midFreq = lookupFreq(midFix.first);
                        int minRight = (dictionary_.size() >= 5000) ? 4500 : 1;
                        if (midFreq < minRight) continue;
                    }

                    double midSegScore = midFix.second + (double)(j - i) * 4.5 - 8.5;
                    double totalScore = dp[i].score + midSegScore + dpBack[j].score;
                    if (totalScore > bestHybridScore) {
                        bestHybridScore = totalScore;
                        bestHybridTokens = dp[i].tokens;
                        bestHybridTokens.push_back(midFix.first);
                        bestHybridTokens.insert(bestHybridTokens.end(), dpBack[j].tokens.begin(), dpBack[j].tokens.end());
                    }
                }
            }

            if (!bestHybridTokens.empty()) {
                double wholeTotalScore = wholeFix.second + (double)n * 4.5 - 8.5;
                if (!hasWholeFix || (wholeFix.second < 22.0 && bestHybridScore > wholeTotalScore + 4.0)) {
                    return bestHybridTokens;
                }
            }
        }

        if (hasWholeFix) {
            return {wholeFix.first};
        }

        return {w};
    }

    void SentenceGrammarEngine::rebuildSentence() {
        processedTokens_ = rawTokens_;
        ruleLogs_.clear();

        if (processedTokens_.empty()) {
            cachedFinalSentence_.clear();
            return;
        }

        auto lookupFreq = [&](const std::wstring& cand) -> int {
            auto it = dictionary_.find(cand);
            if (it != dictionary_.end()) return it->second;
            if (cand.find(L'ё') != std::wstring::npos) {
                auto itYo = dictionary_.find(foldYoStr(cand));
                if (itYo != dictionary_.end()) return itYo->second;
            }
            return 0;
        };

        auto logChange = [&](std::wstring& target, const std::wstring& replacement, const std::wstring& desc) {
            if (target == replacement) return;
            std::wstring old = target;
            target = replacement;
            ruleLogs_.push_back({old, target, desc});
        };

        // 1. Склейка через дефис для неопределённых местоимений и наречий
        static const auto hyphenPrefixes = wordSet(
            L"какой какая какое какие чей кто что кого кому чего чему где куда откуда когда как почему зачем сколько");
        static const auto hyphenToPrefixes = wordSet(
            L"кто кого кому чего чему где куда откуда когда почему зачем какой какая какое какие");
        std::vector<bool> explicitCommas = explicitCommaAfter_;
        if (explicitCommas.size() < processedTokens_.size()) {
            explicitCommas.resize(processedTokens_.size(), false);
        }

        std::vector<std::wstring> mergedTokens;
        std::vector<bool> mergedCommas;
        mergedTokens.reserve(processedTokens_.size());
        mergedCommas.reserve(processedTokens_.size());
        for (size_t i = 0; i < processedTokens_.size(); ++i) {
            const std::wstring& cur = processedTokens_[i];
            if ((cur == L"нибудь" || cur == L"либо") && !mergedTokens.empty() &&
                !mergedCommas.back() && hyphenPrefixes.count(mergedTokens.back())) {
                std::wstring prev = mergedTokens.back();
                mergedTokens.back() = prev + L"-" + cur;
                mergedCommas.back() = explicitCommas[i];
                ruleLogs_.push_back({prev + L" " + cur, mergedTokens.back(), L"Дефисное написание с частицей «-" + cur + L"»"});
            } else if (cur == L"то" && !mergedTokens.empty() && !mergedCommas.back() &&
                       (hyphenToPrefixes.count(mergedTokens.back()) ||
                        (mergedTokens.back() == L"что" && (mergedTokens.size() < 2 || mergedTokens[mergedTokens.size() - 2] != L"если")))) {
                std::wstring prev = mergedTokens.back();
                mergedTokens.back() = prev + L"-то";
                mergedCommas.back() = explicitCommas[i];
                ruleLogs_.push_back({prev + L" " + cur, mergedTokens.back(), L"Дефисное написание с частицей «-то»"});
            } else if (cur == L"кое" && i + 1 < processedTokens_.size() &&
                       !explicitCommas[i] && hyphenPrefixes.count(processedTokens_[i + 1])) {
                std::wstring joined = cur + L"-" + processedTokens_[i + 1];
                mergedTokens.push_back(joined);
                mergedCommas.push_back(explicitCommas[i + 1]);
                ruleLogs_.push_back({cur + L" " + processedTokens_[i + 1], joined, L"Дефисное написание с частицей «кое-»"});
                ++i;
            } else {
                mergedTokens.push_back(cur);
                mergedCommas.push_back(explicitCommas[i]);
            }
        }
        processedTokens_ = std::move(mergedTokens);
        explicitCommas = std::move(mergedCommas);

        // Таблицы для грамматического согласования РЖЯ -> русский язык
        static const auto modalVerbs = wordSet(
            L"хочу хочешь хочет хотим хотите хотят хотел хотела хотели хотеть могу можешь может можем можете могут мог "
            L"могла могли буду будешь будет будем будете будут надо нужно можно нельзя должен должна должны люблю любишь "
            L"любит любим любите любят начал начала начали стал стала стали пошел пошла пошли пошёл пойти иду идешь идёшь "
            L"идет идёт идем идём идете идёте идут еду едешь едет едем едете едут поехал поехала поехали");

        static const auto motionDesireVerbs = wordSet(
            L"хочу хочешь хочет хотим хотите хотят хотел хотела хотели хотеть иду идешь идёшь идет идёт идем идём идете "
            L"идёте идут идти пошел пошёл пошла пошли пойду пойдешь пойдёшь пойдет пойдёт пойдем пойдём пойдете пойдёте "
            L"пойдут пойти еду едешь едет едем едете едут ехать поехал поехала поехали поеду поедешь поедет поедем "
            L"поедете поедут поехать пришел пришёл пришла пришли приду придешь придёшь придет придёт придем придём "
            L"придете придёте придут прийти приехал приехала приехали приехать вернулся вернулась вернулись вернуться "
            L"ушел ушёл ушла ушли уйти уехал уехала уехали уехать зашел зашёл зашла зашли зайти вошел вошёл вошла вошли "
            L"войти сел села сели сесть положил положила положить поставил поставила поставить лечу летишь летит летим "
            L"летите летят лететь полечу полетел полетела бегу спешу собираюсь надо нужно можно");

        static const auto toInfinitive = pairMap(
            L"сказал=сказать сказала=сказать сказали=сказать скажу=сказать скажешь=сказать сделал=сделать сделала=сделать "
            L"делаю=делать делаешь=делать делал=делать пошел=пойти пошёл=пойти пошла=пойти пошли=пойти ходил=ходить "
            L"ходила=ходить иду=идти гулял=гулять гуляла=гулять гуляю=гулять гуляешь=гулять купил=купить купила=купить "
            L"куплю=купить узнал=узнать узнала=узнать знаю=знать знал=знать спросил=спросить спросила=спросить "
            L"спрошу=спросить помог=помочь помогла=помочь помогу=помочь помогаю=помогать посмотрел=посмотреть "
            L"смотрел=смотреть смотрю=смотреть работал=работать работаю=работать работаешь=работать говорил=говорить "
            L"говорю=говорить говоришь=говорить видел=видеть вижу=видеть понял=понять понимаю=понимать пришел=прийти "
            L"пришёл=прийти пришла=прийти приду=прийти звонил=звонить позвонил=позвонить позвоню=позвонить ел=есть "
            L"ела=есть пил=пить пила=пить спал=спать спала=спать сплю=спать жил=жить жила=жить живу=жить читал=читать "
            L"читаю=читать читаешь=читать читает=читать писал=писать пишу=писать пишешь=писать пишет=писать ждал=ждать "
            L"жду=ждать ждет=ждать ждёт=ждать отдыхал=отдыхать отдыхаю=отдыхать отдыхает=отдыхать делает=делать "
            L"работает=работать говорит=говорить гуляет=гулять поможет=помочь придет=прийти придёт=прийти пойдет=пойти "
            L"пойдёт=пойти идет=идти идёт=идти смотрит=смотреть видит=видеть знает=знать понимает=понимать думает=думать "
            L"звонит=звонить позвонит=позвонить купит=купить спросит=спросить едет=ехать ехал=ехать еду=ехать "
            L"думаю=думать думал=думать думала=думать думает=думать люблю=любить любил=любить любила=любить любит=любить "
            L"слышу=слышать слышал=слышать слышала=слышать слышит=слышать сижу=сидеть сидел=сидеть сидела=сидеть "
            L"сидит=сидеть стою=стоять стоял=стоять стояла=стоять стоит=стоять играю=играть играл=играть играла=играть "
            L"играет=играть учу=учить учил=учить учила=учить учит=учить учусь=учиться учился=учиться училась=учиться "
            L"учится=учиться ищу=искать искал=искать искала=искать ищет=искать покупаю=покупать покупал=покупать "
            L"покупала=покупать покупает=покупать");

        struct VerbConjugation {
            std::wstring ya, ty, on, my, vy, oni;
        };
        static const auto infinitiveConj = [] {
            std::unordered_map<std::wstring, VerbConjugation> m;
            for (const auto& kv : listMap(
                L"хотеть=хочу,хочешь,хочет,хотим,хотите,хотят мочь=могу,можешь,может,можем,можете,могут "
                L"знать=знаю,знаешь,знает,знаем,знаете,знают делать=делаю,делаешь,делает,делаем,делаете,делают "
                L"гулять=гуляю,гуляешь,гуляет,гуляем,гуляете,гуляют идти=иду,идешь,идет,идем,идете,идут "
                L"ехать=еду,едешь,едет,едем,едете,едут ходить=хожу,ходишь,ходит,ходим,ходите,ходят "
                L"говорить=говорю,говоришь,говорит,говорим,говорите,говорят "
                L"сказать=скажу,скажешь,скажет,скажем,скажете,скажут "
                L"понимать=понимаю,понимаешь,понимает,понимаем,понимаете,понимают "
                L"любить=люблю,любишь,любит,любим,любите,любят жить=живу,живешь,живет,живем,живете,живут "
                L"работать=работаю,работаешь,работает,работаем,работаете,работают "
                L"учиться=учусь,учишься,учится,учимся,учитесь,учатся "
                L"смотреть=смотрю,смотришь,смотрит,смотрим,смотрите,смотрят ждать=жду,ждешь,ждет,ждем,ждете,ждут "
                L"думать=думаю,думаешь,думает,думаем,думаете,думают видеть=вижу,видишь,видит,видим,видите,видят "
                L"слышать=слышу,слышишь,слышит,слышим,слышите,слышат помочь=помогу,поможешь,поможет,поможем,поможете,помогут "
                L"помогать=помогаю,помогаешь,помогает,помогаем,помогаете,помогают "
                L"прийти=приду,придешь,придет,придем,придете,придут пойти=пойду,пойдешь,пойдет,пойдем,пойдете,пойдут "
                L"звонить=звоню,звонишь,звонит,звоним,звоните,звонят "
                L"позвонить=позвоню,позвонишь,позвонит,позвоним,позвоните,позвонят "
                L"читать=читаю,читаешь,читает,читаем,читаете,читают писать=пишу,пишешь,пишет,пишем,пишете,пишут "
                L"есть=ем,ешь,ест,едим,едите,едят пить=пью,пьешь,пьет,пьем,пьете,пьют спать=сплю,спишь,спит,спим,спите,спят "
                L"отдыхать=отдыхаю,отдыхаешь,отдыхает,отдыхаем,отдыхаете,отдыхают сидеть=сижу,сидишь,сидит,сидим,сидите,сидят "
                L"стоять=стою,стоишь,стоит,стоим,стоите,стоят играть=играю,играешь,играет,играем,играете,играют "
                L"учить=учу,учишь,учит,учим,учите,учат искать=ищу,ищешь,ищет,ищем,ищете,ищут "
                L"покупать=покупаю,покупаешь,покупает,покупаем,покупаете,покупают")) {
                const auto& v = kv.second;
                m[kv.first] = {v[0], v[1], v[2], v[3], v[4], v[5]};
            }
            return m;
        }();

        // Сначала преобразуем устойчивые конструкции и глаголы (чтобы "хотеть" стало "хочу" до проверки предлога "в/на")
        for (size_t i = 0; i < processedTokens_.size(); ++i) {
            std::wstring& cur = processedTokens_[i];

            // Правило 1: "спасибо большой / большая" -> "спасибо большое"
            if (i > 0 && processedTokens_[i - 1] == L"спасибо" &&
                (cur == L"большой" || cur == L"большая" || cur == L"большие" ||
                 cur == L"огромный" || cur == L"огромная" || cur == L"огромные")) {
                bool isOgr = (cur == L"огромный" || cur == L"огромная" || cur == L"огромные");
                logChange(cur, isOgr ? L"огромное" : L"большое",
                          L"Согласование среднего рода со словом «спасибо»");
            }
            if (i + 1 < processedTokens_.size() && processedTokens_[i + 1] == L"спасибо" &&
                (cur == L"большой" || cur == L"большая" || cur == L"большие" ||
                 cur == L"огромный" || cur == L"огромная" || cur == L"огромные")) {
                bool isOgr = (cur == L"огромный" || cur == L"огромная" || cur == L"огромные");
                logChange(cur, isOgr ? L"огромное" : L"большое",
                          L"Согласование среднего рода со словом «спасибо»");
            }

            // Устойчивое приветствие/пожелание "добрый ночь" / "спокойный ночи" / "доброе утро" / "добрый день"
            if (i + 1 < processedTokens_.size() &&
                (processedTokens_[i + 1] == L"ночь" || processedTokens_[i + 1] == L"ночи")) {
                if (cur == L"добрый") {
                    logChange(cur, L"доброй", L"Согласование устойчивого пожелания «доброй ночи»");
                    logChange(processedTokens_[i + 1], L"ночи", L"Родительный падеж пожелания «доброй ночи»");
                } else if (cur == L"спокойный") {
                    logChange(cur, L"спокойной", L"Согласование устойчивого пожелания «спокойной ночи»");
                    logChange(processedTokens_[i + 1], L"ночи", L"Родительный падеж пожелания «спокойной ночи»");
                }
            }
            if (i + 1 < processedTokens_.size() && processedTokens_[i + 1] == L"утро" &&
                (cur == L"добрый" || cur == L"добрая" || cur == L"добрые")) {
                logChange(cur, L"доброе", L"Согласование приветствия «доброе утро»");
            }
            if (i + 1 < processedTokens_.size() &&
                (processedTokens_[i + 1] == L"день" || processedTokens_[i + 1] == L"вечер") &&
                (cur == L"добрая" || cur == L"доброе" || cur == L"добрые")) {
                logChange(cur, L"добрый", L"Согласование приветствия «добрый " + processedTokens_[i + 1] + L"»");
            }

            // Правило 2: Модальный глагол + глагол в прош./личной форме -> инфинитив
            if (i > 0) {
                std::wstring prevModal = processedTokens_[i - 1];
                if (prevModal == L"не" && i >= 2) prevModal = processedTokens_[i - 2];
                if (modalVerbs.count(prevModal)) {
                    auto infIt = toInfinitive.find(cur);
                    if (infIt != toInfinitive.end()) {
                        logChange(cur, infIt->second, L"Инфинитив после модального глагола");
                    }
                }
            }

            // Правило 3: Местоимение + инфинитив -> личная форма глагола
            if (i > 0) {
                std::wstring prev = processedTokens_[i - 1];
                if (prev == L"не" && i >= 2) {
                    prev = processedTokens_[i - 2];
                }
                auto conjIt = infinitiveConj.find(cur);
                if (conjIt != infinitiveConj.end()) {
                    std::wstring replaced;
                    if (prev == L"я") replaced = conjIt->second.ya;
                    else if (prev == L"ты") replaced = conjIt->second.ty;
                    else if (prev == L"он" || prev == L"она" || prev == L"оно") replaced = conjIt->second.on;
                    else if (prev == L"мы") replaced = conjIt->second.my;
                    else if (prev == L"вы") replaced = conjIt->second.vy;
                    else if (prev == L"они") replaced = conjIt->second.oni;

                    if (!replaced.empty()) {
                        logChange(cur, replaced, L"Спряжение глагола по местоимению");
                    }
                } else if (cur.size() >= 4 && cur.back() == L'л' &&
                           (prev == L"она" || prev == L"мы" || prev == L"вы" || prev == L"они") &&
                           lookupFreq(cur + L"е") == 0 && lookupFreq(cur + L"ом") == 0) {
                    static const auto sheolToBase = pairMap(
                        L"пошел=пош пошёл=пош пришел=приш пришёл=приш ушел=уш ушёл=уш вышел=выш зашел=заш "
                        L"зашёл=заш нашел=наш нашёл=наш прошел=прош прошёл=прош подошел=подош подошёл=подош шел=ш шёл=ш");
                    auto sb = sheolToBase.find(cur);
                    std::wstring stem = (sb != sheolToBase.end()) ? sb->second : cur;
                    std::wstring cand = stem + (prev == L"она" ? L"ла" : L"ли");
                    if (sb == sheolToBase.end()) cand = cur + (prev == L"она" ? L"а" : L"и");
                    if (lookupFreq(cand) > 0) {
                        logChange(cur, cand, L"Согласование глагола прошедшего времени с местоимением");
                    }
                }
            }
        }

        // Общие таблицы родов и неименяемых слов (используются в Правилах 4 и 5)
        static const auto mascExceptions = wordSet(
            L"папа дядя дедушка мужчина юноша коллега судья кофе");
        static const auto femThirdDecl = wordSet(
            L"ночь жизнь радость новость любовь дверь тетрадь площадь вещь кровать помощь речь осень память связь боль "
            L"часть цель роль болезнь модель соль мысль сеть очередь подпись область тень");
        static const auto neutExceptions = wordSet(
            L"время имя пламя знамя племя семя");
        static const auto pluralNouns = wordSet(
            L"дела слова друзья братья сыновья дети люди дома города глаза письма окна места времена имена новости "
            L"девушки книги руки ноги дни годы часы цветы родители студенты жесты машины школы улицы комнаты квартиры "
            L"магазины проблемы задачи вопросы ответы подруги деревья праздники телефоны вещи двери ночи деньги наушники "
            L"кроссовки каникулы очки");
        static const auto nonNounSkip = wordSet(
            L"сегодня вчера завтра дома туда сюда куда откуда всегда никогда иногда меня тебя себя его него хорошо плохо "
            L"можно нужно надо много мало сколько быстро медленно далеко близко рядом рано поздно тепло холодно весело "
            L"интересно приятно понятно точно конечно пожалуйста спасибо здравствуйте привет пока очень уже еще ещё тоже "
            L"также ночи");
        static const auto nonInflectExtra = wordSet(
            L"я ты он она оно мы вы они кто что всё все это то её ее их мне тебе ему ей нам вам им нас вас неё нее ним "
            L"них мой твой свой наш ваш этот тот какой один да нет не ни и а но или же бы ли вот там тут здесь где почему "
            L"зачем как столько несколько чтобы потому поэтому если хотя однако кстати например наверное короче кажется "
            L"видимо возможно извините простите нельзя ладно хватит пора сейчас давно недавно скоро потом обычно часто "
            L"редко важно удобно дорого дешево сложно легко трудно грустно прямо просто совсем почти только даже слишком "
            L"готов готова готовы рад рада рады должен должна должны согласен уверен уверена болен здоров занят свободен "
            L"прав права кофе метро такси кино пальто кафе радио фото меню шоссе видео аудио жюри");

        // Правило 4: Предложно-падежное и отрицательное управление (включая конструкцию «предлог + прилагательное + существительное»)
        enum class CaseTag { None, Gen, Dat, Acc, Inst, Prep };
        enum class GenderTag { Unknown, Masc, Fem, Neut };
        struct NounTag {
            CaseTag kase = CaseTag::None;
            GenderTag gender = GenderTag::Unknown;
            bool plural = false;
            bool animate = false;
        };
        std::vector<NounTag> nounTags(processedTokens_.size());

        struct InflectTarget {
            std::wstring form;
            GenderTag gender = GenderTag::Unknown;
            bool plural = false;
            bool animate = false;
        };
        auto nounMap = [](const wchar_t* s) {
            std::unordered_map<std::wstring, InflectTarget> out;
            for (const auto& tok : wordSet(s)) {
                size_t eq = tok.find(L'='), sl = tok.find(L'/', eq + 1);
                if (eq == std::wstring::npos || sl == std::wstring::npos) continue;
                InflectTarget it{tok.substr(eq + 1, sl - eq - 1), GenderTag::Unknown, false, false};
                for (size_t k = sl + 1; k < tok.size(); ++k) {
                    if (tok[k] == L'm') it.gender = GenderTag::Masc;
                    else if (tok[k] == L'f') it.gender = GenderTag::Fem;
                    else if (tok[k] == L'n') it.gender = GenderTag::Neut;
                    else if (tok[k] == L'p') it.plural = true;
                    else if (tok[k] == L'a') it.animate = true;
                }
                out.emplace(tok.substr(0, eq), std::move(it));
            }
            return out;
        };

        auto hasVelarOrSibilant = [](wchar_t ch) -> bool {
            return ch == L'г' || ch == L'к' || ch == L'х' ||
                   ch == L'ж' || ch == L'ш' || ch == L'щ' || ch == L'ч';
        };
        auto isSibilantOrTse = [](wchar_t ch) -> bool {
            return ch == L'ж' || ch == L'ш' || ch == L'щ' || ch == L'ч' || ch == L'ц';
        };
        auto isVowelLower = [](wchar_t ch) -> bool {
            return ch == L'а' || ch == L'е' || ch == L'ё' || ch == L'и' || ch == L'о' ||
                   ch == L'у' || ch == L'ы' || ch == L'э' || ch == L'ю' || ch == L'я';
        };
        auto hasDigitOrHyphen = [](const std::wstring& s) -> bool {
            for (wchar_t c : s) {
                if ((c >= L'0' && c <= L'9') || c == L'-') return true;
            }
            return false;
        };
        auto isNonInflectNounWord = [&](const std::wstring& s) -> bool {
            if (s.size() < 2 || hasDigitOrHyphen(s)) return true;
            if (nonNounSkip.count(s) || nonInflectExtra.count(s)) return true;
            if (!femThirdDecl.count(s) &&
                endsWithAny(s, {L"ть", L"ться", L"тся", L"чь", L"ешь", L"ишь", L"ете", L"ите"})) {
                return true;
            }
            if (s.size() >= 4) {
                std::wstring e2 = s.substr(s.size() - 2);
                if (e2 == L"ый" || e2 == L"ий" || e2 == L"ой" ||
                    e2 == L"ая" || e2 == L"яя" || e2 == L"ое" || e2 == L"ее" ||
                    e2 == L"ые" || e2 == L"ие") {
                    // Сохраняем существительные на -ие (сообщение, задание, приложение) и одушевлённые/исключения
                    if (!(e2 == L"ие" && s.size() >= 5 && lookupFreq(s.substr(0, s.size() - 1) + L"я") > 0)) {
                        return true;
                    }
                }
            }
            return false;
        };

        const int minRegFormFreq = (dictionary_.size() >= 5000) ? 100 : 1;
        auto inferRegularNounCase = [&](const std::wstring& w, CaseTag kase,
                                        std::wstring& outForm, NounTag& outTag) -> bool {
            outForm = w;
            if (isNonInflectNounWord(w) || mascExceptions.count(w) || neutExceptions.count(w)) {
                return false;
            }
            wchar_t last = w.back();
            std::wstring stem = w.substr(0, w.size() - 1);

            // Проверка уже просклонённой формы, чтобы не пересклонять дважды и выставить NounTag
            if (kase == CaseTag::Gen) {
                if (w.size() >= 4 && (w.compare(w.size() - 2, 2, L"ов") == 0 ||
                                      w.compare(w.size() - 2, 2, L"ев") == 0 ||
                                      w.compare(w.size() - 2, 2, L"ей") == 0)) {
                    // Не путаем с существительными м.р. в им.п. ед.ч. (музей, остров, лицей, юбилей)
                    bool looksLikeNomMasc = (last == L'й')
                        ? (lookupFreq(stem + L"я") > 0 && lookupFreq(stem + L"ем") > 0)
                        : (lookupFreq(w + L"а") > 0 || lookupFreq(w + L"я") > 0 ||
                           lookupFreq(w + L"ом") > 0 || lookupFreq(w + L"ем") > 0);
                    if (!looksLikeNomMasc) {
                        outTag = {CaseTag::Gen, GenderTag::Unknown, true, false};
                        return true;
                    }
                }
                if ((last == L'а' || last == L'я') && stem.size() >= 3 &&
                    lookupFreq(stem) >= minRegFormFreq && lookupFreq(stem + L"ом") > 0) {
                    outTag = {CaseTag::Gen, GenderTag::Masc, false, false};
                    return true;
                }
                if (last == L'ы' || last == L'и') {
                    if (femThirdDecl.count(stem + L"ь")) {
                        outTag = {CaseTag::Gen, GenderTag::Fem, false, false};
                        return true;
                    }
                    wchar_t altNom = (last == L'и' && lookupFreq(stem + L"а") == 0) ? L'я' : L'а';
                    if (lookupFreq(stem + altNom) >= minRegFormFreq && lookupFreq(stem + L"е") > 0) {
                        outTag = {CaseTag::Gen, GenderTag::Fem, false, false};
                        return true;
                    }
                }
            } else if (kase == CaseTag::Acc && (last == L'у' || last == L'ю') && stem.size() >= 3) {
                wchar_t nomV = (last == L'у') ? L'а' : L'я';
                if (lookupFreq(stem + nomV) >= minRegFormFreq && lookupFreq(stem + L"е") > 0) {
                    outTag = {CaseTag::Acc, GenderTag::Fem, false, false};
                    return true;
                }
            } else if (kase == CaseTag::Prep && stem.size() >= 3) {
                if (last == L'е' && (lookupFreq(stem + L"а") >= minRegFormFreq || lookupFreq(stem + L"ом") > 0)) {
                    GenderTag g = (lookupFreq(stem + L"а") > 0 && lookupFreq(stem) == 0)
                                      ? GenderTag::Fem : GenderTag::Masc;
                    outTag = {CaseTag::Prep, g, false, false};
                    return true;
                }
                if (last == L'и' && (femThirdDecl.count(stem + L"ь") ||
                                     lookupFreq(stem + L"я") >= minRegFormFreq ||
                                     lookupFreq(stem + L"е") >= minRegFormFreq)) {
                    GenderTag g = (lookupFreq(stem + L"е") >= minRegFormFreq && lookupFreq(stem + L"я") == 0)
                                      ? GenderTag::Neut : GenderTag::Fem;
                    outTag = {CaseTag::Prep, g, false, false};
                    return true;
                }
            } else if (kase == CaseTag::Dat && stem.size() >= 3) {
                if ((last == L'у' || last == L'ю') &&
                    (lookupFreq(stem + L"ом") > 0 || lookupFreq(stem + L"ем") > 0)) {
                    outTag = {CaseTag::Dat, GenderTag::Masc, false, false};
                    return true;
                }
                if (last == L'е' && lookupFreq(stem + L"а") >= minRegFormFreq) {
                    outTag = {CaseTag::Dat, GenderTag::Fem, false, false};
                    return true;
                }
            } else if (kase == CaseTag::Inst && w.size() >= 4) {
                std::wstring e2 = w.substr(w.size() - 2);
                std::wstring base = w.substr(0, w.size() - 2);
                if (e2 == L"ом" && lookupFreq(base) >= minRegFormFreq) {
                    outTag = {CaseTag::Inst, GenderTag::Masc, false, false};
                    return true;
                }
                if ((e2 == L"ой" || e2 == L"ей") && lookupFreq(base + L"а") >= minRegFormFreq) {
                    outTag = {CaseTag::Inst, GenderTag::Fem, false, false};
                    return true;
                }
            }

            // Порождение формы из именительного падежа с проверкой по словарю и парадигме
            auto tryAccept = [&](const std::wstring& cand, GenderTag g) -> bool {
                if (lookupFreq(cand) >= minRegFormFreq) {
                    outForm = cand;
                    outTag = {kase, g, false, false};
                    return true;
                }
                return false;
            };

            if (femThirdDecl.count(w) && last == L'ь') {
                if (kase == CaseTag::Gen || kase == CaseTag::Dat || kase == CaseTag::Prep) {
                    return tryAccept(stem + L"и", GenderTag::Fem);
                }
                if (kase == CaseTag::Inst) return tryAccept(w + L"ю", GenderTag::Fem);
                if (kase == CaseTag::Acc) {
                    outTag = {CaseTag::Acc, GenderTag::Fem, false, false};
                    return true;
                }
                return false;
            }

            if (last == L'а') {
                // Отсеиваем глаголы прош.вр. ("была", "сказала") по наличию предложного -е
                if (dictionary_.size() >= 5000 && lookupFreq(stem + L"е") == 0) return false;
                if (kase == CaseTag::Gen) {
                    wchar_t gEnd = hasVelarOrSibilant(stem.back()) ? L'и' : L'ы';
                    return tryAccept(stem + gEnd, GenderTag::Fem);
                }
                if (kase == CaseTag::Dat || kase == CaseTag::Prep) return tryAccept(stem + L"е", GenderTag::Fem);
                if (kase == CaseTag::Acc) return tryAccept(stem + L"у", GenderTag::Fem);
                if (kase == CaseTag::Inst) {
                    return tryAccept(stem + L"ой", GenderTag::Fem) || tryAccept(stem + L"ей", GenderTag::Fem);
                }
            } else if (last == L'я') {
                bool isIya = (w.size() >= 4 && w.compare(w.size() - 2, 2, L"ия") == 0);
                if (kase == CaseTag::Gen) return tryAccept(stem + L"и", GenderTag::Fem);
                if (kase == CaseTag::Dat || kase == CaseTag::Prep) {
                    return tryAccept(stem + (isIya ? L"и" : L"е"), GenderTag::Fem);
                }
                if (kase == CaseTag::Acc) return tryAccept(stem + L"ю", GenderTag::Fem);
                if (kase == CaseTag::Inst) return tryAccept(stem + L"ей", GenderTag::Fem);
            } else if (last == L'о') {
                if (kase == CaseTag::Gen) return tryAccept(stem + L"а", GenderTag::Neut);
                if (kase == CaseTag::Dat) return tryAccept(stem + L"у", GenderTag::Neut);
                if (kase == CaseTag::Inst) return tryAccept(w + L"м", GenderTag::Neut);
                if (kase == CaseTag::Prep) return tryAccept(stem + L"е", GenderTag::Neut);
                if (kase == CaseTag::Acc) { outTag = {CaseTag::Acc, GenderTag::Neut, false, false}; return true; }
            } else if (last == L'е') {
                bool isIe = (w.size() >= 4 && w.compare(w.size() - 2, 2, L"ие") == 0);
                if (kase == CaseTag::Gen) return tryAccept(stem + L"я", GenderTag::Neut);
                if (kase == CaseTag::Dat) return tryAccept(stem + L"ю", GenderTag::Neut);
                if (kase == CaseTag::Inst) return tryAccept(w + L"м", GenderTag::Neut);
                if (kase == CaseTag::Prep) return tryAccept(stem + (isIe ? L"и" : L"е"), GenderTag::Neut);
                if (kase == CaseTag::Acc) { outTag = {CaseTag::Acc, GenderTag::Neut, false, false}; return true; }
            } else if (last == L'й') {
                if (kase == CaseTag::Gen) return tryAccept(stem + L"я", GenderTag::Masc);
                if (kase == CaseTag::Dat) return tryAccept(stem + L"ю", GenderTag::Masc);
                if (kase == CaseTag::Inst) return tryAccept(stem + L"ем", GenderTag::Masc);
                if (kase == CaseTag::Prep) return tryAccept(stem + L"е", GenderTag::Masc);
                if (kase == CaseTag::Acc) { outTag = {CaseTag::Acc, GenderTag::Masc, false, false}; return true; }
            } else if (last == L'ь') {
                // Исключаем глагольные формы (инфинитивы отсеяны выше) и проверяем падеж м.р.
                if (lookupFreq(stem + L"ем") == 0 && lookupFreq(stem + L"ём") == 0) return false;
                if (kase == CaseTag::Gen) return tryAccept(stem + L"я", GenderTag::Masc);
                if (kase == CaseTag::Dat) return tryAccept(stem + L"ю", GenderTag::Masc);
                if (kase == CaseTag::Inst) {
                    return tryAccept(stem + L"ем", GenderTag::Masc) || tryAccept(stem + L"ём", GenderTag::Masc);
                }
                if (kase == CaseTag::Prep) return tryAccept(stem + L"е", GenderTag::Masc);
            } else if (!isVowelLower(last) && last != L'ъ') {
                // Существительное м.р. на согласную: требуем и предложный (-е), и творительный (-ом/-ем),
                // чтобы не превратить глаголы прош.вр. ("устал" -> "устала") или наст.вр. ("стоит") в родительный падеж
                if (dictionary_.size() >= 5000 &&
                    (lookupFreq(w + L"е") == 0 || (lookupFreq(w + L"ом") == 0 && lookupFreq(w + L"ем") == 0))) {
                    return false;
                }
                if (kase == CaseTag::Gen) return tryAccept(w + L"а", GenderTag::Masc);
                if (kase == CaseTag::Dat) return tryAccept(w + L"у", GenderTag::Masc);
                if (kase == CaseTag::Inst) {
                    if (isSibilantOrTse(last)) {
                        return tryAccept(w + L"ем", GenderTag::Masc) || tryAccept(w + L"ом", GenderTag::Masc);
                    }
                    return tryAccept(w + L"ом", GenderTag::Masc);
                }
                if (kase == CaseTag::Prep) return tryAccept(w + L"е", GenderTag::Masc);
                if (kase == CaseTag::Acc) { outTag = {CaseTag::Acc, GenderTag::Masc, false, false}; return true; }
            }
            return false;
        };

        auto applyCaseLookup = [&](const std::unordered_map<std::wstring, InflectTarget>& tbl,
                                   std::wstring& curWord,
                                   size_t tIdx,
                                   CaseTag kase,
                                   const std::wstring& desc,
                                   bool allowFallback) -> bool {
            auto it = tbl.find(curWord);
            if (it != tbl.end()) {
                logChange(curWord, it->second.form, desc);
                nounTags[tIdx] = {kase, it->second.gender, it->second.plural, it->second.animate};
                return true;
            }
            for (const auto& kv : tbl) {
                if (kv.second.form == curWord) {
                    nounTags[tIdx] = {kase, kv.second.gender, kv.second.plural, kv.second.animate};
                    return true;
                }
            }
            if (allowFallback) {
                std::wstring regForm;
                NounTag regTag;
                if (inferRegularNounCase(curWord, kase, regForm, regTag)) {
                    logChange(curWord, regForm, desc);
                    nounTags[tIdx] = regTag;
                    return true;
                }
            }
            return false;
        };

        static const auto nonAdjectiveOiIy = wordSet(
            L"мной тобой собой домой зимой весной порой долой герой бой слой строй покой конвой прибой разбой отбой "
            L"застой настой гений санаторий комментарий сценарий критерий пролетарий калий натрий кальций магний алюминий");

        auto isModifierBeforeNoun = [&](const std::wstring& tok) -> bool {
            static const auto detModSet = wordSet(
                L"мой моя моё мои твой твоя твоё твои свой своя своё свои наш наша наше наши ваш ваша ваше ваши "
                L"этот эта это эти тот та то те один одна одно какой какая какое каждый каждая каждое весь вся всё все");
            if (nonAdjectiveOiIy.count(tok)) return false;
            if (detModSet.count(tok)) return true;
            return tok.size() >= 4 && endsWithAny(tok, {L"ый", L"ий", L"ой", L"ая", L"яя", L"ое", L"ее", L"ые", L"ие"});
        };

        static const auto genNouns = nounMap(
            L"время=времени/n имя=имени/n деньги=денег/up день=дня/m череповец=череповца/m друг=друга/ma "
            L"подруга=подруги/fa мама=мамы/fa папа=папы/ma врач=врача/ma");
        static const auto prepNouns = nounMap(
            L"день=дне/m череповец=череповце/m друг=друге/ma папа=папе/ma подруга=подруге/fa мама=маме/fa "
            L"время=времени/n имя=имени/n");
        static const auto datNouns = nounMap(
            L"друг=другу/ma врач=врачу/ma учитель=учителю/ma брат=брату/ma папа=папе/ma день=дню/m подруга=подруге/fa "
            L"мама=маме/fa сестра=сестре/fa девушка=девушке/fa время=времени/n");
        static const auto instNouns = nounMap(
            L"друг=другом/ma день=днем/m учитель=учителем/ma врач=врачом/ma брат=братом/ma папа=папой/ma "
            L"подруга=подругой/fa мама=мамой/fa сестра=сестрой/fa девушка=девушкой/fa время=временем/n имя=именем/n");
        static const auto quantGen = nounMap(
            L"время=времени/n работа=работы/f вода=воды/f сахар=сахара/m хлеб=хлеба/m молоко=молока/n помощь=помощи/f "
            L"друг=друзей/upa человек=людей/upa ребенок=детей/upa ребёнок=детей/upa деньги=денег/up день=дней/up "
            L"год=лет/up час=часов/up минута=минут/up неделя=недель/up месяц=месяцев/up дело=дел/up слово=слов/up "
            L"место=мест/up окно=окон/up письмо=писем/up книга=книг/up проблема=проблем/up вопрос=вопросов/up "
            L"ответ=ответов/up ошибка=ошибок/up машина=машин/up дом=домов/up город=городов/up билет=билетов/up "
            L"сообщение=сообщений/up новость=новостей/up");
        static const auto dirAcc = nounMap(
            L"раз=раз/m школа=школу/f работа=работу/f улица=улицу/f больница=больницу/f аптека=аптеку/f "
            L"квартира=квартиру/f комната=комнату/f машина=машину/f москва=москву/f россия=россию/f кухня=кухню/f "
            L"деревня=деревню/f библиотека=библиотеку/f остановка=остановку/f почта=почту/f страна=страну/f дом=дом/m "
            L"город=город/m магазин=магазин/m университет=университет/m парк=парк/m офис=офис/m театр=театр/m "
            L"музей=музей/m стол=стол/m петербург=петербург/m череповец=череповец/m");
        static const auto proAcc = nounMap(
            L"друг=друга/ma брат=брата/ma врач=врача/ma папа=папу/ma мама=маму/fa подруга=подругу/fa");
        static const auto instPron = pairMap(L"я=мной ты=тобой мы=нами вы=вами он=ним она=ней они=ними");
        static const auto datPron  = pairMap(L"я=мне ты=тебе мы=нам вы=вам он=нему она=ней они=ним");
        static const auto prepPron = pairMap(L"я=мне ты=тебе он=нём она=ней мы=нас вы=вас они=них");
        static const auto genPron  = pairMap(L"я=меня ты=тебя мы=нас вы=вас он=него она=неё они=них");

        for (size_t i = 1; i < processedTokens_.size(); ++i) {
            std::wstring& prep = processedTokens_[i - 1];
            size_t targetIdx = i;
            while (targetIdx + 1 < processedTokens_.size() &&
                   (targetIdx - i) < 2 &&
                   isModifierBeforeNoun(processedTokens_[targetIdx])) {
                ++targetIdx;
            }
            std::wstring& cur = processedTokens_[targetIdx];

            auto tryPron = [&](const std::unordered_map<std::wstring, std::wstring>& mp, const wchar_t* yaPrep, const std::wstring& desc) {
                if (targetIdx != i) return false;
                auto it = mp.find(cur);
                if (it == mp.end()) return false;
                if (cur == L"я" && yaPrep) logChange(prep, yaPrep, L"Фонетическая форма предлога");
                logChange(cur, it->second, desc);
                return true;
            };

            if (prep == L"нет") {
                applyCaseLookup(genNouns, cur, targetIdx, CaseTag::Gen, L"Родительный падеж после слова «нет»", true);
            } else if (prep == L"много" || prep == L"мало" || prep == L"сколько" || prep == L"несколько" || prep == L"столько") {
                if (!applyCaseLookup(quantGen, cur, targetIdx, CaseTag::Gen, L"Родительный падеж после количественного слова «" + prep + L"»", false)) {
                    if (cur.size() >= 4 && !isNonInflectNounWord(cur) && endsWithAny(cur, {L"ов", L"ев", L"ей"})) {
                        nounTags[targetIdx] = {CaseTag::Gen, GenderTag::Unknown, true, false};
                    }
                }
            } else if (prep == L"в" || prep == L"на") {
                bool isDirection = (cur == L"раз"), heuristicStartDirection = false;
                if (!isDirection && i == 1 && (processedTokens_.size() == 2 || targetIdx + 1 == processedTokens_.size())) {
                    isDirection = heuristicStartDirection = true;
                } else if (!isDirection) {
                    for (size_t back = 1; back <= 3 && i >= 1 + back; ++back) {
                        const std::wstring& pt = processedTokens_[i - 1 - back];
                        if (pt == L"а" || pt == L"но" || pt == L"и" || pt == L"что" || pt == L"если") break;
                        if (motionDesireVerbs.count(pt)) { isDirection = true; break; }
                    }
                    if (!isDirection && targetIdx + 1 < processedTokens_.size() && motionDesireVerbs.count(processedTokens_[targetIdx + 1])) {
                        isDirection = true;
                    }
                }
                if (isDirection && !applyCaseLookup(dirAcc, cur, targetIdx, CaseTag::Acc,
                        L"Винительный падеж направления с предлогом «" + prep + L"»", !heuristicStartDirection) && heuristicStartDirection) {
                    isDirection = false;
                }
                if (!isDirection) {
                    applyCaseLookup(prepNouns, cur, targetIdx, CaseTag::Prep, L"Предложный падеж места с предлогом «" + prep + L"»", true);
                }
            } else if (prep == L"с" || prep == L"со") {
                if (!tryPron(instPron, L"со", L"Творительный падеж с предлогом «с»"))
                    applyCaseLookup(instNouns, cur, targetIdx, CaseTag::Inst, L"Творительный падеж с предлогом «" + prep + L"»", true);
            } else if (prep == L"к" || prep == L"ко") {
                if (!tryPron(datPron, L"ко", L"Дательный падеж с предлогом «к»"))
                    applyCaseLookup(datNouns, cur, targetIdx, CaseTag::Dat, L"Дательный падеж с предлогом «" + prep + L"»", true);
            } else if (prep == L"о" || prep == L"об" || prep == L"обо") {
                if (!tryPron(prepPron, L"обо", L"Предложный падеж с предлогом «о»"))
                    applyCaseLookup(prepNouns, cur, targetIdx, CaseTag::Prep, L"Предложный падеж с предлогом «" + prep + L"»", true);
            } else if (prep == L"по") {
                if (!tryPron(datPron, nullptr, L"Дательный падеж с предлогом «по»"))
                    applyCaseLookup(datNouns, cur, targetIdx, CaseTag::Dat, L"Дательный падеж с предлогом «по»", true);
            } else if (prep == L"у" || prep == L"без" || prep == L"из" || prep == L"от" ||
                       prep == L"для" || prep == L"до" || prep == L"после" || prep == L"около" || prep == L"про") {
                if (!tryPron(genPron, nullptr, L"Падежная форма местоимения после предлога «" + prep + L"»")) {
                    if (prep == L"про") applyCaseLookup(proAcc, cur, targetIdx, CaseTag::Acc, L"Винительный падеж с предлогом «про»", true);
                    else applyCaseLookup(genNouns, cur, targetIdx, CaseTag::Gen, L"Родительный падеж после предлога «" + prep + L"»", true);
                }
            }
        }

        // Правило 5: Согласование прилагательных и притяжательных/указательных местоимений по роду, числу и падежу
        static const auto femAccForms = wordSet(
            L"школу работу улицу больницу аптеку квартиру комнату машину москву россию кухню деревню библиотеку остановку "
            L"почту страну");
        static const auto femPrepForms = wordSet(
            L"школе работе улице больнице аптеке квартире комнате машине москве россии кухне деревне библиотеке остановке "
            L"почте стране");

        struct DetCaseForms {
            std::wstring fem, neut, plur;
            std::wstring mascGen, mascDat, mascInst, mascPrep;
            std::wstring femAcc, femOblique;
            std::wstring plurGenPrep, plurDat, plurInst;
        };
        static const auto detAgree = [] {
            std::unordered_map<std::wstring, DetCaseForms> m;
            for (const auto& kv : listMap(
                L"мой=моя,моё,мои,моего,моему,моим,моем,мою,моей,моих,моим,моими "
                L"твой=твоя,твоё,твои,твоего,твоему,твоим,твоем,твою,твоей,твоих,твоим,твоими "
                L"свой=своя,своё,свои,своего,своему,своим,своем,свою,своей,своих,своим,своими "
                L"наш=наша,наше,наши,нашего,нашему,нашим,нашем,нашу,нашей,наших,нашим,нашими "
                L"ваш=ваша,ваше,ваши,вашего,вашему,вашим,вашем,вашу,вашей,ваших,вашим,вашими "
                L"этот=эта,это,эти,этого,этому,этим,этом,эту,этой,этих,этим,этими "
                L"тот=та,то,те,того,тому,тем,том,ту,той,тех,тем,теми "
                L"один=одна,одно,одни,одного,одному,одним,одном,одну,одной,одних,одним,одними "
                L"какой=какая,какое,какие,какого,какому,каким,каком,какую,какой,каких,каким,какими "
                L"каждый=каждая,каждое,каждые,каждого,каждому,каждым,каждом,каждую,каждой,каждых,каждым,каждыми "
                L"весь=вся,всё,все,всего,всему,всем,всем,всю,всей,всех,всем,всеми "
                L"сам=сама,само,сами,самого,самому,самим,самом,саму,самой,самих,самим,самими")) {
                const auto& v = kv.second;
                m[kv.first] = {v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11]};
            }
            return m;
        }();

        auto tryAdjCandidates = [&](std::wstring& cur, const std::wstring& c1, const std::wstring& c2, const std::wstring& desc) {
            if (!c1.empty() && lookupFreq(c1) > 0) {
                logChange(cur, c1, desc);
            } else if (!c2.empty() && lookupFreq(c2) > 0) {
                logChange(cur, c2, desc);
            }
        };

        for (size_t i = processedTokens_.size() - 1; i-- > 0;) {
            std::wstring& cur = processedTokens_[i];
            const std::wstring& nextW = processedTokens_[i + 1];
            if (nextW.empty()) continue;

            NounTag tag = nounTags[i + 1];
            if (tag.kase == CaseTag::None) {
                if (nonNounSkip.count(nextW)) continue;
                if (femAccForms.count(nextW)) {
                    tag = {CaseTag::Acc, GenderTag::Fem, false, false};
                } else if (femPrepForms.count(nextW)) {
                    tag = {CaseTag::Prep, GenderTag::Fem, false, false};
                } else if (pluralNouns.count(nextW)) {
                    tag = {CaseTag::None, GenderTag::Unknown, true, false};
                } else if ((nextW.back() == L'о' || nextW.back() == L'е' || neutExceptions.count(nextW)) &&
                           !mascExceptions.count(nextW)) {
                    tag = {CaseTag::None, GenderTag::Neut, false, false};
                } else if (((nextW.back() == L'а' || nextW.back() == L'я') &&
                            !mascExceptions.count(nextW) && !neutExceptions.count(nextW)) ||
                           femThirdDecl.count(nextW)) {
                    tag = {CaseTag::None, GenderTag::Fem, false, false};
                } else {
                    continue;
                }
            }

            auto detIt = detAgree.find(cur);
            if (detIt != detAgree.end()) {
                const auto& f = detIt->second;
                if (tag.plural) {
                    if (tag.kase == CaseTag::None || (tag.kase == CaseTag::Acc && !tag.animate)) {
                        logChange(cur, f.plur, L"Согласование местоимения во мн.ч.");
                    } else if (tag.kase == CaseTag::Dat) {
                        logChange(cur, f.plurDat, L"Согласование местоимения во мн.ч. дательного падежа");
                    } else if (tag.kase == CaseTag::Inst) {
                        logChange(cur, f.plurInst, L"Согласование местоимения во мн.ч. творительного падежа");
                    } else {
                        logChange(cur, f.plurGenPrep, L"Согласование местоимения во мн.ч. косвенного падежа");
                    }
                } else if (tag.gender == GenderTag::Fem) {
                    if (tag.kase == CaseTag::None) logChange(cur, f.fem, L"Согласование местоимения в женском роде");
                    else if (tag.kase == CaseTag::Acc) logChange(cur, f.femAcc, L"Согласование местоимения в винительном падеже ж.р.");
                    else logChange(cur, f.femOblique, L"Согласование местоимения в косвенном падеже ж.р.");
                } else if (tag.gender == GenderTag::Masc || tag.gender == GenderTag::Neut) {
                    if (tag.kase == CaseTag::Gen || (tag.kase == CaseTag::Acc && tag.gender == GenderTag::Masc && tag.animate)) {
                        logChange(cur, f.mascGen, L"Согласование местоимения в родительном/винительном падеже");
                    } else if (tag.kase == CaseTag::Dat) {
                        logChange(cur, f.mascDat, L"Согласование местоимения в дательном падеже");
                    } else if (tag.kase == CaseTag::Inst) {
                        logChange(cur, f.mascInst, L"Согласование местоимения в творительном падеже");
                    } else if (tag.kase == CaseTag::Prep) {
                        logChange(cur, f.mascPrep, L"Согласование местоимения в предложном падеже");
                    } else if (tag.gender == GenderTag::Neut && (tag.kase == CaseTag::None || tag.kase == CaseTag::Acc)) {
                        logChange(cur, f.neut, L"Согласование местоимения в среднем роде");
                    }
                }
                nounTags[i] = tag;
                continue;
            }

            if (!nonAdjectiveOiIy.count(cur) && cur.size() >= 4) {
                std::wstring end2 = cur.substr(cur.size() - 2);
                if (end2 == L"ый" || end2 == L"ий" || end2 == L"ой") {
                    std::wstring stem = cur.substr(0, cur.size() - 2);
                    bool soft = (end2 == L"ий");
                    auto agreeAdj = [&](const wchar_t* sEnd, const wchar_t* hEnd, const wchar_t* desc) {
                        tryAdjCandidates(cur, stem + (soft ? sEnd : hEnd), stem + (soft ? hEnd : sEnd), desc);
                    };
                    if (tag.plural) {
                        if (tag.kase == CaseTag::None || (tag.kase == CaseTag::Acc && !tag.animate)) {
                            agreeAdj(L"ие", L"ые", L"Согласование прилагательного во множественном числе");
                        } else if (tag.kase == CaseTag::Dat) {
                            agreeAdj(L"им", L"ым", L"Согласование прилагательного во мн.ч. дательного падежа");
                        } else if (tag.kase == CaseTag::Inst) {
                            agreeAdj(L"ими", L"ыми", L"Согласование прилагательного во мн.ч. творительного падежа");
                        } else {
                            agreeAdj(L"их", L"ых", L"Согласование прилагательного во мн.ч. косвенного падежа");
                        }
                    } else if (tag.gender == GenderTag::Fem) {
                        if (tag.kase == CaseTag::None) agreeAdj(L"яя", L"ая", L"Согласование прилагательного по женскому роду");
                        else if (tag.kase == CaseTag::Acc) agreeAdj(L"юю", L"ую", L"Согласование прилагательного в винительном падеже ж.р.");
                        else agreeAdj(L"ей", L"ой", L"Согласование прилагательного в косвенном падеже ж.р.");
                    } else if (tag.gender == GenderTag::Masc || tag.gender == GenderTag::Neut) {
                        if (tag.kase == CaseTag::Gen || (tag.kase == CaseTag::Acc && tag.gender == GenderTag::Masc && tag.animate)) {
                            agreeAdj(L"его", L"ого", L"Согласование прилагательного в родительном/винительном падеже");
                        } else if (tag.kase == CaseTag::Dat) {
                            agreeAdj(L"ему", L"ому", L"Согласование прилагательного в дательном падеже");
                        } else if (tag.kase == CaseTag::Inst) {
                            agreeAdj(L"им", L"ым", L"Согласование прилагательного в творительном падеже");
                        } else if (tag.kase == CaseTag::Prep) {
                            agreeAdj(L"ем", L"ом", L"Согласование прилагательного в предложном падеже");
                        } else if (tag.gender == GenderTag::Neut && (tag.kase == CaseTag::None || tag.kase == CaseTag::Acc)) {
                            agreeAdj(L"ее", L"ое", L"Согласование прилагательного по среднему роду");
                        }
                    }
                    nounTags[i] = tag;
                }
            }
        }

        // 7. Сборка итогового предложения с пунктуацией и заглавной буквой
        static const auto alwaysCommaFirst = wordSet(
            L"здравствуйте привет короче да нет конечно пожалуйста наверное кстати например видимо");
        static const auto clauseOpeningImp = wordSet(
            L"извините простите скажи скажите подскажи подскажите слушай слушайте");
        static const auto conjCoords = wordSet(
            L"и а но или");
        static const auto subordAlways = wordSet(
            L"чтобы если потому поэтому поскольку хотя однако зато");
        static const auto kotoryForms = wordSet(
            L"который которая которое которые которого которому которым котором которой которую которых которыми");
        static const auto prepsForRel = wordSet(
            L"в во на с со к ко о об обо у от до из за под над перед между через про для по при без");
        static const auto commaBeforeTriggers = wordSet(
            L"знаю знаешь знает знаем знаете знают знал знала знали думаю думаешь думает думаем думаете думают думал "
            L"думала думали подумал подумала сказал сказала сказали скажу скажи скажите говорю говоришь говорит говорим "
            L"говорите говорят говорил говорила говорили ответил ответила ответили спросил спросила спросили спрашиваю "
            L"спрашиваешь спрашивает написал написала написали напиши напишите вижу видишь видит видим видите видят видел "
            L"видела видели смотрю смотри смотрите смотрел слышу слышишь слышит слышим слышите слышат слышал слышала "
            L"слышали понимаю понимаешь понимает понимаем понимаете понимают понял поняла поняли пойму поймешь поймет "
            L"поймёт кажется надеюсь верю уверен уверена уверены рад рада рады хорошо плохо жаль правда важно понятно "
            L"известно интересно странно удивительно очевидно видно слышно значит чувствую боюсь обещаю помню помнишь "
            L"помнит помним помните помнят забыл забыла забыли сомневаюсь проверь проверьте узнай узнайте объясни "
            L"объясните подскажи подскажите расскажи расскажите покажи покажите напомни напомните выяснил решил");
        static const auto objPronounsBeforeSubord = wordSet(
            L"мне тебе ему ей нам вам им меня тебя нас вас");
        static const auto questionWords = wordSet(
            L"что как где куда откуда почему зачем сколько кто когда какой какая какое какие чей чья чьё чье");
        static const auto subjectPronouns = wordSet(
            L"я ты он она оно мы вы они это");
        static const auto netPredicateObjects = wordSet(
            L"времени имени работы школы денег дома друга подруги телефона машины воды хлеба настроения помощи места мамы "
            L"папы врача города магазина комнаты квартиры улицы книги дела слова сил слов проблем смысла желания "
            L"возможности никого ничего свободного нового интернета связи билета");

        auto isClauseOpenerFirst = [&](const std::wstring& w) -> bool {
            return alwaysCommaFirst.count(w) || clauseOpeningImp.count(w);
        };

        auto isFiniteNonInfVerb = [&](const std::wstring& s) -> bool {
            if (s.size() < 2 || nonNounSkip.count(s) || nonInflectExtra.count(s) ||
                subjectPronouns.count(s) || questionWords.count(s) || prepsForRel.count(s) ||
                conjCoords.count(s) || subordAlways.count(s) || kotoryForms.count(s)) {
                return false;
            }
            if (endsWithAny(s, {L"ть", L"ться", L"ти", L"чь"})) return false;
            static const auto knownVerbs = wordSet(
                L"хочу хочешь хочет хотим хотите хотят могу можешь может можем можете могут буду будешь будет будем будете "
                L"будут приду придешь придёт придет помогу поможешь поможет успею успеешь успеет иду идешь идет идёт пойду "
                L"пойдешь пойдет пойдёт позвони позвоните скажи скажите дай дайте иди идите напиши напишите подожди подождите "
                L"приходи звони пошел пошёл пошла пошли пришел пришёл пришла пришли ушел ушёл ушла ушли устал устала устали "
                L"заболел заболела опоздал опоздала забыл забыла останусь останется болит идет идёт стоит");
            if (knownVerbs.count(s)) return true;
            if (endsWithAny(s, {L"ешь", L"ёшь", L"ишь", L"ете", L"ёте", L"ите",
                                L"усь", L"юсь", L"ись", L"ется", L"ится", L"ются", L"ятся"})) {
                return true;
            }
            if (endsWithAny(s, {L"ет", L"ёт", L"ит", L"ем", L"ём", L"им", L"ут", L"ют", L"ат", L"ят"}) &&
                !(lookupFreq(s + L"ом") > 0 || lookupFreq(s + L"а") > 0)) {
                return true;
            }
            if (s.size() >= 4 && endsWithAny(s, {L"ал", L"ала", L"али", L"ел", L"ела", L"ели",
                                                 L"ил", L"ила", L"или"}) &&
                lookupFreq(s + L"е") == 0) {
                return true;
            }
            return false;
        };

        auto isClausePredicative = [&](const std::wstring& s) -> bool {
            if (isFiniteNonInfVerb(s)) return true;
            static const auto shortPreds = wordSet(
                L"дома нужно надо можно нельзя пора хорошо плохо жаль тепло холодно готов готова готовы рад рада рады болен "
                L"занят занята нет");
            return shortPreds.count(s) > 0;
        };

        const size_t nTok = processedTokens_.size();
        std::vector<bool> commaBefore(nTok, false);

        // 7a. Явные пользовательские запятые из addText / addWord / addLetter
        for (size_t i = 1; i < nTok; ++i) {
            if (explicitCommas[i - 1]) {
                commaBefore[i] = true;
            }
        }

        // 7b. Начальные вводные, приветствия и обращение-глаголы при открытии придаточного/вопроса
        if (nTok >= 2) {
            const std::wstring& first = processedTokens_[0];
            const std::wstring& second = processedTokens_[1];
            if (alwaysCommaFirst.count(first)) {
                bool isNetGen = (first == L"нет" &&
                                 (nounTags[1].kase == CaseTag::Gen || netPredicateObjects.count(second) > 0));
                bool isParticle = (first == L"да" || first == L"нет" || first == L"конечно") &&
                                  (second == L"же" || second == L"бы" || second == L"ли");
                if (!isNetGen && !isParticle) {
                    commaBefore[1] = true;
                }
                if (first == L"конечно" && second == L"же" && nTok >= 3) {
                    commaBefore[2] = true;
                }
            } else if (first == L"может" && second == L"быть" && nTok >= 3) {
                const std::wstring& third = processedTokens_[2];
                if (subjectPronouns.count(third) || third == L"всё" || third == L"все" ||
                    third == L"завтра" || third == L"сегодня" || third == L"скоро" ||
                    third == L"тут" || third == L"здесь" || third == L"там" ||
                    third == L"у" || third == L"не") {
                    commaBefore[2] = true;
                }
            } else if (first == L"к" && second == L"сожалению" && nTok >= 3) {
                commaBefore[2] = true;
            } else if (clauseOpeningImp.count(first)) {
                bool opensClause = (second == L"пожалуйста" ||
                                    questionWords.count(second) > 0 ||
                                    subjectPronouns.count(second) > 0 ||
                                    subordAlways.count(second) > 0 ||
                                    second == L"а" || second == L"но" ||
                                    second == L"да" || second == L"нет" || second == L"ли" ||
                                    second == L"тут" || second == L"здесь" || second == L"там" ||
                                    (second == L"не" && nTok >= 3 && first != L"слушай" && first != L"слушайте"));
                if (opensClause) {
                    commaBefore[1] = true;
                }
            }
        }

        // 7c. Вводные слова и союзы внутри предложения
        for (size_t i = 1; i < nTok; ++i) {
            const std::wstring& word = processedTokens_[i];
            const std::wstring& prev = processedTokens_[i - 1];

            if (word == L"пожалуйста") {
                commaBefore[i] = true;
                if (i + 1 < nTok && processedTokens_[i + 1] != L"же" && processedTokens_[i + 1] != L"ли") {
                    commaBefore[i + 1] = true;
                }
                continue;
            }

            if (word == L"кстати" || word == L"наверное" || word == L"например") {
                if (!(i == 1 && (prev == L"ну" || conjCoords.count(prev)))) {
                    commaBefore[i] = true;
                }
                if (i + 1 < nTok && processedTokens_[i + 1] != L"же" &&
                    processedTokens_[i + 1] != L"ли" && processedTokens_[i + 1] != L"бы") {
                    commaBefore[i + 1] = true;
                }
                continue;
            }

            if (word == L"конечно") {
                if (prev != L"да" && prev != L"нет" && prev != L"ну" && !conjCoords.count(prev)) {
                    commaBefore[i] = true;
                }
                if (i + 1 < nTok && processedTokens_[i + 1] == L"же") {
                    if (i + 2 < nTok) commaBefore[i + 2] = true;
                } else if (i + 1 < nTok && processedTokens_[i + 1] != L"ли" && processedTokens_[i + 1] != L"бы") {
                    commaBefore[i + 1] = true;
                }
                continue;
            }

            if (word == L"а" || word == L"но") {
                if (!conjCoords.count(prev)) commaBefore[i] = true;
                continue;
            }

            if (word == L"так" && i + 1 < nTok &&
                (processedTokens_[i + 1] == L"как" || processedTokens_[i + 1] == L"что")) {
                if (!conjCoords.count(prev)) commaBefore[i] = true;
                continue;
            }

            if (kotoryForms.count(word)) {
                bool isIdiom = (prev == L"в" && word == L"который" &&
                                i + 1 < nTok && processedTokens_[i + 1] == L"раз");
                if (!isIdiom && !conjCoords.count(prev)) {
                    if (prepsForRel.count(prev)) {
                        if (i >= 2 && !conjCoords.count(processedTokens_[i - 2])) {
                            commaBefore[i - 1] = true;
                        }
                    } else {
                        commaBefore[i] = true;
                    }
                }
                continue;
            }

            if (subordAlways.count(word)) {
                if (word == L"хотя" && i + 1 < nTok && processedTokens_[i + 1] == L"бы") {
                    continue;
                }
                if (word == L"однако" && i == 1 && (prev == L"да" || prev == L"нет" || prev == L"ну")) {
                    continue;
                }
                if (word == L"потому" && (prev == L"не" || prev == L"только" || prev == L"именно") &&
                    i + 1 < nTok && processedTokens_[i + 1] == L"что") {
                    commaBefore[i + 1] = true;
                    continue;
                }
                if (conjCoords.count(prev)) {
                    if (prev == L"и" && i >= 2 &&
                        (word == L"поэтому" || word == L"поскольку" || word == L"однако" || word == L"зато")) {
                        commaBefore[i - 1] = true;
                    }
                } else {
                    commaBefore[i] = true;
                }
                continue;
            }

            if (word == L"когда") {
                if (!subjectPronouns.count(prev) && !conjCoords.count(prev) &&
                    !alwaysCommaFirst.count(prev) && !clauseOpeningImp.count(prev) &&
                    prev != L"только" && prev != L"вот" && prev != L"мало" && prev != L"редко") {
                    commaBefore[i] = true;
                }
                continue;
            }

            if (word == L"ли" && i >= 2) {
                if (commaBeforeTriggers.count(processedTokens_[i - 2])) {
                    commaBefore[i - 1] = true;
                } else if (i >= 3 && processedTokens_[i - 2] == L"не" &&
                           commaBeforeTriggers.count(processedTokens_[i - 3])) {
                    commaBefore[i - 2] = true;
                }
                continue;
            }

            if (questionWords.count(word) && word != L"когда") {
                if (prev != L"потому" && prev != L"так" && prev != L"только" &&
                    prev != L"пока" && prev != L"разве" && prev != L"хоть" &&
                    prev != L"за" && prev != L"про" && prev != L"во") {
                    bool isTriggerPrev = commaBeforeTriggers.count(prev) > 0;
                    bool isIndirectObjTrigger = (i >= 2 && objPronounsBeforeSubord.count(prev) > 0 &&
                                                 commaBeforeTriggers.count(processedTokens_[i - 2]) > 0);
                    bool isCorrelateChto = (word == L"что" &&
                                            (prev == L"то" || prev == L"том" || prev == L"того" ||
                                             prev == L"тому" || prev == L"тем")) ||
                                           (word == L"как" && prev == L"того");
                    if (isTriggerPrev || isIndirectObjTrigger || isCorrelateChto) {
                        commaBefore[i] = true;
                    }
                }
            }
        }

        // 7d. Закрывающая запятая после начального или встроенного придаточного («если ..., то ...» / «когда ..., я ...»)
        for (size_t openIdx = 0; openIdx + 2 < nTok; ++openIdx) {
            const std::wstring& opener = processedTokens_[openIdx];
            bool isClauseOpener = (openIdx == 0 && (opener == L"если" || opener == L"когда" ||
                                                   opener == L"хотя" || opener == L"поскольку")) ||
                                  (openIdx > 0 && (opener == L"если" || opener == L"поскольку"));
            if (!isClauseOpener) continue;

            // Вариант «..., то ...»
            bool matchedCorrelTo = false;
            for (size_t j = openIdx + 2; j < nTok; ++j) {
                const std::wstring& tj = processedTokens_[j];
                if (tj == L"то") {
                    const std::wstring& prevTo = processedTokens_[j - 1];
                    if (prevTo != L"что" && prevTo != L"кто" && prevTo != L"как" &&
                        prevTo != L"где" && prevTo != L"куда" && prevTo != L"почему" && prevTo != L"то") {
                        commaBefore[j] = true;
                        matchedCorrelTo = true;
                    }
                    break;
                }
                if (commaBefore[j]) break;
            }
            if (matchedCorrelTo) continue;

            // Вариант без «то»: после предиката придаточного идёт подлежащее-местоимение или финитный глагол главного
            bool sawPred = false;
            size_t predIdx = 0;
            for (size_t j = openIdx + 1; j < nTok; ++j) {
                if (commaBefore[j]) break;
                const std::wstring& tok = processedTokens_[j];
                if (!sawPred) {
                    if (isClausePredicative(tok)) {
                        sawPred = true;
                        predIdx = j;
                    }
                } else {
                    const std::wstring& prevTok = processedTokens_[j - 1];
                    if (j > predIdx &&
                        (subjectPronouns.count(tok) || isFiniteNonInfVerb(tok)) &&
                        prevTok != L"не" && !prepsForRel.count(prevTok) && !conjCoords.count(prevTok)) {
                        commaBefore[j] = true;
                        break;
                    }
                }
            }
        }

        // Формирование строки предложения
        std::wstring result;
        for (size_t i = 0; i < nTok; ++i) {
            std::wstring word = processedTokens_[i];
            if (i == 0 && !word.empty()) {
                word[0] = toUpperRu(word[0]);
            }
            if (i > 0) {
                if (commaBefore[i] && !result.empty() && result.back() != L',') {
                    result += L", ";
                } else {
                    result += L" ";
                }
            }
            result += word;
        }

        // Знак конца предложения
        if (explicitEndPunct_ != 0) {
            result += explicitEndPunct_;
        } else {
            bool isQuestion = false;
            if (!processedTokens_.empty()) {
                if (questionWords.count(processedTokens_[0])) {
                    isQuestion = true;
                    // «Когда придешь, позвони мне.» — сложноподчинённое с главным предложением, а не вопрос
                    if (processedTokens_[0] == L"когда") {
                        size_t commaIdx = 0;
                        for (size_t c = 1; c < nTok; ++c) {
                            if (commaBefore[c]) {
                                commaIdx = c;
                                break;
                            }
                        }
                        if (commaIdx > 0) {
                            bool mainHasQ = false;
                            for (size_t k = commaIdx; k < nTok; ++k) {
                                if (questionWords.count(processedTokens_[k]) || processedTokens_[k] == L"ли") {
                                    mainHasQ = true;
                                    break;
                                }
                            }
                            if (!mainHasQ && processedTokens_.back() != L"можно") {
                                isQuestion = false;
                            }
                        }
                    }
                }
                if (processedTokens_.back() == L"можно") isQuestion = true;

                for (size_t i = 0; i < nTok; ++i) {
                    const std::wstring& tok = processedTokens_[i];
                    if (tok == L"ли") {
                        bool isIndirectSubordLi = false;
                        for (size_t k = 0; k < i; ++k) {
                            if (commaBeforeTriggers.count(processedTokens_[k])) {
                                for (size_t c = k + 1; c <= i; ++c) {
                                    if (commaBefore[c]) {
                                        isIndirectSubordLi = true;
                                        break;
                                    }
                                }
                            }
                            if (isIndirectSubordLi) break;
                        }
                        if (!isIndirectSubordLi) isQuestion = true;
                    }

                    if (i > 0 && processedTokens_[i - 1] == L"а" &&
                        (tok == L"ты" || tok == L"вы" || tok == L"он" || tok == L"она" || tok == L"они")) {
                        if (i + 1 == nTok || (i == 1 && (tok == L"ты" || tok == L"вы"))) {
                            isQuestion = true;
                        }
                    }

                    if (i == 1 && questionWords.count(tok) &&
                        (clauseOpeningImp.count(processedTokens_[0]) ||
                         processedTokens_[0] == L"привет" || processedTokens_[0] == L"здравствуйте" ||
                         processedTokens_[0] == L"а" || processedTokens_[0] == L"и" ||
                         processedTokens_[0] == L"ну")) {
                        isQuestion = true;
                    }

                    if (i >= 1 && questionWords.count(tok) &&
                        processedTokens_[i - 1] != L"потому" &&
                        processedTokens_[i - 1] != L"так" &&
                        !commaBeforeTriggers.count(processedTokens_[i - 1])) {
                        const std::wstring& prevTok = processedTokens_[i - 1];
                        bool isAfterPleaseOpener = (i == 2 && processedTokens_[1] == L"пожалуйста" &&
                                                    (processedTokens_[0] == L"скажите" || processedTokens_[0] == L"скажи" ||
                                                     processedTokens_[0] == L"подскажите" || processedTokens_[0] == L"подскажи" ||
                                                     processedTokens_[0] == L"извините" || processedTokens_[0] == L"простите"));
                        bool isSubjDirectQ = false;
                        if (subjectPronouns.count(prevTok) &&
                            processedTokens_[0] != L"если" && processedTokens_[0] != L"когда" &&
                            processedTokens_[0] != L"хотя" && processedTokens_[0] != L"поскольку") {
                            bool hasEarlierSubord = false;
                            for (size_t k = 0; k + 1 < i; ++k) {
                                if (commaBeforeTriggers.count(processedTokens_[k]) ||
                                    subordAlways.count(processedTokens_[k]) ||
                                    processedTokens_[k] == L"что" || processedTokens_[k] == L"когда") {
                                    if (k == 0 && clauseOpeningImp.count(processedTokens_[0])) continue;
                                    hasEarlierSubord = true;
                                    break;
                                }
                            }
                            isSubjDirectQ = !hasEarlierSubord;
                        }
                        if (isAfterPleaseOpener || isSubjDirectQ ||
                            (i == 2 && (prevTok == L"а" || prevTok == L"ну" || prevTok == L"и") &&
                             isClauseOpenerFirst(processedTokens_[0]))) {
                            isQuestion = true;
                        }
                    }
                }
            }

            bool isExclamation = false;
            for (const auto& t : processedTokens_) {
                if (t == L"спасибо" || t == L"здравствуйте" || t == L"привет" ||
                    t == L"помогите" || t == L"помоги" || t == L"праздником" || t == L"поздравляю") {
                    isExclamation = true;
                }
            }

            if (isQuestion) {
                result += L"?";
            } else if (isExclamation) {
                result += L"!";
            } else {
                result += L".";
            }
        }

        cachedFinalSentence_ = result;
    }

    void SentenceGrammarEngine::addLetter(wchar_t ch) {
        if (ch == L' ' || ch == L'\n' || ch == L'\r' || ch == L'\t') {
            commitWord();
            return;
        }

        if (!qwertyMapping_) {
            if (ch == L'.' || ch == L'!' || ch == L'?') {
                if (!currentInputWord_.empty()) commitWord();
                if (!rawTokens_.empty()) {
                    explicitEndPunct_ = ch;
                    rebuildSentence();
                }
                return;
            }
            if (ch == L',' || ch == L';' || ch == L':') {
                if (!currentInputWord_.empty()) commitWord();
                if (ch == L',' && !explicitCommaAfter_.empty()) {
                    explicitCommaAfter_.back() = true;
                    rebuildSentence();
                }
                return;
            }
            if (ch == L'-') {
                if (!currentInputWord_.empty()) {
                    explicitEndPunct_ = 0;
                    currentInputWord_.push_back(L'-');
                }
            } else if (isWordLetter(ch)) {
                explicitEndPunct_ = 0;
                currentInputWord_.push_back(toLowerRu(ch));
            }
            return;
        }

        wchar_t mapped = mapQwertyToRu(ch);
        if (mapped == L'-') {
            if (!currentInputWord_.empty()) {
                explicitEndPunct_ = 0;
                currentInputWord_.push_back(L'-');
            }
        } else if (isWordLetter(mapped)) {
            explicitEndPunct_ = 0;
            currentInputWord_.push_back(mapped);
        }
    }

    void SentenceGrammarEngine::backspace() {
        if (!currentInputWord_.empty()) {
            currentInputWord_.pop_back();
        } else if (!rawTokens_.empty()) {
            explicitEndPunct_ = 0;
            rawTokens_.pop_back();
            if (!explicitCommaAfter_.empty()) explicitCommaAfter_.pop_back();
            rebuildSentence();
        }
    }

    void SentenceGrammarEngine::commitWord() {
        if (currentInputWord_.empty()) return;
        std::wstring prevWord = rawTokens_.empty() ? L"" : rawTokens_.back();
        std::vector<std::wstring> fixed = fixWordTokens(currentInputWord_, prevWord);
        currentInputWord_.clear();
        for (const auto& tok : fixed) {
            if (!tok.empty()) {
                rawTokens_.push_back(tok);
                explicitCommaAfter_.push_back(false);
            }
        }
        rebuildSentence();
    }

    void SentenceGrammarEngine::addWord(const std::wstring& word) {
        if (word.empty()) {
            commitWord();
            return;
        }
        if (!currentInputWord_.empty()) commitWord();
        addText(word);
    }

    void SentenceGrammarEngine::addText(const std::wstring& text) {
        if (text.empty()) return;

        for (size_t i = 0; i < text.size(); ++i) {
            wchar_t ch = text[i];
            if (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n') {
                commitWord();
            } else if (ch == L'.' || ch == L'!' || ch == L'?') {
                commitWord();
                if (!rawTokens_.empty()) {
                    explicitEndPunct_ = ch;
                    rebuildSentence();
                }
            } else if (ch == L',' || ch == L';' || ch == L':' ||
                       ch == L'(' || ch == L')' || ch == L'"' ||
                       ch == L'«' || ch == L'»' || ch == L'—' || ch == L'–' ||
                       ch == L'…' || ch == L'/') {
                commitWord();
                if (ch == L',' && !explicitCommaAfter_.empty()) {
                    explicitCommaAfter_.back() = true;
                    rebuildSentence();
                }
            } else if (ch == L'-') {
                bool hasNextLetter = (i + 1 < text.size() && isWordLetter(text[i + 1]));
                if (!currentInputWord_.empty() && hasNextLetter) {
                    currentInputWord_.push_back(L'-');
                } else {
                    commitWord();
                }
            } else if (isWordLetter(ch)) {
                explicitEndPunct_ = 0;
                currentInputWord_.push_back(toLowerRu(ch));
            }
        }
        commitWord();
    }

    void SentenceGrammarEngine::clear() {
        currentInputWord_.clear();
        rawTokens_.clear();
        explicitCommaAfter_.clear();
        processedTokens_.clear();
        ruleLogs_.clear();
        cachedFinalSentence_.clear();
        explicitEndPunct_ = 0;
    }

    std::wstring SentenceGrammarEngine::getRawSentence() const {
        std::wstring res;
        for (size_t i = 0; i < rawTokens_.size(); ++i) {
            if (i > 0) res += L" ";
            res += rawTokens_[i];
        }
        if (!currentInputWord_.empty()) {
            if (!res.empty()) res += L" ";
            res += currentInputWord_;
        }
        return res;
    }

} // namespace HandTracker
