// AudioCapture.h - WASAPI 共享模式麦克风采集
//
// 为什么不用 SAPI 默认输入：SAPI 打开的是「默认录音设备（eConsole）」，而会议软件
// 用的是「默认通信设备（eCommunications）」，两者常常不是同一个麦克风。
// 实测：默认录音设备是显示器/音箱自带的麦，采集到的峰值只有 1/32767（等于静音），
// 识别器 3 秒内收不到任何声音 —— 表现就是「点了开始跟读，完全没反应」。
// 因此自己用 WASAPI 共享模式采集（可指定设备、可与会议软件同时占用），
// 再通过 ISpStreamFormat 把 PCM 喂给 SAPI 识别器。
#pragma once

#include "framework.h"
#include <mmdeviceapi.h>
#include <audioclient.h>

struct AudioDeviceInfo
{
    std::wstring id;
    std::wstring name;
    bool isDefaultComm = false;
    bool isDefaultConsole = false;
};

class CAudioCapture
{
public:
    ~CAudioCapture();

    static std::vector<AudioDeviceInfo> EnumDevices();
    static std::wstring DefaultDeviceId(bool communications);

    // deviceId 为空时用「默认通信设备」（会议软件用的那个），再退回默认录音设备
    bool Start(const std::wstring& deviceId, std::wstring& err);
    void Stop();
    bool IsRunning() const { return m_running != 0; }

    // 阻塞读取：最多等 timeoutMs，返回实际字节数（0 = 超时）
    int Read(void* dst, int bytes, DWORD timeoutMs);

    int Level() const;                   // 0..100，最近 400ms 峰值
    const std::wstring& DeviceName() const { return m_name; }
    int SampleRate() const { return 16000; }

private:
    static DWORD WINAPI ThreadEntry(LPVOID p);
    void CaptureLoop();
    bool Push(const BYTE* data, int bytes);

    CComPtr<IAudioClient>        m_client;
    CComPtr<IAudioCaptureClient> m_capture;
    HANDLE m_thread = nullptr;
    HANDLE m_stop = nullptr;
    HANDLE m_dataEvt = nullptr;

    mutable CRITICAL_SECTION m_cs;
    bool m_csInit = false;
    std::vector<BYTE> m_ring;
    int m_head = 0, m_size = 0;

    volatile LONG m_running = 0;
    volatile LONG m_level = 0;
    volatile LONG m_levelTick = 0;
    WORD m_blockAlign = 2;
    std::wstring m_name;
};
