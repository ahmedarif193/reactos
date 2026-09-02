#include <precomp.h>

#define NDEBUG
#include <debug.h>


typedef struct _GDI_COLORSPACE_RECORD
{
    LIST_ENTRY Entry;
    HCOLORSPACE hCS;
    LOGCOLORSPACEW lcs;
} GDI_COLORSPACE_RECORD, *PGDI_COLORSPACE_RECORD;

static LIST_ENTRY g_ColorSpaceList = { &g_ColorSpaceList, &g_ColorSpaceList };
static RTL_CRITICAL_SECTION g_ColorSpaceLock;
static BOOL g_ColorSpaceLockInit;

static VOID
IntColorSpaceLock(VOID)
{
    if (!g_ColorSpaceLockInit)
    {
        RtlEnterCriticalSection(&semLocal);
        if (!g_ColorSpaceLockInit)
        {
            RtlInitializeCriticalSection(&g_ColorSpaceLock);
            g_ColorSpaceLockInit = TRUE;
        }
        RtlLeaveCriticalSection(&semLocal);
    }
    RtlEnterCriticalSection(&g_ColorSpaceLock);
}

static VOID
IntColorSpaceUnlock(VOID)
{
    RtlLeaveCriticalSection(&g_ColorSpaceLock);
}

static VOID
IntColorSpaceRemember(HCOLORSPACE hCS, LPLOGCOLORSPACEW lplcpw)
{
    PGDI_COLORSPACE_RECORD pRec;

    pRec = RtlAllocateHeap(RtlGetProcessHeap(), 0, sizeof(*pRec));
    if (!pRec)
        return;
    pRec->hCS = hCS;
    pRec->lcs = *lplcpw;
    IntColorSpaceLock();
    InsertHeadList(&g_ColorSpaceList, &pRec->Entry);
    IntColorSpaceUnlock();
}

static VOID
IntColorSpaceForget(HCOLORSPACE hCS)
{
    PLIST_ENTRY pEntry;
    PGDI_COLORSPACE_RECORD pRec;

    IntColorSpaceLock();
    for (pEntry = g_ColorSpaceList.Flink; pEntry != &g_ColorSpaceList; pEntry = pEntry->Flink)
    {
        pRec = CONTAINING_RECORD(pEntry, GDI_COLORSPACE_RECORD, Entry);
        if (pRec->hCS == hCS)
        {
            RemoveEntryList(&pRec->Entry);
            RtlFreeHeap(RtlGetProcessHeap(), 0, pRec);
            break;
        }
    }
    IntColorSpaceUnlock();
}

BOOL
FASTCALL
IntGetLogColorSpaceW(HCOLORSPACE hCS, LPLOGCOLORSPACEW lplcpw)
{
    PLIST_ENTRY pEntry;
    PGDI_COLORSPACE_RECORD pRec;
    BOOL bFound = FALSE;

    if (GDI_HANDLE_GET_TYPE(hCS) != GDILoObjType_LO_ICMLCS_TYPE || !GdiValidateHandle(hCS))
        return FALSE;

    IntColorSpaceLock();
    for (pEntry = g_ColorSpaceList.Flink; pEntry != &g_ColorSpaceList; pEntry = pEntry->Flink)
    {
        pRec = CONTAINING_RECORD(pEntry, GDI_COLORSPACE_RECORD, Entry);
        if (pRec->hCS == hCS)
        {
            *lplcpw = pRec->lcs;
            bFound = TRUE;
            break;
        }
    }
    IntColorSpaceUnlock();

    if (!bFound)
    {
        RtlZeroMemory(lplcpw, sizeof(*lplcpw));
        lplcpw->lcsSignature = LCS_SIGNATURE;
        lplcpw->lcsVersion = 0x400;
        lplcpw->lcsSize = sizeof(LOGCOLORSPACEW);
        lplcpw->lcsCSType = LCS_sRGB;
        lplcpw->lcsIntent = LCS_GM_IMAGES;
        wcscpy(lplcpw->lcsFilename, L"sRGB Color Space Profile.icm");
    }
    return TRUE;
}

BOOL
WINAPI
DeleteColorSpace(HCOLORSPACE hCS)
{
    BOOL bRet = NtGdiDeleteColorSpace(hCS);
    if (bRet)
        IntColorSpaceForget(hCS);
    return bRet;
}

