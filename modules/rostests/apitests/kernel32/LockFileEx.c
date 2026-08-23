/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests byte range locking as used by the SQLite Windows VFS
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"

#define PENDING_BYTE 0x40000000
#define RESERVED_BYTE (PENDING_BYTE + 1)
#define SHARED_FIRST (PENDING_BYTE + 2)
#define SHARED_SIZE 510

#define WORKERS 4
#define ITERATIONS 300
#define EXCLUSIVE_TRIES 50

typedef struct _WORKER
{
    WCHAR Path[MAX_PATH];
    ULONG Index;
    ULONG Failures;
    ULONG SharedCount;
    ULONG ExclusiveCount;
} WORKER, *PWORKER;

static BOOL
LockRange(
    _In_ HANDLE File,
    _In_ DWORD Flags,
    _In_ DWORD Offset,
    _In_ DWORD Length)
{
    OVERLAPPED Overlapped;

    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Overlapped.Offset = Offset;
    return LockFileEx(File, Flags, 0, Length, 0, &Overlapped);
}

static BOOL
UnlockRange(
    _In_ HANDLE File,
    _In_ DWORD Offset,
    _In_ DWORD Length)
{
    OVERLAPPED Overlapped;

    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Overlapped.Offset = Offset;
    return UnlockFileEx(File, 0, Length, 0, &Overlapped);
}

