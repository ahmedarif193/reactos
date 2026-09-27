/*
 * PROJECT:         ReactOS user32.dll
 * FILE:            win32ss/user/user32/windows/clipboard.c
 * PURPOSE:         Input
 * PROGRAMMER:      Casper S. Hornstrup (chorns@users.sourceforge.net)
 *                  Pablo Borobia <pborobia@gmail.com>
 * UPDATE HISTORY:
 *      09-05-2001  CSH  Created
 *
 */

#include <user32.h>

#define NDEBUG

WINE_DEFAULT_DEBUG_CHANNEL(user32);

HANDLE WINAPI GdiConvertMetaFilePict(HANDLE);
HANDLE WINAPI GdiConvertEnhMetaFile(HANDLE);
HANDLE WINAPI GdiCreateLocalEnhMetaFile(HANDLE);
HANDLE WINAPI GdiCreateLocalMetaFilePict(HANDLE);

typedef struct _CLIPBOARD_CACHE_ENTRY
{
    LIST_ENTRY ListEntry;
    UINT uFormat;
    HANDLE hKernel;
    HANDLE hData;
} CLIPBOARD_CACHE_ENTRY, *PCLIPBOARD_CACHE_ENTRY;

static LIST_ENTRY ClipboardCache = { &ClipboardCache, &ClipboardCache };
static LIST_ENTRY ClipboardStale = { &ClipboardStale, &ClipboardStale };

static PCLIPBOARD_CACHE_ENTRY
IntFindCachedFormat(UINT uFormat)
{
    PLIST_ENTRY Entry;
    PCLIPBOARD_CACHE_ENTRY Cache;

    for (Entry = ClipboardCache.Flink; Entry != &ClipboardCache; Entry = Entry->Flink)
    {
        Cache = CONTAINING_RECORD(Entry, CLIPBOARD_CACHE_ENTRY, ListEntry);
        if (Cache->uFormat == uFormat)
            return Cache;
    }
    return NULL;
}

static BOOL
IntIsMetaFileFormat(UINT uFormat)
{
    return uFormat == CF_ENHMETAFILE || uFormat == CF_DSPENHMETAFILE ||
           uFormat == CF_METAFILEPICT || uFormat == CF_DSPMETAFILEPICT;
}

static VOID
IntFreeCachedData(UINT uFormat, HANDLE hData)
{
    METAFILEPICT *pmfp;

    switch (uFormat)
    {
        case CF_ENHMETAFILE:
        case CF_DSPENHMETAFILE:
            DeleteEnhMetaFile(hData);
            break;

        case CF_METAFILEPICT:
        case CF_DSPMETAFILEPICT:
            pmfp = GlobalLock(hData);
            if (pmfp)
            {
                DeleteMetaFile(pmfp->hMF);
                GlobalUnlock(hData);
            }
            GlobalFree(hData);
            break;

        default:
            GlobalFree(hData);
            break;
    }
}

static VOID
IntFreeCachedList(PLIST_ENTRY ListHead, BOOL bMemoryOnly)
{
    PLIST_ENTRY Entry, Next;
    PCLIPBOARD_CACHE_ENTRY Cache;

    for (Entry = ListHead->Flink; Entry != ListHead; Entry = Next)
    {
        Next = Entry->Flink;
        Cache = CONTAINING_RECORD(Entry, CLIPBOARD_CACHE_ENTRY, ListEntry);
        if (bMemoryOnly && IntIsMetaFileFormat(Cache->uFormat))
            continue;
        RemoveEntryList(&Cache->ListEntry);
        IntFreeCachedData(Cache->uFormat, Cache->hData);
        HeapFree(GetProcessHeap(), 0, Cache);
    }
}

static VOID
IntCacheFormat(UINT uFormat, HANDLE hKernel, HANDLE hData, BOOL bReplace)
{
    PCLIPBOARD_CACHE_ENTRY Cache, Prev;

    Prev = IntFindCachedFormat(uFormat);
    if (Prev)
    {
        RemoveEntryList(&Prev->ListEntry);
        if (!bReplace)
        {
            InsertTailList(&ClipboardStale, &Prev->ListEntry);
        }
        else
        {
            if (Prev->hData != hData)
                IntFreeCachedData(Prev->uFormat, Prev->hData);
            HeapFree(GetProcessHeap(), 0, Prev);
        }
    }

    if (!hData)
        return;

    Cache = HeapAlloc(GetProcessHeap(), 0, sizeof(*Cache));
    if (!Cache)
        return;
    Cache->uFormat = uFormat;
    Cache->hKernel = hKernel;
    Cache->hData = hData;
    InsertTailList(&ClipboardCache, &Cache->ListEntry);
}


