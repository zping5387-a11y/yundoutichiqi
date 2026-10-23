// SapiAudioStream.h - 把 WASAPI 采集到的 PCM 包装成 SAPI 可拉取的 ISpStreamFormat
//
// SAPI 的 in-proc 识别器在工作线程里不断调用 IStream::Read 拉音频，
// 我们的 Read 会阻塞等待实时采集数据（最长 200ms），从而形成"实时音频源"。
#pragma once

#include "framework.h"
#include "AudioCapture.h"

class CAudioPullStream : public ISpStreamFormat
{
public:
    explicit CAudioPullStream(CAudioCapture* cap) : m_cap(cap) {}

    // ---- IUnknown ----
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IStream || riid == IID_ISpStreamFormat)
        {
            *ppv = static_cast<ISpStreamFormat*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)::InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() override
    {
        const LONG n = ::InterlockedDecrement(&m_ref);
        if (n == 0) delete this;
        return (ULONG)n;
    }

    // ---- IStream ----
    STDMETHODIMP Read(void* pv, ULONG cb, ULONG* pcbRead) override
    {
        if (!pv) return E_POINTER;
        ULONG got = 0;
        if (m_cap)
        {
            // SAPI 要求尽量填满缓冲区（不足会被当成流结束），因此内部会阻塞等待
            got = (ULONG)m_cap->Read(pv, (int)cb, 3000);
        }
        if (pcbRead) *pcbRead = got;
        return S_OK;
    }
    STDMETHODIMP Write(const void*, ULONG, ULONG*) override { return E_NOTIMPL; }
    STDMETHODIMP Seek(LARGE_INTEGER, DWORD, ULARGE_INTEGER* pNew) override
    {
        if (pNew) pNew->QuadPart = 0;
        return S_OK;    // 实时流：忽略定位请求
    }
    STDMETHODIMP SetSize(ULARGE_INTEGER) override { return E_NOTIMPL; }
    STDMETHODIMP CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
    STDMETHODIMP Commit(DWORD) override { return S_OK; }
    STDMETHODIMP Revert() override { return S_OK; }
    STDMETHODIMP LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    STDMETHODIMP UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    STDMETHODIMP Stat(STATSTG* pStat, DWORD) override
    {
        if (!pStat) return E_POINTER;
        ZeroMemory(pStat, sizeof(*pStat));
        pStat->type = STGTY_STREAM;
        return S_OK;
    }
    STDMETHODIMP Clone(IStream**) override { return E_NOTIMPL; }

    // ---- ISpStreamFormat ----
    STDMETHODIMP GetFormat(GUID* pFmtId, WAVEFORMATEX** ppWfex) override
    {
        if (!pFmtId || !ppWfex) return E_POINTER;
        *pFmtId = SPDFID_WaveFormatEx;

        const int rate = m_cap ? m_cap->SampleRate() : 16000;
        const int bytes = sizeof(WAVEFORMATEX);
        WAVEFORMATEX* wf = (WAVEFORMATEX*)::CoTaskMemAlloc(bytes);
        if (!wf) return E_OUTOFMEMORY;
        ZeroMemory(wf, bytes);
        wf->wFormatTag = WAVE_FORMAT_PCM;
        wf->nChannels = 1;
        wf->nSamplesPerSec = (DWORD)rate;
        wf->wBitsPerSample = 16;
        wf->nBlockAlign = (WORD)(wf->nChannels * wf->wBitsPerSample / 8);
        wf->nAvgBytesPerSec = wf->nSamplesPerSec * wf->nBlockAlign;
        wf->cbSize = 0;
        *ppWfex = wf;
        return S_OK;
    }

private:
    LONG m_ref = 1;
    CAudioCapture* m_cap = nullptr;
};
