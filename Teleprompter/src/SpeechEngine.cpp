// SpeechEngine.cpp
#include "SpeechEngine.h"

namespace
{
    std::wstring Hex(DWORD code)
    {
        wchar_t buf[32];
        swprintf_s(buf, L"0x%08X", code);
        return buf;
    }

    std::wstring TokenDescription(ISpObjectToken* token)
    {
        if (!token) return L"";
        LPWSTR desc = nullptr;
        std::wstring out;
        if (SUCCEEDED(SpGetDescription(token, &desc)) && desc)
        {
            out = desc;
            ::CoTaskMemFree(desc);
        }
        return out;
    }

    // SAPI 语言属性写的是不带前导零的十六进制（Language=804），两种都试一遍
    bool SelectRecognizerByLang(ISpRecognizer* reco, int langId)
    {
        if (!reco || langId == 0) return true;

        wchar_t attrShort[64], attrLong[64];
        swprintf_s(attrShort, L"Language=%x", (unsigned)langId);
        swprintf_s(attrLong, L"Language=%04x", (unsigned)langId);

        for (const wchar_t* attr : { attrShort, attrLong })
        {
            CComPtr<IEnumSpObjectTokens> pEnum;
            if (FAILED(SpEnumTokens(SPCAT_RECOGNIZERS, attr, nullptr, &pEnum)) || !pEnum)
                continue;

            CComPtr<ISpObjectToken> token;
            if (pEnum->Next(1, &token, nullptr) != S_OK || !token)
                continue;

            if (SUCCEEDED(reco->SetRecognizer(token)))
                return true;
        }
        return false;
    }
}

bool CSpeechEngine::StartOffline(const std::wstring& deviceId, ResultFn fn, std::wstring& err)
{
    m_sherpa = std::make_unique<CSherpaEngine>();
    if (!m_sherpa->Start(L"", &m_cap, deviceId, fn, err))
    {
        m_sherpa.reset();
        return false;
    }
    // 模型有 700MB，加载要几秒，名字先标成加载中，Poll 里就绪后改成正式名
    m_engineName = L"离线识别 · Zipformer 中文（模型加载中…）";
    return true;
}

bool CSpeechEngine::Start(int langId, const std::wstring& deviceId, ResultFn fn, std::wstring& err)
{
    Stop();
    m_error.clear();
    m_engineName.clear();
    m_inputName.clear();
    m_lastSoundTick = 0;

    // 自检标记：TP_SPEECH_WAV 用文件回灌，此时不该提示「没有收到音频」
    m_selfTest = false;
    {
        wchar_t wavPath[MAX_PATH] = { 0 };
        const DWORD n = ::GetEnvironmentVariableW(L"TP_SPEECH_WAV", wavPath, MAX_PATH);
        if (n > 0 && n < MAX_PATH && ::GetFileAttributesW(wavPath) != INVALID_FILE_ATTRIBUTES)
            m_selfTest = true;
    }

    // 优先走离线引擎：系统自带的 SAPI 中文听写错字太多，只当兜底
    if (m_backend != Backend::Sapi)
    {
        if (StartOffline(deviceId, fn, err))
        {
            m_running = true;
            return true;
        }
        if (m_backend == Backend::Sherpa) return false;   // 用户指定离线，失败直接报错
        err.clear();                                      // Auto：静默回退系统引擎
        m_engineName.clear();
    }

    m_fn = std::move(fn);
    if (InitWith(false, langId, deviceId))
    {
        m_running = true;
        return true;
    }
    m_shared = true;
    std::wstring firstErr = m_error;
    Stop();
    m_error.clear();

    if (InitWith(true, langId, deviceId))
    {
        m_running = true;
        if (!m_engineName.empty())
            m_engineName += L" · 共享模式";
        return true;
    }

    err = L"语音识别启动失败。\n\n本机识别器：" + firstErr;
    if (!m_error.empty())
        err += L"\n备用共享识别器：" + m_error;
    Stop();
    return false;
}

