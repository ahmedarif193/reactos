/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         shell32.dll
 * FILE:            dll/win32/shell32/stubs.c
 * PURPOSE:         shell32.dll stubs
 * PROGRAMMER:      Dmitry Chapyshev (dmitry@reactos.org)
 * NOTES:           If you implement a function, remove it from this file
 * UPDATE HISTORY:
 *      03/02/2009  Created
 */


#include "precomp.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

/*
 * Unimplemented
 */
EXTERN_C VOID
WINAPI
CheckDiskSpace(VOID)
{
    FIXME("CheckDiskSpace() stub\n");
}

/*
 * Unimplemented
 */
EXTERN_C VOID
WINAPI
SHReValidateDarwinCache(VOID)
{
    FIXME("SHReValidateDarwinCache() stub\n");
}

/*
 * Unimplemented
 */
EXTERN_C FILEDESCRIPTOR*
WINAPI
GetFileDescriptor(FILEGROUPDESCRIPTOR *pFileGroupDesc, BOOL bUnicode, INT iIndex, LPWSTR lpName)
{
    FIXME("GetFileDescriptor() stub\n");
    return NULL;
}

/*
 * Unimplemented
 */
EXTERN_C BOOL
WINAPI
MakeShellURLFromPathW(LPCWSTR lpPath, LPWSTR lpUrl, INT cchMax)
{
    FIXME("MakeShellURLFromPathW() stub\n");
    lpUrl = NULL;
    return FALSE;
}

/*
 * Unimplemented
 */
EXTERN_C BOOL
WINAPI
MakeShellURLFromPathA(LPCSTR lpPath, LPSTR lpUrl, INT cchMax)
{
    WCHAR szPath[MAX_PATH], szURL[MAX_PATH];
    BOOL ret;
    SHAnsiToUnicode(lpPath, szPath, _countof(szPath));
    ret = MakeShellURLFromPathW(szPath, szURL, _countof(szURL));
    SHUnicodeToAnsi(szURL, lpUrl, cchMax);
    return ret;
}

/*
 * Unimplemented
 */
EXTERN_C HRESULT
WINAPI
SHParseDarwinIDFromCacheW(LPCWSTR lpUnknown1, LPWSTR lpUnknown2)
{
    FIXME("SHParseDarwinIDFromCacheW() stub\n");
    lpUnknown2 = NULL;
    return E_FAIL;
}

/*
 * Unimplemented
 */
EXTERN_C HRESULT
WINAPI
SHCopyMonikerToTemp(IMoniker *pMoniker, LPCWSTR lpInput, LPWSTR lpOutput, INT cchMax)
{
    /* Unimplemented in XP SP3 */
    TRACE("SHCopyMonikerToTemp() stub\n");
    return E_FAIL;
}

/*
 * Unimplemented
 */
EXTERN_C HLOCAL
WINAPI
CheckWinIniForAssocs(VOID)
{
    FIXME("CheckWinIniForAssocs() stub\n");
    return NULL;
}

/*
 * Unimplemented
 */
EXTERN_C HRESULT
WINAPI
SHGetSetFolderCustomSettingsW(LPSHFOLDERCUSTOMSETTINGSW pfcs,
                              LPCWSTR pszPath,
                              DWORD dwReadWrite)
{
    FIXME("SHGetSetFolderCustomSettingsW() stub\n");
    return E_FAIL;
}

/*
 * Unimplemented
 */
EXTERN_C HRESULT
WINAPI
SHGetSetFolderCustomSettingsA(LPSHFOLDERCUSTOMSETTINGSA pfcs,
                              LPCSTR pszPath,
                              DWORD dwReadWrite)
{
    FIXME("SHGetSetFolderCustomSettingsA() stub\n");
    return E_FAIL;
}

/*
 * Unimplemented
 */
