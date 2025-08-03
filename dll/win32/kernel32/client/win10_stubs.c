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
    return APPMODEL_ERROR_NO_APPLICATION_USER_MODEL_ID;
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
RegisterBadMemoryNotification(PBAD_MEMORY_CALLBACK_ROUTINE Callback)
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
                      PWIN32_MEMORY_RANGE_ENTRY VirtualAddresses,
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
            LPCREATEFILE2_EXTENDED_PARAMETERS pCreateExParams)
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

BOOL
WINAPI
SetFileInformationByHandle(HANDLE hFile,
                           FILE_INFO_BY_HANDLE_CLASS FileInformationClass,
                           LPVOID lpFileInformation,
                           DWORD dwBufferSize)
{
    DPRINT1("SetFileInformationByHandle stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * Windows 10 Synchronization APIs
 */

HANDLE
WINAPI
CreateEventExA(LPSECURITY_ATTRIBUTES lpEventAttributes,
               LPCSTR lpName,
               DWORD dwFlags,
               DWORD dwDesiredAccess)
{
    DPRINT1("CreateEventExA stub\n");
    // Forward to CreateEventA for basic functionality
    return CreateEventA(lpEventAttributes,
                        (dwFlags & CREATE_EVENT_MANUAL_RESET) ? TRUE : FALSE,
                        (dwFlags & CREATE_EVENT_INITIAL_SET) ? TRUE : FALSE,
                        lpName);
}

HANDLE
WINAPI
CreateEventExW(LPSECURITY_ATTRIBUTES lpEventAttributes,
               LPCWSTR lpName,
               DWORD dwFlags,
               DWORD dwDesiredAccess)
{
    DPRINT1("CreateEventExW stub\n");
    // Forward to CreateEventW for basic functionality
    return CreateEventW(lpEventAttributes,
                        (dwFlags & CREATE_EVENT_MANUAL_RESET) ? TRUE : FALSE,
                        (dwFlags & CREATE_EVENT_INITIAL_SET) ? TRUE : FALSE,
                        lpName);
}

HANDLE
WINAPI
CreateMutexExA(LPSECURITY_ATTRIBUTES lpMutexAttributes,
               LPCSTR lpName,
               DWORD dwFlags,
               DWORD dwDesiredAccess)
{
    DPRINT1("CreateMutexExA stub\n");
    // Forward to CreateMutexA for basic functionality
    return CreateMutexA(lpMutexAttributes,
                        (dwFlags & CREATE_MUTEX_INITIAL_OWNER) ? TRUE : FALSE,
                        lpName);
}

HANDLE
WINAPI
CreateMutexExW(LPSECURITY_ATTRIBUTES lpMutexAttributes,
               LPCWSTR lpName,
               DWORD dwFlags,
               DWORD dwDesiredAccess)
{
    DPRINT1("CreateMutexExW stub\n");
    // Forward to CreateMutexW for basic functionality
    return CreateMutexW(lpMutexAttributes,
                        (dwFlags & CREATE_MUTEX_INITIAL_OWNER) ? TRUE : FALSE,
                        lpName);
}

/*
 * Windows 10 Console APIs
 */

BOOL
WINAPI
GetConsoleScreenBufferInfoEx(HANDLE hConsoleOutput,
                             PCONSOLE_SCREEN_BUFFER_INFOEX lpConsoleScreenBufferInfoEx)
{
    DPRINT1("GetConsoleScreenBufferInfoEx stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
SetConsoleScreenBufferInfoEx(HANDLE hConsoleOutput,
                             PCONSOLE_SCREEN_BUFFER_INFOEX lpConsoleScreenBufferInfoEx)
{
    DPRINT1("SetConsoleScreenBufferInfoEx stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

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

int
WINAPI
GetSystemDefaultLocaleName(LPWSTR lpLocaleName,
                            int cchLocaleName)
{
    DPRINT1("GetSystemDefaultLocaleName stub\n");
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

DWORD
WINAPI
GetErrorMode(VOID)
{
    DPRINT1("GetErrorMode stub\n");
    return 0;
}

BOOL
WINAPI
SetThreadErrorMode(DWORD dwNewMode,
                   LPDWORD lpOldMode)
{
    DPRINT1("SetThreadErrorMode stub\n");
    if (lpOldMode)
        *lpOldMode = 0;
    return TRUE;
}

DWORD
WINAPI
GetThreadErrorMode(VOID)
{
    DPRINT1("GetThreadErrorMode stub\n");
    return 0;
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