/*
 * PROJECT:         avicap32
 * FILE:            dll\win32\avicap32\avicap32.c
 * PURPOSE:         Main file
 * PROGRAMMERS:     Dmitry Chapyshev (dmitry@reactos.org)
 */

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <winver.h>
#include <winnls.h>
#include <wingdi.h>
#include <winternl.h>
#include <strsafe.h>
#include <vfw.h>
#include <wine/debug.h>

#define CAP_DESC_MAX 32

WINE_DEFAULT_DEBUG_CHANNEL(avicap32);


HINSTANCE hInstance;

typedef struct _CAP_DRIVER_ENTRY
{
    WCHAR FileName[MAX_PATH];
    WCHAR FriendlyName[MAX_PATH];
} CAP_DRIVER_ENTRY, *PCAP_DRIVER_ENTRY;

static VOID
NormalizeDriverFileName(LPWSTR FileName)
{
    SIZE_T Length;
    WCHAR *Ptr;

    if (!FileName)
        return;

    while (*FileName == L' ' || *FileName == L'\t')
        RtlMoveMemory(FileName, FileName + 1, (lstrlenW(FileName) + 1) * sizeof(WCHAR));

    Length = lstrlenW(FileName);
    while (Length > 0 && (FileName[Length - 1] == L' ' || FileName[Length - 1] == L'\t'))
    {
        FileName[Length - 1] = L'\0';
        Length--;
    }

    Ptr = wcschr(FileName, L',');
    if (Ptr)
        *Ptr = L'\0';

    if (FileName[0] == L'"')
    {
        Length = lstrlenW(FileName);
        if (Length > 1 && FileName[Length - 1] == L'"')
            FileName[Length - 1] = L'\0';
        RtlMoveMemory(FileName, FileName + 1, (lstrlenW(FileName) + 1) * sizeof(WCHAR));
    }
}

static VOID
DeriveFriendlyName(LPWSTR Destination,
                   DWORD cchDestination,
                   LPCWSTR FileName)
{
    LPCWSTR BaseName;

    if (!Destination || !cchDestination)
        return;

    Destination[0] = L'\0';

    if (!FileName || !FileName[0])
        return;

    BaseName = wcsrchr(FileName, L'\\');
    if (BaseName)
        BaseName++;
    else
        BaseName = FileName;

    lstrcpynW(Destination, BaseName, cchDestination);
}

static VOID
DeduplicateDriverEntries(PCAP_DRIVER_ENTRY Entries,
                         DWORD *Count);