EXTERN_C HRESULT
WINAPI
CDefFolderMenu_Create(LPITEMIDLIST pidlFolder,
                      HWND hwnd,
                      UINT uidl,
                      PCUITEMID_CHILD_ARRAY *apidl,
                      IShellFolder *psf,
                      LPFNDFMCALLBACK lpfn,
                      HKEY hProgID,
                      HKEY hBaseProgID,
                      IContextMenu **ppcm)
{
    FIXME("CDefFolderMenu_Create() stub\n");
    return E_FAIL;
}

/*
 * Unimplemented
 */
EXTERN_C VOID
WINAPI
SHWaitOp_Operate(LPVOID lpUnknown1, DWORD dwUnknown2)
{
    FIXME("SHWaitOp_Operate() stub\n");
}

/*
 * Unimplemented
 */
EXTERN_C INT
WINAPI
RealDriveTypeFlags(INT iDrive, BOOL bUnknown)
{
    FIXME("RealDriveTypeFlags() stub\n");
    return 1;
}

/*
 * Unimplemented
 */
EXTERN_C LONG
WINAPI
ShellHookProc(INT iCode, WPARAM wParam, LPARAM lParam)
{
    /* Unimplemented in WinXP SP3 */
    TRACE("ShellHookProc() stub\n");
    return 0;
}

/*
 * Unimplemented
 */
EXTERN_C BOOL
WINAPI
SheShortenPathW(LPWSTR lpPath, BOOL bShorten)
{
    FIXME("SheShortenPathW() stub\n");
    return FALSE;
}

/*
 * Unimplemented
 */
EXTERN_C BOOL
WINAPI
SheShortenPathA(LPSTR lpPath, BOOL bShorten)
{
    BOOL ret;
    WCHAR szPath[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, lpPath, -1, szPath, _countof(szPath));
    ret = SheShortenPathW(szPath, bShorten);
    WideCharToMultiByte(CP_ACP, 0, szPath, -1, lpPath, MAX_PATH, NULL, NULL);
    return ret;
}

/*
 * Unimplemented
 */
EXTERN_C INT
WINAPI
SheSetCurDrive(INT iIndex)
{
    FIXME("SheSetCurDrive() stub\n");
    return 1;
}

/*
 * Unimplemented
 */
EXTERN_C INT
WINAPI
SheGetPathOffsetW(LPWSTR lpPath)
{
    FIXME("SheGetPathOffsetW() stub\n");
    return 0;
}

/*
 * Unimplemented
 */
EXTERN_C BOOL
WINAPI
SheGetDirExW(LPWSTR lpDrive,
             LPDWORD lpCurDirLen,
             LPWSTR lpCurDir)
{
    FIXME("SheGetDirExW() stub\n");
    return FALSE;
}

/*
 * Unimplemented
 */
EXTERN_C INT
WINAPI
SheGetCurDrive(VOID)
{
    FIXME("SheGetCurDrive() stub\n");
    return 1;
}

/*
 * Unimplemented
 */
EXTERN_C INT
WINAPI
SheFullPathW(LPWSTR lpFullName, DWORD dwPathSize, LPWSTR lpBuffer)
{
    FIXME("SheFullPathW() stub\n");
    return 0;
}

/*
 * Unimplemented
 */
EXTERN_C INT
WINAPI
SheFullPathA(LPSTR lpFullName, DWORD dwPathSize, LPSTR lpBuffer)
{
    FIXME("SheFullPathA() stub\n");
    return 0;
}

/*
 * Unimplemented
 */
EXTERN_C BOOL
WINAPI
SheConvertPathW(LPWSTR lpCmd, LPWSTR lpFileName, UINT uCmdLen)
{
    FIXME("SheConvertPathW() stub\n");
    return FALSE;
}

/*
 * Unimplemented
 */
EXTERN_C INT
WINAPI
SheChangeDirExW(LPWSTR lpDir)
{
    FIXME("SheChangeDirExW() stub\n");
    return 0;
}

/*
 * Unimplemented
 */
EXTERN_C INT
WINAPI
SheChangeDirExA(LPSTR lpDir)
{
    FIXME("SheChangeDirExA() stub\n");
    return 0;
}

