/*
 * PROJECT:     ReactOS Disk Cleanup
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     CEmptyVolumeCacheCallBack definition / implementation
 * COPYRIGHT:   Copyright 2023-2025 Mark Jansen <mark.jansen@reactos.org>
 */


// We don't really use this, but some windows handlers crash without it
struct CEmptyVolumeCacheCallBack
    : public IEmptyVolumeCacheCallBack
{
    CEmptyVolumeCacheCallBack() throw()
        : m_refCount(1)
    {
    }

    virtual ~CEmptyVolumeCacheCallBack() throw() = default;

    STDMETHOD_(ULONG, AddRef)() throw() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&m_refCount));
    }
    STDMETHOD_(ULONG, Release)() throw() override
    {
        ULONG newRef = static_cast<ULONG>(InterlockedDecrement(&m_refCount));
        if (newRef == 0)
        {
            delete this;
        }
        return newRef;
    }
    STDMETHOD(QueryInterface)(
        REFIID riid,
        _COM_Outptr_ void** ppvObject) throw() override
    {
        if (!ppvObject)
            return E_POINTER;

        *ppvObject = nullptr;
        if (riid == IID_IUnknown || riid == IID_IEmptyVolumeCacheCallBack)
        {
            *ppvObject = static_cast<IEmptyVolumeCacheCallBack*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }


    STDMETHODIMP ScanProgress(
        _In_ DWORDLONG dwlSpaceUsed,
        _In_ DWORD dwFlags,
        _In_ LPCWSTR pcwszStatus) override
    {
        DPRINT("dwlSpaceUsed: %lld, dwFlags: %x\n", dwlSpaceUsed, dwFlags);
        return S_OK;
    }

    STDMETHODIMP PurgeProgress(
        _In_ DWORDLONG dwlSpaceFreed,
        _In_ DWORDLONG dwlSpaceToFree,
        _In_ DWORD dwFlags,
        _In_ LPCWSTR pcwszStatus) override
    {
        DPRINT("dwlSpaceFreed: %lld, dwlSpaceToFree: %lld, dwFlags: %x\n", dwlSpaceFreed, dwlSpaceToFree, dwFlags);
        return S_OK;
    }

private:
    volatile LONG m_refCount;
};
