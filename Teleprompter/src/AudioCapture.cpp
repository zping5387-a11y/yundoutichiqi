// AudioCapture.cpp
#include "AudioCapture.h"
#include <functiondiscoverykeys_devpkey.h>

#pragma comment(lib, "ole32.lib")

namespace
{
    constexpr int kRingBytes = 16000 * 2 * 6;   // 6 秒 16k/mono/16bit

    std::wstring DeviceIdOf(IMMDevice* dev)
    {
        LPWSTR id = nullptr;
        std::wstring out;
        if (dev && SUCCEEDED(dev->GetId(&id)) && id) { out = id; ::CoTaskMemFree(id); }
        return out;
    }

    std::wstring FriendlyNameOf(IMMDevice* dev)
    {
        CComPtr<IPropertyStore> ps;
        if (!dev || FAILED(dev->OpenPropertyStore(STGM_READ, &ps)) || !ps) return L"";
        PROPVARIANT v;
        PropVariantInit(&v);
        std::wstring out;
        if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
            out = v.vt == VT_LPWSTR ? v.pwszVal : L"";
        PropVariantClear(&v);
        return out;
    }

    CComPtr<IMMDeviceEnumerator> MakeEnumerator()
    {
        CComPtr<IMMDeviceEnumerator> en;
        en.CoCreateInstance(__uuidof(MMDeviceEnumerator));
        return en;
    }
}

std::vector<AudioDeviceInfo> CAudioCapture::EnumDevices()
{
    std::vector<AudioDeviceInfo> out;
    CComPtr<IMMDeviceEnumerator> en = MakeEnumerator();
    if (!en) return out;

    std::wstring defComm, defConsole;
    {
        CComPtr<IMMDevice> d;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(eCapture, eCommunications, &d)) && d)
            defComm = DeviceIdOf(d);
        d.Release();
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(eCapture, eConsole, &d)) && d)
            defConsole = DeviceIdOf(d);
    }

    CComPtr<IMMDeviceCollection> coll;
    if (FAILED(en->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &coll)) || !coll) return out;

    UINT n = 0;
    coll->GetCount(&n);
    for (UINT i = 0; i < n; ++i)
    {
        CComPtr<IMMDevice> dev;
        if (FAILED(coll->Item(i, &dev)) || !dev) continue;
        AudioDeviceInfo info;
        info.id = DeviceIdOf(dev);
        info.name = FriendlyNameOf(dev);
        if (info.name.empty()) continue;
        info.isDefaultComm = (info.id == defComm);
        info.isDefaultConsole = (info.id == defConsole);
        out.push_back(info);
    }

    // 默认通信设备排最前
    std::stable_sort(out.begin(), out.end(), [](const AudioDeviceInfo& a, const AudioDeviceInfo& b) {
        return a.isDefaultComm && !b.isDefaultComm;
    });
    return out;
}

std::wstring CAudioCapture::DefaultDeviceId(bool communications)
{
    CComPtr<IMMDeviceEnumerator> en = MakeEnumerator();
    if (!en) return L"";
    CComPtr<IMMDevice> dev;
    if (FAILED(en->GetDefaultAudioEndpoint(eCapture, communications ? eCommunications : eConsole, &dev)) || !dev)
        return L"";
    return DeviceIdOf(dev);
}

CAudioCapture::~CAudioCapture()
{
    Stop();
}