/*
 * Unimplemented
 */
EXTERN_C BOOL
WINAPI
SHInvokePrinterCommandW(HWND hwnd,
                        UINT uAction,
                        LPCWSTR lpBuf1,
                        LPCWSTR lpBuf2,
                        BOOL fModal)
{
    FIXME("SHInvokePrinterCommandW() stub\n");
    return FALSE;
}

/*
 * Unimplemented
 */
EXTERN_C BOOL
WINAPI
SHInvokePrinterCommandA(HWND hwnd,
                        UINT uAction,
                        LPCSTR lpBuf1,
                        LPCSTR lpBuf2,
                        BOOL fModal)
{
    FIXME("SHInvokePrinterCommandA() stub\n");
    return FALSE;
}

/*
 * Unimplemented
 */
EXTERN_C BOOL
WINAPI
SHCreateProcessAsUserW(PSHCREATEPROCESSINFOW pscpi)
{
    FIXME("SHCreateProcessAsUserW() stub\n");
    return FALSE;
}

/*
 * Unimplemented
 */
EXTERN_C VOID
WINAPI
PrintersGetCommand_RunDLL(HWND hwnd, HINSTANCE hInstance, LPWSTR pszCmdLine, int nCmdShow)
{
    FIXME("PrintersGetCommand_RunDLL() stub\n");
}

/*
 * Unimplemented
 */
EXTERN_C VOID
WINAPI
PrintersGetCommand_RunDLLA(HWND hwnd, HINSTANCE hInstance, LPSTR pszCmdLine, int nCmdShow)
{
    FIXME("PrintersGetCommand_RunDLLA() stub\n");
}

/*
 * Unimplemented
 */
EXTERN_C VOID
WINAPI
PrintersGetCommand_RunDLLW(HWND hwnd, HINSTANCE hInstance, LPWSTR pszCmdLine, int nCmdShow)
{
    FIXME("PrintersGetCommand_RunDLLW() stub\n");
}

/*
 * Unimplemented
 */
EXTERN_C IShellFolderViewCB*
WINAPI
SHGetShellFolderViewCB(HWND hwnd)
{
    FIXME("SHGetShellFolderViewCB() stub\n");
    return NULL;
}

/*
 * Unimplemented
 */
EXTERN_C INT
WINAPI
SHLookupIconIndexA(LPCSTR lpName, INT iIndex, UINT uFlags)
{
    FIXME("SHLookupIconIndexA() stub\n");
    return 0;
}

/*
 * Unimplemented
 */
EXTERN_C INT
WINAPI
SHLookupIconIndexW(LPCWSTR lpName, INT iIndex, UINT uFlags)
{
    FIXME("SHLookupIconIndexW() stub\n");
    return 0;
}

/*
 * Unimplemented
 */
EXTERN_C HANDLE
WINAPI
PifMgr_OpenProperties(LPCWSTR lpAppPath, LPCWSTR lpPifPath, UINT hInfIndex, UINT options)
{
    FIXME("PifMgr_OpenProperties() stub\n");
    return 0;
}

/*
 * Unimplemented
 */
EXTERN_C INT
WINAPI
PifMgr_GetProperties(HANDLE hHandle, LPCSTR lpName, LPVOID lpUnknown, INT iUnknown, UINT uUnknown)
{
    FIXME("PifMgr_GetProperties() stub\n");
    return 0;
}

/*
 * Unimplemented
 */
EXTERN_C INT
WINAPI
PifMgr_SetProperties(HANDLE hHandle, LPCSTR lpName, LPCVOID lpUnknown, INT iUnknown, UINT uUnknown)
{
    FIXME("PifMgr_SetProperties() stub\n");
    return 0;
}

/*
 * Unimplemented
 */
EXTERN_C HRESULT
WINAPI
SHStartNetConnectionDialogW(
    _In_ HWND hwnd,
    _In_ LPCWSTR pszRemoteName,
    _In_ DWORD dwType)
{
    FIXME("SHStartNetConnectionDialogW() stub\n");
    return E_FAIL;
}

