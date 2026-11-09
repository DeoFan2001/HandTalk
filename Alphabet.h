#pragma once
// Alphabet.h - алфавит дактильной азбуки РЖЯ (33 буквы)
// Код буквы пишется латиницей: так его удобно хранить в CSV и вводить без оглядки на кодировку.
// На экран и в озвучивание идёт русская буква.
#include <cstddef>
#include <string>

namespace HandTracker {

    struct AlphabetEntry {
        const char* code;   // код буквы латиницей
        wchar_t letter;     // русская буква (Юникод)
    };

    constexpr std::size_t ALPHABET_SIZE = 33;

    // === ТАБЛИЦА БУКВ (в порядке алфавита) ===
    inline const AlphabetEntry ALPHABET[ALPHABET_SIZE] = {
        {"A",    0x0410}, {"B",    0x0411}, {"V",    0x0412}, {"G",    0x0413},
        {"D",    0x0414}, {"E",    0x0415}, {"YO",   0x0401}, {"ZH",   0x0416},
        {"Z",    0x0417}, {"I",    0x0418}, {"J",    0x0419}, {"K",    0x041A},
        {"L",    0x041B}, {"M",    0x041C}, {"N",    0x041D}, {"O",    0x041E},
        {"P",    0x041F}, {"R",    0x0420}, {"S",    0x0421}, {"T",    0x0422},
        {"U",    0x0423}, {"F",    0x0424}, {"H",    0x0425}, {"C",    0x0426},
        {"CH",   0x0427}, {"SH",   0x0428}, {"SCH",  0x0429}, {"HARD", 0x042A},
        {"Y",    0x042B}, {"SOFT", 0x042C}, {"EH",   0x042D}, {"YU",   0x042E},
        {"YA",   0x042F}
    };

    // Номер буквы в алфавите, -1 - если такого кода нет
    inline int indexFromCode(const std::string& code) {
        for (std::size_t i = 0; i < ALPHABET_SIZE; ++i)
            if (code == ALPHABET[i].code) return (int)i;
        return -1;
    }

    // Русская буква по коду, 0 - если такого кода нет
    inline wchar_t letterFromCode(const std::string& code) {
        int i = indexFromCode(code);
        return i < 0 ? (wchar_t)0 : ALPHABET[i].letter;
    }

} // namespace HandTracker
