#include "precomp.h"

typedef BOOLEAN (WINAPI *PFNWinStationGetProcessSid)(HANDLE, ULONG, LARGE_INTEGER, PSID, PULONG);

START_TEST(WinStationGetProcessSid)
{
    PFNWinStationGetProcessSid pWinStationGetProcessSid;
    HMODULE hWinsta;
    KERNEL_USER_TIMES Times;
    NTSTATUS Status;
    ULONG ProcessId;
    DWORD Error;
    ULONG SidLength = 0;
    BYTE StackSid[SECURITY_MAX_SID_SIZE];
    PBYTE ProcessSid = StackSid;
    BOOL Result;

    hWinsta = LoadLibraryW(L"winsta.dll");
    ok(hWinsta != NULL, "LoadLibraryW failed with %lu\n", GetLastError());
    if (!hWinsta)
        return;

    pWinStationGetProcessSid = (PFNWinStationGetProcessSid)GetProcAddress(hWinsta, "WinStationGetProcessSid");
    if (!pWinStationGetProcessSid)
    {
        skip("WinStationGetProcessSid is not exported by winsta.dll\n");
        FreeLibrary(hWinsta);
        return;
    }

    Status = NtQueryInformationProcess(NtCurrentProcess(),
                                       ProcessTimes,
                                       &Times,
                                       sizeof(Times),
                                       NULL);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        FreeLibrary(hWinsta);
        return;
    }

    ProcessId = GetCurrentProcessId();

    SetLastError(0xdeadbeef);
    Result = pWinStationGetProcessSid(NULL,
                                      ProcessId,
                                      Times.CreateTime,
                                      NULL,
                                      &SidLength);
    Error = GetLastError();
    ok(!Result, "WinStationGetProcessSid should fail when Sid buffer is NULL\n");
    ok(Error == ERROR_INSUFFICIENT_BUFFER,
       "Expected ERROR_INSUFFICIENT_BUFFER, got %lu\n",
       Error);
    ok(SidLength > 0, "Expected SidLength > 0, got %lu\n", SidLength);

    if (SidLength > sizeof(StackSid))
    {
        ProcessSid = HeapAlloc(GetProcessHeap(), 0, SidLength);
        ok(ProcessSid != NULL, "Failed to allocate %lu bytes for SID\n", SidLength);
        if (!ProcessSid)
        {
            FreeLibrary(hWinsta);
            return;
        }
    }

    ULONG SmallerLength = SidLength - 1;
    SetLastError(0xdeadbeef);
    Result = pWinStationGetProcessSid(NULL,
                                      ProcessId,
                                      Times.CreateTime,
                                      ProcessSid,
                                      &SmallerLength);
    Error = GetLastError();
    ok(!Result, "Expected failure when buffer is too small\n");
    ok(Error == ERROR_INSUFFICIENT_BUFFER,
       "Expected ERROR_INSUFFICIENT_BUFFER, got %lu\n",
       Error);
    ok_dec(SmallerLength, SidLength);

    ULONG BufferLength = SidLength;
    Result = pWinStationGetProcessSid(NULL,
                                      ProcessId,
                                      Times.CreateTime,
                                      ProcessSid,
                                      &BufferLength);
    Error = GetLastError();
    ok(Result, "WinStationGetProcessSid failed with %lu\n", Error);
    ok_dec(BufferLength, SidLength);

    if (Result)
    {
        HANDLE Token = NULL;

        Result = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token);
        ok(Result, "OpenProcessToken failed with %lu\n", GetLastError());
        if (Result)
        {
            DWORD TokenLength = 0;
            PTOKEN_USER TokenUserInfo = NULL;

            GetTokenInformation(Token,
                                 TokenUser,
                                 NULL,
                                 0,
                                 &TokenLength);
            ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
               "Expected ERROR_INSUFFICIENT_BUFFER, got %lu\n",
               GetLastError());

            TokenUserInfo = HeapAlloc(GetProcessHeap(), 0, TokenLength);
            ok(TokenUserInfo != NULL, "Failed to allocate %lu bytes for TOKEN_USER\n", TokenLength);
            if (TokenUserInfo)
            {
                Result = GetTokenInformation(Token,
                                              TokenUser,
                                              TokenUserInfo,
                                              TokenLength,
                                              &TokenLength);
                ok(Result, "GetTokenInformation failed with %lu\n", GetLastError());
                if (Result)
                {
                    ok(EqualSid(TokenUserInfo->User.Sid, ProcessSid),
                       "User SID returned by WinStationGetProcessSid does not match the token SID\n");
                }

                HeapFree(GetProcessHeap(), 0, TokenUserInfo);
            }

            CloseHandle(Token);
        }
    }

    LARGE_INTEGER WrongStartTime = Times.CreateTime;
    WrongStartTime.QuadPart += 1;
    BufferLength = SidLength;
    SetLastError(0xdeadbeef);
    Result = pWinStationGetProcessSid(NULL,
                                      ProcessId,
                                      WrongStartTime,
                                      ProcessSid,
                                      &BufferLength);
    Error = GetLastError();
    ok(!Result, "Expected failure for stale start time\n");
    ok(Error == ERROR_FILE_NOT_FOUND,
       "Expected ERROR_FILE_NOT_FOUND, got %lu\n",
       Error);

    BufferLength = SidLength;
    SetLastError(0xdeadbeef);
    Result = pWinStationGetProcessSid(NULL,
                                      ProcessId,
                                      Times.CreateTime,
                                      ProcessSid,
                                      NULL);
    Error = GetLastError();
    ok(!Result, "Expected failure when SidLength is NULL\n");
    ok(Error == ERROR_INVALID_PARAMETER,
       "Expected ERROR_INVALID_PARAMETER, got %lu\n",
       Error);

    if (ProcessSid != StackSid)
        HeapFree(GetProcessHeap(), 0, ProcessSid);

    FreeLibrary(hWinsta);
}
