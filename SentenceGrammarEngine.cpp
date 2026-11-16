#include "SentenceGrammarEngine.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace HandTracker {

    static std::filesystem::path pathFromUtf8(const std::string& s) {
        if (s.empty()) return {};
        return std::filesystem::path(
            std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
    }

    static uint32_t popcount32(uint32_t x) {
        x = x - ((x >> 1) & 0x55555555u);
        x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
        return (((x + (x >> 4)) & 0x0F0F0F0Fu) * 0x01010101u) >> 24;
    }

    static std::wstring utf8ToWide(const std::string& utf8) {
        std::wstring out;
        out.reserve(utf8.size());
        size_t i = 0;
        const size_t n = utf8.size();
        // Skip UTF-8 BOM if present
        if (n >= 3 && (unsigned char)utf8[0] == 0xEF &&
            (unsigned char)utf8[1] == 0xBB && (unsigned char)utf8[2] == 0xBF) {
            i = 3;
        }
        while (i < n) {
            unsigned char c = (unsigned char)utf8[i];
            uint32_t cp = 0;
            size_t len = 1;
            if (c < 0x80) {
                cp = c;
                len = 1;
            } else if ((c & 0xE0) == 0xC0 && i + 1 < n) {
                unsigned char c1 = (unsigned char)utf8[i + 1];
                if ((c1 & 0xC0) == 0x80) {
                    cp = ((uint32_t)(c & 0x1F) << 6) | (uint32_t)(c1 & 0x3F);
                    len = 2;
                } else {
                    ++i;
                    continue;
                }
            } else if ((c & 0xF0) == 0xE0 && i + 2 < n) {
                unsigned char c1 = (unsigned char)utf8[i + 1];
                unsigned char c2 = (unsigned char)utf8[i + 2];
                if ((c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80) {
                    cp = ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(c1 & 0x3F) << 6) | (uint32_t)(c2 & 0x3F);
                    len = 3;
                } else {
                    ++i;
                    continue;
                }
            } else if ((c & 0xF8) == 0xF0 && i + 3 < n) {
                unsigned char c1 = (unsigned char)utf8[i + 1];
                unsigned char c2 = (unsigned char)utf8[i + 2];
                unsigned char c3 = (unsigned char)utf8[i + 3];
                if ((c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80 && (c3 & 0xC0) == 0x80) {
                    cp = ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(c1 & 0x3F) << 12) |
                         ((uint32_t)(c2 & 0x3F) << 6) | (uint32_t)(c3 & 0x3F);
                    len = 4;
                } else {
                    ++i;
                    continue;
                }
            } else {
                ++i;
                continue;
            }
            i += len;

            if (cp == 0xFEFF) continue; // BOM
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

    static wchar_t toLowerRu(wchar_t c) {
        if (c >= L'А' && c <= L'Я') return c + (L'а' - L'А');
        if (c == L'Ё') return L'ё';
        if (c >= L'A' && c <= L'Z') return c + (L'a' - L'A');
        return c;
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
        switch (c) {
            case L'q': case L'Q': return L'й';
            case L'w': case L'W': return L'ц';
            case L'e': case L'E': return L'у';
            case L'r': case L'R': return L'к';
            case L't': case L'T': return L'е';
            case L'y': case L'Y': return L'н';
            case L'u': case L'U': return L'г';
            case L'i': case L'I': return L'ш';
            case L'o': case L'O': return L'щ';
            case L'p': case L'P': return L'з';
            case L'[': case L'{': return L'х';
            case L']': case L'}': return L'ъ';
            case L'a': case L'A': return L'ф';
            case L's': case L'S': return L'ы';
            case L'd': case L'D': return L'в';
            case L'f': case L'F': return L'а';
            case L'g': case L'G': return L'п';
            case L'h': case L'H': return L'р';
            case L'j': case L'J': return L'о';
            case L'k': case L'K': return L'л';
            case L'l': case L'L': return L'д';
            case L';': case L':': return L'ж';
            case L'\'': case L'"': return L'э';
            case L'z': case L'Z': return L'я';
            case L'x': case L'X': return L'ч';
            case L'c': case L'C': return L'с';
            case L'v': case L'V': return L'м';
            case L'b': case L'B': return L'и';
            case L'n': case L'N': return L'т';
            case L'm': case L'M': return L'ь';
            case L',': case L'<': return L'б';
            case L'.': case L'>': return L'ю';
            case L'`': case L'~': return L'ё';
            default: return toLowerRu(c);
        }
    }

    // Проверка фонетической/клавиатурной/дактильной близости букв
    static bool areSimilarChars(wchar_t a, wchar_t b) {
        a = foldYoChar(a);
        b = foldYoChar(b);
        if (a == b) return true;
        static const wchar_t* pairs[] = {
            L"ао", L"еи", L"еэ", L"иы", L"ую", L"ая", L"ое",
            L"дт", L"бп", L"вф", L"гк", L"зс", L"жш", L"шщ", L"чщ", L"цс",
            // Соседние клавиши ЙЦУКЕН и похожие жесты РЖЯ
            L"чя", L"см", L"ми", L"ит", L"ть", L"бь", L"бю", L"жэ", L"хъ",
            L"фы", L"ыв", L"ва", L"ап", L"пр", L"ро", L"ол", L"лд", L"дж",
            L"йц", L"цу", L"ук", L"ке", L"ен", L"нг", L"гш", L"шщ", L"щз", L"зх",
            L"лм", L"нм", L"рл", L"вм", L"кх", L"сч", L"ос", L"пн", L"тм"
        };
        for (const wchar_t* p : pairs) {
            if ((a == p[0] && b == p[1]) || (a == p[1] && b == p[0])) return true;
        }
        return false;
    }

    SentenceGrammarEngine::SentenceGrammarEngine() {
        addSupplementalLexicon();
        rebuildIndices();
    }

    void SentenceGrammarEngine::addSupplementalLexicon() {
        static const std::pair<const wchar_t*, int> supplemental[] = {
            // Имена собственные (отсутствуют в базовом нарицательном словаре)
            {L"анна", 22000}, {L"мария", 22000}, {L"елена", 20000}, {L"ольга", 20000},
            {L"татьяна", 18000}, {L"ирина", 20000}, {L"светлана", 18000}, {L"наталья", 18000},
            {L"екатерина", 18000}, {L"дмитрий", 22000}, {L"александр", 22000}, {L"сергей", 22000},
            {L"алексей", 20000}, {L"андрей", 20000}, {L"максим", 18000}, {L"михаил", 20000},
            {L"николай", 18000}, {L"владимир", 20000}, {L"павел", 18000}, {L"кирилл", 15000},

            // Города и география (отсутствуют в базовом нарицательном словаре)
            {L"москва", 25000}, {L"москвы", 22000}, {L"москве", 20000}, {L"москву", 20000}, {L"москвой", 15000},
            {L"петербург", 20000}, {L"петербурга", 18000}, {L"петербурге", 18000}, {L"санкт-петербург", 15000},
            {L"россия", 25000}, {L"россии", 25000}, {L"россию", 20000}, {L"россией", 15000},
            {L"череповец", 15000}, {L"череповца", 12000}, {L"череповце", 12000},
            {L"казань", 15000}, {L"новосибирск", 15000}, {L"екатеринбург", 15000},

            // Повседневная и ИТ / РЖЯ лексика (отсутствующая или с заниженной частотой в dict_ru.txt)
            {L"программист", 8000}, {L"программиста", 5000}, {L"программисты", 5000},
            {L"студент", 15000}, {L"студента", 10000}, {L"студенты", 10000},
            {L"университет", 15000}, {L"университета", 12000}, {L"университете", 12000},
            {L"кафедра", 8000}, {L"экзамен", 12000}, {L"зачет", 8000},
            {L"телефон", 20000}, {L"телефона", 15000}, {L"телефону", 12000}, {L"телефоном", 12000}, {L"телефоне", 12000},
            {L"смартфон", 12000}, {L"смартфона", 8000},
            {L"интернет", 18000}, {L"интернета", 15000}, {L"интернете", 15000},
            {L"приложение", 15000}, {L"приложения", 12000}, {L"приложении", 10000},
            {L"сообщение", 15000}, {L"сообщения", 12000}, {L"видеозвонок", 8000},
            {L"жест", 15000}, {L"жеста", 10000}, {L"жесты", 12000}, {L"жестов", 12000},
            {L"жестовый", 10000}, {L"дактиль", 8000}, {L"дактильный", 8000},
            {L"микрофон", 10000}, {L"вебкамера", 8000}, {L"аудио", 10000}, {L"нейросеть", 8000},
            {L"ржя", 12000}, {L"озвучить", 10000}, {L"переводчик", 12000},

            // Формы глаголов, местоимений и падежные формы, используемые в грамматике
            {L"помочь", 22000}, {L"помоги", 18000}, {L"помогите", 25000}, {L"помогать", 18000},
            {L"помогу", 16000}, {L"поможешь", 14000}, {L"поможет", 18000}, {L"поможем", 12000},
            {L"поможете", 12000}, {L"помогут", 12000}, {L"помог", 15000}, {L"помогла", 14000}, {L"помогли", 14000},
            {L"произошло", 16000}, {L"произошел", 14000}, {L"произошёл", 14000},
            {L"произошла", 14000}, {L"произошли", 12000}, {L"произойдет", 15000}, {L"произойдёт", 15000},
            {L"спросила", 14000}, {L"ответила", 14000}, {L"забыли", 14000},
            {L"опоздали", 12000}, {L"позвонила", 12000}, {L"осталась", 14000},
            {L"него", 22000}, {L"неё", 22000}, {L"нее", 22000}, {L"ними", 20000},
            {L"мной", 20000}, {L"тобой", 20000}, {L"нём", 20000}, {L"нем", 20000},
            {L"большое", 25000}, {L"огромное", 20000},
            {L"школу", 20000}, {L"школы", 20000}, {L"школе", 20000},
            {L"работу", 25000}, {L"работы", 25000}, {L"работе", 25000},
            {L"улицу", 18000}, {L"улице", 20000},
            {L"больницу", 15000}, {L"больницы", 15000}, {L"больнице", 15000},
            {L"аптеку", 12000}, {L"аптеки", 12000}, {L"аптеке", 12000},
            {L"квартиру", 18000}, {L"квартиры", 18000}, {L"квартире", 18000},
            {L"комнату", 18000}, {L"комнате", 18000},
            {L"машину", 20000}, {L"машины", 20000}, {L"машине", 20000},
            {L"праздником", 18000}, {L"другом", 18000}, {L"другу", 18000},
            {L"подругой", 15000}, {L"подруге", 15000},
            {L"мамой", 15000}, {L"маме", 15000}, {L"папой", 12000}, {L"папе", 12000},
            {L"врачу", 15000}, {L"времени", 24000}, {L"денег", 22000},
            {L"дома", 28000}, {L"дому", 20000}, {L"домой", 25000}, {L"доме", 18000},
            {L"магазине", 15000}, {L"городе", 18000}, {L"столе", 15000}, {L"парке", 12000},
            {L"кухня", 12000}, {L"кухню", 12000}, {L"кухне", 12000}, {L"кухни", 12000},
            {L"деревня", 12000}, {L"деревню", 12000}, {L"деревне", 12000}, {L"деревни", 12000},
            {L"библиотека", 10000}, {L"библиотеку", 10000}, {L"библиотеке", 10000},
            {L"остановка", 10000}, {L"остановку", 10000}, {L"остановке", 10000},
            {L"почта", 10000}, {L"почту", 10000}, {L"почте", 10000},
            {L"страна", 18000}, {L"страну", 15000}, {L"стране", 15000},
            {L"моего", 20000}, {L"моему", 18000}, {L"моим", 18000}, {L"моем", 18000}, {L"моём", 18000}, {L"моей", 20000}, {L"мою", 20000},
            {L"нового", 15000}, {L"новому", 12000}, {L"новым", 15000}, {L"новом", 15000}, {L"новой", 15000}, {L"новую", 15000},
            {L"хорошего", 15000}, {L"хорошему", 12000}, {L"хорошим", 15000}, {L"хорошем", 15000}, {L"хорошей", 15000}, {L"хорошую", 15000},
            {L"большого", 15000}, {L"большому", 12000}, {L"большим", 15000}, {L"большом", 15000}, {L"большую", 15000},
            {L"красивого", 12000}, {L"красивому", 10000}, {L"красивым", 12000}, {L"красивом", 12000}, {L"красивой", 12000}, {L"красивую", 12000},
            {L"друга", 18000}, {L"подруги", 15000}, {L"врача", 15000}, {L"воды", 18000}, {L"хлеба", 15000}, {L"утра", 18000},
            {L"утром", 20000}, {L"годом", 20000}, {L"настроении", 12000}, {L"настроения", 12000}, {L"свободного", 12000},

            // Дефисные слова
            {L"какой-нибудь", 18000}, {L"какая-нибудь", 18000}, {L"какое-нибудь", 18000}, {L"какие-нибудь", 18000},
            {L"что-нибудь", 22000}, {L"кто-нибудь", 20000}, {L"где-нибудь", 18000}, {L"куда-нибудь", 15000},
            {L"когда-нибудь", 20000}, {L"как-нибудь", 18000}, {L"сколько-нибудь", 12000}, {L"чей-нибудь", 12000},
            {L"какой-либо", 12000}, {L"какая-либо", 12000}, {L"какое-либо", 12000}, {L"какие-либо", 12000},
            {L"что-либо", 15000}, {L"кто-либо", 15000}, {L"где-либо", 12000}, {L"когда-либо", 15000}, {L"как-либо", 12000},
            {L"кое-что", 18000}, {L"кое-кто", 15000}, {L"кое-где", 12000}, {L"кое-как", 15000},
            {L"кое-какой", 12000}, {L"кое-какие", 12000},
            {L"кто-то", 25000}, {L"что-то", 28000}, {L"где-то", 22000}, {L"куда-то", 18000},
            {L"когда-то", 22000}, {L"как-то", 25000}, {L"какой-то", 25000}, {L"какая-то", 25000},
            {L"какое-то", 25000}, {L"какие-то", 25000},
            {L"всё-таки", 22000}, {L"все-таки", 22000}, {L"по-моему", 20000}, {L"по-твоему", 15000},
            {L"по-русски", 18000}, {L"по-английски", 15000}, {L"во-первых", 18000}, {L"во-вторых", 18000},
            {L"из-за", 25000}, {L"из-под", 20000}
        };

        for (const auto& item : supplemental) {
            auto it = dictionary_.find(item.first);
            if (it == dictionary_.end() || item.second > it->second) {
                dictionary_[item.first] = item.second;
            }
        }
    }

    void SentenceGrammarEngine::rebuildIndices() {
        dictEntries_.clear();
        bucketsByFirstLen_.clear();
        bucketsBySecondLen_.clear();
        dictEntries_.reserve(dictionary_.size());

        const int minBucketFreq = (dictionary_.size() >= 5000) ? 450 : 1;
        for (const auto& kv : dictionary_) {
            uint32_t idx = (uint32_t)dictEntries_.size();
            dictEntries_.push_back({kv.first, kv.second, computeCharMask(kv.first)});
            if (kv.first.size() >= 2 && kv.second >= minBucketFreq) {
                wchar_t fc = foldYoChar(kv.first[0]);
                wchar_t sc = foldYoChar(kv.first[1]);
                bucketsByFirstLen_[fc][kv.first.size()].push_back(idx);
                bucketsBySecondLen_[sc][kv.first.size()].push_back(idx);
            }
        }
    }

    bool SentenceGrammarEngine::loadDictionary(const std::string& dictPath) {
        std::ifstream file(pathFromUtf8(dictPath));
        if (!file.is_open()) return false;

        dictionary_.clear();
        dictionary_.reserve(350000);

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
            if (wline.empty()) continue;

            size_t tabPos = wline.find(L'\t');
            std::wstring word;
            int freq = 1000;
            if (tabPos != std::wstring::npos) {
                word = toLowerStr(wline.substr(0, tabPos));
                int parsed = 0;
                bool hasDigit = false;
                for (size_t i = tabPos + 1; i < wline.size(); ++i) {
                    wchar_t dc = wline[i];
                    if (dc >= L'0' && dc <= L'9') {
                        hasDigit = true;
                        if (parsed < 100000000) {
                            parsed = parsed * 10 + (int)(dc - L'0');
                        }
                    } else if (hasDigit) {
                        break;
                    }
                }
                if (hasDigit) freq = parsed;
            } else {
                word = toLowerStr(wline);
            }

            while (!word.empty() && (word.front() == L' ' || word.front() == L'\t')) word.erase(word.begin());
            while (!word.empty() && (word.back() == L' ' || word.back() == L'\t')) word.pop_back();
            if (!word.empty()) {
                auto it = dictionary_.find(word);
                if (it == dictionary_.end() || freq > it->second) {
                    dictionary_[word] = freq;
                }
            }
        }

        if (dictionary_.empty()) {
            addSupplementalLexicon();
            rebuildIndices();
            return false;
        }

        addSupplementalLexicon();
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

    int SentenceGrammarEngine::editDistance(const std::wstring& s1, const std::wstring& s2) const {
        const size_t m = s1.size();
        const size_t n = s2.size();
        if (m == 0) return (int)n;
        if (n == 0) return (int)m;

        int stackBuf[64 * 64];
        std::vector<int> heapBuf;
        int* dp = stackBuf;
        const size_t stride = n + 1;
        if ((m + 1) * (n + 1) > 64 * 64) {
            heapBuf.resize((m + 1) * (n + 1));
            dp = heapBuf.data();
        }

        for (size_t i = 0; i <= m; ++i) dp[i * stride] = (int)i;
        for (size_t j = 0; j <= n; ++j) dp[j] = (int)j;

        for (size_t i = 1; i <= m; ++i) {
            for (size_t j = 1; j <= n; ++j) {
                int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
                int v = std::min({
                    dp[(i - 1) * stride + j] + 1,
                    dp[i * stride + (j - 1)] + 1,
                    dp[(i - 1) * stride + (j - 1)] + cost
                });
                if (i > 1 && j > 1 && s1[i - 1] == s2[j - 2] && s1[i - 2] == s2[j - 1]) {
                    v = std::min(v, dp[(i - 2) * stride + (j - 2)] + 1);
                }
                dp[i * stride + j] = v;
            }
        }
        return dp[m * stride + n];
    }

    static const std::unordered_map<std::wstring, std::vector<std::wstring>>& getExactFixesMap() {
        static const std::unordered_map<std::wstring, std::vector<std::wstring>> exactFixes = {
            {L"аты", {L"а", L"ты"}},
            {L"авы", {L"а", L"вы"}},
            {L"ая", {L"а", L"я"}},
            {L"аон", {L"а", L"он"}},
            {L"амы", {L"а", L"мы"}},
            {L"иты", {L"и", L"ты"}},
            {L"ия", {L"и", L"я"}},
            {L"имы", {L"и", L"мы"}},
            {L"ивы", {L"и", L"вы"}},
            {L"ноя", {L"но", L"я"}},
            {L"ноты", {L"но", L"ты"}},
            {L"яне", {L"я", L"не"}},
            {L"ане", {L"а", L"не"}},
            {L"ине", {L"и", L"не"}},
            {L"яже", {L"я", L"же"}},
            {L"ябы", {L"я", L"бы"}},
            {L"незнаю", {L"не", L"знаю"}},
            {L"нехочу", {L"не", L"хочу"}},
            {L"немогу", {L"не", L"могу"}},
            {L"небуду", {L"не", L"буду"}},
            {L"ненадо", {L"не", L"надо"}},
            {L"нельзяли", {L"нельзя", L"ли"}},
            {L"какдела", {L"как", L"дела"}},
            {L"чтоэто", {L"что", L"это"}},
            {L"ктоэто", {L"кто", L"это"}},
            {L"гдеэто", {L"где", L"это"}},
            {L"уменя", {L"у", L"меня"}},
            {L"утебя", {L"у", L"тебя"}},
            {L"унас", {L"у", L"нас"}},
            {L"увас", {L"у", L"вас"}},
            {L"конечноже", {L"конечно", L"же"}},
            {L"яхочу", {L"я", L"хочу"}},
            {L"яхотел", {L"я", L"хотел"}},
            {L"яхотела", {L"я", L"хотела"}},
            {L"ябуду", {L"я", L"буду"}},
            {L"ямогу", {L"я", L"могу"}},
            {L"тыгде", {L"ты", L"где"}},
            {L"тыкак", {L"ты", L"как"}},
            {L"кароче", {L"короче"}},
            {L"кароч", {L"короче"}},
            {L"короч", {L"короче"}},
            {L"щас", {L"сейчас"}},
            {L"сейчвс", {L"сейчас"}},
            {L"спс", {L"спасибо"}},
            {L"спосибо", {L"спасибо"}},
            {L"спасиба", {L"спасибо"}},
            {L"спасбо", {L"спасибо"}},
            {L"пасибо", {L"спасибо"}},
            {L"пасиба", {L"спасибо"}},
            {L"пжлст", {L"пожалуйста"}},
            {L"пожалуста", {L"пожалуйста"}},
            {L"пожалуйсто", {L"пожалуйста"}},
            {L"здраствуйте", {L"здравствуйте"}},
            {L"здравствуйт", {L"здравствуйте"}},
            {L"здрасте", {L"здравствуйте"}},
            {L"здрасьте", {L"здравствуйте"}},
            {L"привед", {L"привет"}},
            {L"превет", {L"привет"}},
            {L"привте", {L"привет"}},
            {L"приветик", {L"привет"}},
            {L"ривет", {L"привет"}},
            {L"гулчаешь", {L"гуляешь"}},
            {L"гуляеш", {L"гуляешь"}},
            {L"делаеш", {L"делаешь"}},
            {L"хочеш", {L"хочешь"}},
            {L"можеш", {L"можешь"}},
            {L"знаеш", {L"знаешь"}},
            {L"идеш", {L"идешь"}},
            {L"живеш", {L"живешь"}},
            {L"говориш", {L"говоришь"}},
            {L"смотриш", {L"смотришь"}},
            {L"любиш", {L"любишь"}},
            {L"учишся", {L"учишься"}},
            {L"хачу", {L"хочу"}},
            {L"хател", {L"хотел"}},
            {L"сказл", {L"сказал"}},
            {L"скозал", {L"сказал"}},
            {L"бальшой", {L"большой"}},
            {L"бальшое", {L"большое"}},
            {L"харашо", {L"хорошо"}},
            {L"хорашо", {L"хорошо"}},
            {L"чиловек", {L"человек"}},
            {L"челавек", {L"человек"}},
            {L"извените", {L"извините"}},
            {L"памагите", {L"помогите"}},
            {L"сигодня", {L"сегодня"}},
            {L"севодня", {L"сегодня"}},
            {L"нибудь", {L"нибудь"}},
            {L"нибуть", {L"нибудь"}},
            {L"нибуд", {L"нибудь"}},
            {L"ниудь", {L"нибудь"}},
            {L"какиенибудь", {L"какие-нибудь"}},
            {L"какойнибудь", {L"какой-нибудь"}},
            {L"какаянибудь", {L"какая-нибудь"}},
            {L"какоенибудь", {L"какое-нибудь"}},
            {L"чтонибудь", {L"что-нибудь"}},
            {L"ктонибудь", {L"кто-нибудь"}},
            {L"гденибудь", {L"где-нибудь"}},
            {L"когданибудь", {L"когда-нибудь"}},
            {L"какнибудь", {L"как-нибудь"}},
            {L"куданибудь", {L"куда-нибудь"}},
            {L"откуданибудь", {L"откуда-нибудь"}},
            {L"чейнибудь", {L"чей-нибудь"}},
            {L"скольконибудь", {L"сколько-нибудь"}},
            {L"какиелибо", {L"какие-либо"}},
            {L"какойлибо", {L"какой-либо"}},
            {L"какаялибо", {L"какая-либо"}},
            {L"какоелибо", {L"какое-либо"}},
            {L"чтолибо", {L"что-либо"}},
            {L"ктолибо", {L"кто-либо"}},
            {L"гделибо", {L"где-либо"}},
            {L"когдалибо", {L"когда-либо"}},
            {L"каклибо", {L"как-либо"}}
        };
        return exactFixes;
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
        if (dupIndices.empty() || dupIndices.size() > 5) return {L"", 0};

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

        const auto& exactFixes = getExactFixesMap();
        auto ef = exactFixes.find(target);
        if (ef != exactFixes.end() && ef->second.size() == 1) {
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

            int lenDiff = std::abs((int)cw.size() - (int)target.size());
            if (lenDiff > maxAllowedDist) return;
            if (lenDiff == 2 && (target.size() < 6 || cfreq < 1800)) return;

            uint32_t d1 = targetMask & ~cand.charMask;
            uint32_t d2 = cand.charMask & ~targetMask;
            if ((int)popcount32(d1) > maxAllowedDist || (int)popcount32(d2) > maxAllowedDist) return;

            std::wstring foldedBuf;
            const std::wstring& foldedCand = (cw.find(L'ё') != std::wstring::npos) ? (foldedBuf = foldYoStr(cw)) : cw;
            int dist = editDistance(foldedTarget, foldedCand);
            if (dist == 0 || dist > maxAllowedDist) return;
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
            if (fc >= L'а' && fc <= L'я') scannedFirstMask |= (1u << (uint32_t)(fc - L'а'));
            auto fIt = bucketsByFirstLen_.find(fc);
            if (fIt == bucketsByFirstLen_.end()) continue;
            for (size_t len = minLen; len <= maxLen; ++len) {
                auto lIt = fIt->second.find(len);
                if (lIt == fIt->second.end()) continue;
                for (uint32_t idx : lIt->second) {
                    evaluateEntry(dictEntries_[idx]);
                }
            }
        }

        // Поиск по 2-й букве слова: мгновенно находит пропущенную, переставленную или ошибочную 1-ю букву
        // (например: "ривет" -> "привет", "пасибо" -> "спасибо", "хривет" -> "привет")
        auto scanSecondBucket = [&](wchar_t sc, size_t len) {
            auto sIt = bucketsBySecondLen_.find(sc);
            if (sIt == bucketsBySecondLen_.end()) return;
            auto lIt = sIt->second.find(len);
            if (lIt == sIt->second.end()) return;
            for (uint32_t idx : lIt->second) {
                const DictEntry& cand = dictEntries_[idx];
                wchar_t candFc = foldYoChar(cand.word[0]);
                if (candFc >= L'а' && candFc <= L'я' && (scannedFirstMask & (1u << (uint32_t)(candFc - L'а')))) {
                    continue; // уже проверено в bucketsByFirstLen_
                }
                evaluateEntry(cand);
            }
        };

        // 1-я буква пропущена (len == target.size() + 1) или первые две переставлены (len == target.size())
        scanSecondBucket(fc0, target.size() + 1);
        scanSecondBucket(fc0, target.size());
        // 1-я буква заменена на любую другую (len == target.size())
        if (target.size() >= 2) {
            wchar_t sc1 = foldYoChar(target[1]);
            if (sc1 != fc0) {
                scanSecondBucket(sc1, target.size());
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
        const auto& exactFixes = getExactFixesMap();
        auto exIt = exactFixes.find(w);
        if (exIt != exactFixes.end()) {
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
                    auto partEf = exactFixes.find(part);
                    if (partEf != exactFixes.end() && partEf->second.size() == 1) {
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

        // 2. Случайное удвоение одной или нескольких букв при задержке жеста
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
        static const std::unordered_set<std::wstring> validLen1 = {
            L"а", L"и", L"в", L"с", L"к", L"у", L"о", L"я"
        };
        static const std::unordered_set<std::wstring> validLen2 = {
            L"не", L"на", L"но", L"по", L"из", L"за", L"от", L"до", L"ты", L"мы", L"вы", L"он",
            L"то", L"же", L"бы", L"да", L"ну", L"ни", L"во", L"со", L"ко", L"ли", L"ей", L"им",
            L"их", L"её", L"ее", L"ух", L"ой", L"ай"
        };
        static const std::unordered_set<std::wstring> validShortLeft = {
            L"а", L"и", L"в", L"с", L"к", L"у", L"о", L"я",
            L"не", L"на", L"но", L"по", L"из", L"за", L"от", L"до", L"ты", L"мы", L"вы", L"он", L"то", L"же", L"бы", L"да", L"ну", L"ни", L"во", L"со", L"ко",
            L"что", L"как", L"так", L"все", L"всё", L"они", L"она", L"оно", L"кто", L"где", L"тут", L"там", L"вот", L"уже", L"еще", L"ещё", L"нет", L"или", L"при", L"про", L"без", L"под", L"над", L"для", L"мой", L"моя", L"мои", L"наш", L"ваш"
        };
        static const std::unordered_set<std::wstring> validShortRight = {
            L"я", L"ты", L"мы", L"вы", L"он", L"же", L"бы", L"ли", L"то", L"да",
            L"это", L"как", L"так", L"все", L"всё", L"они", L"она", L"оно", L"кто", L"где", L"тут", L"там", L"вот", L"нет", L"мне", L"нам", L"вам", L"них", L"ним", L"ней", L"ему", L"его", L"мой", L"моя", L"дом", L"сон", L"год", L"раз", L"час"
        };
        static const std::unordered_set<std::wstring> prefixPrepositions = {
            L"при", L"по", L"про", L"за", L"на", L"от", L"до", L"из", L"вы", L"со", L"во",
            L"об", L"под", L"над", L"без", L"у", L"о", L"с", L"в", L"к", L"ни", L"не"
        };
        auto endsWithAnyW = [](const std::wstring& s, std::initializer_list<const wchar_t*> suffixes) -> bool {
            for (const wchar_t* suf : suffixes) {
                size_t slen = 0;
                while (suf[slen] != L'\0') ++slen;
                if (s.size() > slen && s.compare(s.size() - slen, slen, suf) == 0) return true;
            }
            return false;
        };
        auto looksLikeVerbAfterPrep = [&](const std::wstring& s) -> bool {
            static const std::unordered_set<std::wstring> commonVerbForms = {
                L"могу", L"может", L"можешь", L"можем", L"можете", L"могут",
                L"иду", L"идет", L"идёт", L"иди", L"хочу", L"хочет", L"хочешь",
                L"знаю", L"знает", L"вижу", L"видит", L"делаю", L"делает",
                L"скажу", L"скажет", L"позвоню", L"позвонит", L"приду", L"придет",
                L"уйду", L"уйдет", L"изошло", L"пошло", L"пошел", L"пошёл", L"пошла", L"пойдет"
            };
            if (commonVerbForms.count(s)) return true;
            return endsWithAnyW(s, {
                L"ться", L"тся", L"ешь", L"ёшь", L"ишь", L"ете", L"ёте", L"ите",
                L"ала", L"али", L"ало", L"ила", L"или", L"ило",
                L"ела", L"ели", L"ело", L"шло", L"шла", L"шли", L"шел", L"шёл"
            });
        };
        const int minSegFreq4 = (dictionary_.size() >= 5000) ? 500 : 1;

        const size_t n = w.size();
        struct SegCandidate {
            bool valid = false;
            int freq = 0;
            double score = -1e18;
            std::vector<std::wstring> tokens;
        };

        auto evalSegment = [&](size_t i, size_t len) -> SegCandidate {
            SegCandidate res;
            if (len > 24) return res;
            std::wstring sub = w.substr(i, len);

            if (len >= 3 || i == 0) {
                auto ef = exactFixes.find(sub);
                if (ef != exactFixes.end()) {
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
                static const std::unordered_set<std::wstring> forbidEndLen2 = {
                    L"не", L"ни", L"на", L"по", L"из", L"за", L"от", L"до", L"во", L"со", L"ко"
                };
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

                    SegCandidate seg = evalSegment(i, len);
                    if (!seg.valid) continue;

                    if (!dp[i].tokens.empty() && prefixPrepositions.count(dp[i].tokens.back())) {
                        const std::wstring& pLeft = dp[i].tokens.back();
                        int minRight = (dictionary_.size() >= 5000) ? 4500 : 1;
                        if (seg.freq < minRight) continue;
                        if (pLeft != L"не" && pLeft != L"ни" && !seg.tokens.empty() &&
                            getBigramWeight(pLeft, seg.tokens.front()) == 0 &&
                            looksLikeVerbAfterPrep(seg.tokens.front())) {
                            continue;
                        }
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
                if (dubiousShortSplit && hasWholeFix && wholeDist == 1 && wholeFreq >= 10000) {
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
                    SegCandidate seg = evalSegment(i, len);
                    if (!seg.valid) continue;

                    if (!seg.tokens.empty() && !dpBack[j].tokens.empty() && prefixPrepositions.count(seg.tokens.back())) {
                        const std::wstring& pLeft = seg.tokens.back();
                        int nextFreq = lookupFreq(dpBack[j].tokens.front());
                        int minRight = (dictionary_.size() >= 5000) ? 4500 : 1;
                        if (nextFreq < minRight) continue;
                        if (pLeft != L"не" && pLeft != L"ни" &&
                            getBigramWeight(pLeft, dpBack[j].tokens.front()) == 0 &&
                            looksLikeVerbAfterPrep(dpBack[j].tokens.front())) {
                            continue;
                        }
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
        static const std::unordered_set<std::wstring> hyphenPrefixes = {
            L"какой", L"какая", L"какое", L"какие", L"чей",
            L"кто", L"что", L"кого", L"кому", L"чего", L"чему",
            L"где", L"куда", L"откуда", L"когда", L"как", L"почему", L"зачем", L"сколько"
        };
        static const std::unordered_set<std::wstring> hyphenToPrefixes = {
            L"кто", L"кого", L"кому", L"чего", L"чему",
            L"где", L"куда", L"откуда", L"когда", L"почему", L"зачем",
            L"какой", L"какая", L"какое", L"какие"
        };
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
        static const std::unordered_set<std::wstring> modalVerbs = {
            L"хочу", L"хочешь", L"хочет", L"хотим", L"хотите", L"хотят",
            L"хотел", L"хотела", L"хотели", L"хотеть",
            L"могу", L"можешь", L"может", L"можем", L"можете", L"могут",
            L"мог", L"могла", L"могли",
            L"буду", L"будешь", L"будет", L"будем", L"будете", L"будут",
            L"надо", L"нужно", L"можно", L"нельзя", L"должен", L"должна", L"должны",
            L"люблю", L"любишь", L"любит", L"любим", L"любите", L"любят",
            L"начал", L"начала", L"начали", L"стал", L"стала", L"стали",
            L"пошел", L"пошла", L"пошли", L"пошёл", L"пойти",
            L"иду", L"идешь", L"идёшь", L"идет", L"идёт", L"идем", L"идём", L"идете", L"идёте", L"идут",
            L"еду", L"едешь", L"едет", L"едем", L"едете", L"едут", L"поехал", L"поехала", L"поехали"
        };

        static const std::unordered_set<std::wstring> motionDesireVerbs = {
            L"хочу", L"хочешь", L"хочет", L"хотим", L"хотите", L"хотят",
            L"хотел", L"хотела", L"хотели", L"хотеть",
            L"иду", L"идешь", L"идёшь", L"идет", L"идёт", L"идем", L"идём", L"идете", L"идёте", L"идут", L"идти",
            L"пошел", L"пошёл", L"пошла", L"пошли", L"пойду", L"пойдешь", L"пойдёшь", L"пойдет", L"пойдёт", L"пойдем", L"пойдём", L"пойдете", L"пойдёте", L"пойдут", L"пойти",
            L"еду", L"едешь", L"едет", L"едем", L"едете", L"едут", L"ехать",
            L"поехал", L"поехала", L"поехали", L"поеду", L"поедешь", L"поедет", L"поедем", L"поедете", L"поедут", L"поехать",
            L"пришел", L"пришёл", L"пришла", L"пришли", L"приду", L"придешь", L"придёшь", L"придет", L"придёт", L"придем", L"придём", L"придете", L"придёте", L"придут", L"прийти",
            L"приехал", L"приехала", L"приехали", L"приехать", L"вернулся", L"вернулась", L"вернулись", L"вернуться",
            L"ушел", L"ушёл", L"ушла", L"ушли", L"уйти", L"уехал", L"уехала", L"уехали", L"уехать",
            L"зашел", L"зашёл", L"зашла", L"зашли", L"зайти", L"вошел", L"вошёл", L"вошла", L"вошли", L"войти",
            L"сел", L"села", L"сели", L"сесть", L"положил", L"положила", L"положить", L"поставил", L"поставила", L"поставить",
            L"лечу", L"летишь", L"летит", L"летим", L"летите", L"летят", L"лететь", L"полечу", L"полетел", L"полетела",
            L"бегу", L"спешу", L"собираюсь", L"надо", L"нужно", L"можно"
        };

        static const std::unordered_map<std::wstring, std::wstring> toInfinitive = {
            {L"сказал", L"сказать"}, {L"сказала", L"сказать"}, {L"сказали", L"сказать"}, {L"скажу", L"сказать"}, {L"скажешь", L"сказать"},
            {L"сделал", L"сделать"}, {L"сделала", L"сделать"}, {L"делаю", L"делать"}, {L"делаешь", L"делать"}, {L"делал", L"делать"},
            {L"пошел", L"пойти"}, {L"пошёл", L"пойти"}, {L"пошла", L"пойти"}, {L"пошли", L"пойти"},
            {L"ходил", L"ходить"}, {L"ходила", L"ходить"}, {L"иду", L"идти"},
            {L"гулял", L"гулять"}, {L"гуляла", L"гулять"}, {L"гуляю", L"гулять"}, {L"гуляешь", L"гулять"},
            {L"купил", L"купить"}, {L"купила", L"купить"}, {L"куплю", L"купить"},
            {L"узнал", L"узнать"}, {L"узнала", L"узнать"}, {L"знаю", L"знать"}, {L"знал", L"знать"},
            {L"спросил", L"спросить"}, {L"спросила", L"спросить"}, {L"спрошу", L"спросить"},
            {L"помог", L"помочь"}, {L"помогла", L"помочь"}, {L"помогу", L"помочь"}, {L"помогаю", L"помогать"},
            {L"посмотрел", L"посмотреть"}, {L"смотрел", L"смотреть"}, {L"смотрю", L"смотреть"},
            {L"работал", L"работать"}, {L"работаю", L"работать"}, {L"работаешь", L"работать"},
            {L"говорил", L"говорить"}, {L"говорю", L"говорить"}, {L"говоришь", L"говорить"},
            {L"видел", L"видеть"}, {L"вижу", L"видеть"},
            {L"понял", L"понять"}, {L"понимаю", L"понимать"},
            {L"пришел", L"прийти"}, {L"пришёл", L"прийти"}, {L"пришла", L"прийти"}, {L"приду", L"прийти"},
            {L"звонил", L"звонить"}, {L"позвонил", L"позвонить"}, {L"позвоню", L"позвонить"},
            {L"ел", L"есть"}, {L"ела", L"есть"}, {L"пил", L"пить"}, {L"пила", L"пить"},
            {L"спал", L"спать"}, {L"спала", L"спать"}, {L"сплю", L"спать"},
            {L"жил", L"жить"}, {L"жила", L"жить"}, {L"живу", L"жить"},
            {L"читал", L"читать"}, {L"читаю", L"читать"}, {L"читаешь", L"читать"}, {L"читает", L"читать"},
            {L"писал", L"писать"}, {L"пишу", L"писать"}, {L"пишешь", L"писать"}, {L"пишет", L"писать"},
            {L"ждал", L"ждать"}, {L"жду", L"ждать"}, {L"ждет", L"ждать"}, {L"ждёт", L"ждать"},
            {L"отдыхал", L"отдыхать"}, {L"отдыхаю", L"отдыхать"}, {L"отдыхает", L"отдыхать"},
            {L"делает", L"делать"}, {L"работает", L"работать"}, {L"говорит", L"говорить"},
            {L"гуляет", L"гулять"}, {L"поможет", L"помочь"}, {L"придет", L"прийти"}, {L"придёт", L"прийти"},
            {L"пойдет", L"пойти"}, {L"пойдёт", L"пойти"}, {L"идет", L"идти"}, {L"идёт", L"идти"},
            {L"смотрит", L"смотреть"}, {L"видит", L"видеть"}, {L"знает", L"знать"},
            {L"понимает", L"понимать"}, {L"думает", L"думать"}, {L"звонит", L"звонить"},
            {L"позвонит", L"позвонить"}, {L"купит", L"купить"}, {L"спросит", L"спросить"},
            {L"едет", L"ехать"}, {L"ехал", L"ехать"}, {L"еду", L"ехать"}
        };

        struct VerbConjugation {
            std::wstring ya, ty, on, my, vy, oni;
        };
        static const std::unordered_map<std::wstring, VerbConjugation> infinitiveConj = {
            {L"хотеть",   {L"хочу", L"хочешь", L"хочет", L"хотим", L"хотите", L"хотят"}},
            {L"мочь",     {L"могу", L"можешь", L"может", L"можем", L"можете", L"могут"}},
            {L"знать",    {L"знаю", L"знаешь", L"знает", L"знаем", L"знаете", L"знают"}},
            {L"делать",   {L"делаю", L"делаешь", L"делает", L"делаем", L"делаете", L"делают"}},
            {L"гулять",   {L"гуляю", L"гуляешь", L"гуляет", L"гуляем", L"гуляете", L"гуляют"}},
            {L"идти",     {L"иду", L"идешь", L"идет", L"идем", L"идете", L"идут"}},
            {L"ехать",    {L"еду", L"едешь", L"едет", L"едем", L"едете", L"едут"}},
            {L"ходить",   {L"хожу", L"ходишь", L"ходит", L"ходим", L"ходите", L"ходят"}},
            {L"говорить", {L"говорю", L"говоришь", L"говорит", L"говорим", L"говорите", L"говорят"}},
            {L"сказать",  {L"скажу", L"скажешь", L"скажет", L"скажем", L"скажете", L"скажут"}},
            {L"понимать", {L"понимаю", L"понимаешь", L"понимает", L"понимаем", L"понимаете", L"понимают"}},
            {L"любить",   {L"люблю", L"любишь", L"любит", L"любим", L"любите", L"любят"}},
            {L"жить",     {L"живу", L"живешь", L"живет", L"живем", L"живете", L"живут"}},
            {L"работать", {L"работаю", L"работаешь", L"работает", L"работаем", L"работаете", L"работают"}},
            {L"учиться",  {L"учусь", L"учишься", L"учится", L"учимся", L"учитесь", L"учатся"}},
            {L"смотреть", {L"смотрю", L"смотришь", L"смотрит", L"смотрим", L"смотрите", L"смотрят"}},
            {L"ждать",    {L"жду", L"ждешь", L"ждет", L"ждем", L"ждете", L"ждут"}},
            {L"думать",   {L"думаю", L"думаешь", L"думает", L"думаем", L"думаете", L"думают"}},
            {L"видеть",   {L"вижу", L"видишь", L"видит", L"видим", L"видите", L"видят"}},
            {L"слышать",  {L"слышу", L"слышишь", L"слышит", L"слышим", L"слышите", L"слышат"}},
            {L"помочь",   {L"помогу", L"поможешь", L"поможет", L"поможем", L"поможете", L"помогут"}},
            {L"помогать", {L"помогаю", L"помогаешь", L"помогает", L"помогаем", L"помогаете", L"помогают"}},
            {L"прийти",   {L"приду", L"придешь", L"придет", L"придем", L"придете", L"придут"}},
            {L"пойти",    {L"пойду", L"пойдешь", L"пойдет", L"пойдем", L"пойдете", L"пойдут"}},
            {L"звонить",  {L"звоню", L"звонишь", L"звонит", L"звоним", L"звоните", L"звонят"}},
            {L"позвонить",{L"позвоню", L"позвонишь", L"позвонит", L"позвоним", L"позвоните", L"позвонят"}},
            {L"читать",   {L"читаю", L"читаешь", L"читает", L"читаем", L"читаете", L"читают"}},
            {L"писать",   {L"пишу", L"пишешь", L"пишет", L"пишем", L"пишете", L"пишут"}},
            {L"есть",     {L"ем", L"ешь", L"ест", L"едим", L"едите", L"едят"}},
            {L"пить",     {L"пью", L"пьешь", L"пьет", L"пьем", L"пьете", L"пьют"}},
            {L"спать",    {L"сплю", L"спишь", L"спит", L"спим", L"спите", L"спят"}},
            {L"отдыхать", {L"отдыхаю", L"отдыхаешь", L"отдыхает", L"отдыхаем", L"отдыхаете", L"отдыхают"}},
            {L"сидеть",   {L"сижу", L"сидишь", L"сидит", L"сидим", L"сидите", L"сидят"}},
            {L"стоять",   {L"стою", L"стоишь", L"стоит", L"стоим", L"стоите", L"стоят"}},
            {L"играть",   {L"играю", L"играешь", L"играет", L"играем", L"играете", L"играют"}},
            {L"учить",    {L"учу", L"учишь", L"учит", L"учим", L"учите", L"учат"}},
            {L"искать",   {L"ищу", L"ищешь", L"ищет", L"ищем", L"ищете", L"ищут"}},
            {L"покупать", {L"покупаю", L"покупаешь", L"покупает", L"покупаем", L"покупаете", L"покупают"}}
        };

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

            // Устойчивое приветствие/пожелание "добрый ночь" / "спокойный ночи"
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
                }
            }
        }

        // Общие таблицы родов и неименяемых слов (используются в Правилах 4 и 5)
        static const std::unordered_set<std::wstring> mascExceptions = {
            L"папа", L"дядя", L"дедушка", L"мужчина", L"юноша", L"коллега", L"судья", L"кофе"
        };
        static const std::unordered_set<std::wstring> femThirdDecl = {
            L"ночь", L"жизнь", L"радость", L"новость", L"любовь", L"дверь", L"тетрадь",
            L"площадь", L"вещь", L"кровать", L"помощь", L"речь", L"осень", L"память",
            L"связь", L"боль", L"часть", L"цель", L"роль", L"болезнь", L"модель",
            L"соль", L"мысль", L"сеть", L"очередь", L"подпись", L"область", L"тень"
        };
        static const std::unordered_set<std::wstring> neutExceptions = {
            L"время", L"имя", L"пламя", L"знамя", L"племя", L"семя"
        };
        static const std::unordered_set<std::wstring> pluralNouns = {
            L"дела", L"слова", L"друзья", L"братья", L"сыновья", L"дети", L"люди",
            L"дома", L"города", L"глаза", L"письма", L"окна", L"места", L"времена",
            L"имена", L"новости", L"девушки", L"книги", L"руки", L"ноги", L"дни",
            L"годы", L"часы", L"цветы", L"родители", L"студенты", L"жесты",
            L"машины", L"школы", L"улицы", L"комнаты", L"квартиры", L"магазины",
            L"проблемы", L"задачи", L"вопросы", L"ответы", L"подруги", L"деревья",
            L"праздники", L"телефоны", L"вещи", L"двери", L"ночи", L"деньги",
            L"наушники", L"кроссовки", L"каникулы", L"очки"
        };
        static const std::unordered_set<std::wstring> nonNounSkip = {
            L"сегодня", L"вчера", L"завтра", L"дома", L"туда", L"сюда", L"куда", L"откуда",
            L"всегда", L"никогда", L"иногда", L"меня", L"тебя", L"себя", L"его", L"него",
            L"хорошо", L"плохо", L"можно", L"нужно", L"надо", L"много", L"мало", L"сколько",
            L"быстро", L"медленно", L"далеко", L"близко", L"рядом", L"рано", L"поздно",
            L"тепло", L"холодно", L"весело", L"интересно", L"приятно", L"понятно",
            L"точно", L"конечно", L"пожалуйста", L"спасибо", L"здравствуйте", L"привет", L"пока",
            L"очень", L"уже", L"еще", L"ещё", L"тоже", L"также", L"ночи"
        };
        static const std::unordered_set<std::wstring> nonInflectExtra = {
            L"я", L"ты", L"он", L"она", L"оно", L"мы", L"вы", L"они", L"кто", L"что",
            L"всё", L"все", L"это", L"то", L"её", L"ее", L"их", L"мне", L"тебе",
            L"ему", L"ей", L"нам", L"вам", L"им", L"нас", L"вас", L"неё", L"нее",
            L"ним", L"них", L"мой", L"твой", L"свой", L"наш", L"ваш", L"этот", L"тот",
            L"какой", L"один", L"да", L"нет", L"не", L"ни", L"и", L"а", L"но", L"или",
            L"же", L"бы", L"ли", L"вот", L"там", L"тут", L"здесь", L"где", L"почему",
            L"зачем", L"как", L"столько", L"несколько", L"чтобы", L"потому", L"поэтому",
            L"если", L"хотя", L"однако", L"кстати", L"например", L"наверное", L"короче",
            L"кажется", L"видимо", L"возможно", L"извините", L"простите", L"нельзя",
            L"ладно", L"хватит", L"пора", L"сейчас", L"давно", L"недавно", L"скоро",
            L"потом", L"обычно", L"часто", L"редко", L"важно", L"удобно", L"дорого",
            L"дешево", L"сложно", L"легко", L"трудно", L"грустно", L"прямо", L"просто",
            L"совсем", L"почти", L"только", L"даже", L"слишком",
            L"готов", L"готова", L"готовы", L"рад", L"рада", L"рады",
            L"должен", L"должна", L"должны", L"согласен", L"уверен", L"уверена",
            L"болен", L"здоров", L"занят", L"свободен", L"прав", L"права",
            L"кофе", L"метро", L"такси", L"кино", L"пальто", L"кафе", L"радио",
            L"фото", L"меню", L"шоссе", L"видео", L"аудио", L"жюри"
        };

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
            auto endsWith = [&](const wchar_t* suf) -> bool {
                size_t sl = 0;
                while (suf[sl] != L'\0') ++sl;
                return s.size() > sl && s.compare(s.size() - sl, sl, suf) == 0;
            };
            if (!femThirdDecl.count(s) &&
                (endsWith(L"ть") || endsWith(L"ться") || endsWith(L"тся") || endsWith(L"чь") ||
                 endsWith(L"ешь") || endsWith(L"ишь") || endsWith(L"ете") || endsWith(L"ите"))) {
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
                    outTag = {CaseTag::Gen, GenderTag::Unknown, true, false};
                    return true;
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
            } else if (kase == CaseTag::Prep && last == L'е' && stem.size() >= 3) {
                if (lookupFreq(stem + L"а") >= minRegFormFreq || lookupFreq(stem + L"ом") > 0) {
                    GenderTag g = (lookupFreq(stem + L"а") > 0 && lookupFreq(stem) == 0)
                                      ? GenderTag::Fem : GenderTag::Masc;
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

        static const std::unordered_set<std::wstring> nonAdjectiveOiIy = {
            L"мной", L"тобой", L"собой", L"домой", L"зимой", L"весной", L"порой", L"долой",
            L"герой", L"бой", L"слой", L"строй", L"покой", L"конвой", L"прибой", L"разбой",
            L"отбой", L"застой", L"настой", L"гений", L"санаторий", L"комментарий",
            L"сценарий", L"критерий", L"пролетарий", L"калий", L"натрий", L"кальций",
            L"магний", L"алюминий"
        };

        auto isModifierBeforeNoun = [&](const std::wstring& tok) -> bool {
            if (nonAdjectiveOiIy.count(tok)) return false;
            if (tok == L"мой" || tok == L"моя" || tok == L"моё" || tok == L"мои" ||
                tok == L"твой" || tok == L"твоя" || tok == L"твоё" || tok == L"твои" ||
                tok == L"свой" || tok == L"своя" || tok == L"своё" || tok == L"свои" ||
                tok == L"наш" || tok == L"наша" || tok == L"наше" || tok == L"наши" ||
                tok == L"ваш" || tok == L"ваша" || tok == L"ваше" || tok == L"ваши" ||
                tok == L"этот" || tok == L"эта" || tok == L"это" || tok == L"эти" ||
                tok == L"тот" || tok == L"та" || tok == L"то" || tok == L"те" ||
                tok == L"один" || tok == L"одна" || tok == L"одно" ||
                tok == L"какой" || tok == L"какая" || tok == L"какое" ||
                tok == L"каждый" || tok == L"каждая" || tok == L"каждое" ||
                tok == L"весь" || tok == L"вся" || tok == L"всё" || tok == L"все") {
                return true;
            }
            if (tok.size() >= 4) {
                std::wstring end2 = tok.substr(tok.size() - 2);
                if (end2 == L"ый" || end2 == L"ий" || end2 == L"ой" ||
                    end2 == L"ая" || end2 == L"яя" || end2 == L"ое" || end2 == L"ее" ||
                    end2 == L"ые" || end2 == L"ие") {
                    return true;
                }
            }
            return false;
        };

        for (size_t i = 1; i < processedTokens_.size(); ++i) {
            std::wstring& prep = processedTokens_[i - 1];
            size_t targetIdx = i;
            while (targetIdx + 1 < processedTokens_.size() &&
                   (targetIdx - i) < 2 &&
                   isModifierBeforeNoun(processedTokens_[targetIdx])) {
                ++targetIdx;
            }
            std::wstring& cur = processedTokens_[targetIdx];

            if (prep == L"нет") {
                static const std::unordered_map<std::wstring, InflectTarget> netGen = {
                    {L"время", {L"времени", GenderTag::Neut, false, false}},
                    {L"имя", {L"имени", GenderTag::Neut, false, false}},
                    {L"работа", {L"работы", GenderTag::Fem, false, false}},
                    {L"школа", {L"школы", GenderTag::Fem, false, false}},
                    {L"деньги", {L"денег", GenderTag::Unknown, true, false}},
                    {L"дом", {L"дома", GenderTag::Masc, false, false}},
                    {L"друг", {L"друга", GenderTag::Masc, false, true}},
                    {L"подруга", {L"подруги", GenderTag::Fem, false, true}},
                    {L"телефон", {L"телефона", GenderTag::Masc, false, false}},
                    {L"машина", {L"машины", GenderTag::Fem, false, false}},
                    {L"вода", {L"воды", GenderTag::Fem, false, false}},
                    {L"хлеб", {L"хлеба", GenderTag::Masc, false, false}},
                    {L"настроение", {L"настроения", GenderTag::Neut, false, false}},
                    {L"помощь", {L"помощи", GenderTag::Fem, false, false}},
                    {L"место", {L"места", GenderTag::Neut, false, false}},
                    {L"мама", {L"мамы", GenderTag::Fem, false, true}},
                    {L"папа", {L"папы", GenderTag::Masc, false, true}},
                    {L"врач", {L"врача", GenderTag::Masc, false, true}},
                    {L"город", {L"города", GenderTag::Masc, false, false}},
                    {L"магазин", {L"магазина", GenderTag::Masc, false, false}},
                    {L"комната", {L"комнаты", GenderTag::Fem, false, false}},
                    {L"квартира", {L"квартиры", GenderTag::Fem, false, false}},
                    {L"улица", {L"улицы", GenderTag::Fem, false, false}},
                    {L"книга", {L"книги", GenderTag::Fem, false, false}},
                    {L"дело", {L"дела", GenderTag::Neut, false, false}},
                    {L"слово", {L"слова", GenderTag::Neut, false, false}}
                };
                applyCaseLookup(netGen, cur, targetIdx, CaseTag::Gen,
                                L"Родительный падеж после слова «нет»", true);
            } else if (prep == L"много" || prep == L"мало" || prep == L"сколько" ||
                       prep == L"несколько" || prep == L"столько") {
                static const std::unordered_map<std::wstring, InflectTarget> quantGen = {
                    {L"время", {L"времени", GenderTag::Neut, false, false}},
                    {L"работа", {L"работы", GenderTag::Fem, false, false}},
                    {L"вода", {L"воды", GenderTag::Fem, false, false}},
                    {L"сахар", {L"сахара", GenderTag::Masc, false, false}},
                    {L"хлеб", {L"хлеба", GenderTag::Masc, false, false}},
                    {L"молоко", {L"молока", GenderTag::Neut, false, false}},
                    {L"помощь", {L"помощи", GenderTag::Fem, false, false}},
                    {L"друг", {L"друзей", GenderTag::Unknown, true, true}},
                    {L"человек", {L"людей", GenderTag::Unknown, true, true}},
                    {L"ребенок", {L"детей", GenderTag::Unknown, true, true}},
                    {L"ребёнок", {L"детей", GenderTag::Unknown, true, true}},
                    {L"деньги", {L"денег", GenderTag::Unknown, true, false}},
                    {L"день", {L"дней", GenderTag::Unknown, true, false}},
                    {L"год", {L"лет", GenderTag::Unknown, true, false}},
                    {L"час", {L"часов", GenderTag::Unknown, true, false}},
                    {L"минута", {L"минут", GenderTag::Unknown, true, false}},
                    {L"неделя", {L"недель", GenderTag::Unknown, true, false}},
                    {L"месяц", {L"месяцев", GenderTag::Unknown, true, false}},
                    {L"дело", {L"дел", GenderTag::Unknown, true, false}},
                    {L"слово", {L"слов", GenderTag::Unknown, true, false}},
                    {L"место", {L"мест", GenderTag::Unknown, true, false}},
                    {L"окно", {L"окон", GenderTag::Unknown, true, false}},
                    {L"письмо", {L"писем", GenderTag::Unknown, true, false}},
                    {L"книга", {L"книг", GenderTag::Unknown, true, false}},
                    {L"проблема", {L"проблем", GenderTag::Unknown, true, false}},
                    {L"вопрос", {L"вопросов", GenderTag::Unknown, true, false}},
                    {L"ответ", {L"ответов", GenderTag::Unknown, true, false}},
                    {L"ошибка", {L"ошибок", GenderTag::Unknown, true, false}},
                    {L"машина", {L"машин", GenderTag::Unknown, true, false}},
                    {L"дом", {L"домов", GenderTag::Unknown, true, false}},
                    {L"город", {L"городов", GenderTag::Unknown, true, false}},
                    {L"билет", {L"билетов", GenderTag::Unknown, true, false}},
                    {L"сообщение", {L"сообщений", GenderTag::Unknown, true, false}},
                    {L"новость", {L"новостей", GenderTag::Unknown, true, false}}
                };
                if (!applyCaseLookup(quantGen, cur, targetIdx, CaseTag::Gen,
                                     L"Родительный падеж после количественного слова «" + prep + L"»", false)) {
                    // Проверяем, не стоит ли слово уже в род.п. мн.ч. (-ов/-ев/-ей)
                    if (cur.size() >= 4 && !isNonInflectNounWord(cur) &&
                        (cur.compare(cur.size() - 2, 2, L"ов") == 0 ||
                         cur.compare(cur.size() - 2, 2, L"ев") == 0 ||
                         cur.compare(cur.size() - 2, 2, L"ей") == 0)) {
                        nounTags[targetIdx] = {CaseTag::Gen, GenderTag::Unknown, true, false};
                    }
                }
            } else if (prep == L"в" || prep == L"на") {
                bool isDirection = false;
                bool heuristicStartDirection = false;
                if (i == 1 && (processedTokens_.size() == 2 || targetIdx + 1 == processedTokens_.size())) {
                    isDirection = true;
                    heuristicStartDirection = true;
                } else {
                    for (size_t back = 1; back <= 3 && i >= 1 + back; ++back) {
                        const std::wstring& prevTok = processedTokens_[i - 1 - back];
                        if (prevTok == L"а" || prevTok == L"но" || prevTok == L"и" ||
                            prevTok == L"что" || prevTok == L"если") {
                            break;
                        }
                        if (motionDesireVerbs.count(prevTok)) {
                            isDirection = true;
                            break;
                        }
                    }
                    if (!isDirection && targetIdx + 1 < processedTokens_.size() &&
                        motionDesireVerbs.count(processedTokens_[targetIdx + 1])) {
                        isDirection = true;
                    }
                }
                if (cur == L"раз") {
                    isDirection = true;
                }

                static const std::unordered_map<std::wstring, InflectTarget> dirAcc = {
                    {L"раз", {L"раз", GenderTag::Masc, false, false}},
                    {L"школа", {L"школу", GenderTag::Fem, false, false}},
                    {L"работа", {L"работу", GenderTag::Fem, false, false}},
                    {L"улица", {L"улицу", GenderTag::Fem, false, false}},
                    {L"больница", {L"больницу", GenderTag::Fem, false, false}},
                    {L"аптека", {L"аптеку", GenderTag::Fem, false, false}},
                    {L"квартира", {L"квартиру", GenderTag::Fem, false, false}},
                    {L"комната", {L"комнату", GenderTag::Fem, false, false}},
                    {L"машина", {L"машину", GenderTag::Fem, false, false}},
                    {L"москва", {L"москву", GenderTag::Fem, false, false}},
                    {L"россия", {L"россию", GenderTag::Fem, false, false}},
                    {L"кухня", {L"кухню", GenderTag::Fem, false, false}},
                    {L"деревня", {L"деревню", GenderTag::Fem, false, false}},
                    {L"библиотека", {L"библиотеку", GenderTag::Fem, false, false}},
                    {L"остановка", {L"остановку", GenderTag::Fem, false, false}},
                    {L"почта", {L"почту", GenderTag::Fem, false, false}},
                    {L"страна", {L"страну", GenderTag::Fem, false, false}},
                    {L"дом", {L"дом", GenderTag::Masc, false, false}},
                    {L"город", {L"город", GenderTag::Masc, false, false}},
                    {L"магазин", {L"магазин", GenderTag::Masc, false, false}},
                    {L"университет", {L"университет", GenderTag::Masc, false, false}},
                    {L"парк", {L"парк", GenderTag::Masc, false, false}},
                    {L"офис", {L"офис", GenderTag::Masc, false, false}},
                    {L"театр", {L"театр", GenderTag::Masc, false, false}},
                    {L"музей", {L"музей", GenderTag::Masc, false, false}},
                    {L"стол", {L"стол", GenderTag::Masc, false, false}},
                    {L"петербург", {L"петербург", GenderTag::Masc, false, false}},
                    {L"череповец", {L"череповец", GenderTag::Masc, false, false}}
                };
                static const std::unordered_map<std::wstring, InflectTarget> locPrep = {
                    {L"школа", {L"школе", GenderTag::Fem, false, false}},
                    {L"работа", {L"работе", GenderTag::Fem, false, false}},
                    {L"улица", {L"улице", GenderTag::Fem, false, false}},
                    {L"больница", {L"больнице", GenderTag::Fem, false, false}},
                    {L"аптека", {L"аптеке", GenderTag::Fem, false, false}},
                    {L"квартира", {L"квартире", GenderTag::Fem, false, false}},
                    {L"комната", {L"комнате", GenderTag::Fem, false, false}},
                    {L"машина", {L"машине", GenderTag::Fem, false, false}},
                    {L"москва", {L"москве", GenderTag::Fem, false, false}},
                    {L"россия", {L"россии", GenderTag::Fem, false, false}},
                    {L"кухня", {L"кухне", GenderTag::Fem, false, false}},
                    {L"деревня", {L"деревне", GenderTag::Fem, false, false}},
                    {L"библиотека", {L"библиотеке", GenderTag::Fem, false, false}},
                    {L"остановка", {L"остановке", GenderTag::Fem, false, false}},
                    {L"почта", {L"почте", GenderTag::Fem, false, false}},
                    {L"страна", {L"стране", GenderTag::Fem, false, false}},
                    {L"дом", {L"доме", GenderTag::Masc, false, false}},
                    {L"город", {L"городе", GenderTag::Masc, false, false}},
                    {L"магазин", {L"магазине", GenderTag::Masc, false, false}},
                    {L"университет", {L"университете", GenderTag::Masc, false, false}},
                    {L"парк", {L"парке", GenderTag::Masc, false, false}},
                    {L"офис", {L"офисе", GenderTag::Masc, false, false}},
                    {L"театр", {L"театре", GenderTag::Masc, false, false}},
                    {L"музей", {L"музее", GenderTag::Masc, false, false}},
                    {L"банк", {L"банке", GenderTag::Masc, false, false}},
                    {L"стол", {L"столе", GenderTag::Masc, false, false}},
                    {L"телефон", {L"телефоне", GenderTag::Masc, false, false}},
                    {L"интернет", {L"интернете", GenderTag::Masc, false, false}},
                    {L"петербург", {L"петербурге", GenderTag::Masc, false, false}},
                    {L"череповец", {L"череповце", GenderTag::Masc, false, false}},
                    {L"место", {L"месте", GenderTag::Neut, false, false}},
                    {L"настроение", {L"настроении", GenderTag::Neut, false, false}},
                    {L"окно", {L"окне", GenderTag::Neut, false, false}},
                    {L"приложение", {L"приложении", GenderTag::Neut, false, false}},
                    {L"сообщение", {L"сообщении", GenderTag::Neut, false, false}}
                };

                if (isDirection) {
                    bool matchedDir = applyCaseLookup(
                        dirAcc, cur, targetIdx, CaseTag::Acc,
                        L"Винительный падеж направления с предлогом «" + prep + L"»",
                        !heuristicStartDirection);
                    // Если начало фразы «в/на ...» не относится к направленному глаголу и слово чисто местное (например «в хорошем настроении»), выбираем предложный падеж
                    if (!matchedDir && heuristicStartDirection) {
                        isDirection = false;
                    }
                }
                if (!isDirection) {
                    applyCaseLookup(locPrep, cur, targetIdx, CaseTag::Prep,
                                    L"Предложный падеж места с предлогом «" + prep + L"»", true);
                }
            } else if (prep == L"с" || prep == L"со") {
                static const std::unordered_map<std::wstring, InflectTarget> instNouns = {
                    {L"праздник", {L"праздником", GenderTag::Masc, false, false}},
                    {L"друг", {L"другом", GenderTag::Masc, false, true}},
                    {L"год", {L"годом", GenderTag::Masc, false, false}},
                    {L"день", {L"днем", GenderTag::Masc, false, false}},
                    {L"учитель", {L"учителем", GenderTag::Masc, false, true}},
                    {L"врач", {L"врачом", GenderTag::Masc, false, true}},
                    {L"брат", {L"братом", GenderTag::Masc, false, true}},
                    {L"папа", {L"папой", GenderTag::Masc, false, true}},
                    {L"подруга", {L"подругой", GenderTag::Fem, false, true}},
                    {L"мама", {L"мамой", GenderTag::Fem, false, true}},
                    {L"сестра", {L"сестрой", GenderTag::Fem, false, true}},
                    {L"девушка", {L"девушкой", GenderTag::Fem, false, true}},
                    {L"школа", {L"школой", GenderTag::Fem, false, false}},
                    {L"работа", {L"работой", GenderTag::Fem, false, false}},
                    {L"вода", {L"водой", GenderTag::Fem, false, false}},
                    {L"радость", {L"радостью", GenderTag::Fem, false, false}},
                    {L"утро", {L"утром", GenderTag::Neut, false, false}},
                    {L"настроение", {L"настроением", GenderTag::Neut, false, false}},
                    {L"слово", {L"словом", GenderTag::Neut, false, false}}
                };
                if (targetIdx == i && cur == L"я") {
                    logChange(prep, L"со", L"Фонетическая форма предлога «со мной»");
                    logChange(cur, L"мной", L"Творительный падеж с предлогом «со»");
                } else if (targetIdx == i && cur == L"ты") logChange(cur, L"тобой", L"Творительный падеж с предлогом «с»");
                else if (targetIdx == i && cur == L"мы") logChange(cur, L"нами", L"Творительный падеж с предлогом «с»");
                else if (targetIdx == i && cur == L"вы") logChange(cur, L"вами", L"Творительный падеж с предлогом «с»");
                else if (targetIdx == i && cur == L"он") logChange(cur, L"ним", L"Творительный падеж с предлогом «с»");
                else if (targetIdx == i && cur == L"она") logChange(cur, L"ней", L"Творительный падеж с предлогом «с»");
                else if (targetIdx == i && cur == L"они") logChange(cur, L"ними", L"Творительный падеж с предлогом «с»");
                else {
                    applyCaseLookup(instNouns, cur, targetIdx, CaseTag::Inst,
                                    L"Творительный падеж с предлогом «" + prep + L"»", true);
                }
            } else if (prep == L"к" || prep == L"ко") {
                static const std::unordered_map<std::wstring, InflectTarget> datNouns = {
                    {L"друг", {L"другу", GenderTag::Masc, false, true}},
                    {L"врач", {L"врачу", GenderTag::Masc, false, true}},
                    {L"учитель", {L"учителю", GenderTag::Masc, false, true}},
                    {L"брат", {L"брату", GenderTag::Masc, false, true}},
                    {L"папа", {L"папе", GenderTag::Masc, false, true}},
                    {L"дом", {L"дому", GenderTag::Masc, false, false}},
                    {L"город", {L"городу", GenderTag::Masc, false, false}},
                    {L"подруга", {L"подруге", GenderTag::Fem, false, true}},
                    {L"мама", {L"маме", GenderTag::Fem, false, true}},
                    {L"сестра", {L"сестре", GenderTag::Fem, false, true}},
                    {L"девушка", {L"девушке", GenderTag::Fem, false, true}},
                    {L"школа", {L"школе", GenderTag::Fem, false, false}},
                    {L"работа", {L"работе", GenderTag::Fem, false, false}},
                    {L"аптека", {L"аптеке", GenderTag::Fem, false, false}},
                    {L"больница", {L"больнице", GenderTag::Fem, false, false}}
                };
                if (targetIdx == i && cur == L"я") {
                    logChange(prep, L"ко", L"Фонетическая форма предлога «ко мне»");
                    logChange(cur, L"мне", L"Дательный падеж с предлогом «ко»");
                } else if (targetIdx == i && cur == L"ты") logChange(cur, L"тебе", L"Дательный падеж с предлогом «к»");
                else if (targetIdx == i && cur == L"мы") logChange(cur, L"нам", L"Дательный падеж с предлогом «к»");
                else if (targetIdx == i && cur == L"вы") logChange(cur, L"вам", L"Дательный падеж с предлогом «к»");
                else if (targetIdx == i && cur == L"он") logChange(cur, L"нему", L"Дательный падеж с предлогом «к»");
                else if (targetIdx == i && cur == L"она") logChange(cur, L"ней", L"Дательный падеж с предлогом «к»");
                else if (targetIdx == i && cur == L"они") logChange(cur, L"ним", L"Дательный падеж с предлогом «к»");
                else {
                    applyCaseLookup(datNouns, cur, targetIdx, CaseTag::Dat,
                                    L"Дательный падеж с предлогом «" + prep + L"»", true);
                }
            } else if (prep == L"о" || prep == L"об" || prep == L"обо") {
                static const std::unordered_map<std::wstring, InflectTarget> aboutPrepNouns = {
                    {L"друг", {L"друге", GenderTag::Masc, false, true}},
                    {L"папа", {L"папе", GenderTag::Masc, false, true}},
                    {L"дом", {L"доме", GenderTag::Masc, false, false}},
                    {L"город", {L"городе", GenderTag::Masc, false, false}},
                    {L"подруга", {L"подруге", GenderTag::Fem, false, true}},
                    {L"мама", {L"маме", GenderTag::Fem, false, true}},
                    {L"школа", {L"школе", GenderTag::Fem, false, false}},
                    {L"работа", {L"работе", GenderTag::Fem, false, false}},
                    {L"жизнь", {L"жизни", GenderTag::Fem, false, false}},
                    {L"помощь", {L"помощи", GenderTag::Fem, false, false}},
                    {L"время", {L"времени", GenderTag::Neut, false, false}},
                    {L"настроение", {L"настроении", GenderTag::Neut, false, false}}
                };
                if (targetIdx == i && cur == L"я") {
                    logChange(prep, L"обо", L"Фонетическая форма предлога «обо мне»");
                    logChange(cur, L"мне", L"Предложный падеж с предлогом «обо»");
                } else if (targetIdx == i && cur == L"ты") logChange(cur, L"тебе", L"Предложный падеж с предлогом «о»");
                else if (targetIdx == i && cur == L"он") logChange(cur, L"нём", L"Предложный падеж с предлогом «о»");
                else if (targetIdx == i && cur == L"она") logChange(cur, L"ней", L"Предложный падеж с предлогом «о»");
                else if (targetIdx == i && cur == L"мы") logChange(cur, L"нас", L"Предложный падеж с предлогом «о»");
                else if (targetIdx == i && cur == L"вы") logChange(cur, L"вас", L"Предложный падеж с предлогом «о»");
                else if (targetIdx == i && cur == L"они") logChange(cur, L"них", L"Предложный падеж с предлогом «о»");
                else {
                    applyCaseLookup(aboutPrepNouns, cur, targetIdx, CaseTag::Prep,
                                    L"Предложный падеж с предлогом «" + prep + L"»", true);
                }
            } else if (prep == L"по") {
                static const std::unordered_map<std::wstring, InflectTarget> poDatNouns = {
                    {L"телефон", {L"телефону", GenderTag::Masc, false, false}},
                    {L"улица", {L"улице", GenderTag::Fem, false, false}},
                    {L"город", {L"городу", GenderTag::Masc, false, false}},
                    {L"дорога", {L"дороге", GenderTag::Fem, false, false}},
                    {L"работа", {L"работе", GenderTag::Fem, false, false}}
                };
                applyCaseLookup(poDatNouns, cur, targetIdx, CaseTag::Dat,
                                L"Дательный падеж с предлогом «по»", true);
            } else if (prep == L"у" || prep == L"без" || prep == L"из" || prep == L"от" ||
                       prep == L"для" || prep == L"до" || prep == L"после" || prep == L"около" || prep == L"про") {
                static const std::unordered_map<std::wstring, std::wstring> genOrAccForms = {
                    {L"я", L"меня"}, {L"ты", L"тебя"}, {L"мы", L"нас"}, {L"вы", L"вас"},
                    {L"он", L"него"}, {L"она", L"неё"}, {L"они", L"них"}
                };
                auto pIt = (targetIdx == i) ? genOrAccForms.find(cur) : genOrAccForms.end();
                if (pIt != genOrAccForms.end()) {
                    logChange(cur, pIt->second, L"Падежная форма местоимения после предлога «" + prep + L"»");
                } else if (prep == L"про") {
                    static const std::unordered_map<std::wstring, InflectTarget> proAcc = {
                        {L"друг", {L"друга", GenderTag::Masc, false, true}},
                        {L"папа", {L"папу", GenderTag::Masc, false, true}},
                        {L"мама", {L"маму", GenderTag::Fem, false, true}},
                        {L"подруга", {L"подругу", GenderTag::Fem, false, true}},
                        {L"школа", {L"школу", GenderTag::Fem, false, false}},
                        {L"работа", {L"работу", GenderTag::Fem, false, false}},
                        {L"книга", {L"книгу", GenderTag::Fem, false, false}},
                        {L"дом", {L"дом", GenderTag::Masc, false, false}},
                        {L"город", {L"город", GenderTag::Masc, false, false}}
                    };
                    applyCaseLookup(proAcc, cur, targetIdx, CaseTag::Acc,
                                    L"Винительный падеж с предлогом «про»", true);
                } else {
                    static const std::unordered_map<std::wstring, InflectTarget> nounGen = {
                        {L"школа", {L"школы", GenderTag::Fem, false, false}},
                        {L"работа", {L"работы", GenderTag::Fem, false, false}},
                        {L"больница", {L"больницы", GenderTag::Fem, false, false}},
                        {L"аптека", {L"аптеки", GenderTag::Fem, false, false}},
                        {L"квартира", {L"квартиры", GenderTag::Fem, false, false}},
                        {L"комната", {L"комнаты", GenderTag::Fem, false, false}},
                        {L"улица", {L"улицы", GenderTag::Fem, false, false}},
                        {L"машина", {L"машины", GenderTag::Fem, false, false}},
                        {L"кухня", {L"кухни", GenderTag::Fem, false, false}},
                        {L"деревня", {L"деревни", GenderTag::Fem, false, false}},
                        {L"мама", {L"мамы", GenderTag::Fem, false, true}},
                        {L"подруга", {L"подруги", GenderTag::Fem, false, true}},
                        {L"вода", {L"воды", GenderTag::Fem, false, false}},
                        {L"дом", {L"дома", GenderTag::Masc, false, false}},
                        {L"город", {L"города", GenderTag::Masc, false, false}},
                        {L"магазин", {L"магазина", GenderTag::Masc, false, false}},
                        {L"друг", {L"друга", GenderTag::Masc, false, true}},
                        {L"папа", {L"папы", GenderTag::Masc, false, true}},
                        {L"врач", {L"врача", GenderTag::Masc, false, true}},
                        {L"праздник", {L"праздника", GenderTag::Masc, false, false}},
                        {L"хлеб", {L"хлеба", GenderTag::Masc, false, false}},
                        {L"время", {L"времени", GenderTag::Neut, false, false}},
                        {L"настроение", {L"настроения", GenderTag::Neut, false, false}},
                        {L"москва", {L"москвы", GenderTag::Fem, false, false}},
                        {L"петербург", {L"петербурга", GenderTag::Masc, false, false}},
                        {L"россия", {L"россии", GenderTag::Fem, false, false}},
                        {L"череповец", {L"череповца", GenderTag::Masc, false, false}}
                    };
                    applyCaseLookup(nounGen, cur, targetIdx, CaseTag::Gen,
                                    L"Родительный падеж после предлога «" + prep + L"»", true);
                }
            }
        }

        // Правило 5: Согласование прилагательных и притяжательных/указательных местоимений по роду, числу и падежу
        static const std::unordered_set<std::wstring> femAccForms = {
            L"школу", L"работу", L"улицу", L"больницу", L"аптеку", L"квартиру", L"комнату",
            L"машину", L"москву", L"россию", L"кухню", L"деревню", L"библиотеку", L"остановку",
            L"почту", L"страну"
        };
        static const std::unordered_set<std::wstring> femPrepForms = {
            L"школе", L"работе", L"улице", L"больнице", L"аптеке", L"квартире", L"комнате",
            L"машине", L"москве", L"россии", L"кухне", L"деревне", L"библиотеке", L"остановке",
            L"почте", L"стране"
        };

        struct DetCaseForms {
            std::wstring fem, neut, plur;
            std::wstring mascGen, mascDat, mascInst, mascPrep;
            std::wstring femAcc, femOblique;
            std::wstring plurGenPrep, plurDat, plurInst;
        };
        static const std::unordered_map<std::wstring, DetCaseForms> detAgree = {
            {L"мой",    {L"моя", L"моё", L"мои", L"моего", L"моему", L"моим", L"моем", L"мою", L"моей", L"моих", L"моим", L"моими"}},
            {L"твой",   {L"твоя", L"твоё", L"твои", L"твоего", L"твоему", L"твоим", L"твоем", L"твою", L"твоей", L"твоих", L"твоим", L"твоими"}},
            {L"свой",   {L"своя", L"своё", L"свои", L"своего", L"своему", L"своим", L"своем", L"свою", L"своей", L"своих", L"своим", L"своими"}},
            {L"наш",    {L"наша", L"наше", L"наши", L"нашего", L"нашему", L"нашим", L"нашем", L"нашу", L"нашей", L"наших", L"нашим", L"нашими"}},
            {L"ваш",    {L"ваша", L"ваше", L"ваши", L"вашего", L"вашему", L"вашим", L"вашем", L"вашу", L"вашей", L"ваших", L"вашим", L"вашими"}},
            {L"этот",   {L"эта", L"это", L"эти", L"этого", L"этому", L"этим", L"этом", L"эту", L"этой", L"этих", L"этим", L"этими"}},
            {L"тот",    {L"та", L"то", L"те", L"того", L"тому", L"тем", L"том", L"ту", L"той", L"тех", L"тем", L"теми"}},
            {L"один",   {L"одна", L"одно", L"одни", L"одного", L"одному", L"одним", L"одном", L"одну", L"одной", L"одних", L"одним", L"одними"}},
            {L"какой",  {L"какая", L"какое", L"какие", L"какого", L"какому", L"каким", L"каком", L"какую", L"какой", L"каких", L"каким", L"какими"}},
            {L"каждый", {L"каждая", L"каждое", L"каждые", L"каждого", L"каждому", L"каждым", L"каждом", L"каждую", L"каждой", L"каждых", L"каждым", L"каждыми"}},
            {L"весь",   {L"вся", L"всё", L"все", L"всего", L"всему", L"всем", L"всем", L"всю", L"всей", L"всех", L"всем", L"всеми"}},
            {L"сам",    {L"сама", L"само", L"сами", L"самого", L"самому", L"самим", L"самом", L"саму", L"самой", L"самих", L"самим", L"самими"}}
        };

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
            const NounTag& tag = nounTags[i + 1];
            if (nextW.empty()) continue;

            if (tag.kase != CaseTag::None) {
                auto detIt = detAgree.find(cur);
                if (detIt != detAgree.end()) {
                    const auto& f = detIt->second;
                    if (tag.plural) {
                        if (tag.kase == CaseTag::Dat) {
                            logChange(cur, f.plurDat, L"Согласование местоимения во мн.ч. дательного падежа");
                        } else if (tag.kase == CaseTag::Inst) {
                            logChange(cur, f.plurInst, L"Согласование местоимения во мн.ч. творительного падежа");
                        } else if (tag.kase == CaseTag::Acc && !tag.animate) {
                            logChange(cur, f.plur, L"Согласование местоимения во мн.ч. винительного падежа");
                        } else {
                            logChange(cur, f.plurGenPrep, L"Согласование местоимения во мн.ч. косвенного падежа");
                        }
                    } else if (tag.gender == GenderTag::Fem) {
                        if (tag.kase == CaseTag::Acc) logChange(cur, f.femAcc, L"Согласование местоимения в винительном падеже ж.р.");
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
                        } else if (tag.kase == CaseTag::Acc && tag.gender == GenderTag::Neut) {
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
                        bool softStem = (end2 == L"ий");
                        if (tag.plural) {
                            if (tag.kase == CaseTag::Dat) {
                                tryAdjCandidates(cur, softStem ? stem + L"им" : stem + L"ым",
                                                 softStem ? stem + L"ым" : stem + L"им",
                                                 L"Согласование прилагательного во мн.ч. дательного падежа");
                            } else if (tag.kase == CaseTag::Inst) {
                                tryAdjCandidates(cur, softStem ? stem + L"ими" : stem + L"ыми",
                                                 softStem ? stem + L"ыми" : stem + L"ими",
                                                 L"Согласование прилагательного во мн.ч. творительного падежа");
                            } else if (tag.kase == CaseTag::Acc && !tag.animate) {
                                tryAdjCandidates(cur, softStem ? stem + L"ие" : stem + L"ые",
                                                 softStem ? stem + L"ые" : stem + L"ие",
                                                 L"Согласование прилагательного во мн.ч. винительного падежа");
                            } else {
                                tryAdjCandidates(cur, softStem ? stem + L"их" : stem + L"ых",
                                                 softStem ? stem + L"ых" : stem + L"их",
                                                 L"Согласование прилагательного во мн.ч. косвенного падежа");
                            }
                        } else if (tag.gender == GenderTag::Fem) {
                            if (tag.kase == CaseTag::Acc) {
                                tryAdjCandidates(cur, softStem ? stem + L"юю" : stem + L"ую",
                                                 softStem ? stem + L"ую" : stem + L"юю",
                                                 L"Согласование прилагательного в винительном падеже ж.р.");
                            } else {
                                tryAdjCandidates(cur, softStem ? stem + L"ей" : stem + L"ой",
                                                 softStem ? stem + L"ой" : stem + L"ей",
                                                 L"Согласование прилагательного в косвенном падеже ж.р.");
                            }
                        } else if (tag.gender == GenderTag::Masc || tag.gender == GenderTag::Neut) {
                            if (tag.kase == CaseTag::Gen || (tag.kase == CaseTag::Acc && tag.gender == GenderTag::Masc && tag.animate)) {
                                tryAdjCandidates(cur, softStem ? stem + L"его" : stem + L"ого",
                                                 softStem ? stem + L"ого" : stem + L"его",
                                                 L"Согласование прилагательного в родительном/винительном падеже");
                            } else if (tag.kase == CaseTag::Dat) {
                                tryAdjCandidates(cur, softStem ? stem + L"ему" : stem + L"ому",
                                                 softStem ? stem + L"ому" : stem + L"ему",
                                                 L"Согласование прилагательного в дательном падеже");
                            } else if (tag.kase == CaseTag::Inst) {
                                tryAdjCandidates(cur, softStem ? stem + L"им" : stem + L"ым",
                                                 softStem ? stem + L"ым" : stem + L"им",
                                                 L"Согласование прилагательного в творительном падеже");
                            } else if (tag.kase == CaseTag::Prep) {
                                tryAdjCandidates(cur, softStem ? stem + L"ем" : stem + L"ом",
                                                 softStem ? stem + L"ом" : stem + L"ем",
                                                 L"Согласование прилагательного в предложном падеже");
                            } else if (tag.kase == CaseTag::Acc && tag.gender == GenderTag::Neut) {
                                tryAdjCandidates(cur, softStem ? stem + L"ее" : stem + L"ое",
                                                 softStem ? stem + L"ое" : stem + L"ее",
                                                 L"Согласование прилагательного по среднему роду");
                            }
                        }
                        nounTags[i] = tag;
                    }
                }
                continue;
            }

            if (nonNounSkip.count(nextW)) continue;

            bool nextIsFemAcc = (femAccForms.count(nextW) > 0);
            bool nextIsFemPrep = (femPrepForms.count(nextW) > 0);
            bool nextIsPlur = (pluralNouns.count(nextW) > 0);
            bool nextIsNeut = (!nextIsPlur && !nextIsFemAcc && !nextIsFemPrep &&
                               ((nextW.back() == L'о' || nextW.back() == L'е' || neutExceptions.count(nextW) > 0)) &&
                               !mascExceptions.count(nextW));
            bool nextIsFem = (!nextIsPlur && !nextIsNeut && !nextIsFemAcc && !nextIsFemPrep &&
                              (((nextW.back() == L'а' || nextW.back() == L'я') &&
                                !mascExceptions.count(nextW) && !neutExceptions.count(nextW)) ||
                               femThirdDecl.count(nextW) > 0));

            auto detIt = detAgree.find(cur);
            if (detIt != detAgree.end()) {
                if (nextIsFemAcc) logChange(cur, detIt->second.femAcc, L"Согласование местоимения в винительном падеже ж.р.");
                else if (nextIsFemPrep) logChange(cur, detIt->second.femOblique, L"Согласование местоимения в предложном падеже ж.р.");
                else if (nextIsPlur) logChange(cur, detIt->second.plur, L"Согласование местоимения во множественном числе");
                else if (nextIsFem) logChange(cur, detIt->second.fem, L"Согласование местоимения в женском роде");
                else if (nextIsNeut) logChange(cur, detIt->second.neut, L"Согласование местоимения в среднем роде");
                continue;
            }

            if (!nonAdjectiveOiIy.count(cur) && cur.size() >= 4) {
                std::wstring end2 = cur.substr(cur.size() - 2);
                if (end2 == L"ый" || end2 == L"ий" || end2 == L"ой") {
                    std::wstring stem = cur.substr(0, cur.size() - 2);
                    if (nextIsFemAcc) {
                        std::wstring candSoft = stem + L"юю";
                        std::wstring candHard = stem + L"ую";
                        if (end2 == L"ий" && lookupFreq(candSoft) > 0) {
                            logChange(cur, candSoft, L"Согласование прилагательного в винительном падеже ж.р.");
                        } else if (lookupFreq(candHard) > 0) {
                            logChange(cur, candHard, L"Согласование прилагательного в винительном падеже ж.р.");
                        }
                    } else if (nextIsFemPrep) {
                        std::wstring cand = stem + ((end2 == L"ий") ? L"ей" : L"ой");
                        if (lookupFreq(cand) > 0) {
                            logChange(cur, cand, L"Согласование прилагательного в предложном падеже ж.р.");
                        } else if (end2 == L"ий" && lookupFreq(stem + L"ой") > 0) {
                            logChange(cur, stem + L"ой", L"Согласование прилагательного в предложном падеже ж.р.");
                        }
                    } else if (nextIsPlur) {
                        std::wstring candI = stem + L"ие";
                        std::wstring candY = stem + L"ые";
                        if (end2 == L"ий" && lookupFreq(candI) > 0) {
                            logChange(cur, candI, L"Согласование прилагательного во множественном числе");
                        } else if (lookupFreq(candY) > 0) {
                            logChange(cur, candY, L"Согласование прилагательного во множественном числе");
                        } else if (lookupFreq(candI) > 0) {
                            logChange(cur, candI, L"Согласование прилагательного во множественном числе");
                        }
                    } else if (nextIsFem) {
                        std::wstring candSoft = stem + L"яя";
                        std::wstring candHard = stem + L"ая";
                        if (end2 == L"ий" && lookupFreq(candSoft) > 0) {
                            logChange(cur, candSoft, L"Согласование прилагательного по женскому роду");
                        } else if (lookupFreq(candHard) > 0) {
                            logChange(cur, candHard, L"Согласование прилагательного по женскому роду");
                        }
                    } else if (nextIsNeut) {
                        std::wstring candE = stem + L"ее";
                        std::wstring candO = stem + L"ое";
                        if (end2 == L"ий" && lookupFreq(candE) > 0) {
                            logChange(cur, candE, L"Согласование прилагательного по среднему роду");
                        } else if (lookupFreq(candO) > 0) {
                            logChange(cur, candO, L"Согласование прилагательного по среднему роду");
                        } else if (lookupFreq(candE) > 0) {
                            logChange(cur, candE, L"Согласование прилагательного по среднему роду");
                        }
                    }
                }
            }
        }

        // 7. Сборка итогового предложения с пунктуацией и заглавной буквой
        static const std::unordered_set<std::wstring> alwaysCommaFirst = {
            L"здравствуйте", L"привет", L"короче", L"да", L"нет", L"конечно",
            L"пожалуйста", L"наверное", L"кстати", L"например", L"видимо"
        };
        static const std::unordered_set<std::wstring> clauseOpeningImp = {
            L"извините", L"простите", L"скажи", L"скажите",
            L"подскажи", L"подскажите", L"слушай", L"слушайте"
        };
        static const std::unordered_set<std::wstring> conjCoords = {
            L"и", L"а", L"но", L"или"
        };
        static const std::unordered_set<std::wstring> subordAlways = {
            L"чтобы", L"если", L"потому", L"поэтому", L"поскольку", L"хотя", L"однако", L"зато"
        };
        static const std::unordered_set<std::wstring> kotoryForms = {
            L"который", L"которая", L"которое", L"которые",
            L"которого", L"которому", L"которым", L"котором",
            L"которой", L"которую", L"которых", L"которыми"
        };
        static const std::unordered_set<std::wstring> prepsForRel = {
            L"в", L"во", L"на", L"с", L"со", L"к", L"ко", L"о", L"об", L"обо",
            L"у", L"от", L"до", L"из", L"за", L"под", L"над", L"перед", L"между",
            L"через", L"про", L"для", L"по", L"при", L"без"
        };
        static const std::unordered_set<std::wstring> commaBeforeTriggers = {
            L"знаю", L"знаешь", L"знает", L"знаем", L"знаете", L"знают", L"знал", L"знала", L"знали",
            L"думаю", L"думаешь", L"думает", L"думаем", L"думаете", L"думают", L"думал", L"думала", L"думали",
            L"подумал", L"подумала", L"сказал", L"сказала", L"сказали", L"скажу", L"скажи", L"скажите",
            L"говорю", L"говоришь", L"говорит", L"говорим", L"говорите", L"говорят", L"говорил", L"говорила", L"говорили",
            L"ответил", L"ответила", L"ответили", L"спросил", L"спросила", L"спросили", L"спрашиваю", L"спрашиваешь", L"спрашивает",
            L"написал", L"написала", L"написали", L"напиши", L"напишите",
            L"вижу", L"видишь", L"видит", L"видим", L"видите", L"видят", L"видел", L"видела", L"видели",
            L"смотрю", L"смотри", L"смотрите", L"смотрел",
            L"слышу", L"слышишь", L"слышит", L"слышим", L"слышите", L"слышат", L"слышал", L"слышала", L"слышали",
            L"понимаю", L"понимаешь", L"понимает", L"понимаем", L"понимаете", L"понимают",
            L"понял", L"поняла", L"поняли", L"пойму", L"поймешь", L"поймет", L"поймёт",
            L"кажется", L"надеюсь", L"верю", L"уверен", L"уверена", L"уверены",
            L"рад", L"рада", L"рады", L"хорошо", L"плохо", L"жаль", L"правда", L"важно", L"понятно",
            L"известно", L"интересно", L"странно", L"удивительно", L"очевидно", L"видно", L"слышно", L"значит",
            L"чувствую", L"боюсь", L"обещаю",
            L"помню", L"помнишь", L"помнит", L"помним", L"помните", L"помнят",
            L"забыл", L"забыла", L"забыли", L"сомневаюсь",
            L"проверь", L"проверьте", L"узнай", L"узнайте", L"объясни", L"объясните",
            L"подскажи", L"подскажите", L"расскажи", L"расскажите", L"покажи", L"покажите",
            L"напомни", L"напомните", L"выяснил", L"решил"
        };
        static const std::unordered_set<std::wstring> objPronounsBeforeSubord = {
            L"мне", L"тебе", L"ему", L"ей", L"нам", L"вам", L"им", L"меня", L"тебя", L"нас", L"вас"
        };
        static const std::unordered_set<std::wstring> questionWords = {
            L"что", L"как", L"где", L"куда", L"откуда", L"почему", L"зачем",
            L"сколько", L"кто", L"когда", L"какой", L"какая", L"какое", L"какие", L"чей", L"чья", L"чьё", L"чье"
        };
        static const std::unordered_set<std::wstring> subjectPronouns = {
            L"я", L"ты", L"он", L"она", L"оно", L"мы", L"вы", L"они", L"это"
        };
        static const std::unordered_set<std::wstring> netPredicateObjects = {
            L"времени", L"имени", L"работы", L"школы", L"денег", L"дома",
            L"друга", L"подруги", L"телефона", L"машины", L"воды", L"хлеба",
            L"настроения", L"помощи", L"места", L"мамы", L"папы", L"врача",
            L"города", L"магазина", L"комнаты", L"квартиры", L"улицы", L"книги",
            L"дела", L"слова", L"сил", L"слов", L"проблем", L"смысла",
            L"желания", L"возможности", L"никого", L"ничего", L"свободного", L"нового",
            L"интернета", L"связи", L"билета"
        };

        auto isClauseOpenerFirst = [&](const std::wstring& w) -> bool {
            return alwaysCommaFirst.count(w) || clauseOpeningImp.count(w);
        };

        auto endsWithAnySuffix = [](const std::wstring& s, std::initializer_list<const wchar_t*> suffixes) -> bool {
            for (const wchar_t* suf : suffixes) {
                size_t sl = 0;
                while (suf[sl] != L'\0') ++sl;
                if (s.size() > sl && s.compare(s.size() - sl, sl, suf) == 0) return true;
            }
            return false;
        };

        auto isFiniteNonInfVerb = [&](const std::wstring& s) -> bool {
            if (s.size() < 2 || nonNounSkip.count(s) || nonInflectExtra.count(s) ||
                subjectPronouns.count(s) || questionWords.count(s) || prepsForRel.count(s) ||
                conjCoords.count(s) || subordAlways.count(s) || kotoryForms.count(s)) {
                return false;
            }
            if (endsWithAnySuffix(s, {L"ть", L"ться", L"ти", L"чь"})) return false;
            static const std::unordered_set<std::wstring> knownVerbs = {
                L"хочу", L"хочешь", L"хочет", L"хотим", L"хотите", L"хотят",
                L"могу", L"можешь", L"может", L"можем", L"можете", L"могут",
                L"буду", L"будешь", L"будет", L"будем", L"будете", L"будут",
                L"приду", L"придешь", L"придёт", L"придет", L"помогу", L"поможешь", L"поможет",
                L"успею", L"успеешь", L"успеет", L"иду", L"идешь", L"идет", L"идёт",
                L"пойду", L"пойдешь", L"пойдет", L"пойдёт", L"позвони", L"позвоните",
                L"скажи", L"скажите", L"дай", L"дайте", L"иди", L"идите",
                L"напиши", L"напишите", L"подожди", L"подождите", L"приходи", L"звони",
                L"пошел", L"пошёл", L"пошла", L"пошли", L"пришел", L"пришёл", L"пришла", L"пришли",
                L"ушел", L"ушёл", L"ушла", L"ушли", L"устал", L"устала", L"устали",
                L"заболел", L"заболела", L"опоздал", L"опоздала", L"забыл", L"забыла",
                L"останусь", L"останется", L"болит", L"идет", L"идёт", L"стоит"
            };
            if (knownVerbs.count(s)) return true;
            if (endsWithAnySuffix(s, {L"ешь", L"ёшь", L"ишь", L"ете", L"ёте", L"ите",
                                      L"усь", L"юсь", L"ись", L"ется", L"ится", L"ются", L"ятся"})) {
                return true;
            }
            if (endsWithAnySuffix(s, {L"ет", L"ёт", L"ит", L"ем", L"ём", L"им", L"ут", L"ют", L"ат", L"ят"}) &&
                !(lookupFreq(s + L"ом") > 0 || lookupFreq(s + L"а") > 0)) {
                return true;
            }
            if (s.size() >= 4 && endsWithAnySuffix(s, {L"ал", L"ала", L"али", L"ел", L"ела", L"ели",
                                                       L"ил", L"ила", L"или"}) &&
                lookupFreq(s + L"е") == 0) {
                return true;
            }
            return false;
        };

        auto isClausePredicative = [&](const std::wstring& s) -> bool {
            if (isFiniteNonInfVerb(s)) return true;
            static const std::unordered_set<std::wstring> shortPreds = {
                L"дома", L"нужно", L"надо", L"можно", L"нельзя", L"пора",
                L"хорошо", L"плохо", L"жаль", L"тепло", L"холодно", L"готов", L"готова",
                L"готовы", L"рад", L"рада", L"рады", L"болен", L"занят", L"занята", L"нет"
            };
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
            for (size_t j = openIdx + 3; j < nTok; ++j) {
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
        if (!currentInputWord_.empty()) {
            commitWord();
        }

        std::wstring cur;
        for (size_t i = 0; i < word.size(); ++i) {
            wchar_t ch = word[i];
            if (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n') {
                if (!cur.empty()) {
                    currentInputWord_ = cur;
                    cur.clear();
                    commitWord();
                }
            } else if (ch == L'.' || ch == L'!' || ch == L'?') {
                if (!cur.empty()) {
                    currentInputWord_ = cur;
                    cur.clear();
                    commitWord();
                }
                if (!rawTokens_.empty()) {
                    explicitEndPunct_ = ch;
                    rebuildSentence();
                }
            } else if (ch == L',' || ch == L';' || ch == L':' ||
                       ch == L'(' || ch == L')' || ch == L'"' ||
                       ch == L'«' || ch == L'»' || ch == L'—' || ch == L'–' ||
                       ch == L'…' || ch == L'/') {
                if (!cur.empty()) {
                    currentInputWord_ = cur;
                    cur.clear();
                    commitWord();
                }
                if (ch == L',' && !explicitCommaAfter_.empty()) {
                    explicitCommaAfter_.back() = true;
                    rebuildSentence();
                }
            } else if (ch == L'-') {
                bool hasNextLetter = (i + 1 < word.size() && isWordLetter(word[i + 1]));
                if (!cur.empty() && hasNextLetter) {
                    cur.push_back(ch);
                } else if (!cur.empty()) {
                    currentInputWord_ = cur;
                    cur.clear();
                    commitWord();
                }
            } else if (isWordLetter(ch)) {
                explicitEndPunct_ = 0;
                cur.push_back(toLowerRu(ch));
            }
        }
        if (!cur.empty()) {
            currentInputWord_ = cur;
            commitWord();
        }
    }

    void SentenceGrammarEngine::addText(const std::wstring& text) {
        if (text.empty()) return;

        for (size_t i = 0; i < text.size(); ++i) {
            wchar_t ch = text[i];
            if (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n') {
                if (!currentInputWord_.empty()) commitWord();
            } else if (ch == L'.' || ch == L'!' || ch == L'?') {
                if (!currentInputWord_.empty()) commitWord();
                if (!rawTokens_.empty()) {
                    explicitEndPunct_ = ch;
                    rebuildSentence();
                }
            } else if (ch == L',' || ch == L';' || ch == L':' ||
                       ch == L'(' || ch == L')' || ch == L'"' ||
                       ch == L'«' || ch == L'»' || ch == L'—' || ch == L'–' ||
                       ch == L'…' || ch == L'/') {
                if (!currentInputWord_.empty()) commitWord();
                if (ch == L',' && !explicitCommaAfter_.empty()) {
                    explicitCommaAfter_.back() = true;
                    rebuildSentence();
                }
            } else if (ch == L'-') {
                // Дефис внутри слова сохраняем ("какие-нибудь"), отдельный дефис-тире пропускаем
                bool hasNextLetter = (i + 1 < text.size() && isWordLetter(text[i + 1]));
                if (!currentInputWord_.empty() && hasNextLetter) {
                    currentInputWord_.push_back(L'-');
                } else if (!currentInputWord_.empty()) {
                    commitWord();
                }
            } else if (isWordLetter(ch)) {
                explicitEndPunct_ = 0;
                currentInputWord_.push_back(toLowerRu(ch));
            }
        }

        if (!currentInputWord_.empty()) {
            commitWord();
        }
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
