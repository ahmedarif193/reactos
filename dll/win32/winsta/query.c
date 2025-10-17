/*
 * PROJECT:         ReactOS winsta.dll
 * FILE:            lib/winsta/query.c
 * PURPOSE:         WinStation
 * PROGRAMMER:      Samuel Serapi?n
 * NOTE:            Get, query and enum functions.
 */

#include "winsta.h"

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#ifndef STATUS_BUFFER_TOO_SMALL
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023L)
#endif

VOID
WINSTAAPI
WinStationQueryLogonCredentialsW(PVOID A)
{
    UNIMPLEMENTED;
}

BOOLEAN
WINSTAAPI WinStationQueryInformationA(HANDLE hServer,
                                      ULONG LogonId,
                                      WINSTATIONINFOCLASS WinStationInformationClass,
                                      PVOID pWinStationInformation,
                                      ULONG WinStationInformationLength,
                                      PULONG pReturnLength)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    UNIMPLEMENTED;
    return FALSE;
}

/*
https://learn.microsoft.com/en-us/previous-versions//aa383827(v=vs.85)
*/
BOOLEAN
WINSTAAPI
WinStationQueryInformationW(HANDLE hServer,
                                    ULONG LogonId,
                                    WINSTATIONINFOCLASS WinStationInformationClass,
                                    PVOID pWinStationInformation,
                                    ULONG WinStationInformationLength,
                                    PULONG pReturnLength)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    UNIMPLEMENTED;
    return FALSE;
}

VOID
WINSTAAPI WinStationQueryAllowConcurrentConnections()
{
    UNIMPLEMENTED;
}

VOID
WINSTAAPI WinStationQueryEnforcementCore(PVOID A,
                                         PVOID B,
                                         PVOID C,
                                         PVOID D,
                                         PVOID E,
                                         PVOID F)
{
    UNIMPLEMENTED;
}

VOID
WINSTAAPI WinStationQueryLicense(PVOID A,
                                 PVOID B,
                                 PVOID C)
{
    UNIMPLEMENTED;
}

VOID
WINSTAAPI WinStationQueryUpdateRequired(PVOID A,
                                        PVOID B)
{
    UNIMPLEMENTED;
}

VOID
WINSTAAPI WinStationEnumerateLicenses(PVOID A,
                                      PVOID B,
                                      PVOID C)
{
    UNIMPLEMENTED;
}

VOID
WINSTAAPI WinStationEnumerateProcesses(PVOID A,
                                       PVOID B)
{
    UNIMPLEMENTED;
}

VOID
WINSTAAPI WinStationEnumerateA(PVOID A,
                               PVOID B,
                               PVOID C)
{
    UNIMPLEMENTED;
}

VOID
WINSTAAPI WinStationEnumerateW(PVOID A,
                               PVOID B,
                               PVOID C)
{
    UNIMPLEMENTED;
}

VOID
WINSTAAPI WinStationEnumerate_IndexedA(PVOID A,
                                       PVOID B,
                                       PVOID C,
                                       PVOID D,
                                       PVOID E)
{
    UNIMPLEMENTED;
}

VOID
WINSTAAPI WinStationEnumerate_IndexedW(PVOID A,
                                       PVOID B,
                                       PVOID C,
                                       PVOID D,
                                       PVOID E)
{
    UNIMPLEMENTED;
}

VOID
WINSTAAPI WinStationRequestSessionsList(PVOID A,
                                        PVOID B,
                                        PVOID C)
{
    UNIMPLEMENTED;
}

VOID
WINSTAAPI WinStationGetAllProcesses(PVOID A,
                                    PVOID B,
                                    PVOID C,
                                    PVOID D)
{
    UNIMPLEMENTED;
}

VOID
WINSTAAPI WinStationGetLanAdapterNameA(PVOID A,
                                       PVOID B,
                                       PVOID C,
                                       PVOID D,
                                       PVOID E,
                                       PVOID F)
{
    UNIMPLEMENTED;
}

VOID
WINSTAAPI WinStationGetLanAdapterNameW(PVOID A,
                                       PVOID B,
                                       PVOID C,
                                       PVOID D,
                                       PVOID E,
                                       PVOID F)
{
    UNIMPLEMENTED;
}

VOID
WINSTAAPI WinStationGetConnectionProperty(PVOID A,
                                          PVOID B,
                                          PVOID C)
{
    UNIMPLEMENTED;
}