/*
 * @implemented
 */
BOOL
WINAPI
OpenClipboard(HWND hWndNewOwner)
{
    HWND hwndOwner;
    DWORD dwProcessId = 0;

    if (!NtUserOpenClipboard(hWndNewOwner, 0))
        return FALSE;

    hwndOwner = NtUserGetClipboardOwner();
    if (!hwndOwner ||
        !GetWindowThreadProcessId(hwndOwner, &dwProcessId) ||
        dwProcessId != GetCurrentProcessId())
    {
        IntFreeCachedList(&ClipboardCache, TRUE);
    }
    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
EmptyClipboard(VOID)
{
    if (!NtUserEmptyClipboard())
        return FALSE;

    IntFreeCachedList(&ClipboardStale, FALSE);
    IntFreeCachedList(&ClipboardCache, FALSE);
    return TRUE;
}

/*
 * @implemented
 */
UINT
WINAPI
EnumClipboardFormats(UINT format)
{
    SetLastError(NO_ERROR);
    return NtUserxEnumClipboardFormats(format);
}

/*
 * @implemented
 */
INT
WINAPI
GetClipboardFormatNameA(UINT format,
                        LPSTR lpszFormatName,
                        int cchMaxCount)
{
    LPWSTR lpBuffer;
    INT Length;

    lpBuffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, cchMaxCount * sizeof(WCHAR));
    if (!lpBuffer)
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return 0;
    }

    /* we need a UNICODE string */
    Length = NtUserGetClipboardFormatName(format, lpBuffer, cchMaxCount);

    if (Length != 0)
    {
        if (!WideCharToMultiByte(CP_ACP, 0, lpBuffer, Length, lpszFormatName, cchMaxCount, NULL, NULL))
        {
            /* clear result string */
            Length = 0;
        }
        lpszFormatName[Length] = ANSI_NULL;
    }

    RtlFreeHeap(RtlGetProcessHeap(), 0, lpBuffer);
    return Length;
}

/*
 * @implemented
 */
INT
WINAPI
GetClipboardFormatNameW(UINT uFormat,
                        LPWSTR lpszFormatName,
                        INT cchMaxCount)
{
    return NtUserGetClipboardFormatName(uFormat, lpszFormatName, cchMaxCount);
}

/*
 * @implemented
 */
UINT
WINAPI
RegisterClipboardFormatA(LPCSTR lpszFormat)
{
    UINT ret;
    UNICODE_STRING usFormat = {0};

    if (lpszFormat == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    if (*lpszFormat == ANSI_NULL)
    {
        return 0;
    }

    if (!RtlCreateUnicodeStringFromAsciiz(&usFormat, lpszFormat))
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }

    ret = NtUserRegisterWindowMessage(&usFormat); //(LPCWSTR)

    RtlFreeUnicodeString(&usFormat);

    return ret;
}

/*
 * @implemented
 */
