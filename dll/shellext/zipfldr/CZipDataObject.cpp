/*
 * PROJECT:     LiberNT Zip Shell Extension
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Data object exposing zip entries as file descriptors and content streams
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

struct ZipDataItem
{
    CStringW Name;
    bool Directory;
    bool Password;
    bool HasDate;
    ULONG DosDate;
    ULONG64 Size;
    unz64_file_pos Pos;
};

struct ZipDataSelection
{
    CStringW Name;
    bool Directory;
};

static BOOL ZipDateToFileTime(ULONG DosDate, FILETIME *pFileTime)
{
    FILETIME LocalFileTime;
    if (!DosDateTimeToFileTime(HIWORD(DosDate), LOWORD(DosDate), &LocalFileTime))
        return FALSE;
    return LocalFileTimeToFileTime(&LocalFileTime, pFileTime);
}

class CZipStream :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IStream
{
    unzFile m_Zip = NULL;
    bool m_Opened = false;
    bool m_Encrypted = false;
    CStringA m_Password;
    CStringW m_Name;
    FILETIME m_Time = {};
    ULONGLONG m_Size = 0;
    ULONGLONG m_Position = 0;
    ULONGLONG m_Decoded = 0;

    void CloseEntry()
    {
        if (m_Opened)
            unzCloseCurrentFile(m_Zip);
        m_Opened = false;
    }

    HRESULT OpenEntry()
    {
        CloseEntry();
        int err = m_Encrypted ? unzOpenCurrentFilePassword(m_Zip, m_Password) : unzOpenCurrentFile(m_Zip);
        if (err != UNZ_OK)
        {
            DPRINT1("ERROR, unzOpenCurrentFile: 0x%x\n", err);
            return STG_E_READFAULT;
        }
        m_Opened = true;
        m_Decoded = 0;
        return S_OK;
    }

    HRESULT Sync()
    {
        if (!m_Opened || m_Position < m_Decoded)
        {
            HRESULT hr = OpenEntry();
            if (FAILED(hr))
                return hr;
        }

        BYTE Skip[4096];
        while (m_Decoded < m_Position)
        {
            unsigned Chunk = (unsigned)min((ULONGLONG)sizeof(Skip), m_Position - m_Decoded);
            int Read = unzReadCurrentFile(m_Zip, Skip, Chunk);
            if (Read <= 0)
                return STG_E_READFAULT;
            m_Decoded += Read;
        }
        return S_OK;
    }

public:
    ~CZipStream()
    {
        CloseEntry();
        if (m_Zip)
            unzClose(m_Zip);
    }

    HRESULT Initialize(PCWSTR ZipFile, const ZipDataItem *Item, PCSTR Password)
    {
        m_Zip = unzOpen2_64(ZipFile, &g_FFunc);
        if (!m_Zip)
            return STG_E_FILENOTFOUND;
        if (unzGoToFilePos64(m_Zip, &Item->Pos) != UNZ_OK)
            return STG_E_READFAULT;

        m_Name = PathFindFileNameW(Item->Name);
        m_Size = Item->Size;
        m_Encrypted = Item->Password;
        m_Password = Password;
        if (Item->HasDate)
            ZipDateToFileTime(Item->DosDate, &m_Time);

        HRESULT hr = OpenEntry();
        if (FAILED(hr) || !m_Encrypted)
            return hr;

        BYTE Probe[10];
        if (unzReadCurrentFile(m_Zip, Probe, sizeof(Probe)) < 0)
            return HRESULT_FROM_WIN32(ERROR_INVALID_PASSWORD);
        return OpenEntry();
    }

    // *** ISequentialStream methods ***
    STDMETHODIMP Read(void *pv, ULONG cb, ULONG *pcbRead) override
    {
        if (pcbRead)
            *pcbRead = 0;
        if (!pv)
            return STG_E_INVALIDPOINTER;

        HRESULT hr = S_OK;
        ULONG Done = 0;
        if (cb && m_Position < m_Size)
            hr = Sync();

        while (SUCCEEDED(hr) && Done < cb && m_Position < m_Size)
        {
            unsigned Chunk = (unsigned)min((ULONGLONG)(cb - Done), m_Size - m_Position);
            int Read = unzReadCurrentFile(m_Zip, (BYTE*)pv + Done, Chunk);
            if (Read < 0)
            {
                DPRINT1("ERROR, unzReadCurrentFile: 0x%x\n", Read);
                hr = STG_E_READFAULT;
            }
            else if (Read == 0)
            {
                break;
            }
            else
            {
                Done += Read;
                m_Position += Read;
                m_Decoded += Read;
            }
        }

        if (SUCCEEDED(hr) && m_Opened && m_Decoded == m_Size)
        {
            int err = unzCloseCurrentFile(m_Zip);
            m_Opened = false;
            if (err != UNZ_OK)
            {
                DPRINT1("ERROR, unzCloseCurrentFile: 0x%x\n", err);
                hr = STG_E_DOCFILECORRUPT;
            }
        }

        if (pcbRead)
            *pcbRead = Done;
        if (FAILED(hr))
            return hr;
        return Done == cb ? S_OK : S_FALSE;
    }
    STDMETHODIMP Write(const void *pv, ULONG cb, ULONG *pcbWritten) override
    {
        if (pcbWritten)
            *pcbWritten = 0;
        return STG_E_ACCESSDENIED;
    }

    // *** IStream methods ***
    STDMETHODIMP Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition) override
    {
        LONGLONG Base;
        switch (dwOrigin)
        {
            case STREAM_SEEK_SET:
                Base = 0;
                break;
            case STREAM_SEEK_CUR:
                Base = (LONGLONG)m_Position;
                break;
            case STREAM_SEEK_END:
                Base = (LONGLONG)m_Size;
                break;
            default:
                return STG_E_INVALIDFUNCTION;
        }

        LONGLONG Target = Base + dlibMove.QuadPart;
        if (Target < 0)
            return STG_E_INVALIDFUNCTION;

        m_Position = (ULONGLONG)Target;
        if (plibNewPosition)
            plibNewPosition->QuadPart = m_Position;
        return S_OK;
    }
    STDMETHODIMP SetSize(ULARGE_INTEGER libNewSize) override
    {
        return S_OK;
    }
    STDMETHODIMP CopyTo(IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten) override
    {
        if (!pstm)
            return STG_E_INVALIDPOINTER;

        BYTE Buffer[4096];
        ULONGLONG TotalRead = 0, TotalWritten = 0;
        HRESULT hr = S_OK;
        while (TotalRead < cb.QuadPart)
        {
            ULONG Chunk = (ULONG)min((ULONGLONG)sizeof(Buffer), cb.QuadPart - TotalRead);
            ULONG Got = 0, Put = 0;
            hr = Read(Buffer, Chunk, &Got);
            if (FAILED(hr) || !Got)
                break;
            TotalRead += Got;

            hr = pstm->Write(Buffer, Got, &Put);
            TotalWritten += Put;
            if (FAILED(hr))
                break;
            if (Put != Got)
            {
                hr = STG_E_MEDIUMFULL;
                break;
            }
        }

        if (pcbRead)
            pcbRead->QuadPart = TotalRead;
        if (pcbWritten)
            pcbWritten->QuadPart = TotalWritten;
        return FAILED(hr) ? hr : S_OK;
    }
    STDMETHODIMP Commit(DWORD grfCommitFlags) override
    {
        return S_OK;
    }
    STDMETHODIMP Revert() override
    {
        return E_NOTIMPL;
    }
    STDMETHODIMP LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) override
    {
        return STG_E_INVALIDFUNCTION;
    }
    STDMETHODIMP UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) override
    {
        return STG_E_INVALIDFUNCTION;
    }
    STDMETHODIMP Stat(STATSTG *pstatstg, DWORD grfStatFlag) override
    {
        if (!pstatstg)
            return STG_E_INVALIDPOINTER;

        ZeroMemory(pstatstg, sizeof(*pstatstg));
        if (!(grfStatFlag & STATFLAG_NONAME))
        {
            HRESULT hr = SHStrDupW(m_Name, &pstatstg->pwcsName);
            if (FAILED(hr))
                return hr;
        }
        pstatstg->type = STGTY_STREAM;
        pstatstg->cbSize.QuadPart = m_Size;
        pstatstg->mtime = m_Time;
        pstatstg->ctime = m_Time;
        pstatstg->atime = m_Time;
        pstatstg->grfMode = STGM_SHARE_DENY_WRITE;
        return S_OK;
    }
    STDMETHODIMP Clone(IStream **ppstm) override
    {
        return E_NOTIMPL;
    }

public:
    DECLARE_NOT_AGGREGATABLE(CZipStream)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CZipStream)
        COM_INTERFACE_ENTRY_IID(IID_ISequentialStream, ISequentialStream)
        COM_INTERFACE_ENTRY_IID(IID_IStream, IStream)
    END_COM_MAP()
};

class CZipDataObject :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IDataObject,
    public IAsyncOperation,
    public IZip
{
    CStringW m_ZipFile;
    CStringW m_ZipDir;
    CStringA m_Password;
    HWND m_hwnd = NULL;
    CComPtr<IDataObject> m_Inner;
    CAtlArray<ZipDataSelection> m_Selection;
    CAtlArray<ZipDataItem> m_Items;
    bool m_Rendered = false;
    unzFile m_Zip = NULL;
    BOOL m_Async = FALSE;
    BOOL m_InOperation = FALSE;
    CLIPFORMAT m_cfDescriptor = 0;
    CLIPFORMAT m_cfContents = 0;

    HRESULT ReadEntries(CAtlArray<ZipDataItem> &Entries)
    {
        CZipEnumerator zipEnum;
        if (!zipEnum.Initialize(this))
            return E_FAIL;

        CStringW Name;
        unz_file_info64 Info;
        while (zipEnum.Next(Name, Info))
        {
            ZipDataItem Item;
            Item.Name = Name;
            Item.Directory = Name.GetLength() > 0 && Name[Name.GetLength() - 1] == L'/';
            Item.Password = (Info.flag & MINIZIP_PASSWORD_FLAG) != 0;
            Item.HasDate = true;
            Item.DosDate = Info.dosDate;
            Item.Size = Info.uncompressed_size;
            if (unzGetFilePos64(m_Zip, &Item.Pos) != UNZ_OK)
                return E_FAIL;
            Entries.Add(Item);
        }
        return S_OK;
    }

    HRESULT Render()
    {
        if (m_Rendered)
            return S_OK;

        m_Zip = unzOpen2_64(m_ZipFile.GetString(), &g_FFunc);
        if (!m_Zip)
            return STG_E_FILENOTFOUND;

        CAtlArray<ZipDataItem> Entries;
        HRESULT hr = ReadEntries(Entries);
        unzClose(m_Zip);
        m_Zip = NULL;
        if (FAILED(hr))
            return hr;

        m_Items.SetCount(0);
        for (size_t s = 0; s < m_Selection.GetCount(); ++s)
        {
            const ZipDataSelection &Selected = m_Selection[s];
            CStringW Path = m_ZipDir + Selected.Name;
            if (!Selected.Directory)
            {
                for (size_t i = 0; i < Entries.GetCount(); ++i)
                {
                    if (Entries[i].Directory || Entries[i].Name.CompareNoCase(Path) != 0)
                        continue;
                    ZipDataItem Item = Entries[i];
                    Item.Name = Selected.Name;
                    m_Items.Add(Item);
                    break;
                }
                continue;
            }

            Path += L'/';
            ZipDataItem Folder = {};
            Folder.Name = Selected.Name;
            Folder.Directory = true;
            size_t Top = m_Items.Add(Folder);
            for (size_t i = 0; i < Entries.GetCount(); ++i)
            {
                const ZipDataItem &Entry = Entries[i];
                if (Entry.Name.GetLength() < Path.GetLength() ||
                    StrCmpNIW(Entry.Name, Path, Path.GetLength()) != 0)
                {
                    continue;
                }
                if (Entry.Name.GetLength() == Path.GetLength())
                {
                    m_Items[Top].HasDate = true;
                    m_Items[Top].DosDate = Entry.DosDate;
                    continue;
                }

                ZipDataItem Item = Entry;
                Item.Name = Selected.Name;
                Item.Name += L'\\';
                Item.Name += Entry.Name.Mid(Path.GetLength());
                Item.Name.Replace(L'/', L'\\');
                Item.Name.TrimRight(L'\\');
                m_Items.Add(Item);
            }
        }

        if (m_Items.GetCount() == 0)
            return E_FAIL;

        m_Rendered = true;
        return S_OK;
    }

    HRESULT GetDescriptor(STGMEDIUM *pmedium)
    {
        HRESULT hr = Render();
        if (FAILED(hr))
            return hr;

        size_t Count = m_Items.GetCount();
        SIZE_T cb = FIELD_OFFSET(FILEGROUPDESCRIPTORW, fgd) + Count * sizeof(FILEDESCRIPTORW);
        HGLOBAL hGlobal = GlobalAlloc(GHND, cb);
        if (!hGlobal)
            return E_OUTOFMEMORY;

        FILEGROUPDESCRIPTORW *pGroup = (FILEGROUPDESCRIPTORW*)GlobalLock(hGlobal);
        if (!pGroup)
        {
            GlobalFree(hGlobal);
            return E_OUTOFMEMORY;
        }

        hr = S_OK;
        pGroup->cItems = (UINT)Count;
        for (size_t i = 0; i < Count; ++i)
        {
            const ZipDataItem &Item = m_Items[i];
            FILEDESCRIPTORW &Descriptor = pGroup->fgd[i];

            Descriptor.dwFlags = FD_ATTRIBUTES | FD_FILESIZE | FD_PROGRESSUI;
            if (Item.Directory)
            {
                Descriptor.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
            }
            else
            {
                Descriptor.dwFileAttributes = FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_VIRTUAL;
                Descriptor.nFileSizeHigh = (DWORD)(Item.Size >> 32);
                Descriptor.nFileSizeLow = (DWORD)Item.Size;
            }
            if (Item.HasDate && ZipDateToFileTime(Item.DosDate, &Descriptor.ftLastWriteTime))
                Descriptor.dwFlags |= FD_WRITESTIME;

            hr = StringCchCopyW(Descriptor.cFileName, _countof(Descriptor.cFileName), Item.Name);
            if (FAILED(hr))
            {
                hr = HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
                break;
            }
        }
        GlobalUnlock(hGlobal);

        if (FAILED(hr))
        {
            GlobalFree(hGlobal);
            return hr;
        }

        pmedium->tymed = TYMED_HGLOBAL;
        pmedium->hGlobal = hGlobal;
        return S_OK;
    }

    HRESULT CreateStream(const ZipDataItem &Item, IStream **ppStream)
    {
        for (;;)
        {
            if (!Item.Password || !m_Password.IsEmpty())
            {
                HRESULT hr = ShellObjectCreatorInit<CZipStream>(m_ZipFile.GetString(), &Item, m_Password.GetString(),
                                                                IID_PPV_ARG(IStream, ppStream));
                if (hr != HRESULT_FROM_WIN32(ERROR_INVALID_PASSWORD))
                    return hr;
            }

            if (_CZipAskPassword(m_hwnd, NULL, m_Password) != eAccept)
                return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        }
    }

    HRESULT GetContents(LONG lindex, STGMEDIUM *pmedium)
    {
        HRESULT hr = Render();
        if (FAILED(hr))
            return hr;

        if (lindex < 0 || (size_t)lindex >= m_Items.GetCount())
            return E_INVALIDARG;

        const ZipDataItem &Item = m_Items[lindex];
        if (Item.Directory)
            return E_FAIL;

        hr = CreateStream(Item, &pmedium->pstm);
        if (FAILED(hr))
            return hr;

        pmedium->tymed = TYMED_ISTREAM;
        return S_OK;
    }

public:
    ~CZipDataObject()
    {
        if (m_Zip)
            unzClose(m_Zip);
    }

    HRESULT Initialize(PCWSTR ZipFile, PCWSTR ZipDir, HWND hwnd, PCIDLIST_ABSOLUTE pidlFolder,
                       UINT cidl, PCUITEMID_CHILD_ARRAY apidl)
    {
        m_ZipFile = ZipFile;
        m_ZipDir = ZipDir;
        m_hwnd = hwnd;
        m_cfDescriptor = (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW);
        m_cfContents = (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_FILECONTENTSW);

        for (UINT i = 0; i < cidl; ++i)
        {
            const ZipPidlEntry *Entry = _ZipFromIL(apidl[i]);
            if (!Entry)
                return E_INVALIDARG;

            ZipDataSelection Selected;
            Selected.Name = Entry->Name;
            Selected.Directory = Entry->IsDirectory();
            m_Selection.Add(Selected);
        }

        return CIDLData_CreateFromIDArray(pidlFolder, cidl, apidl, &m_Inner);
    }

    // *** IZip methods ***
    STDMETHODIMP_(unzFile) getZip() override
    {
        return m_Zip;
    }

    // *** IDataObject methods ***
    STDMETHODIMP GetData(FORMATETC *pformatetcIn, STGMEDIUM *pmedium) override
    {
        if (!pformatetcIn || !pmedium)
            return E_INVALIDARG;

        if (pformatetcIn->cfFormat == m_cfDescriptor)
        {
            ZeroMemory(pmedium, sizeof(*pmedium));
            if (!(pformatetcIn->tymed & TYMED_HGLOBAL))
                return DV_E_TYMED;
            return GetDescriptor(pmedium);
        }
        if (pformatetcIn->cfFormat == m_cfContents)
        {
            ZeroMemory(pmedium, sizeof(*pmedium));
            if (!(pformatetcIn->tymed & TYMED_ISTREAM))
                return DV_E_TYMED;
            return GetContents(pformatetcIn->lindex, pmedium);
        }
        return m_Inner->GetData(pformatetcIn, pmedium);
    }
    STDMETHODIMP GetDataHere(FORMATETC *pformatetc, STGMEDIUM *pmedium) override
    {
        return E_NOTIMPL;
    }
    STDMETHODIMP QueryGetData(FORMATETC *pformatetc) override
    {
        if (!pformatetc)
            return E_INVALIDARG;

        if (pformatetc->cfFormat == m_cfDescriptor)
            return (pformatetc->tymed & TYMED_HGLOBAL) ? S_OK : DV_E_TYMED;
        if (pformatetc->cfFormat == m_cfContents)
            return (pformatetc->tymed & TYMED_ISTREAM) ? S_OK : DV_E_TYMED;
        return m_Inner->QueryGetData(pformatetc);
    }
    STDMETHODIMP GetCanonicalFormatEtc(FORMATETC *pformatectIn, FORMATETC *pformatetcOut) override
    {
        return m_Inner->GetCanonicalFormatEtc(pformatectIn, pformatetcOut);
    }
    STDMETHODIMP SetData(FORMATETC *pformatetc, STGMEDIUM *pmedium, BOOL fRelease) override
    {
        return m_Inner->SetData(pformatetc, pmedium, fRelease);
    }
    STDMETHODIMP EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC **ppenumFormatEtc) override
    {
        if (dwDirection != DATADIR_GET)
            return m_Inner->EnumFormatEtc(dwDirection, ppenumFormatEtc);

        CAtlArray<FORMATETC> Formats;
        FORMATETC Descriptor = { m_cfDescriptor, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        FORMATETC Contents = { m_cfContents, NULL, DVASPECT_CONTENT, -1, TYMED_ISTREAM };
        Formats.Add(Descriptor);
        Formats.Add(Contents);

        CComPtr<IEnumFORMATETC> InnerEnum;
        if (SUCCEEDED(m_Inner->EnumFormatEtc(DATADIR_GET, &InnerEnum)))
        {
            FORMATETC Format;
            while (InnerEnum->Next(1, &Format, NULL) == S_OK)
                Formats.Add(Format);
        }

        return SHCreateStdEnumFmtEtc((UINT)Formats.GetCount(), Formats.GetData(), ppenumFormatEtc);
    }
    STDMETHODIMP DAdvise(FORMATETC *pformatetc, DWORD advf, IAdviseSink *pAdvSink, DWORD *pdwConnection) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    STDMETHODIMP DUnadvise(DWORD dwConnection) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    STDMETHODIMP EnumDAdvise(IEnumSTATDATA **ppenumAdvise) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    // *** IAsyncOperation methods ***
    STDMETHODIMP SetAsyncMode(BOOL fDoOpAsync) override
    {
        m_Async = fDoOpAsync;
        return S_OK;
    }
    STDMETHODIMP GetAsyncMode(BOOL *pfIsOpAsync) override
    {
        if (!pfIsOpAsync)
            return E_INVALIDARG;
        *pfIsOpAsync = m_Async;
        return S_OK;
    }
    STDMETHODIMP StartOperation(IBindCtx *pbcReserved) override
    {
        m_InOperation = TRUE;
        return S_OK;
    }
    STDMETHODIMP InOperation(BOOL *pfInAsyncOp) override
    {
        if (!pfInAsyncOp)
            return E_INVALIDARG;
        *pfInAsyncOp = m_InOperation;
        return S_OK;
    }
    STDMETHODIMP EndOperation(HRESULT hResult, IBindCtx *pbcReserved, DWORD dwEffects) override
    {
        m_InOperation = FALSE;
        return S_OK;
    }

public:
    DECLARE_NOT_AGGREGATABLE(CZipDataObject)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CZipDataObject)
        COM_INTERFACE_ENTRY_IID(IID_IDataObject, IDataObject)
        COM_INTERFACE_ENTRY_IID(IID_IAsyncOperation, IAsyncOperation)
    END_COM_MAP()
};

HRESULT _CZipDataObject_CreateInstance(PCWSTR ZipFile, PCWSTR ZipDir, HWND hwnd, PCIDLIST_ABSOLUTE pidlFolder,
                                       UINT cidl, PCUITEMID_CHILD_ARRAY apidl, REFIID riid, LPVOID *ppvOut)
{
    _CComObject<CZipDataObject> *pObject;
    HRESULT hr = _CComObject<CZipDataObject>::CreateInstance(&pObject);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    pObject->AddRef();
    hr = pObject->Initialize(ZipFile, ZipDir, hwnd, pidlFolder, cidl, apidl);
    if (SUCCEEDED(hr))
        hr = pObject->QueryInterface(riid, ppvOut);
    pObject->Release();
    return hr;
}