bool CAudioCapture::Start(const std::wstring& deviceId, std::wstring& err)
{
    Stop();

    // 自检：TP_MIC_LOOPBACK=1 时改抓「扬声器回环」（系统播放的声音），
    // 用于在没有麦克风/无人说话的环境下验证 采集→识别→跟随 整条链路。
    wchar_t lb[8] = { 0 };
    const bool loopback = (::GetEnvironmentVariableW(L"TP_MIC_LOOPBACK", lb, 8) > 0 && lb[0] == L'1');
    const EDataFlow flow = loopback ? eRender : eCapture;
    const ERole role = loopback ? eConsole : eCommunications;

    CComPtr<IMMDeviceEnumerator> en = MakeEnumerator();
    if (!en) { err = L"无法初始化音频设备枚举器"; return false; }

    CComPtr<IMMDevice> dev;
    if (!deviceId.empty() && !loopback)
    {
        if (FAILED(en->GetDevice(deviceId.c_str(), &dev)) || !dev)
            dev.Release();
    }
    if (!dev)  // 未指定或找不到：优先「默认通信设备」（会议软件用的那个）
    {
        if (FAILED(en->GetDefaultAudioEndpoint(flow, role, &dev)) || !dev)
            if (FAILED(en->GetDefaultAudioEndpoint(flow, eConsole, &dev)) || !dev)
            {
                err = loopback ? L"没有可用的播放设备用于回环采集" : L"系统没有可用的麦克风设备";
                return false;
            }
    }
    m_name = FriendlyNameOf(dev);
    if (loopback) m_name = L"扬声器回环（自检）· " + m_name;

    HRESULT hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&m_client);
    if (FAILED(hr) || !m_client)
    {
        // 设备被独占（部分会议软件/驱动会独占）
        err = (hr == AUDCLNT_E_DEVICE_IN_USE)
                  ? L"麦克风正被其他程序独占（请在会议软件里改用共享模式）"
                  : L"无法打开麦克风设备，代码 0x" + std::to_wstring((unsigned)hr);
        return false;
    }

    WAVEFORMATEX want = {};
    want.wFormatTag = WAVE_FORMAT_PCM;
    want.nChannels = 1;
    want.nSamplesPerSec = 16000;
    want.wBitsPerSample = 16;
    want.nBlockAlign = (WORD)(want.nChannels * want.wBitsPerSample / 8);
    want.nAvgBytesPerSec = want.nSamplesPerSec * want.nBlockAlign;
    m_blockAlign = want.nBlockAlign;

    hr = m_client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                              AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY |
                              (loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0),
                              2000000, 0, &want, nullptr);
    if (FAILED(hr))
    {
        // 老驱动不支持自动重采样时，退而求其次用混音格式（16k 由 SAPI 侧不保证，但一般可用）
        WAVEFORMATEX* mix = nullptr;
        if (SUCCEEDED(m_client->GetMixFormat(&mix)) && mix)
        {
            // 只接受 16bit PCM 的混音格式，其它格式（浮点）不做转换，避免喂错数据给识别器
            const bool pcm16 = (mix->wFormatTag == WAVE_FORMAT_PCM && mix->wBitsPerSample == 16);
            hr = pcm16 ? m_client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                              loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0,
                                              2000000, 0, mix, nullptr)
                       : AUDCLNT_E_UNSUPPORTED_FORMAT;
            if (SUCCEEDED(hr) && pcm16) m_blockAlign = mix->nBlockAlign;
            ::CoTaskMemFree(mix);
        }
    }
    if (FAILED(hr))
    {
        err = (hr == AUDCLNT_E_DEVICE_IN_USE)
                  ? L"麦克风正被其他程序独占，无法共享采集"
                  : L"麦克风初始化失败，代码 0x" + std::to_wstring((unsigned)hr);
        return false;
    }

    hr = m_client->GetService(__uuidof(IAudioCaptureClient), (void**)&m_capture);
    if (FAILED(hr) || !m_capture) { err = L"无法获取音频采集接口"; return false; }

    if (!m_csInit) { ::InitializeCriticalSection(&m_cs); m_csInit = true; }
    ::EnterCriticalSection(&m_cs);
    m_ring.assign(kRingBytes, 0);
    m_head = 0;
    m_size = 0;
    ::LeaveCriticalSection(&m_cs);

    m_stop = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_dataEvt = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_stop || !m_dataEvt) { err = L"无法创建音频同步事件"; return false; }

    hr = m_client->Start();
    if (FAILED(hr)) { err = L"启动麦克风采集失败"; return false; }

    m_running = 1;
    m_thread = ::CreateThread(nullptr, 0, &CAudioCapture::ThreadEntry, this, 0, nullptr);
    if (!m_thread) { err = L"无法创建采集线程"; Stop(); return false; }
    return true;
}

void CAudioCapture::Stop()
{
    m_running = 0;
    if (m_stop) ::SetEvent(m_stop);
    if (m_thread)
    {
        ::WaitForSingleObject(m_thread, 1500);
        ::CloseHandle(m_thread);
        m_thread = nullptr;
    }
    if (m_client) m_client->Stop();
    m_capture.Release();
    m_client.Release();
    if (m_stop) { ::CloseHandle(m_stop); m_stop = nullptr; }
    if (m_dataEvt) { ::CloseHandle(m_dataEvt); m_dataEvt = nullptr; }
    if (m_csInit)
    {
        ::EnterCriticalSection(&m_cs);
        m_size = 0; m_head = 0;
        ::LeaveCriticalSection(&m_cs);
    }
    m_level = 0;
}

