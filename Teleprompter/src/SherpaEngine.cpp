// SherpaEngine.cpp
#include "SherpaEngine.h"
#include <shlobj.h>

#pragma comment(lib, "shell32.lib")

namespace
{
    std::string WideToUtf8(const std::wstring& w)
    {
        if (w.empty()) return std::string();
        const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                            nullptr, 0, nullptr, nullptr);
        std::string s((size_t)n, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                              &s[0], n, nullptr, nullptr);
        return s;
    }

    std::wstring Utf8ToWide(const char* p)
    {
        if (!p || !*p) return std::wstring();
        const int n = ::MultiByteToWideChar(CP_UTF8, 0, p, -1, nullptr, 0);
        if (n <= 1) return std::wstring();
        std::wstring w((size_t)(n - 1), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, p, -1, &w[0], n);
        return w;
    }

    std::wstring ExeDir()
    {
        wchar_t buf[MAX_PATH * 2] = { 0 };
        ::GetModuleFileNameW(nullptr, buf, ARRAYSIZE(buf));
        std::wstring p(buf);
        const size_t s = p.find_last_of(L"\\/");
        return (s == std::wstring::npos) ? L"." : p.substr(0, s);
    }

    std::wstring FullPath(const std::wstring& p)
    {
        wchar_t buf[MAX_PATH * 2] = { 0 };
        if (::GetFullPathNameW(p.c_str(), ARRAYSIZE(buf), buf, nullptr) == 0)
            return p;
        return buf;
    }

    bool FileExists(const std::wstring& p)
    {
        return !p.empty() && ::GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    bool IsModelDir(const std::wstring& dir)
    {
        return FileExists(dir + L"\\encoder.int8.onnx") &&
               FileExists(dir + L"\\decoder.onnx") &&
               FileExists(dir + L"\\joiner.int8.onnx") &&
               FileExists(dir + L"\\tokens.txt");
    }

    // 只支持 16k/16bit 的 WAV（SAPI TTS 默认就是这个格式）
    bool LoadWav16k(const std::wstring& path, std::vector<float>& out, std::wstring& err)
    {
        HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                 OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) { err = L"打不开音频文件：" + path; return false; }

        LARGE_INTEGER sz{};
        ::GetFileSizeEx(h, &sz);
        if (sz.QuadPart <= 44 || sz.QuadPart > 200LL * 1024 * 1024)
        {
            ::CloseHandle(h);
            err = L"音频文件大小异常";
            return false;
        }

        std::vector<BYTE> buf((size_t)sz.QuadPart);
        DWORD read = 0;
        ::ReadFile(h, buf.data(), (DWORD)buf.size(), &read, nullptr);
        ::CloseHandle(h);
        buf.resize(read);

        if (buf.size() < 44 || memcmp(buf.data(), "RIFF", 4) != 0 || memcmp(buf.data() + 8, "WAVE", 4) != 0)
        {
            err = L"不是标准 WAV 文件";
            return false;
        }

        WORD channels = 1, bits = 0;
        DWORD rate = 0;
        const BYTE* data = nullptr;
        DWORD dataLen = 0;

        size_t p = 12;
        while (p + 8 <= buf.size())
        {
            DWORD len = 0;
            memcpy(&len, &buf[p + 4], 4);
            const BYTE* body = &buf[p + 8];
            if (p + 8 + (size_t)len > buf.size()) break;

            if (memcmp(&buf[p], "fmt ", 4) == 0 && len >= 16)
            {
                memcpy(&channels, body + 2, 2);
                memcpy(&rate, body + 4, 4);
                memcpy(&bits, body + 14, 2);
            }
            else if (memcmp(&buf[p], "data", 4) == 0)
            {
                data = body;
                dataLen = len;
            }
            p += 8 + len + (len & 1);
        }

        if (!data || !dataLen) { err = L"WAV 缺少音频数据"; return false; }
        if (rate != 16000 || bits != 16)
        {
            err = L"自检音频需为 16kHz/16bit（当前 " + std::to_wstring(rate) + L"Hz/" +
                  std::to_wstring(bits) + L"bit）";
            return false;
        }

        const int ch = (channels == 0) ? 1 : (int)channels;
        const size_t frames = dataLen / (2 * (size_t)ch);
        out.resize(frames);
        for (size_t i = 0; i < frames; ++i)
        {
            int v = 0;
            for (int c = 0; c < ch; ++c)
            {
                short s = 0;
                memcpy(&s, data + (i * ch + c) * 2, 2);
                v += s;
            }
            out[i] = (float)(v / ch) / 32768.0f;
        }
        return true;
    }
}

