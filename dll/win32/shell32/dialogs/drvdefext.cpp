/*
 * Provides default drive shell extension
 *
 * Copyright 2005 Johannes Anderwald
 * Copyright 2012 Rafal Harabien
 * Copyright 2020 Katayama Hirofumi MZ
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "precomp.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <devguid.h>

#define NTOS_MODE_USER
#include <ndk/iofuncs.h>
#include <ndk/obfuncs.h>

WINE_DEFAULT_DEBUG_CHANNEL(shell);

typedef enum
{
    HWPD_STANDARDLIST = 0,
    HWPD_LARGELIST,
    HWPD_MAX = HWPD_LARGELIST
} HWPAGE_DISPLAYMODE, *PHWPAGE_DISPLAYMODE;

EXTERN_C HWND WINAPI
DeviceCreateHardwarePageEx(HWND hWndParent,
                           LPGUID lpGuids,
                           UINT uNumberOfGuids,
                           HWPAGE_DISPLAYMODE DisplayMode);
UINT SH_FormatByteSize(LONGLONG cbSize, LPWSTR pwszResult, UINT cchResultMax);

static VOID
GetDriveNameWithLetter(LPWSTR pwszText, UINT cchTextMax, LPCWSTR pwszDrive)
{
    DWORD dwMaxComp, dwFileSys;
    SIZE_T cchText = 0;

    if (GetVolumeInformationW(pwszDrive, pwszText, cchTextMax, NULL, &dwMaxComp, &dwFileSys, NULL, 0))
    {
        cchText = wcslen(pwszText);
        if (cchText == 0)
        {
            /* load default volume label */
            cchText = LoadStringW(shell32_hInstance, IDS_DRIVE_FIXED, pwszText, cchTextMax);
        }
    }

    StringCchPrintfW(pwszText + cchText, cchTextMax - cchText, L" (%c:)", pwszDrive[0]);
}

static VOID
InitializeChkDskDialog(HWND hwndDlg, LPCWSTR pwszDrive)
{
    WCHAR wszText[100];
    UINT Length;
    SetWindowLongPtr(hwndDlg, DWLP_USER, (INT_PTR)pwszDrive);

    Length = GetWindowTextW(hwndDlg, wszText, sizeof(wszText) / sizeof(WCHAR));
    wszText[Length] = L' ';
    GetDriveNameWithLetter(&wszText[Length + 1], (sizeof(wszText) / sizeof(WCHAR)) - Length - 1, pwszDrive);
    SetWindowText(hwndDlg, wszText);
}

static HWND hChkdskDrvDialog = NULL;
static BOOLEAN bChkdskSuccess = FALSE;

static BOOLEAN NTAPI
ChkdskCallback(
    IN CALLBACKCOMMAND Command,
    IN ULONG SubAction,
    IN PVOID ActionInfo)
{
    PDWORD Progress;
    PBOOLEAN pSuccess;
    switch(Command)
    {
        case PROGRESS:
            Progress = (PDWORD)ActionInfo;
            SendDlgItemMessageW(hChkdskDrvDialog, 14002, PBM_SETPOS, (WPARAM)*Progress, 0);
            break;
        case DONE:
            pSuccess = (PBOOLEAN)ActionInfo;
            bChkdskSuccess = (*pSuccess);
            break;

        case VOLUMEINUSE:
        case INSUFFICIENTRIGHTS:
        case FSNOTSUPPORTED:
        case CLUSTERSIZETOOSMALL:
            bChkdskSuccess = FALSE;
            FIXME("\n");
            break;

        default:
            break;
    }

    return TRUE;
}

