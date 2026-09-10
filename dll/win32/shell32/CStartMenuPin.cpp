/*
 * PROJECT:     ReactOS shell32
 * LICENSE:     GPL-3.0-or-later
 * PURPOSE:     Start-menu pin compatibility and taskbar pin context handler
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#include "precomp.h"
#include <ndk/iofuncs.h>
#include <taskbarpinlock.h>
#include <taskbarpinicon.h>

WINE_DEFAULT_DEBUG_CHANNEL(shell);

#define TASKBAR_PIN_CHANGED_MESSAGE L"TaskbarPinningChanged"

enum
{
    IDC_TASKBAR_PIN = 0
};

static BOOL
NormalizePinPath(PCWSTR pszPath, CStringW &Path)
{
    WCHAR szExpanded[MAX_PATH], szFull[MAX_PATH], szLong[MAX_PATH];
    PCWSTR pszExpanded = pszPath;
    DWORD cch;

    Path.Empty();
    if (!pszPath || !*pszPath)
        return FALSE;

    cch = ExpandEnvironmentStringsW(pszPath, szExpanded, _countof(szExpanded));
    if (cch && cch <= _countof(szExpanded))
        pszExpanded = szExpanded;

    cch = GetFullPathNameW(pszExpanded, _countof(szFull), szFull, NULL);
    if (cch && cch < _countof(szFull))
        Path = szFull;
    else
        Path = pszExpanded;

    cch = GetLongPathNameW(Path, szLong, _countof(szLong));
    if (cch && cch < _countof(szLong))
        Path = szLong;

    PathRemoveBackslashW(Path.GetBuffer());
    Path.ReleaseBuffer();
    return !Path.IsEmpty();
}

static BOOL
ResolvePinTarget(PCWSTR pszPath, CStringW &Target)
{
    CStringW Source;

    if (!NormalizePinPath(pszPath, Source))
        return FALSE;

    if (lstrcmpiW(PathFindExtensionW(Source), L".lnk") != 0)
    {
        Target = Source;
        return TRUE;
    }

    CComPtr<IShellLinkW> Link;
    HRESULT hr = Link.CoCreateInstance(CLSID_ShellLink, IID_IShellLinkW, NULL, CLSCTX_INPROC_SERVER);
    if (FAILED(hr))
        return FALSE;

    CComPtr<IPersistFile> Persist;
    hr = Link->QueryInterface(IID_PPV_ARG(IPersistFile, &Persist));
    if (FAILED(hr) || FAILED(Persist->Load(Source, STGM_READ)))
        return FALSE;

    WCHAR szTarget[MAX_PATH] = L"";
    hr = Link->GetPath(szTarget, _countof(szTarget), NULL, SLGP_UNCPRIORITY);
    return SUCCEEDED(hr) && szTarget[0] && NormalizePinPath(szTarget, Target);
}

static BOOL
IsTaskbarPinnable(PCWSTR pszSource, CStringW *pTarget)
{
    CStringW Target;

    if (!pszSource || PathIsDirectoryW(pszSource) ||
        !ResolvePinTarget(pszSource, Target) ||
        lstrcmpiW(PathFindExtensionW(Target), L".exe") != 0)
    {
        return FALSE;
    }

    DWORD Attributes = GetFileAttributesW(Target);
    if (Attributes == INVALID_FILE_ATTRIBUTES || (Attributes & FILE_ATTRIBUTE_DIRECTORY))
        return FALSE;

    WCHAR szKey[MAX_PATH];
    StringCchPrintfW(szKey, _countof(szKey), L"Applications\\%s", PathFindFileNameW(Target));
    if (SHGetValueW(HKEY_CLASSES_ROOT, szKey, L"NoStartPage", NULL, NULL, NULL) == ERROR_SUCCESS)
        return FALSE;

    if (pTarget)
        *pTarget = Target;
    return TRUE;
}

static BOOL
ReadExplorerPolicy(HKEY hRoot, PCWSTR pszName)
{
    DWORD dwValue = 0, cbValue = sizeof(dwValue), dwType = 0;
    return SHGetValueW(hRoot, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", pszName, &dwType, &dwValue, &cbValue) == ERROR_SUCCESS &&
           dwType == REG_DWORD && dwValue != 0;
}

static BOOL
IsTaskbarPinningDisabled(VOID)
{
    return ReadExplorerPolicy(HKEY_CURRENT_USER, L"NoPinningToTaskbar") ||
           ReadExplorerPolicy(HKEY_LOCAL_MACHINE, L"NoPinningToTaskbar");
}

static HRESULT
GetTaskbarPinFolder(BOOL bCreate, CStringW &Folder)
{
    WCHAR szFolder[MAX_PATH];
    PWSTR pszFolder = NULL;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_UserPinned, bCreate ? KF_FLAG_CREATE : KF_FLAG_DONT_VERIFY, NULL, &pszFolder);
    if (SUCCEEDED(hr))
    {
        hr = StringCchCopyW(szFolder, _countof(szFolder), pszFolder);
        CoTaskMemFree(pszFolder);
    }
    else
    {
        hr = SHGetFolderPathW(NULL, CSIDL_APPDATA | (bCreate ? CSIDL_FLAG_CREATE : 0), NULL,
                              SHGFP_TYPE_CURRENT, szFolder);
        if (SUCCEEDED(hr) &&
            !PathAppendW(szFolder, L"Microsoft\\Internet Explorer\\Quick Launch\\User Pinned"))
        {
            hr = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        }
    }
    if (FAILED(hr) || !PathAppendW(szFolder, L"TaskBar"))
        return FAILED(hr) ? hr : HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);

    if (bCreate)
    {
        INT error = SHCreateDirectoryExW(NULL, szFolder, NULL);
        if (error != ERROR_SUCCESS && error != ERROR_ALREADY_EXISTS &&
            error != ERROR_FILE_EXISTS)
        {
            return HRESULT_FROM_WIN32(error);
        }
    }

    Folder = szFolder;
    return S_OK;
}

static BOOL CALLBACK
NotifyTaskbarChild(HWND hWnd, LPARAM lParam)
{
    WCHAR szClass[32];

    if (GetClassNameW(hWnd, szClass, _countof(szClass)) &&
        !lstrcmpW(szClass, L"MSTaskSwWClass"))
    {
        PostMessageW(hWnd, (UINT)lParam, 0, 0);
        return FALSE;
    }
    return TRUE;
}

static VOID
NotifyTaskbarPinChange(LONG lEvent, PCWSTR pszPath)
{
    SHChangeNotify(lEvent, SHCNF_PATHW | SHCNF_FLUSHNOWAIT, pszPath, NULL);

    UINT uMessage = RegisterWindowMessageW(TASKBAR_PIN_CHANGED_MESSAGE);
    HWND hwndTray = FindWindowW(L"Shell_TrayWnd", NULL);
    if (uMessage && hwndTray)
        EnumChildWindows(hwndTray, NotifyTaskbarChild, (LPARAM)uMessage);
}

typedef struct _PIN_DIR_ENTRY
{
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG FileAttributes;
    ULONG FileNameLength;
    WCHAR FileName[1];
} PIN_DIR_ENTRY;

// FindFirstFile cannot open the pin folder for enumeration on this build,
// so it is read through a directory handle instead.
static HRESULT
FindPinnedShortcut(PCWSTR pszTarget, CStringW *pShortcut)
{
    CStringW Folder;
    HRESULT hr = GetTaskbarPinFolder(FALSE, Folder);
    if (FAILED(hr))
        return (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) ? S_FALSE : hr;

    CTaskbarPinLock Lock;
    hr = Lock.Acquire(Folder);
    if (FAILED(hr))
        return hr;

    HANDLE hFolder = CreateFileW(Folder, FILE_LIST_DIRECTORY,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                 OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (hFolder == INVALID_HANDLE_VALUE)
    {
        DWORD error = GetLastError();
        return (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) ? S_FALSE : HRESULT_FROM_WIN32(error);
    }

    BYTE Buffer[8192];
    BOOLEAN bRestart = TRUE;
    BOOL bFound = FALSE;
    HRESULT hrRead = S_OK;

    while (!bFound)
    {
        IO_STATUS_BLOCK IoStatus;

        RtlZeroMemory(&IoStatus, sizeof(IoStatus));
        if (!NT_SUCCESS(NtQueryDirectoryFile(hFolder, NULL, NULL, NULL, &IoStatus, Buffer,
                                             sizeof(Buffer), FileDirectoryInformation, FALSE,
                                             NULL, bRestart)))
        {
            break;
        }
        bRestart = FALSE;

        for (ULONG Offset = 0;;)
        {
            const PIN_DIR_ENTRY *pEntry = (const PIN_DIR_ENTRY *)&Buffer[Offset];
            WCHAR szName[MAX_PATH], szShortcut[MAX_PATH];
            CStringW Target;
            ULONG cchName = pEntry->FileNameLength / sizeof(WCHAR);

            if (cchName && cchName < _countof(szName) &&
                !(pEntry->FileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                CopyMemory(szName, pEntry->FileName, pEntry->FileNameLength);
                szName[cchName] = UNICODE_NULL;
                if (!lstrcmpiW(PathFindExtensionW(szName), L".lnk") &&
                    SUCCEEDED(StringCchPrintfW(szShortcut, _countof(szShortcut), L"%s\\%s",
                                               Folder.GetString(), szName)))
                {
                    if (!ResolvePinTarget(szShortcut, Target))
                    {
                        hrRead = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                    }
                    else if (Target.CompareNoCase(pszTarget) == 0)
                    {
                        if (pShortcut)
                            *pShortcut = szShortcut;
                        bFound = TRUE;
                        break;
                    }
                }
            }

            if (!pEntry->NextEntryOffset)
                break;
            Offset += pEntry->NextEntryOffset;
            if (Offset >= sizeof(Buffer))
                break;
        }
    }

    CloseHandle(hFolder);
    return bFound ? S_OK : FAILED(hrRead) ? hrRead : S_FALSE;
}

static VOID
StampNewPin(PCWSTR pszPath)
{
    HANDLE hFile = CreateFileW(pszPath, FILE_WRITE_ATTRIBUTES | FILE_WRITE_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        FILETIME Time;
        GetSystemTimeAsFileTime(&Time);
        SetFileTime(hFile, &Time, &Time, &Time);
        FlushFileBuffers(hFile);
        CloseHandle(hFile);
    }
}

static HRESULT
ChoosePinDestination(PCWSTR pszSource, PCWSTR pszTarget, CStringW &Destination)
{
    CStringW Folder;
    HRESULT hr = GetTaskbarPinFolder(TRUE, Folder);
    if (FAILED(hr))
        return hr;

    WCHAR szName[MAX_PATH];
    hr = StringCchCopyW(szName, _countof(szName), PathFindFileNameW(pszSource));
    if (FAILED(hr))
        return hr;
    PathRemoveExtensionW(szName);
    if (!szName[0])
    {
        hr = StringCchCopyW(szName, _countof(szName), PathFindFileNameW(pszTarget));
        if (FAILED(hr))
            return hr;
        PathRemoveExtensionW(szName);
    }

    for (UINT i = 1; i < 10000; ++i)
    {
        WCHAR szPath[MAX_PATH];
        if (i == 1)
            hr = StringCchPrintfW(szPath, _countof(szPath), L"%s\\%s.lnk", Folder.GetString(), szName);
        else
            hr = StringCchPrintfW(szPath, _countof(szPath), L"%s\\%s (%u).lnk", Folder.GetString(), szName, i);
        if (FAILED(hr))
            return hr;
        if (GetFileAttributesW(szPath) == INVALID_FILE_ATTRIBUTES)
        {
            CStringW IconPath;
            hr = TaskbarPin_GetIconPath(szPath, IconPath);
            if (FAILED(hr))
                return hr;
            if (GetFileAttributesW(IconPath) != INVALID_FILE_ATTRIBUTES)
                continue;
            Destination = szPath;
            return S_OK;
        }
    }
    return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
}

static HRESULT
CreateTaskbarPin(PCWSTR pszSource)
{
    CStringW Target, Existing, Destination, Folder;
    if (!IsTaskbarPinnable(pszSource, &Target))
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    HRESULT hr = GetTaskbarPinFolder(TRUE, Folder);
    if (FAILED(hr))
        return hr;
    CTaskbarPinLock Lock;
    hr = Lock.Acquire(Folder);
    if (FAILED(hr))
        return hr;
    hr = FindPinnedShortcut(Target, &Existing);
    if (FAILED(hr))
        return hr;
    if (hr == S_OK)
        return S_OK;

    hr = ChoosePinDestination(pszSource, Target, Destination);
    if (FAILED(hr))
        return hr;

    if (lstrcmpiW(PathFindExtensionW(pszSource), L".lnk") == 0)
    {
        if (!CopyFileW(pszSource, Destination, TRUE))
            return HRESULT_FROM_WIN32(GetLastError());
    }
    else
    {
        CComPtr<IShellLinkW> Link;
        hr = Link.CoCreateInstance(CLSID_ShellLink, IID_IShellLinkW, NULL, CLSCTX_INPROC_SERVER);
        if (FAILED(hr))
            return hr;

        hr = Link->SetPath(Target);
        if (hr != S_OK)
            return FAILED(hr) ? hr : E_FAIL;
        if (SUCCEEDED(hr))
        {
            WCHAR szWorking[MAX_PATH];
            hr = StringCchCopyW(szWorking, _countof(szWorking), Target);
            if (SUCCEEDED(hr) && PathRemoveFileSpecW(szWorking))
                Link->SetWorkingDirectory(szWorking);
        }

        CComPtr<IPersistFile> Persist;
        if (SUCCEEDED(hr))
            hr = Link->QueryInterface(IID_PPV_ARG(IPersistFile, &Persist));
        if (SUCCEEDED(hr))
            hr = Persist->Save(Destination, TRUE);
        if (FAILED(hr))
            return hr;
    }

    CStringW SavedTarget;
    if (!ResolvePinTarget(Destination, SavedTarget) || SavedTarget.CompareNoCase(Target) != 0)
    {
        DeleteFileW(Destination);
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    StampNewPin(Destination);
    NotifyTaskbarPinChange(SHCNE_CREATE, Destination);
    return S_OK;
}

static HRESULT
RemoveTaskbarPinForTarget(PCWSTR pszTarget)
{
    CStringW Shortcut, Folder;
    HRESULT hr = GetTaskbarPinFolder(FALSE, Folder);
    if (FAILED(hr))
        return (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) ? S_OK : hr;
    CTaskbarPinLock Lock;
    hr = Lock.Acquire(Folder);
    if (FAILED(hr))
        return hr;
    while ((hr = FindPinnedShortcut(pszTarget, &Shortcut)) == S_OK)
    {
        CStringW IconPath;
        TaskbarPin_GetOwnedIconPath(Shortcut, IconPath);
        if (!DeleteFileW(Shortcut))
            return HRESULT_FROM_WIN32(GetLastError());
        if (!IconPath.IsEmpty())
            DeleteFileW(IconPath);
        NotifyTaskbarPinChange(SHCNE_DELETE, Shortcut);
        Shortcut.Empty();
    }
    return FAILED(hr) ? hr : S_OK;
}

STDMETHODIMP
CStartMenuPin::Initialize(PCIDLIST_ABSOLUTE pidlFolder, IDataObject *pDataObject, HKEY hkeyProgID)
{
    m_Path.Empty();
    m_HasCommand = FALSE;
    m_Unpin = FALSE;
    if (!pDataObject)
        return S_OK;

    FORMATETC Format = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM Medium;
    HRESULT hr = pDataObject->GetData(&Format, &Medium);
    if (FAILED(hr))
        return S_OK;

    if (DragQueryFileW((HDROP)Medium.hGlobal, -1, NULL, 0) == 1)
    {
        WCHAR szPath[MAX_PATH];
        UINT cch = DragQueryFileW((HDROP)Medium.hGlobal, 0, NULL, 0);
        if (cch && cch < _countof(szPath) && DragQueryFileW((HDROP)Medium.hGlobal, 0, szPath, _countof(szPath)) == cch)
            m_Path = szPath;
    }
    ReleaseStgMedium(&Medium);
    return S_OK;
}

STDMETHODIMP
CStartMenuPin::QueryContextMenu(HMENU hMenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags)
{
    m_HasCommand = FALSE;
    if (!hMenu || idCmdFirst > idCmdLast)
        return E_INVALIDARG;
    if ((uFlags & (CMF_DEFAULTONLY | CMF_NOVERBS)) || m_Path.IsEmpty() ||
        IsTaskbarPinningDisabled())
    {
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
    }

    CStringW Target;
    if (!ResolvePinTarget(m_Path, Target))
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);

    HRESULT hr = FindPinnedShortcut(Target, NULL);
    if (FAILED(hr))
        return hr;
    BOOL bPinned = hr == S_OK;
    if (!bPinned && !IsTaskbarPinnable(m_Path, NULL))
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
    PCWSTR pszText = bPinned ? L"Unpin from Taskbar" : L"Pin to Taskbar";
    if (!InsertMenuW(hMenu, indexMenu, MF_BYPOSITION | MF_STRING, idCmdFirst + IDC_TASKBAR_PIN, pszText))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    m_HasCommand = TRUE;
    m_Unpin = bPinned;
    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 1);
}

STDMETHODIMP
CStartMenuPin::InvokeCommand(LPCMINVOKECOMMANDINFO pici)
{
    if (!pici || pici->cbSize < sizeof(*pici) || m_Path.IsEmpty())
        return E_INVALIDARG;

    BOOL bUnpin;
    const CMINVOKECOMMANDINFOEX *piciEx = (const CMINVOKECOMMANDINFOEX *)pici;
    if (pici->cbSize >= sizeof(*piciEx) && (pici->fMask & CMIC_MASK_UNICODE) && !IS_INTRESOURCE(piciEx->lpVerbW))
    {
        if (!lstrcmpiW(piciEx->lpVerbW, L"taskbarpin"))
            bUnpin = FALSE;
        else if (!lstrcmpiW(piciEx->lpVerbW, L"taskbarunpin"))
            bUnpin = TRUE;
        else
            return E_INVALIDARG;
    }
    else if (!IS_INTRESOURCE(pici->lpVerb))
    {
        if (!lstrcmpiA(pici->lpVerb, "taskbarpin"))
            bUnpin = FALSE;
        else if (!lstrcmpiA(pici->lpVerb, "taskbarunpin"))
            bUnpin = TRUE;
        else
            return E_INVALIDARG;
    }
    else
    {
        if (!m_HasCommand || LOWORD(pici->lpVerb) != IDC_TASKBAR_PIN)
            return E_INVALIDARG;
        // The action belongs to the displayed menu, even if another menu changed the pin.
        bUnpin = m_Unpin;
    }

    if (IsTaskbarPinningDisabled())
        return HRESULT_FROM_WIN32(ERROR_ACCESS_DISABLED_BY_POLICY);

    CStringW Target;
    if (!ResolvePinTarget(m_Path, Target))
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    return bUnpin ? RemoveTaskbarPinForTarget(Target) : CreateTaskbarPin(m_Path);
}

STDMETHODIMP
CStartMenuPin::GetCommandString(UINT_PTR idCommand, UINT uFlags, UINT *pReserved, LPSTR pszName, UINT cchMax)
{
    if (uFlags == GCS_VALIDATEA || uFlags == GCS_VALIDATEW)
        return idCommand == IDC_TASKBAR_PIN && m_HasCommand ? S_OK : S_FALSE;
    if (idCommand != IDC_TASKBAR_PIN || !m_HasCommand || !pszName || cchMax == 0)
        return E_INVALIDARG;

    PCSTR pszVerbA = m_Unpin ? "taskbarunpin" : "taskbarpin";
    PCWSTR pszVerbW = m_Unpin ? L"taskbarunpin" : L"taskbarpin";
    PCWSTR pszHelpW = m_Unpin ? L"Unpin from Taskbar" : L"Pin to Taskbar";

    switch (uFlags)
    {
        case GCS_VERBA:
            return StringCchCopyA(pszName, cchMax, pszVerbA);
        case GCS_VERBW:
            return StringCchCopyW((LPWSTR)pszName, cchMax, pszVerbW);
        case GCS_HELPTEXTA:
            return WideCharToMultiByte(CP_ACP, 0, pszHelpW, -1, pszName, cchMax, NULL, NULL) ? S_OK :
                   HRESULT_FROM_WIN32(GetLastError());
        case GCS_HELPTEXTW:
            return StringCchCopyW((LPWSTR)pszName, cchMax, pszHelpW);
    }
    return E_NOTIMPL;
}

STDMETHODIMP
CStartMenuPin::RemoveFromList(IShellItem *pItem)
{
    if (!pItem)
        return E_INVALIDARG;

    CComHeapPtr<WCHAR> pszPath;
    HRESULT hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
    if (FAILED(hr))
        return hr;

    CStringW Target;
    if (!ResolvePinTarget(pszPath, Target))
        return S_OK;
    return RemoveTaskbarPinForTarget(Target);
}

STDMETHODIMP
CStartMenuPin::SetSite(IUnknown *pSite)
{
    m_Site = pSite;
    return S_OK;
}

STDMETHODIMP
CStartMenuPin::GetSite(REFIID riid, void **ppvSite)
{
    if (!ppvSite)
        return E_POINTER;
    *ppvSite = NULL;
    return m_Site ? m_Site->QueryInterface(riid, ppvSite) : E_FAIL;
}
