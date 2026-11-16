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
            std::wstring out;
            out.reserve(s.size());
            size_t i = 0;
            while (i < s.size()) {
                unsigned char c = (unsigned char)s[i];
                uint32_t cp = c;
                size_t len = 1;
                if (c < 0x80) {
                    cp = c;
                } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size() && ((unsigned char)s[i + 1] & 0xC0) == 0x80) {
                    cp = ((uint32_t)(c & 0x1F) << 6) | ((uint32_t)(unsigned char)s[i + 1] & 0x3F);
                    len = 2;
                } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size() &&
                           ((unsigned char)s[i + 1] & 0xC0) == 0x80 && ((unsigned char)s[i + 2] & 0xC0) == 0x80) {
                    cp = ((uint32_t)(c & 0x0F) << 12) | (((uint32_t)(unsigned char)s[i + 1] & 0x3F) << 6) |
                         ((uint32_t)(unsigned char)s[i + 2] & 0x3F);
                    len = 3;
                } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size() &&
                           ((unsigned char)s[i + 1] & 0xC0) == 0x80 &&
                           ((unsigned char)s[i + 2] & 0xC0) == 0x80 &&
                           ((unsigned char)s[i + 3] & 0xC0) == 0x80) {
                    cp = ((uint32_t)(c & 0x07) << 18) | (((uint32_t)(unsigned char)s[i + 1] & 0x3F) << 12) |
                         (((uint32_t)(unsigned char)s[i + 2] & 0x3F) << 6) | ((uint32_t)(unsigned char)s[i + 3] & 0x3F);
                    len = 4;
                } else {
                    ++i;
                    continue;
                }
                i += len;
                if constexpr (sizeof(wchar_t) == 2) {
                    if (cp <= 0xFFFF) {
                        out.push_back((wchar_t)cp);
                    } else if (cp <= 0x10FFFF) {
                        cp -= 0x10000;
                        out.push_back((wchar_t)(0xD800 + (cp >> 10)));
                        out.push_back((wchar_t)(0xDC00 + (cp & 0x3FF)));
                    }
                } else {
                    out.push_back((wchar_t)cp);
                }
            }
            return out;
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

        const std::unordered_map<std::wstring, std::wstring>& unambiguousYoMap() {
            static const std::unordered_map<std::wstring, std::wstring> m = {
                // Глаголы движения/действия
                {L"идет", L"идёт"}, {L"идем", L"идём"}, {L"идешь", L"идёшь"}, {L"идете", L"идёте"},
                {L"пойдет", L"пойдёт"}, {L"пойдешь", L"пойдёшь"}, {L"пойдем", L"пойдём"}, {L"пойдете", L"пойдёте"},
                {L"придет", L"придёт"}, {L"придешь", L"придёшь"}, {L"придем", L"придём"}, {L"придете", L"придёте"},
                {L"найдет", L"найдёт"}, {L"найдешь", L"найдёшь"}, {L"найдем", L"найдём"}, {L"найдете", L"найдёте"},
                {L"дойдет", L"дойдёт"}, {L"уйдет", L"уйдёт"}, {L"зайдет", L"зайдёт"}, {L"подойдет", L"подойдёт"},
                {L"перейдет", L"перейдёт"}, {L"пройдет", L"пройдёт"}, {L"войдет", L"войдёт"},
                {L"пришел", L"пришёл"}, {L"пошел", L"пошёл"}, {L"ушел", L"ушёл"}, {L"нашел", L"нашёл"},
                {L"вошел", L"вошёл"}, {L"дошел", L"дошёл"}, {L"подошел", L"подошёл"}, {L"прошел", L"прошёл"}, {L"зашел", L"зашёл"},
                {L"шел", L"шёл"}, {L"принес", L"принёс"}, {L"унес", L"унёс"}, {L"привез", L"привёз"},
                {L"привел", L"привёл"}, {L"увел", L"увёл"},
                {L"провел", L"провёл"}, {L"завел", L"завёл"}, {L"навел", L"навёл"}, {L"прочел", L"прочёл"},
                {L"отчет", L"отчёт"}, {L"учет", L"учёт"}, {L"расчет", L"расчёт"}, {L"перечет", L"перечёт"},
                {L"живет", L"живёт"}, {L"живем", L"живём"}, {L"живешь", L"живёшь"}, {L"живете", L"живёте"},
                {L"дает", L"даёт"}, {L"даем", L"даём"}, {L"даешь", L"даёшь"}, {L"даете", L"даёте"},
                {L"берет", L"берёт"}, {L"берем", L"берём"}, {L"берешь", L"берёшь"}, {L"берете", L"берёте"},
                {L"ведет", L"ведёт"}, {L"ведем", L"ведём"}, {L"ведешь", L"ведёшь"}, {L"ведете", L"ведёте"},
                {L"возьмет", L"возьмёт"}, {L"возьмем", L"возьмём"}, {L"возьмешь", L"возьмёшь"}, {L"возьмете", L"возьмёте"},
                {L"вернет", L"вернёт"}, {L"вернем", L"вернём"}, {L"вернется", L"вернётся"},
                {L"вернемся", L"вернёмся"}, {L"вернешься", L"вернёшься"}, {L"вернетесь", L"вернётесь"},
                {L"пьет", L"пьёт"}, {L"пьем", L"пьём"}, {L"пьешь", L"пьёшь"}, {L"пьете", L"пьёте"},
                {L"ждет", L"ждёт"}, {L"ждем", L"ждём"}, {L"ждешь", L"ждёшь"}, {L"ждете", L"ждёте"},
                {L"поймет", L"поймёт"}, {L"поймем", L"поймём"}, {L"поймешь", L"поймёшь"}, {L"поймете", L"поймёте"},
                {L"начнет", L"начнёт"}, {L"начнем", L"начнём"}, {L"начнешь", L"начнёшь"}, {L"начнете", L"начнёте"},
                {L"зовет", L"зовёт"}, {L"зовем", L"зовём"}, {L"несет", L"несёт"}, {L"несем", L"несём"},
                {L"везет", L"везёт"}, {L"везем", L"везём"}, {L"течет", L"течёт"}, {L"печет", L"печёт"},
                {L"включен", L"включён"}, {L"сохранен", L"сохранён"}, {L"определен", L"определён"},
                {L"определенный", L"определённый"}, {L"определенная", L"определённая"},
                {L"определенное", L"определённое"}, {L"определенные", L"определённые"},
                // Наречия, местоимения, частицы, существительные
                {L"еще", L"ещё"}, {L"ее", L"её"}, {L"нее", L"неё"}, {L"мое", L"моё"}, {L"твое", L"твоё"}, {L"свое", L"своё"},
                {L"причем", L"причём"}, {L"все-таки", L"всё-таки"}, {L"вдвоем", L"вдвоём"}, {L"втроем", L"втроём"},
                {L"вперед", L"вперёд"}, {L"заем", L"заём"}, {L"наем", L"наём"},
                {L"объем", L"объём"}, {L"объема", L"объёма"}, {L"разъем", L"разъём"}, {L"прием", L"приём"}, {L"приема", L"приёма"},
                {L"ребенок", L"ребёнок"}, {L"учеба", L"учёба"}, {L"учебы", L"учёбы"}, {L"учебе", L"учёбе"}, {L"учебу", L"учёбу"}, {L"учебой", L"учёбой"},
                {L"елка", L"ёлка"}, {L"елки", L"ёлки"}, {L"елке", L"ёлке"}, {L"елку", L"ёлку"}, {L"елкой", L"ёлкой"},
                {L"теща", L"тёща"}, {L"тещи", L"тёщи"}, {L"теще", L"тёще"}, {L"тещу", L"тёщу"}, {L"тещей", L"тёщей"},
                {L"свекла", L"свёкла"}, {L"свеклы", L"свёклы"}, {L"свекле", L"свёкле"}, {L"свеклу", L"свёклу"},
                {L"звезды", L"звёзды"}, {L"слезы", L"слёзы"}, {L"лед", L"лёд"}, {L"мед", L"мёд"}, {L"щетка", L"щётка"}, {L"щеки", L"щёки"},
                {L"самолет", L"самолёт"}, {L"самолета", L"самолёта"}, {L"самолете", L"самолёте"}, {L"самолеты", L"самолёты"},
                {L"вертолет", L"вертолёт"}, {L"полет", L"полёт"}, {L"кошелек", L"кошелёк"}, {L"счет", L"счёт"}, {L"счета", L"счёта"},
                {L"шофер", L"шофёр"}, {L"актер", L"актёр"}, {L"партнер", L"партнёр"}, {L"тренажер", L"тренажёр"},
                {L"силен", L"силён"}, {L"умен", L"умён"}, {L"веселый", L"весёлый"}, {L"веселая", L"весёлая"},
                {L"веселое", L"весёлое"}, {L"веселые", L"весёлые"},
                {L"жесткий", L"жёсткий"}, {L"жесткая", L"жёсткая"}, {L"жесткое", L"жёсткое"}, {L"жестко", L"жёстко"},
                {L"четкий", L"чёткий"}, {L"четкая", L"чёткая"}, {L"четкое", L"чёткое"}, {L"четко", L"чётко"},
                {L"серьезно", L"серьёзно"}, {L"серьезный", L"серьёзный"}, {L"серьезная", L"серьёзная"},
                {L"серьезное", L"серьёзное"}, {L"серьезные", L"серьёзные"},
                // Частые прилагательные с безусловным 'ё' в корне
                {L"черный", L"чёрный"}, {L"черная", L"чёрная"}, {L"черное", L"чёрное"}, {L"черные", L"чёрные"},
                {L"черного", L"чёрного"}, {L"черному", L"чёрному"}, {L"черным", L"чёрным"}, {L"черном", L"чёрном"}, {L"черной", L"чёрной"}, {L"черную", L"чёрную"},
                {L"зеленый", L"зелёный"}, {L"зеленая", L"зелёная"}, {L"зеленое", L"зелёное"}, {L"зеленые", L"зелёные"},
                {L"зеленого", L"зелёного"}, {L"зеленому", L"зелёному"}, {L"зеленым", L"зелёным"}, {L"зеленом", L"зелёном"}, {L"зеленой", L"зелёной"}, {L"зеленую", L"зелёную"},
                {L"желтый", L"жёлтый"}, {L"желтая", L"жёлтая"}, {L"желтое", L"жёлтое"}, {L"желтые", L"жёлтые"},
                {L"желтого", L"жёлтого"}, {L"желтому", L"жёлтому"}, {L"желтым", L"жёлтым"}, {L"желтой", L"жёлтой"}, {L"желтую", L"жёлтую"},
                {L"теплый", L"тёплый"}, {L"теплая", L"тёплая"}, {L"теплое", L"тёплое"}, {L"теплые", L"тёплые"},
                {L"теплого", L"тёплого"}, {L"теплому", L"тёплому"}, {L"теплым", L"тёплым"}, {L"теплом", L"тёплом"}, {L"теплой", L"тёплой"}, {L"теплую", L"тёплую"},
                {L"темный", L"тёмный"}, {L"темная", L"тёмная"}, {L"темное", L"тёмное"}, {L"темные", L"тёмные"},
                {L"темного", L"тёмного"}, {L"темному", L"тёмному"}, {L"темным", L"тёмным"}, {L"темном", L"тёмном"}, {L"темной", L"тёмной"}, {L"темную", L"тёмную"},
                {L"легкий", L"лёгкий"}, {L"легкая", L"лёгкая"}, {L"легкое", L"лёгкое"}, {L"легкие", L"лёгкие"},
                {L"легкого", L"лёгкого"}, {L"легкому", L"лёгкому"}, {L"легким", L"лёгким"}, {L"легком", L"лёгком"}, {L"легкой", L"лёгкой"}, {L"легкую", L"лёгкую"},
                {L"тяжелый", L"тяжёлый"}, {L"тяжелая", L"тяжёлая"}, {L"тяжелое", L"тяжёлое"}, {L"тяжелые", L"тяжёлые"},
                {L"тяжелого", L"тяжёлого"}, {L"тяжелому", L"тяжёлому"}, {L"тяжелым", L"тяжёлым"}, {L"тяжелом", L"тяжёлом"}, {L"тяжелой", L"тяжёлой"}, {L"тяжелую", L"тяжёлую"},
                {L"дешевый", L"дешёвый"}, {L"дешевая", L"дешёвая"}, {L"дешевое", L"дешёвое"}, {L"дешевые", L"дешёвые"},
                {L"далекий", L"далёкий"}, {L"далекая", L"далёкая"}, {L"далекое", L"далёкое"}, {L"далекие", L"далёкие"}
            };
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

            static const std::unordered_set<std::wstring> vseAfterPlurals = {
                L"мы", L"вы", L"они", L"эти", L"те", L"остальные", L"прочие", L"другие",
                L"наши", L"ваши", L"мои", L"твои", L"свои", L"его", L"ее", L"её", L"их",
                L"два", L"две", L"три", L"четыре", L"пять", L"шесть", L"семь", L"восемь",
                L"девять", L"десять", L"двое", L"трое", L"четверо", L"вдвоем", L"вдвоём",
                L"втроем", L"втроём", L"вчетвером", L"люди", L"друзья", L"дела", L"слова",
                L"дома", L"дни", L"годы", L"новости", L"вопросы", L"задачи", L"проблемы",
                L"машины", L"окна", L"руки", L"ноги", L"глаза", L"места", L"города",
                L"вечера", L"студенты", L"родители", L"дети", L"гости", L"коллеги"
            };
            static const std::unordered_set<std::wstring> singularNonPluralT = {
                L"может", L"будет", L"идет", L"идёт", L"нет", L"ответ", L"привет", L"секрет", L"совет",
                L"свет", L"цвет", L"билет", L"пакет", L"обед", L"интернет", L"университет"
            };
            static const std::unordered_set<std::wstring> vsyoNextPredicates = {
                L"равно", L"время", L"еще", L"ещё", L"такое", L"так", L"это", L"то", L"дело",
                L"было", L"стало", L"будет", L"прошло", L"получилось", L"работает", L"идет", L"идёт",
                L"хорошо", L"плохо", L"нормально", L"отлично", L"понятно", L"ясно", L"готово",
                L"просто", L"сложно", L"важно", L"верно", L"правильно", L"красиво", L"интересно",
                L"весело", L"грустно", L"нужно", L"надо", L"нельзя", L"можно", L"лучше", L"хуже",
                L"больше", L"меньше", L"быстрее", L"медленнее", L"труднее", L"проще", L"дальше", L"ближе"
            };

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

            // 3. Омографы с контекстным выбором ударной гласной
            if (w == L"дела") {
                static const std::unordered_set<std::wstring> genTriggers = {
                    L"без", L"от", L"для", L"до", L"из", L"нет", L"много", L"мало", L"сколько",
                    L"больше", L"меньше", L"какого", L"этого", L"того", L"моего", L"твоего",
                    L"своего", L"вашего", L"нашего", L"общего", L"большого", L"хорошего",
                    L"доброго", L"нового", L"важного", L"одного", L"суть", L"мастер"
                };
                return (genTriggers.count(prev) || next == L"нет") ? L"де\u0301ла" : L"дела\u0301";
            }
            if (w == L"дома") {
                static const std::unordered_set<std::wstring> plPrev = {
                    L"новые", L"большие", L"маленькие", L"красивые", L"старые", L"высокие", L"низкие",
                    L"разные", L"все", L"эти", L"те", L"наши", L"ваши", L"мои", L"твои", L"свои",
                    L"соседние", L"жилые", L"многоэтажные", L"деревянные", L"каменные", L"кирпичные"
                };
                static const std::unordered_set<std::wstring> plNext = {
                    L"стоят", L"строят", L"строятся", L"видны", L"горят", L"растут",
                    L"высокие", L"большие", L"красивые", L"новые", L"старые"
                };
                return (plPrev.count(prev) || plNext.count(next)) ? L"дома\u0301" : L"до\u0301ма";
            }
            if (w == L"воды") {
                static const std::unordered_set<std::wstring> plWater = {
                    L"теплые", L"тёплые", L"минеральные", L"грунтовые", L"подземные",
                    L"сточные", L"лечебные", L"целебные", L"термальные", L"вешние", L"талые"
                };
                return (plWater.count(prev) || next == L"вышли" || next == L"поднялись") ? L"во\u0301ды" : L"воды\u0301";
            }
            if (w == L"руки" || w == L"ноги") {
                static const std::unordered_set<std::wstring> genTriggers = {
                    L"без", L"у", L"от", L"до", L"около", L"возле", L"нет", L"много", L"мало",
                    L"левой", L"правой", L"больной", L"здоровой", L"одной", L"этой", L"той", L"моей", L"твоей"
                };
                bool gen = genTriggers.count(prev) || next == L"нет";
                if (w == L"руки") return gen ? L"руки\u0301" : L"ру\u0301ки";
                return gen ? L"ноги\u0301" : L"но\u0301ги";
            }
            if (w == L"окна") {
                static const std::unordered_set<std::wstring> genTriggers = {
                    L"из", L"у", L"от", L"до", L"около", L"возле", L"мимо", L"без", L"нет",
                    L"два", L"три", L"четыре", L"одного", L"этого", L"того", L"моего", L"нашего",
                    L"большого", L"маленького", L"открытого", L"закрытого", L"первого"
                };
                return (genTriggers.count(prev) || next == L"нет") ? L"окна\u0301" : L"о\u0301кна";
            }
            if (w == L"слова") {
                static const std::unordered_set<std::wstring> genTriggers = {
                    L"без", L"ни", L"от", L"из", L"после", L"до", L"нет", L"два", L"три", L"четыре",
                    L"каждого", L"первого", L"последнего", L"этого", L"того", L"одного",
                    L"доброго", L"плохого", L"нового", L"русского", L"главного", L"нужного", L"смысл", L"значение"
                };
                return (genTriggers.count(prev) || next == L"нет") ? L"сло\u0301ва" : L"слова\u0301";
            }
            if (w == L"глаза") {
                static const std::unordered_set<std::wstring> genTriggers = {
                    L"без", L"из", L"у", L"от", L"около", L"возле", L"левого", L"правого",
                    L"одного", L"больного", L"два", L"три", L"четыре", L"открытого", L"закрытого"
                };
                return genTriggers.count(prev) ? L"гла\u0301за" : L"глаза\u0301";
            }
            if (w == L"душа") {
                static const std::unordered_set<std::wstring> genTriggers = {
                    L"без", L"из", L"после", L"для", L"у", L"до", L"от",
                    L"холодного", L"теплого", L"тёплого", L"горячего", L"летнего", L"контрастного"
                };
                return genTriggers.count(prev) ? L"ду\u0301ша" : L"душа\u0301";
            }
            if (w == L"города") {
                static const std::unordered_set<std::wstring> genTriggers = {
                    L"из", L"до", L"от", L"у", L"около", L"возле", L"без", L"мимо", L"центр", L"центре",
                    L"окраина", L"жители", L"жителей", L"мэр", L"два", L"три", L"четыре",
                    L"нашего", L"вашего", L"моего", L"этого", L"того", L"большого", L"маленького",
                    L"родного", L"красивого", L"нового", L"старого", L"каждого", L"одного"
                };
                return genTriggers.count(prev) ? L"го\u0301рода" : L"города\u0301";
            }
            if (w == L"вечера") {
                static const std::unordered_set<std::wstring> plTriggers = {
                    L"теплые", L"тёплые", L"длинные", L"зимние", L"летние", L"осенние", L"весенние",
                    L"добрые", L"тихие", L"холодные", L"хорошие", L"уютные", L"эти", L"те", L"все", L"наши"
                };
                return (plTriggers.count(prev) || next == L"будут" || next == L"были" || next == L"стали")
                       ? L"вечера\u0301" : L"ве\u0301чера";
            }
            if (w == L"утра") {
                static const std::unordered_set<std::wstring> genAdj = {
                    L"доброго", L"раннего", L"прекрасного", L"свежего", L"следующего",
                    L"хорошего", L"новогоднего", L"воскресного", L"субботнего", L"сегодняшнего", L"завтрашнего"
                };
                static const std::unordered_set<std::wstring> timeTriggers = {
                    L"с", L"до", L"от", L"около", L"после", L"часа", L"часов", L"половина",
                    L"один", L"два", L"три", L"четыре", L"пять", L"шесть", L"семь", L"восемь",
                    L"девять", L"десять", L"одиннадцать", L"двенадцать",
                    L"двух", L"трех", L"трёх", L"четырех", L"четырёх", L"пяти", L"шести",
                    L"семи", L"восьми", L"девяти", L"десяти", L"одиннадцати", L"двенадцати", L"рассвета"
                };
                if (genAdj.count(prev)) return L"у\u0301тра";
                if (prev == L"самого") return (prev2 == L"с" || prev2 == L"до" || prev2 == L"от") ? L"утра\u0301" : L"у\u0301тра";
                if (timeTriggers.count(prev)) return L"утра\u0301";
                return L"у\u0301тра";
            }
            if (w == L"стоит") {
                static const std::unordered_set<std::wstring> locPreps = {
                    L"в", L"во", L"на", L"у", L"под", L"возле", L"около", L"рядом", L"перед", L"за", L"тут", L"там", L"дома"
                };
                static const std::unordered_set<std::wstring> costPrev = {
                    L"сколько", L"много", L"мало", L"дорого", L"дешево", L"дёшево", L"недорого", L"сколько-то"
                };
                static const std::unordered_set<std::wstring> costNext = {
                    L"дорого", L"дешево", L"дёшево", L"недорого", L"рублей", L"рубля", L"рубль",
                    L"тысяч", L"тысячи", L"тысячу", L"миллион", L"миллиона", L"миллионов",
                    L"копеек", L"денег", L"много", L"мало", L"ли", L"внимания", L"времени", L"труда", L"того"
                };
                bool nextInf = !next.empty() &&
                               (endsWithW(next, L"ть") || endsWithW(next, L"ти") ||
                                endsWithW(next, L"чь") || endsWithW(next, L"ться"));
                bool cost = costPrev.count(prev) || costNext.count(next) || nextInf ||
                            ((prev2 == L"ничего" || prev2 == L"того") && prev == L"не") ||
                            ((prev == L"не" || prev == L"это" || prev == L"того") && !locPreps.count(next));
                return cost ? L"сто\u0301ит" : L"стои\u0301т";
            }
            if (w == L"потом") {
                static const std::unordered_set<std::wstring> sweatPrev = {
                    L"с", L"под", L"от", L"пахнет", L"покрыт", L"покрыта", L"покрыты", L"облит", L"седьмым", L"холодным"
                };
                return sweatPrev.count(prev) ? L"по\u0301том" : L"пото\u0301м";
            }
            if (w == L"уже") {
                static const std::unordered_set<std::wstring> narrowPrev = {
                    L"намного", L"гораздо", L"чуть", L"еще", L"ещё", L"немного", L"значительно",
                    L"стала", L"стал", L"стало", L"была", L"был", L"было", L"будет", L"оказалась",
                    L"дорога", L"улица", L"тропа", L"река", L"проход", L"вход", L"дверь"
                };
                return (narrowPrev.count(prev) || next == L"чем") ? L"у\u0301же" : L"уже\u0301";
            }
            if (w == L"сорок") {
                static const std::unordered_set<std::wstring> birdPrev = {
                    L"стая", L"много", L"несколько", L"пять", L"десять", L"белых", L"черных", L"чёрных"
                };
                return birdPrev.count(prev) ? L"соро\u0301к" : L"со\u0301рок";
            }
            if (w == L"дорогой") {
                static const std::unordered_set<std::wstring> roadPrev = {
                    L"этой", L"той", L"другой", L"длинной", L"короткой", L"прямой", L"обратной",
                    L"всей", L"всю", L"хорошей", L"ровной", L"новой", L"старой", L"знакомой",
                    L"лесной", L"горной", L"пыльной", L"скользкой", L"темной", L"тёмной",
                    L"светлой", L"узкой", L"широкой", L"какой", L"одной",
                    L"идти", L"иду", L"идет", L"идёт", L"шел", L"шёл", L"шла", L"шли",
                    L"ехать", L"еду", L"едет", L"ехал", L"пойти", L"поехать", L"бежать", L"пройти", L"вернуться"
                };
                return (roadPrev.count(prev) || next == L"домой" || next == L"обратно")
                       ? L"доро\u0301гой" : L"дорого\u0301й";
            }
            if (w == L"большая" || w == L"большую") {
                bool comp = (next == L"часть" || next == L"половина" || next == L"степень" ||
                             next == L"частью" || next == L"половину" || next == L"степени");
                if (w == L"большая") return comp ? L"бо\u0301льшая" : L"больша\u0301я";
                return comp ? L"бо\u0301льшую" : L"большу\u0301ю";
            }
            if (w == L"большим" || w == L"больших") {
                bool comp = (prev == L"гораздо" || prev == L"намного" || prev == L"значительно" ||
                             next == L"счетом" || next == L"счётом" || next == L"количеством");
                if (w == L"большим") return comp ? L"бо\u0301льшим" : L"больши\u0301м";
                return comp ? L"бо\u0301льших" : L"больши\u0301х";
            }
            if (w == L"моя") {
                static const std::unordered_set<std::wstring> washObj = {
                    L"пол", L"полы", L"посуду", L"руки", L"окна", L"машину", L"чашку", L"лицо"
                };
                return washObj.count(next) ? L"мо\u0301я" : L"моя";
            }
            if (w == L"мою") {
                static const std::unordered_set<std::wstring> washObj = {
                    L"руки", L"посуду", L"пол", L"полы", L"голову", L"чашку", L"лицо", L"яблоки", L"овощи", L"фрукты"
                };
                return washObj.count(next) ? L"мо\u0301ю" : L"мою\u0301";
            }
            if (w == L"бегу") {
                return (prev == L"по" || prev == L"о" || prev == L"к") ? L"бе\u0301гу" : L"бегу\u0301";
            }
            if (w == L"начала") {
                bool gen = (prev == L"с" || prev == L"до" || prev == L"от" || prev == L"после" ||
                            prev == L"у" || (prev == L"самого" && (prev2 == L"с" || prev2 == L"до" || prev2 == L"от")));
                return gen ? L"нача\u0301ла" : L"начала\u0301";
            }
            if (w == L"спала") {
                static const std::unordered_set<std::wstring> dropSubj = {
                    L"температура", L"жара", L"вода", L"боль", L"опухоль", L"усталость", L"волна"
                };
                return dropSubj.count(prev) ? L"спа\u0301ла" : L"спала\u0301";
            }
            if (w == L"жила") {
                return (prev == L"золотая" || prev == L"толстая" || prev == L"тонкая" || prev == L"водная")
                       ? L"жи\u0301ла" : L"жила\u0301";
            }
            if (w == L"замок") {
                static const std::unordered_set<std::wstring> castleAdj = {
                    L"старый", L"древний", L"средневековый", L"королевский", L"рыцарский",
                    L"воздушный", L"песочный", L"каменный", L"красивый", L"мрачный", L"огромный", L"старинный"
                };
                return castleAdj.count(prev) ? L"за\u0301мок" : L"замо\u0301к";
            }
            if (w == L"места") {
                static const std::unordered_set<std::wstring> plTriggers = {
                    L"эти", L"те", L"наши", L"ваши", L"мои", L"твои", L"свои", L"все", L"свободные",
                    L"хорошие", L"красивые", L"новые", L"разные", L"задние", L"передние", L"первые"
                };
                return (plTriggers.count(prev) || next == L"свободны" || next == L"заняты" || next == L"есть")
                       ? L"места\u0301" : L"ме\u0301ста";
            }
            if (w == L"письма") {
                static const std::unordered_set<std::wstring> genTriggers = {
                    L"без", L"от", L"для", L"нет", L"из", L"до", L"два", L"три", L"четыре",
                    L"этого", L"того", L"одного", L"нового", L"моего", L"вашего", L"длинного", L"важного"
                };
                return (genTriggers.count(prev) || next == L"нет") ? L"письма\u0301" : L"пи\u0301сьма";
            }
            if (w == L"числа") {
                static const std::unordered_set<std::wstring> genTriggers = {
                    L"без", L"от", L"до", L"из", L"какого", L"первого", L"второго", L"третьего",
                    L"четвертого", L"пятого", L"десятого", L"двадцатого", L"два", L"три", L"четыре",
                    L"этого", L"того", L"одного", L"нового", L"целого"
                };
                return genTriggers.count(prev) ? L"числа\u0301" : L"чи\u0301сла";
            }
            if (w == L"лица") {
                static const std::unordered_set<std::wstring> genTriggers = {
                    L"с", L"без", L"от", L"до", L"черты", L"выражение", L"два", L"три", L"четыре",
                    L"этого", L"того", L"одного", L"знакомого", L"своего"
                };
                return genTriggers.count(prev) ? L"лица\u0301" : L"ли\u0301ца";
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
        out.reserve(in.size() + 32);
        std::size_t i = 0;

        while (i < in.size()) {
            // Сокращения (с проверкой границ слова, без чувствительности к заглавной первой букве)
            if (i + 3 <= in.size()) {
                std::wstring sub3 = toLowerSimple(in.substr(i, 3));
                if (sub3 == L"ржя") {
                    bool leftOk = (i == 0 || !isLetterW(in[i - 1]));
                    bool rightOk = (i + 3 >= in.size() || !isLetterW(in[i + 3]));
                    if (leftOk && rightOk) {
                        out += L"русский жестовый язык";
                        i += 3;
                        continue;
                    }
                }
            }
            if (i + 4 <= in.size() && (i == 0 || !isLetterW(in[i - 1]))) {
                std::wstring sub4 = toLowerSimple(in.substr(i, 4));
                if (sub4 == L"т.д.") {
                    out += L"так далее";
                    i += 4;
                    continue;
                }
                if (sub4 == L"т.п.") {
                    out += L"тому подобное";
                    i += 4;
                    continue;
                }
                if (sub4 == L"т.е.") {
                    out += L"то есть";
                    i += 4;
                    continue;
                }
            }

            // Отрицательные числа (знак '-' / U+2212 перед цифрами, не являющийся диапазоном/дефисом)
            if ((in[i] == L'-' || in[i] == L'\u2212') && i + 1 < in.size() && isDigitW(in[i + 1])) {
                bool leftOk = (i == 0 || (!isDigitW(in[i - 1]) && !isLetterW(in[i - 1]) &&
                                          in[i - 1] != L'-' && in[i - 1] != L'\u2212'));
                if (leftOk) {
                    std::size_t digitStart = i + 1;
                    std::size_t j = digitStart;
                    long long num = 0;
                    bool tooLarge = false;
                    while (j < in.size() && isDigitW(in[j])) {
                        if (j - digitStart >= 12) {
                            tooLarge = true;
                        } else {
                            num = num * 10 + (in[j] - L'0');
                        }
                        ++j;
                    }
                    if (!tooLarge && num <= 999999999999LL) {
                        if (num == 0) {
                            out += L"ноль";
                        } else {
                            out += L"минус ";
                            out += numberToWords(num);
                        }
                    } else {
                        out += in.substr(i, j - i);
                    }
                    i = j;
                    continue;
                }
            }

            // Числа до 999 999 999 999 (без знакового переполнения)
            if (isDigitW(in[i])) {
                std::size_t j = i;
                long long num = 0;
                bool tooLarge = false;
                while (j < in.size() && isDigitW(in[j])) {
                    if (j - i >= 12) {
                        tooLarge = true;
                    } else {
                        num = num * 10 + (in[j] - L'0');
                    }
                    ++j;
                }
                if (!tooLarge && num <= 999999999999LL) {
                    out += numberToWords(num);
                } else {
                    out += in.substr(i, j - i);
                }
                i = j;
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
