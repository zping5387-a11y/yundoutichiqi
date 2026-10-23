// micprobe.cpp - 采集设备与 SAPI 输入探针
// 1) 枚举录音设备（含状态、默认设备、被占用情况）
// 2) WASAPI 共享模式采集 3 秒，报告电平（会议软件占用时共享模式能否取到数据）
// 3) 同时建立 SAPI in-proc 识别器，观察是否能打开默认输入、是否收到 SOUND_START
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <sapi.h>
#include <sphelper.h>
#include <atlbase.h>
#include <cstdio>
#include <string>
#include <cstdarg>
#include <cstdio>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "sapi.lib")

static const wchar_t* StateName(DWORD s)
{
    switch (s)
    {
    case DEVICE_STATE_ACTIVE:       return L"已启用";
    case DEVICE_STATE_DISABLED:     return L"已禁用";
    case DEVICE_STATE_NOTPRESENT:   return L"未插入";
    case DEVICE_STATE_UNPLUGGED:    return L"已拔出";
    default:                        return L"未知";
    }
}

static std::wstring FriendlyName(IMMDevice* dev)
{
    CComPtr<IPropertyStore> ps;
    if (FAILED(dev->OpenPropertyStore(STGM_READ, &ps)) || !ps) return L"";
    PROPVARIANT v; PropVariantInit(&v);
    std::wstring out;
    if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
        out = v.pwszVal;
    PropVariantClear(&v);
    return out;
}

static std::wstring DefaultId(IMMDeviceEnumerator* en, EDataFlow flow, ERole role)
{
    CComPtr<IMMDevice> dev;
    if (FAILED(en->GetDefaultAudioEndpoint(flow, role, &dev)) || !dev) return L"";
    LPWSTR id = nullptr;
    std::wstring out;
    if (SUCCEEDED(dev->GetId(&id)) && id) { out = id; CoTaskMemFree(id); }
    return out;
}

static FILE* g_log = nullptr;
static void Out(const wchar_t* fmt, ...)
{
    wchar_t buf[2048];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    char utf8[8192] = { 0 };
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8, sizeof(utf8), nullptr, nullptr);
    if (!g_log) g_log = _fsopen("build/micprobe.log", "wb", _SH_DENYNO);
    if (g_log) { fputs(utf8, g_log); fflush(g_log); }
}

