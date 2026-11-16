#pragma once
// SmartVoice.h - Кроссплатформенный офлайн-синтез русской речи (sherpa-onnx + Piper VITS + miniaudio)
// Не зависит от Windows API / SAPI (работает на Windows, Linux, Android, iOS).
// Возможности:
//  - Естественный нейросетевой голос Piper (ru_RU-irina-medium и др.)
//  - Быстрая фоновая загрузка модели при старте приложения (без зависания UI)
//  - Потоковое асинхронное воспроизведение без блокировки основного цикла
//  - Нормализация текста (числа -> русские слова, сокращения РЖЯ / т.д. / т.п. / т.е.)
//  - Пауза / продолжение, прерывание фразы, регулировка скорости и громкости
//  - Офлайн-синтез в PCM-буфер и WAV-файл (для тестов и экспорта)
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct SherpaOnnxOfflineTts;

namespace HandTracker {

    struct VoiceInfo {
        std::wstring id;
        std::wstring name;
        std::wstring gender; // "Male" или "Female"
        std::string modelPath;
        std::string tokensPath;
        std::string dataDir;
    };

    class SmartVoice {
        struct SpeechItem {
            std::wstring text;
            uint64_t seq = 0;
        };

        struct AudioBackend;

        const SherpaOnnxOfflineTts* tts_ = nullptr;
        std::unique_ptr<AudioBackend> audio_;

        mutable std::mutex stateMutex_;
        std::condition_variable cv_;
        std::mutex ttsEngineMutex_;
        mutable std::mutex audioMutex_;
        mutable std::mutex audioDeviceMutex_;

        std::thread workerThread_;
        std::deque<SpeechItem> pendingQueue_;
        std::vector<float> pcmBuffer_;
        size_t pcmReadPos_ = 0;

        std::atomic<bool> available_{false};
        std::atomic<bool> ready_{false};
        std::atomic<bool> loading_{false};
        std::atomic<bool> loadFailed_{false};
        std::atomic<bool> shuttingDown_{false};
        std::atomic<bool> paused_{false};
        std::atomic<bool> synthesizing_{false};
        std::atomic<bool> audioOutputEnabled_{true};
        std::atomic<bool> audioOutputReady_{false};
        std::atomic<uint64_t> utteranceSeq_{1};
        std::atomic<uint64_t> activeSynthesisSeq_{0};
        std::atomic<int> rate_{0};
        std::atomic<float> volume_{1.0f};
        std::atomic<double> lastLoadMs_{0.0};
        std::atomic<int> ttsSampleRate_{22050};

        int numThreads_ = 2;
        std::string modelsRoot_ = "Models/tts";
        std::wstring currentVoiceName_;
        VoiceInfo activeVoice_;
        std::vector<VoiceInfo> voices_;

        void discoverVoices(const std::string& rootDir);
        bool loadVoiceModel(const VoiceInfo& voice, bool preserveExistingOnFail = false);
        bool ensureAudioDevice(int sampleRate);
        void destroyAudioDevice();
        void destroyTts();
        void workerLoop();
        void startWorkerIfNeeded();
        void shutdown();

        static void audioDataCallback(void* pOutput, uint32_t frameCount, void* userData);
        static int32_t ttsProgressCallback(const float* samples, int32_t n, float progress, void* arg);

    public:
        SmartVoice();
        ~SmartVoice();

        SmartVoice(const SmartVoice&) = delete;
        SmartVoice& operator=(const SmartVoice&) = delete;

        // Инициализация движка.
        // voicePreference: "Irina", "Dmitri", id папки или "" (первый найденный)
        // modelsDir: путь к каталогу моделей TTS (по умолчанию "Models/tts")
        // asyncLoad: true -> валидация путей и фоновая загрузка без задержки UI
        bool init(const std::wstring& voicePreference = L"Irina",
                  int rate = 0,
                  int volume = 100,
                  const std::string& modelsDir = "Models/tts",
                  bool asyncLoad = false);

        bool initAsync(const std::wstring& voicePreference = L"Irina",
                       int rate = 0,
                       int volume = 100,
                       const std::string& modelsDir = "Models/tts");

        bool waitUntilReady();

        bool isAvailable() const { return available_.load(); }
        bool isReady() const { return ready_.load(); }
        bool isLoading() const { return loading_.load(); }
        bool isPaused() const { return paused_.load(); }
        bool isAudioOutputEnabled() const { return audioOutputEnabled_.load(); }
        bool hasAudioOutput() const { return audioOutputReady_.load(); }
        void setAudioOutputEnabled(bool enabled);
        double lastLoadTimeMs() const { return lastLoadMs_.load(); }
        int sampleRate() const { return ttsSampleRate_.load(); }

        std::wstring currentVoice() const;
        const std::vector<VoiceInfo>& availableVoices() const { return voices_; }

        // Переключить голос ("Irina", "Dmitri" или id папки)
        bool setVoice(const std::wstring& voiceName);

        // Нормализация текста (числа -> слова, РЖЯ / т.д. / т.п. / т.е.)
        std::wstring normalizeText(const std::wstring& input) const;

        // Подготовка к озвучиванию: нормализация + ёфикация + контекстное разрешение ударений (+ / U+0301)
        std::wstring prepareForSpeech(const std::wstring& input) const;

        // Озвучить текст (асинхронно, нормализуя числа и сокращения)
        void speak(const std::wstring& text, bool purgePrevious = true);

        // Озвучить отдельное слово (добавляется в очередь без агрессивного сброса)
        void speakWord(const std::wstring& word);

        // Синхронный синтез в PCM-буфер или WAV-файл (удобно для тестов и экспорта)
        bool synthesizeToPcm(const std::wstring& text, std::vector<float>& outSamples, int& outSampleRate);
        bool synthesizeToWav(const std::wstring& text, const std::string& wavPath);

        void pauseResume();
        void stop();
        void setRate(int rate);     // -10..10
        void setVolume(int volume); // 0..100
        bool isSpeaking() const;
    };

} // namespace HandTracker