/*
 * Unimplemented
 */
EXTERN_C HANDLE
WINAPI
PifMgr_CloseProperties(HANDLE hHandle, UINT uUnknown)
{
    FIXME("PifMgr_CloseProperties() stub\n");
    return NULL;
}

/*
 * Unimplemented
 */
EXTERN_C BOOL
WINAPI
DAD_DragEnterEx2(HWND hwndTarget,
                 POINT ptStart,
                 IDataObject *pdtObject)
{
    FIXME("DAD_DragEnterEx2() stub\n");
    return FALSE;
}

/*
 * Unimplemented
 */
EXTERN_C UINT
WINAPI
SHGetNetResource(LPVOID lpUnknown1, UINT iIndex, LPVOID lpUnknown2, UINT cchMax)
{
    FIXME("SHGetNetResource() stub\n");
    return 0;
}

/*
 * Unimplemented
 */
EXTERN_C BOOL
WINAPI
DragQueryInfo(HDROP hDrop, DRAGINFO *pDragInfo)
{
    FIXME("DragQueryInfo() stub\n");
    return FALSE;
}

/*
 * Unimplemented
 */
EXTERN_C LPVOID
WINAPI
DDECreatePostNotify(LPVOID lpUnknown)
{
    FIXME("DDECreatePostNotify() stub\n");
    return NULL;
}

/*
 * Unimplemented
 */
EXTERN_C VOID
WINAPI
AppCompat_RunDLLW(HWND hwnd, HINSTANCE hInstance, LPWSTR pszCmdLine, int nCmdShow)
{
    FIXME("AppCompat_RunDLLW() stub\n");
}

/*
 * Unimplemented
 */
EXTERN_C VOID
WINAPI
Control_RunDLLAsUserW(HWND hwnd, HINSTANCE hInstance, LPWSTR pszCmdLine, int nCmdShow)
{
    FIXME("Control_RunDLLAsUserW() stub\n");
}

/*
 * Unimplemented
 */
EXTERN_C UINT
WINAPI
DragQueryFileAorW(HDROP hDrop, UINT iIndex, LPWSTR lpFile, UINT ucb, BOOL bUnicode, BOOL bShorten)
{
    FIXME("DragQueryFileAorW() stub\n");
    return 0;
}

/*
 * Unimplemented
 */
EXTERN_C DWORD
WINAPI
SHNetConnectionDialog(HWND hwndOwner,
                      LPCWSTR lpstrRemoteName,
                      DWORD dwType)
{
    FIXME("SHNetConnectionDialog() stub\n");
    return ERROR_INVALID_PARAMETER;
}

/*
 * Unimplemented
 */
EXTERN_C BOOL
WINAPI
DAD_SetDragImageFromListView(HWND hwnd, POINT pt)
{
    FIXME("DAD_SetDragImageFromListView() stub\n");
    return FALSE;
}

/*
 * Unimplemented
 */
EXTERN_C void
WINAPI
SHHandleDiskFull(HWND hwndOwner, UINT uDrive)
{
    FIXME("SHHandleDiskFull() stub\n");
}

/*
 * Unimplemented
 */
EXTERN_C BOOL
WINAPI
ILGetPseudoNameW(LPCITEMIDLIST pidl1, LPCITEMIDLIST pidl2, LPWSTR szStr, INT iUnknown)
{
    /* Unimplemented in WinXP SP3 */
    TRACE("ILGetPseudoNameW() stub\n");
    *szStr = 0;
    return FALSE;
}

/*
 * Unimplemented
 */
EXTERN_C VOID
WINAPI
SHGlobalDefect(DWORD dwUnknown)
{
    /* Unimplemented in WinXP SP3 */
    TRACE("SHGlobalDefect() stub\n");
}

/*
 * Unimplemented
 */
EXTERN_C LPITEMIDLIST
WINAPI
Printers_GetPidl(LPCITEMIDLIST pidl, LPCWSTR lpName, DWORD dwUnknown1, DWORD dwUnknown2)
{
    FIXME("Printers_GetPidl() stub\n");
    return NULL;
}