static BOOL
LoadIndirectFriendlyName(LPCWSTR Source,
                         LPWSTR Destination,
                         DWORD cchDestination)
{
    WCHAR PathBuffer[MAX_PATH];
    WCHAR ExpandedPath[MAX_PATH];
    DWORD ExpandedLength;
    LPCWSTR Comma;
    INT ResourceId;
    HMODULE hModule;

    if (!Source || Source[0] != L'@')
        return FALSE;

    Comma = wcsrchr(Source, L',');
    if (!Comma || Comma <= Source + 1)
        return FALSE;

    {
        const WCHAR *IdPtr = Comma + 1;
        WCHAR *EndPtr;
        LONG ParsedId;

        while (*IdPtr == L' ' || *IdPtr == L'\t')
            IdPtr++;

        if (!*IdPtr)
            return FALSE;

        ParsedId = wcstol(IdPtr, &EndPtr, 10);
        if (EndPtr == IdPtr)
            return FALSE;

        if (ParsedId < 0)
            ParsedId = -ParsedId;

        if (ParsedId == 0)
            return FALSE;

        ResourceId = (INT)ParsedId;
    }

    if ((Comma - (Source + 1)) >= ARRAYSIZE(PathBuffer))
        return FALSE;

    RtlZeroMemory(PathBuffer, sizeof(PathBuffer));
    RtlMoveMemory(PathBuffer, Source + 1, (Comma - (Source + 1)) * sizeof(WCHAR));

    ExpandedLength = ExpandEnvironmentStringsW(PathBuffer, ExpandedPath, ARRAYSIZE(ExpandedPath));
    if (!ExpandedLength || ExpandedLength >= ARRAYSIZE(ExpandedPath))
        return FALSE;

    hModule = LoadLibraryExW(ExpandedPath, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (!hModule)
        return FALSE;

    if (!LoadStringW(hModule, (UINT)ResourceId, Destination, cchDestination))
    {
        Destination[0] = L'\0';
        FreeLibrary(hModule);
        return FALSE;
    }

    FreeLibrary(hModule);
    return TRUE;
}

static BOOL
ResolveDriverPath(LPCWSTR FileName,
                  LPWSTR ResolvedPath,
                  DWORD cchResolvedPath)
{
    DWORD Length;

    if (!FileName || !FileName[0] || !ResolvedPath || !cchResolvedPath)
        return FALSE;

    Length = SearchPathW(NULL, FileName, NULL, cchResolvedPath, ResolvedPath, NULL);
    if (Length && Length < cchResolvedPath)
        return TRUE;

    return FALSE;
}

static VOID
QueryDriverVersionString(LPCWSTR FileName,
                         LPWSTR Destination,
                         INT cchDestination)
{
    LPVOID VersionData;
    DWORD InfoSize;
    VS_FIXEDFILEINFO *FixedInfo;
    UINT QuerySize;
    WCHAR ResolvedPath[MAX_PATH];
    WCHAR VersionBuffer[64];

    if (!Destination || cchDestination <= 0)
        return;

    Destination[0] = L'\0';

    if (!FileName || !FileName[0])
        return;

    if (!ResolveDriverPath(FileName, ResolvedPath, ARRAYSIZE(ResolvedPath)))
        lstrcpynW(ResolvedPath, FileName, ARRAYSIZE(ResolvedPath));

    InfoSize = GetFileVersionInfoSizeW(ResolvedPath, NULL);
    if (!InfoSize)
        return;

    VersionData = HeapAlloc(GetProcessHeap(), 0, InfoSize);
    if (!VersionData)
        return;

    if (GetFileVersionInfoW(ResolvedPath, 0, InfoSize, VersionData) &&
        VerQueryValueW(VersionData, L"\\", (LPVOID *)&FixedInfo, &QuerySize))
    {
        swprintf(VersionBuffer, L"Version: %d.%d.%d.%d",
                 HIWORD(FixedInfo->dwFileVersionMS),
                 LOWORD(FixedInfo->dwFileVersionMS),
                 HIWORD(FixedInfo->dwFileVersionLS),
                 LOWORD(FixedInfo->dwFileVersionLS));

        lstrcpynW(Destination, VersionBuffer, cchDestination);
    }

    HeapFree(GetProcessHeap(), 0, VersionData);
}

static BOOL
EnsureCapacity(PCAP_DRIVER_ENTRY *Entries,
               DWORD *Count,
               DWORD *Capacity)
{
    PCAP_DRIVER_ENTRY NewEntries;
    DWORD NewCapacity;

    if (*Count < *Capacity)
        return TRUE;

    NewCapacity = (*Capacity == 0) ? 4 : (*Capacity * 2);

    if (*Entries)
        NewEntries = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, *Entries, sizeof(CAP_DRIVER_ENTRY) * NewCapacity);
    else
        NewEntries = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(CAP_DRIVER_ENTRY) * NewCapacity);

    if (!NewEntries)
        return FALSE;

    *Entries = NewEntries;
    *Capacity = NewCapacity;
    return TRUE;
}

static BOOL
AddDriverEntry(PCAP_DRIVER_ENTRY *Entries,
               DWORD *Count,
               DWORD *Capacity,
               LPCWSTR FileName,
               LPCWSTR FriendlyName)
{
    CAP_DRIVER_ENTRY *EntryArray;
    WCHAR NormalizedFile[MAX_PATH];
    WCHAR LocalFriendly[MAX_PATH];

    if (!FileName || !FileName[0])
        return FALSE;

    lstrcpynW(NormalizedFile, FileName, ARRAYSIZE(NormalizedFile));
    NormalizeDriverFileName(NormalizedFile);
    if (!NormalizedFile[0])
        return FALSE;

    if (!EnsureCapacity(Entries, Count, Capacity))
        return FALSE;

    EntryArray = *Entries;
    lstrcpynW(EntryArray[*Count].FileName, NormalizedFile, ARRAYSIZE(EntryArray[*Count].FileName));

    RtlZeroMemory(LocalFriendly, sizeof(LocalFriendly));
    if (FriendlyName && FriendlyName[0])
    {
        if (!LoadIndirectFriendlyName(FriendlyName, LocalFriendly, ARRAYSIZE(LocalFriendly)))
        {
            lstrcpynW(LocalFriendly, FriendlyName, ARRAYSIZE(LocalFriendly));
        }
    }

    if (!LocalFriendly[0])
        DeriveFriendlyName(LocalFriendly, ARRAYSIZE(LocalFriendly), NormalizedFile);

    lstrcpynW(EntryArray[*Count].FriendlyName, LocalFriendly, ARRAYSIZE(EntryArray[*Count].FriendlyName));
    (*Count)++;

    return TRUE;
}

