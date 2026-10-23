// SpeechEngine.h - SAPI 听写识别封装（用于语音跟读）
//
// 实测要点（Windows 11 + 中文 SAPI 引擎）：
//  1. in-proc 识别器不能用 QueryInterface 拿 ISpRecoContext（返回 E_NOINTERFACE），
//     必须调用 ISpRecognizer::CreateRecoContext。
//  2. 该引擎只支持 SPLO_STATIC 听写语法，SPLO_DYNAMIC 会返回 E_INVALIDARG。
//  3. 用 SetNotifyWin32Event + 事件句柄轮询最稳（窗口消息偶尔收不到）。
//  4. 若 in-proc 全流程失败，自动退化到 shared recognizer。
#pragma once

#include "framework.h"
#include "AudioCapture.h"
#include "SapiAudioStream.h"
#include "SherpaEngine.h"

#include <memory>

class CSpeechEngine
{
public:
    using ResultFn = std::function<void(const std::wstring& text, bool isFinal)>;

    // 识别后端：Auto 优先用离线模型（没装则回退系统引擎）
    enum class Backend { Auto = 0, Sherpa = 1, Sapi = 2 };

    ~CSpeechEngine() { Stop(); }

    void SetBackend(Backend b) { m_backend = b; }
    Backend GetBackend() const { return m_backend; }

    // deviceId 为空：自动优先「默认通信设备」（会议软件通常用这个），再退回默认录音设备
    bool Start(int langId, const std::wstring& deviceId, ResultFn fn, std::wstring& err);
    void Stop();
    bool IsRunning() const { return m_running; }

    // 由界面定时器周期调用（非阻塞）
    void Poll();

    static std::vector<AudioDeviceInfo> ListDevices() { return CAudioCapture::EnumDevices(); }
    static bool OfflineModelAvailable() { return CSherpaEngine::ModelAvailable(); }

    bool IsPreparing() const { return m_sherpa && m_sherpa->IsLoading(); }
    bool IsSelfTest() const { return m_selfTest; }   // TP_SPEECH_WAV 自检（两种引擎都可能）

    const std::wstring& EngineName() const { return m_engineName; }
    const std::wstring& LastError() const { return m_error; }
    const std::wstring& InputName() const { return m_inputName; }
    int  InputLevel() const { return m_sherpa ? m_sherpa->Level() : m_cap.Level(); }
    DWORD LastSoundTick() const { return m_lastSoundTick; }

private:
    bool StartOffline(const std::wstring& deviceId, ResultFn fn, std::wstring& err);
    bool InitWith(bool sharedRecognizer, int langId, const std::wstring& deviceId);
    void HandleEvent();

    Backend m_backend = Backend::Auto;
    std::unique_ptr<CSherpaEngine> m_sherpa;
    bool m_selfTest = false;

    CComPtr<ISpRecognizer>  m_reco;
    CComPtr<ISpRecoContext> m_ctx;
    CComPtr<ISpRecoGrammar> m_grammar;
    CComPtr<ISpStream>      m_inStream;     // 仅自检模式（TP_SPEECH_WAV）
    CComPtr<CAudioPullStream> m_pullStream; // 自采集音频 → SAPI
    CAudioCapture m_cap;
    HANDLE    m_evt = nullptr;
    bool      m_running = false;
    bool      m_shared = false;
    DWORD     m_lastSoundTick = 0;
    ResultFn  m_fn;
    std::wstring m_engineName;
    std::wstring m_inputName;
    std::wstring m_error;
};