static VOID
ChkDskNow(HWND hwndDlg, LPCWSTR pwszDrive)
{
    //DWORD ClusterSize = 0;
    WCHAR wszFs[30];
    ULARGE_INTEGER TotalNumberOfFreeBytes, FreeBytesAvailableUser;
    BOOLEAN bCorrectErrors = FALSE, bScanDrive = FALSE;

    if(!GetVolumeInformationW(pwszDrive, NULL, 0, NULL, NULL, NULL, wszFs, _countof(wszFs)))
    {
        FIXME("failed to get drive fs type\n");
        return;
    }

    if (!GetDiskFreeSpaceExW(pwszDrive, &FreeBytesAvailableUser, &TotalNumberOfFreeBytes, NULL))
    {
        FIXME("failed to get drive space type\n");
        return;
    }

    /*if (!GetDefaultClusterSize(wszFs, &ClusterSize, &TotalNumberOfFreeBytes))
    {
        FIXME("invalid cluster size\n");
        return;
    }*/

    if (SendDlgItemMessageW(hwndDlg, 14000, BM_GETCHECK, 0, 0) == BST_CHECKED)
        bCorrectErrors = TRUE;

    if (SendDlgItemMessageW(hwndDlg, 14001, BM_GETCHECK, 0, 0) == BST_CHECKED)
        bScanDrive = TRUE;

    hChkdskDrvDialog = hwndDlg;
    bChkdskSuccess = FALSE;
    SendDlgItemMessageW(hwndDlg, 14002, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    Chkdsk((LPWSTR)pwszDrive, (LPWSTR)wszFs, bCorrectErrors, TRUE, FALSE, bScanDrive, NULL, NULL, ChkdskCallback); // FIXME: casts

    hChkdskDrvDialog = NULL;
    bChkdskSuccess = FALSE;
}

static INT_PTR CALLBACK
ChkDskDlg(
    HWND hwndDlg,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch(uMsg)
    {
        case WM_INITDIALOG:
            SetWindowLongPtr(hwndDlg, DWLP_USER, (LONG_PTR)lParam);
            InitializeChkDskDialog(hwndDlg, (LPCWSTR)lParam);
            return TRUE;
        case WM_COMMAND:
            switch(LOWORD(wParam))
            {
                case IDCANCEL:
                    EndDialog(hwndDlg, 0);
                    break;
                case IDOK:
                {
                    LPCWSTR pwszDrive = (LPCWSTR)GetWindowLongPtr(hwndDlg, DWLP_USER);
                    ChkDskNow(hwndDlg, pwszDrive);
                    break;
                }
            }
            break;
    }

    return FALSE;
}

static DWORD
ShadePixel(COLORREF cr, double fFactor)
{
    INT r = GetRValue(cr), g = GetGValue(cr), b = GetBValue(cr);

    if (fFactor <= 1.0)
    {
        r = (INT)(r * fFactor);
        g = (INT)(g * fFactor);
        b = (INT)(b * fFactor);
    }
    else
    {
        double t = (fFactor - 1.0 > 1.0) ? 1.0 : fFactor - 1.0;
        r += (INT)((255 - r) * t);
        g += (INT)((255 - g) * t);
        b += (INT)((255 - b) * t);
    }
    return (r << 16) | (g << 8) | b;
}

VOID
CDrvDefExt::PaintStaticControls(HWND hwndDlg, LPDRAWITEMSTRUCT pDrawItem)
{
    HBRUSH hBrush;

    if (pDrawItem->CtlID == 14013)
    {
        hBrush = CreateSolidBrush(RGB(38, 160, 218));
        if (hBrush)
        {
            FillRect(pDrawItem->hDC, &pDrawItem->rcItem, hBrush);
            DeleteObject((HGDIOBJ)hBrush);
        }
    }
    else if (pDrawItem->CtlID == 14014)
    {
        hBrush = CreateSolidBrush(RGB(176, 176, 176));
        if (hBrush)
        {
            FillRect(pDrawItem->hDC, &pDrawItem->rcItem, hBrush);
            DeleteObject((HGDIOBJ)hBrush);
        }
    }
    else if (pDrawItem->CtlID == 14015)
    {
        const COLORREF crUsed = RGB(38, 160, 218), crFree = RGB(176, 176, 176);
        const INT nScale = 4, nDepth = 10;
        RECT rc = pDrawItem->rcItem;
        INT cx = rc.right - rc.left, cy = rc.bottom - rc.top;
        if (cx <= 0 || cy <= nDepth ||
            cx > MAXLONG / (nScale * nScale * 4) / cy)
            return;
        INT bw = cx * nScale, bh = cy * nScale;
        INT ew = bw, eh = (cy - nDepth) * nScale, dep = nDepth * nScale;
        INT xc = ew / 2, yc = eh / 2, xRadial, yRadial, x, y, i, j, k;
        double aFree = m_FreeSpacePerc / 100.0 * M_PI * 2.0;
        BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), bw, -bh, 1, 32, BI_RGB } };
        PDWORD pBig = NULL, pSmall = NULL;
        HDC hdcBig = NULL, hdcSmall = NULL;
        HBITMAP hbmBig = NULL, hbmSmall = NULL, hbmOldBig, hbmOldSmall;
        HBRUSH hbrFree, hbrUsed, hbrOld;
        HPEN hpenOld;

        hdcBig = CreateCompatibleDC(pDrawItem->hDC);
        hdcSmall = CreateCompatibleDC(pDrawItem->hDC);
        if (hdcBig && hdcSmall)
        {
            hbmBig = CreateDIBSection(hdcBig, &bmi, DIB_RGB_COLORS, (PVOID *)&pBig, NULL, 0);
            bmi.bmiHeader.biWidth = cx;
            bmi.bmiHeader.biHeight = -cy;
            hbmSmall = CreateDIBSection(hdcSmall, &bmi, DIB_RGB_COLORS, (PVOID *)&pSmall, NULL, 0);
        }

        hbrFree = CreateSolidBrush(crFree);
        hbrUsed = CreateSolidBrush(crUsed);
        if (!hbmBig || !hbmSmall || !hbrFree || !hbrUsed)
        {
            if (hbrFree) DeleteObject(hbrFree);
            if (hbrUsed) DeleteObject(hbrUsed);
            if (hbmBig) DeleteObject(hbmBig);
            if (hbmSmall) DeleteObject(hbmSmall);
            if (hdcBig) DeleteDC(hdcBig);
            if (hdcSmall) DeleteDC(hdcSmall);
            return;
        }

        hbmOldBig = (HBITMAP)SelectObject(hdcBig, hbmBig);
        hbmOldSmall = (HBITMAP)SelectObject(hdcSmall, hbmSmall);

        SetStretchBltMode(hdcBig, COLORONCOLOR);
        StretchBlt(hdcBig, 0, 0, bw, bh, pDrawItem->hDC, rc.left, rc.top, cx, cy, SRCCOPY);
        GdiFlush();

        for (x = 0; x < ew; ++x)
        {
            double cos_val = (x - xc + 0.5) * 2.0 / ew, sin_val, fCurve;
            COLORREF crBase;
            INT yb;

            if (cos_val < -1.0) cos_val = -1.0;
            else if (cos_val > 1.0) cos_val = 1.0;

            sin_val = sin(acos(cos_val));
            yb = yc + (INT)(sin_val * eh / 2) - 1;
            crBase = ((M_PI - acos(cos_val)) < aFree) ? crFree : crUsed;
            fCurve = 0.36 + 0.42 * sin_val;

            for (k = 0; k <= dep; ++k)
            {
                INT yy = yb + k;
                double f;

                if (yy < 0 || yy >= bh)
                    continue;
                f = (k == dep) ? 0.20 : fCurve * (1.0 - 0.30 * k / dep);
                pBig[yy * bw + x] = ShadePixel(crBase, f);
            }
        }

        xRadial = xc + (INT)(cos(M_PI + aFree) * xc);
        yRadial = yc - (INT)(sin(M_PI + aFree) * yc);

        hpenOld = (HPEN)SelectObject(hdcBig, GetStockObject(NULL_PEN));
        hbrOld = (HBRUSH)SelectObject(hdcBig, (m_FreeSpacePerc > 50) ? hbrFree : hbrUsed);

        Ellipse(hdcBig, 0, 0, ew + 1, eh + 1);

        if (m_FreeSpacePerc > 0 && m_FreeSpacePerc < 100)
        {
            if (m_FreeSpacePerc > 50)
            {
                SelectObject(hdcBig, hbrUsed);
                Pie(hdcBig, 0, 0, ew + 1, eh + 1, xRadial, yRadial, 0, yc);
            }
            else
            {
                SelectObject(hdcBig, hbrFree);
                Pie(hdcBig, 0, 0, ew + 1, eh + 1, 0, yc, xRadial, yRadial);
            }
        }

        SelectObject(hdcBig, hbrOld);
        SelectObject(hdcBig, hpenOld);
        DeleteObject(hbrFree);
        DeleteObject(hbrUsed);
        GdiFlush();

        for (y = 0; y < eh; ++y)
        {
            double t = (double)y / eh, dy = (y - eh * 0.30) / (eh * 0.5);

            for (x = 0; x < ew; ++x)
            {
                DWORD px = pBig[y * bw + x];
                COLORREF crBase;
                double dx, dist, hl;

                if (px == 0x26A0DA)
                    crBase = crUsed;
                else if (px == 0xB0B0B0)
                    crBase = crFree;
                else
                    continue;

                dx = (x - ew * 0.36) / (ew * 0.5);
                dist = sqrt(dx * dx + dy * dy);
                hl = (dist < 0.9) ? (1.0 - dist / 0.9) : 0.0;
                pBig[y * bw + x] = ShadePixel(crBase, 1.42 - 0.70 * t + 0.30 * hl * hl);
            }
        }

        for (y = 0; y < cy; ++y)
        {
            for (x = 0; x < cx; ++x)
            {
                UINT r = 0, g = 0, b = 0;

                for (j = 0; j < nScale; ++j)
                {
                    for (i = 0; i < nScale; ++i)
                    {
                        DWORD px = pBig[(y * nScale + j) * bw + x * nScale + i];
                        r += (px >> 16) & 0xff;
                        g += (px >> 8) & 0xff;
                        b += px & 0xff;
                    }
                }
                pSmall[y * cx + x] = ((r / (nScale * nScale)) << 16) |
                                     ((g / (nScale * nScale)) << 8) |
                                     (b / (nScale * nScale));
            }
        }

        BitBlt(pDrawItem->hDC, rc.left, rc.top, cx, cy, hdcSmall, 0, 0, SRCCOPY);

        SelectObject(hdcBig, hbmOldBig);
        SelectObject(hdcSmall, hbmOldSmall);
        DeleteObject(hbmBig);
        DeleteObject(hbmSmall);
        DeleteDC(hdcBig);
        DeleteDC(hdcSmall);
    }
}

