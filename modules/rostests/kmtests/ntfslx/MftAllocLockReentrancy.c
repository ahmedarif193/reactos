/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Regression test (TC.1) for the FAST_MUTEX/sync-IRP deadlock
 *              fixed in drivers/filesystems/ntfslx/diskwrite.c.
 *
 * Background: NtfslxCreateNewFile holds DevExt->MftAllocLock (a FAST_MUTEX)
 * over the allocate-write-link sequence so parallel creates cannot race on
 * $MFT::$BITMAP. While the lock is held, NtfslxWriteDisk MUST NOT call
 * IoBuildSynchronousFsdRequest because that path completes via a special
 * kernel APC, and special APCs are disabled while a FAST_MUTEX is held
 * (KeEnterCriticalRegion). The first MFT-record write past LCN 4 would
 * deadlock the boot.
 *
 * This test exercises the same lock+write sequence by issuing the regression
 * IOCTL on the mounted NTFS volume:
 *   1. The driver acquires DevExt->MftAllocLock on a worker thread.
 *   2. The worker calls NtfslxWriteDisk to rewrite sector 0 with the same
 *      bytes it just read (idempotent, no on-disk content change).
 *   3. The worker releases MftAllocLock and signals an event.
 *   4. The driver caller waits on the event for 10 s. Timeout means
 *      deadlock and the test fails. Completion means the fix holds.
 *
 * If anyone reverts diskwrite.c to IoBuildSynchronousFsdRequest, this test
 * times out and fails before any other harm is done.
 */

#include <kmt_test.h>
#include <ntifs.h>