std::wstring CSherpaEngine::ModelFolderName()
{
    return std::wstring(L"sherpa-onnx-streaming-zipformer-zh-xlarge-int8-2025-06-30");
}

std::wstring CSherpaEngine::FindModelDir()
{
    // 允许用环境变量指定（方便换模型 / 排障）
    wchar_t env[MAX_PATH * 2] = { 0 };
    if (::GetEnvironmentVariableW(L"TP_ASR_MODEL", env, ARRAYSIZE(env)) > 0)
    {
        const std::wstring d = FullPath(env);
        if (IsModelDir(d)) return d;
    }

    const std::wstring exe = ExeDir();
    const std::wstring name = ModelFolderName();

    // exe 在 build\ 下，模型在工程根目录的 models\ 里
    const std::wstring candidates[] = {
        exe + L"\\models\\" + name,
        exe + L"\\..\\models\\" + name,
        exe + L"\\..\\..\\models\\" + name,
        exe + L"\\..\\..\\..\\models\\" + name,
    };
    for (const auto& c : candidates)
    {
        const std::wstring d = FullPath(c);
        if (IsModelDir(d)) return d;
    }

    // 程序内下载的模型：exe 目录不可写时装到 %APPDATA%\TeacherPrompter\models
    // （与 CModelInstaller::InstallRoot 保持一致）
    wchar_t appdata[MAX_PATH] = { 0 };
    if (SUCCEEDED(::SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata)))
    {
        const std::wstring d = std::wstring(appdata) + L"\\TeacherPrompter\\models\\" + name;
        if (IsModelDir(d)) return d;
    }
    return std::wstring();
}

bool CSherpaEngine::Start(const std::wstring& modelDir, CAudioCapture* cap,
                          const std::wstring& deviceId, ResultFn fn, std::wstring& err)
{
    Stop();

    m_cap = cap;
    m_fn = std::move(fn);
    m_deviceId = deviceId;
    m_lastText.clear();
    m_lastSoundTick = 0;
    {
        std::lock_guard<std::mutex> lock(m_errMutex);
        m_errText.clear();
    }

    const std::wstring dir = modelDir.empty() ? FindModelDir() : FullPath(modelDir);
    if (dir.empty())
    {
        err = L"离线语音模型还没下载。请到「语音」页，点状态行的「下载语音模型」；"
              L"也可以把 models\\" + std::wstring(ModelFolderName()) + L" 整个目录拷到程序旁边。";
        return false;
    }
    if (!IsModelDir(dir))
    {
        err = L"离线语音模型文件不完整（缺 encoder/decoder/joiner/tokens）：" + dir;
        return false;
    }

    m_modelDir = dir;
    m_stopFlag = false;
    m_failed = false;
    m_ready = false;
    m_active = false;
    m_loading = true;

    // 自检模式：用 WAV 文件回灌，跟真人说话走同一条链路
    m_fileMode = false;
    m_wavSamples.clear();
    m_wavPos = 0;
    m_lastFeedTick = 0;

    wchar_t wavPath[MAX_PATH * 2] = { 0 };
    if (::GetEnvironmentVariableW(L"TP_SPEECH_WAV", wavPath, ARRAYSIZE(wavPath)) > 0)
    {
        std::wstring werr;
        if (LoadWav16k(wavPath, m_wavSamples, werr)) m_fileMode = true;
    }

    m_thread = std::thread(&CSherpaEngine::LoadThread, this);
    return true;
}

