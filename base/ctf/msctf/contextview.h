/* SPDX-License-Identifier: LGPL-2.0-or-later */
#pragma once

class CContextView final : public ITfContextView
{
    LONG m_refs;
    ITfContext *m_context;
    ITextStoreACP *m_store;
    TsViewCookie m_view;

    BOOL HasReadLock(TfEditCookie ec)
    {
        if (get_Cookie_magic(ec) != COOKIE_MAGIC_EDITCOOKIE) return FALSE;
        EditCookie *cookie = static_cast<EditCookie *>(get_Cookie_data(ec));
        return cookie && cookie->pOwningContext == m_context && (cookie->lockType & TS_LF_READ);
    }
public:
    CContextView(ITfContext *context, ITextStoreACP *store, TsViewCookie view)
        : m_refs(1), m_context(context), m_store(store), m_view(view)
    {
        m_context->AddRef();
        m_store->AddRef();
    }
    ~CContextView() { m_store->Release(); m_context->Release(); }
    STDMETHODIMP QueryInterface(REFIID iid, void **out) override
    {
        if (!out) return E_POINTER;
        *out = NULL;
        if (iid != IID_IUnknown && iid != IID_ITfContextView) return E_NOINTERFACE;
        *out = static_cast<ITfContextView *>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG refs = InterlockedDecrement(&m_refs);
        if (!refs) delete this;
        return refs;
    }
    STDMETHODIMP GetRangeFromPoint(TfEditCookie ec, const POINT *point, DWORD flags, ITfRange **range) override
    {
        if (!range || !point) return E_INVALIDARG;
        *range = NULL;
        if (!HasReadLock(ec)) return TF_E_NOLOCK;
        LONG position;
        HRESULT hr = m_store->GetACPFromPoint(m_view, point, flags, &position);
        if (FAILED(hr)) return hr;
        return Range_Constructor(m_context, position, position, range);
    }
    STDMETHODIMP GetTextExt(TfEditCookie ec, ITfRange *range, RECT *rect, BOOL *clipped) override
    {
        if (!range || !rect || !clipped) return E_INVALIDARG;
        if (!HasReadLock(ec)) return TF_E_NOLOCK;
        ITfContext *context = NULL;
        HRESULT hr = range->GetContext(&context);
        if (FAILED(hr)) return hr;
        BOOL sameContext = context == m_context;
        if (context) context->Release();
        if (!sameContext) return E_INVALIDARG;
        ITfRangeACP *acp;
        hr = range->QueryInterface(IID_ITfRangeACP, reinterpret_cast<void **>(&acp));
        if (FAILED(hr)) return hr;
        LONG start, length;
        hr = acp->GetExtent(&start, &length);
        acp->Release();
        if (FAILED(hr)) return hr;
        return m_store->GetTextExt(m_view, start, start + length, rect, clipped);
    }
    STDMETHODIMP GetScreenExt(RECT *rect) override
    {
        return rect ? m_store->GetScreenExt(m_view, rect) : E_INVALIDARG;
    }
    STDMETHODIMP GetWnd(HWND *window) override
    {
        return window ? m_store->GetWnd(m_view, window) : E_INVALIDARG;
    }
};

class CEnumEditRanges final : public IEnumTfRanges
{
    LONG m_refs;
    ITfRange *m_range;
    BOOL m_consumed;
public:
    CEnumEditRanges(ITfRange *range, BOOL consumed = FALSE)
        : m_refs(1), m_range(range), m_consumed(consumed)
    { if (m_range) m_range->AddRef(); }
    ~CEnumEditRanges() { if (m_range) m_range->Release(); }
    STDMETHODIMP QueryInterface(REFIID iid, void **out) override
    {
        if (!out) return E_POINTER;
        *out = NULL;
        if (iid != IID_IUnknown && iid != IID_IEnumTfRanges) return E_NOINTERFACE;
        *out = static_cast<IEnumTfRanges *>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG refs = InterlockedDecrement(&m_refs);
        if (!refs) delete this;
        return refs;
    }
    STDMETHODIMP Clone(IEnumTfRanges **out) override
    {
        if (!out) return E_INVALIDARG;
        *out = new(cicNoThrow) CEnumEditRanges(m_range, m_consumed);
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP Next(ULONG count, ITfRange **ranges, ULONG *fetched) override
    {
        if (fetched) *fetched = 0;
        if (!ranges || (count != 1 && !fetched)) return E_INVALIDARG;
        if (!count) return S_OK;
        *ranges = NULL;
        if (!m_range || m_consumed) return S_FALSE;
        HRESULT hr = m_range->Clone(ranges);
        if (FAILED(hr)) return hr;
        m_consumed = TRUE;
        if (fetched) *fetched = 1;
        return count == 1 ? S_OK : S_FALSE;
    }
    STDMETHODIMP Reset() override { m_consumed = FALSE; return S_OK; }
    STDMETHODIMP Skip(ULONG count) override
    {
        if (!count) return S_OK;
        BOOL available = m_range && !m_consumed;
        m_consumed = TRUE;
        return count == 1 && available ? S_OK : S_FALSE;
    }
};

class CEditRecord final : public ITfEditRecord
{
    LONG m_refs;
    BOOL m_selectionChanged;
    ITfRange *m_range;
public:
    CEditRecord(BOOL selectionChanged, ITfRange *range)
        : m_refs(1), m_selectionChanged(selectionChanged), m_range(range)
    { if (m_range) m_range->AddRef(); }
    ~CEditRecord() { if (m_range) m_range->Release(); }
    STDMETHODIMP QueryInterface(REFIID iid, void **out) override
    {
        if (!out) return E_POINTER;
        *out = NULL;
        if (iid != IID_IUnknown && iid != IID_ITfEditRecord) return E_NOINTERFACE;
        *out = static_cast<ITfEditRecord *>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG refs = InterlockedDecrement(&m_refs);
        if (!refs) delete this;
        return refs;
    }
    STDMETHODIMP GetSelectionStatus(BOOL *changed) override
    {
        if (!changed) return E_INVALIDARG;
        *changed = m_selectionChanged;
        return S_OK;
    }
    STDMETHODIMP GetTextAndPropertyUpdates(DWORD flags, const GUID **properties,
                                          ULONG count, IEnumTfRanges **out) override
    {
        if (!out) return E_INVALIDARG;
        *out = NULL;
        if ((flags & ~TF_GTP_INCL_TEXT) || (count && !properties)) return E_INVALIDARG;
        *out = new(cicNoThrow) CEnumEditRanges((flags & TF_GTP_INCL_TEXT) ? m_range : NULL);
        return *out ? S_OK : E_OUTOFMEMORY;
    }
};
