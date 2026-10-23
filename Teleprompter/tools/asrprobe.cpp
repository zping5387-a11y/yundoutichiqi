// asrprobe.cpp - 用 TTS 合成中文语音喂给听写引擎，观察本机中文识别的真实错误
#include <windows.h>
#include <sapi.h>
#include <sphelper.h>
#include <atlbase.h>
#include <cstdio>
#include <cstdarg>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "sapi.lib")

static const wchar_t* kText =
    L"同学们，上课！上一节课我们讨论了物体在液体中的沉浮条件，今天这节课，我们来学习浮力的大小究竟与什么有关。"
    L"先看一个生活中的现象：把一个空矿泉水瓶按进水里，手会感觉到一个向上的力。"
    L"于是我们得到阿基米德原理：浸在液体中的物体所受浮力，等于它排开的液体所受的重力。";

static FILE* g_log = nullptr;

static void Log(const wchar_t* fmt, ...)
{
    wchar_t buf[4096];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);

    char utf8[8192];
    int n = ::WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8, sizeof(utf8), nullptr, nullptr);
    if (n > 0) { fwrite(utf8, 1, (size_t)n - 1, g_log); fputc('\n', g_log); fflush(g_log); }
}

static bool SynthWav(const wchar_t* path, const wchar_t* text)
{
    CComPtr<ISpVoice> voice;
    if (FAILED(voice.CoCreateInstance(CLSID_SpVoice))) return false;
    CComPtr<ISpObjectToken> zh;
    CComPtr<IEnumSpObjectTokens> ve;
    if (SUCCEEDED(SpEnumTokens(SPCAT_VOICES, L"Language=804", nullptr, &ve)) && ve) ve->Next(1, &zh, nullptr);
    if (zh) voice->SetVoice(zh);

    CComPtr<ISpStream> st;
    st.CoCreateInstance(CLSID_SpStream);
    HRESULT hr = S_OK;
    CSpStreamFormat fmt(SPSF_16kHz16BitMono, &hr);
    if (FAILED(st->BindToFile(path, SPFM_CREATE_ALWAYS, &fmt.FormatId(), fmt.WaveFormatExPtr(), 0))) return false;
    voice->SetOutput(st, FALSE);
    ULONG n = 0;
    voice->Speak(text, SPF_DEFAULT, &n);
    voice->WaitUntilDone(20000);
    st->Close();
    return true;
}

int wmain(int argc, wchar_t** argv)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    _wfopen_s(&g_log, L"build\\asrprobe.txt", L"wb");

    const wchar_t* wav = (argc > 1) ? argv[1] : L"build\\asrprobe.wav";
    if (!SynthWav(wav, kText)) { wprintf(L"TTS failed\n"); return 1; }
    Log(L"source: %s", kText);
    Log(L"");

    CComPtr<ISpRecognizer> reco;
    if (FAILED(reco.CoCreateInstance(CLSID_SpInprocRecognizer, nullptr, CLSCTX_ALL))) { wprintf(L"reco fail\n"); return 1; }

    CComPtr<ISpStream> in;
    in.CoCreateInstance(CLSID_SpStream);
    if (FAILED(in->BindToFile(wav, SPFM_OPEN_READONLY, nullptr, nullptr, 0))) { wprintf(L"bind fail\n"); return 1; }
    reco->SetInput(in, TRUE);

    CComPtr<ISpRecoContext> ctx;
    if (FAILED(reco->CreateRecoContext(&ctx))) { wprintf(L"ctx fail\n"); return 1; }
    ctx->SetNotifyWin32Event();
    HANDLE hEvt = ctx->GetNotifyEventHandle();

    const ULONGLONG interest = SPFEI(SPEI_RECOGNITION) | SPFEI(SPEI_SOUND_END) | SPFEI(SPEI_FALSE_RECOGNITION);
    ctx->SetInterest(interest, interest);

    CComPtr<ISpRecoGrammar> g;
    ctx->CreateGrammar(1, &g);
    g->LoadDictation(nullptr, SPLO_STATIC);
    g->SetDictationState(SPRS_ACTIVE);
    ctx->SetContextState(SPCS_ENABLED);
    reco->SetRecoState(SPRST_ACTIVE);

    DWORD end = GetTickCount() + 25000;
    while (GetTickCount() < end)
    {
        DWORD w = WaitForSingleObject(hEvt, 300);
        if (w != WAIT_OBJECT_0) continue;
        CSpEvent ev;
        while (ev.GetFrom(ctx) == S_OK)
        {
            if (ev.eEventId == SPEI_RECOGNITION)
            {
                LPWSTR t = nullptr;
                if (ev.RecoResult() && SUCCEEDED(ev.RecoResult()->GetText(SP_GETWHOLEPHRASE, SP_GETWHOLEPHRASE, TRUE, &t, nullptr)) && t)
                {
                    Log(L"RECO: %s", t);
                    CoTaskMemFree(t);
                }
            }
            else if (ev.eEventId == SPEI_SOUND_END)
            {
                Log(L"-- sound end --");
                end = min(end, GetTickCount() + 4000);
            }
        }
        if (GetTickCount() > end) break;
    }
    reco->SetRecoState(SPRST_INACTIVE);
    if (g_log) fclose(g_log);
    return 0;
}
