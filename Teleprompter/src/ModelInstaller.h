// ModelInstaller.h - 离线语音模型「按需下载 / 安装」
//
// 发布包不再内置 570MB 的 zipformer 模型：第一次要用跟读时再联网下载。
//
// 两条通道：
//   1) 首选 —— 从 HF 镜像按「单个文件」拉取（hf-mirror.com → huggingface.co）。
//      不需要 tar、不赌 570MB 单包的完整性，某个文件坏了只重下那一个。
//   2) 兜底 —— 官方 GitHub release 的 tar.bz2 整包（也用 TP_MODEL_URL 指定自建源）。
//
// 安装位置：优先 exe 同级的 models\（绿色便携，能写就写这里），
// 写不进去（装在 Program Files 等只读目录）时退到 %APPDATA%\TeacherPrompter\models\。
// 两处都在 CSherpaEngine::FindModelDir() 的搜索路径里。
#pragma once

#include "framework.h"
#include <winhttp.h>

#include <atomic>
#include <mutex>
#include <thread>

class CModelInstaller
{
public:
    enum class State
    {
        Idle,          // 没跑过 / 上一次已完成并复位
        Connecting,    // 正在建连接
        Downloading,
        Extracting,    // tar 解压安装
        Done,
        Failed,
        Canceled,
    };

    ~CModelInstaller();

    // ---- 静态信息（打包/排障共用同一份定义）----
    static std::wstring ModelName();        // sherpa-onnx-streaming-zipformer-zh-xlarge-int8-2025-06-30
    static std::wstring DownloadUrl();      // TP_MODEL_URL 优先，否则官方 release（tar.bz2 兜底通道）
    static std::wstring InstallRoot();      // …\models
    static std::wstring InstallDir();       // …\models\<ModelName>
    static std::wstring PackagePath();      // …\models\_download\<ModelName>.tar.bz2.part
    static bool Installed();                // 模型文件齐不齐（复用 SherpaEngine 的判断）

    // ---- 生命周期 ----
    void Start(HWND notify, UINT msg);      // 幂等：已在跑直接返回
    void Cancel();                          // 请求取消（保留 .part，下次续传）
    void Wait();                            // 等线程退出（析构 / 退出程序时用）

    bool  Running() const { return m_running; }
    State StateNow() const { return m_state; }
    int   Percent() const { return m_percent; }
    std::wstring Status();                  // 当前进度文本（线程安全）
    bool  Failed() const { return m_state == State::Failed; }
    bool  Cancelled() const { return m_state == State::Canceled; }

private:
    void ThreadMain();
    bool DownloadFiles(std::wstring& err);   // 首选：从镜像逐个文件取（不用 tar）
    bool DownloadOneFile(const std::wstring& url, const std::wstring& dest,
                         const std::wstring& name, ULONGLONG expect,
                         ULONGLONG doneBefore, ULONGLONG totalBytes, std::wstring& err);
    bool DownloadTo(std::wstring& err);      // 兜底：整包 tar.bz2（TP_MODEL_URL 自定义源走这里）
    bool ExtractTo(std::wstring& err);
    void SetProgress(int percent, const std::wstring& text);
    void PostNotify();

    std::thread      m_thread;
    std::atomic<bool> m_running { false };
    std::atomic<bool> m_cancel { false };
    std::atomic<int>  m_percent { 0 };
    std::atomic<State> m_state { State::Idle };
    // 正在读的请求句柄：Cancel 时直接关掉，让阻塞中的 WinHttpReadData 立刻返回
    std::atomic<HINTERNET> m_activeReq { nullptr };
    mutable std::mutex m_mutex;
    std::wstring     m_status;
    HWND             m_notify = nullptr;
    UINT             m_msg = 0;
};