bool CSpeechEngine::InitWith(bool sharedRecognizer, int langId, const std::wstring& deviceId)
{
    HRESULT hr = S_OK;

    const CLSID& clsid = sharedRecognizer ? CLSID_SpSharedRecognizer : CLSID_SpInprocRecognizer;
    hr = m_reco.CoCreateInstance(clsid, nullptr, CLSCTX_ALL);
    if (FAILED(hr) || !m_reco)
    {
        m_error = L"无法创建识别引擎（系统可能未安装语音识别语言包），代码 " + Hex((DWORD)hr);
        return false;
    }

    if (!sharedRecognizer)
    {
        // 选定语言（失败不致命：沿用系统默认识别器）
        SelectRecognizerByLang(m_reco, langId);

        // 自检模式：TP_SPEECH_WAV=<wav 路径> 时用音频文件代替麦克风输入，
        // 方便在没有说话环境的情况下验证"识别 → 匹配 → 滚动"整条链路
        wchar_t wavPath[MAX_PATH] = { 0 };
        DWORD n = ::GetEnvironmentVariableW(L"TP_SPEECH_WAV", wavPath, MAX_PATH);
        if (n > 0 && n < MAX_PATH && ::GetFileAttributesW(wavPath) != INVALID_FILE_ATTRIBUTES)
        {
            hr = m_inStream.CoCreateInstance(CLSID_SpStream);
            if (SUCCEEDED(hr))
                hr = m_inStream->BindToFile(wavPath, SPFM_OPEN_READONLY, nullptr, nullptr, 0);
            if (SUCCEEDED(hr))
                hr = m_reco->SetInput(m_inStream, TRUE);
            if (FAILED(hr))
            {
                m_error = L"无法打开自检音频文件，代码 " + Hex((DWORD)hr);
                return false;
            }
        }
        else
        {
            // 自己用 WASAPI 采集：可以指定设备、能与会议软件并存，
            // 也能避开「SAPI 只认默认录音设备，而会议软件用默认通信设备」的坑
            std::wstring capErr;
            bool captured = m_cap.Start(deviceId, capErr);
            if (captured)
            {
                m_pullStream.Attach(new CAudioPullStream(&m_cap));
                hr = m_reco->SetInput(m_pullStream, TRUE);
                if (SUCCEEDED(hr))
                {
                    m_inputName = m_cap.DeviceName();
                }
                else
                {
                    m_pullStream.Release();
                    m_cap.Stop();
                    captured = false;
                    capErr = L"识别器不接受音频流，代码 0x" + std::to_wstring((unsigned)hr);
                }
            }

            if (!captured)
            {
                // 退回 SAPI 默认输入（老系统兜底）
                hr = m_reco->SetInput(nullptr, TRUE);
                if (FAILED(hr))
                {
                    m_error = (capErr.empty() ? L"无法打开麦克风" : capErr) +
                              L"；SAPI 默认输入也不可用，代码 0x" + std::to_wstring((unsigned)hr);
                    return false;
                }
                m_inputName = L"系统默认输入（" + (capErr.empty() ? L"自采集不可用" : capErr) + L"）";
            }
        }
    }

    // 关键：in-proc 识别器必须用 CreateRecoContext，QueryInterface 拿不到
    hr = m_reco->CreateRecoContext(&m_ctx);
    if (FAILED(hr) || !m_ctx)
    {
        m_error = L"无法创建识别上下文，代码 " + Hex((DWORD)hr);
        return false;
    }

    hr = m_ctx->SetNotifyWin32Event();
    if (FAILED(hr))
    {
        m_error = L"无法设置识别事件通知，代码 " + Hex((DWORD)hr);
        return false;
    }

    m_evt = m_ctx->GetNotifyEventHandle();
    if (!m_evt)
    {
        m_error = L"无法获取识别事件句柄";
        return false;
    }

    const ULONGLONG interest = SPFEI(SPEI_RECOGNITION) | SPFEI(SPEI_HYPOTHESIS) |
                               SPFEI(SPEI_SOUND_START) | SPFEI(SPEI_SOUND_END) |
                               SPFEI(SPEI_FALSE_RECOGNITION);
    m_ctx->SetInterest(interest, interest);

    hr = m_ctx->CreateGrammar(1, &m_grammar);
    if (FAILED(hr) || !m_grammar)
    {
        m_error = L"无法创建听写语法，代码 " + Hex((DWORD)hr);
        return false;
    }

    // 只支持静态听写语法的引擎（中文桌面引擎就是如此）
    hr = m_grammar->LoadDictation(nullptr, SPLO_STATIC);
    if (FAILED(hr))
        hr = m_grammar->LoadDictation(nullptr, SPLO_DYNAMIC);
    if (FAILED(hr))
    {
        m_error = L"加载听写语法失败（该语言可能不支持连续听写），代码 " + Hex((DWORD)hr);
        return false;
    }

    hr = m_grammar->SetDictationState(SPRS_ACTIVE);
    if (FAILED(hr))
    {
        m_error = L"启动听写状态失败，代码 " + Hex((DWORD)hr);
        return false;
    }

    m_ctx->SetContextState(SPCS_ENABLED);
    m_reco->SetRecoState(SPRST_ACTIVE);

    CComPtr<ISpObjectToken> token;
    if (SUCCEEDED(m_reco->GetRecognizer(&token)) && token)
        m_engineName = TokenDescription(token);
    if (m_engineName.empty())
        m_engineName = sharedRecognizer ? L"系统共享识别器" : L"系统默认识别引擎";

    return true;
}

