/*
 * PROJECT:     ReactOS tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ProcessHandleTable enumeration, truncation, and access checks
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define HANDLE_TABLE_INFORMATION 58
#define UNCHANGED 0xdeadbeefUL
#define STATUS_LENGTH ((LONG)0xc0000004)
#define STATUS_ACCESS ((LONG)0xc0000022)
#define STATUS_BAD_HANDLE ((LONG)0xc0000008)
#define STATUS_BAD_TYPE ((LONG)0xc0000024)
#define STATUS_FAULT ((LONG)0xc0000005)
#define STATUS_ALIGNMENT ((LONG)0x80000002)
#define STATUS_TERMINATING ((LONG)0xc000010a)

typedef LONG (NTAPI *QUERY)(HANDLE, ULONG, PVOID, ULONG, PULONG);
static QUERY Query;
static unsigned Failures;
#define check(c) do { if (!(c)) { printf("FAIL line %u: %s (error %lu)\n", __LINE__, #c, GetLastError()); ++Failures; } } while (0)

static void
CheckFailure(HANDLE Process, void *Buffer, ULONG Size, LONG Expected)
{
    ULONG Length = UNCHANGED;
    LONG Status = Query(Process, HANDLE_TABLE_INFORMATION, Buffer, Size, &Length);
    printf("HANDLE_QUERY size=%lu status=%08lx length=%lu\n", Size, Status, Length);
    check(Status == Expected);
    check(Length == UNCHANGED);
}

int main(void)
{
    ULONG Buffer[2048], Length, Sizes[] = {4, 5, 7, 8, 9};
    DWORD Rights[] = {PROCESS_QUERY_LIMITED_INFORMATION, PROCESS_QUERY_INFORMATION, PROCESS_DUP_HANDLE, SYNCHRONIZE, PROCESS_QUERY_INFORMATION | PROCESS_DUP_HANDLE, PROCESS_ALL_ACCESS};
    HANDLE Events[4], Process;
    STARTUPINFOW Startup = {sizeof(Startup)};
    PROCESS_INFORMATION Child;
    WCHAR Path[MAX_PATH], Command[MAX_PATH + 20];
    unsigned i, j, k;
    LONG Status;

    Query = (QUERY)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess");
    check(Query != NULL);
    if (!Query) return 1;
    for (i = 0; i < 4; ++i)
    {
        Events[i] = CreateEventW(NULL, FALSE, FALSE, NULL);
        check(Events[i] != NULL);
    }
    for (i = 0; i < 4; ++i) CheckFailure(GetCurrentProcess(), Buffer, i, STATUS_LENGTH);
    for (i = 0; i < sizeof(Sizes) / sizeof(Sizes[0]); ++i)
    {
        memset(Buffer, 0xcc, sizeof(Buffer));
        Length = UNCHANGED;
        Status = Query(GetCurrentProcess(), HANDLE_TABLE_INFORMATION, Buffer, Sizes[i], &Length);
        check(Status == 0);
        check(Length == Sizes[i] / 4 * 4);
        if (Length < sizeof(Buffer)) check(Buffer[Length / 4] == 0xcccccccc);
    }
    Length = UNCHANGED;
    Status = Query(GetCurrentProcess(), HANDLE_TABLE_INFORMATION, Buffer, sizeof(Buffer), &Length);
    check(Status == 0);
    check(Length <= sizeof(Buffer) && !(Length & 3));
    if (Status == 0 && Length <= sizeof(Buffer))
    {
        for (i = 0; i < Length / 4; ++i)
        {
            DWORD Flags;
            check(Buffer[i] != 0 && !(Buffer[i] & 3));
            check(GetHandleInformation((HANDLE)(ULONG_PTR)Buffer[i], &Flags));
            for (j = 0; j < i; ++j) check(Buffer[i] != Buffer[j]);
        }
        for (i = 0; i < 4; ++i)
        {
            for (j = 0; j < Length / 4; ++j) if (Buffer[j] == (ULONG)(ULONG_PTR)Events[i]) break;
            check(j < Length / 4);
        }
    }
    for (i = 0; i < sizeof(Rights) / sizeof(Rights[0]); ++i)
    {
        Process = OpenProcess(Rights[i], FALSE, GetCurrentProcessId());
        check(Process != NULL);
        if (!Process) continue;
        if (i < 4) CheckFailure(Process, Buffer, sizeof(Buffer), STATUS_ACCESS);
        else check(Query(Process, HANDLE_TABLE_INFORMATION, Buffer, sizeof(Buffer), &Length) == 0);
        CloseHandle(Process);
    }
    CheckFailure(GetCurrentProcess(), NULL, 0, STATUS_LENGTH);
    CheckFailure(GetCurrentProcess(), NULL, 4, STATUS_FAULT);
    CheckFailure(GetCurrentProcess(), (char *)Buffer + 1, 4, STATUS_ALIGNMENT);
    CheckFailure(NULL, Buffer, 0, STATUS_LENGTH);
    CheckFailure(NULL, Buffer, 4, STATUS_BAD_HANDLE);
    CheckFailure(Events[0], Buffer, 4, STATUS_BAD_TYPE);
    check(Query(GetCurrentProcess(), HANDLE_TABLE_INFORMATION, Buffer, sizeof(Buffer), NULL) == 0);
    check(Query(GetCurrentProcess(), HANDLE_TABLE_INFORMATION, Buffer, 4, (PULONG)1) == STATUS_FAULT);

    /* Closed entries must disappear and enumerating must release every lock. */
    for (i = 0; i < 4; ++i) check(CloseHandle(Events[i]));
    Length = UNCHANGED;
    check(Query(GetCurrentProcess(), HANDLE_TABLE_INFORMATION, Buffer, sizeof(Buffer), &Length) == 0);
    if (Length <= sizeof(Buffer))
        for (j = 0; j < Length / 4; ++j)
            for (k = 0; k < 4; ++k) check(Buffer[j] != (ULONG)(ULONG_PTR)Events[k]);

    check(GetModuleFileNameW(NULL, Path, MAX_PATH) != 0);
    swprintf(Command, MAX_PATH + 20, L"\"%ls\"", Path);
    if (CreateProcessW(NULL, Command, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &Startup, &Child))
    {
        Length = UNCHANGED;
        Status = Query(Child.hProcess, HANDLE_TABLE_INFORMATION, Buffer, sizeof(Buffer), &Length);
        printf("HANDLE_REMOTE status=%08lx length=%lu\n", Status, Length);
        check(Status == 0 && Length <= sizeof(Buffer));
        check(TerminateProcess(Child.hProcess, 0));
        check(WaitForSingleObject(Child.hProcess, 10000) == WAIT_OBJECT_0);
        Length = UNCHANGED;
        Status = Query(Child.hProcess, HANDLE_TABLE_INFORMATION, Buffer, sizeof(Buffer), &Length);
        check(Status == STATUS_TERMINATING);
        check(Length == 0);
        CloseHandle(Child.hThread);
        CloseHandle(Child.hProcess);
    }
    else check(FALSE);
    printf("HANDLETABLE_DONE failures=%u\n", Failures);
    return Failures ? 1 : 0;
}