DWORD WINAPI CAudioCapture::ThreadEntry(LPVOID p)
{
    static_cast<CAudioCapture*>(p)->CaptureLoop();
    return 0;
}

void CAudioCapture::CaptureLoop()
{
    std::vector<BYTE> zeros;

    while (m_running)
    {
        UINT32 packet = 0;
        if (FAILED(m_capture->GetNextPacketSize(&packet))) break;

        while (packet)
        {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            HRESULT hr = m_capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr))
            {
                if (hr == AUDCLNT_S_BUFFER_EMPTY) break;
                if (hr == AUDCLNT_E_DEVICE_INVALIDATED) { m_running = 0; return; }
                break;
            }

            if (frames)
            {
                const int bytes = (int)frames * (int)m_blockAlign;
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT || !data)
                {
                    // 静音也要喂零数据：SAPI 靠连续的静音判断句子结束
                    if ((int)zeros.size() < bytes) zeros.assign(bytes, 0);
                    Push(zeros.data(), bytes);
                    if (::GetTickCount() - (DWORD)m_levelTick > 400) m_level = 0;
                }
                else
                {
                    Push(data, bytes);
                    short peak = 0;
                    const short* s = (const short*)data;
                    const int cnt = bytes / 2;
                    for (int i = 0; i < cnt; ++i)
                    {
                        const short v = s[i] < 0 ? (short)-s[i] : s[i];
                        if (v > peak) peak = v;
                    }
                    m_level = (LONG)peak * 100 / 32768;
                    m_levelTick = (LONG)::GetTickCount();
                }
            }
            m_capture->ReleaseBuffer(frames);
            if (FAILED(m_capture->GetNextPacketSize(&packet))) { m_running = 0; return; }
        }

        ::WaitForSingleObject(m_stop, 20);
    }
}

bool CAudioCapture::Push(const BYTE* data, int bytes)
{
    if (!m_csInit || bytes <= 0) return false;
    bool overflow = false;

    ::EnterCriticalSection(&m_cs);
    const int cap = (int)m_ring.size();
    for (int i = 0; i < bytes; ++i)
    {
        if (m_size >= cap) { overflow = true; break; }
        m_ring[(m_head + m_size) % cap] = data[i];
        ++m_size;
    }
    ::LeaveCriticalSection(&m_cs);

    if (m_dataEvt) ::SetEvent(m_dataEvt);
    return !overflow;
}

int CAudioCapture::Read(void* dst, int bytes, DWORD timeoutMs)
{
    if (!m_dataEvt || bytes <= 0) return 0;

    // 关键：SAPI 按"文件"语义读取音频流，一次 Read 若返回不足请求长度，
    // 它就会认为是流结束（这也是一开始识别不出任何文字的原因）。
    // 因此这里必须阻塞到凑满 bytes（或超时/停止）。
    const DWORD start = ::GetTickCount();
    int total = 0;

    while (total < bytes)
    {
        if (m_size <= 0)
        {
            const DWORD elapsed = ::GetTickCount() - start;
            if (elapsed >= timeoutMs) break;
            DWORD wait = timeoutMs - elapsed;
            if (wait > 100) wait = 100;
            if (!m_running && m_size <= 0) break;
            if (::WaitForSingleObject(m_dataEvt, wait) != WAIT_OBJECT_0)
            {
                if (!m_running) break;
                continue;
            }
        }

        ::EnterCriticalSection(&m_cs);
        const int cap = (int)m_ring.size();
        int want = bytes - total;
        if (want > m_size) want = m_size;
        for (int i = 0; i < want; ++i)
            ((BYTE*)dst)[total + i] = m_ring[(m_head + i) % cap];
        m_head = (m_head + want) % cap;
        m_size -= want;
        ::LeaveCriticalSection(&m_cs);
        total += want;
    }

    return total;
}

int CAudioCapture::Level() const
{
    if (::GetTickCount() - (DWORD)m_levelTick > 500) return 0;
    LONG v = m_level;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    return (int)v;
}
