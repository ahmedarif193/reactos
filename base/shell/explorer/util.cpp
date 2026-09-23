#include "precomp.h"
#include <winver.h>
#include <math.h>

typedef struct _LANGCODEPAGE
{
    WORD wLanguage;
    WORD wCodePage;
} LANGCODEPAGE, *PLANGCODEPAGE;

HRESULT
IsSameObject(IN IUnknown *punk1, IN IUnknown *punk2)
{
    HRESULT hRet;

    hRet = punk1->QueryInterface(IID_PPV_ARG(IUnknown, &punk1));
    if (!SUCCEEDED(hRet))
        return hRet;

    hRet = punk2->QueryInterface(IID_PPV_ARG(IUnknown, &punk2));

    punk1->Release();

    if (!SUCCEEDED(hRet))
        return hRet;

    punk2->Release();

    /* We're dealing with the same object if the IUnknown pointers are equal */
    return (punk1 == punk2) ? S_OK : S_FALSE;
}

HMENU
LoadPopupMenu(IN HINSTANCE hInstance,
              IN LPCWSTR lpMenuName)
{
    HMENU hMenu, hSubMenu = NULL;

    hMenu = LoadMenuW(hInstance, lpMenuName);
    if (hMenu != NULL)
    {
        hSubMenu = GetSubMenu(hMenu, 0);
        if ((hSubMenu != NULL) &&
            !RemoveMenu(hMenu, 0, MF_BYPOSITION))
        {
            hSubMenu = NULL;
        }

        DestroyMenu(hMenu);
    }

    return hSubMenu;
}

HMENU
FindSubMenu(IN HMENU hMenu,
            IN UINT uItem,
            IN BOOL fByPosition)
{
    MENUITEMINFOW mii;

    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_SUBMENU;

    if (GetMenuItemInfoW(hMenu, uItem, fByPosition, &mii))
    {
        return mii.hSubMenu;
    }

    return NULL;
}

BOOL
GetCurrentLoggedOnUserName(OUT LPWSTR szBuffer,
                           IN DWORD dwBufferSize)
{
    DWORD dwType;
    DWORD dwSize;

    /* Query the user name from the registry */
    dwSize = (dwBufferSize * sizeof(WCHAR)) - 1;
    if (RegQueryValueExW(hkExplorer,
                         L"Logon User Name",
                         0,
                         &dwType,
                         (LPBYTE)szBuffer,
                         &dwSize) == ERROR_SUCCESS &&
        (dwSize / sizeof(WCHAR)) > 1 &&
        szBuffer[0] != L'\0')
    {
        szBuffer[dwSize / sizeof(WCHAR)] = L'\0';
        return TRUE;
    }

    /* Fall back to GetUserName() */
    dwSize = dwBufferSize;
    if (!GetUserNameW(szBuffer, &dwSize))
    {
        szBuffer[0] = L'\0';
        return FALSE;
    }

    return TRUE;
}

BOOL
FormatMenuString(IN HMENU hMenu,
                 IN UINT uPosition,
                 IN UINT uFlags,
                 ...)
{
    va_list vl;
    MENUITEMINFOW mii;
    WCHAR szBuf[128];
    WCHAR szBufFmt[128];

    /* Find the menu item and read the formatting string */
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STRING;
    mii.dwTypeData = szBufFmt;
    mii.cch = _countof(szBufFmt);
    if (GetMenuItemInfoW(hMenu, uPosition, uFlags, &mii))
    {
        /* Format the string */
        va_start(vl, uFlags);
        _vsntprintf(szBuf,
                    _countof(szBuf) - 1,
                    szBufFmt,
                    vl);
        va_end(vl);
        szBuf[_countof(szBuf) - 1] = L'\0';

        /* Update the menu item */
        mii.dwTypeData = szBuf;
        if (SetMenuItemInfo(hMenu, uPosition, uFlags, &mii))
        {
            return TRUE;
        }
    }

    return FALSE;
}

