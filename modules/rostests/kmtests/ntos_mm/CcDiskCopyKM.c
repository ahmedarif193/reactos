/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Cc cached-copy stress test between two FAT32 data disks.
 *
 * Copies files from a source FAT32 disk to a dest FAT32 disk using buffered
 * (cached) ZwReadFile/ZwWriteFile, so the data flows through the cache manager
 * (CcCopyRead/CcCopyWrite) and the Mm write-back path. Each dest file is then
 * re-opened and re-read (after the copy handle is closed, which flushes the
 * cache to disk) and a rolling checksum is compared to the source - so a Cc/Mm
 * write-back corruption shows up as a checksum mismatch.
 *
 * The disks are reached by device path (\Device\HarddiskN\Partition1\...), so
 * this needs no drive letter and runs entirely in the kmtest harness, bypassing
 * the GUI shell. Source carries \SRCMARK.TAG + \SRC\*.bin; dest carries
 * \DSTMARK.TAG + an (empty) \DST directory.
 */

#include <kmt_test.h>
#include <ntstrsafe.h>

#define TAG_CCC 'CccC'
#define CHUNK (1024 * 1024)

static
ULONG64
ChecksumUpdate(ULONG64 Sum, PUCHAR Buf, ULONG Len)
{
    ULONG i;
    for (i = 0; i < Len; i++)
        Sum = Sum * 1000003ULL + Buf[i];
    return Sum;
}