// https://stackoverflow.com/questions/3098696/get-information-about-disk-drives-result-on-windows7-32-bit-system/3100268#3100268
static BOOL
GetDriveTypeAndCharacteristics(HANDLE hDevice, DEVICE_TYPE *pDeviceType, ULONG *pCharacteristics)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_FS_DEVICE_INFORMATION DeviceInfo;

    Status = NtQueryVolumeInformationFile(hDevice, &IoStatusBlock,
                                          &DeviceInfo, sizeof(DeviceInfo),
                                          FileFsDeviceInformation);
    if (Status == NO_ERROR)
    {
        *pDeviceType = DeviceInfo.DeviceType;
        *pCharacteristics = DeviceInfo.Characteristics;
        return TRUE;
    }

    return FALSE;
}

BOOL IsDriveFloppyW(LPCWSTR pszDriveRoot)
{
    LPCWSTR RootPath = pszDriveRoot;
    WCHAR szRoot[16], szDeviceName[16];
    UINT uType;
    HANDLE hDevice;
    DEVICE_TYPE DeviceType;
    ULONG ulCharacteristics;
    BOOL ret;

    lstrcpynW(szRoot, RootPath, _countof(szRoot));

    if (L'a' <= szRoot[0] && szRoot[0] <= 'z')
    {
        szRoot[0] += ('A' - 'a');
    }

    if ('A' <= szRoot[0] && szRoot[0] <= L'Z' &&
        szRoot[1] == L':' && szRoot[2] == 0)
    {
        // 'C:' --> 'C:\'
        szRoot[2] = L'\\';
        szRoot[3] = 0;
    }

    if (!PathIsRootW(szRoot))
    {
        return FALSE;
    }

    uType = GetDriveTypeW(szRoot);
    if (uType == DRIVE_REMOVABLE)
    {
        if (szRoot[0] == L'A' || szRoot[0] == L'B')
            return TRUE;
    }
    else
    {
        return FALSE;
    }

    lstrcpynW(szDeviceName, L"\\\\.\\", _countof(szDeviceName));
    szDeviceName[4] = szRoot[0];
    szDeviceName[5] = L':';
    szDeviceName[6] = UNICODE_NULL;

    hDevice = CreateFileW(szDeviceName, FILE_READ_ATTRIBUTES,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }

    ret = FALSE;
    if (GetDriveTypeAndCharacteristics(hDevice, &DeviceType, &ulCharacteristics))
    {
        if ((ulCharacteristics & FILE_FLOPPY_DISKETTE) == FILE_FLOPPY_DISKETTE)
            ret = TRUE;
    }

    CloseHandle(hDevice);

    return ret;
}