UINT
WINAPI
RegisterClipboardFormatW(LPCWSTR lpszFormat)
{
    UNICODE_STRING usFormat = {0};

    if (lpszFormat == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    if (*lpszFormat == UNICODE_NULL)
    {
        return 0;
    }

    RtlInitUnicodeString(&usFormat, lpszFormat);
    return NtUserRegisterWindowMessage(&usFormat);
}

static DWORD
IntDibHeaderSize(const BITMAPINFO *pbmi)
{
    DWORD cColors, cMasks = 0, cbHeader;

    if (pbmi->bmiHeader.biSize == sizeof(BITMAPCOREHEADER))
    {
        const BITMAPCOREHEADER *pbch = (const BITMAPCOREHEADER *)pbmi;

        cColors = (pbch->bcBitCount <= 8) ? 1 << pbch->bcBitCount : 0;
        return sizeof(BITMAPCOREHEADER) + cColors * sizeof(RGBTRIPLE);
    }

    cColors = min(pbmi->bmiHeader.biClrUsed, 256);
    if (!cColors && pbmi->bmiHeader.biBitCount <= 8)
        cColors = 1 << pbmi->bmiHeader.biBitCount;
    if (pbmi->bmiHeader.biCompression == BI_BITFIELDS)
        cMasks = 3;
    cbHeader = max(pbmi->bmiHeader.biSize, sizeof(BITMAPINFOHEADER) + cMasks * sizeof(DWORD));
    return cbHeader + cColors * sizeof(RGBQUAD);
}

static PVOID WINAPI
IntSynthesizeDib(PVOID pSrc, DWORD cbSrc, UINT uFormat)
{
    const BITMAPINFO *pbmiSrc = pSrc;
    DWORD cbSrcHeader, cbHeader, cbBits;
    HANDLE hGlobal;
    PBYTE pGlobal;

    if (!pSrc || cbSrc < sizeof(BITMAPINFOHEADER))
        return NULL;

    cbSrcHeader = IntDibHeaderSize(pbmiSrc);
    if (cbSrc <= cbSrcHeader)
        return NULL;

    cbBits = cbSrc - cbSrcHeader;
    cbHeader = (uFormat == CF_DIBV5) ? sizeof(BITMAPV5HEADER) :
               FIELD_OFFSET(BITMAPINFO, bmiColors[pbmiSrc->bmiHeader.biCompression == BI_BITFIELDS ? 3 : 0]);

    hGlobal = GlobalAlloc(GMEM_FIXED, cbHeader + cbBits);
    if (!hGlobal)
        return NULL;

    pGlobal = GlobalLock(hGlobal);
    RtlZeroMemory(pGlobal, cbHeader);
    RtlCopyMemory(pGlobal, pbmiSrc, min(cbHeader, cbSrcHeader));
    ((BITMAPINFOHEADER *)pGlobal)->biSize = cbHeader;
    RtlCopyMemory(pGlobal + cbHeader, (const BYTE *)pbmiSrc + cbSrcHeader, cbBits);
    return pGlobal;
}

static PVOID WINAPI
IntSynthesizeMultiByte(PVOID pwStr, DWORD cbStr, BOOL bOem)
{
    HANDLE hGlobal;
    PVOID pGlobal;
    INT cbGlobal;

    cbGlobal = WideCharToMultiByte(bOem ? CP_OEMCP : CP_ACP,
                                0, pwStr, cbStr / sizeof(WCHAR),
                                NULL, 0, NULL, NULL);
    hGlobal = GlobalAlloc(GMEM_FIXED, cbGlobal);
    if (!hGlobal)
        return NULL;

    pGlobal = GlobalLock(hGlobal);
    WideCharToMultiByte(bOem ? CP_OEMCP : CP_ACP,
                        0, pwStr, cbStr / sizeof(WCHAR),
                        pGlobal, cbGlobal, NULL, NULL);
    if (cbGlobal)
        ((PCHAR)pGlobal)[cbGlobal - 1] = 0;
    return pGlobal;
}

static PVOID WINAPI
IntSynthesizeWideChar(PVOID pwStr, DWORD cbStr, BOOL bOem)
{
    HANDLE hGlobal;
    PVOID pGlobal;
    INT cbGlobal;

    cbGlobal = MultiByteToWideChar(bOem ? CP_OEMCP : CP_ACP,
                                   0, pwStr, cbStr, NULL, 0) * sizeof(WCHAR);
    hGlobal = GlobalAlloc(GMEM_FIXED, cbGlobal);
    if (!hGlobal)
        return NULL;

    pGlobal = GlobalLock(hGlobal);
    MultiByteToWideChar(bOem ? CP_OEMCP : CP_ACP,
                        0, pwStr, cbStr, pGlobal, cbGlobal);
    return pGlobal;
}

/*
 * @implemented
 */
HANDLE
WINAPI
GetClipboardData(UINT uFormat)
{
    HANDLE hData = NULL, hKernel;
    PVOID pData = NULL;
    DWORD cbData = 0;
    GETCLIPBDATA gcd;
    PCLIPBOARD_CACHE_ENTRY Cache;

    hData = NtUserGetClipboardData(uFormat, &gcd);
    if (!hData)
        return NULL;
    hKernel = hData;

    if (IntIsMetaFileFormat(uFormat))
    {
        Cache = IntFindCachedFormat(uFormat);
        if (Cache && Cache->hKernel == hKernel)
            return Cache->hData;

        if (uFormat == CF_METAFILEPICT || uFormat == CF_DSPMETAFILEPICT)
            hData = GdiCreateLocalMetaFilePict(hKernel);
        else
            hData = GdiCreateLocalEnhMetaFile(hKernel);

        if (hData)
            IntCacheFormat(uFormat, hKernel, hData, FALSE);
        return hData;
    }

    if (gcd.fGlobalHandle && gcd.uFmtRet == uFormat)
    {
        Cache = IntFindCachedFormat(uFormat);
        if (Cache && Cache->hKernel == hKernel)
            return Cache->hData;
    }

    if (gcd.fGlobalHandle)
    {
        HANDLE hGlobal;

        NtUserCreateLocalMemHandle(hData, NULL, 0, &cbData);
        hGlobal = GlobalAlloc(GMEM_FIXED, cbData);
        pData = GlobalLock(hGlobal);
        NtUserCreateLocalMemHandle(hData, pData, cbData, NULL);
        hData = hGlobal;
    }

    if (gcd.uFmtRet != uFormat)
    {
        SETCLIPBDATA scd = {FALSE, FALSE};
        HANDLE hNewData = NULL;
        PVOID pNewData = NULL;

        /* Synthesize requested format */
        switch (uFormat)
        {
            case CF_TEXT:
                if (gcd.uFmtRet == CF_UNICODETEXT)
                    pNewData = IntSynthesizeMultiByte(pData, cbData, uFormat == CF_OEMTEXT);
                else // CF_OEMTEXT
                    OemToCharBuffA(pData, pData, cbData);
                break;
            case CF_OEMTEXT:
                if (gcd.uFmtRet == CF_UNICODETEXT)
                    pNewData = IntSynthesizeMultiByte(pData, cbData, uFormat == CF_OEMTEXT);
                else
                    CharToOemBuffA(pData, pData, cbData);
                break;
            case CF_UNICODETEXT:
                pNewData = IntSynthesizeWideChar(pData, cbData, gcd.uFmtRet == CF_OEMTEXT);
                break;
            case CF_DIB:
            case CF_DIBV5:
                pNewData = IntSynthesizeDib(pData, cbData, uFormat);
                break;
            default:
                FIXME("Format: %u != %u\n", uFormat, gcd.uFmtRet);
        }

        /* Is it a global handle? */
        if (pNewData)
            hNewData = GlobalHandle(pNewData);

        if (hNewData)
        {
            /* Free old data */
            if (pData)
            {
                GlobalUnlock(hData);
                GlobalFree(hData);
            }
            hData = hNewData;
            pData = pNewData;
        }

        /* Save synthesized format in clipboard */
        if (pData)
        {
            HANDLE hMem;

            scd.fGlobalHandle = TRUE;
            hMem = NtUserConvertMemHandle(pData, GlobalSize(hData));
            if (hMem && NtUserSetClipboardData(uFormat, hMem, &scd))
                IntCacheFormat(uFormat, hMem, hData, FALSE);
        }
        else if (hData)
            NtUserSetClipboardData(uFormat, hData, &scd);
    }
    else if (gcd.fGlobalHandle)
    {
        IntCacheFormat(uFormat, hKernel, hData, FALSE);
    }

    /* Unlock global handle */
    if (pData)
        GlobalUnlock(hData);

    return hData;
}

/*
 * @implemented
 */
HANDLE
WINAPI
SetClipboardData(UINT uFormat, HANDLE hMem)
{
    DWORD dwSize;
    HANDLE hGlobal;
    LPVOID pMem;
    HANDLE hRet = NULL, hTemp;
    SETCLIPBDATA scd = {FALSE, TRUE};

    /* Check if this is a delayed rendering */
    if (hMem == NULL)
    {
        hRet = NtUserSetClipboardData(uFormat, NULL, &scd);
        if (hRet)
            IntCacheFormat(uFormat, NULL, NULL, TRUE);
        return hRet;
    }

    if (hMem <= (HANDLE)4)
        SetLastError(ERROR_INVALID_PARAMETER);
    /* Bitmaps and palette does not use global handles */
    else if (uFormat == CF_BITMAP || uFormat == CF_DSPBITMAP || uFormat == CF_PALETTE)
        hRet = NtUserSetClipboardData(uFormat, hMem, &scd);
    /* Meta files are probably checked for validity */
    else if (uFormat == CF_DSPMETAFILEPICT || uFormat == CF_METAFILEPICT )
    {
        hTemp = GdiConvertMetaFilePict( hMem );
        hRet = NtUserSetClipboardData(uFormat, hTemp, &scd); // Note : LOL, it returns a BOOL not a HANDLE!!!!
        if (hRet == hTemp)                                   // If successful "TRUE", return the original handle.
        {
            hRet = hMem;
            IntCacheFormat(uFormat, hTemp, hMem, TRUE);
        }
    }
    else if (uFormat == CF_DSPENHMETAFILE || uFormat == CF_ENHMETAFILE)
    {
        hTemp = GdiConvertEnhMetaFile( hMem );
        hRet = NtUserSetClipboardData(uFormat, hTemp, &scd);
        if (hRet == hTemp)
        {
            hRet = hMem;
            IntCacheFormat(uFormat, hTemp, hMem, TRUE);
        }
    }
    else
    {
        /* Some formats accept only global handles, other accept global handles or integer values */
        pMem = GlobalLock(hMem);
        dwSize = GlobalSize(hMem);

        if (pMem || uFormat == CF_DIB || uFormat == CF_DIBV5 ||
            uFormat == CF_DSPTEXT || uFormat == CF_LOCALE ||
            uFormat == CF_OEMTEXT || uFormat == CF_TEXT ||
            uFormat == CF_UNICODETEXT)
        {
            if (pMem && uFormat == CF_UNICODETEXT && dwSize < sizeof(WCHAR))
            {
                GlobalUnlock(hMem);
                pMem = NULL;
            }
            else if (pMem && dwSize && (uFormat == CF_TEXT || uFormat == CF_OEMTEXT))
            {
                ((PCHAR)pMem)[dwSize - 1] = 0;
            }
            else if (pMem && uFormat == CF_UNICODETEXT)
            {
                *((WCHAR *)((PCHAR)pMem + dwSize) - 1) = 0;
            }

            if (pMem)
            {
                /* This is a local memory. Make global memory object */
                hGlobal = NtUserConvertMemHandle(pMem, dwSize);

                /* Unlock memory */
                GlobalUnlock(hMem);
                /* FIXME: free hMem when CloseClipboard is called */

                if (hGlobal)
                {
                    /* Save data */
                    scd.fGlobalHandle = TRUE;
                    hRet = NtUserSetClipboardData(uFormat, hGlobal, &scd);
                }

                /* On success NtUserSetClipboardData returns pMem
                   however caller expects us to return hMem */
                if (hRet == hGlobal)
                {
                    hRet = hMem;
                    IntCacheFormat(uFormat, hGlobal, hMem, TRUE);
                }
            }
            else
                SetLastError(ERROR_INVALID_HANDLE);
        }
        else if ((GlobalFlags(hMem) & (GMEM_DISCARDED | GMEM_INVALID_HANDLE)) == GMEM_DISCARDED)
        {
            SetLastError(ERROR_INVALID_HANDLE);
        }
        else
        {
            /* Save a number */
            hRet = NtUserSetClipboardData(uFormat, hMem, &scd);
        }
    }

    if (!hRet)
        ERR("SetClipboardData(%u, %p) failed\n", uFormat, hMem);
    else if (!IntFindCachedFormat(uFormat) || IntFindCachedFormat(uFormat)->hData != hMem)
        IntCacheFormat(uFormat, NULL, NULL, TRUE);

    return hRet;
}

/*
 * @implemented
 */
BOOL
WINAPI
AddClipboardFormatListener(HWND hwnd)
{
    return NtUserAddClipboardFormatListener(hwnd);
}
/*
 * @implemented
 */
BOOL
WINAPI
RemoveClipboardFormatListener(HWND hwnd)
{
    return NtUserRemoveClipboardFormatListener(hwnd);
}

/*
 * @unimplemented
 */
BOOL
WINAPI
GetUpdatedClipboardFormats(PUINT lpuiFormats,
                           UINT cFormats,
                           PUINT pcFormatsOut)
{
    UNIMPLEMENTED;
    return FALSE;
}
