/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     User-mode functional tests for ntfslx on D:\
 *
 * Self-contained -- no image prepopulation needed.
 * Tests volume info, free space, directory listing,
 * and file create/write/read cycle (when supported).
 */

#include <kmt_test.h>
#include <windows.h>

static BOOL FindNamedEntryInPattern(PCWSTR Pattern, PCWSTR Name, PWIN32_FIND_DATAW Match)
{
    WIN32_FIND_DATAW FindData;
    HANDLE hFind;
    BOOL Found = FALSE;

    hFind = FindFirstFileW(Pattern, &FindData);
    if (hFind == INVALID_HANDLE_VALUE)
        return FALSE;

    do
    {
        if (lstrcmpiW(FindData.cFileName, Name) == 0)
        {
            if (Match != NULL)
                *Match = FindData;
            Found = TRUE;
            break;
        }
    }
    while (FindNextFileW(hFind, &FindData));

    FindClose(hFind);
    return Found;
}

static void FillPattern(BYTE *Buffer, DWORD Size, BYTE Seed, BYTE Step)
{
    DWORD i;

    for (i = 0; i < Size; ++i)
        Buffer[i] = (BYTE)(Seed + (i * Step));
}

typedef struct _NTFSLX_VOLUME_SPACE_SNAPSHOT
{
    ULONGLONG TotalBytes;
    ULONGLONG FreeBytes;
    ULONGLONG FreeBytesAvailable;
    DWORD ClusterSize;
} NTFSLX_VOLUME_SPACE_SNAPSHOT, *PNTFSLX_VOLUME_SPACE_SNAPSHOT;

static BOOL QueryVolumeSpaceSnapshot(PCWSTR RootPath, PNTFSLX_VOLUME_SPACE_SNAPSHOT Snapshot)
{
    ULARGE_INTEGER FreeBytesAvailable;
    ULARGE_INTEGER TotalBytes;
    ULARGE_INTEGER TotalFreeBytes;
    DWORD SectorsPerCluster;
    DWORD BytesPerSector;
    DWORD FreeClusters;
    DWORD TotalClusters;
    BOOL Ok;

    memset(Snapshot, 0, sizeof(*Snapshot));

    Ok = GetDiskFreeSpaceExW(RootPath,
                             &FreeBytesAvailable,
                             &TotalBytes,
                             &TotalFreeBytes);
    if (!Ok)
    {
        return FALSE;
    }

    Ok = GetDiskFreeSpaceW(RootPath,
                           &SectorsPerCluster,
                           &BytesPerSector,
                           &FreeClusters,
                           &TotalClusters);
    if (!Ok)
    {
        return FALSE;
    }

    Snapshot->TotalBytes = TotalBytes.QuadPart;
    Snapshot->FreeBytes = TotalFreeBytes.QuadPart;
    Snapshot->FreeBytesAvailable = FreeBytesAvailable.QuadPart;
    Snapshot->ClusterSize = SectorsPerCluster * BytesPerSector;
    return TRUE;
}

static ULONGLONG ExpectedAllocationBytes(ULONGLONG FileSize, DWORD ClusterSize)
{
    if (FileSize == 0 || ClusterSize == 0)
        return 0;

    return ((FileSize + ClusterSize - 1) / ClusterSize) * ClusterSize;
}

static void TraceVolumeSpaceSnapshot(PCSTR Label, const NTFSLX_VOLUME_SPACE_SNAPSHOT *Snapshot)
{
    trace("%s: total=%I64u free=%I64u avail=%I64u cluster=%lu\n",
          Label,
          Snapshot->TotalBytes,
          Snapshot->FreeBytes,
          Snapshot->FreeBytesAvailable,
          Snapshot->ClusterSize);
}

static void ExpectFreeSpaceDelta(PCSTR Label,
                                 const NTFSLX_VOLUME_SPACE_SNAPSHOT *Baseline,
                                 const NTFSLX_VOLUME_SPACE_SNAPSHOT *Current,
                                 ULONGLONG ExpectedBytes)
{
    ULONGLONG ActualBytes = Baseline->FreeBytes - Current->FreeBytes;

    ok(ActualBytes == ExpectedBytes,
       "%s free-space delta is %I64u, expected %I64u (baseline free=%I64u current free=%I64u)\n",
       Label, ActualBytes, ExpectedBytes, Baseline->FreeBytes, Current->FreeBytes);
}

static void ExpectSameFreeSpace(PCSTR Label,
                                const NTFSLX_VOLUME_SPACE_SNAPSHOT *Expected,
                                const NTFSLX_VOLUME_SPACE_SNAPSHOT *Actual)
{
    ok(Actual->FreeBytes == Expected->FreeBytes,
       "%s free space changed unexpectedly: %I64u -> %I64u\n",
       Label, Expected->FreeBytes, Actual->FreeBytes);
}


static void TestVolumeInfo(void)
{
    WCHAR VolumeName[64];
    WCHAR FsName[32];
    DWORD Serial, MaxComp, Flags;
    BOOL Ok;

    Ok = GetVolumeInformationW(L"D:\\",
                               VolumeName, _countof(VolumeName),
                               &Serial, &MaxComp, &Flags,
                               FsName, _countof(FsName));
    if (!Ok)
    {
        skip(TRUE, "GetVolumeInformationW failed (error %lu) -- D: not mounted by ntfslx?\n",
             GetLastError());
        return;
    }

    ok(wcscmp(FsName, L"NTFS") == 0,
       "Expected fs name 'NTFS', got '%ls'\n", FsName);
    ok(MaxComp == 255, "Expected MaxComp 255, got %lu\n", MaxComp);
    ok(wcslen(VolumeName) > 0, "Volume label should not be empty\n");
    trace("Label='%ls' Serial=0x%08lX Flags=0x%08lX\n", VolumeName, Serial, Flags);
}

static void TestFreeSpace(void)
{
    NTFSLX_VOLUME_SPACE_SNAPSHOT Snapshot;
    BOOL Ok;

    Ok = QueryVolumeSpaceSnapshot(L"D:\\", &Snapshot);
    if (!Ok)
    {
        skip(TRUE, "QueryVolumeSpaceSnapshot failed (error %lu)\n", GetLastError());
        return;
    }

    ok(Snapshot.TotalBytes > 0, "TotalBytes should be > 0\n");
    ok(Snapshot.FreeBytes > 0, "TotalFreeBytes should be > 0 (got %I64u)\n",
       Snapshot.FreeBytes);
    ok(Snapshot.FreeBytes <= Snapshot.TotalBytes,
       "Free (%I64u) should be <= Total (%I64u)\n",
       Snapshot.FreeBytes, Snapshot.TotalBytes);
    ok(Snapshot.ClusterSize > 0, "ClusterSize should be > 0\n");
    TraceVolumeSpaceSnapshot("volume-space", &Snapshot);
}

