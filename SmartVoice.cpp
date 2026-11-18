#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "sherpa-onnx/c-api/c-api.h"
#ifdef _MSC_VER
#pragma comment(lib, "sherpa-onnx-c-api.lib")
#pragma warning(pop)
#endif
#include "SmartVoice.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace HandTracker {

    namespace {

        struct MaCallbackUserData {
            void (*fn)(void*, uint32_t, void*) = nullptr;
            void* self = nullptr;
        };

    } // namespace

    struct SmartVoice::AudioBackend {
        ma_device device{};
        MaCallbackUserData userData{};
        bool initialized = false;
        int sampleRate = 0;

        ~AudioBackend() {
            if (initialized) {
                ma_device_uninit(&device);
                initialized = false;
            }
        }
    };

    namespace {

        std::filesystem::path pathFromUtf8(const std::string& s) {
            if (s.empty()) return {};
            return std::filesystem::path(
                std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
        }

        std::string utf8FromPath(const std::filesystem::path& p) {
            std::u8string u8 = p.u8string();
            return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
        }

        std::string toEspeakSafePath(const std::string& utf8Path) {
            if (utf8Path.empty()) return {};
#ifdef _WIN32
            bool hasNonAscii = false;
            for (unsigned char c : utf8Path) {
                if (c >= 0x80) {
                    hasNonAscii = true;
                    break;
                }
            }
            if (hasNonAscii) {
                std::filesystem::path p = pathFromUtf8(utf8Path);
                p.make_preferred();
                std::wstring wp = p.wstring();
                DWORD needed = GetShortPathNameW(wp.c_str(), nullptr, 0);
                if (needed > 0) {
                    std::wstring shortW(needed, L'\0');
                    DWORD written = GetShortPathNameW(wp.c_str(), shortW.data(), needed);
                    if (written > 0 && written < needed) {
                        shortW.resize(written);
                        int sz = WideCharToMultiByte(CP_UTF8, 0, shortW.c_str(), (int)shortW.size(),
                                                     nullptr, 0, nullptr, nullptr);
                        if (sz > 0) {
                            std::string out(sz, '\0');
                            WideCharToMultiByte(CP_UTF8, 0, shortW.c_str(), (int)shortW.size(),
                                                out.data(), sz, nullptr, nullptr);
                            return out;
                        }
                    }
                }
            }
#endif
            return utf8Path;
        }

        std::string wideToUtf8(const std::wstring& w) {
            std::string out;
            out.reserve(w.size() * 2);
            for (size_t i = 0; i < w.size(); ++i) {
                uint32_t cp = (uint32_t)w[i];
                if constexpr (sizeof(wchar_t) == 2) {
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size()) {
                        uint32_t lo = (uint32_t)w[i + 1];
                        if (lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + (((cp - 0xD800) << 10) | (lo - 0xDC00));
                            ++i;
                        }
                    } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                        continue; // Skip invalid lone surrogate
                    }
                }
                if (cp < 0x80) {
                    out.push_back((char)cp);
                } else if (cp < 0x800) {
                    out.push_back((char)(0xC0 | (cp >> 6)));
                    out.push_back((char)(0x80 | (cp & 0x3F)));
                } else if (cp < 0x10000) {
                    out.push_back((char)(0xE0 | (cp >> 12)));
                    out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back((char)(0x80 | (cp & 0x3F)));
                } else {
                    out.push_back((char)(0xF0 | (cp >> 18)));
                    out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
                    out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back((char)(0x80 | (cp & 0x3F)));
                }
            }
            return out;
        }

        std::wstring asciiOrUtf8ToWide(const std::string& s) {
            return pathFromUtf8(s).wstring();
        }

        std::wstring toLowerSimple(std::wstring s) {
            for (wchar_t& c : s) {
                if (c >= L'A' && c <= L'Z') c += (L'a' - L'A');
                else if (c >= L'А' && c <= L'Я') c += (L'а' - L'А');
                else if (c == L'Ё') c = L'ё';
            }
            return s;
        }

        bool isDigitW(wchar_t c) {
            return c >= L'0' && c <= L'9';
        }

        bool isLetterW(wchar_t c) {
            if ((c >= L'А' && c <= L'Я') || (c >= L'а' && c <= L'я') || c == L'Ё' || c == L'ё') return true;
            if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z')) return true;
            return false;
        }

        bool hasNonWhitespace(const std::wstring& s) {
            for (wchar_t c : s) {
                if (c != L' ' && c != L'\t' && c != L'\r' && c != L'\n') return true;
            }
            return false;
        }

        bool isRuLetterW(wchar_t c) {
            return (c >= L'А' && c <= L'Я') || (c >= L'а' && c <= L'я') || c == L'Ё' || c == L'ё';
        }

        bool isRuVowelW(wchar_t c) {
            switch (c) {
                case L'а': case L'е': case L'ё': case L'и': case L'о':
                case L'у': case L'ы': case L'э': case L'ю': case L'я':
                case L'А': case L'Е': case L'Ё': case L'И': case L'О':
                case L'У': case L'Ы': case L'Э': case L'Ю': case L'Я':
                    return true;
                default:
                    return false;
            }
        }

        bool isRuUpperW(wchar_t c) {
            return (c >= L'А' && c <= L'Я') || c == L'Ё';
        }

        wchar_t toUpperRuW(wchar_t c) {
            if (c >= L'а' && c <= L'я') return c - (L'а' - L'А');
            if (c == L'ё') return L'Ё';
            return c;
        }

        bool endsWithW(const std::wstring& s, const std::wstring& suffix) {
            return s.size() >= suffix.size() &&
                   s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        // Конвертация ручных отметок ударения вида "зам+ок" / "+облако" в U+0301 после гласной
        std::wstring convertPlusStressMarkers(const std::wstring& in) {
            std::wstring out;
            out.reserve(in.size() + 8);
            const std::size_t n = in.size();
            for (std::size_t i = 0; i < n;) {
                if (in[i] == L'+' && i + 1 < n && isRuVowelW(in[i + 1])) {
                    bool leftWord = (i > 0 && (isRuLetterW(in[i - 1]) || in[i - 1] == L'-'));
                    bool rightWord = (i + 2 < n && (isRuLetterW(in[i + 2]) || in[i + 2] == L'-'));
                    if (leftWord || rightWord) {
                        wchar_t v = in[i + 1];
                        out.push_back(v);
                        if (v != L'ё' && v != L'Ё') {
                            out.push_back(L'\u0301');
                        }
                        i += 2;
                        continue;
                    }
                }
                out.push_back(in[i]);
                ++i;
            }
            return out;
        }

        std::wstring applyCasePattern(const std::wstring& orig, const std::wstring& lowerTarget) {
            std::size_t letterCount = 0;
            std::size_t upperCount = 0;
            bool firstUpper = false;
            for (wchar_t c : orig) {
                if (!isRuLetterW(c)) continue;
                if (letterCount == 0) firstUpper = isRuUpperW(c);
                if (isRuUpperW(c)) ++upperCount;
                ++letterCount;
            }
            if (letterCount == 0 || upperCount == 0) return lowerTarget;

            std::wstring res = lowerTarget;
            if (letterCount >= 2 && upperCount == letterCount) {
                for (wchar_t& c : res) c = toUpperRuW(c);
            } else if (firstUpper) {
                for (wchar_t& c : res) {
                    if (isRuLetterW(c)) {
                        c = toUpperRuW(c);
                        break;
                    }
                }
            }
            return res;
        }

        std::unordered_set<std::wstring> wordSet(const wchar_t* s) {
            std::unordered_set<std::wstring> out;
            while (*s) {
                while (*s == L' ' || *s == L'\n' || *s == L'\r' || *s == L'\t') ++s;
                const wchar_t* st = s;
                while (*s && *s != L' ' && *s != L'\n' && *s != L'\r' && *s != L'\t') ++s;
                if (s > st) out.emplace(st, s);
            }
            return out;
        }

        const std::unordered_map<std::wstring, std::wstring>& unambiguousYoMap() {
            static const auto m = []() {
                std::unordered_map<std::wstring, std::wstring> out;
                std::ifstream file(pathFromUtf8("yo_ru.txt"), std::ios::binary);
                if (!file.is_open()) return out;
                std::string line;
                while (std::getline(file, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.empty() || line[0] == '#') continue;
                    size_t tab = line.find('\t');
                    if (tab == std::string::npos) continue;
                    std::wstring k = asciiOrUtf8ToWide(line.substr(0, tab));
                    std::wstring v = asciiOrUtf8ToWide(line.substr(tab + 1));
                    if (!k.empty() && !v.empty()) out.emplace(std::move(k), std::move(v));
                }
                return out;
            }();
            return m;
        }

        struct SpeechWordToken {
            std::size_t start = 0;
            std::size_t end = 0;
            std::wstring orig;
            std::wstring lower;
            bool hasExplicitStress = false;
            bool hasYo = false;
            bool clauseBreakBefore = false;
            bool commaBefore = false;
        };

        std::wstring resolveVseContext(const std::vector<SpeechWordToken>& toks, std::size_t idx) {
            const bool hasPrev = (idx > 0 && !toks[idx].clauseBreakBefore);
            const bool hasNext = (idx + 1 < toks.size() && !toks[idx + 1].clauseBreakBefore);
            const bool commaBefore = toks[idx].commaBefore;
            const bool commaAfter = (idx + 1 < toks.size() && toks[idx + 1].commaBefore && !toks[idx + 1].clauseBreakBefore);
            const std::wstring prev = hasPrev ? toks[idx - 1].lower : L"";
            const std::wstring next = hasNext ? toks[idx + 1].lower : L"";
            const std::wstring next2 = (hasNext && idx + 2 < toks.size() && !toks[idx + 2].clauseBreakBefore && !toks[idx + 2].commaBefore)
                                       ? toks[idx + 2].lower : L"";

            static const auto vseAfterPlurals = wordSet(
                L"мы вы они эти те остальные прочие другие наши ваши мои твои свои его ее её их два две три четыре пять шесть "
                L"семь восемь девять десять двое трое четверо вдвоем вдвоём втроем втроём вчетвером люди друзья дела слова "
                L"дома дни годы новости вопросы задачи проблемы машины окна руки ноги глаза места города вечера студенты "
                L"родители дети гости коллеги");
            static const auto singularNonPluralT = wordSet(
                L"может будет идет идёт нет ответ привет секрет совет свет цвет билет пакет обед интернет университет");
            static const auto vsyoNextPredicates = wordSet(
                L"равно время еще ещё такое так это то дело было стало будет прошло получилось работает идет идёт хорошо "
                L"плохо нормально отлично понятно ясно готово просто сложно важно верно правильно красиво интересно весело "
                L"грустно нужно надо нельзя можно лучше хуже больше меньше быстрее медленнее труднее проще дальше ближе");

            // Строгая защита множественного «все»
            if (commaAfter && next == L"кто") return L"все";
            if (!commaAfter && !next.empty()) {
                if (prev == L"на" && next == L"сто") return L"все";
                if (vseAfterPlurals.count(next)) return L"все";
                if (!singularNonPluralT.count(next) &&
                    (endsWithW(next, L"ют") || endsWithW(next, L"ут") || endsWithW(next, L"ят") ||
                     endsWithW(next, L"ат") || endsWithW(next, L"ли") || endsWithW(next, L"лись") ||
                     endsWithW(next, L"ете") || endsWithW(next, L"ите"))) {
                    return L"все";
                }
                if (vsyoNextPredicates.count(next) && !next2.empty() && !singularNonPluralT.count(next2) &&
                    (endsWithW(next2, L"ют") || endsWithW(next2, L"ут") || endsWithW(next2, L"ят") ||
                     endsWithW(next2, L"ат") || endsWithW(next2, L"ли") || endsWithW(next2, L"лись"))) {
                    return L"все"; // например «все хорошо работают»
                }
            }
            if (!commaBefore && (prev == L"мы" || prev == L"вы" || prev == L"они" || prev == L"люди") &&
                (next.empty() || !vsyoNextPredicates.count(next))) {
                return L"все";
            }

            // «всё» (средний род / обобщение)
            if (commaAfter && (next == L"что" || next == L"чего" || next == L"чем" || next == L"чему" || next == L"чём" || next == L"чтобы")) {
                return L"всё";
            }
            if (!commaAfter && vsyoNextPredicates.count(next)) {
                return L"всё";
            }
            if (!commaAfter && (endsWithW(next, L"ые") || endsWithW(next, L"ие")) &&
                next != L"такие" && next != L"другие") {
                return L"все"; // «все новые ...»
            }
            if (next.empty() && (prev == L"это" || prev == L"вот" || prev == L"и" || prev == L"за" || prev == L"про" || prev == L"на")) {
                return L"всё";
            }
            if (!commaBefore && (prev == L"я" || prev == L"ты" || prev == L"он" || prev == L"она" ||
                                  prev == L"мне" || prev == L"тебе" || prev == L"ему" || prev == L"ей")) {
                return L"всё";
            }
            if (!commaAfter && !next.empty() &&
                (endsWithW(next, L"ет") || endsWithW(next, L"ёт") || endsWithW(next, L"ит") || endsWithW(next, L"лось"))) {
                return L"всё";
            }
            if (next.empty() && hasPrev) {
                return L"всё";
            }
            return L"все";
        }

        std::wstring resolveHomographContext(const std::vector<SpeechWordToken>& toks, std::size_t idx) {
            const std::wstring& w = toks[idx].lower;
            const bool hasPrev = (idx > 0 && !toks[idx].clauseBreakBefore && !toks[idx].commaBefore);
            const bool hasPrev2 = (hasPrev && idx >= 2 && !toks[idx - 1].clauseBreakBefore && !toks[idx - 1].commaBefore);
            const bool hasNext = (idx + 1 < toks.size() && !toks[idx + 1].clauseBreakBefore && !toks[idx + 1].commaBefore);
            const std::wstring prev = hasPrev ? toks[idx - 1].lower : L"";
            const std::wstring prev2 = hasPrev2 ? toks[idx - 2].lower : L"";
            const std::wstring next = hasNext ? toks[idx + 1].lower : L"";

            // 1. Предложные ё-формы (в/во/о/об/обо/на/при)
            auto isPrepLoc = [](const std::wstring& p) {
                return p == L"в" || p == L"во" || p == L"о" || p == L"об" ||
                       p == L"обо" || p == L"на" || p == L"при";
            };
            if (w == L"нем" && isPrepLoc(prev)) return L"нём";
            if (w == L"чем" && isPrepLoc(prev)) return L"чём";
            if (w == L"моем" && isPrepLoc(prev)) return L"моём";
            if (w == L"твоем" && isPrepLoc(prev)) return L"твоём";
            if (w == L"своем" && isPrepLoc(prev)) return L"своём";
            if (w == L"всем" && (isPrepLoc(prev) || ((prev == L"этом" || prev == L"том" || prev == L"своем" || prev == L"своём") && isPrepLoc(prev2)))) {
                return L"всём";
            }
            if (w == L"все") return resolveVseContext(toks, idx);

            // 2. Однозначные ё-слова
            const auto& yoMap = unambiguousYoMap();
            auto yoIt = yoMap.find(w);
            if (yoIt != yoMap.end()) return yoIt->second;

            // 3. Омографы из файла homographs_ru.txt
            struct Homograph { std::wstring w, hit, miss, prevTrig, nextTrig; };
            static const auto kHomographs = []() {
                std::vector<Homograph> out;
                std::ifstream file(pathFromUtf8("homographs_ru.txt"), std::ios::binary);
                if (!file.is_open()) return out;
                std::string line;
                while (std::getline(file, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.empty() || line[0] == '#') continue;
                    std::wstring wl = asciiOrUtf8ToWide(line);
                    std::wstring f[5];
                    size_t p = 0;
                    for (int k = 0; k < 5 && p <= wl.size(); ++k) {
                        size_t t = (k < 4) ? wl.find(L'\t', p) : std::wstring::npos;
                        f[k] = (t == std::wstring::npos) ? wl.substr(p) : wl.substr(p, t - p);
                        if (t == std::wstring::npos) break;
                        p = t + 1;
                    }
                    if (!f[0].empty()) out.push_back({std::move(f[0]), std::move(f[1]), std::move(f[2]), std::move(f[3]), std::move(f[4])});
                }
                return out;
            }();
            auto inList = [](const std::wstring& list, const std::wstring& word) {
                if (word.empty() || list.empty()) return false;
                return (L" " + list + L" ").find(L" " + word + L" ") != std::wstring::npos;
            };
            for (const auto& h : kHomographs) {
                if (w == h.w) return (inList(h.prevTrig, prev) || inList(h.nextTrig, next)) ? h.hit : h.miss;
            }
            if (w == L"начала") {
                bool gen = (prev == L"с" || prev == L"до" || prev == L"от" || prev == L"после" || prev == L"у" ||
                            (prev == L"самого" && (prev2 == L"с" || prev2 == L"до" || prev2 == L"от")));
                return gen ? L"нача\u0301ла" : L"начала\u0301";
            }
            if (w == L"утра") {
                static const wchar_t* genAdj = L"доброго раннего прекрасного свежего следующего хорошего новогоднего воскресного субботнего сегодняшнего завтрашнего";
                static const wchar_t* timeTrig = L"с до от около после часа часов половина один два три четыре пять шесть семь восемь девять десять одиннадцать двенадцать двух трех трёх четырех четырёх пяти шести семи восьми девяти десяти одиннадцати двенадцати рассвета";
                if (inList(genAdj, prev)) return L"у\u0301тра";
                if (prev == L"самого") return (prev2 == L"с" || prev2 == L"до" || prev2 == L"от") ? L"утра\u0301" : L"у\u0301тра";
                return inList(timeTrig, prev) ? L"утра\u0301" : L"у\u0301тра";
            }
            if (w == L"стоит") {
                static const wchar_t* locPreps = L"в во на у под возле около рядом перед за тут там дома";
                static const wchar_t* costPrev = L"сколько много мало дорого дешево дёшево недорого сколько-то";
                static const wchar_t* costNext = L"дорого дешево дёшево недорого рублей рубля рубль тысяч тысячи тысячу миллион миллиона миллионов копеек денег много мало ли внимания времени труда того";
                bool nextInf = !next.empty() && (endsWithW(next, L"ть") || endsWithW(next, L"ти") ||
                                                 endsWithW(next, L"чь") || endsWithW(next, L"ться"));
                bool cost = inList(costPrev, prev) || inList(costNext, next) || nextInf ||
                            ((prev2 == L"ничего" || prev2 == L"того") && prev == L"не") ||
                            ((prev == L"не" || prev == L"это" || prev == L"того") && !inList(locPreps, next));
                return cost ? L"сто\u0301ит" : L"стои\u0301т";
            }
            return L"";
        }

        std::wstring applyYoAndStress(const std::wstring& rawInput) {
            std::wstring s = convertPlusStressMarkers(rawInput);
            if (s.empty()) return s;

            std::vector<SpeechWordToken> toks;
            const std::size_t n = s.size();
            std::size_t i = 0;
            bool nextClauseBreak = true;
            bool nextComma = false;

            while (i < n) {
                wchar_t c = s[i];
                if (isRuLetterW(c)) {
                    std::size_t start = i;
                    bool hasAcute = false;
                    bool hasYo = false;
                    std::wstring orig;
                    std::wstring lower;
                    while (i < n) {
                        wchar_t wc = s[i];
                        if (isRuLetterW(wc)) {
                            orig.push_back(wc);
                            if (wc == L'ё' || wc == L'Ё') hasYo = true;
                            if (wc >= L'А' && wc <= L'Я') lower.push_back(wc + (L'а' - L'А'));
                            else if (wc == L'Ё') lower.push_back(L'ё');
                            else lower.push_back(wc);
                            ++i;
                        } else if (wc == L'\u0301') {
                            orig.push_back(wc);
                            hasAcute = true;
                            ++i;
                        } else if (wc == L'-' && i + 1 < n && isRuLetterW(s[i + 1])) {
                            orig.push_back(wc);
                            lower.push_back(wc);
                            ++i;
                        } else {
                            break;
                        }
                    }
                    toks.push_back({start, i, std::move(orig), std::move(lower),
                                    hasAcute, hasYo, nextClauseBreak, nextComma});
                    nextClauseBreak = false;
                    nextComma = false;
                } else {
                    if (c == L'.' || c == L'!' || c == L'?' || c == L';' || c == L':' ||
                        c == L'\n' || c == L'\u2026' || c == L'(' || c == L')') {
                        nextClauseBreak = true;
                        nextComma = false;
                    } else if (c == L',' || c == L'\u2014' || c == L'\u2013') {
                        nextComma = true;
                    }
                    ++i;
                }
            }

            if (toks.empty()) return s;

            std::wstring out;
            out.reserve(s.size() + toks.size());
            std::size_t cursor = 0;
            for (std::size_t ti = 0; ti < toks.size(); ++ti) {
                out.append(s, cursor, toks[ti].start - cursor);
                if (toks[ti].hasExplicitStress || toks[ti].hasYo) {
                    out += toks[ti].orig;
                } else {
                    std::wstring resolved = resolveHomographContext(toks, ti);
                    if (resolved.empty()) {
                        out += toks[ti].orig;
                    } else {
                        out += applyCasePattern(toks[ti].orig, resolved);
                    }
                }
                cursor = toks[ti].end;
            }
            out.append(s, cursor, n - cursor);
            return out;
        }

        bool writeWavUtf8(const std::string& wavPath, const std::vector<float>& samples, int sampleRate) {
            std::ofstream out(pathFromUtf8(wavPath), std::ios::binary | std::ios::trunc);
            if (!out.is_open()) return false;

            const uint32_t numSamples = (uint32_t)samples.size();
            const uint16_t numChannels = 1;
            const uint16_t bitsPerSample = 16;
            const uint32_t sr = (uint32_t)(sampleRate > 0 ? sampleRate : 22050);
            const uint32_t byteRate = sr * numChannels * (bitsPerSample / 8);
            const uint16_t blockAlign = numChannels * (bitsPerSample / 8);
            const uint32_t dataSize = numSamples * blockAlign;
            const uint32_t riffSize = 36u + dataSize;

            auto writeU16 = [&](uint16_t v) {
                char b[2] = { (char)(v & 0xFF), (char)((v >> 8) & 0xFF) };
                out.write(b, 2);
            };
            auto writeU32 = [&](uint32_t v) {
                char b[4] = {
                    (char)(v & 0xFF),
                    (char)((v >> 8) & 0xFF),
                    (char)((v >> 16) & 0xFF),
                    (char)((v >> 24) & 0xFF)
                };
                out.write(b, 4);
            };

            out.write("RIFF", 4);
            writeU32(riffSize);
            out.write("WAVE", 4);
            out.write("fmt ", 4);
            writeU32(16u);
            writeU16(1u); // PCM
            writeU16(numChannels);
            writeU32(sr);
            writeU32(byteRate);
            writeU16(blockAlign);
            writeU16(bitsPerSample);
            out.write("data", 4);
            writeU32(dataSize);

            std::vector<int16_t> pcm16(samples.size());
            for (size_t i = 0; i < samples.size(); ++i) {
                float s = samples[i];
                if (!std::isfinite(s)) s = 0.0f;
                s = std::max(-1.0f, std::min(1.0f, s));
                long v = std::lround(s * 32767.0f);
                if (v > 32767) v = 32767;
                if (v < -32768) v = -32768;
                pcm16[i] = (int16_t)v;
            }
            if (!pcm16.empty()) {
                out.write(reinterpret_cast<const char*>(pcm16.data()),
                          (std::streamsize)(pcm16.size() * sizeof(int16_t)));
            }
            return (bool)out;
        }

        float speedFromRate(int rate) {
            int clamped = std::min(10, std::max(-10, rate));
            return std::pow(1.05f, (float)clamped);
        }

        std::wstring numberUnder1000ToWords(int n, bool feminine) {
            if (n == 0) return L"ноль";
            static const wchar_t* onesMasc[] = {
                L"", L"один", L"два", L"три", L"четыре", L"пять", L"шесть", L"семь", L"восемь", L"девять"
            };
            static const wchar_t* onesFem[] = {
                L"", L"одна", L"две", L"три", L"четыре", L"пять", L"шесть", L"семь", L"восемь", L"девять"
            };
            static const wchar_t* teens[] = {
                L"десять", L"одиннадцать", L"двенадцать", L"тринадцать", L"четырнадцать", L"пятнадцать",
                L"шестнадцать", L"семнадцать", L"восемнадцать", L"девятнадцать"
            };
            static const wchar_t* tens[] = {
                L"", L"", L"двадцать", L"тридцать", L"сорок", L"пятьдесят", L"шестьдесят", L"семьдесят", L"восемьдесят", L"девяносто"
            };
            static const wchar_t* hundreds[] = {
                L"", L"сто", L"двести", L"триста", L"четыреста", L"пятьсот", L"шестьсот", L"семьсот", L"восемьсот", L"девятьсот"
            };

            std::wstring res;
            if (n >= 100) {
                res += hundreds[n / 100];
                n %= 100;
                if (n > 0) res += L" ";
            }
            if (n >= 10 && n <= 19) {
                res += teens[n - 10];
                return res;
            }
            if (n >= 20) {
                res += tens[n / 10];
                n %= 10;
                if (n > 0) res += L" ";
            }
            if (n > 0 && n <= 9) {
                res += feminine ? onesFem[n] : onesMasc[n];
            }
            return res;
        }

        std::wstring pluralForm(int n, const wchar_t* one, const wchar_t* few, const wchar_t* many) {
            int last2 = n % 100;
            int last1 = n % 10;
            if (last2 >= 11 && last2 <= 14) return many;
            if (last1 == 1) return one;
            if (last1 >= 2 && last1 <= 4) return few;
            return many;
        }

        std::wstring numberToWords(long long n) {
            if (n == 0) return L"ноль";
            if (n < 1000) return numberUnder1000ToWords((int)n, false);

            std::wstring res;
            auto appendPart = [&](int val, bool fem, const wchar_t* one, const wchar_t* few, const wchar_t* many) {
                if (val <= 0) return;
                if (!res.empty()) res += L" ";
                res += numberUnder1000ToWords(val, fem);
                if (one) {
                    res += L" ";
                    res += pluralForm(val, one, few, many);
                }
            };

            int billions = (int)(n / 1000000000LL);
            int millions = (int)((n / 1000000LL) % 1000LL);
            int thousands = (int)((n / 1000LL) % 1000LL);
            int rem = (int)(n % 1000LL);

            appendPart(billions, false, L"миллиард", L"миллиарда", L"миллиардов");
            appendPart(millions, false, L"миллион", L"миллиона", L"миллионов");
            appendPart(thousands, true, L"тысяча", L"тысячи", L"тысяч");
            appendPart(rem, false, nullptr, nullptr, nullptr);
            return res;
        }

        struct TtsCallbackCtx {
            SmartVoice* self = nullptr;
            uint64_t seq = 0;
        };

        void maDataCallbackAdapter(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
            (void)pInput;
            if (!pDevice) return;
            auto* ud = static_cast<MaCallbackUserData*>(pDevice->pUserData);
            if (ud && ud->fn) {
                ud->fn(pOutput, (uint32_t)frameCount, ud->self);
            } else if (pOutput && frameCount > 0) {
                std::memset(pOutput, 0, (size_t)frameCount * sizeof(float));
            }
        }

    } // namespace

    void SmartVoice::audioDataCallback(void* pOutput, uint32_t frameCount, void* userData) {
        float* out = static_cast<float*>(pOutput);
        auto* self = static_cast<SmartVoice*>(userData);
        if (!out || frameCount == 0) return;
        if (!self) {
            std::memset(out, 0, (size_t)frameCount * sizeof(float));
            return;
        }

        if (self->paused_.load() || self->shuttingDown_.load()) {
            std::memset(out, 0, (size_t)frameCount * sizeof(float));
            return;
        }

        const float gain = self->volume_.load();
        size_t copied = 0;

        {
            std::lock_guard<std::mutex> lk(self->audioMutex_);
            const size_t total = self->pcmBuffer_.size();
            const size_t pos = self->pcmReadPos_;
            if (pos < total) {
                const size_t avail = total - pos;
                copied = std::min<size_t>(avail, frameCount);
                for (size_t i = 0; i < copied; ++i) {
                    out[i] = self->pcmBuffer_[pos + i] * gain;
                }
                self->pcmReadPos_ += copied;
                if (self->pcmReadPos_ >= self->pcmBuffer_.size()) {
                    self->pcmBuffer_.clear();
                    self->pcmReadPos_ = 0;
                }
            }
        }

        if (copied < frameCount) {
            std::memset(out + copied, 0, ((size_t)frameCount - copied) * sizeof(float));
        }
    }

    int32_t SmartVoice::ttsProgressCallback(const float* samples, int32_t n, float progress, void* arg) {
        (void)progress;
        auto* ctx = static_cast<TtsCallbackCtx*>(arg);
        if (!ctx || !ctx->self) return 0;
        SmartVoice* self = ctx->self;

        if (self->shuttingDown_.load() || !self->audioOutputReady_.load() || ctx->seq != self->utteranceSeq_.load()) {
            return 0; // Прерываем синтез немедленно
        }

        if (samples && n > 0) {
            std::lock_guard<std::mutex> lk(self->audioMutex_);
            if (self->shuttingDown_.load() || !self->audioOutputReady_.load() || ctx->seq != self->utteranceSeq_.load()) {
                return 0;
            }
            // Компактифицируем прочитанный префикс на рабочем потоке (никогда в audio callback)
            if (self->pcmBuffer_.capacity() < 22050 * 4) {
                self->pcmBuffer_.reserve(22050 * 4);
            }
            if (self->pcmReadPos_ > 32768 && self->pcmReadPos_ * 2 >= self->pcmBuffer_.size()) {
                self->pcmBuffer_.erase(self->pcmBuffer_.begin(),
                                       self->pcmBuffer_.begin() + static_cast<std::ptrdiff_t>(self->pcmReadPos_));
                self->pcmReadPos_ = 0;
            }
            self->pcmBuffer_.insert(self->pcmBuffer_.end(), samples, samples + n);
        }
        return 1;
    }

    SmartVoice::SmartVoice() = default;

    SmartVoice::~SmartVoice() {
        shutdown();
    }

    void SmartVoice::shutdown() {
        utteranceSeq_.fetch_add(1);
        {
            std::lock_guard<std::mutex> lk(stateMutex_);
            shuttingDown_.store(true);
            loading_.store(false);
            pendingQueue_.clear();
        }
        cv_.notify_all();
        if (workerThread_.joinable()) {
            workerThread_.join();
        }
        synthesizing_.store(false);
        activeSynthesisSeq_.store(0);
        paused_.store(false);
        {
            std::lock_guard<std::mutex> audioLk(audioMutex_);
            pcmBuffer_.clear();
            pcmReadPos_ = 0;
        }
        destroyAudioDevice();
        destroyTts();
    }

    void SmartVoice::destroyAudioDevice() {
        std::lock_guard<std::mutex> lk(audioDeviceMutex_);
        audio_.reset();
        audioOutputReady_.store(false);
    }

    void SmartVoice::destroyTts() {
        std::lock_guard<std::mutex> lk(ttsEngineMutex_);
        if (tts_) {
            SherpaOnnxDestroyOfflineTts(tts_);
            tts_ = nullptr;
        }
        ready_.store(false);
    }

    bool SmartVoice::ensureAudioDevice(int sampleRate) {
        if (sampleRate <= 0) sampleRate = 22050;
        std::lock_guard<std::mutex> lk(audioDeviceMutex_);
        if (!audioOutputEnabled_.load()) {
            audio_.reset();
            audioOutputReady_.store(false);
            return false;
        }
        if (audio_ && audio_->initialized && audio_->sampleRate == sampleRate) {
            audioOutputReady_.store(true);
            return true;
        }
        audio_.reset();
        audioOutputReady_.store(false);

        auto ab = std::make_unique<AudioBackend>();
        ab->userData.fn = &SmartVoice::audioDataCallback;
        ab->userData.self = this;

        ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format = ma_format_f32;
        cfg.playback.channels = 1;
        cfg.sampleRate = (ma_uint32)sampleRate;
        cfg.dataCallback = maDataCallbackAdapter;
        cfg.pUserData = &ab->userData;

        if (ma_device_init(nullptr, &cfg, &ab->device) != MA_SUCCESS) {
            return false;
        }
        if (ma_device_start(&ab->device) != MA_SUCCESS) {
            ma_device_uninit(&ab->device);
            return false;
        }

        ab->initialized = true;
        ab->sampleRate = sampleRate;
        audio_ = std::move(ab);
        audioOutputReady_.store(true);
        return true;
    }

    void SmartVoice::discoverVoices(const std::string& rootDir) {
        voices_.clear();
        namespace fs = std::filesystem;
        std::error_code ec;

        fs::path rootPath = pathFromUtf8(rootDir);
        if (!fs::exists(rootPath, ec) && rootDir == "Models/tts") {
            fs::path lowerPath = pathFromUtf8("models/tts");
            if (fs::exists(lowerPath, ec)) {
                rootPath = lowerPath;
            }
        }
        if (!fs::is_directory(rootPath, ec)) return;

        auto inspectVoiceDir = [&](const fs::path& dir) {
            if (!fs::is_directory(dir, ec)) return;
            std::string onnxFile;
            std::string tokensFile;
            std::string dataDir;

            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (ec) break;
                if (entry.is_regular_file(ec)) {
                    std::string fn = utf8FromPath(entry.path().filename());
                    if (fn.size() > 5 && fn.substr(fn.size() - 5) == ".onnx") {
                        if (onnxFile.empty()) onnxFile = utf8FromPath(entry.path());
                    } else if (fn == "tokens.txt") {
                        tokensFile = utf8FromPath(entry.path());
                    }
                } else if (entry.is_directory(ec)) {
                    if (utf8FromPath(entry.path().filename()) == "espeak-ng-data") {
                        dataDir = utf8FromPath(entry.path());
                    }
                }
            }

            if (!onnxFile.empty() && !tokensFile.empty()) {
                VoiceInfo vi;
                std::string folderName = utf8FromPath(dir.filename());
                vi.id = asciiOrUtf8ToWide(folderName);
                std::wstring lowerId = toLowerSimple(vi.id);

                if (lowerId.find(L"irina") != std::wstring::npos) {
                    vi.name = L"Irina (Piper VITS ru-RU)";
                    vi.gender = L"Female";
                } else if (lowerId.find(L"dmitri") != std::wstring::npos) {
                    vi.name = L"Dmitri (Piper VITS ru-RU)";
                    vi.gender = L"Male";
                } else if (lowerId.find(L"ruslan") != std::wstring::npos || lowerId.find(L"denis") != std::wstring::npos) {
                    vi.name = vi.id + L" (Piper VITS ru-RU)";
                    vi.gender = L"Male";
                } else {
                    vi.name = vi.id + L" (Piper VITS)";
                    vi.gender = L"Female";
                }
                vi.modelPath = onnxFile;
                vi.tokensPath = tokensFile;
                vi.dataDir = dataDir;
                voices_.push_back(std::move(vi));
            }
        };

        // Сначала проверяем, не указана ли сразу папка конкретного голоса
        inspectVoiceDir(rootPath);
        if (voices_.empty()) {
            for (const auto& entry : fs::directory_iterator(rootPath, ec)) {
                if (ec) break;
                if (entry.is_directory(ec)) {
                    inspectVoiceDir(entry.path());
                }
            }
        }
        std::sort(voices_.begin(), voices_.end(), [](const VoiceInfo& a, const VoiceInfo& b) {
            return a.id < b.id;
        });
    }

    bool SmartVoice::loadVoiceModel(const VoiceInfo& voice, bool preserveExistingOnFail) {
        namespace fs = std::filesystem;
        std::error_code ec;

        auto markLoadFailedIfNeeded = [&]() {
            if (!preserveExistingOnFail || !ready_.load()) {
                loadFailed_.store(true);
                ready_.store(false);
                available_.store(false);
                std::lock_guard<std::mutex> lk(stateMutex_);
                activeVoice_ = VoiceInfo{};
                currentVoiceName_.clear();
            }
        };

        if (voice.modelPath.empty() || voice.tokensPath.empty()) {
            markLoadFailedIfNeeded();
            return false;
        }
        if (!fs::is_regular_file(pathFromUtf8(voice.modelPath), ec) ||
            !fs::is_regular_file(pathFromUtf8(voice.tokensPath), ec)) {
            markLoadFailedIfNeeded();
            return false;
        }
        if (!voice.dataDir.empty()) {
            fs::path dataPath = pathFromUtf8(voice.dataDir);
            if (!fs::is_directory(dataPath, ec) ||
                !fs::is_regular_file(dataPath / "phontab", ec)) {
                markLoadFailedIfNeeded();
                return false;
            }
        }

        std::string safeModel = toEspeakSafePath(voice.modelPath);
        std::string safeTokens = toEspeakSafePath(voice.tokensPath);
        std::string safeDataDir = voice.dataDir.empty() ? std::string() : toEspeakSafePath(voice.dataDir);

        SherpaOnnxOfflineTtsConfig cfg;
        std::memset(&cfg, 0, sizeof(cfg));
        cfg.model.vits.model = safeModel.c_str();
        cfg.model.vits.tokens = safeTokens.c_str();
        cfg.model.vits.data_dir = safeDataDir.empty() ? "" : safeDataDir.c_str();
        cfg.model.vits.noise_scale = 0.667f;
        cfg.model.vits.noise_scale_w = 0.8f;
        cfg.model.vits.length_scale = 1.0f;
        cfg.model.num_threads = numThreads_;
        cfg.model.provider = "cpu";
        cfg.model.debug = 0;
        cfg.max_num_sentences = 1;

        auto t0 = std::chrono::steady_clock::now();
        const SherpaOnnxOfflineTts* newTts = SherpaOnnxCreateOfflineTts(&cfg);
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        lastLoadMs_.store(ms);

        if (!newTts) {
            markLoadFailedIfNeeded();
            return false;
        }

        int sr = SherpaOnnxOfflineTtsSampleRate(newTts);
        if (sr <= 0) sr = 22050;

        {
            std::lock_guard<std::mutex> lk(ttsEngineMutex_);
            if (tts_) {
                SherpaOnnxDestroyOfflineTts(tts_);
            }
            tts_ = newTts;
        }
        {
            std::lock_guard<std::mutex> lk(stateMutex_);
            activeVoice_ = voice;
            currentVoiceName_ = voice.name;
        }

        ttsSampleRate_.store(sr);
        loadFailed_.store(false);
        ready_.store(true);
        available_.store(true);
        return true;
    }

    void SmartVoice::startWorkerIfNeeded() {
        if (!workerThread_.joinable()) {
            shuttingDown_.store(false);
            workerThread_ = std::thread(&SmartVoice::workerLoop, this);
        }
    }

    void SmartVoice::workerLoop() {
        if (loading_.load()) {
            VoiceInfo initialVoice;
            {
                std::lock_guard<std::mutex> lk(stateMutex_);
                initialVoice = activeVoice_;
            }
            bool ok = false;
            if (!shuttingDown_.load()) {
                ok = loadVoiceModel(initialVoice, false);
            }
            if (ok && !shuttingDown_.load()) {
                ensureAudioDevice(ttsSampleRate_.load());
            }
            {
                std::lock_guard<std::mutex> lk(stateMutex_);
                if (!ok || !audioOutputReady_.load()) {
                    pendingQueue_.clear();
                }
                loading_.store(false);
            }
            cv_.notify_all();
        }

        while (true) {
            SpeechItem item;
            {
                std::unique_lock<std::mutex> lk(stateMutex_);
                cv_.wait(lk, [&]() {
                    return shuttingDown_.load() || (!loading_.load() && !pendingQueue_.empty());
                });
                if (shuttingDown_.load()) break;
                if (pendingQueue_.empty()) continue;

                item = std::move(pendingQueue_.front());
                pendingQueue_.pop_front();
                activeSynthesisSeq_.store(item.seq);
                synthesizing_.store(true);
            }

            if (item.seq == utteranceSeq_.load() && ready_.load() && audioOutputReady_.load()) {
                std::string utf8 = wideToUtf8(item.text);
                if (!utf8.empty()) {
                    TtsCallbackCtx ctx{this, item.seq};
                    SherpaOnnxGenerationConfig gcfg;
                    std::memset(&gcfg, 0, sizeof(gcfg));
                    gcfg.sid = 0;
                    gcfg.speed = speedFromRate(rate_.load());
                    gcfg.silence_scale = 0.2f;

                    std::lock_guard<std::mutex> engineLk(ttsEngineMutex_);
                    if (tts_ && item.seq == utteranceSeq_.load() && !shuttingDown_.load() && audioOutputReady_.load()) {
                        const SherpaOnnxGeneratedAudio* audio =
                            SherpaOnnxOfflineTtsGenerateWithConfig(tts_, utf8.c_str(), &gcfg,
                                                                   &SmartVoice::ttsProgressCallback, &ctx);
                        if (audio) {
                            SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
                        }
                    }
                }
            }

            synthesizing_.store(false);
            activeSynthesisSeq_.store(0);
            cv_.notify_all();
        }
    }

    bool SmartVoice::init(const std::wstring& voicePreference,
                          int rate,
                          int volume,
                          const std::string& modelsDir,
                          bool asyncLoad)
    {
        shutdown();
        shuttingDown_.store(false);

        setRate(rate);
        setVolume(volume);
        modelsRoot_ = modelsDir;
        paused_.store(false);
        ready_.store(false);
        loading_.store(false);
        loadFailed_.store(false);
        available_.store(false);
        lastLoadMs_.store(0.0);
        ttsSampleRate_.store(22050);
        {
            std::lock_guard<std::mutex> lk(stateMutex_);
            activeVoice_ = VoiceInfo{};
            currentVoiceName_.clear();
        }

        discoverVoices(modelsDir);
        if (voices_.empty()) {
            loadFailed_.store(true);
            return false;
        }

        const VoiceInfo* chosen = &voices_[0];
        if (!voicePreference.empty()) {
            std::wstring prefLower = toLowerSimple(voicePreference);
            for (const auto& vi : voices_) {
                if (toLowerSimple(vi.id).find(prefLower) != std::wstring::npos ||
                    toLowerSimple(vi.name).find(prefLower) != std::wstring::npos) {
                    chosen = &vi;
                    break;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lk(stateMutex_);
            activeVoice_ = *chosen;
            currentVoiceName_ = chosen->name;
        }

        if (asyncLoad) {
            loading_.store(true);
            available_.store(true);
            startWorkerIfNeeded();
            return true;
        }

        if (!loadVoiceModel(*chosen, false)) {
            return false;
        }
        ensureAudioDevice(ttsSampleRate_.load());
        startWorkerIfNeeded();
        return true;
    }

    bool SmartVoice::initAsync(const std::wstring& voicePreference,
                               int rate,
                               int volume,
                               const std::string& modelsDir)
    {
        return init(voicePreference, rate, volume, modelsDir, true);
    }

    bool SmartVoice::waitUntilReady() {
        {
            std::unique_lock<std::mutex> lk(stateMutex_);
            cv_.wait(lk, [&]() {
                return shuttingDown_.load() || !loading_.load();
            });
        }
        return ready_.load();
    }

    std::wstring SmartVoice::currentVoice() const {
        std::lock_guard<std::mutex> lk(stateMutex_);
        return currentVoiceName_;
    }

    bool SmartVoice::setVoice(const std::wstring& voiceName) {
        if (voiceName.empty() || voices_.empty()) return false;
        std::wstring targetLower = toLowerSimple(voiceName);

        const VoiceInfo* found = nullptr;
        for (const auto& vi : voices_) {
            if (toLowerSimple(vi.id) == targetLower ||
                toLowerSimple(vi.id).find(targetLower) != std::wstring::npos ||
                toLowerSimple(vi.name).find(targetLower) != std::wstring::npos) {
                found = &vi;
                break;
            }
        }
        if (!found) return false;

        waitUntilReady();
        {
            std::lock_guard<std::mutex> lk(stateMutex_);
            if (ready_.load() && activeVoice_.id == found->id) {
                return true;
            }
        }

        stop();
        VoiceInfo nextVoice = *found;
        bool preserveExisting = ready_.load();
        if (!loadVoiceModel(nextVoice, preserveExisting)) {
            return false;
        }
        ensureAudioDevice(ttsSampleRate_.load());
        return true;
    }

    std::wstring SmartVoice::normalizeText(const std::wstring& in) const {
        std::wstring out;
        out.reserve(in.size() + 48);
        std::size_t i = 0;

        auto appendUnitAfterNumber = [&](long long unitVal) {
            std::size_t k = i;
            if (k < in.size() && in[k] == L' ') ++k;
            if (k >= in.size()) return;
            if (in[k] == L'%') {
                out += L" ";
                out += pluralForm((int)(unitVal % 100), L"процент", L"процента", L"процентов");
                i = k + 1;
            } else if (in[k] == L'\u00B0') { // '°'
                out += L" ";
                out += pluralForm((int)(unitVal % 100), L"градус", L"градуса", L"градусов");
                i = k + 1;
                if (i < in.size() && (in[i] == L'C' || in[i] == L'c' || in[i] == L'С' || in[i] == L'с')) {
                    if (i + 1 >= in.size() || !isLetterW(in[i + 1])) {
                        ++i;
                    }
                }
            } else if (in[k] == L'\u20BD') { // '₽'
                out += L" ";
                out += pluralForm((int)(unitVal % 100), L"рубль", L"рубля", L"рублей");
                i = k + 1;
            }
        };

        while (i < in.size()) {
            // Сокращения (с проверкой границ слова, без аллокаций на обычных символах)
            wchar_t lc = (in[i] >= L'А' && in[i] <= L'Я') ? (in[i] + (L'а' - L'А')) : in[i];
            if (lc == L'р' && i + 3 <= in.size() && (i == 0 || !isLetterW(in[i - 1])) &&
                (i + 3 >= in.size() || !isLetterW(in[i + 3])) && toLowerSimple(in.substr(i, 3)) == L"ржя") {
                out += L"русский жестовый язык";
                i += 3;
                continue;
            }
            if (lc == L'т' && (i == 0 || !isLetterW(in[i - 1]))) {
                if (i + 4 <= in.size()) {
                    std::wstring sub4 = toLowerSimple(in.substr(i, 4));
                    if (sub4 == L"т.д.") { out += L"так далее"; i += 4; continue; }
                    if (sub4 == L"т.п.") { out += L"тому подобное"; i += 4; continue; }
                    if (sub4 == L"т.е.") { out += L"то есть"; i += 4; continue; }
                    if (sub4 == L"т.к.") { out += L"так как"; i += 4; continue; }
                    if (sub4 == L"т.ч.") { out += L"том числе"; i += 4; continue; }
                }
                if (i + 5 <= in.size()) {
                    std::wstring sub5 = toLowerSimple(in.substr(i, 5));
                    if (sub5 == L"т. д.") { out += L"так далее"; i += 5; continue; }
                    if (sub5 == L"т. п.") { out += L"тому подобное"; i += 5; continue; }
                    if (sub5 == L"т. е.") { out += L"то есть"; i += 5; continue; }
                    if (sub5 == L"т. к.") { out += L"так как"; i += 5; continue; }
                    if (sub5 == L"т. ч.") { out += L"том числе"; i += 5; continue; }
                }
            }

            // Числа (опционально со знаком '+'/'-'/U+2212, время HH:MM, десятичные дроби A.B / A,B, целые)
            bool isMinus = (in[i] == L'-' || in[i] == L'\u2212');
            bool isPlus = (in[i] == L'+');
            bool hasSign = (isMinus || isPlus) && i + 1 < in.size() && isDigitW(in[i + 1]) &&
                           (i == 0 || (!isDigitW(in[i - 1]) && !isLetterW(in[i - 1]) &&
                                       in[i - 1] != L'-' && in[i - 1] != L'\u2212' && in[i - 1] != L'+'));
            if (hasSign || isDigitW(in[i])) {
                std::size_t dStart = hasSign ? (i + 1) : i;
                std::size_t j = dStart;
                long long num = 0;
                bool tooLarge = false;
                while (j < in.size() && isDigitW(in[j])) {
                    if (j - dStart >= 12) tooLarge = true;
                    else num = num * 10 + (in[j] - L'0');
                    ++j;
                }

                // Время HH:MM (без знака)
                if (!hasSign && !tooLarge && (j - dStart) <= 2 && num <= 23 && j + 2 < in.size() &&
                    in[j] == L':' && isDigitW(in[j + 1]) && isDigitW(in[j + 2]) &&
                    (j + 3 >= in.size() || !isDigitW(in[j + 3]))) {
                    int mins = (in[j + 1] - L'0') * 10 + (in[j + 2] - L'0');
                    if (mins <= 59) {
                        out += numberToWords(num);
                        out += L" ";
                        if (in[j + 1] == L'0' && mins > 0) out += L"ноль ";
                        out += numberToWords(mins);
                        i = j + 3;
                        continue;
                    }
                }

                auto appendFemNum = [&](long long v) {
                    if (v % 100 != 11 && v % 10 == 1) out += (v == 1) ? L"одна" : (numberToWords(v - 1) + L" одна");
                    else if (v % 100 != 12 && v % 10 == 2) out += (v == 2) ? L"две" : (numberToWords(v - 2) + L" две");
                    else out += numberToWords(v);
                };

                // Десятичная дробь [+/-]A.B или A,B (1..3 знака, не часть цепочки A.B.C)
                bool prevDotAfterDigit = (i >= 2 && (in[i - 1] == L'.' || in[i - 1] == L',') && isDigitW(in[i - 2]));
                if (!tooLarge && !prevDotAfterDigit && num <= 999999999LL && j + 1 < in.size() &&
                    (in[j] == L'.' || in[j] == L',') && isDigitW(in[j + 1])) {
                    std::size_t fStart = j + 1, fEnd = fStart;
                    int fracVal = 0;
                    while (fEnd < in.size() && isDigitW(in[fEnd]) && (fEnd - fStart) < 4) {
                        fracVal = fracVal * 10 + (in[fEnd] - L'0');
                        ++fEnd;
                    }
                    std::size_t fracLen = fEnd - fStart;
                    bool notVersion = (fEnd >= in.size() || (in[fEnd] != L'.' && in[fEnd] != L','));
                    if (notVersion && fracLen >= 1 && fracLen <= 3) {
                        if (hasSign && (num != 0 || fracVal != 0)) out += isMinus ? L"минус " : L"плюс ";
                        appendFemNum(num);
                        out += L" ";
                        out += pluralForm((int)(num % 100), L"целая", L"целых", L"целых");
                        out += L" ";
                        appendFemNum(fracVal);
                        out += L" ";
                        if (fracLen == 1) out += pluralForm(fracVal, L"десятая", L"десятых", L"десятых");
                        else if (fracLen == 2) out += pluralForm(fracVal, L"сотая", L"сотых", L"сотых");
                        else out += pluralForm(fracVal, L"тысячная", L"тысячных", L"тысячных");
                        i = fEnd;
                        appendUnitAfterNumber(2);
                        continue;
                    }
                }

                if (!tooLarge && num <= 999999999999LL) {
                    if (hasSign && !(num == 0 && isMinus)) out += isMinus ? L"минус " : L"плюс ";
                    out += numberToWords(num);
                    i = j;
                    appendUnitAfterNumber(num);
                } else {
                    out += in.substr(i, j - i);
                    i = j;
                }
                continue;
            }

            out += in[i];
            ++i;
        }
        return out;
    }

    std::wstring SmartVoice::prepareForSpeech(const std::wstring& in) const {
        std::wstring normalized = normalizeText(in);
        if (normalized.empty()) return normalized;
        return applyYoAndStress(normalized);
    }

    void SmartVoice::speak(const std::wstring& text, bool purgePrevious) {
        if (!available_.load() || loadFailed_.load() || shuttingDown_.load() || text.empty()) return;
        if (!loading_.load() && !audioOutputReady_.load()) return;

        std::wstring prepared = prepareForSpeech(text);
        if (!hasNonWhitespace(prepared)) return;

        paused_.store(false);

        if (purgePrevious) {
            {
                std::lock_guard<std::mutex> lk(stateMutex_);
                if ((!loading_.load() && !audioOutputReady_.load()) || shuttingDown_.load()) return;
                uint64_t nextSeq = utteranceSeq_.fetch_add(1) + 1;
                pendingQueue_.clear();
                pendingQueue_.push_back({std::move(prepared), nextSeq});
            }
            {
                std::lock_guard<std::mutex> audioLk(audioMutex_);
                pcmBuffer_.clear();
                pcmReadPos_ = 0;
            }
        } else {
            std::lock_guard<std::mutex> lk(stateMutex_);
            if ((!loading_.load() && !audioOutputReady_.load()) || shuttingDown_.load()) return;
            uint64_t curSeq = utteranceSeq_.load();
            pendingQueue_.push_back({std::move(prepared), curSeq});
        }

        startWorkerIfNeeded();
        cv_.notify_all();
    }

    void SmartVoice::speakWord(const std::wstring& word) {
        if (word.empty()) return;
        speak(word, false);
    }

    bool SmartVoice::synthesizeToPcm(const std::wstring& text, std::vector<float>& outSamples, int& outSampleRate) {
        outSamples.clear();
        outSampleRate = 0;
        if (text.empty()) return false;

        if (loading_.load()) {
            waitUntilReady();
        }
        if (!ready_.load()) return false;

        std::wstring prepared = prepareForSpeech(text);
        if (!hasNonWhitespace(prepared)) return false;

        std::string utf8 = wideToUtf8(prepared);
        if (utf8.empty()) return false;

        SherpaOnnxGenerationConfig gcfg;
        std::memset(&gcfg, 0, sizeof(gcfg));
        gcfg.sid = 0;
        gcfg.speed = speedFromRate(rate_.load());
        gcfg.silence_scale = 0.2f;

        std::lock_guard<std::mutex> lk(ttsEngineMutex_);
        if (!tts_) return false;

        const SherpaOnnxGeneratedAudio* audio =
            SherpaOnnxOfflineTtsGenerateWithConfig(tts_, utf8.c_str(), &gcfg, nullptr, nullptr);
        if (!audio || audio->n <= 0 || !audio->samples) {
            if (audio) SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
            return false;
        }

        outSampleRate = audio->sample_rate;
        outSamples.assign(audio->samples, audio->samples + audio->n);
        SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
        return !outSamples.empty();
    }

    bool SmartVoice::synthesizeToWav(const std::wstring& text, const std::string& wavPath) {
        if (wavPath.empty()) return false;
        std::vector<float> pcm;
        int sr = 0;
        if (!synthesizeToPcm(text, pcm, sr)) return false;
        bool isAscii = true;
        for (unsigned char c : wavPath) {
            if (c >= 0x80) {
                isAscii = false;
                break;
            }
        }
        if (isAscii) {
            return SherpaOnnxWriteWave(pcm.data(), (int32_t)pcm.size(), sr, wavPath.c_str()) == 1;
        }
        return writeWavUtf8(wavPath, pcm, sr);
    }

    void SmartVoice::pauseResume() {
        if (!available_.load() || loadFailed_.load()) return;
        paused_.store(!paused_.load());
    }

    void SmartVoice::stop() {
        utteranceSeq_.fetch_add(1);
        paused_.store(false);
        {
            std::lock_guard<std::mutex> lk(stateMutex_);
            pendingQueue_.clear();
        }
        {
            std::lock_guard<std::mutex> audioLk(audioMutex_);
            pcmBuffer_.clear();
            pcmReadPos_ = 0;
        }
        cv_.notify_all();
    }

    void SmartVoice::setRate(int rate) {
        rate_.store(std::min(10, std::max(-10, rate)));
    }

    void SmartVoice::setVolume(int volume) {
        int clamped = std::min(100, std::max(0, volume));
        volume_.store((float)clamped / 100.0f);
    }

    void SmartVoice::setAudioOutputEnabled(bool enabled) {
        audioOutputEnabled_.store(enabled);
        if (!enabled) {
            stop();
            destroyAudioDevice();
        } else if (ready_.load() && !shuttingDown_.load()) {
            ensureAudioDevice(ttsSampleRate_.load());
        }
    }

    bool SmartVoice::isSpeaking() const {
        if (!available_.load() || loadFailed_.load() || shuttingDown_.load()) return false;
        if (paused_.load()) return false;
        {
            std::lock_guard<std::mutex> lk(stateMutex_);
            if (!loading_.load() && !audioOutputReady_.load()) return false;
            if (!pendingQueue_.empty()) return true;
        }
        if (!loading_.load() && !audioOutputReady_.load()) return false;
        if (synthesizing_.load() && activeSynthesisSeq_.load() == utteranceSeq_.load()) return true;
        {
            std::lock_guard<std::mutex> audioLk(audioMutex_);
            if (pcmReadPos_ < pcmBuffer_.size()) return true;
        }
        return false;
    }

} // namespace HandTracker