BOOL IsDriveFloppyA(LPCSTR pszDriveRoot)
{
    WCHAR szRoot[8];
    MultiByteToWideChar(CP_ACP, 0, pszDriveRoot, -1, szRoot, _countof(szRoot));
    return IsDriveFloppyW(szRoot);
}

VOID
CDrvDefExt::InitGeneralPage(HWND hwndDlg)
{
    WCHAR wszVolumeName[MAX_PATH+1] = {0};
    WCHAR wszFileSystem[MAX_PATH+1] = {0};
    WCHAR wszBuf[128];
    BOOL bRet, bFloppy = FALSE, bHasFS = FALSE;

    bRet = GetVolumeInformationW(m_wszDrive, wszVolumeName, _countof(wszVolumeName), NULL, NULL, NULL, wszFileSystem, _countof(wszFileSystem));
    if (bRet)
    {
        /* Set volume label and filesystem */
        SetDlgItemTextW(hwndDlg, 14000, wszVolumeName);
        SetDlgItemTextW(hwndDlg, 14002, wszFileSystem);
        bHasFS = *wszFileSystem != UNICODE_NULL;
    }
    else
    {
        LoadStringW(shell32_hInstance, IDS_FS_UNKNOWN, wszFileSystem, _countof(wszFileSystem));
        SetDlgItemTextW(hwndDlg, 14002, wszFileSystem);
    }

    /* Set drive type and icon */
    // TODO: Call SHGetFileInfo to get this info
    UINT DriveType = GetDriveTypeW(m_wszDrive);
    UINT IconId, TypeStrId;
    switch (DriveType)
    {
        case DRIVE_REMOVABLE:
            bFloppy = IsDriveFloppyW(m_wszDrive);
            IconId = bFloppy ? IDI_SHELL_3_14_FLOPPY : IDI_SHELL_REMOVEABLE;
            TypeStrId = bFloppy ? IDS_DRIVE_FLOPPY : IDS_DRIVE_REMOVABLE;
            break;
        case DRIVE_CDROM: IconId = IDI_SHELL_CDROM; TypeStrId = IDS_DRIVE_CDROM; break;
        case DRIVE_REMOTE: IconId = IDI_SHELL_NETDRIVE; TypeStrId = IDS_DRIVE_NETWORK; break;
        case DRIVE_RAMDISK: IconId = IDI_SHELL_RAMDISK; TypeStrId = IDS_DRIVE_FIXED; break;
        default: IconId = IDI_SHELL_DRIVE; TypeStrId = IDS_DRIVE_FIXED;
    }

    BOOL bCanSetLabel = bHasFS;
    if (DriveType == DRIVE_CDROM || DriveType == DRIVE_REMOTE || bFloppy)
    {
        bCanSetLabel = bFloppy && bHasFS;

        /* disk compression */
        ShowWindow(GetDlgItem(hwndDlg, 14011), FALSE);

        /* index */
        ShowWindow(GetDlgItem(hwndDlg, 14012), FALSE);
    }
    /* volume label textbox */
    SendMessage(GetDlgItem(hwndDlg, 14000), EM_SETREADONLY, !bCanSetLabel, 0);

    HICON hIcon = (HICON)LoadImage(shell32_hInstance, MAKEINTRESOURCE(IconId), IMAGE_ICON, 32, 32, LR_SHARED);
    if (hIcon)
        SendDlgItemMessageW(hwndDlg, 14016, STM_SETICON, (WPARAM)hIcon, 0);
    if (TypeStrId && LoadStringW(shell32_hInstance, TypeStrId, wszBuf, _countof(wszBuf)))
        SetDlgItemTextW(hwndDlg, 14001, wszBuf);

    ULARGE_INTEGER FreeBytesAvailable, TotalNumberOfBytes;
    if(GetDiskFreeSpaceExW(m_wszDrive, &FreeBytesAvailable, &TotalNumberOfBytes, NULL))
    {
        /* Init free space percentage used for drawing piechart */
        m_FreeSpacePerc = (UINT)(FreeBytesAvailable.QuadPart * 100ull / TotalNumberOfBytes.QuadPart);

        /* Used space */
        if (SH_FormatByteSize(TotalNumberOfBytes.QuadPart - FreeBytesAvailable.QuadPart, wszBuf, _countof(wszBuf)))
            SetDlgItemTextW(hwndDlg, 14003, wszBuf);

        if (StrFormatByteSizeW(TotalNumberOfBytes.QuadPart - FreeBytesAvailable.QuadPart, wszBuf, _countof(wszBuf)))
            SetDlgItemTextW(hwndDlg, 14004, wszBuf);

        /* Free space */
        if (SH_FormatByteSize(FreeBytesAvailable.QuadPart, wszBuf, _countof(wszBuf)))
            SetDlgItemTextW(hwndDlg, 14005, wszBuf);

        if (StrFormatByteSizeW(FreeBytesAvailable.QuadPart, wszBuf, _countof(wszBuf)))
            SetDlgItemTextW(hwndDlg, 14006, wszBuf);

        /* Total space */
        if (SH_FormatByteSize(TotalNumberOfBytes.QuadPart, wszBuf, _countof(wszBuf)))
            SetDlgItemTextW(hwndDlg, 14007, wszBuf);

        if (StrFormatByteSizeW(TotalNumberOfBytes.QuadPart, wszBuf, _countof(wszBuf)))
            SetDlgItemTextW(hwndDlg, 14008, wszBuf);
    }
    else
    {
        m_FreeSpacePerc = 0;

        if (SH_FormatByteSize(0, wszBuf, _countof(wszBuf)))
        {
            SetDlgItemTextW(hwndDlg, 14003, wszBuf);
            SetDlgItemTextW(hwndDlg, 14005, wszBuf);
            SetDlgItemTextW(hwndDlg, 14007, wszBuf);
        }
        if (StrFormatByteSizeW(0, wszBuf, _countof(wszBuf)))
        {
            SetDlgItemTextW(hwndDlg, 14004, wszBuf);
            SetDlgItemTextW(hwndDlg, 14006, wszBuf);
            SetDlgItemTextW(hwndDlg, 14008, wszBuf);
        }
    }

    /* Set drive description */
    WCHAR wszFormat[50];
    GetDlgItemTextW(hwndDlg, 14009, wszFormat, _countof(wszFormat));
    _swprintf(wszBuf, wszFormat, m_wszDrive[0]);
    SetDlgItemTextW(hwndDlg, 14009, wszBuf);

    /* show disk cleanup button only for fixed drives */
    ShowWindow(GetDlgItem(hwndDlg, 14010), DriveType == DRIVE_FIXED);
}