void CSherpaEngine::LoadThread()
{
    const std::string enc    = WideToUtf8(m_modelDir + L"\\encoder.int8.onnx");
    const std::string dec    = WideToUtf8(m_modelDir + L"\\decoder.onnx");
    const std::string join   = WideToUtf8(m_modelDir + L"\\joiner.int8.onnx");
    const std::string tokens = WideToUtf8(m_modelDir + L"\\tokens.txt");

    SherpaOnnxOnlineRecognizerConfig cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.feat_config.sample_rate = 16000;
    cfg.feat_config.feature_dim = 80;

    cfg.model_config.transducer.encoder = enc.c_str();
    cfg.model_config.transducer.decoder = dec.c_str();
    cfg.model_config.transducer.joiner  = join.c_str();
    cfg.model_config.tokens             = tokens.c_str();
    cfg.model_config.provider           = "cpu";
    cfg.model_config.num_threads        = 4;
    cfg.model_config.debug              = 0;

    cfg.decoding_method      = "greedy_search";
    cfg.max_active_paths     = 4;
    cfg.enable_endpoint      = 1;
    // 讲课停顿很常见：句尾静音判得宽松一点，让一句话尽量长，匹配更准
    cfg.rule1_min_trailing_silence = 3.5f;
    cfg.rule2_min_trailing_silence = 2.0f;
    cfg.rule3_min_utterance_length = 30.0f;

    const SherpaOnnxOnlineRecognizer* reco = SherpaOnnxCreateOnlineRecognizer(&cfg);
    if (!reco)
    {
        std::lock_guard<std::mutex> lock(m_errMutex);
        m_errText = L"离线识别模型加载失败（文件损坏或内存不足）：" + m_modelDir;
        m_failed = true;
        m_loading = false;
        return;
    }

    if (m_stopFlag)
    {
        SherpaOnnxDestroyOnlineRecognizer(reco);
        m_loading = false;
        return;
    }

    m_reco = reco;
    m_ready = true;
}

