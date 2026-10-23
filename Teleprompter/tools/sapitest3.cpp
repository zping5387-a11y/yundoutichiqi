// sapitest3.cpp - 端到端验证：TTS 合成中文语音 -> 写入 wav -> 喂给识别器 -> 打印识别结果
#include <windows.h>
#include <sapi.h>
#include <sphelper.h>
#include <atlbase.h>
#include <cstdio>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "sapi.lib")

static const UINT WM_RECO = WM_APP + 7;

static void Pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    while (GetTickCount() < end)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(20);
    }
}

static bool SynthWav(const wchar_t* path, const wchar_t* text)
{
    CComPtr<ISpVoice> voice;
    if (FAILED(voice.CoCreateInstance(CLSID_SpVoice))) { wprintf(L"  voice fail\n"); return false; }

    CComPtr<ISpObjectToken> zhVoice;
    CComPtr<IEnumSpObjectTokens> vEnum;
    if (SUCCEEDED(SpEnumTokens(SPCAT_VOICES, L"Language=804", nullptr, &vEnum)) && vEnum)
        vEnum->Next(1, &zhVoice, nullptr);
    if (zhVoice) voice->SetVoice(zhVoice);

    CComPtr<ISpStream> stream;
    if (FAILED(stream.CoCreateInstance(CLSID_SpStream))) return false;
    HRESULT hrf = S_OK;
    CSpStreamFormat fmt(SPSF_22kHz16BitMono, &hrf);
    if (FAILED(stream->BindToFile(path, SPFM_CREATE_ALWAYS, &fmt.FormatId(), fmt.WaveFormatExPtr(), 0)))
    {
        wprintf(L"  bind fail\n");
        return false;
    }

    voice->SetOutput(stream, FALSE);
    ULONG spoken = 0;
    voice->Speak(text, SPF_DEFAULT, &spoken);
    voice->WaitUntilDone(8000);
    stream->Close();
    return true;
}

int wmain()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    wprintf(L"[voices]\n");
    CComPtr<IEnumSpObjectTokens> vEnum;
    if (SUCCEEDED(SpEnumTokens(SPCAT_VOICES, nullptr, nullptr, &vEnum)) && vEnum)
    {
        ULONG n = 0; vEnum->GetCount(&n);
        for (ULONG i = 0; i < n; ++i)
        {
            CComPtr<ISpObjectToken> t; vEnum->Item(i, &t);
            LPWSTR d = nullptr; SpGetDescription(t, &d);
            wprintf(L"  %u: %s\n", i, d ? d : L"?");
            if (d) CoTaskMemFree(d);
        }
    }

    const wchar_t* wav = L"build\\test_tts.wav";
    wprintf(L"\n[synth] -> %s\n", wav);
    if (!SynthWav(wav, L"同学们，上课！今天我们来学习浮力。"))
    {
        wprintf(L"  合成失败\n");
        return 1;
    }

    wprintf(L"\n[recognize from wav]\n");
    CComPtr<ISpRecognizer> reco;
    HRESULT hr = reco.CoCreateInstance(CLSID_SpInprocRecognizer, nullptr, CLSCTX_ALL);
    wprintf(L"  CoCreateInstance hr=0x%08x\n", hr);

    CComPtr<ISpStream> in;
    in.CoCreateInstance(CLSID_SpStream);
    CSpStreamFormat fmt;
    hr = in->BindToFile(wav, SPFM_OPEN_READONLY, nullptr, nullptr, 0);
    wprintf(L"  bind wav hr=0x%08x\n", hr);
    hr = reco->SetInput(in, TRUE);
    wprintf(L"  SetInput(wav) hr=0x%08x\n", hr);

    CComPtr<ISpRecoContext> ctx;
    hr = reco->CreateRecoContext(&ctx);
    wprintf(L"  CreateRecoContext hr=0x%08x\n", hr);

    HWND hwnd = CreateWindowExW(0, L"STATIC", L"x", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    ctx->SetNotifyWindowMessage(hwnd, WM_RECO, 0, 0);
    const ULONGLONG interest = SPFEI(SPEI_RECOGNITION) | SPFEI(SPEI_HYPOTHESIS) |
                               SPFEI(SPEI_SOUND_START) | SPFEI(SPEI_SOUND_END);
    ctx->SetInterest(interest, interest);

    CComPtr<ISpRecoGrammar> g;
    ctx->CreateGrammar(1, &g);
    hr = g->LoadDictation(nullptr, SPLO_STATIC);
    wprintf(L"  LoadDictation(STATIC) hr=0x%08x\n", hr);
    hr = g->SetDictationState(SPRS_ACTIVE);
    wprintf(L"  SetDictationState hr=0x%08x\n", hr);
    hr = ctx->SetContextState(SPCS_ENABLED);
    wprintf(L"  SetContextState hr=0x%08x\n", hr);
    hr = reco->SetRecoState(SPRST_ACTIVE);
    wprintf(L"  SetRecoState hr=0x%08x\n", hr);

    wprintf(L"\n[events]\n");
    DWORD end = GetTickCount() + 8000;
    int n = 0;
    while (GetTickCount() < end)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_RECO)
            {
                CSpEvent ev;
                while (ev.GetFrom(ctx) == S_OK)
                {
                    ++n;
                    const wchar_t* name = (ev.eEventId == SPEI_RECOGNITION) ? L"RECOGNITION"
                                        : (ev.eEventId == SPEI_HYPOTHESIS) ? L"HYPOTHESIS"
                                        : (ev.eEventId == SPEI_SOUND_START) ? L"SOUND_START"
                                        : (ev.eEventId == SPEI_SOUND_END) ? L"SOUND_END" : L"other";
                    wprintf(L"  event %s (id=%d)\n", name, ev.eEventId);
                    if (ev.eEventId == SPEI_RECOGNITION || ev.eEventId == SPEI_HYPOTHESIS)
                    {
                        LPWSTR t = nullptr;
                        if (ev.RecoResult() && SUCCEEDED(ev.RecoResult()->GetText(SP_GETWHOLEPHRASE, SP_GETWHOLEPHRASE, TRUE, &t, nullptr)) && t)
                        {
                            wprintf(L"     text = %s\n", t);
                            CoTaskMemFree(t);
                        }
                    }
                }
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(20);
    }
    wprintf(L"\n[total events] %d\n", n);
    return 0;
}
