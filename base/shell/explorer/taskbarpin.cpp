/*
 * PROJECT:     ReactOS Explorer
 * LICENSE:     GPL-3.0-or-later
 * PURPOSE:     Persistent taskbar pin storage helpers
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#include "precomp.h"
#include "taskbarpin.h"
#include <taskbarpinlock.h>
#include <taskbarpinicon.h>
#include <olectl.h>

static BOOL
NormalizeTaskbarPinPath(PCWSTR pszPath, CStringW &Path)
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
    Path = (cch && cch < _countof(szFull)) ? szFull : pszExpanded;
    cch = GetLongPathNameW(Path, szLong, _countof(szLong));
    if (cch && cch < _countof(szLong))
        Path = szLong;
    PathRemoveBackslashW(Path.GetBuffer());
    Path.ReleaseBuffer();
    return !Path.IsEmpty();
}

BOOL
TaskbarPin_ResolveTarget(PCWSTR pszSource, CStringW &Target)
{
    CStringW Source;
    if (!NormalizeTaskbarPinPath(pszSource, Source))
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
    return SUCCEEDED(hr) && szTarget[0] &&
           NormalizeTaskbarPinPath(szTarget, Target);
}

HRESULT
TaskbarPin_Launch(PCWSTR pszSource, HANDLE *phProcess)
{
    if (!phProcess)
        return E_POINTER;
    *phProcess = NULL;
    CStringW Source, Target, Arguments, Directory;
    if (!NormalizeTaskbarPinPath(pszSource, Source))
        return E_INVALIDARG;
    INT ShowCommand = SW_SHOWNORMAL;
    BOOL RunAs = FALSE;

    if (!lstrcmpiW(PathFindExtensionW(Source), L".lnk"))
    {
        CComPtr<IShellLinkW> Link;
        HRESULT hr = Link.CoCreateInstance(CLSID_ShellLink, IID_IShellLinkW, NULL, CLSCTX_INPROC_SERVER);
        CComPtr<IPersistFile> Persist;
        if (SUCCEEDED(hr))
            hr = Link->QueryInterface(IID_PPV_ARG(IPersistFile, &Persist));
        if (SUCCEEDED(hr))
            hr = Persist->Load(Source, STGM_READ);
        WCHAR Path[MAX_PATH] = L"";
        if (SUCCEEDED(hr))
            hr = Link->GetPath(Path, _countof(Path), NULL, SLGP_RAWPATH);
        if (FAILED(hr) || !Path[0] || !NormalizeTaskbarPinPath(Path, Target))
            return FAILED(hr) ? hr : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        const INT MaxCommand = 32768;
        PWSTR Buffer = Arguments.GetBuffer(MaxCommand);
        Buffer[0] = 0;
        hr = Link->GetArguments(Buffer, MaxCommand);
        Arguments.ReleaseBuffer();
        if (FAILED(hr))
            return hr;
        if (Arguments.GetLength() >= MaxCommand - 1)
            return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
        Buffer = Directory.GetBuffer(MaxCommand);
        Buffer[0] = 0;
        hr = Link->GetWorkingDirectory(Buffer, MaxCommand);
        Directory.ReleaseBuffer();
        if (FAILED(hr))
            return hr;
        if (Directory.GetLength() >= MaxCommand - 1)
            return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
        hr = Link->GetShowCmd(&ShowCommand);
        if (FAILED(hr))
            return hr;
        CComPtr<IShellLinkDataList> Data;
        DWORD Flags = 0;
        if (SUCCEEDED(Link->QueryInterface(IID_PPV_ARG(IShellLinkDataList, &Data))) && SUCCEEDED(Data->GetFlags(&Flags)))
            RunAs = (Flags & SLDF_RUNAS_USER) != 0;
    }
    else
        Target = Source;

    if (lstrcmpiW(PathFindExtensionW(Target), L".exe"))
        return HRESULT_FROM_WIN32(ERROR_BAD_EXE_FORMAT);
    DWORD Attributes = GetFileAttributesW(Target);
    if (Attributes == INVALID_FILE_ATTRIBUTES)
        return HRESULT_FROM_WIN32(GetLastError());
    if (Attributes & FILE_ATTRIBUTE_DIRECTORY)
        return HRESULT_FROM_WIN32(ERROR_BAD_EXE_FORMAT);
    if (!Directory.IsEmpty())
    {
        DWORD Count = ExpandEnvironmentStringsW(Directory, NULL, 0);
        if (!Count || Count > 32768)
            return HRESULT_FROM_WIN32(Count ? ERROR_FILENAME_EXCED_RANGE : GetLastError());
        CStringW Expanded;
        PWSTR Buffer = Expanded.GetBuffer(Count);
        DWORD Written = ExpandEnvironmentStringsW(Directory, Buffer, Count);
        DWORD Error = Written ? ERROR_SUCCESS : GetLastError();
        Expanded.ReleaseBuffer(Written && Written <= Count ? Written - 1 : 0);
        if (!Written || Written > Count)
            return HRESULT_FROM_WIN32(Written ? ERROR_INSUFFICIENT_BUFFER : (Error ? Error : ERROR_GEN_FAILURE));
        Directory = Expanded;
    }

    if (!RunAs)
    {
        // The application name is explicit: the saved .lnk never reaches Open With.
        CStringW CommandLine;
        CommandLine.Format(L"\"%s\"%s%s", Target.GetString(), Arguments.IsEmpty() ? L"" : L" ", Arguments.GetString());
        if (CommandLine.GetLength() >= 32767)
            return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
        STARTUPINFOW Startup = { sizeof(Startup) };
        Startup.dwFlags = STARTF_USESHOWWINDOW;
        Startup.wShowWindow = (WORD)ShowCommand;
        PROCESS_INFORMATION Process = {};
        BOOL Started = CreateProcessW(Target, CommandLine.GetBuffer(), NULL, NULL, FALSE, 0, NULL, Directory.IsEmpty() ? NULL : Directory.GetString(), &Startup, &Process);
        DWORD Error = Started ? ERROR_SUCCESS : GetLastError();
        CommandLine.ReleaseBuffer();
        if (Started)
        {
            CloseHandle(Process.hThread);
            *phProcess = Process.hProcess;
            return S_OK;
        }
        if (Error != ERROR_ELEVATION_REQUIRED)
            return Error ? HRESULT_FROM_WIN32(Error) : E_FAIL;
    }

    SHELLEXECUTEINFOW Execute = { sizeof(Execute) };
    Execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    Execute.lpVerb = L"runas";
    Execute.lpFile = Target;
    Execute.lpParameters = Arguments.IsEmpty() ? NULL : Arguments.GetString();
    Execute.lpDirectory = Directory.IsEmpty() ? NULL : Directory.GetString();
    Execute.nShow = ShowCommand;
    if (!ShellExecuteExW(&Execute))
    {
        DWORD Error = GetLastError();
        if (Execute.hProcess)
            CloseHandle(Execute.hProcess);
        return Error ? HRESULT_FROM_WIN32(Error) : E_FAIL;
    }
    *phProcess = Execute.hProcess;
    return S_OK;
}

static BOOL
ReadTaskbarPinPolicy(HKEY hRoot)
{
    DWORD dwValue = 0, cbValue = sizeof(dwValue), dwType = 0;
    return SHGetValueW(hRoot, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", L"NoPinningToTaskbar", &dwType, &dwValue, &cbValue) == ERROR_SUCCESS &&
           dwType == REG_DWORD && dwValue != 0;
}

BOOL
TaskbarPin_IsDisabled()
{
    return ReadTaskbarPinPolicy(HKEY_CURRENT_USER) || ReadTaskbarPinPolicy(HKEY_LOCAL_MACHINE);
}

BOOL
TaskbarPin_IsPinnable(PCWSTR pszSource, CStringW *pTarget)
{
    CStringW Target;
    if (!pszSource || PathIsDirectoryW(pszSource) ||
        ReadTaskbarPinPolicy(HKEY_CURRENT_USER) ||
        ReadTaskbarPinPolicy(HKEY_LOCAL_MACHINE) ||
        !TaskbarPin_ResolveTarget(pszSource, Target) ||
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

typedef struct _TASKBAR_PIN_DIR_ENTRY
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
} TASKBAR_PIN_DIR_ENTRY;

typedef struct _TASKBAR_PIN_IO_STATUS
{
    union
    {
        LONG Status;
        PVOID Pointer;
    };
    ULONG_PTR Information;
} TASKBAR_PIN_IO_STATUS;

extern "C" LONG NTAPI
NtQueryDirectoryFile(HANDLE FileHandle,
                     HANDLE Event,
                     PVOID ApcRoutine,
                     PVOID ApcContext,
                     TASKBAR_PIN_IO_STATUS *IoStatusBlock,
                     PVOID FileInformation,
                     ULONG Length,
                     ULONG FileInformationClass,
                     BOOLEAN ReturnSingleEntry,
                     PVOID FileName,
                     BOOLEAN RestartScan);

// FindFirstFile cannot open these directories for enumeration on this build,
// so the pin folder is read through a directory handle instead.
static HRESULT
EnumTaskbarPinFolder(PCWSTR pszFolder, TASKBAR_PIN_ENUM_PROC pfnCallback, LPARAM lParam)
{
    BYTE Buffer[8192];
    HRESULT hrRead = S_OK;
    BOOLEAN bRestart = TRUE;

    HANDLE hFolder = CreateFileW(pszFolder, FILE_LIST_DIRECTORY,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                 OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (hFolder == INVALID_HANDLE_VALUE)
    {
        DWORD error = GetLastError();
        return (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
                   ? S_OK
                   : HRESULT_FROM_WIN32(error);
    }

    for (;;)
    {
        TASKBAR_PIN_IO_STATUS IoStatus;

        ZeroMemory(&IoStatus, sizeof(IoStatus));
        if (NtQueryDirectoryFile(hFolder, NULL, NULL, NULL, &IoStatus, Buffer, sizeof(Buffer),
                                 1, FALSE, NULL, bRestart) < 0)
        {
            break;
        }
        bRestart = FALSE;

        for (ULONG Offset = 0;;)
        {
            const TASKBAR_PIN_DIR_ENTRY *pEntry = (const TASKBAR_PIN_DIR_ENTRY *)&Buffer[Offset];
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
                                               pszFolder, szName)))
                {
                    if (!TaskbarPin_ResolveTarget(szShortcut, Target))
                    {
                        hrRead = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                    }
                    else
                    {
                        FILETIME CreationTime;

                        CreationTime.dwLowDateTime = pEntry->CreationTime.LowPart;
                        CreationTime.dwHighDateTime = (DWORD)pEntry->CreationTime.HighPart;
                        if (!pfnCallback(szShortcut, Target, &CreationTime, lParam))
                        {
                            CloseHandle(hFolder);
                            return S_OK;
                        }
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
    return hrRead;
}

HRESULT
TaskbarPin_Enum(TASKBAR_PIN_ENUM_PROC pfnCallback, LPARAM lParam)
{
    if (!pfnCallback)
        return E_INVALIDARG;

    CStringW Folder;
    HRESULT hr = GetTaskbarPinFolder(FALSE, Folder);
    if (FAILED(hr))
    {
        return (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ||
                hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))
                   ? S_OK
                   : hr;
    }

    CTaskbarPinLock Lock;
    hr = Lock.Acquire(Folder);
    if (FAILED(hr))
        return hr;

    return EnumTaskbarPinFolder(Folder, pfnCallback, lParam);
}

struct TASKBAR_PIN_FIND_DATA
{
    PCWSTR pszTarget;
    CStringW *pShortcut;
    BOOL bFound;
};

static BOOL CALLBACK
FindTaskbarPinCallback(PCWSTR pszShortcut, PCWSTR pszTarget, const FILETIME *pCreationTime, LPARAM lParam)
{
    TASKBAR_PIN_FIND_DATA *pData = (TASKBAR_PIN_FIND_DATA *)lParam;
    if (lstrcmpiW(pData->pszTarget, pszTarget) != 0)
        return TRUE;

    if (pData->pShortcut)
        *pData->pShortcut = pszShortcut;
    pData->bFound = TRUE;
    return FALSE;
}

HRESULT
TaskbarPin_Find(PCWSTR pszTarget, CStringW *pShortcut)
{
    CStringW Target;
    if (!NormalizeTaskbarPinPath(pszTarget, Target))
        return E_INVALIDARG;

    TASKBAR_PIN_FIND_DATA Data = { Target, pShortcut, FALSE };
    HRESULT hr = TaskbarPin_Enum(FindTaskbarPinCallback, (LPARAM)&Data);
    return FAILED(hr) ? hr : Data.bFound ? S_OK : S_FALSE;
}

static HRESULT
ChooseTaskbarPinDestination(PCWSTR pszSource, PCWSTR pszTarget, CStringW &Destination)
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

static VOID
StampTaskbarPin(PCWSTR pszPath)
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
SaveTaskbarPinIcon(PCWSTR pszShortcut, HICON hIcon, CStringW &IconPath)
{
    CStringW Path;
    HRESULT hr = TaskbarPin_GetIconPath(pszShortcut, Path);
    if (FAILED(hr))
        return hr;

    PICTDESC Description = {};
    Description.cbSizeofstruct = sizeof(Description);
    Description.picType = PICTYPE_ICON;
    Description.icon.hicon = hIcon;
    CComPtr<IPicture> Picture;
    hr = OleCreatePictureIndirect(&Description, IID_IPicture, FALSE, (PVOID *)&Picture);
    CComPtr<IStream> Stream;
    if (SUCCEEDED(hr))
        hr = CreateStreamOnHGlobal(NULL, TRUE, &Stream);
    LONG cbIcon = 0;
    if (SUCCEEDED(hr))
        hr = Picture->SaveAsFile(Stream, TRUE, &cbIcon);
    if (FAILED(hr) || cbIcon <= 0)
        return FAILED(hr) ? hr : E_FAIL;

    HGLOBAL Buffer = NULL;
    hr = GetHGlobalFromStream(Stream, &Buffer);
    if (FAILED(hr))
        return hr;
    PVOID Data = GlobalLock(Buffer);
    if (!Data)
        return E_OUTOFMEMORY;
    HANDLE File = CreateFileW(Path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        hr = HRESULT_FROM_WIN32(GetLastError());
    else
    {
        IconPath = Path;
        DWORD Written = 0;
        if (!WriteFile(File, Data, cbIcon, &Written, NULL))
            hr = HRESULT_FROM_WIN32(GetLastError());
        else if (Written != (DWORD)cbIcon)
            hr = STG_E_WRITEFAULT;
        CloseHandle(File);
    }
    GlobalUnlock(Buffer);
    if (FAILED(hr))
        return hr;

    CComPtr<IShellLinkW> Link;
    hr = Link.CoCreateInstance(CLSID_ShellLink, IID_IShellLinkW, NULL, CLSCTX_INPROC_SERVER);
    CComPtr<IPersistFile> Persist;
    if (SUCCEEDED(hr))
        hr = Link->QueryInterface(IID_PPV_ARG(IPersistFile, &Persist));
    if (SUCCEEDED(hr))
        hr = Persist->Load(pszShortcut, STGM_READ);
    if (SUCCEEDED(hr))
        hr = Link->SetIconLocation(Path, 0);
    if (SUCCEEDED(hr))
        hr = Persist->Save(pszShortcut, TRUE);
    return hr;
}

HRESULT
TaskbarPin_Create(PCWSTR pszSource, CStringW *pShortcut, HICON hIcon)
{
    CStringW Target, Existing, Destination, Folder;
    if (!TaskbarPin_IsPinnable(pszSource, &Target))
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    HRESULT hr = GetTaskbarPinFolder(TRUE, Folder);
    if (FAILED(hr))
        return hr;
    CTaskbarPinLock Lock;
    hr = Lock.Acquire(Folder);
    if (FAILED(hr))
        return hr;
    hr = TaskbarPin_Find(Target, &Existing);
    if (FAILED(hr))
        return hr;
    if (hr == S_OK)
    {
        if (pShortcut)
            *pShortcut = Existing;
        return S_OK;
    }

    hr = ChooseTaskbarPinDestination(pszSource, Target, Destination);
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

    CStringW IconPath, SavedTarget;
    if (hIcon)
        hr = SaveTaskbarPinIcon(Destination, hIcon, IconPath);
    if (SUCCEEDED(hr) && (!TaskbarPin_ResolveTarget(Destination, SavedTarget) || SavedTarget.CompareNoCase(Target) != 0))
        hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (FAILED(hr))
    {
        DeleteFileW(Destination);
        if (!IconPath.IsEmpty())
            DeleteFileW(IconPath);
        return hr;
    }

    StampTaskbarPin(Destination);
    SHChangeNotify(SHCNE_CREATE, SHCNF_PATHW | SHCNF_FLUSHNOWAIT, Destination, NULL);
    if (pShortcut)
        *pShortcut = Destination;
    return S_OK;
}

HRESULT
TaskbarPin_Remove(PCWSTR pszTarget)
{
    if (TaskbarPin_IsDisabled())
        return HRESULT_FROM_WIN32(ERROR_ACCESS_DISABLED_BY_POLICY);
    CStringW Shortcut, Folder;
    HRESULT hr = GetTaskbarPinFolder(FALSE, Folder);
    if (FAILED(hr))
        return (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) ? S_OK : hr;
    CTaskbarPinLock Lock;
    hr = Lock.Acquire(Folder);
    if (FAILED(hr))
        return hr;
    while ((hr = TaskbarPin_Find(pszTarget, &Shortcut)) == S_OK)
    {
        CStringW IconPath;
        TaskbarPin_GetOwnedIconPath(Shortcut, IconPath);
        if (!DeleteFileW(Shortcut))
            return HRESULT_FROM_WIN32(GetLastError());
        if (!IconPath.IsEmpty())
            DeleteFileW(IconPath);
        SHChangeNotify(SHCNE_DELETE, SHCNF_PATHW | SHCNF_FLUSHNOWAIT, Shortcut, NULL);
        Shortcut.Empty();
    }
    return FAILED(hr) ? hr : S_OK;
}

#define TASKBAR_PIN_ORDER_KEY   L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Taskband"
#define TASKBAR_PIN_ORDER_VALUE L"ReactOSPinOrder"
#define TASKBAR_PIN_ORDER_MAX   0x10000

HRESULT
TaskbarPin_LoadOrder(CAtlArray<TASKBAR_PIN_ORDER> &Order)
{
    HKEY hKey;
    DWORD cbData = 0, dwType = 0;

    Order.SetCount(0);
    LONG error = RegOpenKeyExW(HKEY_CURRENT_USER, TASKBAR_PIN_ORDER_KEY, 0, KEY_QUERY_VALUE, &hKey);
    if (error != ERROR_SUCCESS)
        return error == ERROR_FILE_NOT_FOUND ? S_FALSE : HRESULT_FROM_WIN32(error);

    error = RegQueryValueExW(hKey, TASKBAR_PIN_ORDER_VALUE, NULL, &dwType, NULL, &cbData);
    if (error != ERROR_SUCCESS || dwType != REG_MULTI_SZ ||
        cbData < 2 * sizeof(WCHAR) || cbData > TASKBAR_PIN_ORDER_MAX ||
        cbData % sizeof(WCHAR))
    {
        RegCloseKey(hKey);
        if (error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND)
            return S_FALSE;
        return HRESULT_FROM_WIN32(error);
    }

    CAtlArray<WCHAR> Buffer;
    SIZE_T cch = cbData / sizeof(WCHAR);
    if (!Buffer.SetCount(cch + 2))
    {
        RegCloseKey(hKey);
        return E_OUTOFMEMORY;
    }
    ZeroMemory(Buffer.GetData(), (cch + 2) * sizeof(WCHAR));
    error = RegQueryValueExW(hKey, TASKBAR_PIN_ORDER_VALUE, NULL, &dwType,
                             (LPBYTE)Buffer.GetData(), &cbData);
    RegCloseKey(hKey);
    if (error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(error);
    if (dwType != REG_MULTI_SZ || cbData % sizeof(WCHAR))
        return S_FALSE;

    for (PCWSTR pszEntry = Buffer.GetData(); *pszEntry; pszEntry += lstrlenW(pszEntry) + 1)
    {
        TASKBAR_PIN_ORDER Entry;
        if (SUCCEEDED(StringCchCopyW(Entry.szTarget, _countof(Entry.szTarget), pszEntry)) &&
            Order.Add(Entry) == (SIZE_T)-1)
        {
            return E_OUTOFMEMORY;
        }
    }
    return S_OK;
}

HRESULT
TaskbarPin_SaveOrder(const CAtlArray<TASKBAR_PIN_ORDER> &Order)
{
    CAtlArray<WCHAR> Buffer;
    SIZE_T i, cch = Order.GetCount() ? 1 : 2;

    for (i = 0; i < Order.GetCount(); ++i)
        cch += lstrlenW(Order[i].szTarget) + 1;
    if (cch * sizeof(WCHAR) > TASKBAR_PIN_ORDER_MAX || !Buffer.SetCount(cch + 1))
        return E_OUTOFMEMORY;
    ZeroMemory(Buffer.GetData(), (cch + 1) * sizeof(WCHAR));

    PWSTR pszWrite = Buffer.GetData();
    for (i = 0; i < Order.GetCount(); ++i)
    {
        INT Length = lstrlenW(Order[i].szTarget);
        CopyMemory(pszWrite, Order[i].szTarget, Length * sizeof(WCHAR));
        pszWrite += Length + 1;
    }

    HKEY hKey;
    LONG error = RegCreateKeyExW(HKEY_CURRENT_USER, TASKBAR_PIN_ORDER_KEY, 0, NULL, 0,
                                 KEY_SET_VALUE, NULL, &hKey, NULL);
    if (error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(error);
    error = RegSetValueExW(hKey, TASKBAR_PIN_ORDER_VALUE, 0, REG_MULTI_SZ,
                           (const BYTE *)Buffer.GetData(), (DWORD)(cch * sizeof(WCHAR)));
    if (error == ERROR_SUCCESS)
        RegFlushKey(hKey);
    RegCloseKey(hKey);
    return error == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(error);
}