static
NTSTATUS
OpenDiskFile(
    _In_ PCWSTR Path,
    _In_ ACCESS_MASK Access,
    _In_ ULONG Disposition,
    _In_ ULONG Options,
    _Out_ PHANDLE Handle)
{
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;

    *Handle = NULL;
    RtlInitUnicodeString(&name, Path);
    InitializeObjectAttributes(&oa, &name,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    /* FILE_SYNCHRONOUS_IO_NONALERT: make ZwReadFile/ZwWriteFile block until the
     * I/O completes and return the final status, instead of STATUS_PENDING (the
     * handle then tracks position; required for synchronous Zw file I/O). */
    return ZwCreateFile(Handle, Access, &oa, &iosb, NULL, FILE_ATTRIBUTE_NORMAL,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, Disposition,
                        Options | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
}

/* Find which \Device\HarddiskN\Partition1 holds the root file <Marker>. */
static
BOOLEAN
FindDisk(
    _In_ PCWSTR Marker,
    _Out_writes_(PrefixCch) PWCHAR PrefixBuf,
    _In_ SIZE_T PrefixCch)
{
    WCHAR path[160];
    ULONG n;

    for (n = 1; n <= 6; n++)
    {
        HANDLE h;
        RtlStringCchPrintfW(path, RTL_NUMBER_OF(path),
                            L"\\Device\\Harddisk%lu\\Partition1\\%ls", n, Marker);
        if (NT_SUCCESS(OpenDiskFile(path, FILE_READ_DATA, FILE_OPEN,
                                    FILE_NON_DIRECTORY_FILE, &h)))
        {
            ZwClose(h);
            RtlStringCchPrintfW(PrefixBuf, PrefixCch,
                                L"\\Device\\Harddisk%lu\\Partition1", n);
            return TRUE;
        }
    }
    return FALSE;
}

/* Cached copy SrcPath -> DstPath; returns rolling checksum of the source bytes
 * in *SrcSum and the byte count in *Bytes. */
static
NTSTATUS
CopyFileCached(
    _In_ PCWSTR SrcPath,
    _In_ PCWSTR DstPath,
    _In_ PVOID Buf,
    _Out_ PULONG64 SrcSum,
    _Out_ PLARGE_INTEGER Bytes)
{
    HANDLE hs = NULL, hd = NULL;
    NTSTATUS st;
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER off;
    ULONG64 sum = 0;

    *SrcSum = 0;
    Bytes->QuadPart = 0;

    st = OpenDiskFile(SrcPath, FILE_READ_DATA, FILE_OPEN,
                      FILE_NON_DIRECTORY_FILE | FILE_SEQUENTIAL_ONLY, &hs);
    if (!NT_SUCCESS(st))
        goto done;

    /* DIAG: report what fastfat thinks the source size is (0 == size-read bug). */
    {
        FILE_STANDARD_INFORMATION fsi;
        IO_STATUS_BLOCK qio;
        NTSTATUS qst;
        RtlZeroMemory(&fsi, sizeof(fsi));
        qst = ZwQueryInformationFile(hs, &qio, &fsi, sizeof(fsi), FileStandardInformation);
        trace("  SRC size: qst=0x%08lx EndOfFile=%I64d AllocSize=%I64d\n",
              qst, fsi.EndOfFile.QuadPart, fsi.AllocationSize.QuadPart);
    }

    st = OpenDiskFile(DstPath, FILE_WRITE_DATA, FILE_OVERWRITE_IF,
                      FILE_NON_DIRECTORY_FILE | FILE_SEQUENTIAL_ONLY, &hd);
    if (!NT_SUCCESS(st))
        goto done;

    off.QuadPart = 0;
    for (;;)
    {
        ULONG got;
        st = ZwReadFile(hs, NULL, NULL, NULL, &iosb, Buf, CHUNK, &off, NULL);
        if (off.QuadPart == 0)
            trace("  first ZwReadFile: st=0x%08lx info=%Iu\n", st, (ULONG_PTR)iosb.Information);
        if (st == STATUS_END_OF_FILE) { st = STATUS_SUCCESS; break; }
        if (!NT_SUCCESS(st)) break;
        got = (ULONG)iosb.Information;
        if (got == 0) break;

        sum = ChecksumUpdate(sum, Buf, got);

        st = ZwWriteFile(hd, NULL, NULL, NULL, &iosb, Buf, got, &off, NULL);
        if (!NT_SUCCESS(st)) break;

        off.QuadPart += got;
        if (got < CHUNK) break;   /* short read == EOF */
    }
    Bytes->QuadPart = off.QuadPart;

done:
    if (hs) ZwClose(hs);
    if (hd)
    {
        /* Force the cached writes to disk synchronously so the copy persists
         * immediately (lazy-writer flush may not finish before the VM stops),
         * then close. */
        IO_STATUS_BLOCK fiosb;
        ZwFlushBuffersFile(hd, &fiosb);
        ZwClose(hd);
    }
    *SrcSum = sum;
    return st;
}

/* Re-read a file from disk and return its rolling checksum. */
static
NTSTATUS
ChecksumFile(
    _In_ PCWSTR Path,
    _In_ PVOID Buf,
    _Out_ PULONG64 Sum)
{
    HANDLE h = NULL;
    NTSTATUS st;
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER off;
    ULONG64 sum = 0;

    *Sum = 0;
    st = OpenDiskFile(Path, FILE_READ_DATA, FILE_OPEN,
                      FILE_NON_DIRECTORY_FILE | FILE_SEQUENTIAL_ONLY, &h);
    if (!NT_SUCCESS(st))
        return st;

    off.QuadPart = 0;
    for (;;)
    {
        ULONG got;
        st = ZwReadFile(h, NULL, NULL, NULL, &iosb, Buf, CHUNK, &off, NULL);
        if (st == STATUS_END_OF_FILE) { st = STATUS_SUCCESS; break; }
        if (!NT_SUCCESS(st)) break;
        got = (ULONG)iosb.Information;
        if (got == 0) break;
        sum = ChecksumUpdate(sum, Buf, got);
        off.QuadPart += got;
        if (got < CHUNK) break;
    }
    ZwClose(h);
    *Sum = sum;
    return st;
}

START_TEST(CcDiskCopyKM)
{
    WCHAR srcPfx[48], dstPfx[48], sp[200], dp[200], name[40];
    PVOID buf;
    ULONG i;
    LARGE_INTEGER total = { { 0, 0 } };

    trace("CcDiskCopyKM: cached copy between FAT32 data disks (device paths)\n");

    buf = ExAllocatePoolWithTag(NonPagedPool, CHUNK, TAG_CCC);
    if (!ok(buf != NULL, "chunk buffer alloc failed\n"))
        return;

    if (!ok(FindDisk(L"SRCMARK.TAG", srcPfx, RTL_NUMBER_OF(srcPfx)), "source disk not found\n") ||
        !ok(FindDisk(L"DSTMARK.TAG", dstPfx, RTL_NUMBER_OF(dstPfx)), "dest disk not found\n"))
    {
        ExFreePoolWithTag(buf, TAG_CCC);
        return;
    }
    trace("src=%ls dst=%ls\n", srcPfx, dstPfx);

    /* Copy aaa_small.bin (if present), then big1.bin, big2.bin, ... until a
     * source file is missing - so the same test scales to any data-set size
     * (e.g. 20 x 1GB == a 20GB straight copy) without recompiling. */
    for (i = 0; i <= 128; i++)
    {
        ULONG64 srcSum = 0, dstSum = 0;
        LARGE_INTEGER bytes = { { 0, 0 } };
        NTSTATUS st;

        if (i == 0)
            RtlStringCchCopyW(name, RTL_NUMBER_OF(name), L"aaa_small.bin");
        else
            RtlStringCchPrintfW(name, RTL_NUMBER_OF(name), L"big%lu.bin", i);

        RtlStringCchPrintfW(sp, RTL_NUMBER_OF(sp), L"%ls\\SRC\\%ls", srcPfx, name);
        RtlStringCchPrintfW(dp, RTL_NUMBER_OF(dp), L"%ls\\DST\\%ls", dstPfx, name);

        st = CopyFileCached(sp, dp, buf, &srcSum, &bytes);
        if (st == STATUS_OBJECT_NAME_NOT_FOUND || st == STATUS_OBJECT_PATH_NOT_FOUND)
        {
            if (i == 0)
                continue;   /* no aaa_small.bin - fine, start at big1 */
            break;          /* end of the big%lu set */
        }
        if (!ok(NT_SUCCESS(st), "[%ls] copy failed 0x%08lx\n", name, st))
            continue;

        st = ChecksumFile(dp, buf, &dstSum);
        ok(NT_SUCCESS(st), "[%ls] dest re-read failed 0x%08lx\n", name, st);
        ok(srcSum == dstSum,
           "[%ls] CHECKSUM MISMATCH src=%I64x dst=%I64x (%I64d bytes)\n",
           name, srcSum, dstSum, bytes.QuadPart);
        total.QuadPart += bytes.QuadPart;
        trace("[%ls] done %I64d bytes sum=%I64x (total %I64d MB)\n",
              name, bytes.QuadPart, srcSum, total.QuadPart / (1024 * 1024));
    }

    trace("CcDiskCopyKM: total copied %I64d bytes (%I64d MB)\n",
          total.QuadPart, total.QuadPart / (1024 * 1024));
    ExFreePoolWithTag(buf, TAG_CCC);
}