void CSherpaEngine::Poll()
{
    if (m_loading)
    {
        if (!m_ready && !m_failed) return;   // 还在后台加载

        m_loading = false;
        if (m_failed || !m_reco) return;

        // 模型就绪，这才开始收音频（避免加载期间把环形缓冲灌满）
        if (!m_fileMode)
        {
            std::wstring capErr;
            if (!m_cap || !m_cap->Start(m_deviceId, capErr))
            {
                std::lock_guard<std::mutex> lock(m_errMutex);
                m_errText = capErr.empty() ? L"麦克风打开失败" : capErr;
                m_failed = true;
                ReleaseAll();
                return;
            }
        }

        m_stream = SherpaOnnxCreateOnlineStream(m_reco);
        if (!m_stream)
        {
            std::lock_guard<std::mutex> lock(m_errMutex);
            m_errText = L"无法创建离线识别流";
            m_failed = true;
            ReleaseAll();
            return;
        }

        m_active = true;
        return;
    }

    if (!m_active || !m_reco || !m_stream) return;

    if (m_fileMode)
    {
        // 自检：按真实时间速率喂文件，模拟边讲边识别
        const DWORD now = ::GetTickCount();
        if (m_lastFeedTick == 0) m_lastFeedTick = now;

        int want = (int)(now - m_lastFeedTick) * 16;   // 16kHz => 16 个样本/毫秒
        if (want > 16000) want = 16000;

        if (want > 0)
        {
            const size_t remain = (m_wavPos < m_wavSamples.size()) ? (m_wavSamples.size() - m_wavPos) : 0;
            const int n = (want > (int)remain) ? (int)remain : want;
            if (n > 0)
            {
                SherpaOnnxOnlineStreamAcceptWaveform(m_stream, 16000,
                                                     m_wavSamples.data() + m_wavPos, n);

                // 自检时也得有电平，否则界面会误报「没有收到音频」
                float peakA = 0.0f;
                for (int i = 0; i < n; ++i)
                {
                    const float a = fabsf(m_wavSamples[m_wavPos + i]);
                    if (a > peakA) peakA = a;
                }
                int lv = (int)(peakA * 32768.0f * 100.0f / 8000.0f);
                if (lv > 100) lv = 100;
                m_level = lv;

                m_wavPos += (size_t)n;
                m_lastSoundTick = now;
            }
            m_lastFeedTick = now;
        }
    }
    else if (m_cap)
    {
        // 一次 Timer 里把积压的音频追平（每块 100ms，最多 4 块）
        int16_t pcm[1600];
        for (int chunk = 0; chunk < 4; ++chunk)
        {
            const int bytes = m_cap->Read(pcm, (int)sizeof(pcm), 0);
            if (bytes < 2) break;

            const int n = bytes / 2;
            m_samples.resize((size_t)n);
            int peak = 0;
            for (int i = 0; i < n; ++i)
            {
                const int v = pcm[i];
                m_samples[(size_t)i] = (float)v / 32768.0f;
                const int a = (v < 0) ? -v : v;
                if (a > peak) peak = a;
            }
            if (peak > 260) m_lastSoundTick = ::GetTickCount();
            if (peak > 8000) peak = 8000;
            m_level = peak * 100 / 8000;

            SherpaOnnxOnlineStreamAcceptWaveform(m_stream, 16000, m_samples.data(), n);

            if (bytes < (int)sizeof(pcm)) break;   // 缓冲已读完
        }
    }

    int guard = 0;
    while (SherpaOnnxIsOnlineStreamReady(m_reco, m_stream) && guard++ < 64)
        SherpaOnnxDecodeOnlineStream(m_reco, m_stream);

    const SherpaOnnxOnlineRecognizerResult* r = SherpaOnnxGetOnlineStreamResult(m_reco, m_stream);
    if (r)
    {
        const std::wstring text = Utf8ToWide(r->text);
        SherpaOnnxDestroyOnlineRecognizerResult(r);

        if (text != m_lastText)
        {
            m_lastText = text;
            if (!text.empty()) Emit(text, false);
        }
    }

    if (SherpaOnnxOnlineStreamIsEndpoint(m_reco, m_stream))
    {
        if (!m_lastText.empty()) Emit(m_lastText, true);
        SherpaOnnxOnlineStreamReset(m_reco, m_stream);
        m_lastText.clear();
    }
}

void CSherpaEngine::Emit(const std::wstring& text, bool isFinal)
{
    if (m_fn && !text.empty()) m_fn(text, isFinal);
}

void CSherpaEngine::ReleaseAll()
{
    m_active = false;
    if (m_stream)
    {
        SherpaOnnxDestroyOnlineStream(m_stream);
        m_stream = nullptr;
    }
    if (m_reco)
    {
        SherpaOnnxDestroyOnlineRecognizer(m_reco);
        m_reco = nullptr;
    }
    m_lastText.clear();
}

void CSherpaEngine::Stop()
{
    m_stopFlag = true;
    m_active = false;

    if (m_thread.joinable()) m_thread.join();   // 等后台加载线程收尾
    m_loading = false;
    m_ready = false;

    if (m_cap) m_cap->Stop();
    ReleaseAll();

    m_cap = nullptr;
    m_fn = nullptr;
}

bool CSherpaEngine::HasError() const
{
    return m_failed.load();
}

std::wstring CSherpaEngine::LastError() const
{
    std::lock_guard<std::mutex> lock(m_errMutex);
    return m_errText;
}