static VOID
EnumerateMediaResourceDrivers(PCAP_DRIVER_ENTRY *Entries,
                              DWORD *Count,
                              DWORD *Capacity)
{
    DWORD Index = 0;
    HKEY hKey;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\CurrentControlSet\\Control\\MediaResources\\msvideo",
                      0,
                      KEY_READ,
                      &hKey) != ERROR_SUCCESS)
    {
        return;
    }

    for (;;)
    {
        WCHAR SubKeyName[MAX_PATH];
        DWORD SubKeyLength = ARRAYSIZE(SubKeyName);
        HKEY hSubKey;

        if (RegEnumKeyExW(hKey,
                          Index,
                          SubKeyName,
                          &SubKeyLength,
                          NULL,
                          NULL,
                          NULL,
                          NULL) != ERROR_SUCCESS)
        {
            break;
        }

        if (RegOpenKeyExW(hKey, SubKeyName, 0, KEY_READ, &hSubKey) == ERROR_SUCCESS)
        {
            WCHAR DriverPath[MAX_PATH];
            WCHAR FriendlyName[MAX_PATH];
            DWORD DataSize;

            RtlZeroMemory(DriverPath, sizeof(DriverPath));
            DataSize = sizeof(DriverPath);
            if (RegQueryValueExW(hSubKey,
                                 L"Driver",
                                 NULL,
                                 NULL,
                                 (LPBYTE)DriverPath,
                                 &DataSize) == ERROR_SUCCESS)
            {
                RtlZeroMemory(FriendlyName, sizeof(FriendlyName));
                DataSize = sizeof(FriendlyName);

                if (RegQueryValueExW(hSubKey,
                                     L"FriendlyName",
                                     NULL,
                                     NULL,
                                     (LPBYTE)FriendlyName,
                                     &DataSize) == ERROR_SUCCESS)
                {
                    AddDriverEntry(Entries, Count, Capacity, DriverPath, FriendlyName);
                }
                else
                {
                    AddDriverEntry(Entries, Count, Capacity, DriverPath, NULL);
                }
            }

            RegCloseKey(hSubKey);
        }

        Index++;
    }

    RegCloseKey(hKey);
}

static VOID
EnumerateDrivers32Registry(PCAP_DRIVER_ENTRY *Entries,
                           DWORD *Count,
                           DWORD *Capacity)
{
    HKEY hDriversKey;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Drivers32",
                      0,
                      KEY_READ,
                      &hDriversKey) == ERROR_SUCCESS)
    {
        HKEY hDescKey = NULL;

        RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Drivers.desc",
                      0,
                      KEY_READ,
                      &hDescKey);

        for (DWORD Index = 0;; Index++)
        {
            WCHAR ValueName[128];
            BYTE DataBuffer[MAX_PATH * sizeof(WCHAR)];
            DWORD ValueNameLength = ARRAYSIZE(ValueName);
            DWORD DataSize = sizeof(DataBuffer);
            DWORD Type;

            if (RegEnumValueW(hDriversKey,
                              Index,
                              ValueName,
                              &ValueNameLength,
                              NULL,
                              &Type,
                              DataBuffer,
                              &DataSize) != ERROR_SUCCESS)
            {
                break;
            }

            if (Type == REG_SZ || Type == REG_EXPAND_SZ)
            {
                WCHAR DriverPath[MAX_PATH];
                WCHAR FriendlyName[MAX_PATH];

                RtlZeroMemory(DriverPath, sizeof(DriverPath));
                if (Type == REG_EXPAND_SZ)
                {
                    DWORD Expanded = ExpandEnvironmentStringsW((LPCWSTR)DataBuffer,
                                                               DriverPath,
                                                               ARRAYSIZE(DriverPath));
                    if (!Expanded || Expanded >= ARRAYSIZE(DriverPath))
                        lstrcpynW(DriverPath, (LPCWSTR)DataBuffer, ARRAYSIZE(DriverPath));
                }
                else
                {
                    lstrcpynW(DriverPath, (LPCWSTR)DataBuffer, ARRAYSIZE(DriverPath));
                }

                RtlZeroMemory(FriendlyName, sizeof(FriendlyName));
                if (hDescKey)
                {
                    DWORD DescSize = sizeof(FriendlyName);
                    if (RegQueryValueExW(hDescKey,
                                         ValueName,
                                         NULL,
                                         NULL,
                                         (LPBYTE)FriendlyName,
                                         &DescSize) != ERROR_SUCCESS)
                    {
                        FriendlyName[0] = L'\0';
                    }
                }

                AddDriverEntry(Entries, Count, Capacity, DriverPath, FriendlyName[0] ? FriendlyName : NULL);
            }
        }

        if (hDescKey)
            RegCloseKey(hDescKey);

        RegCloseKey(hDriversKey);
    }
}