static void TestDirListing(void)
{
    WIN32_FIND_DATAW FindData;
    HANDLE hFind;
    DWORD Count = 0;

    hFind = FindFirstFileW(L"D:\\*", &FindData);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        skip(TRUE, "FindFirstFile D:\\* failed (error %lu)\n", GetLastError());
        return;
    }

    do
    {
        Count++;
    }
    while (FindNextFileW(hFind, &FindData));

    FindClose(hFind);

    /* A fresh mkfs.ntfs image has system metadata dirs visible */
    ok(Count >= 1, "Expected at least 1 root entry, got %lu\n", Count);
    trace("Root entries seen: %lu\n", Count);
}

static void TestCreateWriteRead(void)
{
    HANDLE hFile;
    DWORD BytesWritten, BytesRead;
    BOOL Ok;
    BYTE WritePattern[4096];
    BYTE ReadBuffer[4096];
    DWORD i;
    DWORD Errors;
    LARGE_INTEGER FilePos;

    /* Fill write pattern: repeating 0x00..0xFF */
    for (i = 0; i < sizeof(WritePattern); i++)
        WritePattern[i] = (BYTE)(i % 256);

    /* Step 1: Create a new file */
    hFile = CreateFileW(L"D:\\ntfslx_rw_test.bin",
                        GENERIC_READ | GENERIC_WRITE,
                        0,
                        NULL,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        trace("CreateFile(CREATE_ALWAYS) failed with error %lu -- file creation not yet supported\n",
              GetLastError());
        skip(TRUE, "File creation not supported, skipping R/W tests\n");
        return;
    }

    /* Step 2: Write 4KB pattern */
    Ok = WriteFile(hFile, WritePattern, sizeof(WritePattern), &BytesWritten, NULL);
    ok(Ok, "WriteFile failed with error %lu\n", GetLastError());
    ok(BytesWritten == sizeof(WritePattern),
       "Expected %u bytes written, got %lu\n", (unsigned)sizeof(WritePattern), BytesWritten);

    /* Step 3: Seek back to start */
    FilePos.QuadPart = 0;
    Ok = SetFilePointerEx(hFile, FilePos, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx failed with error %lu\n", GetLastError());

    /* Step 4: Read back and compare */
    memset(ReadBuffer, 0xCC, sizeof(ReadBuffer));
    Ok = ReadFile(hFile, ReadBuffer, sizeof(ReadBuffer), &BytesRead, NULL);
    ok(Ok, "ReadFile failed with error %lu\n", GetLastError());
    ok(BytesRead == sizeof(WritePattern),
       "Expected %u bytes read, got %lu\n", (unsigned)sizeof(WritePattern), BytesRead);

    Errors = 0;
    for (i = 0; i < BytesRead; i++)
    {
        if (ReadBuffer[i] != WritePattern[i])
        {
            Errors++;
            if (Errors <= 3)
            {
                ok(0, "Mismatch at offset %lu: expected 0x%02X, got 0x%02X\n",
                   i, (unsigned)WritePattern[i], (unsigned)ReadBuffer[i]);
            }
        }
    }
    ok(Errors == 0, "Total read-back mismatches: %lu / %lu\n", Errors, BytesRead);

    CloseHandle(hFile);

    /* Step 5: Reopen and verify content persisted */
    hFile = CreateFileW(L"D:\\ntfslx_rw_test.bin",
                        GENERIC_READ,
                        FILE_SHARE_READ,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        ok(0, "Reopen failed with error %lu\n", GetLastError());
        return;
    }

    memset(ReadBuffer, 0xDD, sizeof(ReadBuffer));
    Ok = ReadFile(hFile, ReadBuffer, sizeof(ReadBuffer), &BytesRead, NULL);
    ok(Ok, "Reopen ReadFile failed with error %lu\n", GetLastError());
    ok(BytesRead == sizeof(WritePattern),
       "Reopen expected %u bytes, got %lu\n", (unsigned)sizeof(WritePattern), BytesRead);
    ok(memcmp(ReadBuffer, WritePattern, sizeof(WritePattern)) == 0,
       "Reopen content mismatch\n");

    CloseHandle(hFile);

    /* Step 6: Write bigger (16KB) to same file */
    hFile = CreateFileW(L"D:\\ntfslx_rw_test.bin",
                        GENERIC_READ | GENERIC_WRITE,
                        0,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        BYTE *BigBuf;
        DWORD BigSize = 16384;

        BigBuf = HeapAlloc(GetProcessHeap(), 0, BigSize);
        if (BigBuf != NULL)
        {
            for (i = 0; i < BigSize; i++)
                BigBuf[i] = (BYTE)((i * 7 + 13) % 256);

            Ok = WriteFile(hFile, BigBuf, BigSize, &BytesWritten, NULL);
            ok(Ok, "BigWrite failed with error %lu\n", GetLastError());
            ok(BytesWritten == BigSize,
               "BigWrite expected %lu bytes, got %lu\n", BigSize, BytesWritten);

            /* Seek and re-read */
            FilePos.QuadPart = 0;
            SetFilePointerEx(hFile, FilePos, NULL, FILE_BEGIN);

            {
                BYTE *ReadBig = HeapAlloc(GetProcessHeap(), 0, BigSize);
                if (ReadBig != NULL)
                {
                    Ok = ReadFile(hFile, ReadBig, BigSize, &BytesRead, NULL);
                    ok(Ok, "BigRead failed with error %lu\n", GetLastError());
                    ok(BytesRead == BigSize, "BigRead got %lu\n", BytesRead);
                    ok(memcmp(ReadBig, BigBuf, BigSize) == 0,
                       "BigRead content mismatch\n");
                    HeapFree(GetProcessHeap(), 0, ReadBig);
                }
            }

            HeapFree(GetProcessHeap(), 0, BigBuf);
        }
        CloseHandle(hFile);
    }

    /* Cleanup */
    DeleteFileW(L"D:\\ntfslx_rw_test.bin");
}

static void TestExplorerStyleSubdirListing(void)
{
    enum { FirstChunk = 4096, SecondChunk = 2048, TotalBytes = FirstChunk + SecondChunk };
    static const WCHAR DirPath[] = L"D:\\copy_subdir";
    static const WCHAR DirOpenPath[] = L"D:\\copy_subdir\\";
    static const WCHAR FilePath[] = L"D:\\copy_subdir\\copy.bin";
    static const WCHAR Pattern[] = L"D:\\copy_subdir\\*";
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hDir = INVALID_HANDLE_VALUE;
    BYTE ChunkA[FirstChunk];
    BYTE ChunkB[SecondChunk];
    BYTE ReadBuffer[TotalBytes];
    WIN32_FIND_DATAW FindData;
    LARGE_INTEGER FilePos;
    DWORD BytesWritten;
    DWORD BytesRead;
    DWORD Error;
    DWORD pass;
    BOOL Ok;
    BOOL Found;
    ULONGLONG Size;

    DeleteFileW(FilePath);
    RemoveDirectoryW(DirPath);

    Ok = CreateDirectoryW(DirPath, NULL);
    Error = GetLastError();
    ok(Ok || Error == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW failed with error %lu\n", Error);
    if (!Ok && Error != ERROR_ALREADY_EXISTS)
        return;

    memset(ChunkA, 0x11, sizeof(ChunkA));
    memset(ChunkB, 0x22, sizeof(ChunkB));
    memset(ReadBuffer, 0xCC, sizeof(ReadBuffer));

    hFile = CreateFileW(FilePath,
                        GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    ok(hFile != INVALID_HANDLE_VALUE,
       "CreateFileW(%ls) failed with error %lu\n", FilePath, GetLastError());
    if (hFile == INVALID_HANDLE_VALUE)
        goto Cleanup;

    Ok = WriteFile(hFile, ChunkA, sizeof(ChunkA), &BytesWritten, NULL);
    ok(Ok && BytesWritten == sizeof(ChunkA),
       "First chunk write failed: ok=%d bytes=%lu err=%lu\n",
       Ok, BytesWritten, GetLastError());

    Found = FindNamedEntryInPattern(Pattern, L"copy.bin", &FindData);
    ok(Found, "copy.bin missing after first chunk\n");
    if (Found)
    {
        Size = ((ULONGLONG)FindData.nFileSizeHigh << 32) | FindData.nFileSizeLow;
        ok(Size == FirstChunk,
           "copy.bin size after first chunk is %I64u, expected %u\n",
           Size, FirstChunk);
    }

    Ok = WriteFile(hFile, ChunkB, sizeof(ChunkB), &BytesWritten, NULL);
    ok(Ok && BytesWritten == sizeof(ChunkB),
       "Second chunk write failed: ok=%d bytes=%lu err=%lu\n",
       Ok, BytesWritten, GetLastError());

    Found = FindNamedEntryInPattern(Pattern, L"copy.bin", &FindData);
    ok(Found, "copy.bin missing after second chunk\n");
    if (Found)
    {
        Size = ((ULONGLONG)FindData.nFileSizeHigh << 32) | FindData.nFileSizeLow;
        ok(Size == TotalBytes,
           "copy.bin size after second chunk is %I64u, expected %u\n",
           Size, TotalBytes);
    }

    FilePos.QuadPart = 0;
    Ok = SetFilePointerEx(hFile, FilePos, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx failed with error %lu\n", GetLastError());

    Ok = ReadFile(hFile, ReadBuffer, sizeof(ReadBuffer), &BytesRead, NULL);
    ok(Ok && BytesRead == sizeof(ReadBuffer),
       "Read-back while handle open failed: ok=%d bytes=%lu err=%lu\n",
       Ok, BytesRead, GetLastError());
    ok(memcmp(ReadBuffer, ChunkA, sizeof(ChunkA)) == 0,
       "First chunk content mismatch before close\n");
    ok(memcmp(ReadBuffer + sizeof(ChunkA), ChunkB, sizeof(ChunkB)) == 0,
       "Second chunk content mismatch before close\n");

    CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;

    hDir = CreateFileW(DirOpenPath,
                       FILE_LIST_DIRECTORY | SYNCHRONIZE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS,
                       NULL);
    ok(hDir != INVALID_HANDLE_VALUE,
       "Open directory with trailing slash %ls failed with error %lu\n",
       DirOpenPath, GetLastError());
    if (hDir != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hDir);
        hDir = INVALID_HANDLE_VALUE;
    }

    for (pass = 0; pass < 2; ++pass)
    {
        Found = FindNamedEntryInPattern(Pattern, L"copy.bin", &FindData);
        ok(Found, "copy.bin missing on enumeration pass %lu\n", pass);
        if (Found)
        {
            Size = ((ULONGLONG)FindData.nFileSizeHigh << 32) | FindData.nFileSizeLow;
            ok(Size == TotalBytes,
               "copy.bin size on enumeration pass %lu is %I64u, expected %u\n",
               pass, Size, TotalBytes);
        }
    }

    hFile = CreateFileW(FilePath,
                        GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    ok(hFile != INVALID_HANDLE_VALUE,
       "Reopen %ls failed with error %lu\n", FilePath, GetLastError());
    if (hFile != INVALID_HANDLE_VALUE)
    {
        memset(ReadBuffer, 0xDD, sizeof(ReadBuffer));
        Ok = ReadFile(hFile, ReadBuffer, sizeof(ReadBuffer), &BytesRead, NULL);
        ok(Ok && BytesRead == sizeof(ReadBuffer),
           "Reopen read failed: ok=%d bytes=%lu err=%lu\n",
           Ok, BytesRead, GetLastError());
        ok(memcmp(ReadBuffer, ChunkA, sizeof(ChunkA)) == 0,
           "Reopen first chunk content mismatch\n");
        ok(memcmp(ReadBuffer + sizeof(ChunkA), ChunkB, sizeof(ChunkB)) == 0,
           "Reopen second chunk content mismatch\n");
        CloseHandle(hFile);
        hFile = INVALID_HANDLE_VALUE;
    }

Cleanup:
    if (hDir != INVALID_HANDLE_VALUE)
        CloseHandle(hDir);
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
    DeleteFileW(FilePath);
    RemoveDirectoryW(DirPath);
}


static void TestDirectoryChangeNotifications(void)
{
    static const WCHAR DirPath[] = L"D:\\notify_watch";
    static const WCHAR FilePath[] = L"D:\\notify_watch\\change.bin";
    HANDLE hNotify = INVALID_HANDLE_VALUE;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    BYTE Data[256];
    DWORD BytesWritten;
    DWORD WaitStatus;
    DWORD Error;
    BOOL Ok;

    DeleteFileW(FilePath);
    RemoveDirectoryW(DirPath);

    Ok = CreateDirectoryW(DirPath, NULL);
    Error = GetLastError();
    ok(Ok || Error == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(%ls) failed with error %lu\n",
       DirPath, Error);
    if (!Ok && Error != ERROR_ALREADY_EXISTS)
        return;

    hNotify = FindFirstChangeNotificationW(DirPath,
                                           FALSE,
                                           FILE_NOTIFY_CHANGE_FILE_NAME |
                                           FILE_NOTIFY_CHANGE_SIZE |
                                           FILE_NOTIFY_CHANGE_LAST_WRITE);
    if (hNotify == INVALID_HANDLE_VALUE)
    {
        skip(TRUE, "FindFirstChangeNotificationW failed with error %lu\n",
             GetLastError());
        goto Cleanup;
    }

    memset(Data, 0x5A, sizeof(Data));

    hFile = CreateFileW(FilePath,
                        GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    ok(hFile != INVALID_HANDLE_VALUE,
       "CreateFileW(%ls) failed with error %lu\n",
       FilePath, GetLastError());
    if (hFile == INVALID_HANDLE_VALUE)
        goto Cleanup;

    Ok = WriteFile(hFile, Data, sizeof(Data), &BytesWritten, NULL);
    ok(Ok && BytesWritten == sizeof(Data),
       "WriteFile(%ls) failed: ok=%d bytes=%lu err=%lu\n",
       FilePath, Ok, BytesWritten, GetLastError());
    CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;

    WaitStatus = WaitForSingleObject(hNotify, 2000);
    ok(WaitStatus == WAIT_OBJECT_0,
       "WaitForSingleObject(create/write) returned %lu\n",
       WaitStatus);

    Ok = FindNextChangeNotification(hNotify);
    ok(Ok, "FindNextChangeNotification failed with error %lu\n", GetLastError());

    Ok = DeleteFileW(FilePath);
    ok(Ok, "DeleteFileW(%ls) failed with error %lu\n",
       FilePath, GetLastError());

    WaitStatus = WaitForSingleObject(hNotify, 2000);
    ok(WaitStatus == WAIT_OBJECT_0,
       "WaitForSingleObject(delete) returned %lu\n",
       WaitStatus);

Cleanup:
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
    if (hNotify != INVALID_HANDLE_VALUE)
        FindCloseChangeNotification(hNotify);
    DeleteFileW(FilePath);
    RemoveDirectoryW(DirPath);
}

static void TestGapWriteZeroFill(void)
{
    enum { GapBytes = 8192, TailBytes = 1024, TotalBytes = GapBytes + TailBytes };
    static const WCHAR FilePath[] = L"D:\\gap_write.bin";
    HANDLE hFile = INVALID_HANDLE_VALUE;
    BYTE Tail[TailBytes];
    BYTE *ReadBuffer = NULL;
    LARGE_INTEGER FilePos;
    LARGE_INTEGER FileSize;
    DWORD BytesWritten;
    DWORD BytesRead;
    DWORD Errors = 0;
    DWORD i;
    BOOL Ok;

    DeleteFileW(FilePath);
    FillPattern(Tail, sizeof(Tail), 0x31, 7);

    ReadBuffer = HeapAlloc(GetProcessHeap(), 0, TotalBytes);
    ok(ReadBuffer != NULL, "HeapAlloc(%u) failed\n", TotalBytes);
    if (ReadBuffer == NULL)
        return;
    memset(ReadBuffer, 0xCC, TotalBytes);

    hFile = CreateFileW(FilePath,
                        GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    ok(hFile != INVALID_HANDLE_VALUE,
       "CreateFileW(%ls) failed with error %lu\n", FilePath, GetLastError());
    if (hFile == INVALID_HANDLE_VALUE)
        goto Cleanup;

    FilePos.QuadPart = GapBytes;
    Ok = SetFilePointerEx(hFile, FilePos, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx(%ls, %u) failed with error %lu\n",
       FilePath, GapBytes, GetLastError());

    Ok = WriteFile(hFile, Tail, sizeof(Tail), &BytesWritten, NULL);
    ok(Ok && BytesWritten == sizeof(Tail),
       "Gap write failed: ok=%d bytes=%lu err=%lu\n",
       Ok, BytesWritten, GetLastError());

    Ok = FlushFileBuffers(hFile);
    ok(Ok, "FlushFileBuffers(%ls) failed with error %lu\n",
       FilePath, GetLastError());

    Ok = GetFileSizeEx(hFile, &FileSize);
    ok(Ok && FileSize.QuadPart == TotalBytes,
       "Gap-write size mismatch: ok=%d size=%I64d expected=%u err=%lu\n",
       Ok, FileSize.QuadPart, TotalBytes, GetLastError());

    CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;

    hFile = CreateFileW(FilePath,
                        GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    ok(hFile != INVALID_HANDLE_VALUE,
       "Reopen %ls failed with error %lu\n", FilePath, GetLastError());
    if (hFile == INVALID_HANDLE_VALUE)
        goto Cleanup;

    Ok = ReadFile(hFile, ReadBuffer, TotalBytes, &BytesRead, NULL);
    ok(Ok && BytesRead == TotalBytes,
       "Gap read failed: ok=%d bytes=%lu expected=%u err=%lu\n",
       Ok, BytesRead, TotalBytes, GetLastError());

    for (i = 0; i < GapBytes && i < BytesRead; ++i)
    {
        if (ReadBuffer[i] != 0)
        {
            if (Errors < 3)
            {
                ok(0, "Gap byte %lu leaked stale value 0x%02X\n",
                   i, ReadBuffer[i]);
            }
            ++Errors;
        }
    }
    ok(Errors == 0, "Gap zero-fill mismatches: %lu\n", Errors);
    ok(BytesRead >= GapBytes + TailBytes &&
       memcmp(ReadBuffer + GapBytes, Tail, sizeof(Tail)) == 0,
       "Gap tail content mismatch\n");

Cleanup:
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
    HeapFree(GetProcessHeap(), 0, ReadBuffer);
    DeleteFileW(FilePath);
}

static void TestTruncateRewriteNoStaleData(void)
{
    enum { OriginalBytes = 4096, RewriteBytes = 512 };
    static const WCHAR FilePath[] = L"D:\\rewrite_small.bin";
    HANDLE hFile = INVALID_HANDLE_VALUE;
    BYTE Original[OriginalBytes];
    BYTE Rewrite[RewriteBytes];
    BYTE ReadBuffer[OriginalBytes];
    LARGE_INTEGER FileSize;
    DWORD BytesWritten;
    DWORD BytesRead;
    BOOL Ok;

    DeleteFileW(FilePath);
    FillPattern(Original, sizeof(Original), 0x41, 3);
    FillPattern(Rewrite, sizeof(Rewrite), 0xC1, 5);
    memset(ReadBuffer, 0xDD, sizeof(ReadBuffer));

    hFile = CreateFileW(FilePath,
                        GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    ok(hFile != INVALID_HANDLE_VALUE,
       "CreateFileW(%ls) failed with error %lu\n", FilePath, GetLastError());
    if (hFile == INVALID_HANDLE_VALUE)
        goto Cleanup;

    Ok = WriteFile(hFile, Original, sizeof(Original), &BytesWritten, NULL);
    ok(Ok && BytesWritten == sizeof(Original),
       "Initial write failed: ok=%d bytes=%lu err=%lu\n",
       Ok, BytesWritten, GetLastError());
    Ok = FlushFileBuffers(hFile);
    ok(Ok, "Initial FlushFileBuffers(%ls) failed with error %lu\n",
       FilePath, GetLastError());
    CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;

    hFile = CreateFileW(FilePath,
                        GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    ok(hFile != INVALID_HANDLE_VALUE,
       "Rewrite CreateFileW(%ls) failed with error %lu\n",
       FilePath, GetLastError());
    if (hFile == INVALID_HANDLE_VALUE)
        goto Cleanup;

    Ok = WriteFile(hFile, Rewrite, sizeof(Rewrite), &BytesWritten, NULL);
    ok(Ok && BytesWritten == sizeof(Rewrite),
       "Rewrite write failed: ok=%d bytes=%lu err=%lu\n",
       Ok, BytesWritten, GetLastError());
    Ok = FlushFileBuffers(hFile);
    ok(Ok, "Rewrite FlushFileBuffers(%ls) failed with error %lu\n",
       FilePath, GetLastError());
    Ok = GetFileSizeEx(hFile, &FileSize);
    ok(Ok && FileSize.QuadPart == RewriteBytes,
       "Rewrite size mismatch while open: ok=%d size=%I64d expected=%u err=%lu\n",
       Ok, FileSize.QuadPart, RewriteBytes, GetLastError());
    CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;

    hFile = CreateFileW(FilePath,
                        GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    ok(hFile != INVALID_HANDLE_VALUE,
       "Reopen %ls failed with error %lu\n", FilePath, GetLastError());
    if (hFile == INVALID_HANDLE_VALUE)
        goto Cleanup;

    Ok = GetFileSizeEx(hFile, &FileSize);
    ok(Ok && FileSize.QuadPart == RewriteBytes,
       "Rewrite size mismatch after reopen: ok=%d size=%I64d expected=%u err=%lu\n",
       Ok, FileSize.QuadPart, RewriteBytes, GetLastError());

    Ok = ReadFile(hFile, ReadBuffer, sizeof(ReadBuffer), &BytesRead, NULL);
    ok(Ok && BytesRead == RewriteBytes,
       "Rewrite read failed: ok=%d bytes=%lu expected=%u err=%lu\n",
       Ok, BytesRead, RewriteBytes, GetLastError());
    ok(memcmp(ReadBuffer, Rewrite, sizeof(Rewrite)) == 0,
       "Rewrite content mismatch\n");

Cleanup:
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
    DeleteFileW(FilePath);
}

static void TestVolumeSpaceAccountingSingleFile(void)
{
    static const WCHAR FilePath[] = L"D:\\space_accounting.bin";
    HANDLE hFile = INVALID_HANDLE_VALUE;
    NTFSLX_VOLUME_SPACE_SNAPSHOT Baseline;
    NTFSLX_VOLUME_SPACE_SNAPSHOT Snapshot;
    LARGE_INTEGER FileSize;
    BYTE *Buffer = NULL;
    LARGE_INTEGER FilePos;
    DWORD ClusterSize;
    DWORD BytesWritten;
    BOOL Ok;

    DeleteFileW(FilePath);

    Ok = QueryVolumeSpaceSnapshot(L"D:\\", &Baseline);
    if (!Ok)
    {
        skip(TRUE, "Single-file space test: QueryVolumeSpaceSnapshot failed (%lu)\n",
             GetLastError());
        return;
    }

    ClusterSize = Baseline.ClusterSize;
    Buffer = HeapAlloc(GetProcessHeap(), 0, ClusterSize * 3);
    ok(Buffer != NULL, "HeapAlloc(%lu) failed\n", ClusterSize * 3);
    if (Buffer == NULL)
        return;

    FillPattern(Buffer, ClusterSize * 3, 0x51, 11);
    TraceVolumeSpaceSnapshot("single baseline", &Baseline);

    hFile = CreateFileW(FilePath,
                        GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    ok(hFile != INVALID_HANDLE_VALUE,
       "CreateFileW(%ls) failed with error %lu\n", FilePath, GetLastError());
    if (hFile == INVALID_HANDLE_VALUE)
        goto Cleanup;

    Ok = GetFileSizeEx(hFile, &FileSize);
    ok(Ok, "GetFileSizeEx(%ls) failed with error %lu\n",
       FilePath, GetLastError());
    if (!Ok)
        goto Cleanup;

    ok(FileSize.QuadPart == 0,
       "Fresh file size is %I64d, expected 0\n",
       FileSize.QuadPart);

    Ok = WriteFile(hFile, Buffer, ClusterSize, &BytesWritten, NULL);
    ok(Ok && BytesWritten == ClusterSize,
       "First-cluster write failed: ok=%d bytes=%lu expected=%lu err=%lu\n",
       Ok, BytesWritten, ClusterSize, GetLastError());
    Ok = FlushFileBuffers(hFile);
    ok(Ok, "FlushFileBuffers(first cluster) failed with error %lu\n", GetLastError());

    Ok = GetFileSizeEx(hFile, &FileSize);
    ok(Ok, "GetFileSizeEx(first cluster) failed with error %lu\n",
       GetLastError());
    Ok = QueryVolumeSpaceSnapshot(L"D:\\", &Snapshot);
    ok(Ok, "QueryVolumeSpaceSnapshot(first cluster) failed with error %lu\n",
       GetLastError());
    if (!Ok)
        goto Cleanup;
    ExpectFreeSpaceDelta("single first cluster", &Baseline, &Snapshot,
                         ExpectedAllocationBytes((ULONGLONG)FileSize.QuadPart, ClusterSize));

    Ok = WriteFile(hFile, Buffer + ClusterSize, ClusterSize * 2, &BytesWritten, NULL);
    ok(Ok && BytesWritten == ClusterSize * 2,
       "Append write failed: ok=%d bytes=%lu expected=%lu err=%lu\n",
       Ok, BytesWritten, ClusterSize * 2, GetLastError());
    Ok = FlushFileBuffers(hFile);
    ok(Ok, "FlushFileBuffers(append) failed with error %lu\n", GetLastError());

    Ok = GetFileSizeEx(hFile, &FileSize);
    ok(Ok, "GetFileSizeEx(append) failed with error %lu\n",
       GetLastError());
    Ok = QueryVolumeSpaceSnapshot(L"D:\\", &Snapshot);
    ok(Ok, "QueryVolumeSpaceSnapshot(append) failed with error %lu\n",
       GetLastError());
    if (!Ok)
        goto Cleanup;
    ExpectFreeSpaceDelta("single three clusters", &Baseline, &Snapshot,
                         ExpectedAllocationBytes((ULONGLONG)FileSize.QuadPart, ClusterSize));

    FilePos.QuadPart = ClusterSize / 2;
    Ok = SetFilePointerEx(hFile, FilePos, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx(overwrite) failed with error %lu\n", GetLastError());

    Ok = WriteFile(hFile, Buffer, ClusterSize / 2, &BytesWritten, NULL);
    ok(Ok && BytesWritten == ClusterSize / 2,
       "Overwrite write failed: ok=%d bytes=%lu expected=%lu err=%lu\n",
       Ok, BytesWritten, ClusterSize / 2, GetLastError());
    Ok = FlushFileBuffers(hFile);
    ok(Ok, "FlushFileBuffers(overwrite) failed with error %lu\n", GetLastError());

    {
        NTFSLX_VOLUME_SPACE_SNAPSHOT OverwriteSnapshot;
        Ok = QueryVolumeSpaceSnapshot(L"D:\\", &OverwriteSnapshot);
        ok(Ok, "QueryVolumeSpaceSnapshot(overwrite) failed with error %lu\n",
           GetLastError());
        if (!Ok)
            goto Cleanup;
        ExpectSameFreeSpace("single overwrite", &Snapshot, &OverwriteSnapshot);
        Snapshot = OverwriteSnapshot;
    }

    FilePos.QuadPart = ClusterSize;
    Ok = SetFilePointerEx(hFile, FilePos, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx(truncate to one cluster) failed with error %lu\n",
       GetLastError());
    Ok = SetEndOfFile(hFile);
    ok(Ok, "SetEndOfFile(one cluster) failed with error %lu\n", GetLastError());
    Ok = FlushFileBuffers(hFile);
    ok(Ok, "FlushFileBuffers(one cluster) failed with error %lu\n", GetLastError());

    Ok = GetFileSizeEx(hFile, &FileSize);
    ok(Ok, "GetFileSizeEx(one cluster) failed with error %lu\n",
       GetLastError());
    Ok = QueryVolumeSpaceSnapshot(L"D:\\", &Snapshot);
    ok(Ok, "QueryVolumeSpaceSnapshot(one cluster) failed with error %lu\n",
       GetLastError());
    if (!Ok)
        goto Cleanup;
    ExpectFreeSpaceDelta("single truncate to one cluster", &Baseline, &Snapshot,
                         ExpectedAllocationBytes((ULONGLONG)FileSize.QuadPart, ClusterSize));

    FilePos.QuadPart = 0;
    Ok = SetFilePointerEx(hFile, FilePos, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx(truncate to zero) failed with error %lu\n", GetLastError());
    Ok = SetEndOfFile(hFile);
    ok(Ok, "SetEndOfFile(zero) failed with error %lu\n", GetLastError());
    Ok = FlushFileBuffers(hFile);
    ok(Ok, "FlushFileBuffers(zero) failed with error %lu\n", GetLastError());

    Ok = GetFileSizeEx(hFile, &FileSize);
    ok(Ok, "GetFileSizeEx(zero) failed with error %lu\n", GetLastError());
    if (Ok)
    {
        ok(ExpectedAllocationBytes((ULONGLONG)FileSize.QuadPart, ClusterSize) == 0,
           "Zero-truncated file allocation expectation is %I64u, expected 0\n",
           ExpectedAllocationBytes((ULONGLONG)FileSize.QuadPart, ClusterSize));
    }
    Ok = QueryVolumeSpaceSnapshot(L"D:\\", &Snapshot);
    ok(Ok, "QueryVolumeSpaceSnapshot(zero) failed with error %lu\n", GetLastError());
    if (!Ok)
        goto Cleanup;
    ExpectSameFreeSpace("single truncate to zero", &Baseline, &Snapshot);

Cleanup:
    if (hFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hFile);
        hFile = INVALID_HANDLE_VALUE;
    }

    DeleteFileW(FilePath);
    if (QueryVolumeSpaceSnapshot(L"D:\\", &Snapshot))
    {
        ExpectSameFreeSpace("single delete cleanup", &Baseline, &Snapshot);
    }

    if (Buffer != NULL)
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
    }
}

static void TestVolumeSpaceAccountingMultiFile(void)
{
    static const WCHAR DirPath[] = L"D:\\space_multi";
    static const WCHAR FileAPath[] = L"D:\\space_multi\\a.bin";
    static const WCHAR FileBPath[] = L"D:\\space_multi\\b.bin";
    static const WCHAR FileCPath[] = L"D:\\space_multi\\c.bin";
    HANDLE hA = INVALID_HANDLE_VALUE;
    HANDLE hB = INVALID_HANDLE_VALUE;
    HANDLE hC = INVALID_HANDLE_VALUE;
    NTFSLX_VOLUME_SPACE_SNAPSHOT Baseline;
    NTFSLX_VOLUME_SPACE_SNAPSHOT Snapshot;
    LARGE_INTEGER SizeA;
    LARGE_INTEGER SizeB;
    LARGE_INTEGER SizeC;
    BYTE *Buffer = NULL;
    DWORD ClusterSize;
    DWORD BytesWritten;
    BOOL Ok;
    LARGE_INTEGER FilePos;

    DeleteFileW(FileAPath);
    DeleteFileW(FileBPath);
    DeleteFileW(FileCPath);
    RemoveDirectoryW(DirPath);

    Ok = CreateDirectoryW(DirPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(%ls) failed with error %lu\n", DirPath, GetLastError());
    if (!Ok && GetLastError() != ERROR_ALREADY_EXISTS)
        return;

    Ok = QueryVolumeSpaceSnapshot(L"D:\\", &Baseline);
    if (!Ok)
    {
        skip(TRUE, "Multi-file space test: QueryVolumeSpaceSnapshot failed (%lu)\n",
             GetLastError());
        goto Cleanup;
    }

    ClusterSize = Baseline.ClusterSize;
    Buffer = HeapAlloc(GetProcessHeap(), 0, ClusterSize * 4);
    ok(Buffer != NULL, "HeapAlloc(%lu) failed\n", ClusterSize * 4);
    if (Buffer == NULL)
        goto Cleanup;

    FillPattern(Buffer, ClusterSize * 4, 0x27, 9);

    hA = CreateFileW(FileAPath, GENERIC_READ | GENERIC_WRITE,
                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                     NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    hB = CreateFileW(FileBPath, GENERIC_READ | GENERIC_WRITE,
                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                     NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    hC = CreateFileW(FileCPath, GENERIC_READ | GENERIC_WRITE,
                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                     NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(hA != INVALID_HANDLE_VALUE && hB != INVALID_HANDLE_VALUE && hC != INVALID_HANDLE_VALUE,
       "CreateFileW multi-file failed: a=%p b=%p c=%p err=%lu\n",
       hA, hB, hC, GetLastError());
    if (hA == INVALID_HANDLE_VALUE || hB == INVALID_HANDLE_VALUE || hC == INVALID_HANDLE_VALUE)
        goto Cleanup;

    Ok = WriteFile(hA, Buffer, ClusterSize, &BytesWritten, NULL);
    ok(Ok && BytesWritten == ClusterSize, "WriteFile(a.bin) failed\n");
    Ok = WriteFile(hB, Buffer, ClusterSize * 2, &BytesWritten, NULL);
    ok(Ok && BytesWritten == ClusterSize * 2, "WriteFile(b.bin) failed\n");
    Ok = WriteFile(hC, Buffer, ClusterSize * 4, &BytesWritten, NULL);
    ok(Ok && BytesWritten == ClusterSize * 4, "WriteFile(c.bin) failed\n");
    Ok = FlushFileBuffers(hA);
    ok(Ok, "FlushFileBuffers(a.bin) failed with error %lu\n", GetLastError());
    Ok = FlushFileBuffers(hB);
    ok(Ok, "FlushFileBuffers(b.bin) failed with error %lu\n", GetLastError());
    Ok = FlushFileBuffers(hC);
    ok(Ok, "FlushFileBuffers(c.bin) failed with error %lu\n", GetLastError());

    Ok = GetFileSizeEx(hA, &SizeA);
    ok(Ok, "GetFileSizeEx(a.bin) failed with error %lu\n", GetLastError());
    Ok = GetFileSizeEx(hB, &SizeB);
    ok(Ok, "GetFileSizeEx(b.bin) failed with error %lu\n", GetLastError());
    Ok = GetFileSizeEx(hC, &SizeC);
    ok(Ok, "GetFileSizeEx(c.bin) failed with error %lu\n", GetLastError());

    Ok = QueryVolumeSpaceSnapshot(L"D:\\", &Snapshot);
    ok(Ok, "QueryVolumeSpaceSnapshot(multi initial) failed with error %lu\n", GetLastError());
    if (!Ok)
        goto Cleanup;
    ExpectFreeSpaceDelta("multi initial", &Baseline, &Snapshot,
                         ExpectedAllocationBytes((ULONGLONG)SizeA.QuadPart, ClusterSize) +
                         ExpectedAllocationBytes((ULONGLONG)SizeB.QuadPart, ClusterSize) +
                         ExpectedAllocationBytes((ULONGLONG)SizeC.QuadPart, ClusterSize));

    FilePos.QuadPart = ClusterSize / 4;
    Ok = SetFilePointerEx(hB, FilePos, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx(b.bin overwrite) failed with error %lu\n", GetLastError());
    Ok = WriteFile(hB, Buffer, ClusterSize / 2, &BytesWritten, NULL);
    ok(Ok && BytesWritten == ClusterSize / 2,
       "WriteFile(b.bin overwrite) failed: ok=%d bytes=%lu expected=%lu err=%lu\n",
       Ok, BytesWritten, ClusterSize / 2, GetLastError());
    Ok = FlushFileBuffers(hB);
    ok(Ok, "FlushFileBuffers(b.bin overwrite) failed with error %lu\n", GetLastError());

    {
        NTFSLX_VOLUME_SPACE_SNAPSHOT OverwriteSnapshot;
        Ok = QueryVolumeSpaceSnapshot(L"D:\\", &OverwriteSnapshot);
        ok(Ok, "QueryVolumeSpaceSnapshot(multi overwrite) failed with error %lu\n",
           GetLastError());
        if (!Ok)
            goto Cleanup;
        ExpectSameFreeSpace("multi overwrite", &Snapshot, &OverwriteSnapshot);
        Snapshot = OverwriteSnapshot;
    }

    CloseHandle(hB);
    hB = INVALID_HANDLE_VALUE;
    Ok = DeleteFileW(FileBPath);
    ok(Ok, "DeleteFileW(%ls) failed with error %lu\n", FileBPath, GetLastError());
    Ok = QueryVolumeSpaceSnapshot(L"D:\\", &Snapshot);
    ok(Ok, "QueryVolumeSpaceSnapshot(after delete b) failed with error %lu\n",
       GetLastError());
    if (!Ok)
        goto Cleanup;
    ExpectFreeSpaceDelta("multi after delete b", &Baseline, &Snapshot,
                         ExpectedAllocationBytes((ULONGLONG)SizeA.QuadPart, ClusterSize) +
                         ExpectedAllocationBytes((ULONGLONG)SizeC.QuadPart, ClusterSize));

    FilePos.QuadPart = ClusterSize;
    Ok = SetFilePointerEx(hC, FilePos, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx(c.bin truncate) failed with error %lu\n", GetLastError());
    Ok = SetEndOfFile(hC);
    ok(Ok, "SetEndOfFile(c.bin) failed with error %lu\n", GetLastError());
    Ok = FlushFileBuffers(hC);
    ok(Ok, "FlushFileBuffers(c.bin truncate) failed with error %lu\n", GetLastError());
    Ok = GetFileSizeEx(hC, &SizeC);
    ok(Ok, "GetFileSizeEx(c.bin truncate) failed with error %lu\n",
       GetLastError());
    Ok = QueryVolumeSpaceSnapshot(L"D:\\", &Snapshot);
    ok(Ok, "QueryVolumeSpaceSnapshot(after truncate c) failed with error %lu\n",
       GetLastError());
    if (!Ok)
        goto Cleanup;
    ExpectFreeSpaceDelta("multi after truncate c", &Baseline, &Snapshot,
                         ExpectedAllocationBytes((ULONGLONG)SizeA.QuadPart, ClusterSize) +
                         ExpectedAllocationBytes((ULONGLONG)SizeC.QuadPart, ClusterSize));

Cleanup:
    if (hA != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hA);
        hA = INVALID_HANDLE_VALUE;
    }
    if (hB != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hB);
        hB = INVALID_HANDLE_VALUE;
    }
    if (hC != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hC);
        hC = INVALID_HANDLE_VALUE;
    }

    DeleteFileW(FileAPath);
    DeleteFileW(FileBPath);
    DeleteFileW(FileCPath);
    RemoveDirectoryW(DirPath);

    if (QueryVolumeSpaceSnapshot(L"D:\\", &Snapshot))
    {
        ExpectSameFreeSpace("multi cleanup", &Baseline, &Snapshot);
    }

    if (Buffer != NULL)
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
    }
}

static void TestDirectoryChangeNotificationLoop(void)
{
    /* Higher counts currently reproduce a process-quota leak on shutdown;
     * keep the autorun pressure below the point where the whole VM drops
     * into the debugger, and treat the larger count as a manual stress case. */
    enum { Iterations = 4 };
    static const WCHAR DirPath[] = L"D:\\notify_loop";
    HANDLE hNotify = INVALID_HANDLE_VALUE;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    WCHAR FilePath[MAX_PATH];
    BYTE Data = 0xA5;
    DWORD BytesWritten;
    DWORD WaitStatus;
    DWORD Error;
    DWORD It;
    BOOL Ok;

    RemoveDirectoryW(DirPath);
    Ok = CreateDirectoryW(DirPath, NULL);
    Error = GetLastError();
    ok(Ok || Error == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(%ls) failed with error %lu\n",
       DirPath, Error);
    if (!Ok && Error != ERROR_ALREADY_EXISTS)
        return;

    for (It = 0; It < Iterations; ++It)
    {
        swprintf(FilePath, L"%ls\\iter_%02lu.bin", DirPath, It);
        DeleteFileW(FilePath);

        hNotify = FindFirstChangeNotificationW(DirPath,
                                               FALSE,
                                               FILE_NOTIFY_CHANGE_FILE_NAME |
                                               FILE_NOTIFY_CHANGE_SIZE |
                                               FILE_NOTIFY_CHANGE_LAST_WRITE);
        ok(hNotify != INVALID_HANDLE_VALUE,
           "FindFirstChangeNotificationW iter=%lu failed with error %lu\n",
           It, GetLastError());
        if (hNotify == INVALID_HANDLE_VALUE)
            break;

        hFile = CreateFileW(FilePath,
                            GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL,
                            CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);
        ok(hFile != INVALID_HANDLE_VALUE,
           "CreateFileW(%ls) iter=%lu failed with error %lu\n",
           FilePath, It, GetLastError());
        if (hFile == INVALID_HANDLE_VALUE)
            goto IterCleanup;

        Ok = WriteFile(hFile, &Data, sizeof(Data), &BytesWritten, NULL);
        ok(Ok && BytesWritten == sizeof(Data),
           "WriteFile(%ls) iter=%lu failed: ok=%d bytes=%lu err=%lu\n",
           FilePath, It, Ok, BytesWritten, GetLastError());
        Ok = FlushFileBuffers(hFile);
        ok(Ok, "FlushFileBuffers(%ls) iter=%lu failed with error %lu\n",
           FilePath, It, GetLastError());
        CloseHandle(hFile);
        hFile = INVALID_HANDLE_VALUE;

        WaitStatus = WaitForSingleObject(hNotify, 2000);
        ok(WaitStatus == WAIT_OBJECT_0,
           "Notify create/write iter=%lu returned %lu\n",
           It, WaitStatus);

        Ok = FindNextChangeNotification(hNotify);
        ok(Ok, "FindNextChangeNotification iter=%lu failed with error %lu\n",
           It, GetLastError());

        Ok = DeleteFileW(FilePath);
        ok(Ok, "DeleteFileW(%ls) iter=%lu failed with error %lu\n",
           FilePath, It, GetLastError());

        WaitStatus = WaitForSingleObject(hNotify, 2000);
        ok(WaitStatus == WAIT_OBJECT_0,
           "Notify delete iter=%lu returned %lu\n",
           It, WaitStatus);

IterCleanup:
        if (hFile != INVALID_HANDLE_VALUE)
        {
            CloseHandle(hFile);
            hFile = INVALID_HANDLE_VALUE;
        }
        if (hNotify != INVALID_HANDLE_VALUE)
        {
            FindCloseChangeNotification(hNotify);
            hNotify = INVALID_HANDLE_VALUE;
        }
        DeleteFileW(FilePath);
    }

    Ok = RemoveDirectoryW(DirPath);
    ok(Ok, "RemoveDirectoryW(%ls) failed with error %lu\n",
       DirPath, GetLastError());
}

static void TestSectionMapping(void)
{
    HANDLE hFile, hMapping;
    PVOID View;
    DWORD BytesWritten;
    BOOL Ok;
    BYTE TestData[256];
    DWORD i;

    for (i = 0; i < sizeof(TestData); i++)
        TestData[i] = (BYTE)(i ^ 0xAA);

    /* Create file for section test */
    hFile = CreateFileW(L"D:\\ntfslx_section.bin",
                        GENERIC_READ | GENERIC_WRITE,
                        0,
                        NULL,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        skip(TRUE, "Section test: file creation not supported (error %lu)\n",
             GetLastError());
        return;
    }

    Ok = WriteFile(hFile, TestData, sizeof(TestData), &BytesWritten, NULL);
    ok(Ok && BytesWritten == sizeof(TestData), "Section write failed\n");

    /* Create read-only section */
    hMapping = CreateFileMappingW(hFile, NULL, PAGE_READONLY,
                                  0, sizeof(TestData), NULL);
    if (hMapping == NULL)
    {
        trace("CreateFileMapping failed with error %lu -- sections not yet supported\n",
              GetLastError());
        skip(TRUE, "Section creation not supported\n");
        CloseHandle(hFile);
        DeleteFileW(L"D:\\ntfslx_section.bin");
        return;
    }

    View = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, sizeof(TestData));
    ok(View != NULL, "MapViewOfFile failed with error %lu\n", GetLastError());
    if (View != NULL)
    {
        ok(memcmp(View, TestData, sizeof(TestData)) == 0,
           "Section content mismatch\n");
        UnmapViewOfFile(View);
    }

    CloseHandle(hMapping);
    CloseHandle(hFile);
    DeleteFileW(L"D:\\ntfslx_section.bin");
}

static void RunKernelPhases(void)
{
    static const char * const Phases[] = {
        "NtfslxFuncSmoke",
        "NtfslxFuncStressA",
        "NtfslxFuncStressB",
        "NtfslxFuncStressC",
    };
    DWORD Error;
    UINT i;

    for (i = 0; i < _countof(Phases); ++i)
    {
        trace("NTFSLX-HARNESS: running %s\n", Phases[i]);
        Error = KmtRunKernelTest(Phases[i]);
        ok(Error == ERROR_SUCCESS,
           "KmtRunKernelTest(%s) failed with error %lu\n",
           Phases[i], Error);
        if (Error != ERROR_SUCCESS)
        {
            trace("NTFSLX-HARNESS: aborting after %s\n", Phases[i]);
            return;
        }
    }

    trace("NTFSLX-HARNESS: all phases complete\n");
}

START_TEST(NtfslxFunc)
{
    TestVolumeInfo();
    TestFreeSpace();
    TestDirListing();
    TestCreateWriteRead();
    TestExplorerStyleSubdirListing();
    TestDirectoryChangeNotifications();
    TestGapWriteZeroFill();
    TestTruncateRewriteNoStaleData();
    TestVolumeSpaceAccountingSingleFile();
    TestVolumeSpaceAccountingMultiFile();
    TestDirectoryChangeNotificationLoop();
    TestSectionMapping();
    RunKernelPhases();
}