INT_PTR CALLBACK
CDrvDefExt::GeneralPageProc(
    HWND hwndDlg,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch(uMsg)
    {
        case WM_INITDIALOG:
        {
            LPPROPSHEETPAGEW ppsp = (LPPROPSHEETPAGEW)lParam;
            if (ppsp == NULL)
                break;

            CDrvDefExt *pDrvDefExt = reinterpret_cast<CDrvDefExt *>(ppsp->lParam);
            SetWindowLongPtr(hwndDlg, DWLP_USER, (LONG_PTR)pDrvDefExt);
            pDrvDefExt->InitGeneralPage(hwndDlg);
            return TRUE;
        }
        case WM_DRAWITEM:
        {
            LPDRAWITEMSTRUCT pDrawItem = (LPDRAWITEMSTRUCT)lParam;

            if (pDrawItem->CtlID >= 14013 && pDrawItem->CtlID <= 14015)
            {
                CDrvDefExt *pDrvDefExt = reinterpret_cast<CDrvDefExt *>(GetWindowLongPtr(hwndDlg, DWLP_USER));
                pDrvDefExt->PaintStaticControls(hwndDlg, pDrawItem);
                return TRUE;
            }
            break;
        }
        case WM_PAINT:
            break;
        case WM_COMMAND:
            if (LOWORD(wParam) == 14010) /* Disk Cleanup */
            {
                CDrvDefExt *pDrvDefExt = reinterpret_cast<CDrvDefExt *>(GetWindowLongPtr(hwndDlg, DWLP_USER));
                WCHAR wszBuf[256];
                DWORD cbBuf = sizeof(wszBuf);

                if (RegGetValueW(HKEY_LOCAL_MACHINE,
                                 L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\MyComputer\\CleanupPath",
                                 NULL,
                                 RRF_RT_REG_SZ,
                                 NULL,
                                 (PVOID)wszBuf,
                                 &cbBuf) == ERROR_SUCCESS)
                {
                    WCHAR wszCmd[MAX_PATH];

                    StringCbPrintfW(wszCmd, sizeof(wszCmd), wszBuf, pDrvDefExt->m_wszDrive[0]);
                    WCHAR* wszArgs = PathGetArgsW(wszCmd);
                    if (wszArgs && *wszArgs && wszArgs != wszCmd)
                        wszArgs[-1] = UNICODE_NULL;
                    else
                        wszArgs = NULL;

                    if (ShellExecuteW(hwndDlg, NULL, wszCmd, wszArgs, NULL, SW_SHOW) <= (HINSTANCE)32)
                        ERR("Failed to create cleanup process %ls\n", wszCmd);
                }
            }
            else if (LOWORD(wParam) == 14000) /* Label */
            {
                if (HIWORD(wParam) == EN_CHANGE)
                    PropSheet_Changed(GetParent(hwndDlg), hwndDlg);
            }
            break;
        case WM_NOTIFY:
            if (((LPNMHDR)lParam)->hwndFrom == GetParent(hwndDlg))
            {
                /* Property Sheet */
                LPPSHNOTIFY lppsn = (LPPSHNOTIFY)lParam;

                if (lppsn->hdr.code == PSN_APPLY)
                {
                    CDrvDefExt *pDrvDefExt = reinterpret_cast<CDrvDefExt *>(GetWindowLongPtr(hwndDlg, DWLP_USER));

                    HRESULT hr = E_FAIL;
                    HWND hLabel = GetDlgItem(hwndDlg, 14000);
                    WCHAR wszBuf[256];
                    *wszBuf = UNICODE_NULL;
                    if (GetWindowTextW(hLabel, wszBuf, _countof(wszBuf)) || GetWindowTextLengthW(hLabel) == 0)
                        hr = CDrivesFolder::SetDriveLabel(hwndDlg, pDrvDefExt->m_wszDrive, wszBuf);

                    SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, FAILED(hr) ? PSNRET_INVALID : PSNRET_NOERROR);
                    return TRUE;
                }
            }
            break;

        default:
            break;
    }

    return FALSE;
}