int wmain()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);


    CComPtr<IMMDeviceEnumerator> en;
    if (FAILED(en.CoCreateInstance(__uuidof(MMDeviceEnumerator))))
    {
        Out(L"[FAIL] 无法创建设备枚举器\n");
        return 1;
    }

    const std::wstring defConsole = DefaultId(en, eCapture, eConsole);
    const std::wstring defComm    = DefaultId(en, eCapture, eCommunications);

    Out(L"=== 录音设备 ===\n");
    CComPtr<IMMDeviceCollection> coll;
    en->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE | DEVICE_STATE_DISABLED | DEVICE_STATE_UNPLUGGED, &coll);
    UINT n = 0;
    if (coll) coll->GetCount(&n);
    for (UINT i = 0; i < n; ++i)
    {
        CComPtr<IMMDevice> dev;
        if (FAILED(coll->Item(i, &dev)) || !dev) continue;
        LPWSTR id = nullptr;
        std::wstring sid;
        if (SUCCEEDED(dev->GetId(&id)) && id) { sid = id; CoTaskMemFree(id); }
        DWORD st = 0;
        dev->GetState(&st);
        Out(L"  [%u] %s  (%s)%s%s\n", i, FriendlyName(dev).c_str(), StateName(st),
                sid == defConsole ? L"  <= 默认(播放/录音)" : L"",
                sid == defComm ? L"  <= 默认(通信)" : L"");
    }

    // ---------- WASAPI 共享采集 ----------
    Out(L"\n=== WASAPI 共享模式采集（3 秒）===\n");
    CComPtr<IMMDevice> cap;
    HRESULT hr = en->GetDefaultAudioEndpoint(eCapture, eConsole, &cap);
    if (FAILED(hr) || !cap)
    {
        Out(L"[FAIL] 取默认录音设备失败 0x%08X\n", (unsigned)hr);
    }
    else
    {
        CComPtr<IAudioClient> ac;
        hr = cap->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&ac);
        Out(L"  Activate IAudioClient: 0x%08X\n", (unsigned)hr);
        if (SUCCEEDED(hr) && ac)
        {
            WAVEFORMATEX want = {};
            want.wFormatTag = WAVE_FORMAT_PCM;
            want.nChannels = 1;
            want.nSamplesPerSec = 16000;
            want.wBitsPerSample = 16;
            want.nBlockAlign = (WORD)(want.nChannels * want.wBitsPerSample / 8);
            want.nAvgBytesPerSec = want.nSamplesPerSec * want.nBlockAlign;

            hr = ac->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                2000000, 0, &want, nullptr);
            Out(L"  Initialize 16k/mono/16bit: 0x%08X%s\n", (unsigned)hr,
                    hr == AUDCLNT_E_UNSUPPORTED_FORMAT ? L"  (格式不支持)" :
                    hr == AUDCLNT_E_DEVICE_IN_USE ? L"  (设备被独占占用)" : L"");

            if (FAILED(hr))
            {
                WAVEFORMATEX* mix = nullptr;
                if (SUCCEEDED(ac->GetMixFormat(&mix)) && mix)
                {
                    Out(L"  mix format: %uHz %uch %ubit\n", mix->nSamplesPerSec, mix->nChannels, mix->wBitsPerSample);
                    hr = ac->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 2000000, 0, mix, nullptr);
                    Out(L"  Initialize 按 mix format: 0x%08X\n", (unsigned)hr);
                    CoTaskMemFree(mix);
                }
            }

            if (SUCCEEDED(hr))
            {
                CComPtr<IAudioCaptureClient> cc;
                hr = ac->GetService(__uuidof(IAudioCaptureClient), (void**)&cc);
                if (SUCCEEDED(hr) && cc && SUCCEEDED(ac->Start()))
                {
                    LARGE_INTEGER f, t0, t;
                    QueryPerformanceFrequency(&f);
                    QueryPerformanceCounter(&t0);
                    double peakAll = 0.0, peakWin = 0.0;
                    ULONGLONG frames = 0;
                    int win = 0;
                    for (;;)
                    {
                        QueryPerformanceCounter(&t);
                        double el = double(t.QuadPart - t0.QuadPart) / f.QuadPart;
                        if (el > 3.0) break;

                        UINT32 packet = 0;
                        cc->GetNextPacketSize(&packet);
                        while (packet)
                        {
                            BYTE* data = nullptr; UINT32 nf = 0; DWORD flags = 0;
                            if (FAILED(cc->GetBuffer(&data, &nf, &flags, nullptr, nullptr))) break;
                            if (nf && data && !(flags & AUDCLNT_BUFFERFLAGS_SILENT))
                            {
                                const short* s = (const short*)data;
                                for (UINT32 i = 0; i < nf * want.nChannels; ++i)
                                {
                                    double v = s[i] < 0 ? -s[i] : s[i];
                                    if (v > peakWin) peakWin = v;
                                    if (v > peakAll) peakAll = v;
                                }
                                frames += nf;
                            }
                            cc->ReleaseBuffer(nf);
                            cc->GetNextPacketSize(&packet);
                        }
                        Sleep(100);
                        if (++win % 10 == 0)   // 每秒打印一次
                        {
                            Out(L"  t=%.1fs 峰值=%5.0f (%s)\n", el, peakWin,
                                    peakWin > 200 ? L"有声音" : L"很安静");
                            peakWin = 0.0;
                        }
                    }
                    ac->Stop();
                    Out(L"  采集帧数=%llu  全程峰值=%.0f / 32767  -> %s\n", frames, peakAll,
                            peakAll > 200 ? L"共享模式能拿到真实音频" : L"共享受限或麦克风被静音/占用");
                }
                else
                {
                    Out(L"[FAIL] 启动采集失败 0x%08X\n", (unsigned)hr);
                }
            }
        }
    }

    // ---------- SAPI in-proc 输入 ----------
    Out(L"\n=== SAPI in-proc 识别器（3 秒）===\n");
    {
        CComPtr<ISpRecognizer> reco;
        HRESULT hr2 = reco.CoCreateInstance(CLSID_SpInprocRecognizer, nullptr, CLSCTX_ALL);
        Out(L"  CoCreateInstance: 0x%08X\n", (unsigned)hr2);
        if (SUCCEEDED(hr2))
        {
            hr2 = reco->SetInput(nullptr, TRUE);
            Out(L"  SetInput(nullptr=默认麦克风): 0x%08X%s\n", (unsigned)hr2,
                    FAILED(hr2) ? L"  (打不开音频输入)" : L"");
            if (SUCCEEDED(hr2))
            {
                CComPtr<ISpRecoContext> ctx;
                hr2 = reco->CreateRecoContext(&ctx);
                if (SUCCEEDED(hr2) && ctx)
                {
                    ctx->SetNotifyWin32Event();
                    const ULONGLONG interest = SPFEI(SPEI_RECOGNITION) | SPFEI(SPEI_SOUND_START) |
                                               SPFEI(SPEI_SOUND_END) | SPFEI(SPEI_HYPOTHESIS);
                    ctx->SetInterest(interest, interest);
                    CComPtr<ISpRecoGrammar> g;
                    ctx->CreateGrammar(1, &g);
                    if (g) { g->LoadDictation(nullptr, SPLO_STATIC); g->SetDictationState(SPRS_ACTIVE); }
                    ctx->SetContextState(SPCS_ENABLED);
                    reco->SetRecoState(SPRST_ACTIVE);

                    HANDLE ev = ctx->GetNotifyEventHandle();
                    int soundStart = 0, recoCnt = 0, hypo = 0;
                    LARGE_INTEGER f, t0, t;
                    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
                    while (true)
                    {
                        QueryPerformanceCounter(&t);
                        if (double(t.QuadPart - t0.QuadPart) / f.QuadPart > 3.0) break;
                        if (ev && WaitForSingleObject(ev, 100) == WAIT_OBJECT_0)
                        {
                            CSpEvent e;
                            while (e.GetFrom(ctx) == S_OK)
                            {
                                if (e.eEventId == SPEI_SOUND_START) ++soundStart;
                                else if (e.eEventId == SPEI_RECOGNITION) ++recoCnt;
                                else if (e.eEventId == SPEI_HYPOTHESIS) ++hypo;
                            }
                        }
                    }
                    Out(L"  3 秒内：SOUND_START=%d  RECOGNITION=%d  HYPOTHESIS=%d\n", soundStart, recoCnt, hypo);
                    Out(L"  -> %s\n", soundStart > 0 ? L"SAPI 能收到声音" : L"SAPI 收不到任何声音（这就是『完全没反应』）");
                }
            }
        }
    }

    Out(L"\n[完成，日志见 build/micprobe.log]\n");
    if (g_log) fclose(g_log);
    CoUninitialize();
    return 0;
}
