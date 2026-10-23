// sapitest.cpp - SAPI 环境诊断（控制台）
#include <windows.h>
#include <sapi.h>
#include <sphelper.h>
#include <atlbase.h>
#include <cstdio>
#include <string>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "sapi.lib")

static const UINT WM_RECO = WM_APP + 7;
static int g_events = 0;

static void PumpFor(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    while (GetTickCount() < end)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(20);
    }
}

int wmain()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    CComPtr<IEnumSpObjectTokens> pEnum;
    HRESULT hr = SpEnumTokens(SPCAT_RECOGNIZERS, nullptr, nullptr, &pEnum);
    wprintf(L"[enum] hr=0x%08x\n", hr);
    if (pEnum)
    {
        ULONG n = 0;
        pEnum->GetCount(&n);
        wprintf(L"[enum] count=%u\n", n);
        for (ULONG i = 0; i < n; ++i)
        {
            CComPtr<ISpObjectToken> tok;
            if (pEnum->Item(i, &tok) != S_OK) continue;
            LPWSTR id = nullptr, desc = nullptr;
            tok->GetId(&id);
            SpGetDescription(tok, &desc);
            wprintf(L"  [%u] id=%s\n      desc=%s\n", i, id ? id : L"?", desc ? desc : L"?");
            CComPtr<ISpDataKey> attrs;
            if (SUCCEEDED(tok->OpenKey(L"Attributes", &attrs)) && attrs)
            {
                LPWSTR lang = nullptr;
                if (SUCCEEDED(attrs->GetStringValue(L"Language", &lang)) && lang)
                    wprintf(L"      Language=%s\n", lang);
                if (lang) CoTaskMemFree(lang);
            }
            CoTaskMemFree(id);
            if (desc) CoTaskMemFree(desc);
        }
    }

    wprintf(L"\n[test] CLSID_SpInprocRecognizer\n");
    CComPtr<ISpRecognizer> reco;
    hr = reco.CoCreateInstance(CLSID_SpInprocRecognizer, nullptr, CLSCTX_ALL);
    wprintf(L"  CoCreateInstance hr=0x%08x\n", hr);
    if (!reco) { getchar(); return 1; }

    hr = reco->SetInput(nullptr, TRUE);
    wprintf(L"  SetInput(NULL,TRUE) hr=0x%08x\n", hr);

    CComPtr<ISpRecoContext> ctxQI;
    hr = reco->QueryInterface(IID_ISpRecoContext, (void**)&ctxQI);
    wprintf(L"  QI(ISpRecoContext) hr=0x%08x ptr=%p\n", hr, (void*)ctxQI.p);

    CComPtr<ISpRecoContext> ctxCC;
    hr = reco->CreateRecoContext(&ctxCC);
    wprintf(L"  CreateRecoContext hr=0x%08x ptr=%p\n", hr, (void*)ctxCC.p);

    CComPtr<ISpRecoContext> ctx = ctxCC ? ctxCC : ctxQI;
    if (!ctx) { wprintf(L"  !! no context\n"); getchar(); return 2; }

    HWND hwnd = CreateWindowExW(0, L"STATIC", L"diag", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    ctx->SetNotifyWindowMessage(hwnd, WM_RECO, 0, 0);
    const ULONGLONG interest = SPFEI(SPEI_RECOGNITION) | SPFEI(SPEI_HYPOTHESIS) |
                               SPFEI(SPEI_SOUND_START) | SPFEI(SPEI_SOUND_END) |
                               SPFEI(SPEI_FALSE_RECOGNITION);
    ctx->SetInterest(interest, interest);

    CComPtr<ISpRecoGrammar> grammar;
    hr = ctx->CreateGrammar(1, &grammar);
    wprintf(L"  CreateGrammar hr=0x%08x\n", hr);
    if (grammar)
    {
        hr = grammar->LoadDictation(nullptr, SPLO_DYNAMIC);
        wprintf(L"  LoadDictation hr=0x%08x\n", hr);
        hr = grammar->SetDictationState(SPRS_ACTIVE);
        wprintf(L"  SetDictationState hr=0x%08x\n", hr);
    }
    hr = ctx->SetContextState(SPCS_ENABLED);
    wprintf(L"  SetContextState hr=0x%08x\n", hr);
    hr = reco->SetRecoState(SPRST_ACTIVE);
    wprintf(L"  SetRecoState hr=0x%08x\n", hr);

    CComPtr<ISpObjectToken> cur;
    if (SUCCEEDED(reco->GetRecognizer(&cur)) && cur)
    {
        LPWSTR d = nullptr;
        SpGetDescription(cur, &d);
        wprintf(L"  active recognizer: %s\n", d ? d : L"?");
        if (d) CoTaskMemFree(d);
    }

    wprintf(L"\n[listen] 5 秒，请对着麦克风说两句话...\n");
    PumpFor(5000);

    MSG msg;
    while (PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_RECO)
        {
            CSpEvent ev;
            while (ev.GetFrom(ctx) == S_OK)
            {
                ++g_events;
                wprintf(L"  <event> id=%d\n", ev.eEventId);
                if (ev.eEventId == SPEI_RECOGNITION || ev.eEventId == SPEI_HYPOTHESIS)
                {
                    LPWSTR t = nullptr;
                    if (ev.RecoResult() &&
                        SUCCEEDED(ev.RecoResult()->GetText(SP_GETWHOLEPHRASE, SP_GETWHOLEPHRASE, TRUE, &t, nullptr)) && t)
                    {
                        wprintf(L"      text=%s\n", t);
                        CoTaskMemFree(t);
                    }
                }
            }
        }
    }

    wprintf(L"\n[result] events=%d\n", g_events);
    return 0;
}
