// SherpaEngine.h - 离线流式语音识别（sherpa-onnx + zipformer 中文模型）
//
// 为什么要换掉系统自带的 SAPI 听写：
//   Windows 桌面中文识别器用的是十几年前的老声学模型，实测连干净的合成语音
//   都会把「同学们，上课」识别成「窃听那上课」，一句话里一半错字。
//   匹配算法再强也扛不住输入全是噪声，所以这里改用 sherpa-onnx 跑
//   streaming zipformer 中文模型（xlarge int8），纯本地、不联网。
//
// 实现要点：
//  1. 复用语雀（CSpeechEngine）已经验证过的 WASAPI 自采集，16k/mono/PCM16，
//     避免「SAPI 只认默认录音设备」那类坑。
//  2. 模型加载要 3~7 秒（700MB 权重），放到后台线程，界面不卡。
//  3. 端点检测（endpoint）打开：一句话说完静音 2~3.5 秒就收尾，避免把整堂课
//     识别成一段无限增长的文本。
#pragma once

#include "framework.h"
#include "AudioCapture.h"
#include "sherpa-onnx/c-api/c-api.h"

#include <thread>
#include <atomic>
#include <mutex>

class CSherpaEngine
{
public:
    using ResultFn = std::function<void(const std::wstring& text, bool isFinal)>;

    ~CSherpaEngine() { Stop(); }

    // 返回 true 只代表「加载任务已启动」；加载结果通过 IsLoading/IsReady 观察。
    bool Start(const std::wstring& modelDir, CAudioCapture* cap,
               const std::wstring& deviceId, ResultFn fn, std::wstring& err);
    void Stop();
    void Poll();

    bool IsLoading() const { return m_loading; }      // 正在后台加载模型
    bool IsReady() const { return m_active; }         // 已经能出识别结果
    bool IsFileMode() const { return m_fileMode; }    // 自检：WAV 回灌
    int  Level() const { return (int)m_level; }       // 0..100 实时电平
    DWORD LastSoundTick() const { return m_lastSoundTick; }
    bool HasError() const;
    std::wstring LastError() const;

    static std::wstring FindModelDir();               // 自动查找模型目录（空 = 没找到）
    static std::wstring ModelFolderName();
    static bool ModelAvailable() { return !FindModelDir().empty(); }

private:
    void LoadThread();
    void ReleaseAll();
    void Emit(const std::wstring& text, bool isFinal);

    const SherpaOnnxOnlineRecognizer* m_reco = nullptr;
    const SherpaOnnxOnlineStream*     m_stream = nullptr;

    CAudioCapture* m_cap = nullptr;
    ResultFn       m_fn;
    std::wstring   m_modelDir;
    std::wstring   m_deviceId;

    std::thread       m_thread;
    std::atomic<bool> m_loading{ false };
    std::atomic<bool> m_ready{ false };
    std::atomic<bool> m_failed{ false };
    std::atomic<bool> m_active{ false };
    std::atomic<bool> m_stopFlag{ false };

    mutable std::mutex m_errMutex;
    std::wstring       m_errText;

    std::wstring m_lastText;
    std::vector<float> m_samples;
    DWORD m_lastSoundTick = 0;
    std::atomic<int> m_level{ 0 };

    // 自检：TP_SPEECH_WAV=<16k/16bit wav> 时用文件代替麦克风，
    // 按实时速率喂给识别器，行为和真麦克风一致（只是不需要人说话）
    bool               m_fileMode = false;
    std::vector<float> m_wavSamples;
    size_t             m_wavPos = 0;
    DWORD              m_lastFeedTick = 0;
};
