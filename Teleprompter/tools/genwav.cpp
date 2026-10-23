// genwav.cpp - 用中文 TTS 生成测试音频：genwav <out.wav> <text>
#include <windows.h>
#include <sapi.h>
#include <sphelper.h>
#include <atlbase.h>
#include <cstdio>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "sapi.lib")

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) { wprintf(L"usage: genwav out.wav text\n"); return 1; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    CComPtr<ISpVoice> voice;
    voice.CoCreateInstance(CLSID_SpVoice);
    CComPtr<IEnumSpObjectTokens> ve;
    CComPtr<ISpObjectToken> zh;
    if (SUCCEEDED(SpEnumTokens(SPCAT_VOICES, L"Language=804", nullptr, &ve)) && ve)
    {
        ve->Next(1, &zh, nullptr);
        if (zh) voice->SetVoice(zh);
    }

    CComPtr<ISpStream> st;
    st.CoCreateInstance(CLSID_SpStream);
    HRESULT hr = S_OK;
    CSpStreamFormat fmt(SPSF_16kHz16BitMono, &hr);
    if (FAILED(st->BindToFile(argv[1], SPFM_CREATE_ALWAYS, &fmt.FormatId(), fmt.WaveFormatExPtr(), 0)))
    {
        wprintf(L"bind fail 0x%08x\n", hr);
        return 2;
    }
    voice->SetOutput(st, FALSE);
    ULONG n = 0;
    voice->Speak(argv[2], SPF_DEFAULT, &n);
    voice->WaitUntilDone(30000);
    st->Close();
    wprintf(L"ok %s\n", argv[1]);
    return 0;
}