static VOID
EnumerateSystemIniDrivers(PCAP_DRIVER_ENTRY *Entries,
                          DWORD *Count,
                          DWORD *Capacity)
{
    DWORD BufferSize = 32 * 1024;
    LPWSTR SectionBuffer;
    DWORD Characters;

    SectionBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, BufferSize * sizeof(WCHAR));
    if (!SectionBuffer)
        return;

    Characters = GetPrivateProfileSectionW(L"drivers32",
                                           SectionBuffer,
                                           BufferSize,
                                           L"system.ini");

    if (!Characters)
    {
        HeapFree(GetProcessHeap(), 0, SectionBuffer);
        return;
    }

    for (LPWSTR Entry = SectionBuffer; *Entry; Entry += lstrlenW(Entry) + 1)
    {
        LPCWSTR EqualSign = wcschr(Entry, L'=');
        WCHAR KeyName[64];
        WCHAR DriverPath[MAX_PATH];
        WCHAR FriendlyName[MAX_PATH];

        if (!EqualSign)
        {
            continue;
        }

        RtlZeroMemory(KeyName, sizeof(KeyName));
        RtlZeroMemory(DriverPath, sizeof(DriverPath));
        RtlZeroMemory(FriendlyName, sizeof(FriendlyName));

        {
            SIZE_T KeyLength = EqualSign - Entry;
            if (KeyLength >= ARRAYSIZE(KeyName))
                KeyLength = ARRAYSIZE(KeyName) - 1;

            RtlMoveMemory(KeyName, Entry, KeyLength * sizeof(WCHAR));
            KeyName[KeyLength] = L'\0';
        }

        lstrcpynW(DriverPath, EqualSign + 1, ARRAYSIZE(DriverPath));
        NormalizeDriverFileName(DriverPath);

        if (!DriverPath[0])
        {
            continue;
        }

        GetPrivateProfileStringW(L"drivers.desc",
                                 KeyName,
                                 L"",
                                 FriendlyName,
                                 ARRAYSIZE(FriendlyName),
                                 L"system.ini");

        AddDriverEntry(Entries, Count, Capacity, DriverPath, FriendlyName[0] ? FriendlyName : NULL);
    }

    HeapFree(GetProcessHeap(), 0, SectionBuffer);
}


/* INTRENAL FUNCTIONS **************************************************/

LRESULT
CALLBACK
CaptureWindowProc(HWND hwnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    switch (Msg)
    {
        case WM_CREATE:
            break;

        case WM_PAINT:
            break;

        case WM_DESTROY:
            break;
    }

    return DefWindowProc(hwnd, Msg, wParam, lParam);
}


/* FUNCTIONS ***********************************************************/

/*
 * implemented
 */
HWND
VFWAPI
capCreateCaptureWindowW(LPCWSTR lpszWindowName,
                        DWORD dwStyle,
                        INT x,
                        INT y,
                        INT nWidth,
                        INT nHeight,
                        HWND hWnd,
                        INT nID)
{
    WCHAR szWindowClass[] = L"ClsCapWin";
    WNDCLASSEXW WndClass = {0};
    DWORD dwExStyle = 0;

    FIXME("capCreateCaptureWindowW() not fully implemented!\n");

    WndClass.cbSize        = sizeof(WNDCLASSEXW);
    WndClass.lpszClassName = szWindowClass;
    WndClass.lpfnWndProc   = CaptureWindowProc; /* TODO: Implement CaptureWindowProc */
    WndClass.hInstance     = hInstance;
    WndClass.style         = CS_HREDRAW | CS_VREDRAW;
    WndClass.hCursor       = LoadCursorW(0, IDC_ARROW);
    WndClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);

    if (RegisterClassExW(&WndClass) == (ATOM)0)
    {
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            return NULL;
    }

    return CreateWindowExW(dwExStyle,
                           szWindowClass,
                           lpszWindowName,
                           dwStyle,
                           x, y,
                           nWidth,
                           nHeight,
                           hWnd,
                           ULongToHandle(nID),
                           hInstance,
                           NULL);
}

/*
 * implemented
 */
