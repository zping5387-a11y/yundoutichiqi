// sapitest4.cpp - 用 Win32 事件通知 + 16kHz 音频验证听写识别
#include <windows.h>
#include <sapi.h>
#include <sphelper.h>
#include <atlbase.h>
#include <cstdio>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "sapi.lib")

static bool SynthWav(const wchar_t* path, const wchar_t* text, SPSTREAMFORMAT f)
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
    CSpStreamFormat fmt(f, &hr);
    if (FAILED(st->BindToFile(path, SPFM_CREATE_ALWAYS, &fmt.FormatId(), fmt.WaveFormatExPtr(), 0))) return false;
    voice->SetOutput(st, FALSE);
    ULONG n = 0;
    voice->Speak(text, SPF_DEFAULT, &n);
    voice->WaitUntilDone(10000);
    st->Close();
    return true;
}

static int Run(const wchar_t* wav, const wchar_t* tag)
{
    wprintf(L"\n===== %s =====\n", tag);
    CComPtr<ISpRecognizer> reco;
    if (FAILED(reco.CoCreateInstance(CLSID_SpInprocRecognizer, nullptr, CLSCTX_ALL))) return 0;

    CComPtr<ISpStream> in;
    in.CoCreateInstance(CLSID_SpStream);
    if (FAILED(in->BindToFile(wav, SPFM_OPEN_READONLY, nullptr, nullptr, 0))) { wprintf(L"  bind fail\n"); return 0; }
    if (FAILED(reco->SetInput(in, TRUE))) { wprintf(L"  SetInput fail\n"); return 0; }

    CComPtr<ISpRecoContext> ctx;
    if (FAILED(reco->CreateRecoContext(&ctx))) { wprintf(L"  ctx fail\n"); return 0; }
    ctx->SetNotifyWin32Event();
    HANDLE hEvt = ctx->GetNotifyEventHandle();
    wprintf(L"  notify handle=%p\n", hEvt);

    const ULONGLONG interest = SPFEI(SPEI_RECOGNITION) | SPFEI(SPEI_HYPOTHESIS) |
                               SPFEI(SPEI_SOUND_START) | SPFEI(SPEI_SOUND_END) | SPFEI(SPEI_FALSE_RECOGNITION);
    ctx->SetInterest(interest, interest);

    CComPtr<ISpRecoGrammar> g;
    ctx->CreateGrammar(1, &g);
    HRESULT hr = g->LoadDictation(nullptr, SPLO_STATIC);
    wprintf(L"  LoadDictation=0x%08x\n", hr);
    hr = g->SetDictationState(SPRS_ACTIVE);
    wprintf(L"  SetDictationState=0x%08x\n", hr);
    hr = ctx->SetContextState(SPCS_ENABLED);
    wprintf(L"  SetContextState=0x%08x\n", hr);
    hr = reco->SetRecoState(SPRST_ACTIVE);
    wprintf(L"  SetRecoState=0x%08x\n", hr);

    SPRECOSTATE st = SPRST_INACTIVE;
    reco->GetRecoState(&st);
    wprintf(L"  GetRecoState=%d (2=ACTIVE)\n", (int)st);

    int total = 0;
    DWORD end = GetTickCount() + 12000;
    while (GetTickCount() < end)
    {
        DWORD w = WaitForSingleObject(hEvt, 300);
        if (w != WAIT_OBJECT_0) continue;
        CSpEvent ev;
        while (ev.GetFrom(ctx) == S_OK)
        {
            ++total;
            wprintf(L"  <event id=%d>\n", ev.eEventId);
            if (ev.eEventId == SPEI_RECOGNITION || ev.eEventId == SPEI_HYPOTHESIS)
            {
                LPWSTR t = nullptr;
                if (ev.RecoResult() && SUCCEEDED(ev.RecoResult()->GetText(SP_GETWHOLEPHRASE, SP_GETWHOLEPHRASE, TRUE, &t, nullptr)) && t)
                {
                    wprintf(L"     TEXT: %s\n", t);
                    CoTaskMemFree(t);
                }
            }
        }
    }
    wprintf(L"  total events=%d\n", total);
    return total;
}

int wmain()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SynthWav(L"build\\tts16k.wav", L"同学们，上课！今天我们来学习浮力。", SPSF_16kHz16BitMono);
    SynthWav(L"build\\tts22k.wav", L"同学们，上课！今天我们来学习浮力。", SPSF_22kHz16BitMono);

    int a = Run(L"build\\tts16k.wav", L"16kHz wav -> dictation");
    int b = Run(L"build\\tts22k.wav", L"22kHz wav -> dictation");
    wprintf(L"\n结果 16k=%d 22k=%d\n", a, b);
    return 0;
}
