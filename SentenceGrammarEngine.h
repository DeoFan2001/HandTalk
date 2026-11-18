#pragma once
// SentenceGrammarEngine.h - Кроссплатформенный модуль интеллектуальной сборки слов в грамматически правильные предложения
// Не зависит от Windows API (подходит для Windows, Linux, Android, iOS).
// Решает ключевые задачи перевода РЖЯ -> Русский язык:
//  1. Посимвольная и потоковая сборка букв в слова (с поддержкой пробела-разделителя и QWERTY-режима)
//  2. Автоматическое исправление опечаток (частотный словарь + биграммы + расстояние Дамерау-Левенштейна)
//  3. Автоматическое разделение склеенных слов (Missing Space)
//  4. Грамматическое согласование цепочки слов (модальные глаголы, спряжение, падежи, род, дефисы)
//  5. Синтаксическая пунктуация и заглавные буквы
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace HandTracker {

    // Описание примененного грамматического правила (для отчёта и экрана)
    struct GrammarRuleLog {
        std::wstring before;
        std::wstring after;
        std::wstring description;
    };

    class SentenceGrammarEngine {
        struct DictEntry {
            std::wstring word;
            int freq = 0;
            uint32_t charMask = 0;
        };

        // Основной словарь: слово -> частота
        std::unordered_map<std::wstring, int> dictionary_;
        // Компактное хранилище записей словаря и быстрые плоские индексы для опечаток (< 1 мс)
        static constexpr size_t kNumRuLetters = 32;
        static constexpr size_t kMaxBucketLen = 32;
        std::vector<DictEntry> dictEntries_;
        std::vector<std::vector<uint32_t>> bucketsByFirstLen_;
        std::vector<std::vector<uint32_t>> bucketsBySecondLen_;

        // Словарь биграмм: prevWord -> (nextWord -> weight)
        std::unordered_map<std::wstring, std::unordered_map<std::wstring, int>> bigrams_;
        size_t bigramCount_ = 0;

        // Буферы набора
        std::wstring currentInputWord_;             // Текущие буквы незаконченного слова
        std::vector<std::wstring> rawTokens_;       // Список сырых слов, как их ввели дактилемами/исправлением
        std::vector<bool> explicitCommaAfter_;      // Явная запятая после соответствующего токена rawTokens_
        std::vector<std::wstring> processedTokens_; // Список грамматически согласованных слов
        std::vector<GrammarRuleLog> ruleLogs_;      // История сработавших правил грамматики

        std::wstring cachedFinalSentence_;          // Итоговое предложение со знаками препинания
        wchar_t explicitEndPunct_ = 0;              // Явный знак конца (если передан в addText)
        bool qwertyMapping_ = true;                 // Автоконвертация латиницы QWERTY -> ЙЦУКЕН в addLetter()
        std::string dataDir_;
        std::unordered_map<std::wstring, std::vector<std::wstring>> exactFixes_;

        // Вспомогательные алгоритмы
        void loadSupplementalLexicon();
        void loadExactFixes();
        void rebuildIndices();
        int getBigramWeight(const std::wstring& prev, const std::wstring& cur) const;
        int editDistance(const std::wstring& s1, const std::wstring& s2) const;
        int editDistanceBounded(const std::wstring& s1, const std::wstring& s2, int maxDist) const;
        std::pair<std::wstring, double> findBestTypoCorrection(const std::wstring& target,
                                                                const std::wstring& prevWord = L"",
                                                                int* outDist = nullptr,
                                                                int* outFreq = nullptr) const;
        std::vector<std::wstring> fixWordTokens(const std::wstring& rawWord,
                                                const std::wstring& prevWord = L"");
        void rebuildSentence();

    public:
        SentenceGrammarEngine();

        bool loadDictionary(const std::string& dictPath);
        bool loadBigrams(const std::string& bigramPath);

        // Включить/выключить маппинг латинской раскладки QWERTY -> ЙЦУКЕН для addLetter()
        void setQwertyMapping(bool enabled) { qwertyMapping_ = enabled; }
        bool qwertyMapping() const { return qwertyMapping_; }

        // --- Ввод символов и текста ---
        void addLetter(wchar_t ch);                  // Пробел/перевод строки автоматически подтверждают слово
        void backspace();
        void commitWord();                           // Вызывается при пробеле или паузе без руки
        void addWord(const std::wstring& word);      // Добавить слово (или несколько слов через пробел)
        void addText(const std::wstring& text);      // Потоковый ввод строки без QWERTY-искажения пунктуации
        void clear();

        // --- Получение результатов ---
        const std::wstring& getCurrentWord() const { return currentInputWord_; }
        const std::wstring& getFinalSentence() const { return cachedFinalSentence_; }
        std::wstring getRawSentence() const;
        const std::vector<GrammarRuleLog>& getRuleLogs() const { return ruleLogs_; }
        const std::vector<std::wstring>& getRawTokens() const { return rawTokens_; }
        const std::vector<std::wstring>& getProcessedTokens() const { return processedTokens_; }

        int getWordCount() const { return (int)processedTokens_.size(); }
        size_t getDictionarySize() const { return dictionary_.size(); }
        size_t getBigramCount() const { return bigramCount_; }
    };

} // namespace HandTracker