HWND
VFWAPI
capCreateCaptureWindowA(LPCSTR lpszWindowName,
                        DWORD dwStyle,
                        INT x,
                        INT y,
                        INT nWidth,
                        INT nHeight,
                        HWND hWnd,
                        INT nID)
{
    UNICODE_STRING Name;
    HWND Wnd;

    if (lpszWindowName)
        RtlCreateUnicodeStringFromAsciiz(&Name, lpszWindowName);
    else
        Name.Buffer = NULL;

    Wnd = capCreateCaptureWindowW(Name.Buffer,
                                  dwStyle,
                                  x, y,
                                  nWidth,
                                  nHeight,
                                  hWnd,
                                  nID);

    RtlFreeUnicodeString(&Name);
    return Wnd;
}


/*
 * implemented
 */
BOOL
VFWAPI
capGetDriverDescriptionW(WORD wDriverIndex,
                         LPWSTR lpszName,
                         INT cbName,
                         LPWSTR lpszVer,
                         INT cbVer)
{
    PCAP_DRIVER_ENTRY Entries = NULL;
    DWORD EntryCount = 0;
    DWORD Capacity = 0;
    BOOL Result = FALSE;

    if (lpszName && cbName)
        lpszName[0] = L'\0';

    if (lpszVer && cbVer)
        lpszVer[0] = L'\0';

    EnumerateMediaResourceDrivers(&Entries, &EntryCount, &Capacity);
    EnumerateDrivers32Registry(&Entries, &EntryCount, &Capacity);
    EnumerateSystemIniDrivers(&Entries, &EntryCount, &Capacity);

    DeduplicateDriverEntries(Entries, &EntryCount);

    if (wDriverIndex < EntryCount)
    {
        PCAP_DRIVER_ENTRY Entry = &Entries[wDriverIndex];

        TRACE("Returning capture driver %u -> %S (%S)\n",
              wDriverIndex,
              Entry->FriendlyName,
              Entry->FileName);

        if (lpszName && cbName)
            lstrcpynW(lpszName, Entry->FriendlyName, cbName);

        if (lpszVer && cbVer)
            QueryDriverVersionString(Entry->FileName, lpszVer, cbVer);

        Result = TRUE;
    }

    if (Entries)
        HeapFree(GetProcessHeap(), 0, Entries);

    return Result;
}


/*
 * implemented
 */
BOOL
VFWAPI
capGetDriverDescriptionA(WORD wDriverIndex,
                         LPSTR lpszName,
                         INT cbName,
                         LPSTR lpszVer,
                         INT cbVer)
{
    WCHAR DevName[CAP_DESC_MAX], DevVer[CAP_DESC_MAX];
    BOOL Result;

    Result = capGetDriverDescriptionW(wDriverIndex, DevName, CAP_DESC_MAX, DevVer, CAP_DESC_MAX);
    if (Result)
    {
        WideCharToMultiByte(CP_ACP, 0, DevName, -1, lpszName, cbName, NULL, NULL);
        WideCharToMultiByte(CP_ACP, 0, DevVer, -1, lpszVer, cbVer, NULL, NULL);
    }

    return Result;
}


/*
 * unimplemented
 */
VOID
VFWAPI
AppCleanup(HINSTANCE hInst)
{
    UNIMPLEMENTED;
}


/*
 * unimplemented
 */
DWORD
VFWAPI
videoThunk32(DWORD dwUnknown1, DWORD dwUnknown2, DWORD dwUnknown3, DWORD dwUnknown4, DWORD dwUnknown5)
{
    UNIMPLEMENTED;
    return 0;
}


BOOL
WINAPI
DllMain(IN HINSTANCE hinstDLL,
        IN DWORD dwReason,
        IN LPVOID lpvReserved)
{
    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:
            TRACE("avicap32 attached!\n");
            hInstance = hinstDLL;
            break;
    }

    return TRUE;
}
static VOID
DeduplicateDriverEntries(PCAP_DRIVER_ENTRY Entries,
                         DWORD *Count)
{
    if (!Entries || !Count || *Count < 2)
        return;

    for (DWORD i = 0; i < *Count; ++i)
    {
        for (DWORD j = i + 1; j < *Count;)
        {
            if (_wcsicmp(Entries[i].FileName, Entries[j].FileName) == 0)
            {
                if (j + 1 < *Count)
                {
                    RtlMoveMemory(&Entries[j],
                                  &Entries[j + 1],
                                  (*Count - (j + 1)) * sizeof(CAP_DRIVER_ENTRY));
                }
                (*Count)--;
            }
            else
            {
                j++;
            }
        }
    }
}