/*
 * Unimplemented
 */
EXTERN_C LONG
WINAPI
Printers_AddPrinterPropPages(LPVOID lpUnknown1, LPVOID lpUnknown2)
{
    FIXME("Printers_AddPrinterPropPages() stub\n");
    return 0;
}

/*
 * Unimplemented
 */
EXTERN_C WORD
WINAPI
ExtractIconResInfoW(
    _In_ HANDLE hHandle,
    _In_ LPCWSTR lpFileName,
    _In_ WORD wIndex,
    _Out_ LPWORD lpSize,
    _Out_ LPHANDLE lpIcon)
{
    FIXME("ExtractIconResInfoW() stub\n");
    return 0;
}

/*
 * Unimplemented
 */
EXTERN_C DWORD
WINAPI
ExtractVersionResource16W(LPWSTR lpName, LPHANDLE lpHandle)
{
    FIXME("ExtractVersionResource16W() stub\n");
    return 0;
}

/*
 * Unimplemented
 */
EXTERN_C BOOL*
WINAPI
FindExeDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    FIXME("FindExeDlgProc() stub\n");
    return 0;
}

/*
 * Unimplemented
 */
EXTERN_C HANDLE
WINAPI
InternalExtractIconListW(HANDLE hHandle,
                         LPWSTR lpFileName,
                         LPINT lpCount)
{
    FIXME("InternalExtractIconListW() stub\n");
    return NULL;
}

/*
 * Unimplemented
 */
EXTERN_C HANDLE
WINAPI
InternalExtractIconListA(HANDLE hHandle,
                         LPSTR lpFileName,
                         LPINT lpCount)
{
    FIXME("InternalExtractIconListA() stub\n");
    return NULL;
}

/*
 * Unimplemented
 */
EXTERN_C HRESULT
WINAPI
FirstUserLogon(LPWSTR lpUnknown1, LPWSTR lpUnknown2)
{
    FIXME("FirstUserLogon() stub\n");
    return E_FAIL;
}

/*
 * Unimplemented
 */
EXTERN_C HRESULT
WINAPI
SHSetFolderPathW(
    _In_ INT csidl,
    _In_ HANDLE hToken,
    _In_ DWORD dwFlags,
    _In_ LPCWSTR pszPath)
{
    FIXME("SHSetFolderPathW() stub\n");
    return E_FAIL;
}

/*
 * Unimplemented
 */
EXTERN_C HRESULT
WINAPI
SHGetUserPicturePathW(LPCWSTR lpPath, int csidl, LPVOID lpUnknown)
{
    FIXME("SHGetUserPicturePathW() stub\n");
    return E_FAIL;
}

/*
 * Unimplemented
 */
EXTERN_C HRESULT
WINAPI
SHSetUserPicturePathW(LPCWSTR lpPath, int csidl, LPVOID lpUnknown)
{
    FIXME("SHGetUserPicturePathA() stub\n");
    return E_FAIL;
}

/*
 * Unimplemented
 */
EXTERN_C BOOL
WINAPI
PathIsSlowW(
    _In_ LPCWSTR pszFile,
    _In_ DWORD dwAttr)
{
    FIXME("PathIsSlowW() stub\n");
    return FALSE;
}

/*
 * Unimplemented
 */
EXTERN_C DWORD
WINAPI
SHGetProcessDword(DWORD dwUnknown1, DWORD dwUnknown2)
{
    /* Unimplemented in WinXP SP3 */
    TRACE("SHGetProcessDword() stub\n");
    return 0;
}

EXTERN_C
DWORD WINAPI CheckStagingArea(VOID)
{
    /* Called by native explorer */
    return 0;
}

EXTERN_C VOID
WINAPI
SHChangeNotifyRegisterThread(_In_ SCNRT_STATUS Status)
{
    /* ReactOS registers change notifications synchronously, so the
     * asynchronous register/deregister opt-in has nothing to switch. */
    TRACE("SHChangeNotifyRegisterThread(%d)\n", Status);
}