HCOLORSPACE
FASTCALL
IntCreateColorSpaceW(
    LPLOGCOLORSPACEW lplcpw,
    BOOL Ascii
)
{
    LOGCOLORSPACEEXW lcpeexw;
    HCOLORSPACE hCS;

    if ((lplcpw->lcsSignature != LCS_SIGNATURE) ||
            (lplcpw->lcsVersion != 0x400) ||
            (lplcpw->lcsSize != sizeof(LOGCOLORSPACEW)))
    {
        SetLastError(ERROR_INVALID_COLORSPACE);
        return NULL;
    }
    RtlCopyMemory(&lcpeexw.lcsColorSpace, lplcpw, sizeof(LOGCOLORSPACEW));
    lcpeexw.dwFlags = 0;
    hCS = NtGdiCreateColorSpace(&lcpeexw);
    if (hCS)
        IntColorSpaceRemember(hCS, lplcpw);
    return hCS;
}

/*
 * @implemented
 */
HCOLORSPACE
WINAPI
CreateColorSpaceW(
    LPLOGCOLORSPACEW lplcpw
)
{
    return IntCreateColorSpaceW(lplcpw, FALSE);
}


/*
 * @implemented
 */
HCOLORSPACE
WINAPI
CreateColorSpaceA(
    LPLOGCOLORSPACEA lplcpa
)
{
    LOGCOLORSPACEW lcpw;

    if ((lplcpa->lcsSignature != LCS_SIGNATURE) ||
            (lplcpa->lcsVersion != 0x400) ||
            (lplcpa->lcsSize != sizeof(LOGCOLORSPACEA)))
    {
        SetLastError(ERROR_INVALID_COLORSPACE);
        return NULL;
    }

    lcpw.lcsSignature  = lplcpa->lcsSignature;
    lcpw.lcsVersion    = lplcpa->lcsVersion;
    lcpw.lcsSize       = sizeof(LOGCOLORSPACEW);
    lcpw.lcsCSType     = lplcpa->lcsCSType;
    lcpw.lcsIntent     = lplcpa->lcsIntent;
    lcpw.lcsEndpoints  = lplcpa->lcsEndpoints;
    lcpw.lcsGammaRed   = lplcpa->lcsGammaRed;
    lcpw.lcsGammaGreen = lplcpa->lcsGammaGreen;
    lcpw.lcsGammaBlue  = lplcpa->lcsGammaBlue;

    RtlMultiByteToUnicodeN( lcpw.lcsFilename,
                            MAX_PATH,
                            NULL,
                            lplcpa->lcsFilename,
                            strlen(lplcpa->lcsFilename) + 1);

    return IntCreateColorSpaceW(&lcpw, FALSE);
}

/*
 * @implemented
 */
HCOLORSPACE
WINAPI
GetColorSpace(HDC hDC)
{
    PDC_ATTR pDc_Attr;

    if (!GdiGetHandleUserData(hDC, GDI_OBJECT_TYPE_DC, (PVOID)&pDc_Attr))
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return NULL;
    }
    return pDc_Attr->hColorSpace;
}


/*
 * @implemented
 */
HCOLORSPACE
WINAPI
SetColorSpace(
    HDC hDC,
    HCOLORSPACE hCS
)
{
    HCOLORSPACE rhCS = GetColorSpace(hDC);

    if (GDI_HANDLE_GET_TYPE(hDC) == GDILoObjType_LO_DC_TYPE)
    {
        if (NtGdiSetColorSpace(hDC, hCS)) return rhCS;
    }
#if 0
    if (GDI_HANDLE_GET_TYPE(hDC) != GDILoObjType_LO_METADC16_TYPE)
    {
        PLDC pLDC = GdiGetLDC(hDC);
        if ( !pLDC )
        {
            SetLastError(ERROR_INVALID_HANDLE);
            return NULL;
        }
        if (pLDC->iType == LDC_EMFLDC && !EMFDC_SetColorSpace( pLDC, hCS ))
        {
            return NULL;
        }
    }
#endif
    return NULL;
}


/*
 * @unimplemented
 */
BOOL
WINAPI
GetICMProfileA(
    HDC		hdc,
    LPDWORD pBufSize,
    LPSTR		pszFilename
)
{
    WCHAR filenameW[MAX_PATH];
    DWORD buflen = MAX_PATH;
    BOOL ret = FALSE;

    if (!hdc || !pBufSize) return FALSE;

    if (GetICMProfileW(hdc, &buflen, filenameW))
    {
        ULONG len = WideCharToMultiByte(CP_ACP, 0, filenameW, -1, NULL, 0, NULL, NULL);

        if (!pszFilename)
        {
            *pBufSize = len;
            return FALSE;
        }

        if (*pBufSize >= len)
        {
            WideCharToMultiByte(CP_ACP, 0, filenameW, -1, pszFilename, *pBufSize, NULL, NULL);
            ret = TRUE;
        }
        else SetLastError(ERROR_INSUFFICIENT_BUFFER);
        *pBufSize = len;
    }

    return ret;
}


