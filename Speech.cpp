#define NOMINMAX
#include "Speech.h"
#include <windows.h>
#include <sapi.h>
#include <algorithm>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "sapi.lib")

namespace HandTracker {

    Speech::~Speech() {
        if (voice_) { voice_->Release(); voice_ = nullptr; }
        if (comInit_) CoUninitialize();
    }

    bool Speech::init(int rate, int volume) {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        comInit_ = SUCCEEDED(hr);
        hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, (void**)&voice_);
        if (FAILED(hr) || !voice_) { voice_ = nullptr; return false; }

        // ищем русский голос (код языка 419 = ru-RU)
        ISpObjectTokenCategory* cat = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
            IID_ISpObjectTokenCategory, (void**)&cat)) && cat) {
            if (SUCCEEDED(cat->SetId(SPCAT_VOICES, FALSE))) {
                IEnumSpObjectTokens* en = nullptr;
                if (SUCCEEDED(cat->EnumTokens(L"Language=419", nullptr, &en)) && en) {
                    ISpObjectToken* tok = nullptr;
                    ULONG got = 0;
                    if (SUCCEEDED(en->Next(1, &tok, &got)) && got == 1 && tok) {
                        if (SUCCEEDED(voice_->SetVoice(tok))) russian_ = true;
                        tok->Release();
                    }
                    en->Release();
                }
            }
            cat->Release();
        }
        voice_->SetRate(std::min(10, std::max(-10, rate)));
        voice_->SetVolume((USHORT)std::min(100, std::max(0, volume)));
        return true;
    }

    void Speech::speak(const std::wstring& text) {
        if (!voice_ || text.empty()) return;
        if (paused_) { voice_->Resume(); paused_ = false; }
        voice_->Speak(text.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
    }

    void Speech::pauseResume() {
        if (!voice_) return;
        if (paused_) { voice_->Resume(); paused_ = false; }
        else { voice_->Pause(); paused_ = true; }
    }

    void Speech::stop() {
        if (!voice_) return;
        if (paused_) { voice_->Resume(); paused_ = false; }
        voice_->Speak(L"", SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
    }

    bool Speech::busy() const {
        if (!voice_) return false;
        SPVOICESTATUS st{};
        if (FAILED(voice_->GetStatus(&st, nullptr))) return false;
        return st.dwRunningState == SPRS_IS_SPEAKING;
    }

} // namespace HandTracker