INT_PTR CALLBACK
CDrvDefExt::ExtraPageProc(
    HWND hwndDlg,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            LPPROPSHEETPAGEW ppsp = (LPPROPSHEETPAGEW)lParam;
            SetWindowLongPtr(hwndDlg, DWLP_USER, (LONG_PTR)ppsp->lParam);
            return TRUE;
        }
        case WM_COMMAND:
        {
            WCHAR wszBuf[MAX_PATH];
            DWORD cbBuf = sizeof(wszBuf);
            CDrvDefExt *pDrvDefExt = reinterpret_cast<CDrvDefExt *>(GetWindowLongPtr(hwndDlg, DWLP_USER));

            switch(LOWORD(wParam))
            {
                case 14000:
                    DialogBoxParamW(shell32_hInstance, MAKEINTRESOURCEW(IDD_CHECK_DISK), hwndDlg, ChkDskDlg, (LPARAM)pDrvDefExt->m_wszDrive);
                    break;
                case 14001:
                    if (RegGetValueW(HKEY_LOCAL_MACHINE,
                                     L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\MyComputer\\DefragPath",
                                     NULL,
                                     RRF_RT_REG_SZ,
                                     NULL,
                                     (PVOID)wszBuf,
                                     &cbBuf) == ERROR_SUCCESS)
                    {
                        WCHAR wszCmd[MAX_PATH];

                        StringCbPrintfW(wszCmd, sizeof(wszCmd), wszBuf, pDrvDefExt->m_wszDrive[0]);

                        if (ShellExecuteW(hwndDlg, NULL, wszCmd, NULL, NULL, SW_SHOW) <= (HINSTANCE)32)
                            ERR("Failed to create defrag process %ls\n", wszCmd);
                    }
                    break;
                case 14002:
                    if (RegGetValueW(HKEY_LOCAL_MACHINE,
                                     L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\MyComputer\\BackupPath",
                                     NULL,
                                     RRF_RT_REG_SZ,
                                     NULL,
                                     (PVOID)wszBuf,
                                     &cbBuf) == ERROR_SUCCESS)
                    {
                        if (ShellExecuteW(hwndDlg, NULL, wszBuf, NULL, NULL, SW_SHOW) <= (HINSTANCE)32)
                            ERR("Failed to create backup process %ls\n", wszBuf);
                    }
            }
            break;
        }
    }
    return FALSE;
}

