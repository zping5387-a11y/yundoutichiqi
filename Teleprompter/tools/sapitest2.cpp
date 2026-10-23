// sapitest2.cpp - 找出可用的听写识别组合
#include <windows.h>
#include <sapi.h>
#include <sphelper.h>
#include <atlbase.h>
#include <cstdio>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "sapi.lib")

static void Show(const wchar_t* tag, HRESULT hr) { wprintf(L"   %-46s hr=0x%08x\n", tag, hr); }

// 返回 1 表示听写启动成功
static int TryInproc(bool useExplicitAudio, bool setRecognizerFirst, SPLOADOPTIONS opt, bool activateFirst)
{
    CComPtr<ISpRecognizer> reco;
    HRESULT hr = reco.CoCreateInstance(CLSID_SpInprocRecognizer, nullptr, CLSCTX_ALL);
    Show(L"CoCreateInstance(Inproc)", hr);
    if (FAILED(hr)) return 0;

    if (setRecognizerFirst)
    {
        CComPtr<IEnumSpObjectTokens> en;
        hr = SpEnumTokens(SPCAT_RECOGNIZERS, L"Language=804", nullptr, &en);
        CComPtr<ISpObjectToken> tok;
        if (en) en->Next(1, &tok, nullptr);
        if (tok) Show(L"SetRecognizer", reco->SetRecognizer(tok));
    }

    if (useExplicitAudio)
    {
        CComPtr<ISpAudio> audio;
        hr = SpCreateDefaultObjectFromCategoryId(SPCAT_AUDIOIN, &audio);
        Show(L"SpCreateDefaultObjectFromCategoryId(AUDIOIN)", hr);
        if (audio)
        {
            GUID fmtId = GUID_NULL;
            WAVEFORMATEX* wf = nullptr;
            Show(L"  audio->GetFormat", audio->GetFormat(&fmtId, &wf));
            if (wf)
            {
                wprintf(L"     fmt: %u Hz, %u ch, %u bits\n", wf->nSamplesPerSec, wf->nChannels, wf->wBitsPerSample);
                CoTaskMemFree(wf);
            }
            Show(L"SetInput(audio,TRUE)", reco->SetInput(audio, TRUE));
        }
    }
    else
    {
        Show(L"SetInput(NULL,TRUE)", reco->SetInput(nullptr, TRUE));
    }

    if (activateFirst) Show(L"SetRecoState(ACTIVE)", reco->SetRecoState(SPRST_ACTIVE));

    CComPtr<ISpRecoContext> ctx;
    Show(L"CreateRecoContext", reco->CreateRecoContext(&ctx));
    if (!ctx) return 0;

    CComPtr<ISpRecoGrammar> g;
    Show(L"CreateGrammar", ctx->CreateGrammar(1, &g));
    if (!g) return 0;

    hr = g->LoadDictation(nullptr, opt);
    Show(opt == SPLO_DYNAMIC ? L"LoadDictation(NULL, DYNAMIC)" : L"LoadDictation(NULL, STATIC)", hr);
    if (FAILED(hr))
    {
        hr = g->LoadDictation(L"", opt);
        Show(L"LoadDictation(\"\", ...)", hr);
    }
    if (FAILED(hr)) return 0;

    Show(L"SetDictationState(ACTIVE)", g->SetDictationState(SPRS_ACTIVE));
    Show(L"SetContextState(ENABLED)", ctx->SetContextState(SPCS_ENABLED));
    Show(L"SetRecoState(ACTIVE)", reco->SetRecoState(SPRST_ACTIVE));
    return 1;
}

static int TryShared()
{
    CComPtr<ISpRecognizer> reco;
    HRESULT hr = reco.CoCreateInstance(CLSID_SpSharedRecognizer, nullptr, CLSCTX_ALL);
    Show(L"CoCreateInstance(Shared)", hr);
    if (FAILED(hr)) return 0;

    CComPtr<ISpRecoContext> ctx;
    Show(L"CreateRecoContext", reco->CreateRecoContext(&ctx));
    if (!ctx) return 0;

    CComPtr<ISpRecoGrammar> g;
    Show(L"CreateGrammar", ctx->CreateGrammar(1, &g));
    if (!g) return 0;

    hr = g->LoadDictation(nullptr, SPLO_STATIC);
    Show(L"LoadDictation(NULL, STATIC)", hr);
    if (FAILED(hr)) return 0;

    Show(L"SetDictationState(ACTIVE)", g->SetDictationState(SPRS_ACTIVE));
    Show(L"SetContextState(ENABLED)", ctx->SetContextState(SPCS_ENABLED));
    return 1;
}

int wmain()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wprintf(L"exe=%s\n\n", exePath);

    wprintf(L"[1] Inproc + SetInput(NULL) + DYNAMIC\n");
    int r1 = TryInproc(false, false, SPLO_DYNAMIC, false);

    wprintf(L"\n[2] Inproc + 显式音频对象 + DYNAMIC\n");
    int r2 = TryInproc(true, false, SPLO_DYNAMIC, false);

    wprintf(L"\n[3] Inproc + 先 SetRecognizer(Language=804) + DYNAMIC\n");
    int r3 = TryInproc(false, true, SPLO_DYNAMIC, false);

    wprintf(L"\n[4] Inproc + 先激活 + STATIC\n");
    int r4 = TryInproc(false, false, SPLO_STATIC, true);

    wprintf(L"\n[5] Shared recognizer + STATIC\n");
    int r5 = TryShared();

    wprintf(L"\n结果: [1]=%d [2]=%d [3]=%d [4]=%d [5]=%d\n", r1, r2, r3, r4, r5);
    return 0;
}
