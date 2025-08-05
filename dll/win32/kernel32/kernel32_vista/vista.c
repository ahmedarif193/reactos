/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS system libraries
 * PURPOSE:         Vista functions
 * PROGRAMMER:      Thomas Weidenmueller <w3seek@reactos.com>
 */

/* INCLUDES *******************************************************************/

#include <k32_vista.h>

#if _WIN32_WINNT < _WIN32_WINNT_VISTA
#error "This file must be compiled with _WIN32_WINNT >= _WIN32_WINNT_VISTA"
#endif

// This is defined only in ntifs.h
#define REPARSE_DATA_BUFFER_HEADER_SIZE   FIELD_OFFSET(REPARSE_DATA_BUFFER, GenericReparseBuffer)

#define NDEBUG
#include <debug.h>

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
    DPRINT1("%x %p %p %p\n", dwFlags, pcwszFilePath, pFileMUIInfo, pcbFileMUIInfo);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
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
    DPRINT1("%x %p %p %p %p %p\n", dwFlags, pcwszFilePath, pwszLanguage, pwszFileMUIPath, pcchFileMUIPath, pululEnumerator);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
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


/* Windows 10 API Stubs */

/*
 * @unimplemented
 */
BOOL
WINAPI
GetFileAttributesExFromAppW(LPCWSTR lpFileName,
                           GET_FILEEX_INFO_LEVELS fInfoLevelId,
                           LPVOID lpFileInformation)
{
    DPRINT1("GetFileAttributesExFromAppW stub: %S %d %p\n", lpFileName, fInfoLevelId, lpFileInformation);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
GetNumaNodeNumberFromHandle(HANDLE hFile,
                           PUSHORT NodeNumber)
{
    DPRINT1("GetNumaNodeNumberFromHandle stub: %p %p\n", hFile, NodeNumber);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
LONG
WINAPI
GetPackageApplicationIds(PCWSTR packageFullName,
                        UINT32 *packageApplicationIdLength,
                        PWSTR *packageApplicationIds,
                        UINT32 *applicationIdCount)
{
    DPRINT1("GetPackageApplicationIds stub: %S %p %p %p\n", packageFullName, packageApplicationIdLength, packageApplicationIds, applicationIdCount);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
GetPackageInfo(HANDLE hProcess,
               DWORD flags,
               UINT32 *bufferLength,
               BYTE *buffer,
               UINT32 *count)
{
    DPRINT1("GetPackageInfo stub: %p %lx %p %p %p\n", hProcess, flags, bufferLength, buffer, count);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
GetPackagePath(HANDLE hProcess,
               DWORD reserved,
               UINT32 *pathLength,
               PWSTR path)
{
    DPRINT1("GetPackagePath stub: %p %lx %p %p\n", hProcess, reserved, pathLength, path);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
GetProcessGroupAffinity(HANDLE hProcess,
                       PUSHORT GroupCount,
                       PUSHORT GroupArray)
{
    DPRINT1("GetProcessGroupAffinity stub: %p %p %p\n", hProcess, GroupCount, GroupArray);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
LONG
WINAPI
GetStagedPackageOrigin(PCWSTR packageFullName,
                      DWORD *origin)
{
    DPRINT1("GetStagedPackageOrigin stub: %S %p\n", packageFullName, origin);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
DWORD
WINAPI
GetTempPathFromAppW(DWORD nBufferLength,
                   LPWSTR lpBuffer)
{
    DPRINT1("GetTempPathFromAppW stub: %lx %p\n", nBufferLength, lpBuffer);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
GetThreadGroupAffinity(HANDLE hThread,
                      PGROUP_AFFINITY GroupAffinity)
{
    DPRINT1("GetThreadGroupAffinity stub: %p %p\n", hThread, GroupAffinity);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
GetThreadIdealProcessorEx(HANDLE hThread,
                         PPROCESSOR_NUMBER lpIdealProcessor)
{
    DPRINT1("GetThreadIdealProcessorEx stub: %p %p\n", hThread, lpIdealProcessor);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
MoveFileExFromAppW(LPCWSTR lpExistingFileName,
                  LPCWSTR lpNewFileName,
                  DWORD dwFlags)
{
    DPRINT1("MoveFileExFromAppW stub: %S %S %lx\n", lpExistingFileName, lpNewFileName, dwFlags);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
MoveFileFromAppW(LPCWSTR lpExistingFileName,
                LPCWSTR lpNewFileName)
{
    DPRINT1("MoveFileFromAppW stub: %S %S\n", lpExistingFileName, lpNewFileName);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
LONG
WINAPI
PackageFamilyNameFromFullName(PCWSTR packageFullName,
                             UINT32 *packageFamilyNameLength,
                             PWSTR packageFamilyName)
{
    DPRINT1("PackageFamilyNameFromFullName stub: %S %p %p\n", packageFullName, packageFamilyNameLength, packageFamilyName);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
PackageFamilyNameFromId(HANDLE hProcess,
                       UINT32 *packageFamilyNameLength,
                       PWSTR packageFamilyName)
{
    DPRINT1("PackageFamilyNameFromId stub: %p %p %p\n", hProcess, packageFamilyNameLength, packageFamilyName);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
PackageFullNameFromId(HANDLE hProcess,
                     UINT32 *packageFullNameLength,
                     PWSTR packageFullName)
{
    DPRINT1("PackageFullNameFromId stub: %p %p %p\n", hProcess, packageFullNameLength, packageFullName);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
PackageIdFromFullName(PCWSTR packageFullName,
                     DWORD flags,
                     UINT32 *bufferLength,
                     BYTE *buffer)
{
    DPRINT1("PackageIdFromFullName stub: %S %lx %p %p\n", packageFullName, flags, bufferLength, buffer);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
PackageNameAndPublisherIdFromFamilyName(PCWSTR packageFamilyName,
                                       UINT32 *packageNameLength,
                                       PWSTR packageName,
                                       UINT32 *packagePublisherIdLength,
                                       PWSTR packagePublisherId)
{
    DPRINT1("PackageNameAndPublisherIdFromFamilyName stub: %S %p %p %p %p\n", packageFamilyName, packageNameLength, packageName, packagePublisherIdLength, packagePublisherId);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
ParseApplicationUserModelId(PCWSTR applicationUserModelId,
                           UINT32 *packageFamilyNameLength,
                           PWSTR packageFamilyName,
                           UINT32 *packageRelativeApplicationIdLength,
                           PWSTR packageRelativeApplicationId)
{
    DPRINT1("ParseApplicationUserModelId stub: %S %p %p %p %p\n", applicationUserModelId, packageFamilyNameLength, packageFamilyName, packageRelativeApplicationIdLength, packageRelativeApplicationId);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
RegCopyTreeW(HKEY hKeySrc,
            LPCWSTR lpSubKey,
            HKEY hKeyDest)
{
    DPRINT1("RegCopyTreeW stub: %p %S %p\n", hKeySrc, lpSubKey, hKeyDest);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
RegCreateKeyTransactedA(HKEY hKey,
                       LPCSTR lpSubKey,
                       DWORD Reserved,
                       LPSTR lpClass,
                       DWORD dwOptions,
                       DWORD samDesired,
                       LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                       PHKEY phkResult,
                       LPDWORD lpdwDisposition)
{
    DPRINT1("RegCreateKeyTransactedA stub: %p %s %lx %s %lx %lx %p %p %p\n", hKey, lpSubKey, Reserved, lpClass, dwOptions, samDesired, lpSecurityAttributes, phkResult, lpdwDisposition);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
RegCreateKeyTransactedW(HKEY hKey,
                       LPCWSTR lpSubKey,
                       DWORD Reserved,
                       LPWSTR lpClass,
                       DWORD dwOptions,
                       DWORD samDesired,
                       LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                       PHKEY phkResult,
                       LPDWORD lpdwDisposition)
{
    DPRINT1("RegCreateKeyTransactedW stub: %p %S %lx %S %lx %lx %p %p %p\n", hKey, lpSubKey, Reserved, lpClass, dwOptions, samDesired, lpSecurityAttributes, phkResult, lpdwDisposition);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
RegDeleteKeyTransactedA(HKEY hKey,
                       LPCSTR lpSubKey,
                       DWORD samDesired,
                       DWORD Reserved,
                       HANDLE hTransaction)
{
    DPRINT1("RegDeleteKeyTransactedA stub: %p %s %lx %lx %p\n", hKey, lpSubKey, samDesired, Reserved, hTransaction);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
RegDeleteKeyTransactedW(HKEY hKey,
                       LPCWSTR lpSubKey,
                       DWORD samDesired,
                       DWORD Reserved,
                       HANDLE hTransaction)
{
    DPRINT1("RegDeleteKeyTransactedW stub: %p %S %lx %lx %p\n", hKey, lpSubKey, samDesired, Reserved, hTransaction);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
RegDeleteKeyValueA(HKEY hKey,
                  LPCSTR lpSubKey,
                  LPCSTR lpValueName)
{
    DPRINT1("RegDeleteKeyValueA stub: %p %s %s\n", hKey, lpSubKey, lpValueName);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
RegDeleteKeyValueW(HKEY hKey,
                  LPCWSTR lpSubKey,
                  LPCWSTR lpValueName)
{
    DPRINT1("RegDeleteKeyValueW stub: %p %S %S\n", hKey, lpSubKey, lpValueName);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
RegDeleteTreeA(HKEY hKey,
              LPCSTR lpSubKey)
{
    DPRINT1("RegDeleteTreeA stub: %p %s\n", hKey, lpSubKey);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
RegDeleteTreeW(HKEY hKey,
              LPCWSTR lpSubKey)
{
    DPRINT1("RegDeleteTreeW stub: %p %S\n", hKey, lpSubKey);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
RegDisableReflectionKey(HKEY hBase)
{
    DPRINT1("RegDisableReflectionKey stub: %p\n", hBase);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
RegEnableReflectionKey(HKEY hBase)
{
    DPRINT1("RegEnableReflectionKey stub: %p\n", hBase);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
RegGetValueA(HKEY hkey,
            LPCSTR lpSubKey,
            LPCSTR lpValue,
            DWORD dwFlags,
            LPDWORD pdwType,
            PVOID pvData,
            LPDWORD pcbData)
{
    DPRINT1("RegGetValueA stub: %p %s %s %lx %p %p %p\n", hkey, lpSubKey, lpValue, dwFlags, pdwType, pvData, pcbData);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
RegGetValueW(HKEY hkey,
            LPCWSTR lpSubKey,
            LPCWSTR lpValue,
            DWORD dwFlags,
            LPDWORD pdwType,
            PVOID pvData,
            LPDWORD pcbData)
{
    DPRINT1("RegGetValueW stub: %p %S %S %lx %p %p %p\n", hkey, lpSubKey, lpValue, dwFlags, pdwType, pvData, pcbData);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
RegLoadAppKeyA(LPCSTR lpFile,
              PHKEY phkResult,
              DWORD samDesired,
              DWORD dwOptions,
              DWORD Reserved)
{
    DPRINT1("RegLoadAppKeyA stub: %s %p %lx %lx %lx\n", lpFile, phkResult, samDesired, dwOptions, Reserved);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
RegLoadAppKeyW(LPCWSTR lpFile,
              PHKEY phkResult,
              DWORD samDesired,
              DWORD dwOptions,
              DWORD Reserved)
{
    DPRINT1("RegLoadAppKeyW stub: %S %p %lx %lx %lx\n", lpFile, phkResult, samDesired, dwOptions, Reserved);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
RegOpenKeyTransactedA(HKEY hKey,
                     LPCSTR lpSubKey,
                     DWORD ulOptions,
                     DWORD samDesired,
                     PHKEY phkResult,
                     HANDLE hTransaction)
{
    DPRINT1("RegOpenKeyTransactedA stub: %p %s %lx %lx %p %p\n", hKey, lpSubKey, ulOptions, samDesired, phkResult, hTransaction);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
RegOpenKeyTransactedW(HKEY hKey,
                     LPCWSTR lpSubKey,
                     DWORD ulOptions,
                     DWORD samDesired,
                     PHKEY phkResult,
                     HANDLE hTransaction)
{
    DPRINT1("RegOpenKeyTransactedW stub: %p %S %lx %lx %p %p\n", hKey, lpSubKey, ulOptions, samDesired, phkResult, hTransaction);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
RegQueryReflectionKey(HKEY hBase,
                     BOOL *bIsReflectionDisabled)
{
    DPRINT1("RegQueryReflectionKey stub: %p %p\n", hBase, bIsReflectionDisabled);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
RegSetKeyValueA(HKEY hKey,
               LPCSTR lpSubKey,
               LPCSTR lpValueName,
               DWORD dwType,
               LPCVOID lpData,
               DWORD cbData)
{
    DPRINT1("RegSetKeyValueA stub: %p %s %s %lx %p %lx\n", hKey, lpSubKey, lpValueName, dwType, lpData, cbData);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
RegSetKeyValueW(HKEY hKey,
               LPCWSTR lpSubKey,
               LPCWSTR lpValueName,
               DWORD dwType,
               LPCVOID lpData,
               DWORD cbData)
{
    DPRINT1("RegSetKeyValueW stub: %p %S %S %lx %p %lx\n", hKey, lpSubKey, lpValueName, dwType, lpData, cbData);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
RemoveDirectoryFromAppW(LPCWSTR lpPathName)
{
    DPRINT1("RemoveDirectoryFromAppW stub: %S\n", lpPathName);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
ReplaceFileFromAppW(LPCWSTR lpReplacedFileName,
                   LPCWSTR lpReplacementFileName,
                   LPCWSTR lpBackupFileName,
                   DWORD dwReplaceFlags,
                   LPVOID lpExclude,
                   LPVOID lpReserved)
{
    DPRINT1("ReplaceFileFromAppW stub: %S %S %S %lx %p %p\n", lpReplacedFileName, lpReplacementFileName, lpBackupFileName, dwReplaceFlags, lpExclude, lpReserved);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
SetCalendarInfoEx(LPCWSTR lpLocaleName,
                 CALID Calendar,
                 LPCWSTR lpCalData,
                 CALTYPE CalType,
                 LPCWSTR lpReserved)
{
    DPRINT1("SetCalendarInfoEx stub: %S %lx %S %lx %S\n", lpLocaleName, Calendar, lpCalData, CalType, lpReserved);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
SetFileAttributesFromAppW(LPCWSTR lpFileName,
                         DWORD dwFileAttributes)
{
    DPRINT1("SetFileAttributesFromAppW stub: %S %lx\n", lpFileName, dwFileAttributes);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
SetLocaleInfoEx(LPCWSTR lpLocaleName,
               LCTYPE LCType,
               LPCWSTR lpLCData)
{
    DPRINT1("SetLocaleInfoEx stub: %S %lx %S\n", lpLocaleName, LCType, lpLCData);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
SetProcessGroupAffinity(HANDLE hProcess,
                       CONST GROUP_AFFINITY *GroupAffinity)
{
    DPRINT1("SetProcessGroupAffinity stub: %p %p\n", hProcess, GroupAffinity);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
SetThreadGroupAffinity(HANDLE hThread,
                      CONST GROUP_AFFINITY *GroupAffinity,
                      PGROUP_AFFINITY PreviousGroupAffinity)
{
    DPRINT1("SetThreadGroupAffinity stub: %p %p %p\n", hThread, GroupAffinity, PreviousGroupAffinity);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
SetThreadIdealProcessorEx(HANDLE hThread,
                         PPROCESSOR_NUMBER lpIdealProcessor,
                         PPROCESSOR_NUMBER lpPreviousIdealProcessor)
{
    DPRINT1("SetThreadIdealProcessorEx stub: %p %p %p\n", hThread, lpIdealProcessor, lpPreviousIdealProcessor);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
HRESULT
WINAPI
CopyFile2(PCWSTR pwszExistingFileName,
          PCWSTR pwszNewFileName,
          COPYFILE2_EXTENDED_PARAMETERS *pExtendedParameters)
{
    DPRINT1("CopyFile2 stub: %S %S %p\n", pwszExistingFileName, pwszNewFileName, pExtendedParameters);
    return HRESULT_FROM_WIN32(ERROR_CALL_NOT_IMPLEMENTED);
}

/*
 * @unimplemented
 */
BOOL
WINAPI
CopyFileExFromAppW(LPCWSTR lpExistingFileName,
                  LPCWSTR lpNewFileName,
                  PVOID lpProgressRoutine,
                  LPVOID lpData,
                  LPBOOL pbCancel,
                  DWORD dwCopyFlags)
{
    DPRINT1("CopyFileExFromAppW stub: %S %S %p %p %p %lx\n", lpExistingFileName, lpNewFileName, lpProgressRoutine, lpData, pbCancel, dwCopyFlags);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
CopyFileFromAppW(LPCWSTR lpExistingFileName,
                LPCWSTR lpNewFileName,
                BOOL bFailIfExists)
{
    DPRINT1("CopyFileFromAppW stub: %S %S %d\n", lpExistingFileName, lpNewFileName, bFailIfExists);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
CreateDirectoryFromAppW(LPCWSTR lpPathName,
                       LPCWSTR lpTemplateDirectory,
                       LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    DPRINT1("CreateDirectoryFromAppW stub: %S %S %p\n", lpPathName, lpTemplateDirectory, lpSecurityAttributes);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
HANDLE
WINAPI
CreateFileFromAppW(LPCWSTR lpFileName,
                  DWORD dwDesiredAccess,
                  DWORD dwShareMode,
                  DWORD dwCreationDisposition,
                  LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                  DWORD dwFlagsAndAttributes,
                  HANDLE hTemplateFile)
{
    DPRINT1("CreateFileFromAppW stub: %S %lx %lx %lx %p %lx %p\n", lpFileName, dwDesiredAccess, dwShareMode, dwCreationDisposition, lpSecurityAttributes, dwFlagsAndAttributes, hTemplateFile);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return INVALID_HANDLE_VALUE;
}

/*
 * @unimplemented
 */
HANDLE
WINAPI
CreateRemoteThreadEx(HANDLE hProcess,
                    LPSECURITY_ATTRIBUTES lpThreadAttributes,
                    SIZE_T dwStackSize,
                    PVOID lpStartAddress,
                    LPVOID lpParameter,
                    DWORD dwCreationFlags,
                    PVOID lpAttributeList,
                    LPDWORD lpThreadId)
{
    DPRINT1("CreateRemoteThreadEx stub: %p %p %Ix %p %p %lx %p %p\n", hProcess, lpThreadAttributes, dwStackSize, lpStartAddress, lpParameter, dwCreationFlags, lpAttributeList, lpThreadId);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
DeleteFileFromAppW(LPCWSTR lpFileName)
{
    DPRINT1("DeleteFileFromAppW stub: %S\n", lpFileName);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
HANDLE
WINAPI
FindFirstFileExFromAppW(LPCWSTR lpFileName,
                       DWORD fInfoLevelId,
                       LPVOID lpFindFileData,
                       DWORD fSearchOp,
                       LPVOID lpSearchFilter,
                       DWORD dwAdditionalFlags)
{
    DPRINT1("FindFirstFileExFromAppW stub: %S %lx %p %lx %p %lx\n", lpFileName, fInfoLevelId, lpFindFileData, fSearchOp, lpSearchFilter, dwAdditionalFlags);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return INVALID_HANDLE_VALUE;
}

/*
 * @unimplemented
 */
LONG
WINAPI
FormatApplicationUserModelId(PCWSTR packageFamilyName,
                            PCWSTR packageRelativeApplicationId,
                            PWSTR applicationUserModelId,
                            UINT32 *applicationUserModelIdLength)
{
    DPRINT1("FormatApplicationUserModelId stub: %S %S %p %p\n", packageFamilyName, packageRelativeApplicationId, applicationUserModelId, applicationUserModelIdLength);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
LONG
WINAPI
GetCurrentApplicationUserModelId(UINT32 *applicationUserModelIdLength,
                                PWSTR applicationUserModelId)
{
    DPRINT1("GetCurrentApplicationUserModelId stub: %p %p\n", applicationUserModelIdLength, applicationUserModelId);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