void CSpeechEngine::Stop()
{
    m_running = false;
    m_evt = nullptr;   // 句柄属于 context，不需要关闭

    // 离线引擎先停（它会 join 后台加载线程、关麦克风）
    if (m_sherpa)
    {
        m_sherpa->Stop();
        m_sherpa.reset();
    }

    if (m_grammar)
    {
        m_grammar->SetDictationState(SPRS_INACTIVE);
        m_grammar.Release();
    }
    if (m_ctx)
    {
        m_ctx->SetContextState(SPCS_DISABLED);
        m_ctx.Release();
    }

    // 先停采集：让可能阻塞在 Read 里的识别线程尽快返回，再释放识别器
    m_cap.Stop();
    if (m_pullStream) m_pullStream.Release();

    if (m_reco)
    {
        m_reco->SetRecoState(SPRST_INACTIVE);
        m_reco.Release();
    }
    if (m_inStream)
    {
        m_inStream->Close();
        m_inStream.Release();
    }
    m_shared = false;
}

void CSpeechEngine::Poll()
{
    // 离线引擎：自己从采集缓冲取 PCM 喂给 sherpa
    if (m_sherpa)
    {
        m_sherpa->Poll();
        m_lastSoundTick = m_sherpa->LastSoundTick();

        if (m_sherpa->IsReady())
        {
            if (m_inputName.empty())
                m_inputName = m_sherpa->IsFileMode() ? L"自检音频文件" : m_cap.DeviceName();
            if (m_engineName.find(L"加载中") != std::wstring::npos)
                m_engineName = L"离线识别 · Zipformer 中文（高精度）";
        }
        else if (m_sherpa->HasError())
        {
            m_error = m_sherpa->LastError();
            m_engineName = L"离线识别（失败）";
        }
        return;
    }

    if (!m_ctx || !m_evt) return;
    if (::WaitForSingleObject(m_evt, 0) != WAIT_OBJECT_0) return;
    HandleEvent();
}

void CSpeechEngine::HandleEvent()
{
    CSpEvent evt;
    while (evt.GetFrom(m_ctx) == S_OK)
    {
        switch (evt.eEventId)
        {
        case SPEI_SOUND_START:
            m_lastSoundTick = ::GetTickCount();
            break;

        case SPEI_RECOGNITION:
        case SPEI_HYPOTHESIS:
        {
            ISpRecoResult* pResult = evt.RecoResult();
            if (!pResult) break;

            LPWSTR text = nullptr;
            if (SUCCEEDED(pResult->GetText(SP_GETWHOLEPHRASE, SP_GETWHOLEPHRASE, TRUE, &text, nullptr)) && text)
            {
                std::wstring s(text);
                ::CoTaskMemFree(text);
                if (m_fn && !s.empty())
                    m_fn(s, evt.eEventId == SPEI_RECOGNITION);
            }
            break;
        }

        default:
            break;
        }
    }
}
