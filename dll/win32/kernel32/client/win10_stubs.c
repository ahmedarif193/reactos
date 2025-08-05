/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS system libraries
 * FILE:            dll/win32/kernel32/client/win10_stubs.c
 * PURPOSE:         Stub implementations for Windows 10 APIs
 * PROGRAMMERS:     ReactOS Development Team
 */

/* INCLUDES *******************************************************************/

#include <k32.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS ******************************************************************/

/*
 * Windows 10 Process and Thread APIs
 */

DWORD
WINAPI
GetCurrentProcessorNumberEx(PPROCESSOR_NUMBER ProcNumber)
{
    DPRINT1("GetCurrentProcessorNumberEx stub\n");
    if (ProcNumber)
    {
        ProcNumber->Group = 0;
        ProcNumber->Number = 0;
        ProcNumber->Reserved = 0;
    }
    return 0;
}

BOOL
WINAPI
GetLogicalProcessorInformationEx(LOGICAL_PROCESSOR_RELATIONSHIP RelationshipType,
                                  PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Buffer,
                                  PDWORD ReturnedLength)
{
    DPRINT1("GetLogicalProcessorInformationEx stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

DWORD
WINAPI
GetActiveProcessorCount(WORD GroupNumber)
{
    DPRINT1("GetActiveProcessorCount stub\n");
    return 1;
}

WORD
WINAPI
GetActiveProcessorGroupCount(VOID)
{
    DPRINT1("GetActiveProcessorGroupCount stub\n");
    return 1;
}

DWORD
WINAPI
GetMaximumProcessorCount(WORD GroupNumber)
{
    DPRINT1("GetMaximumProcessorCount stub\n");
    return 1;
}

WORD
WINAPI
GetMaximumProcessorGroupCount(VOID)
{
    DPRINT1("GetMaximumProcessorGroupCount stub\n");
    return 1;
}

/*
 * Windows 10 UWP and AppContainer APIs
 */

LONG
WINAPI
GetPackageFullName(HANDLE hProcess,
                   UINT32 *packageFullNameLength,
                   PWSTR packageFullName)
{
    DPRINT1("GetPackageFullName stub\n");
    return APPMODEL_ERROR_NO_PACKAGE;
}

LONG
WINAPI
GetPackageFamilyName(HANDLE hProcess,
                     UINT32 *packageFamilyNameLength,
                     PWSTR packageFamilyName)
{
    DPRINT1("GetPackageFamilyName stub\n");
    return APPMODEL_ERROR_NO_PACKAGE;
}

LONG
WINAPI
GetApplicationUserModelId(HANDLE hProcess,
                          UINT32 *applicationUserModelIdLength,
                          PWSTR applicationUserModelId)
{
    DPRINT1("GetApplicationUserModelId stub\n");
    return ERROR_NOT_SUPPORTED;
}

BOOL
WINAPI
IsAppContainerProcess(HANDLE hProcess,
                      PBOOL isAppContainer)
{
    DPRINT1("IsAppContainerProcess stub\n");
    if (isAppContainer)
        *isAppContainer = FALSE;
    return TRUE;
}

/*
 * Windows 10 Memory Management APIs
 */

BOOL
WINAPI
GetMemoryErrorHandlingCapabilities(PULONG Capabilities)
{
    DPRINT1("GetMemoryErrorHandlingCapabilities stub\n");
    if (Capabilities)
        *Capabilities = 0;
    return TRUE;
}

PVOID
WINAPI
RegisterBadMemoryNotification(PVOID Callback)
{
    DPRINT1("RegisterBadMemoryNotification stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

BOOL
WINAPI
UnregisterBadMemoryNotification(PVOID RegistrationHandle)
{
    DPRINT1("UnregisterBadMemoryNotification stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

DWORD
WINAPI
DiscardVirtualMemory(PVOID VirtualAddress,
                     SIZE_T Size)
{
    DPRINT1("DiscardVirtualMemory stub\n");
    return ERROR_CALL_NOT_IMPLEMENTED;
}

BOOL
WINAPI
PrefetchVirtualMemory(HANDLE hProcess,
                      ULONG_PTR NumberOfEntries,
                      PVOID VirtualAddresses,
                      ULONG Flags)
{
    DPRINT1("PrefetchVirtualMemory stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * Windows 10 File System APIs
 */

HANDLE
WINAPI
CreateFile2(LPCWSTR lpFileName,
            DWORD dwDesiredAccess,
            DWORD dwShareMode,
            DWORD dwCreationDisposition,
            PVOID pCreateExParams)
{
    DPRINT1("CreateFile2 stub\n");
    // For now, forward to CreateFileW with basic parameters
    return CreateFileW(lpFileName,
                       dwDesiredAccess,
                       dwShareMode,
                       NULL,
                       dwCreationDisposition,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
}

BOOL
WINAPI
GetFileInformationByHandleEx(HANDLE hFile,
                              FILE_INFO_BY_HANDLE_CLASS FileInformationClass,
                              LPVOID lpFileInformation,
                              DWORD dwBufferSize)
{
    DPRINT1("GetFileInformationByHandleEx stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}


/*
 * Windows 10 Synchronization APIs
 */


/*
 * Windows 10 Console APIs
 */


/*
 * Windows 10 Locale and Globalization APIs
 */

int
WINAPI
GetUserDefaultLocaleName(LPWSTR lpLocaleName,
                          int cchLocaleName)
{
    DPRINT1("GetUserDefaultLocaleName stub\n");
    if (lpLocaleName && cchLocaleName > 0)
    {
        wcscpy(lpLocaleName, L"en-US");
        return 6;
    }
    return 0;
}


LCID
WINAPI
LocaleNameToLCID(LPCWSTR lpName,
                  DWORD dwFlags)
{
    DPRINT1("LocaleNameToLCID stub\n");
    return LOCALE_USER_DEFAULT;
}

int
WINAPI
LCIDToLocaleName(LCID Locale,
                 LPWSTR lpName,
                 int cchName,
                 DWORD dwFlags)
{
    DPRINT1("LCIDToLocaleName stub\n");
    if (lpName && cchName > 0)
    {
        wcscpy(lpName, L"en-US");
        return 6;
    }
    return 0;
}

BOOL
WINAPI
IsValidLocaleName(LPCWSTR lpLocaleName)
{
    DPRINT1("IsValidLocaleName stub\n");
    return TRUE;
}

/*
 * Windows 10 Error Handling APIs
 */

VOID
WINAPI
RaiseFailFastException(PEXCEPTION_RECORD pExceptionRecord,
                       PCONTEXT pContextRecord,
                       DWORD dwFlags)
{
    DPRINT1("RaiseFailFastException stub\n");
    TerminateProcess(GetCurrentProcess(), STATUS_FATAL_APP_EXIT);
}



/*
 * Windows 10 DLL Management APIs
 */

HMODULE
WINAPI
LoadPackagedLibrary(LPCWSTR lpwLibFileName,
                    DWORD Reserved)
{
    DPRINT1("LoadPackagedLibrary stub\n");
    // Forward to LoadLibraryExW for basic functionality
    return LoadLibraryExW(lpwLibFileName, NULL, 0);
}

BOOL
WINAPI
SetDefaultDllDirectories(DWORD DirectoryFlags)
{
    DPRINT1("SetDefaultDllDirectories stub\n");
    return TRUE;
}

DLL_DIRECTORY_COOKIE
WINAPI
AddDllDirectory(PCWSTR NewDirectory)
{
    DPRINT1("AddDllDirectory stub\n");
    return NULL;
}

BOOL
WINAPI
RemoveDllDirectory(DLL_DIRECTORY_COOKIE Cookie)
{
    DPRINT1("RemoveDllDirectory stub\n");
    return TRUE;
}

/*
 * Additional Windows 10 APIs
 */

PVOID
WINAPI
VirtualAllocFromApp(PVOID BaseAddress,
                    SIZE_T Size,
                    DWORD AllocationType,
                    DWORD Protect)
{
    DPRINT1("VirtualAllocFromApp stub\n");
    return VirtualAlloc(BaseAddress, Size, AllocationType, Protect);
}

BOOL
WINAPI
VirtualProtectFromApp(PVOID BaseAddress,
                      SIZE_T Size,
                      DWORD NewProtect,
                      PDWORD OldProtect)
{
    DPRINT1("VirtualProtectFromApp stub\n");
    return VirtualProtect(BaseAddress, Size, NewProtect, OldProtect);
}

HANDLE
WINAPI
OpenFileMappingFromApp(DWORD DesiredAccess,
                       BOOL InheritHandle,
                       LPCWSTR Name)
{
    DPRINT1("OpenFileMappingFromApp stub\n");
    return OpenFileMappingW(DesiredAccess, InheritHandle, Name);
}

HANDLE
WINAPI
CreateFileMappingFromApp(HANDLE File,
                         PSECURITY_ATTRIBUTES FileMappingAttributes,
                         ULONG PageProtection,
                         ULONG64 MaximumSize,
                         PCWSTR Name)
{
    DPRINT1("CreateFileMappingFromApp stub\n");
    return CreateFileMappingW(File, FileMappingAttributes, PageProtection, 
                              (DWORD)(MaximumSize >> 32), (DWORD)MaximumSize, Name);
}

PVOID
WINAPI
MapViewOfFileFromApp(HANDLE FileMappingObject,
                     ULONG DesiredAccess,
                     ULONG64 FileOffset,
                     SIZE_T NumberOfBytesToMap)
{
    DPRINT1("MapViewOfFileFromApp stub\n");
    return MapViewOfFile(FileMappingObject, DesiredAccess,
                         (DWORD)(FileOffset >> 32), (DWORD)FileOffset,
                         NumberOfBytesToMap);
}

BOOL
WINAPI
UnmapViewOfFileEx(PVOID BaseAddress,
                  ULONG UnmapFlags)
{
    DPRINT1("UnmapViewOfFileEx stub\n");
    return UnmapViewOfFile(BaseAddress);
}

DWORD
WINAPI
OfferVirtualMemory(PVOID VirtualAddress,
                   SIZE_T Size,
                   DWORD Priority)
{
    DPRINT1("OfferVirtualMemory stub\n");
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD
WINAPI
ReclaimVirtualMemory(PVOID VirtualAddress,
                     SIZE_T Size)
{
    DPRINT1("ReclaimVirtualMemory stub\n");
    return ERROR_CALL_NOT_IMPLEMENTED;
}

BOOL
WINAPI
MapUserPhysicalPagesScatterNuma(PVOID VirtualAddresses,
                                 ULONG_PTR NumberOfPages,
                                 PULONG_PTR PageArray,
                                 USHORT NodeNumber)
{
    DPRINT1("MapUserPhysicalPagesScatterNuma stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}


BOOL
WINAPI
TryAcquireSRWLockExclusive(PSRWLOCK SRWLock)
{
    DPRINT1("TryAcquireSRWLockExclusive stub\n");
    return FALSE;
}

BOOL
WINAPI
TryAcquireSRWLockShared(PSRWLOCK SRWLock)
{
    DPRINT1("TryAcquireSRWLockShared stub\n");
    return FALSE;
}