INT_PTR CALLBACK
CDrvDefExt::HardwarePageProc(
    HWND hwndDlg,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    UNREFERENCED_PARAMETER(wParam);

    switch(uMsg)
    {
        case WM_INITDIALOG:
        {
            GUID Guids[2];
            Guids[0] = GUID_DEVCLASS_DISKDRIVE;
            Guids[1] = GUID_DEVCLASS_CDROM;

            /* create the hardware page */
            DeviceCreateHardwarePageEx(hwndDlg, Guids, _countof(Guids), HWPD_STANDARDLIST);
            break;
        }
    }

    return FALSE;
}

CDrvDefExt::CDrvDefExt()
{
    m_wszDrive[0] = L'\0';
}

CDrvDefExt::~CDrvDefExt()
{

}

struct CDrop
{
    HRESULT hr;
    STGMEDIUM stgm;
    HDROP hDrop;

    explicit CDrop(IDataObject *pDO)
    {
        FORMATETC format = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        hDrop = SUCCEEDED(hr = pDO->GetData(&format, &stgm)) ? (HDROP)stgm.hGlobal : NULL;
    }

    ~CDrop()
    {
        if (hDrop)
            ReleaseStgMedium(&stgm);
    }

    UINT GetCount()
    {
        return DragQueryFileW(hDrop, -1, NULL, 0);
    }
};

static inline bool
IsValidDrivePath(PCWSTR Path)
{
    return GetDriveTypeW(Path) > DRIVE_NO_ROOT_DIR;
}

