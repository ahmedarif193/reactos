/* SPDX-License-Identifier: LGPL-2.0-or-later */
#pragma once

class CSinkSnapshot
{
    IUnknown **m_sinks;
    ULONG m_count;
public:
    CSinkSnapshot() : m_sinks(NULL), m_count(0) {}
    ~CSinkSnapshot()
    {
        for (ULONG i = 0; i < m_count; ++i) m_sinks[i]->Release();
        cicMemFree(m_sinks);
    }
    HRESULT Capture(struct list *sinks)
    {
        struct list *cursor;
        ULONG count = 0;
        LIST_FOR_EACH(cursor, sinks) ++count;
        if (!count) return S_OK;
        m_sinks = static_cast<IUnknown **>(cicMemAlloc(count * sizeof(*m_sinks)));
        if (!m_sinks) return E_OUTOFMEMORY;
        LIST_FOR_EACH(cursor, sinks)
        {
            IUnknown *sink = LIST_ENTRY(cursor, Sink, entry)->interfaces.pIUnknown;
            sink->AddRef();
            m_sinks[m_count++] = sink;
        }
        return S_OK;
    }
    ULONG Count() const { return m_count; }
    IUnknown *operator[](ULONG index) const { return m_sinks[index]; }
private:
    CSinkSnapshot(const CSinkSnapshot &);
    CSinkSnapshot &operator=(const CSinkSnapshot &);
};