VOID
WINSTAAPI WinStationGetInitialApplication(PVOID A,
                                          PVOID B,
                                          PVOID C,
                                          PVOID D,
                                          PVOID E)
{
    UNIMPLEMENTED;
}

BOOLEAN
WINSTAAPI
WinStationGetProcessSid(
    _In_opt_ HANDLE hServer,
    _In_ ULONG ProcessId,
    _In_ LARGE_INTEGER ProcessStartTime,
    _Out_writes_bytes_opt_(*SidLength) PSID ProcessUserSid,
    _Inout_ PULONG SidLength)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    CLIENT_ID ClientId;
    HANDLE ProcessHandle = NULL;
    HANDLE TokenHandle = NULL;
    PTOKEN_USER TokenUserInfo = NULL;
    ULONG TokenInformationLength = 0;
    ULONG RequiredSidLength = 0;
    ULONG ProvidedSidLength = 0;
    KERNEL_USER_TIMES ProcessTimeInfo;
    NTSTATUS Status;
    BOOLEAN Result = FALSE;

    UNREFERENCED_PARAMETER(hServer);

    if (SidLength == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    ProvidedSidLength = *SidLength;
    *SidLength = 0;

    InitializeObjectAttributes(&ObjectAttributes, NULL, 0, NULL, NULL);
    ClientId.UniqueProcess = (HANDLE)(ULONG_PTR)ProcessId;
    ClientId.UniqueThread = NULL;

    Status = NtOpenProcess(&ProcessHandle,
                           PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                           &ObjectAttributes,
                           &ClientId);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        goto Cleanup;
    }

    Status = NtQueryInformationProcess(ProcessHandle,
                                       ProcessTimes,
                                       &ProcessTimeInfo,
                                       sizeof(ProcessTimeInfo),
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        goto Cleanup;
    }

    if ((ProcessStartTime.QuadPart != 0) &&
        (ProcessStartTime.QuadPart != ProcessTimeInfo.CreateTime.QuadPart))
    {
        SetLastError(ERROR_FILE_NOT_FOUND);
        goto Cleanup;
    }

    Status = NtOpenProcessToken(ProcessHandle, TOKEN_QUERY, &TokenHandle);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        goto Cleanup;
    }

    Status = NtQueryInformationToken(TokenHandle,
                                     TokenUser,
                                     NULL,
                                     0,
                                     &TokenInformationLength);
    if (Status != STATUS_BUFFER_TOO_SMALL)
    {
        SetLastError(RtlNtStatusToDosError(Status));
        goto Cleanup;
    }

    TokenUserInfo = HeapAlloc(GetProcessHeap(), 0, TokenInformationLength);
    if (TokenUserInfo == NULL)
    {
        SetLastError(ERROR_OUTOFMEMORY);
        goto Cleanup;
    }

    Status = NtQueryInformationToken(TokenHandle,
                                     TokenUser,
                                     TokenUserInfo,
                                     TokenInformationLength,
                                     &TokenInformationLength);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        goto Cleanup;
    }

    RequiredSidLength = GetLengthSid(TokenUserInfo->User.Sid);
    *SidLength = RequiredSidLength;

    if ((ProcessUserSid == NULL) || (ProvidedSidLength < RequiredSidLength))
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        goto Cleanup;
    }

    if (!CopySid(ProvidedSidLength, ProcessUserSid, TokenUserInfo->User.Sid))
    {
        /* CopySid already sets the thread's last error */
        goto Cleanup;
    }

    Result = TRUE;
    SetLastError(ERROR_SUCCESS);

Cleanup:
    if (TokenUserInfo != NULL)
        HeapFree(GetProcessHeap(), 0, TokenUserInfo);

    if (TokenHandle != NULL)
        NtClose(TokenHandle);

    if (ProcessHandle != NULL)
        NtClose(ProcessHandle);

    return Result;
}

VOID
WINSTAAPI
WinStationGetUserCertificates(PVOID A)
{
    UNIMPLEMENTED;
}

VOID
WINSTAAPI
WinStationGetUserCredentials(PVOID A)
{
    UNIMPLEMENTED;
}

VOID
WINSTAAPI WinStationGetUserProfile(PVOID A,
                                   PVOID B,
                                   PVOID C,
                                   PVOID D)
{
    UNIMPLEMENTED;
}


VOID
WINSTAAPI _WinStationGetApplicationInfo(PVOID A,
                                        PVOID B,
                                        PVOID C,
                                        PVOID D)
{
    UNIMPLEMENTED;
}
