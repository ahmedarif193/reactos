/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     T1.6 in-VM crash workload. Drives sustained metadata
 *              mutation against the mounted ntfslx NTFS volume so a
 *              host-side fuzzer can hard-kill QEMU mid-write and verify
 *              the on-disk state is recoverable on next mount.
 *
 * The workload creates a fixed sequence of small files inside a known
 * directory tree on the NTFS volume. Each file is written then closed,
 * and a per-file completion marker is emitted to the kernel debug log:
 *
 *      NTFSLX-T16-COMMITTED: idx=42 path=\\T16\\f0042.bin
 *
 * The host fuzzer parses these markers from the boot log to know
 * exactly which files must be present and intact after a crash and
 * remount. A file is committed once both its data write and its
 * directory entry update have returned success in-driver — covering
 * the journaled paths from T1.3 (MFT bitmap), T1.4 (cluster bitmap),
 * and T1.5 ($INDEX_ALLOCATION). If the harness kills QEMU between
 * the COMMITTED marker emission and the next file's marker, the
 * harness expects the previously-marked files to be present after
 * remount.
 *
 * Latency / call-time guarantees are NOT this test's responsibility
 * (TC.4 covers that); this test is purely a stable mutation source.
 */

#include <kmt_test.h>
#include <ntifs.h>

#define T16LOG(fmt, ...) DbgPrint("NTFSLX-T16: " fmt, ##__VA_ARGS__)
#define T16COMMIT(fmt, ...) DbgPrint("NTFSLX-T16-COMMITTED: " fmt, ##__VA_ARGS__)

/*
 * File count is bounded so a single 25 s QEMU iteration can complete
 * the entire workload (~4-5 ms per file at our latency baseline = a
 * few hundred ms for 50 files). The host fuzzer crash-kills at a
 * random t in [0, expected-completion-time + slack] so most kills
 * land inside the workload window rather than past it.
 */
#define T16_FILE_COUNT       50
#define T16_FILE_SIZE        256
#define T16_DIRECTORY_NAME   L"T16"

static NTSTATUS
T16DetectNtfsDriveLetter(_Out_ PWCHAR OutLetter)
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

static VOID
T16FillBuffer(
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length,
    _In_ ULONG Index)
{
    ULONG I;
    /* Deterministic per-index pattern so the host can recompute the
     * expected bytes without parsing any guest-side state. */
    UCHAR Seed = (UCHAR)((Index * 0x9DU) ^ 0x37U);
    for (I = 0; I < Length; I++)
        Buffer[I] = (UCHAR)(Seed + (UCHAR)I);
}

static NTSTATUS
T16CreateDirectory(
    _In_ PCWSTR DirPath)
{
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Oa;
    IO_STATUS_BLOCK IoSb;
    HANDLE Dir;
    NTSTATUS St;

    RtlInitUnicodeString(&Name, DirPath);
    InitializeObjectAttributes(&Oa, &Name,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL, NULL);
    St = ZwCreateFile(&Dir,
                      FILE_LIST_DIRECTORY | FILE_ADD_FILE | SYNCHRONIZE,
                      &Oa, &IoSb, NULL, FILE_ATTRIBUTE_DIRECTORY,
                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                      FILE_OPEN_IF,
                      FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                      NULL, 0);
    if (NT_SUCCESS(St))
        ZwClose(Dir);
    return St;
}

START_TEST(Phase1JournalWorkload)
{
    WCHAR Letter = 0;
    WCHAR DirPath[64];
    WCHAR FilePath[80];
    UCHAR Buffer[T16_FILE_SIZE];
    NTSTATUS St;
    ULONG I;

    T16LOG("=== T1.6 Phase1JournalWorkload: BEGIN ===\n");

    St = T16DetectNtfsDriveLetter(&Letter);
    if (!NT_SUCCESS(St) || Letter == 0)
    {
        T16LOG("no NTFS volume detected; workload skipped\n");
        ok(TRUE, "no NTFS volume to test against; skipped\n");
        return;
    }
    T16LOG("NTFS volume detected at %C: file_count=%lu file_size=%lu\n",
           Letter, (ULONG)T16_FILE_COUNT, (ULONG)T16_FILE_SIZE);

    RtlStringCbPrintfW(DirPath, sizeof(DirPath),
                       L"\\??\\%c:\\%s", Letter, T16_DIRECTORY_NAME);

    St = T16CreateDirectory(DirPath);
    ok(NT_SUCCESS(St), "create workload dir '%ls': 0x%08lx\n",
       DirPath, St);
    if (!NT_SUCCESS(St))
    {
        T16LOG("=== T1.6 Phase1JournalWorkload: END (mkdir failed) ===\n");
        return;
    }

    for (I = 0; I < T16_FILE_COUNT; I++)
    {
        UNICODE_STRING Name;
        OBJECT_ATTRIBUTES Oa;
        IO_STATUS_BLOCK IoSb;
        HANDLE File;

        RtlStringCbPrintfW(FilePath, sizeof(FilePath),
                           L"\\??\\%c:\\%s\\f%04lu.bin",
                           Letter, T16_DIRECTORY_NAME, I);

        T16FillBuffer(Buffer, T16_FILE_SIZE, I);

        RtlInitUnicodeString(&Name, FilePath);
        InitializeObjectAttributes(&Oa, &Name,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   NULL, NULL);
        St = ZwCreateFile(&File,
                          FILE_GENERIC_WRITE | SYNCHRONIZE,
                          &Oa, &IoSb, NULL, FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ,
                          FILE_OVERWRITE_IF,
                          FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT |
                          FILE_WRITE_THROUGH,
                          NULL, 0);
        if (!NT_SUCCESS(St))
        {
            T16LOG("idx=%lu create failed 0x%08lx\n", I, St);
            ok(NT_SUCCESS(St), "T1.6 create idx=%lu: 0x%08lx\n", I, St);
            break;
        }

        St = ZwWriteFile(File, NULL, NULL, NULL, &IoSb,
                         Buffer, T16_FILE_SIZE,
                         NULL, NULL);
        if (!NT_SUCCESS(St))
        {
            T16LOG("idx=%lu write failed 0x%08lx\n", I, St);
            ok(NT_SUCCESS(St), "T1.6 write idx=%lu: 0x%08lx\n", I, St);
            ZwClose(File);
            break;
        }

        St = ZwClose(File);
        if (!NT_SUCCESS(St))
        {
            T16LOG("idx=%lu close failed 0x%08lx\n", I, St);
            ok(NT_SUCCESS(St), "T1.6 close idx=%lu: 0x%08lx\n", I, St);
            break;
        }

        /*
         * Emit the durability marker. Once this line is in the kernel
         * log, both the data and the directory entry have returned
         * success from the driver — meaning all journaled bitmap +
         * index updates plus the data IO have been issued
         * synchronously. A crash *after* this print is the recovery
         * test the harness asserts: the file must remain on remount.
         */
        T16COMMIT("idx=%lu path=\\%S\\f%04lu.bin\n",
                  I, T16_DIRECTORY_NAME, I);
    }

    T16LOG("=== T1.6 Phase1JournalWorkload: END (committed=%lu/%lu) ===\n",
           I, (ULONG)T16_FILE_COUNT);
}