/* Keep in sync with drivers/filesystems/ntfslx/ntfslx.h */
#define IOCTL_NTFSLX_TC1_MFTLOCK_REENTRANCY \
    CTL_CODE(FILE_DEVICE_DISK_FILE_SYSTEM, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _NTFSLX_TC1_REENTRANCY_RESULT
{
    ULONG    Version;
    ULONG    TestCompleted;
    NTSTATUS WriteStatus;
    ULONG    ElapsedMs;
} NTFSLX_TC1_REENTRANCY_RESULT, *PNTFSLX_TC1_REENTRANCY_RESULT;

#define TC1LOG(fmt, ...) DbgPrint("NTFSLX-TC1: " fmt, ##__VA_ARGS__)

static NTSTATUS
DetectNtfsDriveLetter(_Out_ PWCHAR OutLetter)
{
    struct { FILE_FS_ATTRIBUTE_INFORMATION I; WCHAR B[16]; } Attr;
    IO_STATUS_BLOCK IoSb;
    NTSTATUS St;
    HANDLE Probe;
    UNICODE_STRING RootName;
    OBJECT_ATTRIBUTES RootOa;
    WCHAR Root[] = L"\\??\\C:\\";
    WCHAR Letter;

    *OutLetter = 0;
    for (Letter = L'C'; Letter <= L'Z'; Letter++)
    {
        Root[4] = Letter;
        RtlInitUnicodeString(&RootName, Root);
        InitializeObjectAttributes(&RootOa, &RootName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   NULL, NULL);
        St = ZwCreateFile(&Probe, FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                          &RootOa, &IoSb, NULL, FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          FILE_OPEN,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_DIRECTORY_FILE,
                          NULL, 0);
        if (!NT_SUCCESS(St))
            continue;
        RtlZeroMemory(&Attr, sizeof(Attr));
        St = ZwQueryVolumeInformationFile(Probe, &IoSb, &Attr, sizeof(Attr),
                                          FileFsAttributeInformation);
        ZwClose(Probe);
        if (!NT_SUCCESS(St))
            continue;
        if (Attr.I.FileSystemNameLength == 4 * sizeof(WCHAR) &&
            Attr.I.FileSystemName[0] == L'N' && Attr.I.FileSystemName[1] == L'T' &&
            Attr.I.FileSystemName[2] == L'F' && Attr.I.FileSystemName[3] == L'S')
        {
            *OutLetter = Letter;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

START_TEST(MftAllocLockReentrancy)
{
    WCHAR Letter = 0;
    WCHAR VolumePath[16];
    UNICODE_STRING VolName;
    OBJECT_ATTRIBUTES Oa;
    HANDLE Volume = NULL;
    IO_STATUS_BLOCK IoSb;
    NTFSLX_TC1_REENTRANCY_RESULT Result;
    NTSTATUS St;

    TC1LOG("=== TC.1 MftAllocLockReentrancy: BEGIN ===\n");

    St = DetectNtfsDriveLetter(&Letter);
    if (!NT_SUCCESS(St) || Letter == 0)
    {
        TC1LOG("no NTFS volume detected, test skipped\n");
        ok(TRUE, "no NTFS volume to test against; skipped\n");
        return;
    }
    TC1LOG("NTFS volume detected at %C:\n", Letter);

    /*
     * Open the volume device directly (no trailing backslash, no path) so
     * the IOCTL lands on NtfslxDeviceControl with FileObject->FileName
     * empty - that's where IOCTL_NTFSLX_TC1_MFTLOCK_REENTRANCY is handled.
     */
    RtlStringCbPrintfW(VolumePath, sizeof(VolumePath), L"\\??\\%c:", Letter);
    RtlInitUnicodeString(&VolName, VolumePath);
    InitializeObjectAttributes(&Oa, &VolName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL, NULL);
    St = ZwCreateFile(&Volume,
                      FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE,
                      &Oa, &IoSb, NULL, 0,
                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                      FILE_OPEN,
                      FILE_SYNCHRONOUS_IO_NONALERT,
                      NULL, 0);
    ok(NT_SUCCESS(St), "open %wZ for IOCTL: 0x%08lx\n", &VolName, St);
    if (!NT_SUCCESS(St))
    {
        TC1LOG("=== TC.1 MftAllocLockReentrancy: END (open failed) ===\n");
        return;
    }

    RtlZeroMemory(&Result, sizeof(Result));
    St = ZwDeviceIoControlFile(Volume, NULL, NULL, NULL, &IoSb,
                               IOCTL_NTFSLX_TC1_MFTLOCK_REENTRANCY,
                               NULL, 0,
                               &Result, sizeof(Result));
    if (skip(NT_SUCCESS(St),
             "Ntfslx TC1 IOCTL unavailable on this NTFS volume: 0x%08lx\n", St))
    {
        ZwClose(Volume);
        TC1LOG("=== TC.1 MftAllocLockReentrancy: END (IOCTL unavailable) ===\n");
        return;
    }

    TC1LOG("Version=%lu Completed=%lu WriteStatus=0x%08lx ElapsedMs=%lu\n",
           Result.Version, Result.TestCompleted,
           Result.WriteStatus, Result.ElapsedMs);

    /* The load-bearing assertion: completion means no deadlock. */
    ok(Result.TestCompleted != 0,
       "TC.1: lock+write sequence completed without deadlock (Completed=%lu)\n",
       Result.TestCompleted);

    /* The write itself must succeed - if it returned an error, the
     * lock-and-IRP path is alive but something else is broken. */
    ok(Result.TestCompleted == 0 || NT_SUCCESS(Result.WriteStatus),
       "TC.1: NtfslxWriteDisk under MftAllocLock returned 0x%08lx\n",
       Result.WriteStatus);

    /* A single-sector write should finish in well under the 10 s
     * watchdog. Anything close to the watchdog window is suspicious -
     * possibly a slow path that needs investigation - so we report
     * the elapsed time but do not strictly fail on it. */
    if (Result.TestCompleted)
    {
        ok(Result.ElapsedMs < 5000,
           "TC.1: single-sector write took %lu ms (suspiciously slow)\n",
           Result.ElapsedMs);
    }

    ZwClose(Volume);
    TC1LOG("=== TC.1 MftAllocLockReentrancy: END ===\n");
}
