/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS system libraries
 * PURPOSE:         Vista functions
 * PROGRAMMER:      Thomas Weidenmueller <w3seek@reactos.com>
 */

/* INCLUDES *******************************************************************/

#include <k32_vista.h>
#include <ndk/ntndk.h>
#include <stdio.h>
#include <wchar.h>

#if _WIN32_WINNT != _WIN32_WINNT_VISTA
#error "This file must be compiled with _WIN32_WINNT == _WIN32_WINNT_VISTA"
#endif

// This is defined only in ntifs.h
#define REPARSE_DATA_BUFFER_HEADER_SIZE   FIELD_OFFSET(REPARSE_DATA_BUFFER, GenericReparseBuffer)

#define NDEBUG
#include <debug.h>

NTSYSAPI
NTSTATUS
NTAPI
NtSetInformationProcess(
    _In_ HANDLE ProcessHandle,
    _In_ PROCESSINFOCLASS ProcessInformationClass,
    _In_reads_bytes_(ProcessInformationLength) PVOID ProcessInformation,
    _In_ ULONG ProcessInformationLength);

NTSYSAPI
NTSTATUS
NTAPI
NtFlushProcessWriteBuffers(VOID);

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @implemented
 */
BOOL
WINAPI
QueryFullProcessImageNameW(HANDLE hProcess,
                           DWORD dwFlags,
                           LPWSTR lpExeName,
                           PDWORD pdwSize)
{
    BYTE Buffer[sizeof(UNICODE_STRING) + MAX_PATH * sizeof(WCHAR)];
    UNICODE_STRING *DynamicBuffer = NULL;
    UNICODE_STRING *Result = NULL;
    NTSTATUS Status;
    DWORD Needed;

    Status = NtQueryInformationProcess(hProcess,
                                       ProcessImageFileName,
                                       Buffer,
                                       sizeof(Buffer) - sizeof(WCHAR),
                                       &Needed);
    if (Status == STATUS_INFO_LENGTH_MISMATCH)
    {
        DynamicBuffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, Needed + sizeof(WCHAR));
        if (!DynamicBuffer)
        {
            BaseSetLastNTError(STATUS_NO_MEMORY);
            return FALSE;
        }

        Status = NtQueryInformationProcess(hProcess,
                                           ProcessImageFileName,
                                           (LPBYTE)DynamicBuffer,
                                           Needed,
                                           &Needed);
        Result = DynamicBuffer;
    }
    else Result = (PUNICODE_STRING)Buffer;

    if (!NT_SUCCESS(Status)) goto Cleanup;

    if (Result->Length / sizeof(WCHAR) + 1 > *pdwSize)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Cleanup;
    }

    *pdwSize = Result->Length / sizeof(WCHAR);
    memcpy(lpExeName, Result->Buffer, Result->Length);
    lpExeName[*pdwSize] = 0;

Cleanup:
    RtlFreeHeap(RtlGetProcessHeap(), 0, DynamicBuffer);

    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
    }

    return !Status;
}


/*
 * @implemented
 */
BOOL
WINAPI
QueryFullProcessImageNameA(HANDLE hProcess,
                           DWORD dwFlags,
                           LPSTR lpExeName,
                           PDWORD pdwSize)
{
    DWORD pdwSizeW = *pdwSize;
    BOOL Result;
    LPWSTR lpExeNameW;

    lpExeNameW = RtlAllocateHeap(RtlGetProcessHeap(),
                                 HEAP_ZERO_MEMORY,
                                 *pdwSize * sizeof(WCHAR));
    if (!lpExeNameW)
    {
        BaseSetLastNTError(STATUS_NO_MEMORY);
        return FALSE;
    }

    Result = QueryFullProcessImageNameW(hProcess, dwFlags, lpExeNameW, &pdwSizeW);

    if (Result)
        Result = (0 != WideCharToMultiByte(CP_ACP, 0,
                                           lpExeNameW,
                                           -1,
                                           lpExeName,
                                           *pdwSize,
                                           NULL, NULL));

    if (Result)
        *pdwSize = strlen(lpExeName);

    RtlFreeHeap(RtlGetProcessHeap(), 0, lpExeNameW);
    return Result;
}


/*
 * @implemented
 */