HRESULT WINAPI
CDrvDefExt::Initialize(PCIDLIST_ABSOLUTE pidlFolder, IDataObject *pDataObj, HKEY hkeyProgID)
{
    HRESULT hr;

    TRACE("%p %p %p %p\n", this, pidlFolder, pDataObj, hkeyProgID);

    if (!pDataObj)
        return E_FAIL;


    CDrop drop(pDataObj);
    if (FAILED_UNEXPECTEDLY(hr = drop.hr))
        return hr;

    if (!DragQueryFileW(drop.hDrop, 0, m_wszDrive, _countof(m_wszDrive)))
    {
        ERR("DragQueryFileW failed\n");
        return E_FAIL;
    }

    if (drop.GetCount() > 1)
        m_Multiple = pDataObj;

    TRACE("Drive properties %ls\n", m_wszDrive);

    return S_OK;
}

HRESULT WINAPI
CDrvDefExt::QueryContextMenu(HMENU hmenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags)
{
    UNIMPLEMENTED;
    return E_NOTIMPL;
}

HRESULT WINAPI
CDrvDefExt::InvokeCommand(LPCMINVOKECOMMANDINFO lpici)
{
    UNIMPLEMENTED;
    return E_NOTIMPL;
}

HRESULT WINAPI
CDrvDefExt::GetCommandString(UINT_PTR idCmd, UINT uType, UINT *pwReserved, LPSTR pszName, UINT cchMax)
{
    UNIMPLEMENTED;
    return E_NOTIMPL;
}

HRESULT
CDrvDefExt::AddMainPage(LPFNADDPROPSHEETPAGE pfnAddPage, LPARAM lParam)
{
    WCHAR szTitle[MAX_PATH], *pszTitle = NULL;
    if (m_Multiple)
    {
        CComHeapPtr<ITEMIDLIST_ABSOLUTE> pidl(SHSimpleIDListFromPathW(m_wszDrive));
        if (SUCCEEDED(SHGetNameAndFlagsW(pidl, SHGDN_INFOLDER, szTitle, _countof(szTitle), NULL)))
            pszTitle = szTitle;
    }

    HPROPSHEETPAGE hPage;
    hPage = SH_CreatePropertySheetPageEx(IDD_DRIVE_PROPERTIES, GeneralPageProc, (LPARAM)this,
                                         pszTitle, &PropSheetPageLifetimeCallback<CDrvDefExt>);
    HRESULT hr = AddPropSheetPage(hPage, pfnAddPage, lParam);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;
    else
        AddRef(); // For PropSheetPageLifetimeCallback
    return hr;
}

HRESULT WINAPI
CDrvDefExt::AddPages(LPFNADDPROPSHEETPAGE pfnAddPage, LPARAM lParam)
{
    HRESULT hr = AddMainPage(pfnAddPage, lParam);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    if (m_Multiple)
    {
        CDrop drop(m_Multiple);
        UINT count = SUCCEEDED(drop.hr) ? drop.GetCount() : 0;
        for (UINT i = 0; ++i < count;) // Skipping the first drive since it already has a page
        {
            CComPtr<CDrvDefExt> SheetExt;
            if (FAILED_UNEXPECTEDLY(hr = ShellObjectCreator(SheetExt)))
                continue;
            if (!DragQueryFileW(drop.hDrop, i, SheetExt->m_wszDrive, _countof(SheetExt->m_wszDrive)))
                continue;
            if (!IsValidDrivePath(SheetExt->m_wszDrive))
                continue;

            SheetExt->m_Multiple = m_Multiple;
            SheetExt->AddMainPage(pfnAddPage, lParam);
        }
    }
    else
    {
        HPROPSHEETPAGE hPage;
        if (GetDriveTypeW(m_wszDrive) == DRIVE_FIXED)
        {
            hPage = SH_CreatePropertySheetPage(IDD_DRIVE_TOOLS,
                                               ExtraPageProc,
                                               (LPARAM)this,
                                               NULL);
            if (hPage)
                pfnAddPage(hPage, lParam);
        }

        if (GetDriveTypeW(m_wszDrive) != DRIVE_REMOTE)
        {
            hPage = SH_CreatePropertySheetPage(IDD_DRIVE_HARDWARE,
                                               HardwarePageProc,
                                               (LPARAM)this,
                                               NULL);
            if (hPage)
                pfnAddPage(hPage, lParam);
        }
    }

    return S_OK;
}

HRESULT WINAPI
CDrvDefExt::ReplacePage(UINT uPageID, LPFNADDPROPSHEETPAGE pfnReplacePage, LPARAM lParam)
{
    UNIMPLEMENTED;
    return E_NOTIMPL;
}

HRESULT WINAPI
CDrvDefExt::SetSite(IUnknown *punk)
{
    UNIMPLEMENTED;
    return E_NOTIMPL;
}

HRESULT WINAPI
CDrvDefExt::GetSite(REFIID iid, void **ppvSite)
{
    UNIMPLEMENTED;
    return E_NOTIMPL;
}