static HANDLE
OpenShared(
    _In_ PCWSTR Path)
{
    return CreateFileW(Path,
                       GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
}

static void
TestSemantics(
    _In_ PCWSTR Path)
{
    HANDLE First, Second;
    BOOL Success;
    DWORD Error;

    First = OpenShared(Path);
    Second = OpenShared(Path);
    ok(First != INVALID_HANDLE_VALUE && Second != INVALID_HANDLE_VALUE, "open failed with %lu\n", GetLastError());
    if (First == INVALID_HANDLE_VALUE || Second == INVALID_HANDLE_VALUE)
        goto Cleanup;

    Success = LockRange(First, LOCKFILE_FAIL_IMMEDIATELY, SHARED_FIRST, SHARED_SIZE);
    ok(Success, "first shared lock failed with %lu\n", GetLastError());

    Success = LockRange(Second, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, SHARED_FIRST, SHARED_SIZE);
    Error = Success ? 0 : GetLastError();
    ok(!Success && Error == ERROR_LOCK_VIOLATION, "exclusive over shared returned %d error %lu\n", Success, Error);
    if (Success)
        UnlockRange(Second, SHARED_FIRST, SHARED_SIZE);

    Success = LockRange(Second, LOCKFILE_FAIL_IMMEDIATELY, SHARED_FIRST, SHARED_SIZE);
    ok(Success, "second shared lock failed with %lu\n", GetLastError());

    Success = UnlockRange(First, SHARED_FIRST, 100);
    Error = Success ? 0 : GetLastError();
    ok(!Success && Error == ERROR_NOT_LOCKED, "partial unlock returned %d error %lu\n", Success, Error);

    Success = UnlockRange(First, SHARED_FIRST, SHARED_SIZE);
    ok(Success, "first unlock failed with %lu\n", GetLastError());

    Success = UnlockRange(First, SHARED_FIRST, SHARED_SIZE);
    Error = Success ? 0 : GetLastError();
    ok(!Success && Error == ERROR_NOT_LOCKED, "double unlock returned %d error %lu\n", Success, Error);

    Success = LockRange(First, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, SHARED_FIRST, SHARED_SIZE);
    Error = Success ? 0 : GetLastError();
    ok(!Success && Error == ERROR_LOCK_VIOLATION, "exclusive over remaining shared returned %d error %lu\n", Success, Error);
    if (Success)
        UnlockRange(First, SHARED_FIRST, SHARED_SIZE);

    Success = UnlockRange(Second, SHARED_FIRST, SHARED_SIZE);
    ok(Success, "second unlock failed with %lu\n", GetLastError());

    Success = LockRange(First, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, SHARED_FIRST, SHARED_SIZE);
    ok(Success, "exclusive after release failed with %lu\n", GetLastError());

    Success = LockRange(Second, LOCKFILE_FAIL_IMMEDIATELY, SHARED_FIRST, 1);
    Error = Success ? 0 : GetLastError();
    ok(!Success && Error == ERROR_LOCK_VIOLATION, "shared over exclusive returned %d error %lu\n", Success, Error);
    if (Success)
        UnlockRange(Second, SHARED_FIRST, 1);

    Success = UnlockRange(First, SHARED_FIRST, SHARED_SIZE);
    ok(Success, "exclusive unlock failed with %lu\n", GetLastError());

    Success = LockRange(First, LOCKFILE_FAIL_IMMEDIATELY, 0, 100);
    ok(Success, "overlap lock A failed with %lu\n", GetLastError());
    Success = LockRange(Second, LOCKFILE_FAIL_IMMEDIATELY, 50, 100);
    ok(Success, "overlap lock B failed with %lu\n", GetLastError());
    Success = UnlockRange(First, 0, 100);
    ok(Success, "overlap unlock A failed with %lu\n", GetLastError());
    Success = LockRange(Second, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 50);
    ok(Success, "exclusive on released overlap failed with %lu\n", GetLastError());
    Success = LockRange(First, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 100, 50);
    Error = Success ? 0 : GetLastError();
    ok(!Success && Error == ERROR_LOCK_VIOLATION, "exclusive inside overlap B returned %d error %lu\n", Success, Error);
    if (Success)
        UnlockRange(First, 100, 50);
    Success = UnlockRange(Second, 0, 50);
    ok(Success, "overlap exclusive unlock failed with %lu\n", GetLastError());
    Success = UnlockRange(Second, 50, 100);
    ok(Success, "overlap unlock B failed with %lu\n", GetLastError());

Cleanup:
    if (First != INVALID_HANDLE_VALUE)
        CloseHandle(First);
    if (Second != INVALID_HANDLE_VALUE)
        CloseHandle(Second);
}

static BOOL
ExpectLockViolation(
    _In_ PWORKER Worker,
    _In_ BOOL Success,
    _In_ PCSTR What)
{
    DWORD Error;

    if (Success)
        return TRUE;

    Error = GetLastError();
    if (Error != ERROR_LOCK_VIOLATION)
    {
        ok(FALSE, "worker %lu: %s failed with %lu\n", Worker->Index, What, Error);
        ++Worker->Failures;
    }

    return FALSE;
}

static DWORD
WINAPI
SqliteWorker(
    _In_ PVOID Parameter)
{
    PWORKER Worker = Parameter;
    HANDLE File;
    ULONG Iteration;

    File = OpenShared(Worker->Path);
    if (File == INVALID_HANDLE_VALUE)
    {
        ok(FALSE, "worker %lu: open failed with %lu\n", Worker->Index, GetLastError());
        ++Worker->Failures;
        return 0;
    }

    for (Iteration = 0; Iteration < ITERATIONS; ++Iteration)
    {
        BOOL HaveShared, HaveExclusive = FALSE;
        ULONG Tries;

        for (Tries = 0; Tries < EXCLUSIVE_TRIES; ++Tries)
        {
            if (ExpectLockViolation(Worker, LockRange(File, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, PENDING_BYTE, 1), "pending lock"))
                break;
            Sleep(0);
        }
        if (Tries == EXCLUSIVE_TRIES)
            continue;

        HaveShared = ExpectLockViolation(Worker, LockRange(File, LOCKFILE_FAIL_IMMEDIATELY, SHARED_FIRST, SHARED_SIZE), "shared lock");

        if (!UnlockRange(File, PENDING_BYTE, 1))
        {
            ok(FALSE, "worker %lu: pending unlock failed with %lu\n", Worker->Index, GetLastError());
            ++Worker->Failures;
        }

        if (!HaveShared)
            continue;

        ++Worker->SharedCount;

        if ((Iteration + Worker->Index) % 3 == 0 &&
            ExpectLockViolation(Worker, LockRange(File, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, RESERVED_BYTE, 1), "reserved lock"))
        {
            if (ExpectLockViolation(Worker, LockRange(File, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, PENDING_BYTE, 1), "pending lock for exclusive"))
            {
                if (!UnlockRange(File, SHARED_FIRST, SHARED_SIZE))
                {
                    ok(FALSE, "worker %lu: shared unlock before exclusive failed with %lu\n", Worker->Index, GetLastError());
                    ++Worker->Failures;
                }
                HaveShared = FALSE;

                for (Tries = 0; Tries < EXCLUSIVE_TRIES; ++Tries)
                {
                    if (ExpectLockViolation(Worker, LockRange(File, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, SHARED_FIRST, SHARED_SIZE), "exclusive lock"))
                    {
                        HaveExclusive = TRUE;
                        break;
                    }
                    Sleep(0);
                }

                if (HaveExclusive)
                {
                    DWORD Written;
                    BYTE Byte = (BYTE)Worker->Index;

                    ++Worker->ExclusiveCount;
                    SetFilePointer(File, Worker->Index, NULL, FILE_BEGIN);
                    WriteFile(File, &Byte, 1, &Written, NULL);

                    if (!UnlockRange(File, SHARED_FIRST, SHARED_SIZE))
                    {
                        ok(FALSE, "worker %lu: exclusive unlock failed with %lu\n", Worker->Index, GetLastError());
                        ++Worker->Failures;
                    }
                    HaveExclusive = FALSE;

                    HaveShared = ExpectLockViolation(Worker, LockRange(File, LOCKFILE_FAIL_IMMEDIATELY, SHARED_FIRST, SHARED_SIZE), "shared relock");
                }
                else
                {
                    HaveShared = ExpectLockViolation(Worker, LockRange(File, LOCKFILE_FAIL_IMMEDIATELY, SHARED_FIRST, SHARED_SIZE), "shared relock after failed exclusive");
                }

                if (!UnlockRange(File, PENDING_BYTE, 1))
                {
                    ok(FALSE, "worker %lu: pending unlock after exclusive failed with %lu\n", Worker->Index, GetLastError());
                    ++Worker->Failures;
                }
            }

            if (!UnlockRange(File, RESERVED_BYTE, 1))
            {
                ok(FALSE, "worker %lu: reserved unlock failed with %lu\n", Worker->Index, GetLastError());
                ++Worker->Failures;
            }
        }

        if (HaveShared && !UnlockRange(File, SHARED_FIRST, SHARED_SIZE))
        {
            ok(FALSE, "worker %lu: shared unlock failed with %lu\n", Worker->Index, GetLastError());
            ++Worker->Failures;
        }
    }

    CloseHandle(File);
    return 0;
}

static void
TestSqliteStress(
    _In_ PCWSTR Path)
{
    WORKER Workers[WORKERS];
    HANDLE Threads[WORKERS];
    ULONG Index;
    ULONG SharedTotal = 0, ExclusiveTotal = 0, FailureTotal = 0;

    for (Index = 0; Index < WORKERS; ++Index)
    {
        ZeroMemory(&Workers[Index], sizeof(Workers[Index]));
        lstrcpyW(Workers[Index].Path, Path);
        Workers[Index].Index = Index;
        Threads[Index] = CreateThread(NULL, 0, SqliteWorker, &Workers[Index], 0, NULL);
        ok(Threads[Index] != NULL, "CreateThread %lu failed with %lu\n", Index, GetLastError());
    }

    for (Index = 0; Index < WORKERS; ++Index)
    {
        if (!Threads[Index])
            continue;
        WaitForSingleObject(Threads[Index], INFINITE);
        CloseHandle(Threads[Index]);
        SharedTotal += Workers[Index].SharedCount;
        ExclusiveTotal += Workers[Index].ExclusiveCount;
        FailureTotal += Workers[Index].Failures;
    }

    ok(FailureTotal == 0, "%lu unexpected lock failures\n", FailureTotal);
    ok(SharedTotal > WORKERS * ITERATIONS / 2, "only %lu shared locks were taken\n", SharedTotal);
    ok(ExclusiveTotal > 0, "no exclusive lock was ever taken\n");
}

START_TEST(LockFileEx)
{
    WCHAR TempPath[MAX_PATH], Path[MAX_PATH];
    HANDLE File;
    DWORD Written;
    BYTE Zero[1024];
    UINT Length;

    Length = GetTempPathW(ARRAYSIZE(TempPath), TempPath);
    ok(Length && Length < ARRAYSIZE(TempPath), "GetTempPathW failed: %lu\n", GetLastError());
    if (!Length || Length >= ARRAYSIZE(TempPath)) return;

    if (!GetTempFileNameW(TempPath, L"lck", 0, Path))
    {
        ok(FALSE, "GetTempFileNameW failed: %lu\n", GetLastError());
        return;
    }

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE) return;
    ZeroMemory(Zero, sizeof(Zero));
    WriteFile(File, Zero, sizeof(Zero), &Written, NULL);
    CloseHandle(File);

    TestSemantics(Path);
    TestSqliteStress(Path);

    DeleteFileW(Path);
}