BOOL GetRegBool(IN LPCWSTR pszSubKey, IN LPCWSTR pszValueName, IN BOOL bDefaultValue)
{
    return SHRegGetBoolUSValueW(pszSubKey, pszValueName, FALSE, bDefaultValue);
}

BOOL SetRegDword(IN LPCWSTR pszSubKey, IN LPCWSTR pszValueName, IN DWORD dwValue)
{
    return (SHRegSetUSValueW(pszSubKey, pszValueName, REG_DWORD, &dwValue,
                             sizeof(dwValue), SHREGSET_FORCE_HKCU) == ERROR_SUCCESS);
}

#define REGKEY_ADVANCED L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced"

BOOL GetAdvancedBool(IN LPCWSTR pszValueName, IN BOOL bDefaultValue)
{
    return GetRegBool(REGKEY_ADVANCED, pszValueName, bDefaultValue);
}

BOOL SetAdvancedDword(IN LPCWSTR pszValueName, IN DWORD dwValue)
{
    return SetRegDword(REGKEY_ADVANCED, pszValueName, dwValue);
}

BOOL
GetVersionInfoString(IN LPCWSTR szFileName,
                     IN LPCWSTR szVersionInfo,
                     OUT LPWSTR szBuffer,
                     IN UINT cbBufLen)
{
    LPVOID lpData = NULL;
    WCHAR szSubBlock[128];
    WCHAR *lpszLocalBuf = NULL;
    LANGID UserLangId;
    PLANGCODEPAGE lpTranslate = NULL;
    DWORD dwLen;
    DWORD dwHandle;
    UINT cbTranslate;
    UINT cbLen;
    BOOL bRet = FALSE;
    UINT i;

    dwLen = GetFileVersionInfoSizeW(szFileName, &dwHandle);

    if (dwLen > 0)
    {
        lpData = HeapAlloc(hProcessHeap, 0, dwLen);

        if (lpData != NULL)
        {
            if (GetFileVersionInfoW(szFileName,
                                    0,
                                    dwLen,
                                    lpData) != 0)
            {
                UserLangId = GetUserDefaultLangID();

                VerQueryValueW(lpData,
                               L"\\VarFileInfo\\Translation",
                               (LPVOID*)&lpTranslate,
                               &cbTranslate);

                for (i = 0; i < cbTranslate / sizeof(LANGCODEPAGE); i++)
                {
                    /* If the bottom eight bits of the language id's
                    match, use this version information (since this
                    means that the version information and the users
                    default language are the same). */
                    if (LOBYTE(lpTranslate[i].wLanguage) == LOBYTE(UserLangId))
                    {
                        wnsprintf(szSubBlock,
                                  _countof(szSubBlock),
                                  L"\\StringFileInfo\\%04X%04X\\%s",
                                  lpTranslate[i].wLanguage,
                                  lpTranslate[i].wCodePage,
                                  szVersionInfo);

                        if (VerQueryValueW(lpData,
                                           szSubBlock,
                                           (LPVOID*)&lpszLocalBuf,
                                           &cbLen) != 0)
                        {
                            wcsncpy(szBuffer, lpszLocalBuf, cbBufLen / sizeof(*szBuffer));

                            bRet = TRUE;
                            break;
                        }
                    }
                }
            }

            HeapFree(hProcessHeap, 0, lpData);
            lpData = NULL;
        }
    }

    return bRet;
}

static INT
TrayPillCoverage(INT x, INT y, INT cx, INT cy, INT r)
{
    FLOAT fx, fy, dx, dy, d;

    if (r <= 0)
        return 256;
    fx = x + 0.5f;
    fy = y + 0.5f;
    if (fx < r)
        dx = r - fx;
    else if (fx > cx - r)
        dx = fx - (cx - r);
    else
        return 256;
    if (fy < r)
        dy = r - fy;
    else if (fy > cy - r)
        dy = fy - (cy - r);
    else
        return 256;
    d = r + 0.5f - sqrtf(dx * dx + dy * dy);
    if (d <= 0.0f)
        return 0;
    if (d >= 1.0f)
        return 256;
    return (INT)(d * 256.0f);
}