/*
 * @implemented
 */
BOOL
WINAPI
GetICMProfileW(
    HDC		hdc,
    LPDWORD		size,
    LPWSTR		filename
)
{
    WCHAR wszProfile[MAX_PATH];
    DWORD cchProfile;

    if (!hdc || !size) return FALSE;

    /* We report the default sRGB profile without checking whether the
       file actually exists, like Windows does */
    if (!GetSystemDirectoryW(wszProfile, _countof(wszProfile)))
        return FALSE;

    cchProfile = lstrlenW(wszProfile);
    if (cchProfile + sizeof("\\spool\\drivers\\color\\sRGB Color Space Profile.icm") >
        _countof(wszProfile))
        return FALSE;

    lstrcatW(wszProfile, L"\\spool\\drivers\\color\\sRGB Color Space Profile.icm");
    cchProfile = lstrlenW(wszProfile) + 1;

    if (!filename)
    {
        *size = cchProfile;
        return FALSE;
    }

    if (*size >= cchProfile)
    {
        lstrcpyW(filename, wszProfile);
        *size = cchProfile;
        return TRUE;
    }

    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    *size = cchProfile;
    return FALSE;
}


/*
 * @unimplemented
 */
BOOL
WINAPI
SetICMProfileA(
    HDC	a0,
    LPSTR	a1
)
{
    UNIMPLEMENTED;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}


/*
 * @unimplemented
 */
BOOL
WINAPI
SetICMProfileW(
    HDC	a0,
    LPWSTR	a1
)
{
    UNIMPLEMENTED;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}


/*
 * @unimplemented
 */
int
WINAPI
EnumICMProfilesA(
    HDC		a0,
    ICMENUMPROCA	a1,
    LPARAM		a2
)
{
    /*
     * FIXME - call NtGdiEnumICMProfiles with NULL for lpstrBuffer
     * to find out how big a buffer we need. Then allocate that buffer
     * and call NtGdiEnumICMProfiles again to have the buffer filled.
     *
     * Finally, step through the buffer ( MULTI-SZ recommended for format ),
     * and convert each string to ANSI, calling the user's callback function
     * until we run out of strings or the user returns FALSE
     */

    UNIMPLEMENTED;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}


/*
 * @unimplemented
 */
int
WINAPI
EnumICMProfilesW(
    HDC		hDC,
    ICMENUMPROCW	lpEnumICMProfilesFunc,
    LPARAM		lParam
)
{
    /*
     * FIXME - call NtGdiEnumICMProfiles with NULL for lpstrBuffer
     * to find out how big a buffer we need. Then allocate that buffer
     * and call NtGdiEnumICMProfiles again to have the buffer filled.
     *
     * Finally, step through the buffer ( MULTI-SZ recommended for format ),
     * and call the user's callback function until we run out of strings or
     * the user returns FALSE
     */
    UNIMPLEMENTED;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}


/*
 * @unimplemented
 */
BOOL
WINAPI
UpdateICMRegKeyA(
    DWORD	a0,
    LPSTR	a1,
    LPSTR	a2,
    UINT	a3
)
{
    UNIMPLEMENTED;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}


/*
 * @unimplemented
 */
BOOL
WINAPI
UpdateICMRegKeyW(
    DWORD	a0,
    LPWSTR	a1,
    LPWSTR	a2,
    UINT	a3
)
{
    UNIMPLEMENTED;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @implemented
 */
int
WINAPI
SetICMMode(
    HDC	hdc,
    int	iEnableICM
)
{
    PDC_ATTR pdcattr;

    pdcattr = GdiGetDcAttr(hdc);
    if (pdcattr == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }

    if (iEnableICM == ICM_QUERY)
    {
        return pdcattr->lIcmMode ? pdcattr->lIcmMode : ICM_OFF;
    }

    if ((iEnableICM != ICM_ON) &&
        (iEnableICM != ICM_OFF) &&
        (iEnableICM != ICM_DONE_OUTSIDEDC))
    {
        return 0;
    }

    /* Color management is not actually performed, we only track the mode */
    pdcattr->lIcmMode = iEnableICM;
    return iEnableICM;
}

/*
 * @unimplemented
 *
 */
HBITMAP
WINAPI
GdiConvertBitmapV5(
    HBITMAP in_format_BitMap,
    HBITMAP src_BitMap,
    INT bpp,
    INT unuse)
{
    /* FIXME guessing the prototypes */

    /*
     * it have create a new bitmap with desired in format,
     * then convert it src_bitmap to new format
     * and return it as HBITMAP
     */

    return FALSE;
}