BOOL
WINAPI
GetPhysicallyInstalledSystemMemory(PULONGLONG TotalMemoryInKilobytes)
{
    SYSTEM_BASIC_INFORMATION BasicInfo;
    NTSTATUS Status;

    if (!TotalMemoryInKilobytes)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Status = NtQuerySystemInformation(SystemBasicInformation,
                                      &BasicInfo,
                                      sizeof(BasicInfo),
                                      NULL);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    *TotalMemoryInKilobytes = ((ULONGLONG)BasicInfo.NumberOfPhysicalPages *
                               (ULONGLONG)BasicInfo.PageSize) / 1024ULL;
    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
GetProcessDEPPolicy(HANDLE hProcess,
                    LPDWORD lpFlags,
                    PBOOL lpPermanent)
{
    ULONG ExecuteFlags;
    NTSTATUS Status;

    if (!lpFlags && !lpPermanent)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Status = NtQueryInformationProcess(hProcess,
                                       ProcessExecuteFlags,
                                       &ExecuteFlags,
                                       sizeof(ExecuteFlags),
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    if (lpFlags)
    {
        DWORD Result = 0;

        if (ExecuteFlags & MEM_EXECUTE_OPTION_DISABLE)
            Result |= PROCESS_DEP_ENABLE;
        if (ExecuteFlags & MEM_EXECUTE_OPTION_DISABLE_THUNK_EMULATION)
            Result |= PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION;

        *lpFlags = Result;
    }

    if (lpPermanent)
        *lpPermanent = (ExecuteFlags & MEM_EXECUTE_OPTION_PERMANENT) != 0;

    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
SetProcessDEPPolicy(DWORD dwFlags)
{
    ULONG ExecuteFlags = 0;
    NTSTATUS Status;

    if (dwFlags & ~(PROCESS_DEP_ENABLE | PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if ((dwFlags & PROCESS_DEP_ENABLE) == 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    ExecuteFlags |= MEM_EXECUTE_OPTION_DISABLE | MEM_EXECUTE_OPTION_PERMANENT;
    if (dwFlags & PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION)
        ExecuteFlags |= MEM_EXECUTE_OPTION_DISABLE_THUNK_EMULATION;

    Status = NtSetInformationProcess(GetCurrentProcess(),
                                     ProcessExecuteFlags,
                                     &ExecuteFlags,
                                     sizeof(ExecuteFlags));
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
DEP_SYSTEM_POLICY_TYPE
WINAPI
GetSystemDEPPolicy(VOID)
{
    return (DEP_SYSTEM_POLICY_TYPE)SharedUserData->NXSupportPolicy;
}


/*
 * @implemented
 */
VOID
WINAPI
FlushProcessWriteBuffers(VOID)
{
    NTSTATUS Status;

    Status = NtFlushProcessWriteBuffers();
    if (!NT_SUCCESS(Status))
    {
        RtlRaiseStatus(Status);
    }
}


/*
 * @unimplemented
 */
HRESULT
WINAPI
GetApplicationRecoveryCallback(IN HANDLE hProcess,
                               OUT APPLICATION_RECOVERY_CALLBACK* pRecoveryCallback,
                               OUT PVOID* ppvParameter,
                               PDWORD dwPingInterval,
                               PDWORD dwFlags)
{
    UNIMPLEMENTED;
    return E_FAIL;
}


/*
 * @unimplemented
 */
HRESULT
WINAPI
GetApplicationRestart(IN HANDLE hProcess,
                      OUT PWSTR pwzCommandline  OPTIONAL,
                      IN OUT PDWORD pcchSize,
                      OUT PDWORD pdwFlags  OPTIONAL)
{
    UNIMPLEMENTED;
    return E_FAIL;
}


/*
 * @unimplemented
 */
VOID
WINAPI
ApplicationRecoveryFinished(IN BOOL bSuccess)
{
    UNIMPLEMENTED;
}


/*
 * @unimplemented
 */
HRESULT
WINAPI
ApplicationRecoveryInProgress(OUT PBOOL pbCancelled)
{
    UNIMPLEMENTED;
    return E_FAIL;
}


/*
 * @unimplemented
 */
HRESULT
WINAPI
RegisterApplicationRecoveryCallback(IN APPLICATION_RECOVERY_CALLBACK pRecoveryCallback,
                                    IN PVOID pvParameter  OPTIONAL,
                                    DWORD dwPingInterval,
                                    DWORD dwFlags)
{
    UNIMPLEMENTED;
    return E_FAIL;
}


/*
 * @unimplemented
 */
HRESULT
WINAPI
RegisterApplicationRestart(IN PCWSTR pwzCommandline  OPTIONAL,
                           IN DWORD dwFlags)
{
    UNIMPLEMENTED;
    return E_FAIL;
}


/*
 * @implemented
 */
BOOLEAN
WINAPI
CreateSymbolicLinkW(IN LPCWSTR lpSymlinkFileName,
                    IN LPCWSTR lpTargetFileName,
                    IN DWORD dwFlags)
{
    IO_STATUS_BLOCK IoStatusBlock;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE hSymlink = NULL;
    UNICODE_STRING SymlinkFileName = { 0, 0, NULL };
    UNICODE_STRING TargetFileName = { 0, 0, NULL };
    BOOLEAN bAllocatedTarget = FALSE, bRelativePath = FALSE;
    LPWSTR lpTargetFullFileName = NULL;
    SIZE_T cbPrintName;
    SIZE_T cbReparseData;
    PREPARSE_DATA_BUFFER pReparseData = NULL;
    PBYTE pBufTail;
    NTSTATUS Status;
    ULONG dwCreateOptions;
    DWORD dwErr;

    if(!lpSymlinkFileName || !lpTargetFileName || (dwFlags | SYMBOLIC_LINK_FLAG_DIRECTORY) != SYMBOLIC_LINK_FLAG_DIRECTORY)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if(dwFlags & SYMBOLIC_LINK_FLAG_DIRECTORY)
        dwCreateOptions = FILE_DIRECTORY_FILE;
    else
        dwCreateOptions = FILE_NON_DIRECTORY_FILE;

    switch(RtlDetermineDosPathNameType_U(lpTargetFileName))
    {
    case RtlPathTypeUnknown:
    case RtlPathTypeRooted:
    case RtlPathTypeRelative:
        bRelativePath = TRUE;
        RtlInitUnicodeString(&TargetFileName, lpTargetFileName);
        break;

    case RtlPathTypeDriveRelative:
        {
            LPWSTR FilePart;
            SIZE_T cchTargetFullFileName;

            cchTargetFullFileName = GetFullPathNameW(lpTargetFileName, 0, NULL, &FilePart);

            if(cchTargetFullFileName == 0)
            {
                dwErr = GetLastError();
                goto Cleanup;
            }

            lpTargetFullFileName = RtlAllocateHeap(RtlGetProcessHeap(), 0, cchTargetFullFileName * sizeof(WCHAR));

            if(lpTargetFullFileName == NULL)
            {
                dwErr = ERROR_NOT_ENOUGH_MEMORY;
                goto Cleanup;
            }

            if(GetFullPathNameW(lpTargetFileName, cchTargetFullFileName, lpTargetFullFileName, &FilePart) == 0)
            {
                dwErr = GetLastError();
                goto Cleanup;
            }
        }

        lpTargetFileName = lpTargetFullFileName;

        // fallthrough

    case RtlPathTypeUncAbsolute:
    case RtlPathTypeDriveAbsolute:
    case RtlPathTypeLocalDevice:
    case RtlPathTypeRootLocalDevice:
    default:
        if(!RtlDosPathNameToNtPathName_U(lpTargetFileName, &TargetFileName, NULL, NULL))
        {
            bAllocatedTarget = TRUE;
            dwErr = ERROR_INVALID_PARAMETER;
            goto Cleanup;
        }
    }

    cbPrintName = wcslen(lpTargetFileName) * sizeof(WCHAR);
    cbReparseData = FIELD_OFFSET(REPARSE_DATA_BUFFER, SymbolicLinkReparseBuffer.PathBuffer) + TargetFileName.Length + cbPrintName;
    pReparseData = RtlAllocateHeap(RtlGetProcessHeap(), 0, cbReparseData);

    if(pReparseData == NULL)
    {
        dwErr = ERROR_NOT_ENOUGH_MEMORY;
        goto Cleanup;
    }

    pBufTail = (PBYTE)(pReparseData->SymbolicLinkReparseBuffer.PathBuffer);

    pReparseData->ReparseTag = (ULONG)IO_REPARSE_TAG_SYMLINK;
    pReparseData->ReparseDataLength = (USHORT)cbReparseData - REPARSE_DATA_BUFFER_HEADER_SIZE;
    pReparseData->Reserved = 0;

    pReparseData->SymbolicLinkReparseBuffer.SubstituteNameOffset = 0;
    pReparseData->SymbolicLinkReparseBuffer.SubstituteNameLength = TargetFileName.Length;
    pBufTail += pReparseData->SymbolicLinkReparseBuffer.SubstituteNameOffset;
    RtlCopyMemory(pBufTail, TargetFileName.Buffer, TargetFileName.Length);

    pReparseData->SymbolicLinkReparseBuffer.PrintNameOffset = pReparseData->SymbolicLinkReparseBuffer.SubstituteNameLength;
    pReparseData->SymbolicLinkReparseBuffer.PrintNameLength = (USHORT)cbPrintName;
    pBufTail += pReparseData->SymbolicLinkReparseBuffer.PrintNameOffset;
    RtlCopyMemory(pBufTail, lpTargetFileName, cbPrintName);

    pReparseData->SymbolicLinkReparseBuffer.Flags = 0;

    if(bRelativePath)
        pReparseData->SymbolicLinkReparseBuffer.Flags |= 1; // TODO! give this lone flag a name

    if(!RtlDosPathNameToNtPathName_U(lpSymlinkFileName, &SymlinkFileName, NULL, NULL))
    {
        dwErr = ERROR_PATH_NOT_FOUND;
        goto Cleanup;
    }

    InitializeObjectAttributes(&ObjectAttributes, &SymlinkFileName, OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status = NtCreateFile
    (
        &hSymlink,
        FILE_WRITE_ATTRIBUTES | DELETE | SYNCHRONIZE,
        &ObjectAttributes,
        &IoStatusBlock,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        0,
        FILE_CREATE,
        FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | dwCreateOptions,
        NULL,
        0
    );

    if(!NT_SUCCESS(Status))
    {
        dwErr = RtlNtStatusToDosError(Status);
        goto Cleanup;
    }

    Status = NtFsControlFile
    (
        hSymlink,
        NULL,
        NULL,
        NULL,
        &IoStatusBlock,
        FSCTL_SET_REPARSE_POINT,
        pReparseData,
        cbReparseData,
        NULL,
        0
    );

    if(!NT_SUCCESS(Status))
    {
        FILE_DISPOSITION_INFORMATION DispInfo;
        DispInfo.DeleteFile = TRUE;
        NtSetInformationFile(hSymlink, &IoStatusBlock, &DispInfo, sizeof(DispInfo), FileDispositionInformation);

        dwErr = RtlNtStatusToDosError(Status);
        goto Cleanup;
    }

    dwErr = NO_ERROR;

Cleanup:
    if(hSymlink)
        NtClose(hSymlink);

    RtlFreeUnicodeString(&SymlinkFileName);
    if (bAllocatedTarget)
    {
        RtlFreeHeap(RtlGetProcessHeap(),
                    0,
                    TargetFileName.Buffer);
    }

    if(lpTargetFullFileName)
        RtlFreeHeap(RtlGetProcessHeap(), 0, lpTargetFullFileName);

    if(pReparseData)
        RtlFreeHeap(RtlGetProcessHeap(), 0, pReparseData);

    if(dwErr)
    {
        SetLastError(dwErr);
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
BOOLEAN
NTAPI
CreateSymbolicLinkA(IN LPCSTR lpSymlinkFileName,
                    IN LPCSTR lpTargetFileName,
                    IN DWORD dwFlags)
{
    PWCHAR SymlinkW, TargetW;
    BOOLEAN Ret;

    if(!lpSymlinkFileName || !lpTargetFileName)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!(SymlinkW = FilenameA2W(lpSymlinkFileName, FALSE)))
        return FALSE;

    if (!(TargetW = FilenameA2W(lpTargetFileName, TRUE)))
        return FALSE;

    Ret = CreateSymbolicLinkW(SymlinkW,
                              TargetW,
                              dwFlags);

    RtlFreeHeap(RtlGetProcessHeap(), 0, SymlinkW);
    RtlFreeHeap(RtlGetProcessHeap(), 0, TargetW);

    return Ret;
}


/*
 * @implemented
 */
DWORD
WINAPI
GetFinalPathNameByHandleA(IN HANDLE hFile,
                          OUT LPSTR lpszFilePath,
                          IN DWORD cchFilePath,
                          IN DWORD dwFlags)
{
    WCHAR FilePathW[MAX_PATH];
    UNICODE_STRING FilePathU;
    DWORD PrevLastError;
    DWORD Ret = 0;

    if (cchFilePath != 0 &&
        cchFilePath > sizeof(FilePathW) / sizeof(FilePathW[0]))
    {
        FilePathU.Length = 0;
        FilePathU.MaximumLength = (USHORT)cchFilePath * sizeof(WCHAR);
        FilePathU.Buffer = RtlAllocateHeap(RtlGetProcessHeap(),
                                           0,
                                           FilePathU.MaximumLength);
        if (FilePathU.Buffer == NULL)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
    }
    else
    {
        FilePathU.Length = 0;
        FilePathU.MaximumLength = sizeof(FilePathW);
        FilePathU.Buffer = FilePathW;
    }

    /* save the last error code */
    PrevLastError = GetLastError();
    SetLastError(ERROR_SUCCESS);

    /* call the unicode version that does all the work */
    Ret = GetFinalPathNameByHandleW(hFile,
                                    FilePathU.Buffer,
                                    cchFilePath,
                                    dwFlags);

    if (GetLastError() == ERROR_SUCCESS)
    {
        /* no error, restore the last error code and convert the string */
        SetLastError(PrevLastError);

        Ret = FilenameU2A_FitOrFail(lpszFilePath,
                                    cchFilePath,
                                    &FilePathU);
    }

    /* free allocated memory if necessary */
    if (FilePathU.Buffer != FilePathW)
    {
        RtlFreeHeap(RtlGetProcessHeap(),
                    0,
                    FilePathU.Buffer);
    }

    return Ret;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
SetFileBandwidthReservation(IN HANDLE hFile,
                            IN DWORD nPeriodMilliseconds,
                            IN DWORD nBytesPerPeriod,
                            IN BOOL bDiscardable,
                            OUT LPDWORD lpTransferSize,
                            OUT LPDWORD lpNumOutstandingRequests)
{
    UNIMPLEMENTED;
    return FALSE;
}


/*
 * @unimplemented
 */
BOOL
WINAPI
GetFileBandwidthReservation(IN HANDLE hFile,
                            OUT LPDWORD lpPeriodMilliseconds,
                            OUT LPDWORD lpBytesPerPeriod,
                            OUT LPBOOL pDiscardable,
                            OUT LPDWORD lpTransferSize,
                            OUT LPDWORD lpNumOutstandingRequests)
{
    UNIMPLEMENTED;
    return FALSE;
}


/*
 * @unimplemented
 */
HANDLE
WINAPI
OpenFileById(IN HANDLE hFile,
             IN LPFILE_ID_DESCRIPTOR lpFileID,
             IN DWORD dwDesiredAccess,
             IN DWORD dwShareMode,
             IN LPSECURITY_ATTRIBUTES lpSecurityAttributes  OPTIONAL,
             IN DWORD dwFlags)
{
    UNIMPLEMENTED;
    return INVALID_HANDLE_VALUE;
}



/*
  Vista+ MUI support functions

  References:
   Evolution of MUI Support across Windows Versions: https://learn.microsoft.com/en-us/windows/win32/intl/evolution-of-mui-support-across-windows-versions
   Comparing Windows XP Professional Multilingual Options: https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-xp/bb457045(v=technet.10)?redirectedfrom=MSDN

  More info:
   https://web.archive.org/web/20170930153551/http://msdn.microsoft.com/en-us/goglobal/bb978454.aspx
   https://learn.microsoft.com/en-us/windows/win32/intl/multilingual-user-interface-functions
*/

/* FUNCTIONS *****************************************************************/

BOOL
WINAPI
GetFileMUIInfo(
    DWORD dwFlags,
    PCWSTR pcwszFilePath,
    PFILEMUIINFO pFileMUIInfo,
    DWORD *pcbFileMUIInfo)
{
    static const DWORD AllowedFlags =
        MUI_QUERY_TYPE | MUI_QUERY_CHECKSUM | MUI_QUERY_LANGUAGE_NAME | MUI_QUERY_RESOURCE_TYPES;
    DWORD EffectiveFlags;
    DWORD RequiredSize;
    DWORD Attributes;
    WCHAR LanguageBuffer[LOCALE_NAME_MAX_LENGTH];
    ULONG LanguageLength;
    ULONG LanguageBytes;
    ULONG Offset;

    if (!pcwszFilePath || !pcbFileMUIInfo)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if ((dwFlags & ~AllowedFlags) != 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Attributes = GetFileAttributesW(pcwszFilePath);
    if (Attributes == INVALID_FILE_ATTRIBUTES)
        return FALSE;

    EffectiveFlags = dwFlags;
    if (EffectiveFlags == 0)
        EffectiveFlags = MUI_QUERY_TYPE | MUI_QUERY_LANGUAGE_NAME;

    LanguageLength = 0;
    LanguageBytes = 0;
    if (EffectiveFlags & MUI_QUERY_LANGUAGE_NAME)
    {
        int Copied = GetUserDefaultLocaleName(LanguageBuffer, ARRAYSIZE(LanguageBuffer));
        if (Copied <= 0)
        {
            wcscpy(LanguageBuffer, L"en-US");
            LanguageLength = 6;
        }
        else
        {
            LanguageLength = (ULONG)Copied;
        }

        LanguageBytes = LanguageLength * sizeof(WCHAR);
    }

    Offset = FIELD_OFFSET(FILEMUIINFO, abBuffer);
    RequiredSize = Offset + LanguageBytes;

    if (!pFileMUIInfo || *pcbFileMUIInfo < RequiredSize)
    {
        *pcbFileMUIInfo = RequiredSize;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    RtlZeroMemory(pFileMUIInfo, *pcbFileMUIInfo);
    pFileMUIInfo->dwSize = RequiredSize;
    pFileMUIInfo->dwVersion = MUI_FILEINFO_VERSION;

    if (EffectiveFlags & MUI_QUERY_TYPE)
        pFileMUIInfo->dwFileType = MUI_FILETYPE_NOT_LANGUAGE_NEUTRAL;

    if (EffectiveFlags & MUI_QUERY_LANGUAGE_NAME)
    {
        pFileMUIInfo->dwLanguageNameOffset = Offset;
        RtlCopyMemory((PBYTE)pFileMUIInfo + Offset, LanguageBuffer, LanguageBytes);
    }

    if (EffectiveFlags & MUI_QUERY_CHECKSUM)
    {
        RtlZeroMemory(pFileMUIInfo->pChecksum, sizeof(pFileMUIInfo->pChecksum));
        RtlZeroMemory(pFileMUIInfo->pServiceChecksum, sizeof(pFileMUIInfo->pServiceChecksum));
    }

    *pcbFileMUIInfo = RequiredSize;
    return TRUE;
}

static BOOL
K32VistaCopyMuiString(PCWSTR Source,
                      PWSTR Destination,
                      PULONG InOutLength)
{
    ULONG Required;

    if (!InOutLength)
        return FALSE;

    Required = (ULONG)(wcslen(Source) + 1);
    if (!Destination || *InOutLength < Required)
    {
        *InOutLength = Required;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    wmemcpy(Destination, Source, Required);
    *InOutLength = Required;
    return TRUE;
}

static BOOL
K32VistaMuiCandidateExists(PCWSTR Candidate)
{
    DWORD Attributes;

    if (!Candidate)
        return FALSE;

    Attributes = GetFileAttributesW(Candidate);
    return Attributes != INVALID_FILE_ATTRIBUTES && !(Attributes & FILE_ATTRIBUTE_DIRECTORY);
}

BOOL
WINAPI
GetFileMUIPath(
    DWORD dwFlags,
    PCWSTR pcwszFilePath,
    PWSTR pwszLanguage,
    PULONG pcchLanguage,
    PWSTR pwszFileMUIPath,
    PULONG pcchFileMUIPath,
    PULONGLONG pululEnumerator)
{
    static const DWORD AllowedFlags = MUI_LANGUAGE_NAME | MUI_LANGUAGE_ID | MUI_MACHINE_LANGUAGE_SETTINGS;
    WCHAR LanguageName[LOCALE_NAME_MAX_LENGTH];
    WCHAR LanguageId[5];
    const WCHAR *LanguageValue = L"";
    ULONG LanguageChars = 1; /* include terminator */
    WCHAR *FullPath = NULL;
    WCHAR *CandidatePrimary = NULL;
    WCHAR *CandidateAlt = NULL;
    WCHAR *FilePart = NULL;
    DWORD FullLength;
    DWORD ActualLength;
    BOOL AppendSlash;
    BOOL Success = FALSE;

    if (!pcwszFilePath || !pcchFileMUIPath)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if ((dwFlags & ~AllowedFlags) != 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if ((dwFlags & (MUI_LANGUAGE_NAME | MUI_LANGUAGE_ID)) == (MUI_LANGUAGE_NAME | MUI_LANGUAGE_ID))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (pwszLanguage && !pcchLanguage)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if ((dwFlags & (MUI_LANGUAGE_NAME | MUI_LANGUAGE_ID)) && !pcchLanguage)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (dwFlags & MUI_LANGUAGE_ID)
    {
        LANGID LangId = GetUserDefaultUILanguage();
        _snwprintf(LanguageId, ARRAYSIZE(LanguageId), L"%04x", LangId);
        LanguageValue = LanguageId;
        LanguageChars = (ULONG)wcslen(LanguageValue) + 1;
    }
    else
    {
        int Copied = GetUserDefaultLocaleName(LanguageName, ARRAYSIZE(LanguageName));
        if (Copied <= 0)
        {
            wcscpy(LanguageName, L"en-US");
            LanguageChars = 6;
        }
        else
        {
            LanguageChars = (ULONG)Copied;
        }

        LanguageValue = LanguageName;
    }

    FullLength = GetFullPathNameW(pcwszFilePath, 0, NULL, NULL);
    if (FullLength == 0)
        goto Cleanup;

    FullPath = RtlAllocateHeap(RtlGetProcessHeap(), 0, (FullLength + 1) * sizeof(WCHAR));
    if (!FullPath)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        goto Cleanup;
    }

    ActualLength = GetFullPathNameW(pcwszFilePath, FullLength + 1, FullPath, &FilePart);
    if (ActualLength == 0 || ActualLength > FullLength)
        goto Cleanup;

    CandidatePrimary = RtlAllocateHeap(RtlGetProcessHeap(), 0, (ActualLength + 5) * sizeof(WCHAR));
    if (!CandidatePrimary)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        goto Cleanup;
    }

    if (_snwprintf(CandidatePrimary, ActualLength + 5, L"%ls.mui", FullPath) < 0)
        goto Cleanup;

    if (FilePart && *LanguageValue)
    {
        SIZE_T DirectoryChars = (SIZE_T)(FilePart - FullPath);
        SIZE_T FileNameChars = wcslen(FilePart);
        AppendSlash = DirectoryChars > 0 && FullPath[DirectoryChars - 1] != L'\\';

        SIZE_T TotalChars = DirectoryChars + (AppendSlash ? 1 : 0) + (LanguageChars - 1) + 1 + FileNameChars + 4 + 1;
        CandidateAlt = RtlAllocateHeap(RtlGetProcessHeap(), 0, TotalChars * sizeof(WCHAR));
        if (!CandidateAlt)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            goto Cleanup;
        }

        if (_snwprintf(CandidateAlt, TotalChars, L"%.*ls%ls%ls\\%ls.mui",
                     (int)DirectoryChars,
                     FullPath,
                     AppendSlash ? L"\\" : L"",
                     LanguageValue,
                     FilePart) < 0)
        {
            goto Cleanup;
        }
    }

    if (K32VistaMuiCandidateExists(CandidatePrimary))
    {
        Success = TRUE;
    }
    else if (K32VistaMuiCandidateExists(CandidateAlt))
    {
        Success = TRUE;
        RtlFreeHeap(RtlGetProcessHeap(), 0, CandidatePrimary);
        CandidatePrimary = CandidateAlt;
        CandidateAlt = NULL;
    }
    else
    {
        SetLastError(ERROR_FILE_NOT_FOUND);
        goto Cleanup;
    }

    if (pcchLanguage)
    {
        if (!K32VistaCopyMuiString(LanguageValue, pwszLanguage, pcchLanguage))
        {
            Success = FALSE;
            goto Cleanup;
        }
    }

    if (!K32VistaCopyMuiString(CandidatePrimary, pwszFileMUIPath, pcchFileMUIPath))
    {
        Success = FALSE;
        goto Cleanup;
    }

    if (pululEnumerator)
        *pululEnumerator = 0;

    Success = TRUE;

Cleanup:
    if (CandidateAlt)
        RtlFreeHeap(RtlGetProcessHeap(), 0, CandidateAlt);
    if (CandidatePrimary)
        RtlFreeHeap(RtlGetProcessHeap(), 0, CandidatePrimary);
    if (FullPath)
        RtlFreeHeap(RtlGetProcessHeap(), 0, FullPath);

    return Success;
}

/*
 * @unimplemented
 */
#if 0 // This is Windows 7+
BOOL
WINAPI
GetProcessPreferredUILanguages(
    DWORD dwFlags,
    PULONG pulNumLanguages,
    PZZWSTR pwszLanguagesBuffer,
    PULONG pcchLanguagesBuffer)
{
    DPRINT1("%x %p %p %p\n", dwFlags, pulNumLanguages, pwszLanguagesBuffer, pcchLanguagesBuffer);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}
#endif

/*
* @unimplemented
*/
BOOL
WINAPI
GetSystemPreferredUILanguages(
    DWORD dwFlags,
    PULONG pulNumLanguages,
    PZZWSTR pwszLanguagesBuffer,
    PULONG pcchLanguagesBuffer)
{
    DPRINT1("%x %p %p %p\n", dwFlags, pulNumLanguages, pwszLanguagesBuffer, pcchLanguagesBuffer);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
GetThreadPreferredUILanguages(
    DWORD dwFlags,
    PULONG pulNumLanguages,
    PZZWSTR pwszLanguagesBuffer,
    PULONG pcchLanguagesBuffer)
{
    DPRINT1("%x %p %p %p\n", dwFlags, pulNumLanguages, pwszLanguagesBuffer, pcchLanguagesBuffer);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
LANGID
WINAPI
GetThreadUILanguage(VOID)
{
    UNIMPLEMENTED;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
GetUILanguageInfo(
    DWORD dwFlags,
    PCZZWSTR pwmszLanguage,
    PZZWSTR pwszFallbackLanguages,
    PDWORD pcchFallbackLanguages,
    PDWORD pdwAttributes)
{
    DPRINT1("%x %p %p %p %p\n", dwFlags, pwmszLanguage, pwszFallbackLanguages, pcchFallbackLanguages, pdwAttributes);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}


/*
 * @unimplemented
 */
BOOL
WINAPI
GetUserPreferredUILanguages(
    DWORD dwFlags,
    PULONG pulNumLanguages,
    PZZWSTR pwszLanguagesBuffer,
    PULONG pcchLanguagesBuffer)
{
    DPRINT1("%x %p %p %p\n", dwFlags, pulNumLanguages, pwszLanguagesBuffer, pcchLanguagesBuffer);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
#if 0 // Tis is Windows 7+
BOOL
WINAPI
SetProcessPreferredUILanguages(
    DWORD dwFlags,
    PCZZWSTR pwszLanguagesBuffer,
    PULONG pulNumLanguages)
{
    DPRINT1("%x %p %p\n", dwFlags, pwszLanguagesBuffer, pulNumLanguages);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}
#endif

/*
 * @unimplemented
 */
BOOL
WINAPI
SetThreadPreferredUILanguages(
    DWORD dwFlags,
    PCZZWSTR pwszLanguagesBuffer,
    PULONG pulNumLanguages
    )
{
    DPRINT1("%x %p %p\n", dwFlags, pwszLanguagesBuffer, pulNumLanguages);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}