VOID
ShellDrawTrayPill(IN HDC hdc, IN const RECT *prc, IN INT iState)
{
    static const BYTE s_DarkFill[] = { 0, 15, 10, 21 };
    static const BYTE s_DarkEdge[] = { 0, 26, 20, 32 };
    static const BYTE s_LightFill[] = { 0, 9, 6, 13 };
    INT cx = prc->right - prc->left, cy = prc->bottom - prc->top;
    INT r = ShellScaleForDpi(4);
    BITMAPINFO bmi;
    PULONG pBits = NULL;
    HBITMAP hbm;
    HDC hdcMem;
    HGDIOBJ hbmOld;
    ULONGLONG Sum = 0;
    BOOL bLight;
    INT x, y, i;

    if (iState <= 0 || iState >= (INT)_countof(s_DarkFill) || cx <= 0 || cy <= 0)
        return;

    hdcMem = CreateCompatibleDC(hdc);
    if (!hdcMem)
        return;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = cx;
    bmi.bmiHeader.biHeight = -cy;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    hbm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (PVOID *)&pBits, NULL, 0);
    if (!hbm || !pBits)
    {
        if (hbm)
            DeleteObject(hbm);
        DeleteDC(hdcMem);
        return;
    }
    hbmOld = SelectObject(hdcMem, hbm);
    if (BitBlt(hdcMem, 0, 0, cx, cy, hdc, prc->left, prc->top, SRCCOPY))
    {
        GdiFlush();
        for (i = 0; i < cx * cy; i++)
        {
            ULONG px = pBits[i];
            Sum += ((px >> 16) & 0xFF) * 299 + ((px >> 8) & 0xFF) * 587 + (px & 0xFF) * 114;
        }
        bLight = (Sum / ((ULONGLONG)cx * cy * 1000)) >= 140;

        for (y = 0; y < cy; y++)
        {
            for (x = 0; x < cx; x++)
            {
                INT Coverage = TrayPillCoverage(x, y, cx, cy, r);
                INT Alpha, Target, rr, gg, bb;
                ULONG px;

                if (!Coverage)
                    continue;
                if (bLight)
                    Alpha = s_LightFill[iState];
                else
                    Alpha = (y == 0) ? s_DarkEdge[iState] : s_DarkFill[iState];
                Alpha = Alpha * Coverage / 256;
                Target = bLight ? 0 : 255;
                px = pBits[y * cx + x];
                rr = (px >> 16) & 0xFF;
                gg = (px >> 8) & 0xFF;
                bb = px & 0xFF;
                rr += (Target - rr) * Alpha / 255;
                gg += (Target - gg) * Alpha / 255;
                bb += (Target - bb) * Alpha / 255;
                pBits[y * cx + x] = ((ULONG)rr << 16) | ((ULONG)gg << 8) | (ULONG)bb;
            }
        }
        BitBlt(hdc, prc->left, prc->top, cx, cy, hdcMem, 0, 0, SRCCOPY);
    }
    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
}

VOID
ShellGetTrayPillRect(IN const RECT *prcClient, OUT RECT *prcPill)
{
    INT cyPill = min(ShellScaleForDpi(40), (INT)(prcClient->bottom - prcClient->top));

    *prcPill = *prcClient;
    prcPill->top += ((prcClient->bottom - prcClient->top) - cyPill) / 2;
    prcPill->bottom = prcPill->top + cyPill;
}

VOID
ShellDrawTrayGlyph(IN HDC hdc, IN const RECT *prc, IN UINT nIconId)
{
    INT cx = GetSystemMetrics(SM_CXSMICON), cy = GetSystemMetrics(SM_CYSMICON);
    HICON hIcon = (HICON)LoadImageW(hExplorerInstance, MAKEINTRESOURCEW(nIconId),
                                    IMAGE_ICON, cx, cy, LR_SHARED);

    if (hIcon)
    {
        DrawIconEx(hdc, prc->left + (prc->right - prc->left - cx) / 2,
                   prc->top + (prc->bottom - prc->top - cy) / 2,
                   hIcon, cx, cy, 0, NULL, DI_NORMAL);
    }
}