EXTERN_C HRESULT
WINAPI
SHGetLocalizedName(
    _In_ PCWSTR pszPath,
    _Out_writes_(cch) PWSTR pszResModule,
    _In_ UINT cch,
    _Out_ int *pidsRes)
{
    WCHAR szIniFile[MAX_PATH];
    WCHAR szValue[MAX_PATH + 32];
    WCHAR szExpanded[MAX_PATH];
    PWSTR pszComma;
    int idsRes;

    TRACE("(%s, %p, %u, %p)\n", debugstr_w(pszPath), pszResModule, cch, pidsRes);

    if (!pszPath || !pszResModule || !cch || !pidsRes)
        return E_INVALIDARG;

    *pszResModule = UNICODE_NULL;
    *pidsRes = 0;

    if (FAILED(StringCchCopyW(szIniFile, _countof(szIniFile), pszPath)))
        return E_FAIL;
    if (!PathAppendW(szIniFile, L"desktop.ini"))
        return E_FAIL;

    if (!GetPrivateProfileStringW(L".ShellClassInfo", L"LocalizedResourceName", L"",
                                  szValue, _countof(szValue), szIniFile) ||
        !szValue[0])
    {
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }

    pszComma = wcsrchr(szValue, L',');
    if (!pszComma)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    *pszComma++ = UNICODE_NULL;
    idsRes = _wtoi(pszComma);
    if (idsRes >= 0)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    if (szValue[0] == L'@')
        StringCchCopyW(szValue, _countof(szValue), &szValue[1]);

    if (!ExpandEnvironmentStringsW(szValue, szExpanded, _countof(szExpanded)))
        return HRESULT_FROM_WIN32(GetLastError());

    if (wcslen(szExpanded) >= cch)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);

    StringCchCopyW(pszResModule, cch, szExpanded);
    *pidsRes = -idsRes;
    return S_OK;
}

EXTERN_C DWORD
WINAPI
DisconnectWindowsDialog(_In_ HWND hwndParent)
{
    FIXME("DisconnectWindowsDialog(%p) stub\n", hwndParent);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

EXTERN_C int
WINAPI
SHMapIDListToSystemImageListIndex(
    _In_ IShellFolder *psf,
    _In_ PCUITEMID_CHILD pidl,
    _In_ DWORD dwFlags,
    _Out_opt_ int *piIndexSel)
{
    FIXME("SHMapIDListToSystemImageListIndex(%p, %p, 0x%lx, %p) stub\n",
          psf, pidl, dwFlags, piIndexSel);

    if (piIndexSel)
        *piIndexSel = -1;
    return -1;
}

EXTERN_C HRESULT
WINAPI
SHMapIDListToSystemImageListIndexAsync(
    _In_ PVOID pts,
    _In_ IShellFolder *psf,
    _In_ PCUITEMID_CHILD pidl,
    _In_ DWORD dwFlags,
    _In_opt_ PVOID pfn,
    _In_opt_ PVOID pvData,
    _In_opt_ PVOID pvHint,
    _Out_opt_ int *piIndex)
{
    FIXME("SHMapIDListToSystemImageListIndexAsync(%p, %p, %p, 0x%lx) stub\n",
          pts, psf, pidl, dwFlags);

    UNREFERENCED_PARAMETER(pfn);
    UNREFERENCED_PARAMETER(pvData);
    UNREFERENCED_PARAMETER(pvHint);

    if (piIndex)
        *piIndex = -1;
    return E_NOTIMPL;
}

EXTERN_C HRESULT
WINAPI
PathGetPathDisplayName(
    _In_ LPCWSTR pszPath,
    _Out_writes_(cchPath) LPWSTR pszDisplayName,
    _In_ UINT cchPath)
{
    FIXME("PathGetPathDisplayName(%s, %p, %u) stub\n",
          debugstr_w(pszPath), pszDisplayName, cchPath);

    if (!pszDisplayName || !cchPath)
        return E_INVALIDARG;

    *pszDisplayName = UNICODE_NULL;
    return E_NOTIMPL;
}

EXTERN_C BOOL
WINAPI
PathComparePaths(_In_ LPCWSTR pszPath1, _In_ LPCWSTR pszPath2)
{
    if (!pszPath1 || !pszPath2)
        return FALSE;

    return (StrCmpIW(pszPath1, pszPath2) == 0);
}

EXTERN_C HRESULT
WINAPI
SHEnableServiceObject(_In_ REFCLSID rclsid, _In_ BOOL fEnable)
{
    FIXME("SHEnableServiceObject(%s, %d) stub\n", wine_dbgstr_guid(&rclsid), fEnable);
    return E_NOTIMPL;
}

EXTERN_C HRESULT
WINAPI
Shell32Ordinal206(PVOID Arg1)
{
    UNREFERENCED_PARAMETER(Arg1);

    FIXME("shell32.#206 stub\n");
    return E_NOTIMPL;
}

EXTERN_C HRESULT
WINAPI
Shell32Ordinal792(PVOID Arg1, PVOID Arg2, PVOID Arg3)
{
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);

    FIXME("shell32.#792 stub\n");
    return E_NOTIMPL;
}

EXTERN_C HRESULT
WINAPI
Shell32Ordinal885(PVOID Arg1)
{
    UNREFERENCED_PARAMETER(Arg1);

    FIXME("shell32.#885 stub\n");
    return E_NOTIMPL;
}

EXTERN_C BOOL
WINAPI
Shell32Ordinal892(PVOID Arg1, PVOID Arg2, PVOID Arg3)
{
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);

    FIXME("shell32.#892 stub\n");
    return FALSE;
}

EXTERN_C HRESULT
WINAPI
Shell32Ordinal893(PVOID Arg1, PVOID Arg2, PVOID Arg3, PVOID Arg4, PVOID Arg5, PVOID Arg6)
{
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);

    FIXME("shell32.#893 stub\n");
    return E_NOTIMPL;
}

EXTERN_C HRESULT
WINAPI
Shell32Ordinal894(PVOID Arg1, PVOID Arg2, PVOID Arg3, PVOID Arg4, PVOID Arg5, PVOID Arg6, PVOID Arg7)
{
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);
    UNREFERENCED_PARAMETER(Arg6);
    UNREFERENCED_PARAMETER(Arg7);

    FIXME("shell32.#894 stub\n");
    return E_NOTIMPL;
}

EXTERN_C HRESULT
WINAPI
Shell32Ordinal895(PVOID Arg1, PVOID Arg2, PVOID Arg3)
{
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);

    FIXME("shell32.#895 stub\n");
    return E_NOTIMPL;
}

EXTERN_C HRESULT
WINAPI
Shell32Ordinal896(PVOID Arg1)
{
    UNREFERENCED_PARAMETER(Arg1);

    FIXME("shell32.#896 stub\n");
    return E_NOTIMPL;
}

static PVOID g_Shell32Ordinal899Value;

EXTERN_C HRESULT
WINAPI
Shell32Ordinal899(PVOID Arg1)
{
    g_Shell32Ordinal899Value = Arg1;
    return S_OK;
}

EXTERN_C HRESULT
WINAPI
Shell32Ordinal904(PVOID Arg1, PVOID Arg2)
{
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);

    FIXME("shell32.#904 stub\n");
    return E_NOTIMPL;
}

EXTERN_C HRESULT
WINAPI
Shell32Ordinal905(PVOID Arg1, PVOID Arg2)
{
    UNREFERENCED_PARAMETER(Arg1);

    FIXME("shell32.#905 stub\n");

    if (Arg2)
        *(PDWORD)Arg2 = 0;

    return E_FAIL;
}

EXTERN_C HRESULT
WINAPI
Shell32Ordinal906(VOID)
{

    FIXME("shell32.#906 stub\n");
    return E_NOTIMPL;
}

